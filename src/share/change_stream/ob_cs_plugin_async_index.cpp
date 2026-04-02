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
#include "lib/oblog/ob_log_module.h"
#include "share/change_stream/ob_cs_plugin_async_index.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_tenant_schema_service.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/tablet/ob_tablet_to_ls_operator.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_column_schema.h"
#include "share/vector_index/ob_vector_index_util.h"
#include "share/vector_index/ob_plugin_vector_index_service.h"
#include "share/vector_index/ob_plugin_vector_index_utils.h"
#include "lib/vector/ob_vector_util.h"
#include "share/rc/ob_tenant_base.h"
#include "storage/memtable/ob_memtable_mutator.h"
#include "storage/blocksstable/ob_row_reader.h"
#include "storage/blocksstable/ob_storage_datum.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/tx_storage/ob_access_service.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/tx_storage/ob_ls_handle.h"
#include "storage/ls/ob_ls.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "share/table/ob_table_util.h"
#include "share/ob_server_struct.h"
#include "share/location_cache/ob_location_service.h"
#include "lib/mysqlclient/ob_isql_connection.h"
#include "storage/tx/ob_trans_define.h"
#include "observer/ob_inner_sql_connection.h"
#include "sql/das/ob_das_factory.h"
#include "sql/das/ob_das_insert_op.h"
#include "lib/allocator/ob_allocator.h"
#include "sql/das/ob_das_dml_ctx_define.h"
#include "share/schema/ob_table_param.h"
#include "lib/time/ob_time_utility.h"
#include "share/ob_server_struct.h"

