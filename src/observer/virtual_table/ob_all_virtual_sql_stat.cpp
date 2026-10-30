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

#define USING_LOG_PREFIX SQL_ENG

#include "ob_all_virtual_sql_stat.h"
#include "lib/utility/ob_mod_define.h"
#include "share/rc/ob_server_runtime.h"
#include "share/rc/ob_server_runtime.h"
#include "share/rc/ob_context.h"
#include "share/ob_server_struct.h"
#include "observer/ob_server_runtime_access.h"
#include "sql/ob_sql.h"
#include "sql/plan_cache/ob_plan_cache.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::observer;
using namespace oceanbase::share;

int ObGetAllSqlStatCacheIdOp::operator()(common::hash::HashMapPair<ObCacheObjID, ObILibCacheObject *> &entry)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(key_array_) || OB_ISNULL(entry.second)) {
    ret = common::OB_NOT_INIT;
  } else if (entry.second->get_ns() == ObLibCacheNameSpace::NS_SQLSTAT ||
             entry.second->get_ns() == ObLibCacheNameSpace::NS_CRSR) {
    if (!entry.second->added_lc()) {
      // do nothing
    } else if (OB_FAIL(key_array_->push_back(entry.first))) {
    }
  }
  return ret;
}

ObAllVirtualSqlStatIter::ObAllVirtualSqlStatIter() :
  allocator_(nullptr),
  done_(false),
  tmp_sql_stat_map_(),
  sql_stat_cache_id_array_(),
  sql_stat_cache_id_array_idx_(0)
{
}

void ObAllVirtualSqlStatIter::destroy()
{
  reset();
}

void ObAllVirtualSqlStatIter::reset()
{
  done_ = false;
  tmp_sql_stat_map_.destroy();
  sql_stat_cache_id_array_.reset();
  sql_stat_cache_id_array_idx_ = 0;
}

int ObAllVirtualSqlStatIter::init(ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(allocator)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const int64_t default_bucket_num  = 64;
    if (OB_FAIL(tmp_sql_stat_map_.create(default_bucket_num, ObMemAttr("TmpSqlStatMgr")))) {
    } else {
      allocator_ = allocator;
    }
  }
  return ret;
}

int ObAllVirtualSqlStatIter::get_next_batch_sql_stat()
{
  int ret = OB_SUCCESS;
  if (done_) {
    ret = OB_ITER_END;
  } else if (OB_FAIL(tmp_sql_stat_map_.clear())) {
  } else if (FALSE_IT(sql_stat_cache_id_array_.reuse())) {
  } else {
    if (!share::g_server_modules_ready) {
      done_ = true;
      ret = OB_ITER_END;
    } else {
      ObReqTimeGuard req_timeinfo_guard;
      ObPlanCache* plan_cache = ::oceanbase::share::server_service<::oceanbase::sql::ObPlanCache>();
      if (OB_NOT_NULL(plan_cache)) {
        ObGetAllSqlStatCacheIdOp op(&sql_stat_cache_id_array_);
        if (OB_FAIL(plan_cache->foreach_cache_obj(op))) {
        }
      } else {
      }

      if (OB_SUCC(ret)) {
        if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>())) {
          ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->for_each_session(*this);
        }
      }
      done_ = true;
    }
  }
  return ret;
}

