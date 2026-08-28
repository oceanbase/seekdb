/*
 * Copyright (c) 2025 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define USING_LOG_PREFIX SHARE

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "share/ob_autoincrement_service.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_sql_client_decorator.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_schema_utils.h"
#include "share/schema/ob_table_schema.h"
#include "lib/wait_event/ob_inner_sql_wait_type.h"

using namespace oceanbase::common;
using namespace oceanbase::common::hash;
using namespace oceanbase::common::sqlclient;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace share
{
#ifndef INT24_MIN
#define INT24_MIN     (-8388607 - 1)
#endif
#ifndef INT24_MAX
#define INT24_MAX     (8388607)
#endif
#ifndef UINT24_MAX
#define UINT24_MAX    (16777215U)
#endif

int CacheNode::combine_cache_node(CacheNode &new_node)
{
  int ret = OB_SUCCESS;
  if (new_node.cache_start_ > 0) {
    if (cache_end_ < cache_start_
        || new_node.cache_end_ < new_node.cache_start_
        || cache_end_ > new_node.cache_start_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("cache node is invalid", K(*this), K(new_node), K(ret));
    } else {
      if (cache_end_ > 0 && cache_end_ == new_node.cache_start_ - 1) {
        cache_end_ = new_node.cache_end_;
      } else {
        cache_start_ = new_node.cache_start_;
        cache_end_ = new_node.cache_end_;
      }
      new_node.reset();
    }
  }
  return ret;
}

int TableNode::alloc_handle(ObSmallAllocator &allocator,
                            const uint64_t offset,
                            const uint64_t increment,
                            const uint64_t desired_count,
                            const uint64_t max_value,
                            CacheHandle *&handle,
                            const bool is_retry_alloc)
{
  int ret = OB_SUCCESS;
  CacheNode node = curr_node_;
  uint64_t min_value = 0;
  const uint64_t local_sync = ATOMIC_LOAD(&local_sync_);
  if (local_sync < next_value_) {
    min_value = next_value_;
  } else {
    if (local_sync >= max_value) {
      min_value = max_value;
    } else {
      min_value = local_sync + 1;
    }
  }
  if (min_value < node.cache_start_) {
    min_value = node.cache_start_;
  }
  uint64_t new_next_value = 0;
  uint64_t needed_interval = 0;
  if (curr_node_state_is_pending_) {
    ret = OB_SIZE_OVERFLOW;
  } else if (min_value >= max_value) {
    new_next_value = max_value;
    needed_interval = max_value;
  } else if (min_value > node.cache_end_) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    ret = ObAutoincrementService::calc_next_value(min_value,
                                                  offset,
                                                  increment,
                                                  new_next_value);
    if (OB_FAIL(ret)) {
    } else {
      bool reach_upper_limit = false;
      if (max_value == node.cache_end_) {
        // No larger cache range is available.
        reach_upper_limit = true;
      }
      if (new_next_value > node.cache_end_) {
        if (reach_upper_limit) {
          new_next_value = max_value;
          needed_interval = max_value;
        } else {
          ret = OB_SIZE_OVERFLOW;
          LOG_WARN("fail to alloc handle; cache is not enough",
                   K(node), K(min_value), K(offset), K(increment), K(max_value), K(new_next_value),
                   K(*this), K(ret));
        }
      } else {
        needed_interval = new_next_value + increment * (desired_count - 1);
        // check overflow
        if (needed_interval < new_next_value) {
          needed_interval = UINT64_MAX;
        }
        if (needed_interval > node.cache_end_) {
          if (reach_upper_limit) {
            needed_interval = max_value;
          } else {
            ret = OB_SIZE_OVERFLOW;
            // don't print warn log for common buffer burnout case, as we will fetch next buffer
            if (is_retry_alloc) {
              LOG_WARN("fail to alloc handle; cache is not enough", K(*this), K(ret));
            }
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    handle = static_cast<CacheHandle *>(allocator.alloc());
    if (NULL == handle) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("failed to alloc cache handle", K(ret));
    } else {
      if (UINT64_MAX == needed_interval) {
        // compatible with MySQL; return error when reach UINT64_MAX
        ret = OB_ERR_REACH_AUTOINC_MAX;
        LOG_WARN("reach UINT64_MAX", K(ret));
      } else {
        handle = new (handle) CacheHandle;
        handle->offset_ = offset;
        handle->increment_ = increment;
        handle->next_value_ = new_next_value;
        handle->prefetch_start_ = new_next_value;
        handle->prefetch_end_ = needed_interval;
        handle->max_value_ = max_value;
        // Consume the same raw integer interval as the former ORDER service.
        // min_value is deliberately not aligned to offset/increment here:
        // the next statement may use different session values and will align
        // the first still-unallocated integer through calc_next_value().
        if (min_value >= max_value
            || 0 == increment
            || desired_count > (max_value - min_value) / increment) {
          next_value_ = max_value;
        } else {
          next_value_ = min_value + increment * desired_count;
        }
      }
    }
  } else if (OB_SIZE_OVERFLOW == ret) {
    if (prefetch_node_.cache_start_ > 0) {
      if (OB_FAIL(curr_node_.combine_cache_node(prefetch_node_))) {
      } else if (OB_FAIL(alloc_handle(allocator, offset, increment, desired_count, max_value, handle))) {
      }
    }
  } else {
    LOG_WARN("unexpected error", K(ret));
  }

  return ret;
}

int TableNode::init(int64_t autoinc_table_part_num)
{
  UNUSED(autoinc_table_part_num);
  return OB_SUCCESS;
}

int CacheHandle::next_value(uint64_t &next_value)
{
  int ret = OB_SUCCESS;
  if (last_row_dup_flag_ && 0 != last_value_to_confirm_) {
    // use last auto-increment value
    next_value = last_value_to_confirm_;
    last_row_dup_flag_ = false;
  } else {
    if (next_value_ >= max_value_) {
      if (OB_FAIL(ObAutoincrementService::calc_prev_value(max_value_, offset_, increment_, next_value))) {
      }
    } else {
      if (next_value_ > prefetch_end_) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        next_value = next_value_;
        // for insert...on duplicate key update
        // if last row is duplicate, auto-increment value should be used in next row
        last_value_to_confirm_ = next_value_;
        last_row_dup_flag_ = false;

        next_value_ += increment_;
      }
    }
  }
  return ret;
}

ObAutoincrementService::ObAutoincrementService()
  : node_allocator_(),
    handle_allocator_(),
    map_mutex_(common::ObLatchIds::AUTO_INCREMENT_INIT_LOCK)
{
}

ObAutoincrementService::~ObAutoincrementService()
{
}

ObAutoincrementService &ObAutoincrementService::get_instance()
{
  static ObAutoincrementService autoinc_service;
  return autoinc_service;
}

int ObAutoincrementService::init(ObMySQLProxy *mysql_proxy)
{
  int ret = OB_SUCCESS;

  ObMemAttr attr(ObModIds::OB_AUTOINCREMENT);
  if (OB_FAIL(autoinc_store_.init(mysql_proxy))) {
  } else if (OB_FAIL(node_allocator_.init(sizeof(TableNode), attr))) {
  } else if (OB_FAIL(handle_allocator_.init(sizeof(CacheHandle), attr))) {
  } else if (OB_FAIL(node_map_.init(attr))) {
  } else {
  }
  return ret;
}

//only used for logic backup

int ObAutoincrementService::get_handle(AutoincParam &param, CacheHandle *&handle)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_sequence_load);
  int ret = OB_SUCCESS;

  const ObObjType column_type  = param.autoinc_col_type_;
  const uint64_t offset        = param.autoinc_offset_;
  const uint64_t increment     = param.autoinc_increment_;
  const uint64_t max_value     = get_max_value(column_type);
  uint64_t effective_base_value = param.autoinc_auto_increment_;
  if (OB_UNLIKELY(offset > 1 && increment >= offset)) {
    effective_base_value = std::max(effective_base_value, offset);
  }

  uint64_t desired_count = 0;
  // calc nb_desired_values in MySQL
  if (0 == param.autoinc_intervals_count_) {
    desired_count = param.total_value_count_;
  } else if (param.autoinc_intervals_count_ <= AUTO_INC_DEFAULT_NB_MAX_BITS) {
    desired_count = AUTO_INC_DEFAULT_NB_ROWS * (1 << param.autoinc_intervals_count_);
    if (desired_count > AUTO_INC_DEFAULT_NB_MAX) {
      desired_count = AUTO_INC_DEFAULT_NB_MAX;
    }
  } else {
    desired_count = AUTO_INC_DEFAULT_NB_MAX;
  }

  // allocate auto-increment value first time
  if (0 == param.autoinc_desired_count_) {
    param.autoinc_desired_count_ = desired_count;
  }

  desired_count = param.autoinc_desired_count_;

  TableNode *table_node = NULL;
  if (OB_UNLIKELY(effective_base_value > max_value)) {
    ret = param.autoinc_auto_increment_ > max_value
            ? OB_ERR_REACH_AUTOINC_MAX : OB_DATA_OUT_OF_RANGE;
    LOG_WARN("auto-increment base value exceeds column range",
             K(ret), K(effective_base_value), K(max_value), K(param));
  } else if (OB_FAIL(get_table_node(param, table_node))) {
  }

  // alloc handle
  bool need_prefetch = false;
  if (OB_SUCC(ret)) {
    if (OB_FAIL(alloc_autoinc_try_lock(table_node->alloc_mutex_))) {
    } else {
      if (OB_SIZE_OVERFLOW == (ret = table_node->alloc_handle(handle_allocator_,
                                                                   offset,
                                                                   increment,
                                                                   desired_count,
                                                                   max_value,
                                                                   handle))) {
        TableNode mock_node;
        if (param.autoinc_desired_count_ <= 0) {
          // do nothing
        } else if (OB_FAIL(fetch_table_node(param, &mock_node))) {
        } else {
          atomic_update(table_node->local_sync_, mock_node.local_sync_);
          table_node->prefetch_node_.reset();
          if (mock_node.curr_node_.cache_start_ == table_node->curr_node_.cache_end_ + 1) {
            // when the above condition is true, it means that no other thread has consume
            // any cache. intra-partition ascending property can be kept
            table_node->curr_node_.cache_end_ = mock_node.curr_node_.cache_end_;
          } else {
            table_node->curr_node_.cache_start_ = mock_node.curr_node_.cache_start_;
            table_node->curr_node_.cache_end_ = mock_node.curr_node_.cache_end_;
          }
          table_node->curr_node_state_is_pending_ = false;
          if (OB_FAIL(table_node->alloc_handle(handle_allocator_,
                                                offset, increment,
                                                desired_count, max_value, handle))) {
          } else {
          }
        }
      }
      table_node->alloc_mutex_.unlock();
    }
    if (OB_SUCC(ret) && table_node->prefetch_condition() && !table_node->prefetching_) {
      need_prefetch = true;
      table_node->prefetching_ = true;
    }
    if (OB_SUCC(ret) && OB_UNLIKELY(need_prefetch)) {
      // ensure single thread to prefetch
      TableNode mock_node;
      if (OB_FAIL(fetch_table_node(param, &mock_node, true))) {
      } else if (OB_FAIL(alloc_autoinc_try_lock(table_node->alloc_mutex_))) {
      } else {
        LOG_INFO("fetch table node success", K(param), K(mock_node), K(*table_node));
        if (table_node->prefetch_node_.cache_start_ != 0 ||
            mock_node.prefetch_node_.cache_start_ <= table_node->curr_node_.cache_end_) {
        } else {
          atomic_update(table_node->local_sync_, mock_node.local_sync_);
          table_node->prefetch_node_.cache_start_ = mock_node.prefetch_node_.cache_start_;
          table_node->prefetch_node_.cache_end_ = mock_node.prefetch_node_.cache_end_;
        }
        table_node->prefetching_ = false;
        table_node->alloc_mutex_.unlock();
      }
    }
  }

  // table node must be reverted after get to decrement reference count
  if (OB_UNLIKELY(NULL != table_node)) {
    node_map_.revert(table_node);
  }
  return ret;
}

void ObAutoincrementService::release_handle(CacheHandle *&handle)
{
  handle_allocator_.free(handle);
  // bug#8200783, should reset handle here
  handle = NULL;
}

int ObAutoincrementService::refresh_local_sync_value(const uint64_t table_id,
                                                     const uint64_t column_id,
                                                     const uint64_t sync_value)
{
  int ret = OB_SUCCESS;
  TableNode *table_node = NULL;
  const AutoincKey key(table_id, column_id);
  if (OB_ENTRY_NOT_EXIST == (ret = node_map_.get(key, table_node))) {
    ret = OB_SUCCESS;
  } else if (OB_SUCC(ret)) {
    atomic_update(table_node->local_sync_, sync_value);
  } else {
    LOG_WARN("failed to get local auto-increment cache", K(key), K(ret));
  }
  // table node must be reverted after get to decrement reference count
  if (NULL != table_node) {
    node_map_.revert(table_node);
  }
  return ret;
}

int ObAutoincrementService::lock_autoinc_row(const uint64_t &table_id,
                                             const uint64_t &column_id,
                                             common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSqlString lock_sql;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_LOAD);
    ObMySQLResult *result = NULL;
    ObISQLClient *sql_client = &trans;
    if (OB_FAIL(lock_sql.assign_fmt("SELECT sequence_key, sequence_value, sync_value "
                                    "FROM %s WHERE sequence_key = %lu "
                                    "AND column_id = %lu FOR UPDATE",
                                    OB_ALL_AUTO_INCREMENT_TNAME,
                                    ObSchemaUtils::get_extract_schema_id(table_id),
                                    column_id))) {
    } else if (OB_FAIL(trans.read(res, lock_sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get result, result is NULL", KR(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        LOG_WARN("autoincrement not exist", KR(ret), K(lock_sql));
      } else {
        LOG_WARN("iterate next result fail", KR(ret), K(lock_sql));
      }
    }
  }
  return ret;
}

//This implement is only for Truncate, table need to reset autoinc version after truncate
int ObAutoincrementService::reset_autoinc_row(const uint64_t &table_id,
                                              const uint64_t &column_id,
                                              const int64_t &autoinc_version,
                                              common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSqlString update_sql;
  int64_t affected_rows = 0;
  ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_SAVE);
  if (OB_FAIL(update_sql.assign_fmt("UPDATE %s SET sequence_value = 1, sync_value = 0, truncate_version = %ld",
                                    OB_ALL_AUTO_INCREMENT_TNAME,
                                    autoinc_version))) {
  } else if (OB_FAIL(update_sql.append_fmt(" WHERE sequence_key = %lu AND column_id = %lu",
                                            ObSchemaUtils::get_extract_schema_id(table_id),
                                            column_id))) {
  } else if (OB_FAIL(trans.write(update_sql.ptr(), affected_rows))) {
  } else if (OB_UNLIKELY(affected_rows > 1)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected affected rows", KR(ret), K(table_id), K(affected_rows), K(update_sql));
  }
  return ret;
}

// for new truncate table
int ObAutoincrementService::reinit_autoinc_row(const uint64_t &table_id,
                                               const uint64_t &column_id,
                                               const int64_t &autoinc_version,
                                               common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(lock_autoinc_row(table_id, column_id, trans))) {
  } else if (OB_FAIL(reset_autoinc_row(table_id, column_id, autoinc_version, trans))) {
  }
  return ret;
}

// use for alter table add autoincrement
int ObAutoincrementService::try_lock_autoinc_row(const uint64_t &table_id,
                                                 const uint64_t &column_id,
                                                 const int64_t &autoinc_version,
                                                 bool &need_update_inner_table,
                                                 common::ObMySQLTransaction &trans)
{
  int ret = OB_SUCCESS;
  ObSqlString lock_sql;
  need_update_inner_table = false;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObMySQLResult *result = NULL;
    if (OB_UNLIKELY(OB_INVALID_ID == table_id
                    || 0 == column_id)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("arg is not invalid", KR(ret), K(table_id), K(column_id));
    } else if (OB_FAIL(lock_sql.assign_fmt("SELECT truncate_version "
                                    "FROM %s WHERE sequence_key = %lu "
                                    "AND column_id = %lu FOR UPDATE",
                                    OB_ALL_AUTO_INCREMENT_TNAME,
                                    ObSchemaUtils::get_extract_schema_id(table_id),
                                    column_id))) {
    } else if (OB_FAIL(trans.read(res, lock_sql.ptr()))) {
    } else if (OB_ISNULL(result = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to get result, result is NULL", KR(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        LOG_INFO("autoinc row not exist", K(table_id), K(column_id));
      } else {
        LOG_WARN("iterate next result fail", KR(ret), K(lock_sql));
      }
    } else {
      int64_t inner_autoinc_version = OB_INVALID_VERSION;
      if (OB_FAIL(result->get_int(static_cast<int64_t>(0), inner_autoinc_version))) {
      } else if (inner_autoinc_version > autoinc_version) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("autoincrement's newest version can not less than inner version",
                  KR(ret), K(table_id), K(column_id),
                  K(inner_autoinc_version), K(autoinc_version));
      } else if (inner_autoinc_version < autoinc_version) {
        need_update_inner_table = true;
        LOG_INFO("inner autoinc version is old, we need to update inner table",
                  K(table_id), K(column_id), K(inner_autoinc_version), K(autoinc_version));
      } else {
      }
    }
  }
  return ret;
}

int ObAutoincrementService::calculate_idempotent_autoinc_val_for_ddl(
                                               AutoincParam *autoinc_param,
                                               const int64_t table_all_slice_count,
                                               const int64_t table_level_slice_idx,
                                               const int64_t slice_row_idx,
                                               const int64_t autoinc_range_interval,
                                               uint64_t &autoinc_value)
{
  int ret = OB_SUCCESS;

  if (OB_ISNULL(autoinc_param)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(autoinc_param),
             K(table_all_slice_count), K(table_level_slice_idx), K(slice_row_idx));
  } else if (autoinc_param->pending_value_to_sync_ >= UINT64_MAX) {
    ret = OB_ERR_REACH_AUTOINC_MAX;
    LOG_WARN("autoinc reach max", K(ret), K(autoinc_param->pending_value_to_sync_));
  } else {
    const int64_t range_id = slice_row_idx / autoinc_range_interval;
    const int64_t row_id_in_range = slice_row_idx % autoinc_range_interval;
    const ObObjType column_type = autoinc_param->autoinc_col_type_;
    // for now, only `offset` param is supported, `increment` param is not supported
    const uint64_t offset = autoinc_param->autoinc_offset_;
    const uint64_t max_value = get_max_value(column_type);
    if (1 == table_all_slice_count) {
      // if generate in only one thread, calc next value by prev value and increment step to compat MySQL
      autoinc_value = OB_UNLIKELY(0 == slice_row_idx) ? offset : min(autoinc_param->pending_value_to_sync_ + autoinc_param->autoinc_increment_, max_value);
    } else {
      autoinc_value =
        min(offset + table_level_slice_idx * autoinc_range_interval +
                (table_all_slice_count * range_id * autoinc_range_interval) + row_id_in_range,
            max_value);
    }
    autoinc_param->pending_value_to_sync_ = max(autoinc_param->pending_value_to_sync_, autoinc_value);
  }

  return ret;
}

int ObAutoincrementService::clear_autoinc_cache(const uint64_t table_id,
                                                const uint64_t column_id)
{
  int ret = OB_SUCCESS;
  LOG_INFO("begin to clear local auto-increment cache", K(table_id), K(column_id));
  const AutoincKey key(table_id, column_id);

  map_mutex_.lock();
  if (OB_FAIL(node_map_.del(key))) {
  }
  map_mutex_.unlock();

  if (OB_ENTRY_NOT_EXIST == ret) {
    // do nothing; key does not exist
    ret = OB_SUCCESS;
  }
  return ret;
}

uint64_t ObAutoincrementService::get_max_value(const common::ObObjType type)
{
  static const uint64_t type_max_value[] =
  {
    // null
    0,
    // signed
    INT8_MAX,               // tiny int   127
    INT16_MAX,              // short      32767
    INT24_MAX,              // medium int 8388607
    INT32_MAX,              // int        2147483647
    INT64_MAX,              // bigint     9223372036854775807
    // unsigned
    UINT8_MAX,              //            255
    UINT16_MAX,             //            65535
    UINT24_MAX,             //            16777215
    UINT32_MAX,             //            4294967295
    UINT64_MAX,             //            18446744073709551615
    // float
    0x1000000ULL,           // float      /* We use the maximum as per IEEE754-2008 standard, 2^24; compatible with MySQL */
    0x20000000000000ULL,    // double     /* We use the maximum as per IEEE754-2008 standard, 2^53; compatible with MySQL */
    0x1000000ULL,           // ufloat
    0x20000000000000ULL,    // udouble

    // do not support
