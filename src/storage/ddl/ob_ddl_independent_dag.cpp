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

#include "storage/ddl/ob_ddl_independent_dag.h"
#include "share/rc/ob_module_provider.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_ddl_macro_block_write_task.h"
#include "storage/ddl/ob_ddl_pipeline.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_ddl_merge_task_v2.h"
#include "storage/ddl/ob_tablet_ddl_kv_mgr.h"
#include "share/ob_server_struct.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase;
using namespace oceanbase::storage;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

ObDDLIndependentDag::ObDDLIndependentDag()
  : ObIndependentDag(share::ObDagType::DAG_TYPE_DDL),
    is_inited_(false),
    arena_(ObMemAttr("ddl_dag")),
    direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID),
    ddl_thread_count_(0),
    pipeline_count_(0),
    ret_code_(OB_SUCCESS)
{

}

void free_tablet_context(ObIAllocator &allocator, ObDDLTabletContext *tablet_context)
{
  if (OB_NOT_NULL(tablet_context)) {
    tablet_context->~ObDDLTabletContext();
    allocator.free(tablet_context);
  }
}

ObDDLIndependentDag::~ObDDLIndependentDag()
{
  reuse();
}

void ObDDLIndependentDag::reuse()
{
  FLOG_INFO("ddl independent dag reuse");
  is_inited_ = false;
  direct_load_type_ = ObDirectLoadType::DIRECT_LOAD_INVALID;
  ddl_thread_count_ = 0;
  ddl_task_param_.reset();
  ObTabletObjLoadHelper::free(arena_, ddl_table_schema_.storage_schema_);
  ObTabletObjLoadHelper::free(arena_, ddl_table_schema_.lob_meta_storage_schema_);
  ddl_table_schema_.reset();
  tablet_ids_.reset();
  FOREACH(tc_it, tablet_context_map_) {
    ObDDLTabletContext *tablet_context = tc_it->second;
    free_tablet_context(arena_,  tablet_context);
  }
  IGNORE_RETURN tablet_context_map_.destroy();
  pipeline_count_ = 0;
  ret_code_ = OB_SUCCESS;
  arena_.reset();
}

int ObDDLIndependentDag::init_by_param(const share::ObIDagInitParam *param)
{
  int ret = OB_SUCCESS;
  const  ObDDLIndependentDagInitParam *init_param = static_cast<const ObDDLIndependentDagInitParam *>(param);
  if (OB_UNLIKELY(nullptr == init_param || !init_param->is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KPC(init_param));
  } else if (OB_FAIL(tablet_ids_.assign(init_param->tablet_ids_))) {
    LOG_WARN("assign tablet id array failed", K(ret), K(init_param->tablet_ids_));
  } else {
    direct_load_type_ = init_param->direct_load_type_;
    ddl_thread_count_ = init_param->ddl_thread_count_;
    ddl_task_param_ = init_param->ddl_task_param_;
    if (OB_FAIL(init_ddl_table_schema())) {
      LOG_WARN("init ddl table schema failed", K(ret));
    } else if (OB_FAIL(init_tablet_context_map())) {
      LOG_WARN("init tablet context failed", K(ret));
    } else {
      is_inited_ = true;
    }
  }
  FLOG_INFO("ddl independent dag init", K(ret), KPC(this), K(ddl_table_schema_), K(tablet_ids_), K(tablet_context_map_.size()));
  return ret;
}

int ObDDLIndependentDag::init_ddl_table_schema()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObDDLTableSchema::fill_ddl_table_schema(ddl_task_param_.target_table_id_, arena_, ddl_table_schema_))) {
    LOG_WARN("fill ddl table schema failed", K(ret));
  }
  return ret;
}

