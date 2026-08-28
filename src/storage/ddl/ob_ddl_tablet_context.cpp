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

#include "storage/ddl/ob_ddl_tablet_context.h"
#include "share/rc/ob_server_runtime.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "storage/ddl/ob_ddl_pipeline.h"
#include "storage/ob_storage_schema_util.h"
#include "storage/ddl/ob_ddl_merge_helper.h"

#define USING_LOG_PREFIX STORAGE

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;


ObDDLTabletContext::MergeCtx::~MergeCtx() 
{
  fifo_.reset();
  for (hash::ObHashMap<int64_t, ObArray<ObTableHandleV2>*>::const_iterator iter = slice_sstables_.begin();
      iter != slice_sstables_.end();
      iter++) {
    if (nullptr != iter->second) {
      iter->second->~ObArray<ObTableHandleV2>();
    }
  }
  if (nullptr != merge_helper_) {
    merge_helper_->~ObIDDLMergeHelper();
    merge_helper_ = nullptr;
  }
  slice_sstables_.destroy();
  arena_.reset();
  is_inited_ = false;
}

int ObDDLTabletContext::MergeCtx::init(const ObDirectLoadType direct_load_type)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_FAIL(ObIDDLMergeHelper::get_merge_helper(arena_, direct_load_type, merge_helper_))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

ObDDLSlice::ObDDLSlice()
  : is_inited_(false), has_end_chunk_(false), slice_idx_(-1)
{

}

ObDDLSlice::~ObDDLSlice()
{
  int ret = OB_SUCCESS;
  while (OB_SUCC(ret)) {
    void *tmp = nullptr;
    if (OB_FAIL(chunk_queue_.pop(tmp))) {
      if (OB_ENTRY_NOT_EXIST != ret) {
        LOG_WARN("pop chunk failed", K(ret));
      }
    } else {
      ObChunk *tmp_chunk = (ObChunk *)tmp;
      if (OB_NOT_NULL(tmp_chunk)) {
        tmp_chunk->~ObChunk();
        ob_free(tmp_chunk);
      }
    }
  }
  chunk_queue_.destroy();
}

int ObDDLSlice::init(const ObTabletID &tablet_id, const int64_t slice_idx)
{
  int ret = OB_SUCCESS;
  const int64_t queue_cap = 100;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(tablet_id), K(slice_idx));
  } else if (OB_FAIL(chunk_queue_.init(queue_cap, "DDL_ChunkQueue"))) {
  } else {
    tablet_id_ = tablet_id;
    slice_idx_ = slice_idx;
    is_inited_ = true;
  }
  return ret;
}

int ObDDLSlice::push_chunk(ObChunk *&chunk_data)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(chunk_data)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(chunk_data));
  } else {
    const bool is_end_chunk = chunk_data->is_end_chunk();
    const int64_t DEFAULT_TIMEOUT_US = 5LL * 1000 * 1000; // 5s
    if (OB_FAIL(chunk_queue_.push(chunk_data, DEFAULT_TIMEOUT_US))) {
      if (OB_UNLIKELY(OB_TIMEOUT != ret)) {
        LOG_WARN("push chunk data failed", K(ret), KPC(chunk_data));
      } else {
        ret = OB_EAGAIN;
      }
    } else {
      chunk_data = nullptr;
      has_end_chunk_ = is_end_chunk;
    }
  }
  return ret;
}

int ObDDLSlice::pop_chunk(ObChunk *&chunk_data)
{
  int ret = OB_SUCCESS;
  void *tmp = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(chunk_queue_.pop(tmp))) {
    if (OB_ENTRY_NOT_EXIST != ret) {
      LOG_WARN("pop chunk data failed", K(ret));
    }
  } else {
    chunk_data = (ObChunk *)tmp;
  }
  return ret;
}

ObDDLTabletContext::ObDDLTabletContext()
  : is_inited_(false), arena_(ObMemAttr("ddl_tblt_ctx")),
    slice_count_(0), table_slice_offset_(0), scan_task_(nullptr),
    lob_read_service_(nullptr),
    last_lob_id_(0), last_autoinc_val_(0), bucket_count_(0),
    vector_index_ctx_(nullptr)
{

}

ObDDLTabletContext::~ObDDLTabletContext()
{
  reset();
}