//    "NUMBER",
//    "NUMBER UNSIGNED",
//
//    "DATETIME",
//    "TIMESTAMP",
//    "DATE",
//    "TIME",
//    "YEAR",
//
//    "VARCHAR",
//    "CHAR",
//    "VARBINARY",
//    "BINARY",
//
//    "EXT",
//    "UNKNOWN",
    0
  };
  return type_max_value[OB_LIKELY(type < ObNumberType) ? type : ObNumberType];
}

int ObAutoincrementService::get_table_node(const AutoincParam &param, TableNode *&table_node)
{
  int ret = OB_SUCCESS;
  
  uint64_t table_id      = param.autoinc_table_id_;
  uint64_t column_id     = param.autoinc_col_id_;
  // auto-increment key
  AutoincKey key;
  
  key.table_id_  = table_id;
  key.column_id_ = column_id;
  int64_t autoinc_version = param.autoinc_version_;
  if (OB_FAIL(node_map_.get(key, table_node))) {
    if (ret != OB_ENTRY_NOT_EXIST) {
      LOG_ERROR("get from map failed", K(ret));
    } else {
      common::ObLatch &mutex = init_node_mutex_[table_id % INIT_NODE_MUTEX_NUM];
      if (OB_FAIL(mutex.wrlock(common::ObLatchIds::AUTO_INCREMENT_INIT_LOCK))) {
      } else {
        if (OB_ENTRY_NOT_EXIST == (ret = node_map_.get(key, table_node))) {
          LOG_INFO("alloc table node for auto increment key", K(key));
          if (OB_FAIL(node_map_.alloc_value(table_node))) {
          } else if (OB_FAIL(table_node->init(param.autoinc_table_part_num_))) {
          } else {
            table_node->prefetch_node_.reset();
            table_node->autoinc_version_ = autoinc_version;
            table_node->max_value_ = get_max_value(param.autoinc_col_type_);
            lib::ObMutexGuard guard(map_mutex_);
            if (OB_FAIL(node_map_.insert_and_get(key, table_node))) {
            }
          }
          if (OB_FAIL(ret) && table_node != nullptr) {
            node_map_.free_value(table_node);
            table_node = NULL;
          }
        }
        mutex.unlock();
      }
    }
  } else {
    if (OB_FAIL(alloc_autoinc_try_lock(table_node->alloc_mutex_))) {
    } else {
      //  local cache is expired
      if (OB_UNLIKELY(autoinc_version > table_node->autoinc_version_)) {
        LOG_INFO("start to reset table node", K(*table_node), K(param));
        table_node->next_value_ = 0;
        table_node->local_sync_ = 0;
        table_node->curr_node_.reset();
        table_node->prefetch_node_.reset();
        table_node->prefetching_ = false;
        table_node->curr_node_state_is_pending_ = true;
        table_node->autoinc_version_ = autoinc_version;
        table_node->max_value_ = get_max_value(param.autoinc_col_type_);
      // old request cannot get table node, it should retry
      } else if (OB_UNLIKELY(autoinc_version < table_node->autoinc_version_)) {
        ret = OB_AUTOINC_CACHE_NOT_EQUAL;
        LOG_WARN("old reqeust can not get table node, it should retry", KR(ret), K(autoinc_version), K(table_node->autoinc_version_));
      } else {
        table_node->max_value_ = get_max_value(param.autoinc_col_type_);
      }
      table_node->alloc_mutex_.unlock();
    }
  }
  if (OB_SUCC(ret)) {
  } else {
    LOG_WARN("failed to get table node", K(param), K(ret));
  }
  return ret;
}

