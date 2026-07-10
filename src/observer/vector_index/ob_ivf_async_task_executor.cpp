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
#include "ob_ivf_async_task_executor.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "observer/vector_index/ob_vector_index_ivf_cache_util.h"
#include "storage/ls/ob_ls.h"

namespace oceanbase
{
using namespace storage;
namespace share
{
int ObIvfAsyncTaskExector::LoadTaskCallback::is_cache_mgr_deprecated(ObIvfCacheMgr &cache_mgr,
                                                                     bool &is_deprecated)
{
  int ret = OB_SUCCESS;
  is_deprecated = false;
  const ObTableSchema *table_schema = nullptr;
  ObTabletHandle tablet_handle;
  if (OB_FAIL(schema_guard_.get_table_schema( cache_mgr.get_table_id(), table_schema))) {
    LOG_WARN("failed to get simple schema", KR(ret), K(cache_mgr));
  } else if (OB_ISNULL(table_schema) || table_schema->is_in_recyclebin()) {
    is_deprecated = true;
  } else if (OB_FAIL(
                 ls_->get_tablet_svr()->get_tablet(cache_mgr.get_cache_mgr_key(), tablet_handle))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("fail to get tablet", K(ret), K(cache_mgr));
    } else {
      ret = OB_SUCCESS;  // not found, moved from this ls
      is_deprecated = true;
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::LoadTaskCallback::operator()(IvfCacheMgrEntry &entry)
{
  int ret = OB_SUCCESS;
  ObTabletID tablet_id = entry.first;
  ObIvfCacheMgr *cache_mgr = entry.second;
  bool is_deprecated = false;
  if (OB_ISNULL(cache_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret));
  } else if (OB_FAIL(is_cache_mgr_deprecated(*cache_mgr, is_deprecated))) {
    LOG_WARN("fail to check cache mgr is deprecated",
             K(ret),
             K(cache_mgr->get_table_id()),
             K(tablet_id));
  } else if (is_deprecated) {
    ObIAllocator *allocator = task_opt_.get_allocator();
    ObVecIndexAsyncTaskCtx *task_ctx = nullptr;
    int64_t new_task_id = OB_INVALID_ID;
    int64_t index_table_id = OB_INVALID_ID;
    common::ObCurTraceId::TraceId new_trace_id;
    bool inc_new_task = false;

    if (OB_ISNULL(task_ctx = OB_NEWx(ObVecIndexAsyncTaskCtx, allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObVecIndexAsyncTaskCtx", K(ret));
    } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_task_id(new_task_id))) {
      LOG_WARN("fail to fetch new task id", K(ret));
    } else if (tablet_id != cache_mgr->get_cache_mgr_key()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tablet id is not match", K(ret), K(tablet_id), K(cache_mgr->get_cache_mgr_key()));
    } else if (FALSE_IT(index_table_id = cache_mgr->get_table_id())) {
    } else if (OB_INVALID_ID == index_table_id) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table id should be invalid", K(ret));
    } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_trace_id(
                   ++task_trace_base_num_, allocator, new_trace_id))) {
      LOG_WARN("fail to fetch new trace id", K(ret), K(tablet_id));
    } else {
      LOG_DEBUG("start load task", K(ret), K(tablet_id), K(new_task_id), K(index_table_id));
      // 1. update task_ctx to async task map
      
      task_ctx->ls_ = ls_;
      task_ctx->task_status_.tablet_id_ = tablet_id.id();
      
      task_ctx->task_status_.table_id_ = index_table_id;
      task_ctx->task_status_.task_id_ = new_task_id;
      task_ctx->task_status_.task_type_ = ObVecIndexAsyncTaskType::OB_VECTOR_ASYNC_INDEX_IVF_CLEAN;
      task_ctx->task_status_.trigger_type_ = ObVecIndexAsyncTaskTriggerType::OB_VEC_TRIGGER_AUTO;
      task_ctx->task_status_.status_ = ObVecIndexAsyncTaskStatus::OB_VECTOR_ASYNC_TASK_PREPARE;
      task_ctx->task_status_.trace_id_ = new_trace_id;
      task_ctx->task_status_.target_scn_.convert_from_ts(ObTimeUtility::current_time());
      if (OB_FAIL(task_opt_.add_task_ctx(tablet_id, task_ctx, inc_new_task))) {  // not overwrite
        LOG_WARN("fail to add task ctx", K(ret));
      } else if (inc_new_task && OB_FAIL(task_status_array_.push_back(task_ctx))) {
        LOG_WARN("fail to push back task status", K(ret), K(task_ctx));
      }
    }
    // release memory when fail
    if (OB_FAIL(ret) || !inc_new_task) {
      if (OB_NOT_NULL(task_ctx)) {
        task_ctx->~ObVecIndexAsyncTaskCtx();
        allocator->free(task_ctx);  // arena need free
        task_ctx = nullptr;
      }
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::LoadTaskCallback::is_cache_writable(const ObIvfAuxTableInfo &table_info,
                                                               int64_t idx, bool &is_writable)
{
  int ret = OB_SUCCESS;
  is_writable = false;
  ObVectorIndexParam vec_param;
  if (idx < 0 || idx >= table_info.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid idx", K(ret), K(idx), K(table_info));
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_param_with_dim(
                 schema_guard_,
                 table_info.centroid_table_id_,
                 table_info.data_table_id_,
                 ObVectorIndexType::VIT_IVF_INDEX,
                 vec_param))) {
    LOG_WARN("fail to get vector index param with dim", K(ret), K(table_info));
  } else if (OB_FAIL(ObIvfCacheUtil::is_cache_writable(ls_->get_ls_id(),
                                                       table_info.centroid_table_id_,
                                                       table_info.centroid_tablet_ids_[idx],
                                                       vec_param,
                                                       vec_param.dim_,
                                                       is_writable))) {
    LOG_WARN("fail to check is cache writable", K(ret), K(table_info));
  } else if (!is_writable && table_info.type_ == ObVectorIndexAlgorithmType::VIAT_IVF_PQ) {
    if (OB_FAIL(ObIvfCacheUtil::is_cache_writable(ls_->get_ls_id(),
                                                  table_info.centroid_table_id_,
                                                  table_info.centroid_tablet_ids_[idx],
                                                  vec_param,
                                                  vec_param.dim_,
                                                  is_writable))) {
      LOG_WARN("fail to check is cache writable", K(ret), K(table_info));
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::LoadTaskCallback::operator()(ObIvfAuxTableInfoEntry &entry)
{
  int ret = OB_SUCCESS;
  const ObIvfAuxTableInfo &table_info = entry.second;
  ObIAllocator *allocator = task_opt_.get_allocator();

  if (!table_info.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid table info", K(ret), K(table_info));
  }

  for (int i = 0; OB_SUCC(ret) && i < table_info.count(); ++i) {
    ObVecIndexAsyncTaskCtx *task_ctx = nullptr;
    int64_t new_task_id = OB_INVALID_ID;
    int64_t index_table_id = table_info.centroid_table_id_;
    common::ObCurTraceId::TraceId new_trace_id;
    bool inc_new_task = false;
    ObTabletID tablet_id = table_info.centroid_tablet_ids_[i];
    bool is_exist = false;
    bool is_writable = false;

    if (OB_FAIL(task_opt_.is_task_ctx_exist(tablet_id, is_exist))) {
      LOG_WARN("fail to check is task ctx exist", K(ret), K(tablet_id));
    } else if (is_exist) {
      // do nothing
    } else if (OB_FAIL(is_cache_writable(table_info, i, is_writable))) {
      LOG_WARN("fail to check is cache writable", K(ret), K(table_info));
    } else if (!is_writable) {
      // do nothing
    } else if (OB_ISNULL(task_ctx = OB_NEWx(ObVecIndexAsyncTaskCtx, allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to new ObVecIndexAsyncTaskCtx", K(ret));
    } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_task_id(new_task_id))) {
      LOG_WARN("fail to fetch new task id", K(ret));
    } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::fetch_new_trace_id(
                   ++task_trace_base_num_, allocator, new_trace_id))) {
      LOG_WARN("fail to fetch new trace id", K(ret), K(task_trace_base_num_));
    } else {
      LOG_DEBUG("start load task", K(ret), K(task_trace_base_num_), K(new_task_id), K(index_table_id));
      // 1. update task_ctx to async task map
      
      task_ctx->ls_ = ls_;
      task_ctx->task_status_.tablet_id_ = tablet_id.id();
      
      task_ctx->task_status_.table_id_ = index_table_id;
      task_ctx->task_status_.task_id_ = new_task_id;
      task_ctx->task_status_.task_type_ = ObVecIndexAsyncTaskType::OB_VECTOR_ASYNC_INDEX_IVF_LOAD;
      task_ctx->task_status_.trigger_type_ = ObVecIndexAsyncTaskTriggerType::OB_VEC_TRIGGER_AUTO;
      task_ctx->task_status_.status_ = ObVecIndexAsyncTaskStatus::OB_VECTOR_ASYNC_TASK_PREPARE;
      task_ctx->task_status_.trace_id_ = new_trace_id;
      task_ctx->task_status_.target_scn_.convert_from_ts(ObTimeUtility::current_time());
      
      ObIvfAuxTableInfo *copied_aux_table = nullptr;
      if (OB_ISNULL(copied_aux_table = OB_NEWx(ObIvfAuxTableInfo, &task_ctx->allocator_))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("fail to new ObIvfAuxTableInfo", K(ret));
      } else if (OB_FAIL(table_info.copy_ith_tablet(i, *copied_aux_table))) {
        LOG_WARN("fail to copy ith tablet", K(ret), K(i));
      } else if (FALSE_IT(task_ctx->extra_data_ = static_cast<void *>(copied_aux_table))) {
      } else if (OB_FAIL(
                     task_opt_.add_task_ctx(tablet_id, task_ctx, inc_new_task))) {  // not overwrite
        LOG_WARN("fail to add task ctx", K(ret));
      } else if (inc_new_task && OB_FAIL(task_status_array_.push_back(task_ctx))) {
        LOG_WARN("fail to push back task status", K(ret), K(task_ctx));
      }
    }
    // release memory when fail
    if (OB_FAIL(ret)) {
      if (OB_NOT_NULL(task_ctx)) {
        task_ctx->~ObVecIndexAsyncTaskCtx();
        allocator->free(task_ctx);  // arena need free
        task_ctx = nullptr;
      }
    }
  }

  return ret;
}

bool ObIvfAsyncTaskExector::check_operation_allow()
{
  // NOTE: vector_index_optimize_duty_time (in_active_time) only constrains the
  // creation of AUTO-triggered per-tablet IVF maintenance tasks (i.e. load_task()).
  // It must NOT block:
  //   - loading existing MANUAL tasks from the inner table (load_task_from_inner_table)
  //   - executing already-loaded tasks (start_task)
  //   - cleaning up finished/stale task contexts (clear_old_task_ctx_if_need)
  // Otherwise dbms_vector.rebuild_index and the auto-registered <vidx>_rebuild
  // sched job would be silently blocked outside the duty window.
  return true;
}

int ObIvfAsyncTaskExector::check_and_set_thread_pool()
{
  int ret = OB_SUCCESS;
  ObPluginVectorIndexMgr *index_ls_mgr = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("vector index load task not inited", K(ret));
  } else if (OB_ISNULL(vector_index_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected nullptr", K(ret));
  } else if (OB_FAIL(get_index_ls_mgr(index_ls_mgr))) {
    LOG_WARN("fail to get index ls mgr", K(ret));
  } else {
    ObIAllocator *allocator = index_ls_mgr->get_async_task_opt().get_allocator();
    ObVecIndexAsyncTaskHandler &thread_pool_handle =
        vector_index_service_->get_vec_async_task_handle();
    if (thread_pool_handle.is_inited()) {  // no need to init twice, skip
    } else if (OB_FAIL(thread_pool_handle.init())) {
      LOG_WARN("fail to init vec async task handle", K(ret));
    } else if (OB_FAIL(thread_pool_handle.start())) {
      LOG_WARN("fail to start thread pool", K(ret));
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::get_tablet_ids_by_ls(const ObTableSchema &index_table_schema,
                                                common::ObIArray<ObTabletID> &tablet_id_array)
{
  int ret = OB_SUCCESS;
  ObSEArray<ObTabletID, 1> tmp_tablet_id_array;
  if (OB_ISNULL(ls_) || OB_ISNULL(ls_->get_tablet_svr())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null ls", K(ret));
  } else if (OB_FAIL(index_table_schema.get_tablet_ids(tmp_tablet_id_array))) {
    LOG_WARN("fail to get tablet ids", K(ret), K(tablet_id_array));
  } else {
    ObTabletHandle tablet_handle;
    // check tablet if exist in self ls
    for (int64_t i = 0; i < tmp_tablet_id_array.count(); ++i) {
      ret = ls_->get_tablet_svr()->get_tablet(tmp_tablet_id_array.at(i), tablet_handle);
      if (OB_SUCC(ret)) {
        if (OB_FAIL(tablet_id_array.push_back(tmp_tablet_id_array.at(i)))) {
          LOG_WARN("fail to push back tablet id", K(ret), K(tmp_tablet_id_array.at(i)));
        }
      } else if (ret == OB_TABLET_NOT_EXIST) {
        // do nothing
        ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to get tablet", K(ret), K(tmp_tablet_id_array.at(i)));
      }
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::record_aux_table_info(ObSchemaGetterGuard &schema_guard,
                                                 const ObTableSchema &index_table_schema,
                                                 ObIvfAuxTableInfo &aux_table_info)
{
  int ret = OB_SUCCESS;
  // NOTE(liyao): record smallest version during rebuild
  bool need_record = true;
  if (index_table_schema.is_vec_ivfpq_pq_centroid_index()) {
    if (aux_table_info.pq_centroid_table_id_ != OB_INVALID_ID) {
      const ObTableSchema *other_idx_tb_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema( aux_table_info.pq_centroid_table_id_, other_idx_tb_schema))) {
        LOG_WARN("failed to get simple schema", KR(ret), K(aux_table_info));
      } else if (OB_ISNULL(other_idx_tb_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table schema is null", KR(ret), K(aux_table_info));
      } else if (index_table_schema.get_schema_version()
                 > other_idx_tb_schema->get_schema_version()) {
        need_record = false;
      }
    }

    if (OB_SUCC(ret) && need_record) {
      aux_table_info.pq_centroid_tablet_ids_.reset();
      if (OB_FAIL(get_tablet_ids_by_ls(index_table_schema, aux_table_info.pq_centroid_tablet_ids_))) {
        LOG_WARN("fail to get tablet ids", K(ret), K(aux_table_info));
      } else {
        aux_table_info.pq_centroid_table_id_ = index_table_schema.get_table_id();
      }
    }
  } else {
    if (aux_table_info.centroid_table_id_ != OB_INVALID_ID) {
      const ObTableSchema *other_idx_tb_schema = nullptr;
      if (OB_FAIL(schema_guard.get_table_schema( aux_table_info.centroid_table_id_, other_idx_tb_schema))) {
        LOG_WARN("failed to get simple schema", KR(ret), K(aux_table_info));
      } else if (OB_ISNULL(other_idx_tb_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table schema is null", KR(ret), K(aux_table_info));
      } else if (index_table_schema.get_schema_version()
                 < other_idx_tb_schema->get_schema_version()) {
        need_record = false;
      }
    }

    if (OB_SUCC(ret) && need_record) {
      aux_table_info.centroid_tablet_ids_.reset();
      if (OB_FAIL(get_tablet_ids_by_ls(index_table_schema, aux_table_info.centroid_tablet_ids_))) {
        LOG_WARN("fail to get tablet ids", K(ret), K(aux_table_info));
      } else {
        aux_table_info.centroid_table_id_ = index_table_schema.get_table_id();
        if (index_table_schema.is_vec_ivfflat_centroid_index()) {
          aux_table_info.type_ = VIAT_IVF_FLAT;
        } else if (index_table_schema.is_vec_ivfsq8_centroid_index()) {
          aux_table_info.type_ = VIAT_IVF_SQ8;
        } else if (index_table_schema.is_vec_ivfpq_centroid_index()) {
          aux_table_info.type_ = VIAT_IVF_PQ;
        }
      }
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::generate_aux_table_info_map(ObSchemaGetterGuard &schema_guard,
                                                       const int64_t table_id,
                                                       ObIvfAuxTableInfoMap &aux_table_info_map)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *index_table_schema = nullptr;
  const ObTableSchema *data_table_schema = nullptr;
  ObSEArray<uint64_t, 1> col_ids;
  ObIvfAuxTableInfo cur_aux_table_info;
  if (is_sys_table(table_id)) {
    // do nothing
  } else if (OB_FAIL(schema_guard.get_table_schema( table_id, index_table_schema))) {
    LOG_WARN("failed to get simple schema", KR(ret), K(table_id));
  } else if (OB_ISNULL(index_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("table schema is null", KR(ret), K(table_id));
  } else if (index_table_schema->is_in_recyclebin() || !index_table_schema->can_read_index()) {
    // skip incomplete or in recyclebin indexes
  } else if (index_table_schema->is_vec_ivfpq_pq_centroid_index()
             || index_table_schema->is_vec_ivf_centroid_index()) {
    if (OB_FAIL(schema_guard.get_table_schema( index_table_schema->get_data_table_id(), data_table_schema))) {
      LOG_WARN("failed to get simple schema", KR(ret), K(table_id));
    } else if (OB_ISNULL(data_table_schema)) {
      ret = OB_TABLE_NOT_EXIST;
      LOG_WARN("table schema is null",
               KR(ret),
               K(index_table_schema->get_data_table_id()));
    } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_column_id(
                   *data_table_schema, *index_table_schema, col_ids))) {
      LOG_WARN("fail to get vector index column id", K(ret));
    } else if (col_ids.count() != 1) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("count of col ids should be 1", K(ret), K(col_ids.count()));
    } else {
      ObIvfAuxKey key(data_table_schema->get_table_id(), col_ids.at(0));
      if (OB_FAIL(aux_table_info_map.get_refactored(key, cur_aux_table_info))) {
        if (ret != OB_HASH_NOT_EXIST) {
          LOG_WARN("fail to get refactored", K(ret), K(key));
        } else {
          ret = OB_SUCCESS;
          cur_aux_table_info.data_table_id_ = index_table_schema->get_data_table_id();
        }
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(record_aux_table_info(
                     schema_guard, *index_table_schema, cur_aux_table_info))) {
        LOG_WARN("fail to record aux table info", K(ret), K(cur_aux_table_info));
      } else if (OB_FAIL(
                     aux_table_info_map.set_refactored(key, cur_aux_table_info, 1 /*overwrite*/))) {
        LOG_WARN("fail to set refactored", K(ret), K(key));
      }

      if (OB_FAIL(ret)) {
        // clean invalid aux table info
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(aux_table_info_map.erase_refactored(key))) {
          if (OB_HASH_NOT_EXIST != tmp_ret) {
            LOG_WARN("fail to erase refactored", K(ret), K(key));
          }
        }
      }
    }
  }

  return ret;
}

int ObIvfAsyncTaskExector::check_schema_version_changed(bool &schema_changed)
{
  int ret = OB_SUCCESS;
  schema_changed = false;
  int64_t schema_version = 0;
  ObSchemaGetterGuard schema_guard;
  
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
          schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_schema_version(schema_version))) {
    LOG_WARN("fail to get tenant schema version", K(ret));
  } else if (!ObSchemaService::is_formal_version(schema_version)) {
    ret = OB_EAGAIN;
    LOG_INFO("is not a formal_schema_version", KR(ret), K(schema_version));
  } else if (local_schema_version_ == OB_INVALID_VERSION || local_schema_version_ < schema_version) {
    LOG_INFO("schema changed", KR(ret), K_(local_schema_version), K(schema_version));
    local_schema_version_ = schema_version;
    schema_changed = true;
  }
  return ret;
}

int ObIvfAsyncTaskExector::generate_aux_table_info_map(ObIvfAuxTableInfoMap &aux_table_info_map)
{
  int ret = OB_SUCCESS;
  ObSEArray<uint64_t, DEFAULT_TABLE_ID_ARRAY_SIZE> table_id_array;
  ObMemAttr memattr("IvfTaskExec");
  if (OB_FAIL(ObTTLUtil::get_tenant_table_ids(table_id_array))) {
    LOG_WARN("fail to get tenant table ids", KR(ret));
  } else if (!table_id_array.empty() &&
             OB_FAIL(aux_table_info_map.create(DEFAULT_TABLE_ID_ARRAY_SIZE, memattr, memattr))) {
    LOG_WARN("fail to create param map", KR(ret));
  }
  int64_t start_idx = 0;
  int64_t end_idx = 0;
  while (OB_SUCC(ret) && start_idx < table_id_array.count()) {
    ObSchemaGetterGuard schema_guard;
    start_idx = end_idx;
    end_idx = MIN(table_id_array.count(), start_idx + DEFAULT_TABLE_ID_ARRAY_SIZE);

    if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
            schema_guard))) {
      LOG_WARN("fail to get schema guard", KR(ret));
    }

    for (int64_t idx = start_idx; OB_SUCC(ret) && idx < end_idx; ++idx) {
      const int64_t table_id = table_id_array.at(idx);
      if (OB_FAIL(generate_aux_table_info_map(schema_guard, table_id, aux_table_info_map))) {
        LOG_WARN("fail to generate aux table info map for single table", K(ret), K(table_id));
      }
    }
  }
  return ret;
}

int ObIvfAsyncTaskExector::load_task(uint64_t &task_trace_base_num)
{
  int ret = OB_SUCCESS;
  ObPluginVectorIndexMgr *index_ls_mgr = nullptr;
  ObSchemaGetterGuard schema_guard;
  bool is_active_time = true;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("vector async task not init", KR(ret));
  } else if (!check_operation_allow()) {                 // skip
  // vector_index_optimize_duty_time only constrains AUTO-triggered per-tablet
  // IVF maintenance task creation here. MANUAL tasks (dbms_vector.rebuild_index
  // and the auto-registered <vidx>_rebuild sched job) go through a separate
  // path (ObVecTaskManager::create_task) and must not be blocked by it.
  } else if (OB_FAIL(ObVecIndexAsyncTaskUtil::in_active_time(is_active_time))) {
    LOG_WARN("fail to get active time", KR(ret));
  } else if (!is_active_time) {
    LOG_INFO("skip auto-create per-tablet ivf maintenance tasks, not in active time",
             K(ls_->get_ls_id()));
  } else if (OB_FAIL(get_index_ls_mgr(index_ls_mgr))) {  // skip
    LOG_WARN("fail to get index ls mgr", K(ret), K(ls_->get_ls_id()));
  } else if (OB_ISNULL(ls_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null ls", K(ret));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_tenant_schema_guard(
                 schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else {
    ObVecIndexTaskCtxArray task_status_array;
    LoadTaskCallback load_task_func(
        index_ls_mgr->get_async_task_opt(), *ls_, task_status_array, schema_guard, task_trace_base_num);
    ObIvfAuxTableInfoMap aux_table_info_map;

    if (OB_FAIL(index_ls_mgr->get_ivf_cache_mgr_map().foreach_refactored(
            load_task_func))) {  // ivf clean task
      LOG_WARN("fail to do load task each entry", K(ret), K(load_task_func));
    } else if (OB_FAIL(generate_aux_table_info_map(aux_table_info_map))) {
      LOG_WARN("fail to generate aux table info map", K(ret), K(ls_->get_ls_id()));
    } else if (OB_FAIL(aux_table_info_map.foreach_refactored(load_task_func))) {  // ivf load task
      LOG_WARN("fail to do load task each entry", K(ret), K(load_task_func));
    } else if (OB_FAIL(insert_new_task(task_status_array))) {
      LOG_WARN("fail to insert new task", K(ret), K(ls_->get_ls_id()));
    }
    // clear on fail
    if (OB_FAIL(ret) && !task_status_array.empty()) {
      if (OB_FAIL(clear_task_ctxs(index_ls_mgr->get_async_task_opt(), task_status_array))) {
        LOG_WARN("fail to clear task ctx", K(ret));
      }
    }
  }
  return ret;
}

}  // namespace share
}  // namespace oceanbase