int init_tablet_param(ObTablet *tablet, ObStorageSchema *storage_schema, const ObDirectLoadType direct_load_type, ObIAllocator &allocator, ObWriteTabletParam &tablet_param)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == tablet || nullptr == storage_schema)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(tablet), KP(storage_schema));
  } else {
    ObDDLKvMgrHandle ddl_kv_mgr_handle;
    const ObTabletMeta &tablet_meta = tablet->get_tablet_meta();
    tablet_param.is_micro_index_clustered_ = tablet_meta.micro_index_clustered_;
    tablet_param.storage_schema_ = storage_schema;
    if (OB_FAIL(tablet->get_ddl_kv_mgr(ddl_kv_mgr_handle, true /*try_create]*/))) {
    }
  }
  return ret;
}

int ObDDLTabletContext::init(
    const ObTabletID &tablet_id,
    const int64_t ddl_thread_count,
    const int64_t snapshot_version,
    const ObDirectLoadType direct_load_type,
    const ObDDLTableSchema &ddl_table_schema,
    common::ObILobReadService &lob_read_service)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(!tablet_id.is_valid() || ddl_thread_count <= 0 ||
                         !is_valid_direct_load(direct_load_type))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invlaid argument", K(ret), K(tablet_id), K(ddl_thread_count),
             K(direct_load_type));
  } else {
    tablet_id_ = tablet_id;
    lob_read_service_ = &lob_read_service;
    bucket_count_ = ddl_thread_count * 2;
    if (OB_FAIL(slice_map_.create(bucket_count_, ObMemAttr("tblt_slice_map")))) {
    } else if (OB_FAIL(bucket_lock_.init(bucket_count_))) {
    } else {
      ObLS *ls = nullptr;
      ObTabletHandle tablet_handle;
      ObTabletBindingMdsUserData mds_data;
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObLSService>()->get_ls(ls))) {
      } else if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls, tablet_id, tablet_handle, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
      } else if (OB_FAIL(init_tablet_param(tablet_handle.get_obj(), ddl_table_schema.storage_schema_, direct_load_type, arena_, tablet_param_))) {
      } else if (OB_FAIL(merge_ctx_.init(direct_load_type))) {
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_ddl_data(share::SCN::max_scn(), mds_data))) {
      } else if (mds_data.lob_meta_tablet_id_.is_valid()) {
        lob_meta_tablet_id_ = mds_data.lob_meta_tablet_id_;
        ObTabletHandle lob_meta_tablet_handle;
        if (OB_FAIL(ObDDLStorageUtil::ddl_get_tablet(ls, lob_meta_tablet_id_, lob_meta_tablet_handle, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
        } else if (OB_FAIL(init_tablet_param(lob_meta_tablet_handle.get_obj(), ddl_table_schema.lob_meta_storage_schema_, direct_load_type, arena_, lob_meta_tablet_param_))) {
        } else if (OB_FAIL(lob_merge_ctx_.init(direct_load_type))) {
        }
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(init_vector_index_context(snapshot_version, ddl_table_schema))) {
      } else {
        is_inited_ = true;
        LOG_INFO("[CS-Replica] init tablet context", K(tablet_id), K(direct_load_type), K(tablet_param_));
      }
    }
  }
  return ret;
}