int ObAutoincrementService::alloc_autoinc_try_lock(lib::ObMutex &alloc_mutex)
{
  int ret = OB_SUCCESS;
  static const int64_t SLEEP_TS_US = 10;
  while (OB_SUCC(ret) && OB_FAIL(alloc_mutex.trylock())) {
    if (OB_EAGAIN == ret) {
      THIS_WORKER.check_wait();
      ob_usleep(SLEEP_TS_US);
      if (OB_FAIL(THIS_WORKER.check_status())) {
      }
    } else {
      LOG_WARN("fail to try lock mutex", K(ret));
    }
  }
  return ret;
}

int ObAutoincrementService::fetch_table_node(const AutoincParam &param,
                                             TableNode *table_node,
                                             const bool fetch_prefetch)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id      = param.autoinc_table_id_;
  const uint64_t column_id     = param.autoinc_col_id_;
  const ObObjType column_type  = param.autoinc_col_type_;
  const uint64_t part_num      = param.autoinc_table_part_num_;
  const uint64_t desired_count = param.autoinc_desired_count_;
  const uint64_t offset        = param.autoinc_offset_;
  const uint64_t increment     = param.autoinc_increment_;
  const int64_t autoinc_version = param.autoinc_version_;
  if (part_num <= 0 || ObNullType == column_type) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(part_num), K(column_type), K(ret));
  } else {
    uint64_t sync_value = 0;
    uint64_t max_value = get_max_value(column_type);
    int64_t auto_increment_cache_size = param.auto_increment_cache_size_;
    uint64_t start_inclusive = 0;
    uint64_t end_inclusive = 0;
    if (auto_increment_cache_size < 1 || auto_increment_cache_size > MAX_INCREMENT_CACHE_SIZE) {
      // no ret, just log error
      LOG_ERROR("unexpected auto_increment_cache_size", K(auto_increment_cache_size));
      auto_increment_cache_size = DEFAULT_INCREMENT_CACHE_SIZE;
    }
    const uint64_t local_cache_size = auto_increment_cache_size;
    uint64_t prefetch_count = std::min(max_value / 100 / part_num, local_cache_size);
    uint64_t batch_count = 0;
    if (prefetch_count > 1) {
      batch_count = std::max(increment * prefetch_count, increment * desired_count);
    } else {
      batch_count = increment * desired_count;
    }
    AutoincKey key(table_id, column_id);
    uint64_t table_auto_increment = param.autoinc_auto_increment_;
    if (OB_UNLIKELY(table_auto_increment > max_value)) {
      ret = OB_ERR_REACH_AUTOINC_MAX;
      LOG_WARN("reach max autoinc", K(ret), K(table_auto_increment));
    } else if (OB_FAIL(autoinc_store_.get_value(
                          key, offset, increment, max_value, table_auto_increment,
                          batch_count, auto_increment_cache_size, autoinc_version, sync_value,
                          start_inclusive, end_inclusive))) {
    } else if (sync_value > max_value || start_inclusive > max_value) {
      ret = OB_ERR_REACH_AUTOINC_MAX;
      LOG_WARN("reach max autoinc", K(start_inclusive), K(max_value), K(ret));
    }

    if (OB_SUCC(ret)) {
      atomic_update(table_node->local_sync_, sync_value);
      if (fetch_prefetch) {
        table_node->prefetch_node_.cache_start_ = start_inclusive;
        table_node->prefetch_node_.cache_end_ = std::min(max_value, end_inclusive);
      } else {
        // there is no prefetch_node here
        // because we must have tried to allocate cache handle from curr_node and prefetch_node
        // before allocate new cache node
        // if we allocate new cache node, curr_node and prefetch_node should have been combined;
        // and prefetch_node should have been reset
        CacheNode new_node;
        new_node.cache_start_ = start_inclusive;
        new_node.cache_end_   = std::min(max_value, end_inclusive);
        if (OB_FAIL(table_node->curr_node_.combine_cache_node(new_node))) {
        } else if (0 == table_node->next_value_) {
          table_node->next_value_ = start_inclusive;
        }
      }
    }

    // ignore error for prefetch, cache is enough here
    // other thread will try next time
    if (fetch_prefetch && OB_FAIL(ret)) {
      LOG_WARN("failed to prefetch; ignore this", K(ret));
      ret = OB_SUCCESS;
    }
  }

  return ret;
}