namespace oceanbase
{
using namespace common;
namespace share
{

// ---------------------------------------------------------------------------
// ObCSAsyncIndexProcessor: per-thread processor (stack-allocated per process() call)
// ---------------------------------------------------------------------------

ObCSAsyncIndexProcessor::ObCSAsyncIndexProcessor(ObCSExecCtx &ctx)
  : ctx_(ctx)
{
}

int ObCSAsyncIndexProcessor::init_schema_guard_()
{
  int ret = common::OB_SUCCESS;
  ObMultiVersionSchemaService *schema_service =
      MTL(ObTenantSchemaService *) != nullptr ? MTL(ObTenantSchemaService *)->get_schema_service() : nullptr;
  if (OB_ISNULL(schema_service)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", K(ret));
  } else if (OB_FAIL(schema_service->get_tenant_schema_guard(
      MTL_ID(), schema_guard_, ctx_.schema_version_, common::OB_INVALID_VERSION,
      ObMultiVersionSchemaService::RefreshSchemaMode::FORCE_FALLBACK))) {
    LOG_WARN("get_tenant_schema_guard failed", K(ret), K(ctx_.schema_version_));
  }
  return ret;
}

ObCSAsyncIndexProcessor::~ObCSAsyncIndexProcessor()
{
}

int ObCSAsyncIndexProcessor::get_or_cache_vec_index_info_(
    uint64_t table_id,
    schema::ObSchemaGetterGuard &schema_guard,
    const common::ObIArray<ObCSVecIndexInfo> *&out_vec_infos_ptr)
{
  int ret = common::OB_SUCCESS;
  out_vec_infos_ptr = nullptr;

  const common::ObSEArray<ObCSVecIndexInfo, 4> *cached = vec_index_cache_.get(table_id);
  if (cached != nullptr) {
    out_vec_infos_ptr = cached;
  } else {
    common::ObSEArray<ObCSVecIndexInfo, 4> resolved_infos;
    ret = resolve_vector_index_info_(table_id, schema_guard, resolved_infos);
    if (OB_FAIL(ret)) {
      LOG_WARN("fail to resolve vector index info", K(ret), K(table_id));
    } else {
      if (OB_FAIL(vec_index_cache_.set_refactored(table_id, resolved_infos))) {
        LOG_WARN("fail to cache resolved vec index infos", K(ret), K(table_id));
      } else {
        out_vec_infos_ptr = vec_index_cache_.get(table_id);
      }
    }
  }
  return ret;
}

int ObCSAsyncIndexProcessor::process(common::ObIArray<ObCSRow> &rows)
{
  int ret = common::OB_SUCCESS;
  if (rows.count() == 0) {
    // nothing to process
  } else if (!vec_index_cache_.created() && OB_FAIL(vec_index_cache_.create(64, "CSAsyncIdxCa"))) {
    LOG_WARN("create vec_index_cache failed", K(ret));
  } else if (!tablet_to_table_.created() && OB_FAIL(tablet_to_table_.create(64, "CSTabletToTb"))) {
    LOG_WARN("create tablet_to_table failed", K(ret));
  } else if (OB_FAIL(init_schema_guard_())) {
    LOG_WARN("init_schema_guard_ failed", K(ret));
  } else {
    common::ObSEArray<TabletEventGroup, 16> groups;
    common::hash::ObHashMap<GroupKey, int64_t, common::hash::NoPthreadDefendMode> key_to_group;
    if (OB_FAIL(key_to_group.create(64, "CSAsyncGrp"))) {
      LOG_WARN("create key_to_group map failed", K(ret));
    }

    // Step 1 — iterate rows, look up vec_infos, build events, group by (tablet_id, index_id_table_id)
    for (int64_t i = 0; OB_SUCC(ret) && i < rows.count(); ++i) {
      const ObCSRow &row = rows.at(i);
      uint64_t data_table_id = common::OB_INVALID_ID;
      bool need_process  = false;
      if (OB_FAIL(resolve_table_id_from_tablet_id_(row.tablet_id_, data_table_id))) {
        if (common::OB_TABLET_NOT_EXIST == ret) {
          LOG_INFO("skip non-existent tablet_id", K(row.tablet_id_), K(row));
          ret = common::OB_SUCCESS;
        } else {
          LOG_WARN("resolve_table_id_from_tablet_id_ failed", K(ret), K(row.tablet_id_), K(row));
        }
      } else if (common::OB_INVALID_ID == data_table_id) {
        LOG_INFO("skip invalid tablet_id", K(row.tablet_id_), K(row));
      } else {
        const common::ObIArray<ObCSVecIndexInfo> *vec_infos_ptr = nullptr;
        if (OB_FAIL(get_or_cache_vec_index_info_(data_table_id, schema_guard_, vec_infos_ptr))) {
          LOG_WARN("get_or_cache_vec_index_info_ failed", K(ret), K(data_table_id), K(row));
        } else if (OB_ISNULL(vec_infos_ptr) || vec_infos_ptr->count() <= 0) {
        } else {
          for (int64_t vi = 0; OB_SUCC(ret) && vi < vec_infos_ptr->count(); ++vi) {
            const ObCSVecIndexInfo &vec_info = vec_infos_ptr->at(vi);
            ObASyncIndexEvent event;
            bool skip_row = false;

            if (OB_FAIL(build_event_from_row_(row, vec_info, event, skip_row))) {
              LOG_WARN("build_event_from_row_ failed", K(ret), K(row), K(vec_info));
            } else if (skip_row) {
              continue;
            } else {
              const GroupKey key(row.tablet_id_, vec_info.index_id_table_id_);
              int64_t group_idx = -1;
              ret = key_to_group.get_refactored(key, group_idx);
              if (common::OB_HASH_NOT_EXIST == ret) {
                TabletEventGroup g;
                g.tablet_id_ = row.tablet_id_;
                g.table_id_ = data_table_id;
                g.vec_info_ = vec_info;
                if (OB_FAIL(groups.push_back(g))) {
                  LOG_WARN("push back TabletEventGroup failed", K(ret), K(row), K(vec_info));
                } else {
                  group_idx = groups.count() - 1;
                  if (OB_FAIL(key_to_group.set_refactored(key, group_idx))) {
                    LOG_WARN("set key_to_group map failed", K(ret), K(key), K(group_idx));
                  }
                }
                ret = common::OB_SUCCESS;
              } else if (OB_SUCCESS != ret) {
                LOG_WARN("key_to_group get_refactored failed", K(ret), K(key));
              }

              if (OB_SUCC(ret)) {
                if (group_idx < 0 || group_idx >= groups.count()) {
                  ret = common::OB_ERR_UNEXPECTED;
                  LOG_WARN("invalid group_idx", K(ret), K(group_idx), K(groups.count()), K(key));
                } else if (groups.at(group_idx).table_id_ != data_table_id) {
                  ret = common::OB_ERR_UNEXPECTED;
                  LOG_WARN("group key maps to different table_id in this batch",
                           K(ret), K(key),
                           "old_table_id", groups.at(group_idx).table_id_,
                           "new_table_id", data_table_id);
                } else if (groups.at(group_idx).vec_info_.index_id_table_id_ != vec_info.index_id_table_id_) {
                  ret = common::OB_ERR_UNEXPECTED;
                  LOG_WARN("group vec_info mismatch", K(ret), K(key), K(vec_info), K(groups.at(group_idx).vec_info_));
                } else if (OB_FAIL(groups.at(group_idx).events_.push_back(event))) {
                  LOG_WARN("push back event failed", K(ret), K(event), K(key));
                } else {
                  // Successfully built event and added to corresponding tablet group
                  need_process = true;
                }
              }
            }

          }
        }
      }
      if (need_process) {
        ATOMIC_INC(&ctx_.process_cnt_);
      }
      //FLOG_INFO("process row for async index", K(row), K(need_process));
    }

    // Step 2 — for each tablet group: batch insert into index_id_table, then write into vsag
    for (int64_t gi = 0; OB_SUCC(ret) && gi < groups.count(); ++gi) {
      TabletEventGroup &g = groups.at(gi);
      if (g.events_.count() <= 0) {
      } else if (OB_FAIL(insert_vector_index_log_batch_(g.events_, g.vec_info_))) {
        handle_insert_or_write_error_(ret, g, true /* is_insert */);
      } else if (OB_FAIL(write_to_vsag_(g.events_, g.vec_info_))) {
        handle_insert_or_write_error_(ret, g, false /* is_insert */);
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// ObCSAsyncIndexProcessor: implementation methods
// ---------------------------------------------------------------------------

void ObCSAsyncIndexProcessor::handle_insert_or_write_error_(
    int &ret,
    const TabletEventGroup &g,
    bool is_insert)
{
  const char *op_name = is_insert ? "insert_vector_index_log" : "write_to_vsag";
  if (common::OB_TABLET_NOT_EXIST == ret) {
    // Tablet or index table was dropped/truncated by DDL, skip this group
    LOG_INFO("skip tablet group due to DDL deletion",
             K(ret), K(op_name),
             K(g.tablet_id_), K(g.table_id_),
             "index_id_table", g.vec_info_.index_id_table_id_,
             "event_count", g.events_.count());
    ret = common::OB_SUCCESS;
  } else {
    // Real system error - should propagate and fail the batch
    LOG_WARN("failed to process tablet group",
             K(ret), K(op_name),
             K(g.tablet_id_), K(g.table_id_),
             "index_id_table", g.vec_info_.index_id_table_id_,
             "delta_buffer_table", g.vec_info_.delta_buffer_table_id_,
             "event_count", g.events_.count(),
             "first_event", g.events_.count() > 0 ? g.events_.at(0) : ObASyncIndexEvent());
  }
}

int ObCSAsyncIndexProcessor::resolve_vector_index_info_(
    uint64_t table_id,
    schema::ObSchemaGetterGuard &schema_guard,
    common::ObIArray<ObCSVecIndexInfo> &vec_infos)
{
  int ret = common::OB_SUCCESS;
  vec_infos.reset();
  const uint64_t tenant_id = MTL_ID();
  const schema::ObTableSchema *data_table_schema = nullptr;
  bool need_resolve = true;

  if (OB_FAIL(schema_guard.get_table_schema(tenant_id, table_id, data_table_schema))) {
    LOG_WARN("fail to get data table schema", K(ret), K(tenant_id), K(table_id));
  } else if (OB_ISNULL(data_table_schema)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("data table schema is null", K(ret), K(table_id));
  } else if (!data_table_schema->is_user_table() || !data_table_schema->is_heap_organized_table()) {
    // Skip: not user table or not heap table (async index only supports heap + ASYNC)
    need_resolve = false;
  }

  if (OB_SUCC(ret) && need_resolve) {
    common::ObSEArray<schema::ObAuxTableMetaInfo, 16> simple_index_infos;
    common::ObSEArray<schema::ObColDesc, 16> col_descs;
    if (OB_FAIL(data_table_schema->get_simple_index_infos(simple_index_infos))) {
      LOG_WARN("fail to get simple index infos", K(ret), K(table_id));
    } else if (OB_FAIL(data_table_schema->get_column_ids(col_descs))) {
      LOG_WARN("fail to get column descs", K(ret), K(table_id));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < simple_index_infos.count(); ++i) {
        const uint64_t index_table_id = simple_index_infos.at(i).table_id_;
        const schema::ObTableSchema *index_schema = nullptr;
        if (OB_FAIL(schema_guard.get_table_schema(tenant_id, index_table_id, index_schema))) {
          LOG_WARN("fail to get index table schema", K(ret), K(tenant_id), K(index_table_id));
        } else if (OB_ISNULL(index_schema)) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("index table schema is null", K(ret), K(tenant_id), K(index_table_id));
        } else if (!schema::is_vec_index_id_type(index_schema->get_index_type())) {
          // skip non-vector-index-id tables
        } else {
          uint64_t index_id_table_id = index_table_id;
          common::ObSEArray<uint64_t, 1> vec_col_ids;
          if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_id(
                    *data_table_schema, *index_schema, vec_col_ids))) {
            LOG_WARN("fail to get vector index column id", K(ret), K(table_id), K(index_id_table_id));
          } else if (vec_col_ids.count() <= 0) {
            ret = common::OB_ERR_UNEXPECTED;
            LOG_WARN("no vector column id found for vector index", K(ret), K(table_id), K(index_id_table_id));
          } else {
            // Check SYNC_MODE=ASYNC: only async HNSW indexes on heap table are supported.
            // Note: For HNSW+heap+async, there is no delta_buffer table; index_params come from index_id table.
            // vec_index_id + heap: SYNC_MODE default is ASYNC when not specified (index_id table may have empty params).
            ObString index_params_str = index_schema->get_index_params();
            bool is_async_mode = ObVectorIndexUtil::is_sync_mode_async(index_params_str, true /* is_hnsw_heap_table */);
            if (OB_SUCC(ret) && !is_async_mode) {
              //skip non-async vector index (SYNC_MODE!=ASYNC)
            } else if (OB_SUCC(ret)) {
              for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < vec_col_ids.count(); ++col_idx) {
                int64_t vec_column_id = vec_col_ids.at(col_idx);
                int64_t vec_col_idx = -1;
                for (int64_t j = 0; j < col_descs.count(); ++j) {
                  if (col_descs.at(j).col_id_ == static_cast<uint32_t>(vec_column_id)) {
                    vec_col_idx = j;
                    break;
                  }
                }
                if (vec_col_idx < 0) {
                  ret = common::OB_ERR_UNEXPECTED;
                  LOG_WARN("vector column not found in col_descs", K(ret), K(table_id), K(vec_column_id));
                }

                int64_t dim = 0;
                if (OB_FAIL(ret)) {
                } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_dim(*index_schema, dim))) {
                  LOG_WARN("fail to get vector dimension", K(ret), K(table_id), K(index_id_table_id));
                } else if (dim <= 0) {
                  ret = common::OB_ERR_UNEXPECTED;
                  LOG_WARN("unexpected vector dimension", K(ret), K(table_id), K(dim), K(index_id_table_id));
                }

                uint64_t delta_buffer_table_id = common::OB_INVALID_ID;
                if (OB_SUCC(ret)) {
                  ObString index_id_prefix;
                  if (OB_FAIL(ObPluginVectorIndexUtils::get_vector_index_prefix(*index_schema, index_id_prefix))) {
                    LOG_WARN("fail to get index prefix for index_id table", K(ret), K(index_id_table_id));
                  } else {
                    for (int64_t k = 0; OB_SUCC(ret) && k < simple_index_infos.count(); ++k) {
                      const schema::ObTableSchema *candidate_schema = nullptr;
                      if (OB_FAIL(schema_guard.get_table_schema(tenant_id, simple_index_infos.at(k).table_id_, candidate_schema))) {
                        LOG_WARN("fail to get candidate schema", K(ret));
                      } else if (OB_ISNULL(candidate_schema)) {
                        ret = common::OB_ERR_UNEXPECTED;
                        LOG_WARN("candidate schema is null", K(ret));
                      } else if (schema::is_vec_delta_buffer_type(candidate_schema->get_index_type())) {
                        ObString candidate_prefix;
                        if (OB_FAIL(ObPluginVectorIndexUtils::get_vector_index_prefix(*candidate_schema, candidate_prefix))) {
                          LOG_WARN("fail to get candidate prefix", K(ret));
                        } else if (candidate_prefix == index_id_prefix) {
                          delta_buffer_table_id = simple_index_infos.at(k).table_id_;
                          break;
                        }
                      }
                    }
                    if (OB_SUCC(ret) && delta_buffer_table_id == common::OB_INVALID_ID) {
                      ret = common::OB_ERR_UNEXPECTED;
                      LOG_WARN("delta_buffer table not found for vector index",
                               K(ret), K(table_id), K(index_id_table_id));
                    }
                  }
                }

                if (OB_SUCC(ret)) {
                  ObCSVecIndexInfo vec_info;
                  vec_info.data_table_id_ = table_id;
                  vec_info.index_id_table_id_ = index_id_table_id;
                  vec_info.delta_buffer_table_id_ = delta_buffer_table_id;
                  vec_info.vec_column_id_ = vec_column_id;
                  vec_info.vec_col_idx_ = vec_col_idx;
                  vec_info.index_type_ = index_schema->get_index_type();
                  vec_info.dim_ = dim;
                  if (OB_FAIL(vec_infos.push_back(vec_info))) {
                    LOG_WARN("fail to push back vec_info", K(ret), K(vec_info));
                  }
                }
              }
            }
          }
        }
      }
    }
    // vec_infos.count() == 0: skip (no async vector index); count() > 0: success
  }
  return ret;
}

