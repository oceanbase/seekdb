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
#define USING_LOG_PREFIX SERVER
#include "ob_ivf_async_task.h"
#include "share/rc/ob_server_runtime.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "observer/vector_index/ob_vector_index_ivf_cache_util.h"

namespace oceanbase
{
namespace share
{
int ObIvfAsyncTask::delete_deprecated_cache(ObPluginVectorIndexService &vector_index_service)
{
  int ret = OB_SUCCESS;
  ObPluginVectorIndexMgr *index_mgr = &vector_index_service.get_index_mgr();
  if (OB_FAIL(index_mgr->erase_ivf_cache_mgr(ctx_->task_status_.tablet_id_))) {
    if (ret != OB_HASH_NOT_EXIST) {
      LOG_WARN("failed to erase vector index ivf cache mgr",
               K(ctx_->task_status_.tablet_id_),
               KR(ret));
    } else {  // already removed
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

int ObIvfAsyncTask::write_cache(ObPluginVectorIndexService &vector_index_service)
{
  int ret = OB_SUCCESS;
  ObIvfCacheMgrGuard cache_guard;
  ObIvfCacheMgr *cache_mgr = nullptr;
  ObVectorIndexParam vec_param;
  ObIvfCentCache *cent_cache = nullptr;
  ObIvfAuxTableInfo *aux_table_info = nullptr;
  ObSchemaGetterGuard schema_guard;

  if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null ctx_", K(ret), KP(ctx_));
  } else if (OB_ISNULL(aux_table_info = reinterpret_cast<ObIvfAuxTableInfo *>(ctx_->extra_data_))) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null aux_table_info", K(ret), KP(ctx_->extra_data_));
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                 schema_guard))) {
  } else if (OB_FAIL(ObVectorIndexUtil::get_vector_index_param_with_dim(
                 schema_guard,
                 ctx_->task_status_.table_id_,
                 aux_table_info->data_table_id_,
                 ObVectorIndexType::VIT_IVF_INDEX,
                 vec_param))) {
  } else if (OB_FAIL(vector_index_service.acquire_ivf_cache_mgr_guard(ctx_->task_status_.tablet_id_,
                                                                      vec_param,
                                                                      vec_param.dim_,
                                                                      ctx_->task_status_.table_id_,
                                                                      cache_guard))) {
  } else if (OB_ISNULL(cache_mgr = cache_guard.get_ivf_cache_mgr())) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("invalid null cache mgr", K(ret));
  } else if (OB_FAIL(cache_mgr->get_or_create_cache_node(IvfCacheType::IVF_CENTROID_CACHE,
                                                         cent_cache))) {
  } else if (OB_FAIL(ObIvfCacheUtil::scan_and_write_ivf_cent_cache(
                 vector_index_service,
                 aux_table_info->centroid_table_id_,
                 aux_table_info->centroid_tablet_ids_[0],
                 *cent_cache,
                 false /* is_pq_centroid */))) {
  } else if (aux_table_info->type_ == VIAT_IVF_PQ) {
    ObIvfCentCache *pq_cent_cache = nullptr;
    if (OB_FAIL(cache_mgr->get_or_create_cache_node(IvfCacheType::IVF_PQ_CENTROID_CACHE,
                                                    pq_cent_cache))) {
    } else if (OB_FAIL(ObIvfCacheUtil::scan_and_write_ivf_cent_cache(
                   vector_index_service,
                   aux_table_info->pq_centroid_table_id_,
                   aux_table_info->pq_centroid_tablet_ids_[0],
                   *pq_cent_cache,
                   true /* is_pq_centroid */))) {
    }
  }
  return ret;
}

int ObIvfAsyncTask::do_work()
{
  int ret = OB_SUCCESS;
  bool is_deprecated = false;
  ObPluginVectorIndexService *vector_index_service = ::oceanbase::share::server_service<::oceanbase::share::ObPluginVectorIndexService>();
  DEBUG_SYNC(HANDLE_VECTOR_INDEX_ASYNC_TASK);
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObVecIndexAsyncTask is not init", KR(ret));
  } else if (OB_ISNULL(ctx_) || OB_ISNULL(ctx_->ls_) || OB_ISNULL(vector_index_service)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("unexpected nullptr", K(ret), KP(ctx_), KP(vector_index_service));
  } else if (OB_ISNULL(vec_idx_mgr_)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("get invalid vector index manager", KR(ret));
  } else if (ctx_->task_status_.task_type_ == OB_VECTOR_ASYNC_INDEX_IVF_CLEAN) {
    if (OB_FAIL(delete_deprecated_cache(*vector_index_service))) {
    }
  } else if (ctx_->task_status_.task_type_ == OB_VECTOR_ASYNC_INDEX_IVF_LOAD) {
    if (OB_FAIL(write_cache(*vector_index_service))) {
    }
  } else {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid task type", K(ret), KPC(ctx_));
  }

  if (OB_NOT_NULL(ctx_)) {
    common::ObSpinLockGuard ctx_guard(ctx_->lock_);
    ctx_->task_status_.ret_code_ = ret;
    ctx_->in_thread_pool_ = false;
  }
  LOG_INFO("end ivf do_work", K(ret), K(ctx_->task_status_.tablet_id_));
  return ret;
}
}  // namespace share
}  // namespace oceanbase