/* core logic:
 * 1. persist insert_value when it is larger than local_sync_
 * 2. update the local table node with the persisted high watermark
 */
int ObAutoincrementService::sync_insert_value_to_store(AutoincParam &param,
                                                       CacheHandle *&cache_handle,
                                                       const uint64_t insert_value)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id      = param.autoinc_table_id_;
  const uint64_t column_id     = param.autoinc_col_id_;
  const ObObjType column_type  = param.autoinc_col_type_;
  const uint64_t max_value     = get_max_value(column_type);
  const int64_t autoinc_version = param.autoinc_version_;
  TableNode *table_node = NULL;
  if (OB_FAIL(get_table_node(param, table_node))) {
  } else {
    uint64_t stored_sync_value = 0;
    AutoincKey key(table_id, column_id);
    if (insert_value <= ATOMIC_LOAD(&table_node->local_sync_)) {
      // do nothing
    } else {
      // The persisted sync value tracks the greatest explicit value, while the
      // sequence value already tracks the end of the reserved cache range.
      // Rounding an explicit value to the cache boundary would discard the
      // remaining local range and create a cache-sized gap.
      const uint64_t value_to_sync = std::min(insert_value, max_value);
      if (OB_FAIL(autoinc_store_.sync_value(key, max_value, value_to_sync,
                                           autoinc_version, 0, stored_sync_value))) {
      } else if (OB_FAIL(alloc_autoinc_try_lock(table_node->alloc_mutex_))) {
      } else {
        atomic_update(table_node->local_sync_, stored_sync_value);
        table_node->alloc_mutex_.unlock();
      }
    }

    // INSERT INTO t1 VALUES (null), (null), (4), (null), (null);
    //                          1      2      4     5       6
    // assume cache_handle saves [1, 5]
    // in this case, values will be allocated as above. in order to generate the forth value '5',
    // we need following logic:
    if (OB_SUCC(ret)) {
      if (NULL != cache_handle) {
        if (insert_value < cache_handle->prefetch_end_) {
          if (insert_value >= cache_handle->next_value_) {
            if (OB_FAIL(calc_next_value(insert_value + 1,
                                        param.autoinc_offset_,
                                        param.autoinc_increment_,
                                        cache_handle->next_value_))) {
            }
          }
        } else {
          // release handle No.
          handle_allocator_.free(cache_handle);
          cache_handle = NULL;
          // invalid cache handle; record count
          param.autoinc_intervals_count_++;
        }
      }
    }

    if (OB_SUCC(ret)) {
      // Note: when insert_value is larger than current cache value, do we really need to
      // refresh local cache? What if another thread is refreshing local cache?
      //   - if we don't, and another thread fetches a small cache value, it is not OK.
      // SO, we must fetch table node here.
      // syncing insert_value is not the common case. perf acceptable
      if (OB_FAIL(alloc_autoinc_try_lock(table_node->alloc_mutex_))) {
      } else {
        if (insert_value >= table_node->curr_node_.cache_end_
            && insert_value >= table_node->prefetch_node_.cache_end_) {
          TableNode mock_node;
          if (param.autoinc_desired_count_ <= 0) {
            // do nothing
          } else if (OB_FAIL(fetch_table_node(param, &mock_node))) {
          } else {
            table_node->prefetch_node_.reset();
            if (mock_node.curr_node_.cache_end_ > table_node->curr_node_.cache_end_) {
              table_node->curr_node_.cache_start_ = mock_node.curr_node_.cache_start_;
              table_node->curr_node_.cache_end_ = mock_node.curr_node_.cache_end_;
            }
            LOG_INFO("fetch table node success", K(param), K(*table_node));
          }
        }
        table_node->alloc_mutex_.unlock();
      }
    }
  }
  // table node must be reverted after get to decrement reference count
  if (NULL != table_node) {
    node_map_.revert(table_node);
  }
  return ret;
}

