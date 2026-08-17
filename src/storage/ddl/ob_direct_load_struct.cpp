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

#define USING_LOG_PREFIX STORAGE

#include "ob_direct_load_struct.h"
#include "data_plane/vector/ob_i_vector_index_runtime.h"
#include "storage/ddl/ob_ddl_vector_utils.h"
#include "storage/ddl/ob_ddl_storage_util.h"
#include "storage/tx/ob_trans_service.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_ddl_error_message_table_operator.h"
#include "storage/ob_tablet_autoincrement_service.h"
#include "storage/ob_storage_schema_util.h"
#include "share/ob_lob_access_utils.h"
#include "share/vector/ob_bit_vector.h"
#include "data_plane/access/ob_datum_reshape.h"
#include "storage/tx_storage/ob_ls_service.h"
#include "query/engine/vector/ob_continuous_base.h"
#include "query/engine/vector/ob_discrete_format.h"
#include "query/engine/vector/ob_uniform_base.h"
#include "query/vector/ob_vector_index_util.h"
#include "storage/vector_index/ob_vector_kmeans_ctx.h"
#include "storage/api/storage/vector/ob_i_vector_index_runtime.h"
#include "query/engine/expr/ob_array_expr_utils.h"
#include "storage/blocksstable/index_block/ob_macro_meta_temp_store.h"
#include "storage/ddl/ob_ddl_tablet_context.h"
#include "storage/ddl/ob_ddl_merge_helper.h"
#include "storage/ddl/ob_ddl_direct_load_utils.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::sql;
using namespace oceanbase::transaction;

int ObTabletDirectLoadInsertParam::assign(const ObTabletDirectLoadInsertParam &other_param)
{
  int ret = OB_SUCCESS;
  if (other_param.common_param_.is_valid()) {
    common_param_ = other_param.common_param_;
  }
  if (other_param.runtime_only_param_.is_valid()) {
    runtime_only_param_ = other_param.runtime_only_param_;
  }
  is_replay_ = other_param.is_replay_;
  return ret;
}

ObLobMetaRowIterator::ObLobMetaRowIterator()
  : is_inited_(false), iter_(nullptr), trans_version_(0),
    tmp_row_(), lob_meta_write_result_()
{
}

ObLobMetaRowIterator::~ObLobMetaRowIterator()
{
  reset();
}

int ObLobMetaRowIterator::init(ObLobMetaWriteIter *iter,
                                const int64_t trans_version)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("init twice", K(ret));
  } else if (OB_ISNULL(iter) || OB_UNLIKELY(trans_version < 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), KP(iter), K(trans_version));
  } else if (!tmp_row_.is_valid() && OB_FAIL(tmp_row_.init(ObLobMetaUtil::LOB_META_COLUMN_CNT + ObLobMetaUtil::SKIP_INVALID_COLUMN))) {
    LOG_WARN("Failed to init datum row", K(ret));
  } else {
    iter_ = iter;
    trans_version_ = trans_version;
    is_inited_ = true;
  }
  return ret;
}

void ObLobMetaRowIterator::reset()
{
  is_inited_ = false;
  iter_ = nullptr;
  trans_version_ = 0;
  tmp_row_.reset();
}

void ObLobMetaRowIterator::reuse()
{
  is_inited_ = false;
  iter_ = nullptr;
  trans_version_ = 0;
  tmp_row_.reuse();
}

int ObLobMetaRowIterator::get_next_row(const blocksstable::ObDatumRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(iter_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObLobMetaWriteIter is nullptr", K(ret));
  } else if (OB_FAIL(iter_->get_next_row(lob_meta_write_result_))) {
    if (OB_UNLIKELY(ret != OB_ITER_END)) {
      LOG_WARN("failed to get next row", K(ret));
    }
  } else {
    if (OB_FAIL(ObLobMetaUtil::transform_from_info_to_row(lob_meta_write_result_.info_, &tmp_row_, true))) {
    } else {
      tmp_row_.storage_datums_[ObLobMetaUtil::SEQ_ID_COL_ID + 1].set_int(-trans_version_);
      tmp_row_.storage_datums_[ObLobMetaUtil::SEQ_ID_COL_ID + 2].set_int(0);
      tmp_row_.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
      tmp_row_.mvcc_row_flag_.set_compacted_multi_version_row(true);
      tmp_row_.mvcc_row_flag_.set_first_multi_version_row(true);
      tmp_row_.mvcc_row_flag_.set_last_multi_version_row(true);
      tmp_row_.mvcc_row_flag_.set_uncommitted_row(false);
      row = &tmp_row_;
    }
  }
  return ret;
}

ObTabletDDLParam::ObTabletDDLParam()
  : direct_load_type_(ObDirectLoadType::DIRECT_LOAD_INVALID),
    start_scn_(SCN::min_scn()),
    commit_scn_(SCN::min_scn()),
    data_format_version_(0),
    table_key_(),
    snapshot_version_(0)
{

}

ObTabletDDLParam::~ObTabletDDLParam()
{

}

/**
 * ObChunkSliceStore
 */