int ObCSAsyncIndexProcessor::resolve_table_id_from_tablet_id_(
    const common::ObTabletID &tablet_id,
    uint64_t &table_id)
{
  int ret = common::OB_SUCCESS;
  table_id = common::OB_INVALID_ID;
  const uint64_t tenant_id = MTL_ID();

  if (OB_UNLIKELY(!tablet_id.is_valid())) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tablet_id", K(ret), K(tablet_id));
  } else if (tablet_id.is_inner_tablet()) {
    table_id = tablet_id.id();
  } else if (OB_SUCC(tablet_to_table_.get_refactored(tablet_id.id(), table_id))) {
    if (common::OB_INVALID_ID == table_id) {
      ret = common::OB_TABLET_NOT_EXIST;
    }
  } else if (OB_HASH_NOT_EXIST != ret) {
    LOG_WARN("get cache failed", KR(ret));
  } else if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("sql_proxy is null", K(ret), K(tenant_id));
  } else {
    common::ObSEArray<common::ObTabletID, 1> tablet_ids;
    common::ObSEArray<ObTabletToLSInfo, 1> infos;
    if (OB_FAIL(tablet_ids.push_back(tablet_id))) {
      LOG_WARN("fail to push back tablet_id", K(ret), K(tablet_id));
    } else if (OB_FAIL(ObTabletToLSTableOperator::batch_get(
                   *GCTX.sql_proxy_, tenant_id, tablet_ids, infos))) {
      if (common::OB_ITEM_NOT_MATCH == ret) {
        LOG_WARN("tablet not in __all_tablet_to_ls", K(ret), K(tenant_id), K(tablet_id));
        ret = common::OB_TABLET_NOT_EXIST;
        table_id = common::OB_INVALID_ID;
        if (OB_FAIL(tablet_to_table_.set_refactored(tablet_id.id(), common::OB_INVALID_ID))) {
          LOG_WARN("fail to cache invalid tablet_id", K(ret), K(tenant_id), K(tablet_id));
        }
      } else {
        LOG_WARN("fail to get table_id from __all_tablet_to_ls", K(ret), K(tenant_id), K(tablet_id));
      }
    } else if (OB_UNLIKELY(infos.count() != 1)) {
      ret = common::OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected infos count", K(ret), K(infos.count()));
    } else {
      table_id = infos.at(0).get_table_id();
      if (common::OB_INVALID_ID == table_id) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("table_id invalid in __all_tablet_to_ls", K(ret), K(tenant_id), K(tablet_id));
      } else if (OB_FAIL(tablet_to_table_.set_refactored(tablet_id.id(), table_id))) {
        LOG_WARN("set tablet_to_table failed", KR(ret));
      }
    }
  }
  return ret;
}

int ObCSAsyncIndexProcessor::build_event_from_row_(
    const ObCSRow &row,
    const ObCSVecIndexInfo &vec_info,
    ObASyncIndexEvent &event,
    bool &skip)
{
  int ret = common::OB_SUCCESS;
  skip = false;
  event.reset();
  // Step 1: determine DML type -> event type
  char event_type = ObCSAsyncIndexEventType::INSERT;
  if (blocksstable::DF_DELETE == row.dml_flag_) {
    event_type = ObCSAsyncIndexEventType::DELETE;
  } else if (blocksstable::DF_INSERT == row.dml_flag_) {
    event_type = ObCSAsyncIndexEventType::INSERT;
  } else {
    skip = true;  // not INSERT/DELETE (e.g. UPDATE), no need to report as error
  }
  char *vec_data = nullptr;
  int64_t vec_len = 0;

  if (skip) {
    // no-op, fall through to single exit
  } else if (ObCSAsyncIndexEventType::INSERT == event_type
             && (row.new_row_.size_ <= 0 || OB_ISNULL(row.new_row_.data_))) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("INSERT/UPDATE row has no new_row data", K(ret), K(row));
  } else if (ObCSAsyncIndexEventType::INSERT == event_type) {
    int extract_ret = extract_vector_data_(row.new_row_, vec_info, vec_data, vec_len);
    if (common::OB_ERR_UNEXPECTED == extract_ret && OB_ISNULL(vec_data)) {
      // NULL vector: skip writing to index table and vector library
      skip = true;
    } else if (OB_FAIL(extract_ret)) {
      LOG_WARN("fail to extract vector data from new_row", K(ret), K(row), K(vec_info));
    }
  } else if (ObCSAsyncIndexEventType::DELETE == event_type) {
    // For DELETE, check if the old row contains NULL vector
    // If yes, skip because it was never in the index/vector library
    if (row.old_row_.size_ > 0 && OB_NOT_NULL(row.old_row_.data_)) {
      int extract_ret = extract_vector_data_(row.old_row_, vec_info, vec_data, vec_len);
      if (common::OB_ERR_UNEXPECTED == extract_ret && OB_ISNULL(vec_data)) {
        // NULL vector was never in the index, skip DELETE
        skip = true;
      } else if (OB_FAIL(extract_ret)) {
        LOG_WARN("fail to extract vector data from old_row for DELETE", K(ret), K(row), K(vec_info));
      }
    } else {
      // DELETE without old_row data should not happen for heap table, but skip if it does
      skip = true;
    }
  }
  if (!skip && OB_SUCC(ret)) {
    event.tablet_id_ = row.tablet_id_;
    event.table_id_  = vec_info.data_table_id_;
    event.commit_version_ = row.commit_version_;
    event.vid_       = row.heap_pk_;
    event.type_      = event_type;
    event.vec_data_  = vec_data;
    event.vec_data_len_ = vec_len;
  }
  return ret;
}