// sync last user specified value first(compatible with MySQL)
int ObAutoincrementService::sync_insert_value(AutoincParam &param)
{
  int ret = OB_SUCCESS;
  if (0 != param.pending_value_to_sync_) {
    if (param.pending_value_to_sync_ < param.autoinc_auto_increment_) {
      // do nothing, insert value directly
    } else if (OB_FAIL(sync_insert_value_to_store(param,
                                                 param.cache_handle_,
                                                 param.pending_value_to_sync_))) {
    }
    param.pending_value_to_sync_ = 0;
  }
  return ret;
}

// sync last user specified value in stmt
int ObAutoincrementService::sync_insert_value_local(AutoincParam &param)
{
  int ret = OB_SUCCESS;
  if (param.sync_flag_) {
    if (param.pending_value_to_sync_ < param.value_to_sync_) {
      param.pending_value_to_sync_ = param.value_to_sync_;
    }
    param.sync_flag_ = false;
  }
  return ret;
}

int ObAutoincrementService::sync_auto_increment(const ObTableSchema &table_schema,
                                                const uint64_t sync_value)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = table_schema.get_table_id();
  const uint64_t column_id = table_schema.get_autoinc_column_id();
  const ObColumnSchemaV2 *column_schema = table_schema.get_column_schema(column_id);
  if (OB_UNLIKELY(OB_INVALID_ID == table_id || 0 == column_id)
      || OB_ISNULL(column_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid auto-increment schema", K(ret), K(table_id), K(column_id),
             KP(column_schema));
  } else {
    const uint64_t max_value = get_max_value(column_schema->get_data_type());
    const uint64_t value_to_sync = std::min(sync_value, max_value);
    const AutoincKey key(table_id, column_id);
    // AUTO_INCREMENT is persisted in the table schema.  Existing local caches
    // must observe the new lower bound, but the sequence row must not be
    // advanced by ALTER TABLE itself.  A later allocation persists its cache
    // reservation in the normal path.
    LOG_INFO("begin to refresh local auto-increment lower bound",
             K(key), K(value_to_sync), K(sync_value));
    if (OB_FAIL(refresh_local_sync_value(table_id, column_id, value_to_sync))) {
    }
  }
  return ret;
}

int ObAutoincrementService::calc_next_value(const uint64_t last_next_value,
                                            const uint64_t offset,
                                            const uint64_t increment,
                                            uint64_t &new_next_value)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(increment <= 0)) {
    //There is a division by zero error, need defensive check
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("increment is invalid", K(ret), K(increment));
  } else {
    uint64_t real_offset = offset;

    if (real_offset > increment) {
      real_offset = 0;
    }
    if (last_next_value <= real_offset) {
      new_next_value = real_offset;
    } else {
      new_next_value = ((last_next_value - real_offset + increment - 1) / increment) * increment + real_offset;
    }
    if (new_next_value < last_next_value) {
      new_next_value = UINT64_MAX;
    }
  }
  return ret;
}

// Tow params control the starting value
// - auto_increment_increment controls the interval between successive column values.
// - auto_increment_offset determines the starting point for the AUTO_INCREMENT column value.
//
// When the auto value reaches its end, it will keep producing the max value.
// The max value is decided by:
//
// If either of these variables is changed, and then new rows inserted into a table containing an
// AUTO_INCREMENT column, the results may seem counterintuitive because the series of AUTO_INCREMENT
// values is calculated without regard to any values already present in the column, and the next
// value inserted is the least value in the series that is greater than the maximum existing value
// in the AUTO_INCREMENT column. The series is calculated like this:
//
// prev_value =  auto_increment_offset + N × auto_increment_increment
//
// More details:
// https://dev.mysql.com/doc/refman/5.6/en/replication-options-master.html#sysvar_auto_increment_increment
//
// The doc does not mention one case: when offset > max_value, the formulator is not right.
// a bug is recorded here: 
int ObAutoincrementService::calc_prev_value(const uint64_t max_value,
                                            const uint64_t offset,
                                            const uint64_t increment,
                                            uint64_t &prev_value)
{
  int ret = OB_SUCCESS;
  if (max_value <= offset) {
    prev_value = max_value;
  } else {
    if (0 == increment) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      prev_value = ((max_value - offset) / increment) * increment + offset;
    }
  }
  LOG_INFO("out of range for column. calc prev value",
           K(prev_value), K(max_value), K(offset), K(increment));
  return ret;
}

int ObAutoincrementService::get_local_sequence_value_(const AutoincKey &key,
                                                      const int64_t autoinc_version,
                                                      uint64_t &seq_value,
                                                      bool &found)
{
  int ret = OB_SUCCESS;
  TableNode *table_node = nullptr;
  found = false;

  // The value persisted in __all_auto_increment is the end of the reserved
  // cache range.  The current process owns the only auto-increment cache in a
  // standalone deployment, so its next unallocated value is the logical
  // sequence value exposed to SHOW/FORK, just as the former ORDER service did.
  int tmp_ret = node_map_.get(key, table_node);
  if (OB_SUCCESS == tmp_ret) {
    if (OB_ISNULL(table_node)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("auto increment table node is null", K(ret), K(key));
    } else if (OB_FAIL(alloc_autoinc_try_lock(table_node->alloc_mutex_))) {
    } else {
      if (autoinc_version == table_node->autoinc_version_
          && table_node->max_value_ > 0
          && (table_node->next_value_ > 0 || table_node->local_sync_ > 0)) {
        const uint64_t local_sync = ATOMIC_LOAD(&table_node->local_sync_);
        const uint64_t next_after_sync = local_sync == UINT64_MAX
                                           ? UINT64_MAX : local_sync + 1;
        seq_value = std::min(table_node->max_value_,
                             std::max(table_node->next_value_, next_after_sync));
        found = true;
      }
      table_node->alloc_mutex_.unlock();
    }
  } else if (OB_ENTRY_NOT_EXIST != tmp_ret) {
    ret = tmp_ret;
    LOG_WARN("failed to get auto increment table node", K(ret), K(key));
  }
  if (OB_NOT_NULL(table_node)) {
    node_map_.revert(table_node);
  }
  return ret;
}

int ObAutoincrementService::get_sequence_value(const uint64_t table_id,
                                               const uint64_t column_id,
                                               const int64_t autoinc_version,
                                               uint64_t &seq_value)
{
  int ret = OB_SUCCESS;
  const AutoincKey key(table_id, column_id);
  bool found_in_local_cache = false;
  if (OB_FAIL(get_local_sequence_value_(key, autoinc_version, seq_value,
                                        found_in_local_cache))) {
  } else if (!found_in_local_cache
             && OB_FAIL(autoinc_store_.get_sequence_value(
                  key, autoinc_version, seq_value))) {
    LOG_WARN("autoincrement store get sequence value failed", K(ret), K(key));
  }
  return ret;
}

// Used by SHOW TABLE STATUS.
int ObAutoincrementService::get_sequence_values(const ObIArray<AutoincKey> &autoinc_keys,
    const ObIArray<int64_t> &autoinc_versions,
    ObHashMap<AutoincKey, uint64_t> &seq_values)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(autoinc_store_.get_auto_increment_values(
                autoinc_keys, autoinc_versions, seq_values))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < autoinc_keys.count(); ++i) {
      const AutoincKey &key = autoinc_keys.at(i);
      uint64_t seq_value = 0;
      bool found = false;
      if (i >= autoinc_versions.count()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("auto increment version count does not match key count",
                 K(ret), K(i), K(autoinc_keys.count()), K(autoinc_versions.count()));
      } else if (OB_FAIL(get_local_sequence_value_(
                   key, autoinc_versions.at(i), seq_value, found))) {
      } else if (found && OB_FAIL(seq_values.set_refactored(
                            key, seq_value, 1 /* overwrite */))) {
        LOG_WARN("failed to update cached auto increment value", K(ret), K(key));
      }
    }
  }
  return ret;
}