int ObDDLIndependentDag::init_tablet_context_map()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(tablet_context_map_.create(tablet_ids_.count(), ObMemAttr("ddl_dag_ctx_map")))) {
    LOG_WARN("create tablet context map failed", K(ret), K(tablet_ids_.count()));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); ++i) {
    const ObTabletID &tablet_id = tablet_ids_.at(i);
    ObDDLTabletContext *tablet_context = nullptr;
    if (OB_ISNULL(tablet_context = OB_NEWx(ObDDLTabletContext, &arena_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory for tablet context failed", K(ret));
    } else if (OB_FAIL(tablet_context->init(tablet_id, ddl_thread_count_, ddl_task_param_.snapshot_version_, direct_load_type_, ddl_table_schema_))) {
      LOG_WARN("init ddl tablet context failed", K(ret), K(tablet_id), K(ddl_thread_count_));
    } else if (use_tablet_mode() && OB_FAIL(alloc_task(tablet_context->scan_task_))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("alloc tablet scan task failed", K(ret), K(tablet_id));
    } else if (OB_FAIL(tablet_context_map_.set_refactored(tablet_id, tablet_context))) {
      LOG_WARN("set tablet context into map failed", K(ret), K(tablet_id), KPC(tablet_context));
    } else {
      FLOG_INFO("init ddl tablet context", K(tablet_id), KPC(tablet_context));
    }
    if (OB_FAIL(ret) && nullptr != tablet_context) {
      free_tablet_context(arena_, tablet_context);
      tablet_context = nullptr;
    }
  }
  return ret;
}

int ObDDLIndependentDag::get_tablet_context(const ObTabletID &tablet_id, ObDDLTabletContext *&tablet_context)
{
  int ret = OB_SUCCESS;
  tablet_context = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(tablet_context_map_.get_refactored(tablet_id, tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret), K(tablet_id));
  }
  return ret;
}

int ObDDLIndependentDag::schedule_tablet_merge_task()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); ++i) {
      share::SCN mock_start_scn;
      const ObTabletID &tablet_id = tablet_ids_.at(i);

      ObDDLTabletContext *tablet_context = nullptr;

      if (OB_FAIL(mock_start_scn.convert_for_tx(SS_DDL_START_SCN_VAL))) {
        LOG_WARN("failed to convert for tx", K(ret));
      } else if (OB_FAIL(get_tablet_context(tablet_id, tablet_context))) {
        LOG_WARN("get ddl tablet context failed", K(ret), K(tablet_id));
      } 
      /* create merge task for data tablet*/
      ObDDLTabletMergeDagParamV2 merge_param;
      ObDDLMergePrepareTask *ddl_merge_task = nullptr;
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(merge_param.init(true  /*for major*/,
                                          false /* for lob*/,
                                          false /* for replay*/,
                                          mock_start_scn, 
                                          direct_load_type_,
                                          ddl_task_param_,
                                          tablet_context))) {
        LOG_WARN("failed to init  ddl merge task param", K(ret));
      } else if (OB_FAIL(create_task(nullptr /* parent task*/, ddl_merge_task, merge_param))) {
        LOG_WARN("failed to create ddl merge taks ", K(ret));
      } else if (OB_FAIL(add_task(*ddl_merge_task))) {
        LOG_WARN("failed to add task", K(ret));
      }

      /* create merge task for lob tablet*/
      ObDDLTabletMergeDagParamV2 lob_merge_param;
      ObDDLMergePrepareTask *lob_merge_task = nullptr;
      if (OB_FAIL(ret)) {
      } else if (!tablet_context->lob_meta_tablet_id_.is_valid()) {
        /* skip */
      } else if (OB_FAIL(lob_merge_param.init(true  /*for major*/,
                                          true /* for lob*/,
                                          false /* for replay*/,
                                          mock_start_scn,
                                          direct_load_type_,
                                          ddl_task_param_,
                                          tablet_context))) {
        LOG_WARN("failed to init  ddl merge task param", K(ret));
      } else if (OB_FAIL(create_task(nullptr /* parent task*/, lob_merge_task, lob_merge_param))) {
        LOG_WARN("failed to create ddl merge taks ", K(ret));
      } else if (OB_FAIL(add_task(*lob_merge_task))) {
        LOG_WARN("failed to add task", K(ret));
      }
    }
  }
  return ret;
}