int ObCSAsyncIndexProcessor::extract_vector_data_(
    const memtable::ObRowData &new_row,
    const ObCSVecIndexInfo &vec_info,
    char *&vec_data,
    int64_t &vec_len)
{
  int ret = common::OB_SUCCESS;
  vec_data = nullptr;
  vec_len = 0;
  if (OB_ISNULL(new_row.data_) || new_row.size_ <= 0) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("new_row data is null or empty", K(ret), K(new_row));
  } else if (vec_info.vec_col_idx_ < 0) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("invalid vec_col_idx", K(ret), K(vec_info));
  }
  // Step 1: use ObRowReader::read_column() to read the specific vector column
  blocksstable::ObStorageDatum datum;
  blocksstable::ObRowReader row_reader;
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(row_reader.read_column(
                 new_row.data_, new_row.size_,
                 vec_info.vec_col_idx_, datum))) {
    LOG_WARN("fail to read vector column from new_row",
             K(ret), K(vec_info.vec_col_idx_), K(new_row));
  }
  // Step 2: validate the datum
  if (OB_FAIL(ret)) {
  } else if (datum.is_nop()) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("vector column is NOP (not modified)", K(ret), K(vec_info));
  } else if (datum.is_null()) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("vector column is NULL", K(ret), K(vec_info));
  } else if (datum.len_ <= 0) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("vector datum has zero length", K(ret), K(vec_info), K(datum.len_));
  } else if (OB_ISNULL(datum.ptr_)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("vector datum ptr is null", K(ret), K(vec_info), K(datum.len_));
  } else {
    // Step 3: parse vector data and return pointer + length
    const int64_t expected_len = vec_info.dim_ * static_cast<int64_t>(sizeof(float));
    const int64_t header_len = static_cast<int64_t>(sizeof(uint32_t));
    if (datum.len_ == expected_len) {
      vec_data = const_cast<char *>(datum.ptr_);
      vec_len = datum.len_;
    } else if (datum.len_ == header_len + expected_len) {
      vec_data = const_cast<char *>(datum.ptr_) + header_len;
      vec_len = expected_len;
    } else {
      ret = common::OB_ERR_UNEXPECTED;
      LOG_WARN("vector data length does not match expected dense size",
               K(ret), K(datum.len_), K(expected_len), K(header_len), K(vec_info));
    }
  }
  return ret;
}

int ObCSAsyncIndexProcessor::get_tx_desc_from_ctx_(transaction::ObTxDesc *&tx_desc)
{
  int ret = common::OB_SUCCESS;
  tx_desc = nullptr;
  observer::ObInnerSQLConnection *inner_conn = nullptr;
  if (OB_ISNULL(ctx_.trans_.get_connection())) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("ObMySQLTransaction connection is null", K(ret));
  } else if (OB_ISNULL(inner_conn = static_cast<observer::ObInnerSQLConnection *>(ctx_.trans_.get_connection()))) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("Failed to cast ObISQLConnection to ObInnerSQLConnection", K(ret));
  } else if (OB_ISNULL(tx_desc = inner_conn->get_session().get_tx_desc())) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("Failed to get tx_desc from ObInnerSQLConnection session", K(ret));
  }
  return ret;
}

int ObCSAsyncIndexProcessor::build_das_ins_ctdef_(common::ObArenaAllocator &allocator,
    const ObCSVecIndexInfo &vec_info,
    sql::ObDASInsertOp *insert_op,
    sql::ObDASInsCtDef *&ins_ctdef)
{
  int ret = common::OB_SUCCESS;
  ins_ctdef = nullptr;
  if (OB_ISNULL(insert_op)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("insert_op is null", K(ret));
  } else if (OB_FAIL(sql::ObDASTaskFactory::alloc_das_ctdef(
          sql::DAS_OP_TABLE_INSERT, allocator, ins_ctdef))) {
    LOG_WARN("Failed to allocate ObDASInsCtDef", K(ret));
  } else if (OB_ISNULL(ins_ctdef)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("ins_ctdef is null after allocation", K(ret));
  } else {
    const uint64_t tenant_id = MTL_ID();
    const schema::ObTableSchema *index_id_schema = nullptr;
    if (OB_FAIL(schema_guard_.get_table_schema(
            tenant_id, vec_info.index_id_table_id_, index_id_schema))) {
      LOG_WARN("Failed to get index_id_table schema", K(ret), K(vec_info.index_id_table_id_));
    } else if (OB_ISNULL(index_id_schema)) {
      ret = common::OB_ERR_UNEXPECTED;
      LOG_WARN("index_id_table schema not found, may be dropped",
                K(ret), K(vec_info.index_id_table_id_));
    } else {
      ins_ctdef->table_id_ = vec_info.data_table_id_;
      ins_ctdef->index_tid_ = vec_info.index_id_table_id_;
      ins_ctdef->rowkey_cnt_ = index_id_schema->get_rowkey_info().get_size();
      ins_ctdef->spk_cnt_ = index_id_schema->get_shadow_rowkey_info().get_size();
      ins_ctdef->schema_version_ = index_id_schema->get_schema_version();

      const int64_t column_count = index_id_schema->get_column_count();
      if (OB_FAIL(ins_ctdef->column_ids_.reserve(column_count)) ||
          OB_FAIL(ins_ctdef->column_types_.reserve(column_count)) ||
          OB_FAIL(ins_ctdef->column_accuracys_.reserve(column_count))) {
        LOG_WARN("Failed to reserve column arrays", K(ret), K(column_count));
      } else {
        common::ObSEArray<const schema::ObColumnSchemaV2 *, 8> sorted_columns;
        for (schema::ObTableSchema::const_column_iterator iter = index_id_schema->column_begin();
             OB_SUCC(ret) && iter != index_id_schema->column_end(); ++iter) {
          if (OB_FAIL(sorted_columns.push_back(*iter))) {
            LOG_WARN("Failed to collect column", K(ret));
          }
        }
        if (OB_SUCC(ret)) {
          std::sort(sorted_columns.begin(), sorted_columns.end(),
                   [](const schema::ObColumnSchemaV2 *a, const schema::ObColumnSchemaV2 *b) {
                     int64_t a_pos = a->get_rowkey_position();
                     int64_t b_pos = b->get_rowkey_position();
                     if (a_pos > 0 && b_pos > 0) return a_pos < b_pos;
                     if (a_pos > 0) return true;
                     if (b_pos > 0) return false;
                     return a->get_column_id() < b->get_column_id();
                   });
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < sorted_columns.count(); ++i) {
          const schema::ObColumnSchemaV2 *column_schema = sorted_columns.at(i);
          if (OB_ISNULL(column_schema)) {
            ret = common::OB_ERR_UNEXPECTED;
            LOG_WARN("column_schema is null", K(ret));
          } else if (OB_FAIL(ins_ctdef->column_ids_.push_back(column_schema->get_column_id())) ||
                     OB_FAIL(ins_ctdef->column_types_.push_back(column_schema->get_meta_type())) ||
                     OB_FAIL(ins_ctdef->column_accuracys_.push_back(column_schema->get_accuracy()))) {
            LOG_WARN("Failed to push column info", K(ret), K(column_schema->get_column_id()));
          }
        }
      }

      if (OB_SUCC(ret) && OB_FAIL(ins_ctdef->table_param_.convert(
              index_id_schema, ctx_.schema_version_, ins_ctdef->column_ids_))) {
        LOG_WARN("Failed to convert table_param", K(ret), K(vec_info.index_id_table_id_));
      } else if (OB_SUCC(ret) && OB_FAIL(ins_ctdef->new_row_projector_.prepare_allocate(column_count))) {
        LOG_WARN("Failed to prepare new_row_projector", K(ret), K(column_count));
      } else if (OB_SUCC(ret)) {
        // Identity mapping: output column i = stored row index i (DASDMLIterator uses this for col_count).
        for (int64_t i = 0; i < column_count; ++i) {
          ins_ctdef->new_row_projector_.at(i) = i;
        }
        ins_ctdef->is_ignore_ = false;
        ins_ctdef->is_batch_stmt_ = false;
        ins_ctdef->is_table_api_ = false;
        // Direct insert into index_id_table: bypass ObVecIndexDMLIterator which returns 0 rows for
        // is_no_need_update_vector_index() (vec_index_id_type). Use raw write_iter path instead.
        ins_ctdef->is_access_vidx_as_master_table_ = true;
        insert_op->set_das_ctdef(ins_ctdef);
      }
    }
  }
  return ret;
}