int ObInnerTableAutoincrementStore::get_value(
    const AutoincKey &key,
    const uint64_t offset,
    const uint64_t increment,
    const uint64_t max_value,
    const uint64_t table_auto_increment,
    const uint64_t desired_count,
    const uint64_t cache_size,
    const int64_t &autoinc_version,
    uint64_t &sync_value,
    uint64_t &start_inclusive,
    uint64_t &end_inclusive)
{
  UNUSED(cache_size);
  int ret = OB_SUCCESS;
  uint64_t sync_value_from_inner_table = 0;
  ret = inner_table_proxy_.next_autoinc_value(
          key, offset, increment, table_auto_increment, max_value, desired_count, autoinc_version,
          start_inclusive, end_inclusive, sync_value_from_inner_table);
  if (OB_SUCC(ret)) {
    if (table_auto_increment != 0 && table_auto_increment - 1 > sync_value_from_inner_table) {
      sync_value = table_auto_increment -1;
    } else {
      sync_value = sync_value_from_inner_table;
    }
  }
  return ret;
}

int ObInnerTableAutoincrementStore::get_sequence_value(const AutoincKey &key,
                                                       const int64_t &autoinc_version,
                                                       uint64_t &sequence_value)
{
  uint64_t sync_value = 0; // unused
  return inner_table_proxy_.get_autoinc_value(key, autoinc_version, sequence_value, sync_value);
}

int ObInnerTableAutoincrementStore::get_auto_increment_values(
    const common::ObIArray<AutoincKey> &autoinc_keys,
    const common::ObIArray<int64_t> &autoinc_versions,
    common::hash::ObHashMap<AutoincKey, uint64_t> &seq_values)
{
  UNUSED(autoinc_versions);
  return inner_table_proxy_.get_autoinc_value_in_batch(autoinc_keys, seq_values);
}

int ObInnerTableAutoincrementStore::sync_value(
    const AutoincKey &key,
    const uint64_t max_value,
    const uint64_t insert_value,
    const int64_t &autoinc_version,
    const int64_t cache_size,
    uint64_t &sync_value)
{
  UNUSED(cache_size);
  uint64_t seq_value = 0; // unused, * MUST * set seq_value to 0 here.
  return inner_table_proxy_.sync_autoinc_value(key, insert_value, max_value, autoinc_version,
                                               seq_value, sync_value);
}

int ObAutoIncInnerTableProxy::check_inner_autoinc_version(const int64_t &request_autoinc_version,
                                                          const int64_t &inner_autoinc_version,
                                                          const AutoincKey &key)
{
  int ret = OB_SUCCESS;
  if (0 == request_autoinc_version || 0 == inner_autoinc_version) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("autoinc version is zero", KR(ret), K(request_autoinc_version), K(inner_autoinc_version));
  // inner table did not update
  } else if (OB_UNLIKELY(inner_autoinc_version < request_autoinc_version)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("inner_autoinc_version can not less than autoinc_version", KR(ret), K(key),
                                                                        K(inner_autoinc_version), K(request_autoinc_version));
  // old request
  } else if (OB_UNLIKELY(inner_autoinc_version > request_autoinc_version)) {
    ret = OB_AUTOINC_CACHE_NOT_EQUAL;
    LOG_WARN("inner_autoinc_version is greater than autoinc_version, request needs retry", KR(ret), K(key),
                                                                                           K(inner_autoinc_version), K(request_autoinc_version));
  }
  return ret;
}

int ObAutoIncInnerTableProxy::next_autoinc_value(const AutoincKey &key,
                                                 const uint64_t offset,
                                                 const uint64_t increment,
                                                 const uint64_t base_value,
                                                 const uint64_t max_value,
                                                 const uint64_t desired_count,
                                                 const int64_t &autoinc_version,
                                                 uint64_t &start_inclusive,
                                                 uint64_t &end_inclusive,
                                                 uint64_t &sync_value)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  bool with_snap_shot = true;
  
  const uint64_t table_id = key.table_id_;
  const uint64_t column_id = key.column_id_;
  uint64_t sequence_value = 0;
  int64_t inner_autoinc_version = OB_INVALID_VERSION;
  int64_t tmp_autoinc_version = autoinc_version;
  if (OB_ISNULL(mysql_proxy_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy is null", K(ret));
  } else if (OB_FAIL(trans.start(mysql_proxy_, with_snap_shot))) {
  } else {
    int sql_len = 0;
    SMART_VAR(char[OB_MAX_SQL_LENGTH], sql) {
      
      const char *table_name = OB_ALL_AUTO_INCREMENT_TNAME;
      sql_len = snprintf(sql, OB_MAX_SQL_LENGTH,
                         " SELECT sequence_key, sequence_value, sync_value, truncate_version FROM %s WHERE sequence_key = %lu AND column_id = %lu FOR UPDATE",
                         table_name,
                         ObSchemaUtils::get_extract_schema_id(table_id),
                         column_id);
      if (sql_len >= OB_MAX_SQL_LENGTH || sql_len <= 0) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("failed to format sql. size not enough", K(ret), K(sql_len));
      } else {
        int64_t fetch_table_id = OB_INVALID_ID;
        { // make sure %res destructed before execute other sql in the same transaction
          SMART_VAR(ObMySQLProxy::MySQLResult, res) {
            ObMySQLResult *result = NULL;
            ObISQLClient *sql_client = &trans;
            ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_LOAD);
            auto &sql_client_retry_weak = *sql_client;
            if (OB_FAIL(sql_client_retry_weak.read(res, sql))) {
            } else if (NULL == (result = res.get_result())) {
              LOG_WARN("failed to get result", K(ret));
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_FAIL(result->next())) {
              LOG_WARN("failed to get next", K(ret));
              if (OB_ITER_END == ret) {
                // auto-increment column has been deleted
                ret = OB_SCHEMA_ERROR;
                LOG_WARN("failed to get next", K(ret));
              }
            } else {
              if (OB_FAIL(result->get_int(static_cast<int64_t>(0), fetch_table_id))) {
              } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(1), sequence_value))) {
              } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(2), sync_value))) {
              } else if (OB_FAIL(result->get_int(static_cast<int64_t>(3), inner_autoinc_version))) {
              } else if (OB_FAIL(check_inner_autoinc_version(tmp_autoinc_version, inner_autoinc_version, key))) {
              } else {
                if (sync_value >= max_value) {
                  sequence_value = max_value;
                } else {
                  sequence_value = std::max(sequence_value, sync_value + 1);
                }
                if (base_value > sequence_value) {
                  sequence_value = base_value;
                }
              }
              if (OB_SUCC(ret)) {
                int tmp_ret = OB_SUCCESS;
                if (OB_ITER_END != (tmp_ret = result->next())) {
                  if (OB_SUCCESS == tmp_ret) {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("more than one row", K(ret), K(table_id), K(column_id));
                  } else {
                    ret = tmp_ret;
                    LOG_WARN("fail to iter next row", K(ret), K(table_id),
                                                      K(column_id));
                  }
                }
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          uint64_t curr_new_value = 0;
          if (OB_FAIL(ObAutoincrementService::calc_next_value(sequence_value, offset, increment, curr_new_value))) {
          } else {
            uint64_t next_sequence_value = 0;
            if (max_value < desired_count || curr_new_value >= max_value - desired_count) {
              end_inclusive = max_value;
              next_sequence_value = max_value;
              if (OB_UNLIKELY(curr_new_value > max_value)) {
                curr_new_value = max_value;
              }
            } else {
              end_inclusive = curr_new_value + desired_count - 1;
              next_sequence_value = curr_new_value + desired_count;
              if (OB_UNLIKELY(end_inclusive >= max_value || end_inclusive < curr_new_value /* overflow */)) {
                end_inclusive = max_value;
                next_sequence_value = max_value;
              }
            }
            start_inclusive = curr_new_value;

            sql_len = snprintf(sql, OB_MAX_SQL_LENGTH,
                              "UPDATE %s SET sequence_value = %lu, gmt_modified = now(6)"
                              " WHERE sequence_key = %lu AND column_id = %lu AND truncate_version = %ld",
                              table_name,
                              next_sequence_value,
                              table_id,
                              column_id,
                              inner_autoinc_version);
            int64_t affected_rows = 0;
            ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_SAVE);
            if (sql_len >= OB_MAX_SQL_LENGTH || sql_len <= 0) {
              ret = OB_SIZE_OVERFLOW;
              LOG_WARN("failed to format sql. size not enough", K(ret), K(sql_len));
            } else if (OB_FAIL(trans.write(sql, affected_rows))) {
            } else if (affected_rows != 1) {
              LOG_WARN("failed to update sequence value",
                      K(table_id), K(column_id), K(ret));
            }
          }
        }
      }
    }

    // commit transaction or rollback
    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true))) {
      }
    } else {
      int err = OB_SUCCESS;
      if (OB_SUCCESS != (err = trans.end(false))) {
      }
    }
  }
  return ret;
}