int ObDDLIndependentDag::add_scan_chunk(ObDDLChunk &ddl_chunk, const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!ddl_chunk.is_valid() || timeout_us < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(ddl_chunk), K(timeout_us));
  } else {
    ObDDLTabletContext *tablet_context = nullptr;
    ObDDLSlice *ddl_slice = nullptr;
    bool is_new_slice = false;
    const bool need_end_chunk = ddl_chunk.is_slice_end_ && (nullptr == ddl_chunk.chunk_data_ ||
                                                            !ddl_chunk.chunk_data_->is_end_chunk());
    
    if (OB_UNLIKELY(nullptr != ddl_chunk.chunk_data_ &&
                    !(ddl_chunk.chunk_data_->is_ddl_row_tmp_files_type() || ddl_chunk.chunk_data_->is_end_chunk()))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid chunk data", K(ret), KPC(ddl_chunk.chunk_data_));
    } else if (OB_FAIL(get_tablet_context(ddl_chunk.tablet_id_, tablet_context))) {
      LOG_WARN("get tablet context failed", K(ret), K(ddl_chunk));
    } else if (OB_FAIL(tablet_context->get_or_create_slice(ddl_chunk.slice_idx_, ddl_slice, is_new_slice))) {
      LOG_WARN("get ddl slice failed", K(ret));
    } else if (nullptr != ddl_chunk.chunk_data_ &&
               OB_FAIL(push_chunk(ddl_slice, ddl_chunk.chunk_data_))) {
      LOG_WARN("push chunk failed", K(ret), KPC(ddl_slice));
    } else if (FALSE_IT(ddl_chunk.chunk_data_ = nullptr)) {
    } else if (need_end_chunk) {
      ObChunk *end_chunk = OB_NEW(ObChunk, ObMemAttr("ddl_end_chunk"));
      if (OB_ISNULL(end_chunk)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else {
        end_chunk->set_end_chunk();
        if (OB_FAIL(push_chunk(ddl_slice, end_chunk))) {
          LOG_WARN("push end chunk failed", K(ret), KPC(ddl_slice));
          int tmp_ret = OB_SUCCESS;
          // ignore ret
          (void)finish_chunk(end_chunk);
        }
      }
    }
    if (OB_SUCC(ret) && is_new_slice) {
      const ObIndexType index_type = tablet_context->tablet_param_.storage_schema_->get_index_type();
      LOG_INFO("add pipeline", K(ret), K(index_type));
      if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, index_type))) {
        LOG_WARN("fail to add pipeline", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
      // ignore ret
      (void)finish_chunk(ddl_chunk.chunk_data_);
    }
  }
  return ret;
}

int ObDDLIndependentDag::push_chunk(ObDDLSlice *ddl_slice, ObChunk *&chunk_data)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == ddl_slice || nullptr == chunk_data)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", KR(ret), KP(ddl_slice), KP(chunk_data));
  } else {
    while (OB_SUCC(ret)) {
      if (OB_UNLIKELY(is_final_status())) {
        ret = get_dag_ret();
        ret = COVER_SUCC(OB_CANCELED);
        LOG_WARN("dag is stoped", K(ret));
      } else if (OB_FAIL(ddl_slice->push_chunk(chunk_data))) {
        if (OB_UNLIKELY(OB_EAGAIN != ret)) {
          LOG_WARN("push chunk failed", K(ret), KPC(chunk_data));
        } else {
          ret = OB_SUCCESS;
        }
      } else {
        break;
      }
    }
  }
  return ret;
}

int ObDDLIndependentDag::add_pipeline(
    ObDDLTabletContext *tablet_context,
    ObDDLSlice *ddl_slice,
    const ObIndexType &index_type)
{
  int ret = OB_SUCCESS;
  if (ObDDLUtil::is_vector_index_complement(index_type)) {
    if (OB_FAIL(add_vector_index_append_pipeline(index_type, tablet_context, ddl_slice))) {
      LOG_WARN("add vector index pipeline failed", K(ret));
    }
  } else {
    ObDDLMemoryFriendWriteMacroBlockPipeline *pipeline = nullptr;
    if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, pipeline))) {
      LOG_WARN("fail to add pipeline", K(ret), KPC(ddl_slice));
    }
  }
  return ret;
}