int ObCSAsyncIndexProcessor::build_das_ins_rtdef_(common::ObArenaAllocator &allocator,
    sql::ObDASInsertOp *insert_op,
    sql::ObDASInsRtDef *&ins_rtdef)
{
  int ret = common::OB_SUCCESS;
  ins_rtdef = nullptr;
  if (OB_ISNULL(insert_op)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("insert_op is null", K(ret));
  } else if (OB_FAIL(sql::ObDASTaskFactory::alloc_das_rtdef(
          sql::DAS_OP_TABLE_INSERT, allocator, ins_rtdef))) {
    LOG_WARN("Failed to allocate ObDASInsRtDef", K(ret));
  } else if (OB_ISNULL(ins_rtdef)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("ins_rtdef is null after allocation", K(ret));
  } else {
    const int64_t current_time = common::ObTimeUtility::current_time();
    const int64_t timeout_us = GCONF.internal_sql_execute_timeout;
    ins_rtdef->timeout_ts_ = current_time + timeout_us;
    ins_rtdef->tenant_schema_version_ = ctx_.schema_version_;
    ins_rtdef->prelock_ = false;
    ins_rtdef->is_for_foreign_key_check_ = false;
    ins_rtdef->is_immediate_row_conflict_check_ = true;
    ins_rtdef->need_fetch_conflict_ = false;
    ins_rtdef->is_duplicated_ = false;
    ins_rtdef->use_put_ = false;
    ins_rtdef->direct_insert_task_id_ = 0;
    ins_rtdef->ddl_task_id_ = 0;
    insert_op->set_das_rtdef(ins_rtdef);
  }
  return ret;
}