bool ObAllVirtualSqlStatIter::operator()(sql::ObSQLSessionMgr::Key key, ObSQLSessionInfo *sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (false == sess_info->is_valid()) {
    // do nothing
  } else if (ObSQLSessionState::QUERY_ACTIVE != sess_info->get_session_state()) {
    // do nothing
  } else {
    // WARNNIGN!!!
    // Access to things like cur_sql_ctx_ and cur_plan is forbidden, 
    // these pointers are not guaranteed to be thread-safe and risk CORE!
    ObSqlStatRecordKey key;
    sess_info->get_cur_sql_id(key.sql_id_, sizeof(key.sql_id_));
    key.set_plan_hash(sess_info->get_current_plan_hash()); 

    ObExecutingSqlStatRecord &executing_sql_stat_record = sess_info->get_executing_sql_stat_record();
    ObExecutedSqlStatRecord *value = nullptr;
    {
      if (!key.is_valid()) {
        // do nothing
      } else if (OB_ISNULL(get_observer_sql_engine())) {
        ret = OB_NOT_INIT;
      } else if (OB_FAIL(executing_sql_stat_record.record_sqlstat_end_value(
                     get_observer_sql_engine()->get_query_runtime_environment()))) {
      } else if (OB_SUCC(tmp_sql_stat_map_.get_refactored(key, value))) {
        if (OB_FAIL(value->sum_stat_value(executing_sql_stat_record))) {
        }
      } else if (OB_HASH_NOT_EXIST == ret) {
        void *buf = nullptr;
        ObString sql = ObString::make_empty_string();
        if (obmysql::COM_QUERY == sess_info->get_mysql_cmd() ||
            obmysql::COM_STMT_EXECUTE == sess_info->get_mysql_cmd() ||
            obmysql::COM_STMT_PREPARE == sess_info->get_mysql_cmd()) {
          sql = sess_info->get_current_query_string();
        }

        if (OB_ISNULL(allocator_)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(buf = allocator_->alloc(sizeof(ObExecutedSqlStatRecord), ObMemAttr("TmpSqlStatMgr")))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else if (FALSE_IT(value = new(buf) ObExecutedSqlStatRecord())) {
        } else if (OB_FAIL(value->get_sql_stat_info().init(key, *sess_info, sql, nullptr /*phy_plan*/))) {
        } else if (OB_FAIL(value->sum_stat_value(executing_sql_stat_record))) {
        } else if (OB_FAIL(tmp_sql_stat_map_.set_refactored(key, value))) {
        }
      } else {
      }
    }
  }
  return true;
}

int ObAllVirtualSqlStatIter::get_next_sql_stat (
  sql::ObExecutedSqlStatRecord &sql_stat_value)
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret) && sql_stat_cache_id_array_idx_ >= sql_stat_cache_id_array_.count() && tmp_sql_stat_map_.empty()) {
    if (OB_FAIL(get_next_batch_sql_stat())) {
      if (OB_ITER_END != ret) {
      }
    } else {
      sql_stat_cache_id_array_idx_ = 0;
    }
  }

  if (OB_SUCC(ret)) {
    if (!share::g_server_modules_ready) {
      ret = OB_ITER_END;
    } else {
      if (sql_stat_cache_id_array_idx_ < sql_stat_cache_id_array_.count()) {
        ObReqTimeGuard req_timeinfo_guard;
        ObPlanCache* plan_cache = ::oceanbase::share::server_service<::oceanbase::sql::ObPlanCache>();
        if (OB_NOT_NULL(plan_cache)) {
          uint64_t cur_sql_stat_cache_id = sql_stat_cache_id_array_.at(sql_stat_cache_id_array_idx_);
          sql_stat_cache_id_array_idx_++;
          ObCacheObjGuard guard;
          int tmp_ret = plan_cache->ref_cache_obj(cur_sql_stat_cache_id, guard);
          if (OB_HASH_NOT_EXIST == tmp_ret) {
            //do nothing;
          } else if (OB_SUCCESS != tmp_ret) {
            ret = tmp_ret;
          } else if (OB_ISNULL(guard.get_cache_obj())) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            bool is_succ_get_stat_value = false;
            const ObExecutedSqlStatRecord *tmp_sql_stat_value = nullptr;
            if (ObLibCacheNameSpace::NS_SQLSTAT == guard.get_cache_obj()->get_ns()) {
              ObSqlStatRecordObj *cache_obj = static_cast<ObSqlStatRecordObj *>(guard.get_cache_obj());
              if (OB_NOT_NULL(cache_obj) && OB_NOT_NULL(cache_obj->get_record_value()) && 
                  cache_obj->get_record_value()->get_key().is_valid()) {
                tmp_sql_stat_value = cache_obj->get_record_value();
                is_succ_get_stat_value = true;
              } 
            } else if (ObLibCacheNameSpace::NS_CRSR == guard.get_cache_obj()->get_ns()) {
              ObPhysicalPlan *plan = static_cast<ObPhysicalPlan *>(guard.get_cache_obj());
              if (OB_NOT_NULL(plan) && plan->sql_stat_record_value_.get_key().is_valid()) {
                tmp_sql_stat_value = &(plan->sql_stat_record_value_);
                is_succ_get_stat_value = true;
              } 
            } 
            if (OB_SUCC(ret) && is_succ_get_stat_value) {
              if (OB_ISNULL(tmp_sql_stat_value)) {
                // continue, do nothing
              } else {
                ObExecutedSqlStatRecord *value = nullptr;
                if (OB_FAIL(sql_stat_value.assign(*(tmp_sql_stat_value)))) {
                } else {
                  int tmp_ret = tmp_sql_stat_map_.get_refactored(sql_stat_value.get_key(), value);
                  if (OB_HASH_NOT_EXIST == tmp_ret) {
                    // do nothing
                  } else if (OB_SUCCESS == tmp_ret) {
                    if (OB_FAIL(sql_stat_value.sum_stat_value(*value))) {
                    } else if (OB_FAIL(tmp_sql_stat_map_.erase_refactored(sql_stat_value.get_key()))) {
                    }
                  } else {
                    ret = OB_ERR_UNEXPECTED;
                    LOG_WARN("fail to get from refactored", KR(ret), KR(tmp_ret));
                  }
                }
              }
            }

          }
        }
      } else if (!tmp_sql_stat_map_.empty()) {
        if (OB_ISNULL(tmp_sql_stat_map_.begin()->second)) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          const ObExecutedSqlStatRecord *tmp_sql_stat_value = tmp_sql_stat_map_.begin()->second;
          if (!tmp_sql_stat_value->get_key().is_valid()) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_FAIL(sql_stat_value.assign(*(tmp_sql_stat_value)))) {
          } else if (OB_FAIL(tmp_sql_stat_map_.erase_refactored(sql_stat_value.get_key()))) {
          }
        }
      }
    }
  }
  return ret;
}