int ObDDLIndependentDag::add_vector_index_append_pipeline(const ObIndexType &index_type, ObDDLTabletContext *tablet_context, ObDDLSlice *ddl_slice)
{
  int ret = OB_SUCCESS;
  if (schema::is_vec_index_snapshot_data_type(index_type)) {
    ObHNSWAppendPipeline *pipeline = nullptr;
    if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, pipeline))) {
      LOG_WARN("init hnsw index failed", K(ret));
    }
  } else if (schema::is_local_vec_ivf_centroid_index(index_type)) {
    ObIVFCenterAppendPipeline *pipeline = nullptr;
    if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, pipeline))) {
      LOG_WARN("init hnsw index failed", K(ret));
    }
  } else if (schema::is_vec_ivfsq8_meta_index(index_type)) {
    ObIVFSq8MetaAppendPipeline *pipeline = nullptr;
    if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, pipeline))) {
      LOG_WARN("init hnsw index failed", K(ret));
    }
  } else if (schema::is_vec_ivfpq_pq_centroid_index(index_type)) {
    ObIVFPqAppendPipeline *pipeline = nullptr;
    if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, pipeline))) {
      LOG_WARN("init hnsw index failed", K(ret));
    }
  } else if (schema::is_hybrid_vec_index_embedded_type(index_type)) {
    ObHNSWEmbeddingAppendAndWritePipeline *pipeline = nullptr;
    if (OB_FAIL(add_pipeline(tablet_context, ddl_slice, pipeline))) {
      LOG_WARN("init hnsw index failed", K(ret));
    }
  }
  return ret;
}

int ObDDLIndependentDag::alloc_vector_index_write_and_build_pipeline(
    const ObIndexType &index_type,
    const ObIArray<ObTabletID> &tablet_ids,
    ObIArray<ObITask *> &vector_index_task_array)
{
  int ret = OB_SUCCESS;
  vector_index_task_array.reuse();
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids.count(); ++i) {
    const ObTabletID &tablet_id = tablet_ids.at(i);
    ObITask *vector_index_task = nullptr;
    if (schema::is_vec_index_snapshot_data_type(index_type)) {
      ObHNSWBuildAndWritePipeline *pipeline = nullptr;
      if (OB_FAIL(alloc_task(pipeline))) {
        LOG_WARN("alloc task failed", K(ret));
      } else if (OB_FAIL(pipeline->init(tablet_id))) {
        LOG_WARN("init pipeline failed", K(ret));
      } else {
        vector_index_task = pipeline;
      }
    } else if (schema::is_local_vec_ivf_centroid_index(index_type)) {
      ObIVFCenterBuildAndWritePipeline *pipeline = nullptr;
      if (OB_FAIL(alloc_task(pipeline))) {
        LOG_WARN("alloc task failed", K(ret));
      } else if (OB_FAIL(pipeline->init(tablet_id))) {
        LOG_WARN("init pipeline failed", K(ret));
      } else {
        vector_index_task = pipeline;
      }
    } else if (schema::is_vec_ivfsq8_meta_index(index_type)) {
      ObIVFSq8MetaBuildAndWritePipeline *pipeline = nullptr;
      if (OB_FAIL(alloc_task(pipeline))) {
        LOG_WARN("alloc task failed", K(ret));
      } else if (OB_FAIL(pipeline->init(tablet_id))) {
        LOG_WARN("init pipeline failed", K(ret));
      } else {
        vector_index_task = pipeline;
      }
    } else if (schema::is_vec_ivfpq_pq_centroid_index(index_type)) {
      ObIVFPqBuildAndWritePipeline *pipeline = nullptr;
      if (OB_FAIL(alloc_task(pipeline))) {
        LOG_WARN("init hnsw index failed", K(ret));
      } else if (OB_FAIL(pipeline->init(tablet_id))) {
        LOG_WARN("init pipeline failed", K(ret));
      } else {
        vector_index_task = pipeline;
      }
    }
    if (OB_SUCC(ret) && nullptr != vector_index_task) {
      if (OB_FAIL(vector_index_task_array.push_back(vector_index_task))) {
        LOG_WARN("push back vector index task failed", K(ret));
      } else {
        LOG_INFO("alloc vector index write and build pipeline", K(index_type), K(*vector_index_task));
      }
    }
  }
  return ret;
}

template<typename T>
int ObDDLIndependentDag::add_pipeline(ObDDLTabletContext *tablet_context, ObDDLSlice *ddl_slice, T *&pipeline)
{
  int ret = OB_SUCCESS;
  pipeline = nullptr;
  if (OB_UNLIKELY(nullptr == tablet_context || nullptr == ddl_slice)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(tablet_context), KP(ddl_slice));
  } else if (OB_FAIL(alloc_task(pipeline))) {
    LOG_WARN(" alloc pipeline failed", K(ret));
  } else if (OB_FAIL(pipeline->init(ddl_slice))) {
    LOG_WARN("init pipeline failed", K(ret));
  } else if (nullptr != tablet_context->scan_task_ &&
             OB_FAIL(pipeline->add_child(*tablet_context->scan_task_))) {
    LOG_WARN("fail to add child", K(ret));
  } else {
    inc_pipeline_count();
    if (OB_FAIL(add_task(*pipeline))) {
      LOG_WARN("add pipeline failed", K(ret));
      dec_pipeline_count();
    }
  }
  return ret;
}