int ObCSAsyncIndexProcessor::build_insert_buffer_from_events_(common::ObArenaAllocator &allocator,
    const common::ObIArray<ObASyncIndexEvent> &events,
    const ObCSVecIndexInfo &vec_info,
    sql::ObDASInsertOp *insert_op,
    sql::ObDASInsCtDef *ins_ctdef)
{
  int ret = common::OB_SUCCESS;
  const uint64_t tenant_id = MTL_ID();
  const schema::ObTableSchema *index_id_schema = nullptr;
  common::hash::ObHashMap<uint64_t, int64_t> col_id_to_idx_map;
  uint64_t scn_col_id = common::OB_INVALID_ID;
  uint64_t vid_col_id = common::OB_INVALID_ID;
  uint64_t type_col_id = common::OB_INVALID_ID;
  uint64_t vector_col_id = common::OB_INVALID_ID;
  int64_t scn_idx = -1;
  int64_t vid_idx = -1;
  int64_t type_idx = -1;
  int64_t vector_idx = -1;
  sql::ObDASWriteBuffer::DmlShadowRow shadow_row;

  if (OB_ISNULL(insert_op) || OB_ISNULL(ins_ctdef)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("insert_op or ins_ctdef is null", K(ret));
  } else if (OB_FAIL(insert_op->init_task_info(sql::ObDASWriteBuffer::DAS_ROW_DEFAULT_EXTEND_SIZE))) {
    LOG_WARN("Failed to init insert_buffer", K(ret));
  } else if (OB_FAIL(schema_guard_.get_table_schema(tenant_id, vec_info.index_id_table_id_, index_id_schema))) {
    LOG_WARN("Failed to get index_id_table schema", K(ret), K(vec_info.index_id_table_id_));
  } else if (OB_ISNULL(index_id_schema)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("index_id_table schema not found", K(ret), K(vec_info.index_id_table_id_));
  } else if (OB_FAIL(col_id_to_idx_map.create(ins_ctdef->column_ids_.count(), "CSColIdMap"))) {
    LOG_WARN("Failed to create column_id to index map", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ins_ctdef->column_ids_.count(); ++i) {
      if (OB_FAIL(col_id_to_idx_map.set_refactored(ins_ctdef->column_ids_.at(i), i))) {
        LOG_WARN("Failed to set column_id mapping", K(ret), K(i), K(ins_ctdef->column_ids_.at(i)));
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (3 != index_id_schema->get_rowkey_info().get_size()) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("index_id_table rowkey count must be 3 (scn, vid, type)", K(ret),
             K(index_id_schema->get_rowkey_info().get_size()));
  } else if (OB_FAIL(index_id_schema->get_rowkey_info().get_column_id(0, scn_col_id)) ||
             OB_FAIL(index_id_schema->get_rowkey_info().get_column_id(1, vid_col_id)) ||
             OB_FAIL(index_id_schema->get_rowkey_info().get_column_id(2, type_col_id))) {
    LOG_WARN("Failed to get rowkey column ids from index_id_table", K(ret));
  } else {
    for (schema::ObTableSchema::const_column_iterator iter = index_id_schema->column_begin();
         OB_SUCC(ret) && iter != index_id_schema->column_end(); ++iter) {
      const schema::ObColumnSchemaV2 *col_schema = *iter;
      if (OB_ISNULL(col_schema)) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("column_schema is null", K(ret));
        break;
      } else if (0 == col_schema->get_rowkey_position() &&
                 col_schema->get_column_name_str().prefix_match("__vector_")) {
        vector_col_id = col_schema->get_column_id();
        break;
      }
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(col_id_to_idx_map.get_refactored(scn_col_id, scn_idx)) ||
             OB_FAIL(col_id_to_idx_map.get_refactored(vid_col_id, vid_idx)) ||
             OB_FAIL(col_id_to_idx_map.get_refactored(type_col_id, type_idx))) {
    LOG_WARN("Column id not in insert column list", K(ret),
             K(scn_col_id), K(vid_col_id), K(type_col_id));
  } else if (common::OB_INVALID_ID != vector_col_id &&
             OB_FAIL(col_id_to_idx_map.get_refactored(vector_col_id, vector_idx))) {
    LOG_WARN("Vector column id not in insert column list", K(ret), K(vector_col_id));
  } else if (OB_FAIL(shadow_row.init(allocator, ins_ctdef->column_types_, false))) {
    LOG_WARN("Failed to init DmlShadowRow", K(ret));
  } else {
    sql::ObDASWriteBuffer &insert_buffer = insert_op->get_insert_buffer();
    const int64_t col_count = ins_ctdef->column_ids_.count();
    for (int64_t i = 0; OB_SUCC(ret) && i < events.count(); ++i) {
      const ObASyncIndexEvent &event = events.at(i);
      blocksstable::ObDatumRow datum_row;
      if (OB_FAIL(datum_row.init(allocator, col_count))) {
        LOG_WARN("Failed to init ObDatumRow", K(ret), K(i));
      } else {
        datum_row.count_ = col_count;
        for (int64_t j = 0; j < col_count; ++j) {
          datum_row.storage_datums_[j].set_null();
        }
        if (scn_idx >= 0 && scn_idx < col_count) {
          datum_row.storage_datums_[scn_idx].set_uint(event.commit_version_);
        }
        if (vid_idx >= 0 && vid_idx < col_count) {
          datum_row.storage_datums_[vid_idx].set_int(event.vid_);
        }
        if (type_idx >= 0 && type_idx < col_count) {
          char *type_buf = static_cast<char*>(allocator.alloc(1));
          if (OB_ISNULL(type_buf)) {
            ret = common::OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("Failed to allocate memory for type column", K(ret));
          } else {
            *type_buf = event.type_;
            datum_row.storage_datums_[type_idx].set_string(type_buf, 1);
          }
        }
        if (OB_SUCC(ret) && vector_idx >= 0 && vector_idx < col_count) {
          datum_row.storage_datums_[vector_idx].set_null();
        }
      }
      if (OB_FAIL(ret)) {
      } else {
        shadow_row.reuse();
        if (OB_FAIL(shadow_row.shadow_copy(datum_row))) {
          LOG_WARN("Failed to shadow_copy ObDatumRow", K(ret), K(i));
        } else {
          sql::ObDASWriteBuffer::DmlRow *stored_row = nullptr;
          if (OB_FAIL(insert_buffer.add_row(shadow_row, &stored_row))) {
            LOG_WARN("Failed to add row to insert_buffer", K(ret), K(i));
          }
        }
      }
    }
  }
  return ret;
}

int ObCSAsyncIndexProcessor::set_das_insert_context_(const common::ObIArray<ObASyncIndexEvent> &events,
    const ObCSVecIndexInfo &vec_info,
    transaction::ObTxDesc *tx_desc,
    sql::ObDASInsertOp *insert_op,
    common::ObArenaAllocator &allocator)
{
  int ret = common::OB_SUCCESS;
  if (OB_ISNULL(tx_desc) || OB_ISNULL(insert_op)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("tx_desc or insert_op is null", K(ret));
  } else if (events.count() <= 0) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("events array is empty", K(ret));
  } else {
    insert_op->set_trans_desc(tx_desc);
    share::ObLSID ls_id = share::SYS_LS;
    const common::ObTabletID &data_tablet_id = events.at(0).tablet_id_;
    const uint64_t tenant_id = MTL_ID();

    // Resolve vbitmap_tablet_id from schema: (data_tablet_id, index_id_table_id) -> vbitmap_tablet_id.
    int64_t part_idx = 0;
    int64_t subpart_idx = 0;
    common::ObTabletID vbitmap_tablet_id;
    if (OB_FAIL(table::ObTableUtils::get_part_idx_by_tablet_id(schema_guard_,
                                                               vec_info.data_table_id_,
                                                               data_tablet_id,
                                                               part_idx,
                                                               subpart_idx))) {
      LOG_WARN("Failed to get part_idx from data tablet", K(ret), K(vec_info.data_table_id_),
               K(data_tablet_id));
    } else if (OB_FAIL(table::ObTableUtils::get_tablet_id_by_part_idx(schema_guard_,
                                                                      vec_info.index_id_table_id_,
                                                                      part_idx,
                                                                      subpart_idx,
                                                                      vbitmap_tablet_id))) {
      LOG_WARN("Failed to get vbitmap tablet_id from index_id_table schema",
               K(ret), K(vec_info.index_id_table_id_), K(part_idx), K(subpart_idx));
    } else if (!vbitmap_tablet_id.is_valid()) {
      ret = common::OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid vbitmap_tablet_id from schema", K(ret), K(vec_info.index_id_table_id_));
    } else {
      insert_op->set_tablet_id(vbitmap_tablet_id);
      insert_op->set_ls_id(ls_id);
    }

    if (OB_SUCC(ret)) {
      void *snapshot_buf = allocator.alloc(sizeof(transaction::ObTxReadSnapshot));
      if (OB_ISNULL(snapshot_buf)) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("Failed to allocate ObTxReadSnapshot", K(ret));
      } else {
        transaction::ObTxReadSnapshot *snapshot = new(snapshot_buf) transaction::ObTxReadSnapshot();
        transaction::ObTransService *txs = MTL(transaction::ObTransService *);
        if (OB_ISNULL(txs)) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("ObTransService is null", K(ret));
        } else {
          const int64_t timeout_ts = common::ObTimeUtility::current_time() + GCONF.internal_sql_execute_timeout;
          if (OB_FAIL(txs->get_ls_read_snapshot(*tx_desc,
                                                 transaction::ObTxIsolationLevel::RC,
                                                 ls_id,
                                                 timeout_ts,
                                                 *snapshot))) {
            LOG_WARN("Failed to get ls read snapshot", K(ret), K(ls_id), K(timeout_ts));
          } else if (!snapshot->is_valid()) {
            ret = common::OB_ERR_UNEXPECTED;
            LOG_WARN("Invalid snapshot from get_ls_read_snapshot", K(ret), KPC(snapshot));
          } else {
            insert_op->set_snapshot(snapshot);
          }
        }
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// insert_vector_index_log_batch_(): batch DAS insert into index_id_table
// ---------------------------------------------------------------------------

int ObCSAsyncIndexProcessor::insert_vector_index_log_batch_(
    const common::ObIArray<ObASyncIndexEvent> &events,
    const ObCSVecIndexInfo &target_vec_info)
{
  int ret = common::OB_SUCCESS;
  common::ObArenaAllocator das_allocator(common::ObMemAttr(MTL_ID(), "CSDASInsert"));
  sql::ObDASTaskFactory das_factory(das_allocator);
  transaction::ObTxDesc *tx_desc = nullptr;
  sql::ObIDASTaskOp *das_op = nullptr;
  sql::ObDASInsertOp *insert_op = nullptr;
  sql::ObDASInsCtDef *ins_ctdef = nullptr;
  sql::ObDASInsRtDef *ins_rtdef = nullptr;

  if (events.count() <= 0) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("[insert_vector_index_log_batch] step=check_events, events array is empty", K(ret));
  } else if (OB_UNLIKELY(events.at(0).table_id_ != target_vec_info.data_table_id_)) {
    ret = common::OB_INVALID_ARGUMENT;
    LOG_WARN("[insert_vector_index_log_batch] step=check_table_id, events data_table_id mismatch with vec_info",
             K(ret), "event_table_id", events.at(0).table_id_, K(target_vec_info));
  } else if (OB_FAIL(get_tx_desc_from_ctx_(tx_desc))) {
    LOG_WARN("[insert_vector_index_log_batch] step=get_tx_desc, failed to get tx_desc from ctx",
             K(ret), "event_cnt", events.count(), K(target_vec_info));
  } else if (OB_FAIL(das_factory.create_das_task_op(sql::DAS_OP_TABLE_INSERT, das_op))) {
    LOG_WARN("[insert_vector_index_log_batch] step=create_das_op, failed to create DAS InsertOp",
             K(ret), "event_cnt", events.count(), K(target_vec_info));
  } else if (OB_ISNULL(das_op)) {
    ret = common::OB_ERR_UNEXPECTED;
    LOG_WARN("[insert_vector_index_log_batch] step=create_das_op, DAS InsertOp is null after creation",
             K(ret), "event_cnt", events.count());
  } else {
    insert_op = static_cast<sql::ObDASInsertOp *>(das_op);
    if (OB_FAIL(build_das_ins_ctdef_(das_allocator, target_vec_info, insert_op, ins_ctdef))) {
      LOG_WARN("[insert_vector_index_log_batch] step=build_das_ins_ctdef, failed",
               K(ret), "event_cnt", events.count(), K(target_vec_info));
    } else if (OB_FAIL(build_das_ins_rtdef_(das_allocator, insert_op, ins_rtdef))) {
      LOG_WARN("[insert_vector_index_log_batch] step=build_das_ins_rtdef, failed",
               K(ret), "event_cnt", events.count(), K(target_vec_info));
    } else if (OB_FAIL(build_insert_buffer_from_events_(
            das_allocator, events, target_vec_info, insert_op, ins_ctdef))) {
      LOG_WARN("[insert_vector_index_log_batch] step=build_insert_buffer, failed",
               K(ret), "event_cnt", events.count(), K(target_vec_info),
               "first_event", events.at(0));
    } else if (OB_FAIL(set_das_insert_context_(
            events, target_vec_info, tx_desc, insert_op, das_allocator))) {
      LOG_WARN("[insert_vector_index_log_batch] step=set_das_insert_context, failed",
               K(ret), "event_cnt", events.count(), K(target_vec_info));
    } else {
      if (OB_FAIL(insert_op->open_op())) {
        const sql::ObDASInsCtDef *ctdef = static_cast<const sql::ObDASInsCtDef *>(insert_op->get_ctdef());
        const sql::ObDASInsRtDef *rtdef = static_cast<sql::ObDASInsRtDef *>(insert_op->get_rtdef());
        LOG_WARN("[insert_vector_index_log_batch] step=open_op, failed to execute DAS insert",
                 K(ret), "event_cnt", events.count(), K(target_vec_info),
                 "first_event", events.at(0), "index_id_table_id", target_vec_info.index_id_table_id_,
                 "data_tablet_id", events.at(0).tablet_id_,
                 "ls_id", insert_op->get_ls_id(), "tablet_id", insert_op->get_tablet_id(),
                 "tx_desc_valid", (tx_desc != nullptr && tx_desc->is_valid()),
                 "schema_version", ctx_.schema_version_, "timeout_ts", rtdef != nullptr ? rtdef->timeout_ts_ : -1,
                 "column_ids_cnt", ctdef != nullptr ? ctdef->column_ids_.count() : 0,
                 "insert_row_cnt", insert_op->get_row_cnt());
      } else {
        const int64_t affected_rows = insert_op->get_affected_rows();
        const int64_t expected_rows = events.count();
        const sql::ObDASInsCtDef *ctdef = static_cast<const sql::ObDASInsCtDef *>(insert_op->get_ctdef());
        const bool is_domain_index = (ctdef != nullptr &&
            ctdef->table_param_.get_data_table().is_domain_index());
        if (!is_domain_index && affected_rows != expected_rows) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("Affected rows mismatch", K(ret), K(affected_rows), K(expected_rows), K(events.count()));
        }
      }
    }
  }
  return ret;
}

// ---------------------------------------------------------------------------
// write_to_vsag_(): write vectors into vsag for HNSW indexes
//
// Adapter lookup: resolve inc_tablet_id from (data_tablet_id, delta_buffer_table)
// via partition mapping, then acquire adapter keyed by inc_tablet_id.
// This ensures vsag data lives in incr_data_ of the delta_buffer adapter,
// which survives the merge into a complete/full_partial adapter.
// ---------------------------------------------------------------------------

int ObCSAsyncIndexProcessor::write_to_vsag_(
    const common::ObIArray<ObASyncIndexEvent> &events,
    const ObCSVecIndexInfo &vec_info)
{
  int ret = common::OB_SUCCESS;

  const bool need_vsag = (events.count() > 0) &&
      (schema::is_vec_delta_buffer_type(vec_info.index_type_) ||
       schema::is_vec_index_id_type(vec_info.index_type_) ||
       schema::is_hybrid_vec_index_log_type(vec_info.index_type_));

  if (!need_vsag) {
  } else {
    const common::ObTabletID &data_tablet_id = events.at(0).tablet_id_;
    const share::ObLSID ls_id = share::SYS_LS;
    ObPluginVectorIndexService *vec_index_service = MTL(ObPluginVectorIndexService *);
    ObPluginVectorIndexAdapterGuard adapter_guard;
    ObPluginVectorIndexAdaptor *adaptor = nullptr;
    int64_t part_idx = 0;
    int64_t subpart_idx = 0;
    common::ObTabletID inc_tablet_id;
    if (OB_ISNULL(vec_index_service)) {
      ret = common::OB_ERR_UNEXPECTED;
      LOG_WARN("ObPluginVectorIndexService is null", K(ret));
    } else if (OB_FAIL(table::ObTableUtils::get_part_idx_by_tablet_id(schema_guard_,
                                                                       vec_info.data_table_id_,
                                                                       data_tablet_id,
                                                                       part_idx,
                                                                       subpart_idx))) {
      LOG_WARN("Failed to get part_idx from data tablet", K(ret), K(vec_info.data_table_id_),
               K(data_tablet_id));
    } else if (OB_FAIL(table::ObTableUtils::get_tablet_id_by_part_idx(schema_guard_,
                                                                      vec_info.delta_buffer_table_id_,
                                                                      part_idx,
                                                                      subpart_idx,
                                                                      inc_tablet_id))) {
      LOG_WARN("Failed to get inc_tablet_id from delta_buffer_table schema",
               K(ret), K(vec_info.delta_buffer_table_id_), K(part_idx), K(subpart_idx));
    } else if (!inc_tablet_id.is_valid()) {
      ret = common::OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid inc_tablet_id from schema", K(ret), K(vec_info.delta_buffer_table_id_));
    } else {
      const uint64_t tenant_id = MTL_ID();
      const schema::ObTableSchema *delta_buf_schema = nullptr;
      const schema::ObTableSchema *index_id_schema = nullptr;
      ObString vec_idx_param;
      if (OB_FAIL(schema_guard_.get_table_schema(tenant_id, vec_info.delta_buffer_table_id_, delta_buf_schema))) {
        LOG_WARN("Failed to get delta_buffer schema for adapter creation",
                 K(ret), K(vec_info.delta_buffer_table_id_));
      } else if (OB_ISNULL(delta_buf_schema)) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("delta_buffer schema is null", K(ret), K(vec_info.delta_buffer_table_id_));
      } else {
        vec_idx_param = delta_buf_schema->get_index_params();
        // For HNSW+heap+async, delta_buffer may have empty index_params; fallback to index_id table.
        if (vec_idx_param.empty() &&
            OB_SUCC(schema_guard_.get_table_schema(tenant_id, vec_info.index_id_table_id_, index_id_schema)) &&
            OB_NOT_NULL(index_id_schema)) {
          vec_idx_param = index_id_schema->get_index_params();
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(vec_index_service->acquire_adapter_guard(ls_id,
                                                                   inc_tablet_id,
                                                                   schema::INDEX_TYPE_VEC_DELTA_BUFFER_LOCAL,
                                                                   adapter_guard,
                                                                   &vec_idx_param,
                                                                   vec_info.dim_))) {
        LOG_WARN("Failed to acquire adapter for inc tablet",
                 K(ret), K(inc_tablet_id), K(ls_id), K(vec_info.dim_));
      } else {
        adaptor = adapter_guard.get_adatper();
        if (OB_ISNULL(adaptor)) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("Adapter is null after acquire_adapter_guard", K(ret));
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObVectorIndexMemData *incr_data = adaptor->get_incr_data();
      obvsag::VectorIndexPtr index_handler = nullptr;
      if (OB_ISNULL(incr_data) || !incr_data->is_inited()) {
        if (OB_FAIL(adaptor->try_init_mem_data(VIRT_INC))) {
          LOG_WARN("Failed to lazily init vsag for async index", K(ret));
        } else {
          incr_data = adaptor->get_incr_data();
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_ISNULL(incr_data)) {
        ret = common::OB_ERR_UNEXPECTED;
        LOG_WARN("incr_data is null after init attempt", K(ret));
      } else {
        index_handler = static_cast<obvsag::VectorIndexPtr>(incr_data->index_);
        if (OB_ISNULL(index_handler)) {
          ret = common::OB_ERR_UNEXPECTED;
          LOG_WARN("vsag index handler is null after init attempt", K(ret));
        }
      }

      if (OB_SUCC(ret)) {
        int64_t insert_count = 0;
        for (int64_t i = 0; i < events.count(); ++i) {
          if (events.at(i).type_ == ObCSAsyncIndexEventType::INSERT &&
              events.at(i).vec_data_ != nullptr &&
              events.at(i).vec_data_len_ > 0) {
            insert_count++;
          }
        }
        if (insert_count == 0) {
          // No INSERT events to add, success
        } else {
          common::ObArenaAllocator tmp_allocator(common::ObMemAttr(MTL_ID(), "CSVsagWrite"));
          int64_t *vids = nullptr;
          float *vectors = nullptr;
          const int64_t dim = vec_info.dim_;
          const int64_t vector_size = sizeof(float) * dim;

          if (OB_ISNULL(vids = static_cast<int64_t *>(tmp_allocator.alloc(sizeof(int64_t) * insert_count)))) {
            ret = common::OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("Failed to allocate memory for vids", K(ret), K(insert_count));
          } else if (OB_ISNULL(vectors = static_cast<float *>(
                                  tmp_allocator.alloc(sizeof(float) * insert_count * dim)))) {
            ret = common::OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("Failed to allocate memory for vectors", K(ret), K(insert_count), K(dim));
          } else {
            int64_t insert_idx = 0;
            for (int64_t i = 0; OB_SUCC(ret) && i < events.count(); ++i) {
              const ObASyncIndexEvent &event = events.at(i);
              if (event.type_ == ObCSAsyncIndexEventType::INSERT &&
                  event.vec_data_ != nullptr &&
                  event.vec_data_len_ > 0) {
                const int64_t expected_len = dim * static_cast<int64_t>(sizeof(float));
                if (event.vec_data_len_ != expected_len) {
                  ret = common::OB_ERR_UNEXPECTED;
                  LOG_WARN("Vector data length mismatch",
                           K(ret), K(event.vec_data_len_), K(expected_len), K(dim));
                } else {
                  vids[insert_idx] = event.vid_;
                  const float *src_vector = reinterpret_cast<const float *>(event.vec_data_);
                  float *dst_vector = vectors + insert_idx * dim;
                  MEMCPY(dst_vector, src_vector, vector_size);
                  insert_idx++;
                }
              }
            }

            if (OB_SUCC(ret) && insert_count > 0) {
              lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr(MTL_ID(), "CSVsagAdd"));
              if (OB_FAIL(obvectorutil::add_index(index_handler,
                                                  vectors,
                                                  vids,
                                                  dim,
                                                  nullptr,
                                                  insert_count))) {
                LOG_WARN("Failed to add vectors to vsag index",
                         K(ret), K(insert_count), K(dim), K(ls_id), K(vec_info.index_id_table_id_));
              }
            }
          }
        }
      }
      // After writing to vsag, proactively refresh the bitmap so the first query
      // does not incur the overhead of building it from scratch.  This mirrors the
      // query-time path (complete_index_mem_data_incremental) but is triggered here
      // because change_stream already holds the adapter and knows data has landed.
      // A failure is non-fatal: the bitmap will still be refreshed lazily on the
      // next query if this call fails.
      if (OB_SUCC(ret) && OB_NOT_NULL(adaptor)) {
        if (!adaptor->is_vbitmap_tablet_valid()) {
          LOG_INFO("skip background bitmap refresh: vbitmap tablet id not ready yet",
                   K(inc_tablet_id), K(vec_info.index_id_table_id_), KPC(adaptor));
        } else {
          int tmp_ret = adaptor->refresh_bitmap_background();
          if (OB_SUCCESS != tmp_ret) {
            LOG_WARN("background bitmap refresh failed (non-fatal), will retry on next query",
                     K(tmp_ret), K(inc_tablet_id), K(vec_info.index_id_table_id_));
          }
        }
      }
    }
  }
  return ret;
}

ObCSPluginAsyncIndex::ObCSPluginAsyncIndex()
  : ObCSPlugin(),
    is_inited_(false)
{
  set_plugin_type(CS_PLUGIN_ASYNC_INDEX);
}

ObCSPluginAsyncIndex::~ObCSPluginAsyncIndex()
{
  destroy();
}

// ---------------------------------------------------------------------------
// Lifecycle: init / destroy
// ---------------------------------------------------------------------------

int ObCSPluginAsyncIndex::init()
{
  int ret = common::OB_SUCCESS;
  if (is_inited_) {
    ret = common::OB_INIT_TWICE;
    LOG_WARN("ObCSPluginAsyncIndex already inited", K(ret));
  } else {
    is_inited_ = true;
  }
  return ret;
}

void ObCSPluginAsyncIndex::destroy()
{
  if (is_inited_) {
    is_inited_ = false;
  }
}

// ---------------------------------------------------------------------------
// process(): delegate to per-thread processor (stack-allocated)
// ---------------------------------------------------------------------------

int ObCSPluginAsyncIndex::process(common::ObIArray<ObCSRow> &rows, ObCSExecCtx &ctx)
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObCSPluginAsyncIndex not inited", K(ret));
  } else if (rows.count() == 0) {
    // nothing to process, ret stays OB_SUCCESS
  } else {
    ObCSAsyncIndexProcessor processor(ctx);
    ret = processor.process(rows);
  }
  return ret;
}