ObAllVirtualSqlStat::ObAllVirtualSqlStat() :
  ipstr_(), port_(0), iter_()
{}

void ObAllVirtualSqlStat::destroy()
{
  ipstr_.reset();
  iter_.destroy();
}

void ObAllVirtualSqlStat::reset()
{
  port_ = 0;
  ipstr_.reset();
  iter_.reset();
  start_to_read_ = false;
}

int ObAllVirtualSqlStat::get_server_ip_and_port()
{
  int ret = OB_SUCCESS;
  char ipbuf[common::OB_IP_STR_BUFF];
  const common::ObAddr &addr = GCTX.self_addr();
  if (!addr.ip_to_string(ipbuf, sizeof(ipbuf))) {
    SERVER_LOG(ERROR, "ip to string failed");
    ret = OB_ERR_UNEXPECTED;
  } else {
    ipstr_ = ObString::make_string(ipbuf);
    if (OB_FAIL(ob_write_string(*allocator_, ipstr_, ipstr_))) {
    }
    port_ = addr.get_port();
  }
  return ret;
}

int ObAllVirtualSqlStat::fill_row(
  const ObExecutedSqlStatRecord *sql_stat_record,
  common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  DatumValue *cells = cur_row_.cells_;

  for (int64_t cell_idx = 0; OB_SUCC(ret) && cell_idx < output_column_ids_.count(); ++cell_idx) {
    uint64_t col_id = output_column_ids_.at(cell_idx);
    switch(col_id) {
      case SQL_ID: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString::make_string(sql_stat_record->get_key().sql_id_)) : ret);
        cells[cell_idx].set_collation_type(
          ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case PLAN_ID: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_sql_stat_info().get_plan_id()) : ret);
        break;
      }
      case PLAN_HASH: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_uint64(cells, cur_row_.count_, cell_idx, sql_stat_record->get_key().plan_hash_) : ret);
        break;
      }
      case PLAN_TYPE: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_sql_stat_info().get_plan_type()) : ret);
        break;
      }
      case QUERY_SQL: {
        ObCollationType src_cs_type = ObCharset::is_valid_collation(sql_stat_record->get_sql_stat_info().get_sql_cs_type()) ?
                sql_stat_record->get_sql_stat_info().get_sql_cs_type() : ObCharset::get_system_collation();
        ObString src_string(static_cast<int32_t>(STRLEN(sql_stat_record->get_sql_stat_info().get_query_sql())), sql_stat_record->get_sql_stat_info().get_query_sql());
        ObString dst_string;
        if (OB_FAIL(ObCharset::charset_convert(row_calc_buf_,
                                                      src_string,
                                                      src_cs_type,
                                                      ObCharset::get_system_collation(),
                                                      dst_string,
                                                      ObCharset::REPLACE_UNKNOWN_CHARACTER))) {
        } else {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_lob_view(cells, cur_row_.count_, cell_idx, ObLongTextType, ObString(min(dst_string.length(), 1024), dst_string.ptr())) : ret);
          cells[cell_idx].set_collation_type(ObCharset::get_default_collation(
                                              ObCharset::get_default_charset()));
        }
        break;
      }
      case SQL_TYPE: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_sql_stat_info().get_sql_type()) : ret);
        break;
      }
      case MODULE: {
        cells[cell_idx].set_null(); // impl. later
        break;
      }
      case ACTION: {
        cells[cell_idx].set_null(); // impl. later
        break;
      }
      case PARSING_DB_ID: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_sql_stat_info().get_parsing_db_id()) : ret);
        break;
      }
      case PARSING_DB_NAME: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ObString(static_cast<ObString::obstr_size_t>(STRLEN(sql_stat_record->get_sql_stat_info().get_parsing_db_name())), sql_stat_record->get_sql_stat_info().get_parsing_db_name())) : ret);
        cells[cell_idx].set_collation_type(
          ObCharset::get_default_collation(ObCharset::get_default_charset()));
        break;
      }
      case PARSING_USER_ID: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_sql_stat_info().get_parsing_user_id()) : ret);
        break;
      }
      case EXECUTIONS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_executions_total()) : ret);
        break;
      }
      case EXECUTIONS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_executions_delta()) : ret);
        break;
      }
      case DISK_READS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_disk_reads_total()) : ret);
        break;
      }
      case DISK_READS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_disk_reads_delta()) : ret);
        break;
      }
      case BUFFER_GETS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_buffer_gets_total()) : ret);
        break;
      }
      case BUFFER_GETS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_buffer_gets_delta()) : ret);
        break;
      }
      case ELAPSED_TIME_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_elapsed_time_total()) : ret);
        break;
      }
      case ELAPSED_TIME_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_elapsed_time_delta()) : ret);
        break;
      }
      case CPU_TIME_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_cpu_time_total()) : ret);
        break;
      }
      case CPU_TIME_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_cpu_time_delta()) : ret);
        break;
      }
      case CCWAIT_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_ccwait_total()) : ret);
        break;
      }
      case CCWAIT_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_ccwait_delta()) : ret);
        break;
      }
      case USERIO_WAIT_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_userio_wait_total()) : ret);
        break;
      }
      case USERIO_WAIT_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_userio_wait_delta()) : ret);
        break;
      }
      case APWAIT_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_apwait_total()) : ret);
        break;
      }
      case APWAIT_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_apwait_delta()) : ret);
        break;
      }
      case PHYSICAL_READ_REQUESTS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_physical_read_requests_total()) : ret);
        break;
      }
      case PHYSICAL_READ_REQUESTS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_physical_read_requests_delta()) : ret);
        break;
      }
      case PHYSICAL_READ_BYTES_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_physical_read_bytes_total()) : ret);
        break;
      }
      case PHYSICAL_READ_BYTES_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_physical_read_bytes_delta()) : ret);
        break;
      }
      case WRITE_THROTTLE_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_write_throttle_total()) : ret);
        break;
      }
      case WRITE_THROTTLE_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_write_throttle_delta()) : ret);
        break;
      }
      case ROWS_PROCESSED_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_rows_processed_total()) : ret);
        break;
      }
      case ROWS_PROCESSED_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_rows_processed_delta()) : ret);
        break;
      }
      case MEMSTORE_READ_ROWS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_memstore_read_rows_total()) : ret);
        break;
      }
      case MEMSTORE_READ_ROWS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_memstore_read_rows_delta()) : ret);
        break;
      }
      case MINOR_SSSTORE_READ_ROWS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_minor_ssstore_read_rows_total()) : ret);
        break;
      }
      case MINOR_SSSTORE_READ_ROWS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_minor_ssstore_read_rows_delta()) : ret);
        break;
      }
      case MAJOR_SSSTORE_READ_ROWS_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_major_ssstore_read_rows_total()) : ret);
        break;
      }
      case MAJOR_SSSTORE_READ_ROWS_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_major_ssstore_read_rows_delta()) : ret);
        break;
      }
      case RPC_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_rpc_total()) : ret);
        break;
      }
      case RPC_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_rpc_delta()) : ret);
        break;
      }
      case FETCHES_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_fetches_total()) : ret);
        break;
      }
      case FETCHES_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_fetches_delta()) : ret);
        break;
      }
      case RETRY_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_retry_total()) : ret);
        break;
      }
      case RETRY_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_retry_delta()) : ret);
        break;
      }
      case PARTITION_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_partition_total()) : ret);
        break;
      }
      case PARTITION_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_partition_delta()) : ret);
        break;
      }
      case NESTED_SQL_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_nested_sql_total()) : ret);
        break;
      }
      case NESTED_SQL_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_nested_sql_delta()) : ret);
        break;
      }
      case SOURCE_IP: {
        if (sql_stat_record->get_key().source_addr_.is_valid()) {
          char ipbuf[common::OB_IP_STR_BUFF];
          ObString source_ip_str;
          const common::ObAddr &addr = sql_stat_record->get_key().source_addr_;
          if (!addr.ip_to_string(ipbuf, sizeof(ipbuf))) {
            SERVER_LOG(ERROR, "ip to string failed");
            ret = OB_ERR_UNEXPECTED;
          } else {
            source_ip_str = ObString::make_string(ipbuf);
            if (OB_FAIL(ob_write_string(*allocator_, source_ip_str, source_ip_str))) {
            } else {
              (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, source_ip_str) : ret);
              cells[cell_idx].set_collation_type(
                ObCharset::get_default_collation(ObCharset::get_default_charset()));
            }
          }
        } else {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_varchar_view(cells, cur_row_.count_, cell_idx, ipstr_) : ret);
          cells[cell_idx].set_collation_type(
            ObCharset::get_default_collation(ObCharset::get_default_charset()));
        }
        
        break;
      }
      case SOURCE_PORT: {
        if (sql_stat_record->get_key().source_addr_.is_valid()) {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_key().source_addr_.get_port()) : ret);
        } else {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, port_) : ret);
        }
        break;
      }
      case FIRST_LOAD_TIME: {
        int64_t first_load_timestamp = sql_stat_record->get_sql_stat_info().get_first_load_time();
        if (first_load_timestamp == 0) {
          cells[cell_idx].set_null();
        } else {
          (OB_SUCC(ret) ? ret = DatumValueBuffer::set_timestamp(cells, cur_row_.count_, cell_idx, first_load_timestamp) : ret);
        }
        break;
      }
      case PLAN_CACHE_HIT_TOTAL: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_plan_cache_hit_total()) : ret);
        break;
      }
      case PLAN_CACHE_HIT_DELTA: {
        (OB_SUCC(ret) ? ret = DatumValueBuffer::set_int(cells, cur_row_.count_, cell_idx, sql_stat_record->get_plan_cache_hit_delta()) : ret);
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected column id", K(col_id));
      }
    }
  }
  if (OB_SUCC(ret)) {
    row = &cur_row_;
  }
  return ret;
}