int ObAutoIncInnerTableProxy::get_autoinc_value(const AutoincKey &key,
                                                const int64_t &autoinc_version,
                                                uint64_t &seq_value,
                                                uint64_t &sync_value)
{
  int ret = OB_SUCCESS;
  
  const int64_t tmp_autoinc_version = autoinc_version;
  SMART_VARS_2((ObMySQLProxy::MySQLResult, res), (char[OB_MAX_SQL_LENGTH], sql)) {
    ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_LOAD);
    ObMySQLResult *result = NULL;
    int sql_len = 0;

    const char *table_name = OB_ALL_AUTO_INCREMENT_TNAME;
    sql_len = snprintf(sql, OB_MAX_SQL_LENGTH,
                        " SELECT sequence_value, sync_value, truncate_version FROM %s"
                        " WHERE sequence_key = %lu AND column_id = %lu",
                        table_name,
                        ObSchemaUtils::get_extract_schema_id(key.table_id_),
                        key.column_id_);
    if (OB_ISNULL(mysql_proxy_)) {
      ret = OB_NOT_INIT;
      LOG_WARN("mysql proxy is null", K(ret));
    } else if (sql_len >= OB_MAX_SQL_LENGTH || sql_len <= 0) {
      ret = OB_SIZE_OVERFLOW;
      LOG_WARN("failed to format sql. size not enough", K(ret), K(sql_len));
    } else if (OB_FAIL(mysql_proxy_->read(res, sql))) {
    } else if (NULL == (result = res.get_result())) {
      LOG_WARN("failed to get result", K(ret));
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        LOG_INFO("there is no autoinc column record, return 0 as seq_value by default",
                  K(key), K(ret));
        seq_value = 0;
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail get next value", K(key), K(ret));
      }
    } else {
      int64_t inner_autoinc_version = OB_INVALID_VERSION;
      if (OB_FAIL(result->get_uint(static_cast<int64_t>(0), seq_value))) {
      } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(1), sync_value))) {
      } else if (OB_FAIL(result->get_int(static_cast<int64_t>(2), inner_autoinc_version))) {
      } else if (OB_FAIL(check_inner_autoinc_version(tmp_autoinc_version, inner_autoinc_version, key))) {
      }
      if (OB_SUCC(ret)) {
        int tmp_ret = OB_SUCCESS;
        if (OB_ITER_END != (tmp_ret = result->next())) {
          if (OB_SUCCESS == tmp_ret) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("more than one row", K(ret), K(key));
          } else {
            ret = tmp_ret;
            LOG_WARN("fail to iter next row", K(ret), K(key));
          }
        }
      }
    }
  }
  return ret;
}

// TODO: verify autoinc_version before expanding this batch interface to other callers.
int ObAutoIncInnerTableProxy::get_autoinc_value_in_batch(const common::ObIArray<AutoincKey> &keys,
    common::hash::ObHashMap<AutoincKey, uint64_t> &seq_values)
{
  int ret = OB_SUCCESS;
  int64_t N = keys.count() / FETCH_SEQ_NUM_ONCE;
  int64_t M = keys.count() % FETCH_SEQ_NUM_ONCE;
  N += (M == 0) ? 0 : 1;
  ObSqlString sql;
  if (OB_ISNULL(mysql_proxy_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy is null", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
    sql.reset();
    if (OB_FAIL(sql.assign_fmt(
        " SELECT sequence_key, column_id, sequence_value FROM %s"
        " WHERE (sequence_key, column_id) IN (", OB_ALL_AUTO_INCREMENT_TNAME))) {
    }

    // last iteration
    int64_t P = (0 != M && N - 1 == i) ? M : FETCH_SEQ_NUM_ONCE;
    for (int64_t j = 0; OB_SUCC(ret) && j < P; ++j) {
      AutoincKey key = keys.at(i * FETCH_SEQ_NUM_ONCE + j);
      if (OB_FAIL(sql.append_fmt("%s(%lu, %lu)",
                                 (0 == j) ? "" : ", ",
                                 key.table_id_,
                                 key.column_id_))) {
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(sql.append_fmt(")"))) {
      }
    }

    if (OB_SUCC(ret)) {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_LOAD);
        ObMySQLResult *result = NULL;
        int64_t table_id  = 0;
        int64_t column_id = 0;
        uint64_t seq_value = 0;
        ObISQLClient *sql_client = mysql_proxy_;
        auto &sql_client_retry_weak = *sql_client;
        if (OB_FAIL(sql_client_retry_weak.read(res, sql.ptr()))) {
        } else if (NULL == (result = res.get_result())) {
          LOG_WARN("failed to get result", K(ret));
          ret = OB_ERR_UNEXPECTED;
        } else {
          while(OB_SUCC(ret) && OB_SUCC(result->next())) {
            if (OB_FAIL(result->get_int(static_cast<int64_t>(0), table_id))) {
            } else if (OB_FAIL(result->get_int(static_cast<int64_t>(1), column_id))) {
            } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(2), seq_value))) {
            } else {
              AutoincKey key;
              
              
              key.table_id_  = table_id;
              key.column_id_ = static_cast<uint64_t>(column_id);
              if (OB_FAIL(seq_values.set_refactored(key, seq_value))) {
              }
            }
          }
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("fail to get next result", K(ret), K(sql));
          }
        }
      }
    }

    if (OB_FAIL(ret)) {
      break;
    }
  }
  return ret;
}