int ObDDLTabletContext::init_vector_index_context(const int64_t snapshot_version, const ObDDLTableSchema &ddl_table_schema)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (ddl_table_schema.table_item_.vec_dim_ > 0) {
    if (OB_ISNULL(buf = arena_.alloc(sizeof(ObVectorIndexTabletContext)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("allocate memory failed", K(ret));
    } else {
      vector_index_ctx_ = new (buf) ObVectorIndexTabletContext();
      if (OB_FAIL(vector_index_ctx_->init(tablet_id_, tablet_param_.storage_schema_->get_index_type(), snapshot_version, ddl_table_schema))) {
      }
    }
  }
  return ret;
}

void ObDDLTabletContext::reset()
{
  is_inited_ = false;
  tablet_id_.reset();
  tablet_param_.reset();
  lob_meta_tablet_id_.reset();
  lob_meta_tablet_param_.reset();
  slice_count_ = 0;
  table_slice_offset_ = 0;
  scan_task_ = nullptr;
  last_lob_id_ = 0;
  last_autoinc_val_ = 0;
  bucket_lock_.destroy();
  bucket_count_ = 0;
  SLICE_MAP::iterator slice_iter = slice_map_.begin();
  for (; slice_iter != slice_map_.end(); ++slice_iter) {
    ObDDLSlice *ddl_slice = slice_iter->second;
    if (OB_NOT_NULL(ddl_slice)) {
      ddl_slice->~ObDDLSlice();
      ob_free(ddl_slice);
    }
  }
  slice_map_.destroy();

  if (nullptr != vector_index_ctx_) {
    vector_index_ctx_->~ObVectorIndexTabletContext();
    arena_.free(vector_index_ctx_);
    vector_index_ctx_ = nullptr;
  }
  arena_.reset();
}

int ObDDLTabletContext::update_max_lob_id(const int64_t lob_id)
{
  int ret = OB_SUCCESS;
  ObMutexGuard guard(mutex_);
  last_lob_id_ = max(last_lob_id_, lob_id);
  return ret;
}

int ObDDLTabletContext::update_max_autoinc_val(const int64_t val)
{
  int ret = OB_SUCCESS;
  ObMutexGuard guard(mutex_);
  last_autoinc_val_ = max(last_autoinc_val_, val);
  return ret;
}

int ObDDLTabletContext::get_or_create_slice(const int64_t slice_idx, ObDDLSlice *&ddl_slice, bool &is_new_slice)
{
  int ret = OB_SUCCESS;
  ddl_slice = nullptr;
  is_new_slice = false;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(slice_idx));
  } else {
    ObBucketRLockGuard guard(bucket_lock_, slice_idx % bucket_count_);
    if (OB_FAIL(slice_map_.get_refactored(slice_idx, ddl_slice))) {
      if (OB_HASH_NOT_EXIST != ret) {
        LOG_WARN("get slice failed", K(ret));
      }
    } else {
      is_new_slice = false;
    }
  }
  if (OB_HASH_NOT_EXIST == ret) {
    ObBucketWLockGuard guard(bucket_lock_, slice_idx % bucket_count_);
    ret = slice_map_.get_refactored(slice_idx, ddl_slice);
    if (OB_SUCCESS == ret) {
      is_new_slice = false;
    } else if (OB_HASH_NOT_EXIST != ret) {
      LOG_WARN("get slice failed", K(ret));
    } else {
      ObDDLSlice *tmp_slice = OB_NEW(ObDDLSlice, ObMemAttr("dag_ddl_slice"));
      if (OB_ISNULL(tmp_slice)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("allocate memory failed", K(ret));
      } else if (OB_FAIL(tmp_slice->init(tablet_id_, slice_idx))) {
      } else if (OB_FAIL(slice_map_.set_refactored(slice_idx, tmp_slice))) {
      } else {
        ddl_slice = tmp_slice;
        is_new_slice = true;
      }
      if (OB_FAIL(ret) && nullptr != tmp_slice) {
        tmp_slice->~ObDDLSlice();
        ob_free(tmp_slice);
        tmp_slice = nullptr;
      }
    }
  }
  return ret;
}

int ObDDLTabletContext::remove_slice(const int64_t slice_idx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_UNLIKELY(slice_idx < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(slice_idx));
  } else {
    ObDDLSlice *ddl_slice = nullptr;
    ObBucketWLockGuard guard(bucket_lock_, slice_idx % bucket_count_);
    if (OB_FAIL(slice_map_.erase_refactored(slice_idx, &ddl_slice))) {
    } else if (OB_ISNULL(ddl_slice)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ddl slice is null", K(ret), KP(ddl_slice));
    } else {
      ddl_slice->~ObDDLSlice();
      ob_free(ddl_slice);
    }
  }
  return ret;
}

int ObDDLTabletContext::get_all_slices(ObIArray<ObDDLSlice *> &ddl_slices)
{
  int ret = OB_SUCCESS;
  ddl_slices.reuse();
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret), K(is_inited_));
  } else if (OB_FAIL(ddl_slices.reserve(slice_map_.size()))) {
  } else {
    SLICE_MAP::iterator slice_iter = slice_map_.begin();
    for (; slice_iter != slice_map_.end(); ++slice_iter) {
      ObDDLSlice *ddl_slice = slice_iter->second;
      if (OB_ISNULL(ddl_slice)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ddl slice is null", K(ret), K(ddl_slice));
      } else if (OB_FAIL(ddl_slices.push_back(ddl_slice))) {
      }
    }
  }
  return ret;
}