bool ObTabletDDLParam::is_valid() const
{
  return is_valid_direct_load(direct_load_type_)
    && table_key_.is_valid()
    && start_scn_.is_valid_and_not_min()
    && commit_scn_.is_valid() && commit_scn_ != SCN::max_scn()
    && snapshot_version_ > 0
    && data_format_version_ > 0;
}

int ObDDLTableMergeDagParam::assign(const ObDDLTableMergeDagParam &merge_param)
{
  int ret = OB_SUCCESS;
  if (!merge_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_param));
  } else {
    direct_load_type_ = merge_param.direct_load_type_;
    tablet_id_        = merge_param.tablet_id_;
    rec_scn_          = merge_param.rec_scn_;
    is_commit_        = merge_param.is_commit_;
    start_scn_        = merge_param.start_scn_;
    data_format_version_ = merge_param.data_format_version_;
    snapshot_version_    = merge_param.snapshot_version_;
    table_key_           = merge_param.table_key_;
    if (is_commit_ && is_idem_type(direct_load_type_) &&
        OB_FAIL(user_data_.assign(arena_, merge_param.user_data_))) {
      LOG_WARN("failed to assign user data", K(ret));
    }
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::init(const bool for_major,
                                     const bool for_lob,
                                     const bool for_replay,
                                     const share::SCN start_scn,
                                     const ObDirectLoadType &direct_load_type,
                                     const ObDDLTaskParam &task_param,
                                     ObDDLTabletContext *tablet_ctx)
{
  int ret = OB_SUCCESS;
  ObWriteTabletParam              *tablet_param = nullptr;
  ObDDLTabletContext::MergeCtx    *merge_ctx    = nullptr;
  
  if ((is_full_direct_load(direct_load_type) && !for_replay
                                             && (0 == task_param.ddl_task_id_ || 0 == task_param.execution_id_))
      || (nullptr == tablet_ctx)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("ddl task id and execution id must be valid", K(ret), K(direct_load_type), K(task_param), KPC(tablet_ctx));
  } else if (FALSE_IT(tablet_param  = for_lob ? &tablet_ctx->lob_meta_tablet_param_ :
                                                 &tablet_ctx->tablet_param_)) {
  } else if (OB_ISNULL(tablet_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param should not be null", K(ret), K(for_lob), KPC(tablet_ctx));
  } else if (OB_ISNULL(merge_ctx  = for_lob ? &tablet_ctx->lob_merge_ctx_ :
                                              &tablet_ctx->merge_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be bull", K(ret));
  } else if (OB_ISNULL(tablet_param->storage_schema_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet param should not be null", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(merge_ctx->slice_sstables_.create(DDL_SLICE_BUCKET_NUM, ObMemAttr("Ddl_Mrg_Task")))) {
  } else {
    if (for_major) {
      table_key_.table_type_ = ObITable::TableType::MAJOR_SSTABLE;
      table_key_.version_range_.snapshot_version_ = task_param.snapshot_version_;
    } else {
      table_key_.table_type_ = ObITable::TableType::DDL_DUMP_SSTABLE;
      table_key_.scn_range_.start_scn_ = SCN::scn_dec(start_scn);
      table_key_.scn_range_.end_scn_ = start_scn;
    }

    if (OB_FAIL(ret)) {
    } else {
      table_key_.tablet_id_   =  for_lob ? tablet_ctx->lob_meta_tablet_id_ : tablet_ctx->tablet_id_;

      for_major_  = for_major;
      for_lob_    = for_lob;
      for_replay_ = for_replay;

      direct_load_type_ = direct_load_type;
      ddl_task_param_ = task_param;
      start_scn_ = start_scn;
      tablet_ctx_ = tablet_ctx;
      is_inited_ = true;
    }
  }
  return ret;
}

bool ObDDLTabletMergeDagParamV2::is_valid() const
{
  return is_inited_;
}

int ObDDLTabletMergeDagParamV2::get_merge_helper(ObIDDLMergeHelper *&merge_helper)
{
  int ret = OB_SUCCESS;
  merge_helper = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge dag param not inited", K(ret), KPC(this));
  } else if (nullptr == tablet_ctx_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ctx should not be null", K(ret), KPC(this));
  } else if (for_lob_) {
    merge_helper = tablet_ctx_->lob_merge_ctx_.merge_helper_;
  } else {
    merge_helper = tablet_ctx_->merge_ctx_.merge_helper_;
  } 
  
  if (OB_FAIL(ret)) {
  } else if (nullptr == merge_helper) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge helper should not be null", K(ret), KPC(this));
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::set_slice_sstable(const int64_t slice_idx, const ObTableHandleV2 &sstable_handle)
{
  int ret =  OB_SUCCESS;
  ObArray<ObTableHandleV2> *table_array = nullptr;
  ObDDLTabletContext::MergeCtx    *merge_ctx = nullptr;
  const int64_t row_store_sstable_slot_idx = 0;
  if (!is_inited_) {
    ret= OB_NOT_INIT;
    LOG_WARN("merge param has not been inited", K(ret), KPC(this), K(lbt()));
  } else if (OB_FAIL(get_merge_ctx(merge_ctx))) {
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be null", K(ret), KPC(this));
  } else if (OB_FAIL(merge_ctx->slice_sstables_.get_refactored(slice_idx, table_array))) {
  }

  if (OB_FAIL(ret)) {
  } else if (row_store_sstable_slot_idx >= table_array->size()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected slice sstable array size", K(ret), K(table_array->size()));
  } else {
    table_array->at(row_store_sstable_slot_idx) = sstable_handle;
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::assign(const ObDDLTabletMergeDagParamV2 &merge_dag_param)
{
  int ret = OB_SUCCESS;
  if (!merge_dag_param.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(merge_dag_param));
  } else {
    for_major_        = merge_dag_param.for_major_;
    for_replay_       = merge_dag_param.for_replay_;
    for_lob_          = merge_dag_param.for_lob_;
    merge_all_slice_  = merge_dag_param.merge_all_slice_;
    direct_load_type_ = merge_dag_param.direct_load_type_;
    start_scn_        = merge_dag_param.start_scn_;
    rec_scn_          = merge_dag_param.rec_scn_;
    table_key_        = merge_dag_param.table_key_;
    tablet_ctx_       = merge_dag_param.tablet_ctx_;
    is_inited_        = true;
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::get_tablet_param(ObTabletID &tablet_id,
                                                 ObWriteTabletParam *&tablet_param) const 
{
  int ret = OB_SUCCESS;
  tablet_param =  nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge dag param don't init yet", K(ret), KPC(this));
  } else if (OB_ISNULL(tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ctx should not be null", K(ret), KPC(this));
  } else if (for_lob_) {
    if (!tablet_ctx_->lob_meta_tablet_id_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lob tablet id not exist", K(ret), KPC(this));
    } else {
      tablet_id = tablet_ctx_->lob_meta_tablet_id_;
      tablet_param = &tablet_ctx_->lob_meta_tablet_param_;
    }
  } else {
    tablet_id = tablet_ctx_->tablet_id_;
    tablet_param = &tablet_ctx_->tablet_param_;
  }
  return ret;
}

int ObDDLTabletMergeDagParamV2::get_merge_ctx(ObDDLTabletContext::MergeCtx *&merge_ctx)
{
  int ret = OB_SUCCESS;
  merge_ctx = nullptr;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("merge dag param don't init", K(ret), KPC(this));
  } else if (OB_ISNULL(tablet_ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tablet ctx should not be null", K(ret));
  } else if (for_lob_) {
    if (!tablet_ctx_->lob_meta_tablet_id_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("lob tablet id not exist", K(ret));
    } else {
      merge_ctx = &tablet_ctx_->lob_merge_ctx_;
    }
  } else {
    merge_ctx = &tablet_ctx_->merge_ctx_;
  }
  return ret;
}
int ObDDLTabletMergeDagParamV2::init_slice_sstable_array(hash::ObHashSet<int64_t> &slice_idxes)
{
  int ret = OB_SUCCESS;
  const int64_t row_store_sstable_slot_count = 1;
  ObDDLTabletContext::MergeCtx *merge_ctx = nullptr;

  if (OB_FAIL(get_merge_ctx(merge_ctx))) {
  } else if (OB_ISNULL(merge_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("merge ctx should not be null", K(ret), KPC(this));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(merge_ctx->fifo_.init(ObMallocAllocator::get_instance(), OB_MALLOC_MIDDLE_BLOCK_SIZE,
                                           ObMemAttr("ddl_tblt_prm")))) {
  }
  
  for (hash::ObHashSet<int64_t>::const_iterator iter = slice_idxes.begin(); OB_SUCC(ret) && iter != slice_idxes.end(); iter++) {
    char* buf = nullptr;
    ObArray<ObTableHandleV2> *table_array = nullptr;
    if (OB_ISNULL(buf = static_cast<char*>(merge_ctx->arena_.alloc(sizeof(ObArray<ObTableHandleV2>))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("failed to allocate memory", K(ret));
    } else if (FALSE_IT(table_array = new (buf) ObArray<ObTableHandleV2>())) {
    } else if (OB_FAIL(merge_ctx->slice_sstables_.set_refactored(iter->first, table_array))) {
      LOG_WARN("failed to set refactorted", K(ret));
      /* destroy struct when set refactor failed */
      table_array->~ObArray<ObTableHandleV2>();
    } else if (OB_FAIL(table_array->prepare_allocate(row_store_sstable_slot_count))) {
    }
  }

  if (OB_FAIL(ret)) {
    /* release mem when failed */
    for (hash::ObHashMap<int64_t, ObArray<ObTableHandleV2>*>::const_iterator iter = merge_ctx->slice_sstables_.begin();
         iter != merge_ctx->slice_sstables_.end();
         iter++) {
      if (nullptr != iter->second) {
        iter->second->~ObArray<ObTableHandleV2>();
      }
    }
    merge_ctx->slice_sstables_.destroy();
    merge_ctx->arena_.reset();
  }
  return ret;
}