int ObAutoIncInnerTableProxy::sync_autoinc_value(const AutoincKey &key,
                                                 const uint64_t insert_value,
                                                 const uint64_t max_value,
                                                 const int64_t autoinc_version,
                                                 uint64_t &seq_value,
                                                 uint64_t &sync_value)
{
  int ret = OB_SUCCESS;
  
  const uint64_t table_id = key.table_id_;
  const uint64_t column_id = key.column_id_;
  ObMySQLTransaction trans;
  ObSqlString sql;
  bool with_snap_shot = true;
  uint64_t fetch_seq_value = 0;
  int64_t inner_autoinc_version = OB_INVALID_VERSION;
  int64_t tmp_autoinc_version = autoinc_version;
  if (OB_ISNULL(mysql_proxy_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy is null", K(ret));
  } else if (OB_FAIL(trans.start(mysql_proxy_, with_snap_shot))) {
  } else {
    
    const char *table_name = OB_ALL_AUTO_INCREMENT_TNAME;
    int64_t fetch_table_id = OB_INVALID_ID;
    if (OB_FAIL(sql.assign_fmt(" SELECT sequence_key, sequence_value, sync_value, truncate_version FROM %s WHERE sequence_key = %lu"
                               " AND column_id = %lu FOR UPDATE",
                               table_name,
                               ObSchemaUtils::get_extract_schema_id(table_id),
                               column_id))) {
    }
    if (OB_SUCC(ret)) {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_LOAD);
        ObMySQLResult *result = NULL;
        ObISQLClient *sql_client = &trans;
        auto &sql_client_retry_weak = *sql_client;
        if (OB_FAIL(sql_client_retry_weak.read(res, sql.ptr()))) {
        } else if (NULL == (result = res.get_result())) {
          LOG_WARN("failed to get result", K(ret));
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(result->next())) {
          LOG_WARN("failed to get next", K(ret));
          if (OB_ITER_END == ret) {
            // auto-increment column has been deleted
            ret = OB_SCHEMA_ERROR;
            LOG_WARN("failed to get next", K(ret));
          }
        } else if (OB_FAIL(result->get_int(static_cast<int64_t>(0), fetch_table_id))) {
        } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(1), fetch_seq_value))) {
        } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(2), sync_value))) {
        } else if (OB_FAIL(result->get_int(static_cast<int64_t>(3), inner_autoinc_version))) {
        } else if (OB_FAIL(check_inner_autoinc_version(tmp_autoinc_version, inner_autoinc_version, key))) {
        }
        if (OB_SUCC(ret)) {
          int tmp_ret = OB_SUCCESS;
          if (OB_ITER_END != (tmp_ret = result->next())) {
            if (OB_SUCCESS == tmp_ret) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("more than one row", K(ret), K(table_id), K(column_id));
            } else {
              ret = tmp_ret;
              LOG_WARN("fail to iter next row", K(ret), K(table_id), K(column_id));
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      uint64_t new_seq_value = 0;
      if (insert_value > sync_value) {
        // sequence_value is the end of the range already reserved by the
        // local cache, whereas sync_value is the greatest explicit value.
        // Keep the reservation when the explicit value falls inside it;
        // otherwise advance the next sequence past the explicit value.
        sync_value = insert_value;
        const uint64_t next_after_insert =
            insert_value >= max_value ? max_value : insert_value + 1;
        new_seq_value = std::max(fetch_seq_value, next_after_insert);
        seq_value = new_seq_value;

        // Persist the explicit high watermark and the non-decreasing cache
        // reservation atomically in __all_auto_increment.
        int64_t affected_rows = 0;
        // NOTE: Why the sequence value is also updated?
        //       > In order to support display correct AUTO_INCREMENT property in SHOW CREATE TABLE
        //       statment.
        //       Why don't we calculate AUTO_INCREMENT in real time when we execute the SHOW
        //       statement?
        //       > I can't get MAX_VALUE in DDL context. auto inc column type is needed.
        ObASHSetInnerSqlWaitGuard ash_inner_sql_guard(ObInnerSqlWaitTypeId::SEQUENCE_SAVE);
        if (OB_FAIL(sql.assign_fmt(
                    "UPDATE %s SET sync_value = %lu, sequence_value = %lu, gmt_modified = now(6) "
                    "WHERE sequence_key=%lu AND column_id=%lu AND truncate_version=%ld",
                    table_name, sync_value, new_seq_value,
                    table_id, column_id, inner_autoinc_version))) {
        } else if (OB_FAIL((trans.write(sql.ptr(), affected_rows)))) {
        } else if (!is_single_row(affected_rows)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected error", K(affected_rows), K(ret));
        } else {
        }

        // commit transaction or rollback
        if (OB_SUCC(ret)) {
          if (OB_FAIL(trans.end(true))) {
          }
        } else {
          int err = OB_SUCCESS;
          if (OB_SUCCESS != (err = trans.end(false))) {
          }
        }
      } else {
        seq_value = fetch_seq_value;
      }

      // if transactin is started above(but do nothing), end it here
      if (trans.is_started()) {
        if (OB_SUCC(ret)) {
          if (OB_FAIL(trans.end(true))) {
          }
        } else {
          int err = OB_SUCCESS;
          if (OB_SUCCESS != (err = trans.end(false))) {
          }
        }
      }
    }
  }
  return ret;
}

int ObAutoIncInnerTableProxy::read_and_push_inner_table(const AutoincKey &key,
                                                        const uint64_t max_value,
                                                        const uint64_t cache_end,
                                                        const int64_t autoinc_version,
                                                        bool &is_valid,
                                                        uint64_t &new_end)
{
  int ret = OB_SUCCESS;

  const uint64_t table_id = key.table_id_;
  const uint64_t column_id = key.column_id_;
  is_valid = false;
  ObMySQLTransaction trans;
  ObSqlString sql;
  bool with_snap_shot = true;
  uint64_t fetch_seq_value = 0;
  int64_t inner_autoinc_version = OB_INVALID_VERSION;
  uint64_t sync_value = 0;
  if (OB_ISNULL(mysql_proxy_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("mysql proxy is null", K(ret));
  } else if (OB_FAIL(trans.start(mysql_proxy_, with_snap_shot))) {
  } else {

    const char *table_name = OB_ALL_AUTO_INCREMENT_TNAME;
    int64_t fetch_table_id = OB_INVALID_ID;
    if (OB_FAIL(sql.assign_fmt(" SELECT sequence_value, truncate_version FROM %s WHERE sequence_key = %lu"
                               " AND column_id = %lu FOR UPDATE",
                               table_name,
                               ObSchemaUtils::get_extract_schema_id(table_id),
                               column_id))) {
    }
    if (OB_SUCC(ret)) {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ObMySQLResult *result = NULL;
        ObISQLClient *sql_client = &trans;
        auto &sql_client_retry_weak = *sql_client;
        if (OB_FAIL(sql_client_retry_weak.read(res, sql.ptr()))) {
        } else if (NULL == (result = res.get_result())) {
          LOG_WARN("failed to get result", K(ret));
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(result->next())) {
          LOG_WARN("failed to get next", K(ret));
          if (OB_ITER_END == ret) {
            // auto-increment column has been deleted
            ret = OB_SCHEMA_ERROR;
            LOG_WARN("failed to get next", K(ret));
          }
        } else if (OB_FAIL(result->get_uint(static_cast<int64_t>(0), fetch_seq_value))) {
        } else if (OB_FAIL(result->get_int(static_cast<int64_t>(1), inner_autoinc_version))) {
        }
        if (OB_SUCC(ret)) {
          int tmp_ret = OB_SUCCESS;
          if (OB_ITER_END != (tmp_ret = result->next())) {
            if (OB_SUCCESS == tmp_ret) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("more than one row", K(ret), K(table_id), K(column_id));
            } else {
              ret = tmp_ret;
              LOG_WARN("fail to iter next row", K(ret), K(table_id), K(column_id));
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (autoinc_version != inner_autoinc_version) {
        is_valid = false;
      } else if (cache_end == fetch_seq_value && cache_end == max_value) {
        // the column reach max value, keep the maximum value unchanged
        is_valid = true;
        new_end = max_value;
      } else if (cache_end == fetch_seq_value - 1) {
        // The cache is continuous and the verification passes.
        is_valid = true;
        uint64_t new_seq_value = fetch_seq_value;
        if (fetch_seq_value >= max_value) {
          new_end = max_value;
        } else {
          new_end = fetch_seq_value;
          new_seq_value += 1;
          // push new seq value to inner table
          int64_t affected_rows = 0;
          if (OB_FAIL(sql.assign_fmt(
                      "UPDATE %s SET sequence_value = %lu, gmt_modified = now(6) "
                      "WHERE sequence_key=%lu AND column_id=%lu AND truncate_version=%ld",
                      table_name, new_seq_value,
                      table_id, column_id, inner_autoinc_version))) {
          } else if (OB_FAIL((trans.write(sql.ptr(), affected_rows)))) {
          } else if (!is_single_row(affected_rows)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected error", K(affected_rows), K(ret));
          } else {
          }

          // commit transaction or rollback
          if (OB_SUCC(ret)) {
            if (OB_FAIL(trans.end(true))) {
            }
          } else {
            int err = OB_SUCCESS;
            if (OB_SUCCESS != (err = trans.end(false))) {
            }
          }
        }
      }

      // if transactin is started above(but do nothing), end it here
      if (trans.is_started()) {
        if (OB_SUCC(ret)) {
          if (OB_FAIL(trans.end(true))) {
          }
        } else {
          int err = OB_SUCCESS;
          if (OB_SUCCESS != (err = trans.end(false))) {
          }
        }
      }
    }
  }
  return ret;
}

}//end namespace share
}//end namespace oceanbase