int ObAllVirtualSqlStat::inner_get_next_row(common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  if (!start_to_read_) {
    if (OB_FAIL(iter_.init(allocator_))) {
    } else {
      start_to_read_ = true;
      if (OB_FAIL(get_server_ip_and_port())) {
      }
    }
  }

  // clear last sql stat record
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(last_sql_stat_record_)) {
      // is nullptr , do nothing
    } else {
      last_sql_stat_record_->~ObExecutedSqlStatRecord();
      allocator_->free(last_sql_stat_record_);
    }
  }

  if (OB_SUCC(ret)) {
    ObExecutedSqlStatRecord *sql_stat_record = nullptr;
    void *buf = nullptr;
    if (OB_ISNULL(allocator_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(buf = allocator_->alloc(sizeof(ObExecutedSqlStatRecord), ObMemAttr("TmpSqlStat")))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(sql_stat_record = new(buf) ObExecutedSqlStatRecord())) {
    } else {
      while (OB_SUCC(ret) && !sql_stat_record->get_key().is_valid()) {
        if (OB_FAIL(iter_.get_next_sql_stat(*sql_stat_record))) {
          if (OB_ITER_END != ret) {
          }
        }
      } // end while

      if (OB_SUCC(ret)) {
        if (sql_stat_record->get_key().is_valid()) {
          if (OB_FAIL(fill_row(sql_stat_record, row))) {
          } else {
            last_sql_stat_record_ = sql_stat_record;
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
  }
  return ret;
}