void ObDDLIndependentDag::set_ret_code(const int ret_code)
{
  if (OB_SUCCESS == ret_code_) {
    ATOMIC_SET(&ret_code_, ret_code);
  }
}

int ObDDLIndependentDag::generate_start_tasks(ObIArray<ObITask *> &start_tasks)
{
  int ret = OB_SUCCESS;
  start_tasks.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLIndependentDag not init", KR(ret), KP(this));
  }
  return ret;
}

int ObDDLIndependentDag::full_generate_write_macro_block_tasks(ObIArray<ObITask *> &write_macro_block_tasks, ObITask *next_task)
{
  int ret = OB_SUCCESS;
  // scan_task -> vector_index_tasks -> merge_tasks -> [next_task]
  ObDDLScanTask *scan_task = nullptr;
  ObArray<ObITask *> vector_index_tasks;
  ObArray<ObITask*> data_merge_tasks;
  ObArray<ObITask*> lob_merge_tasks;
  // scan_task
  if (OB_FAIL(alloc_task(scan_task))) {
    LOG_WARN("fail to alloc scan task", KR(ret));
  } else if (OB_FAIL(scan_task->init(this))) {
    LOG_WARN("fail to init scan task", K(ret));
  } else if (OB_FAIL(write_macro_block_tasks.push_back(scan_task))) {
    LOG_WARN("fail to push back", KR(ret));
  }
  // vector_index_task
  else if (OB_FAIL(alloc_vector_index_write_and_build_pipeline(ddl_table_schema_.table_item_.index_type_, tablet_ids_, vector_index_tasks))) {
    LOG_WARN("alloc vector index failed", K(ret));
  } else if (!vector_index_tasks.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < vector_index_tasks.count(); ++i) {
      ObITask *vector_index_task = vector_index_tasks.at(i);
      if (OB_FAIL(write_macro_block_tasks.push_back(vector_index_task))) {
        LOG_WARN("fail to push back", KR(ret));
      }
    }
  }
  // merge_tasks
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(init_merge_tasks(true, data_merge_tasks, lob_merge_tasks))) {
    LOG_WARN("fail to init merge tasks", KR(ret));
  } else if (OB_UNLIKELY(data_merge_tasks.empty() ||
                         (!lob_merge_tasks.empty() &&
                          data_merge_tasks.count() != lob_merge_tasks.count()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected merge tasks", KR(ret), K(data_merge_tasks.count()),
             K(lob_merge_tasks.count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < data_merge_tasks.count(); ++i) {
      ObITask *data_merge_task = data_merge_tasks.at(i);
      ObITask *lob_merge_task = lob_merge_tasks.empty() ? nullptr : lob_merge_tasks.at(i);
      if (OB_FAIL(write_macro_block_tasks.push_back(data_merge_task))) {
        LOG_WARN("fail to push back", KR(ret));
      } else if (nullptr != lob_merge_task &&
                 OB_FAIL(write_macro_block_tasks.push_back(lob_merge_task))) {
        LOG_WARN("fail to push back", KR(ret));
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (vector_index_tasks.empty()) {
    for (int64_t i = 0; OB_SUCC(ret) && i < data_merge_tasks.count(); ++i) {
      ObITask *data_merge_task = data_merge_tasks.at(i);
      ObITask *lob_merge_task = lob_merge_tasks.empty() ? nullptr : lob_merge_tasks.at(i);
      if (OB_FAIL(scan_task->add_child(*data_merge_task))) {
        LOG_WARN("fail to add child", K(ret));
      } else if (nullptr != lob_merge_task && OB_FAIL(scan_task->add_child(*lob_merge_task))) {
        LOG_WARN("fail to add child", K(ret));
      } else if (nullptr != next_task) {
        if (OB_FAIL(data_merge_task->add_child(*next_task))) {
          LOG_WARN("fail to add child", K(ret));
        } else if (nullptr != lob_merge_task && OB_FAIL(lob_merge_task->add_child(*next_task))) {
          LOG_WARN("fail to add child", K(ret));
        }
      }
    }
  } else {
    if (OB_UNLIKELY(data_merge_tasks.count() != vector_index_tasks.count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected task count not match", KR(ret), K(data_merge_tasks.count()), K(vector_index_tasks.count()));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < vector_index_tasks.count(); ++i) {
      ObITask *vector_index_task = vector_index_tasks.at(i);
      ObITask *data_merge_task = data_merge_tasks.at(i);
      ObITask *lob_merge_task = lob_merge_tasks.empty() ? nullptr : lob_merge_tasks.at(i);
      if (OB_FAIL(scan_task->add_child(*vector_index_task))) {
        LOG_WARN("fail to add child", KR(ret));
      } else if (OB_FAIL(vector_index_task->add_child(*data_merge_task))) {
        LOG_WARN("fail to add child", KR(ret));
      } else if (nullptr != lob_merge_task && OB_FAIL(vector_index_task->add_child(*lob_merge_task))) {
        LOG_WARN("fail to add child", KR(ret));
      } else if (nullptr != next_task) {
        if (OB_FAIL(data_merge_task->add_child(*next_task))) {
          LOG_WARN("fail to add child", K(ret));
        } else if (nullptr != lob_merge_task && OB_FAIL(lob_merge_task->add_child(*next_task))) {
          LOG_WARN("fail to add child", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObDDLIndependentDag::generate_write_macro_block_tasks(ObIArray<ObITask *> &write_macro_block_tasks, ObITask *next_task)
{
  int ret = OB_SUCCESS;
  write_macro_block_tasks.reset();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLIndependentDag not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(use_tablet_mode())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected mode", KR(ret), KPC(this));
  } else if (OB_FAIL(full_generate_write_macro_block_tasks(write_macro_block_tasks, next_task))) {
    LOG_WARN("fail to full_generate_write_macro_block_tasks", KR(ret));
  }
  return ret;
}

int ObDDLIndependentDag::generate_tablet_write_macro_block_tasks(
    const ObTabletID &tablet_id,
    ObIArray<share::ObITask *> &write_macro_block_tasks,
    ObITask *next_task)
{
  int ret = OB_SUCCESS;
  write_macro_block_tasks.reset();
  ObDDLTabletContext *tablet_context = nullptr;
  ObDDLTabletScanTask *scan_task = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObDDLIndependentDag not init", KR(ret), KP(this));
  } else if (OB_UNLIKELY(!use_tablet_mode())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected mode", KR(ret), KPC(this));
  } else if (OB_FAIL(get_tablet_context(tablet_id, tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret), K(tablet_id));
  } else if (OB_ISNULL(scan_task = tablet_context->scan_task_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected scan task is null", K(ret), K(tablet_id), KPC(tablet_context));
  } else if (FALSE_IT(tablet_context->scan_task_ = nullptr)) {
  } else if (OB_FAIL(write_macro_block_tasks.push_back(scan_task))) {
    LOG_WARN("fail to push back", KR(ret));
  } else {
    // scan_task -> merge_tasks -> [next_task]
    ObITask *data_merge_task = nullptr;
    ObITask *lob_merge_task = nullptr;
    // merge_task
    if (OB_FAIL(init_tablet_merge_task(tablet_id, true/*for_major*/, data_merge_task, lob_merge_task))) {
      LOG_WARN("fail to init tablet merge task", KR(ret));
    } else if (OB_FAIL(write_macro_block_tasks.push_back(data_merge_task))) {
      LOG_WARN("fail to push back", KR(ret));
    } else if (nullptr != lob_merge_task &&
               OB_FAIL(write_macro_block_tasks.push_back(lob_merge_task))) {
      LOG_WARN("fail to push back", KR(ret));
    }
    // 依赖关系
    else if (OB_FAIL(scan_task->add_child(*data_merge_task))) {
      LOG_WARN("fail to add child", KR(ret));
    } else if (nullptr != next_task && OB_FAIL(data_merge_task->add_child(*next_task))) {
      LOG_WARN("fail to add child", KR(ret));
    } else if (nullptr != lob_merge_task) {
      if (OB_FAIL(scan_task->add_child(*lob_merge_task))) {
        LOG_WARN("fail to add child", KR(ret));
      } else if (nullptr != next_task && OB_FAIL(lob_merge_task->add_child(*next_task))) {
        LOG_WARN("fail to add child", KR(ret));
      }
    }
  }
  return ret;
}

int ObDDLIndependentDag::init_tablet_merge_task(
    const ObTabletID &tablet_id,
    const bool for_major,
    ObITask *&data_task,
    ObITask *&lob_task)
{
  int ret = OB_SUCCESS;
  data_task = nullptr;
  lob_task = nullptr;

  share::SCN mock_start_scn;
  ObDDLTabletContext *tablet_context = nullptr;
  ObDDLTabletMergeDagParamV2 merge_param;
  ObDDLMergePrepareTask *ddl_merge_task = nullptr;
  if (OB_FAIL(mock_start_scn.convert_for_tx(SS_DDL_START_SCN_VAL))) {
    LOG_WARN("failed to convert for tx", K(ret));
  } else if (OB_FAIL(get_tablet_context(tablet_id, tablet_context))) {
    LOG_WARN("get ddl tablet context failed", K(ret), K(tablet_id));
  } 
  
  if (OB_FAIL(ret)) {
  } else {
    if (OB_FAIL(merge_param.init(for_major  /*for major*/,
      false /* for lob*/,
      false /* for replay*/,
      mock_start_scn, 
      direct_load_type_,
      ddl_task_param_,
      tablet_context))) {
      LOG_WARN("failed to init  ddl merge task param", K(ret));
    } else if (!for_major && FALSE_IT(merge_param.set_merge_all_slice())) {
    } else if (OB_FAIL(alloc_task(ddl_merge_task))) {
    LOG_WARN("failed to alloc ddl merge task", K(ret));
    } else if (OB_FAIL(ddl_merge_task->init(merge_param))) {
    LOG_WARN("failed to init ddl merge task", K(ret));
    } else {
      data_task = ddl_merge_task;
    }
  }

  /* create merge task for lob tablet*/
  ObDDLTabletMergeDagParamV2 lob_merge_param;
  ObDDLMergePrepareTask *lob_merge_task = nullptr;
  if (OB_FAIL(ret)) {
  } else if (tablet_context->lob_meta_tablet_id_.is_valid()) {
    if (OB_FAIL(lob_merge_param.init(for_major  /*for major*/,
                                      true /* for lob*/,
                                      false /* for replay*/,
                                      mock_start_scn, 
                                      direct_load_type_,
                                      ddl_task_param_,
                                      tablet_context))) {
      LOG_WARN("failed to init  ddl merge task param", K(ret));
    } else if (!for_major && FALSE_IT(lob_merge_param.set_merge_all_slice())) {
    } else if (OB_FAIL(alloc_task(lob_merge_task))) {
      LOG_WARN("failed to create ddl merge taks ", K(ret));
    } else if (OB_FAIL(lob_merge_task->init(lob_merge_param))) {
      LOG_WARN("failed to init task", K(ret));
    } else {
      lob_task = lob_merge_task;
    }
  }
  return ret;
}

int ObDDLIndependentDag::init_merge_tasks(bool for_major, ObArray<ObITask*> &data_merge_tasks, ObArray<ObITask*> &lob_merge_tasks)
{
  int ret = OB_SUCCESS;
  data_merge_tasks.reset();
  lob_merge_tasks.reset();
  for (int64_t i = 0; OB_SUCC(ret) && i < tablet_ids_.count(); ++i) {
    const ObTabletID &tablet_id = tablet_ids_.at(i);
    ObITask *data_merge_task = nullptr;
    ObITask *lob_merge_task = nullptr;
    if (OB_FAIL(init_tablet_merge_task(tablet_id, for_major, data_merge_task, lob_merge_task))) {
      LOG_WARN("fail to init tablet merge task", KR(ret));
    } else if (OB_ISNULL(data_merge_task)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected data merge task is null", KR(ret));
    } else if (OB_FAIL(data_merge_tasks.push_back(data_merge_task))) {
      LOG_WARN("failed to push back merge task", K(ret));
    } else if (nullptr != lob_merge_task && OB_FAIL(lob_merge_tasks.push_back(lob_merge_task))) {
      LOG_WARN("failed to push back merge task", K(ret));
    }
  }
  return ret;
}

int ObDDLIndependentDag::finish_chunk(ObChunk *&chunk)
{
  int ret = OB_SUCCESS;
  if (nullptr != chunk) {
    chunk->~ObChunk();
    ob_free(chunk);
    chunk = nullptr;
  }
  return ret;
}