// ---------------------------------------------------------------------------
// commit(): do nothing for now
// ---------------------------------------------------------------------------

int ObCSPluginAsyncIndex::commit()
{
  int ret = common::OB_SUCCESS;
  if (!is_inited_) {
    ret = common::OB_NOT_INIT;
    LOG_WARN("ObCSPluginAsyncIndex not inited", K(ret));
  } else {
    // Periodically trigger GC of fallback schema cache to prevent
    // TenaSchMgrForLi arena memory leak.
    // At commit() time all process() calls have completed and all
    // schema_guard handles are released (ref_cnt == 0), so GC can
    // evict every fallback cache entry and reset the arena.
    static int64_t last_gc_time = 0;
    const int64_t GC_INTERVAL_US = 30L * 1000L * 1000L; // 30 seconds
    const int64_t now = common::ObTimeUtility::current_time();
    if (now - ATOMIC_LOAD(&last_gc_time) > GC_INTERVAL_US) {
      ATOMIC_STORE(&last_gc_time, now);
      schema::ObMultiVersionSchemaService *schema_service =
          MTL(schema::ObTenantSchemaService *) != nullptr
              ? MTL(schema::ObTenantSchemaService *)->get_schema_service()
              : nullptr;
      if (OB_NOT_NULL(schema_service)) {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(schema_service->try_eliminate_schema_mgr())) {
          LOG_WARN("try_eliminate_schema_mgr for fallback gc failed", K(tmp_ret));
        }
      }
    }
  }
  return ret;
}
// ---------------------------------------------------------------------------
// Factory function
// ---------------------------------------------------------------------------

ObCSPlugin *create_async_index_plugin()
{
  return OB_NEW(ObCSPluginAsyncIndex, common::ObMemAttr(MTL_ID(), "CSAsyncIdx"));
}

}  // namespace share
}  // namespace oceanbase
