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

namespace oceanbase
{
namespace storage
{

namespace
{
int read_vector_lob_data(ObIAllocator &allocator,
                         ObILobReadService *lob_read_service,
                         ObString &data)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(lob_read_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("lob read service is not configured", K(ret));
  } else {
    const ObLobReadOptions options(*lob_read_service);
    ret = lob_helper::read_real_string_data(
        &allocator, ObLongTextType, CS_TYPE_BINARY, true, data, &options);
  }
  return ret;
}
}

class ObVectorIndexBaseSliceStore : public ObTabletSliceStore
{
public:
  ObVectorIndexBaseSliceStore()
    : is_inited_(false),
      lob_read_service_(nullptr),
      row_cnt_(0),
      vec_dim_(0),
      cur_row_pos_(0),
      tablet_id_(),
      table_id_(),
      vec_idx_param_(),
      current_row_()
  {}
  virtual ~ObVectorIndexBaseSliceStore() { reset(); }
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) override
  {
    return OB_NOT_IMPLEMENT;
  }
  virtual int append_batch(const blocksstable::ObBatchDatumRows &datum_rows) override
  {
    return OB_NOT_IMPLEMENT;
  }
  virtual int close() override;
  virtual void cancel() override {}
  virtual int64_t get_row_count() const { return row_cnt_; }

  virtual int init(ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
                   const ObString vec_idx_param,
                   const int64_t vec_dim,
                   const ObIArray<ObColumnSchemaItem> &col_array,
                   const int64_t context_id)
  {
    return OB_NOT_IMPLEMENT;
  }
  virtual int get_next_vector_data_row(
      const int64_t rowkey_cnt,
      const int64_t column_cnt,
      const int64_t snapshot_version,
      ObVectorIndexAlgorithmType index_type,
      blocksstable::ObDatumRow *&datum_row)
  {
    return OB_NOT_IMPLEMENT;
  }
  void set_lob_read_service(ObILobReadService &lob_read_service)
  {
    lob_read_service_ = &lob_read_service;
  }
  void reset();
  TO_STRING_KV(K_(is_inited), K_(row_cnt), K_(vec_dim), K_(tablet_id), K_(vec_idx_param));
public:
  bool is_inited_;
  ObILobReadService *lob_read_service_;
  int64_t row_cnt_;
  int64_t vec_dim_;
  int64_t cur_row_pos_;
  ObTabletID tablet_id_;
  ObTableID table_id_;
  ObString vec_idx_param_;
  blocksstable::ObDatumRow current_row_;
};

class ObVectorIndexSliceStore : public ObVectorIndexBaseSliceStore
{
public:
  static constexpr int64_t OB_VEC_IDX_SNAPSHOT_KEY_LENGTH =
      data_plane::OB_VECTOR_INDEX_SNAPSHOT_KEY_LENGTH;
  ObVectorIndexSliceStore()
    : ObVectorIndexBaseSliceStore(), vec_allocator_("VecIdxSS", OB_MALLOC_NORMAL_BLOCK_SIZE),
      tmp_allocator_("VecIdxSSAR", OB_MALLOC_NORMAL_BLOCK_SIZE),
      ctx_(), vector_vid_col_idx_(-1),
      vector_col_idx_(-1)
  {
    extra_column_idx_types_.set_attr(ObMemAttr("VecIdxExCol"));
  }
  virtual ~ObVectorIndexSliceStore() { reset(); }
  virtual int init(ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const ObIArray<ObColumnSchemaItem> &col_array,
      const int64_t context_id) override;
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) override;
  void reset();
  int serialize_vector_index(
      ObIAllocator *allocator,
      transaction::ObTxDesc *tx_desc,
      int64_t lob_inrow_threshold,
      ObVectorIndexAlgorithmType &type,
      const int64_t snapshot_version);
  virtual int get_next_vector_data_row(
      const int64_t rowkey_cnt,
      const int64_t column_cnt,
      const int64_t snapshot_version,
      ObVectorIndexAlgorithmType index_type,
      blocksstable::ObDatumRow *&datum_row) override;
  INHERIT_TO_STRING_KV("ObVectorIndexBaseSliceStore", ObVectorIndexBaseSliceStore,
      K(ctx_), K(vector_vid_col_idx_), K(vector_col_idx_), K(vector_key_col_idx_), K(vector_data_col_idx_), K(extra_column_idx_types_));
private:
  bool is_vec_idx_col_invalid(const int64_t column_cnt) const;
public:
  ObArenaAllocator vec_allocator_;
  ObArenaAllocator tmp_allocator_;
  ObVecIdxSnapshotDataWriteCtx ctx_;
  int32_t vector_vid_col_idx_;
  int32_t vector_col_idx_;
  int32_t vector_key_col_idx_;
  int32_t vector_data_col_idx_;
  ObSEArray<ObExtraInfoIdxType, 4> extra_column_idx_types_;
};

class ObIvfSliceStore : public ObVectorIndexBaseSliceStore
{
public:
  ObIvfSliceStore()
    : ObVectorIndexBaseSliceStore(),
      tmp_allocator_("IvfSSTmp", OB_MALLOC_NORMAL_BLOCK_SIZE),
      helper_guard_(),
      context_id_(-1),
      lob_inrow_threshold_(-1)
  {}

  virtual ~ObIvfSliceStore() {}
  virtual int init(
      ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const ObIArray<ObColumnSchemaItem> &col_array,
      const int64_t context_id) override;
  virtual void reset();
  virtual int build_clusters(ObInsertMonitor *insert_monitor) = 0;
  virtual int is_empty(bool &empty) = 0;
  OB_INLINE int64_t get_context_id() { return context_id_; }
  OB_INLINE void set_lob_inrow_threshold(int64_t lob_inrow_threshold) { lob_inrow_threshold_ = lob_inrow_threshold; }

protected:
  template<typename HelperType>
  int get_spec_ivf_helper(HelperType *&helper);

  ObArenaAllocator vec_allocator_;
  ObArenaAllocator tmp_allocator_;
  ObIvfBuildHelperGuard helper_guard_;
  int64_t context_id_;
  int64_t lob_inrow_threshold_;
};

template<typename HelperType>
int ObIvfSliceStore::get_spec_ivf_helper(HelperType *&helper)
{
  int ret = OB_SUCCESS;
  helper = nullptr;
  if (OB_NOT_NULL(helper_guard_.get_helper())) {
    helper = reinterpret_cast<HelperType *>(helper_guard_.get_helper());
  }

  if (OB_ISNULL(helper)) {
    ret = OB_ERR_NULL_VALUE;
    OB_LOG(WARN, "fail to get spec helper", K(ret), KP(helper_guard_.get_helper()));
  }
  return ret;
}

class ObIvfCenterSliceStore : public ObIvfSliceStore
{
public:
  ObIvfCenterSliceStore()
    : ObIvfSliceStore(),
      center_id_col_idx_(-1),
      center_vector_col_idx_(-1)
  {}

  virtual ~ObIvfCenterSliceStore() { reset(); }
  virtual int init(ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const ObIArray<ObColumnSchemaItem> &col_array,
      const int64_t context_id) override;
  virtual void reset() override;
  virtual int build_clusters(ObInsertMonitor *insert_monitor) override;
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) override;
  virtual int is_empty(bool &empty) override;
  virtual int get_next_vector_data_row(
      const int64_t rowkey_cnt,
      const int64_t column_cnt,
      const int64_t snapshot_version,
      ObVectorIndexAlgorithmType index_type,
      blocksstable::ObDatumRow *&datum_row) override;
public:
  ObArenaAllocator tmp_allocator_;
  int32_t center_id_col_idx_;
  int32_t center_vector_col_idx_;
};

class ObIvfSq8MetaSliceStore : public ObIvfSliceStore
{
public:
  ObIvfSq8MetaSliceStore()
    : ObIvfSliceStore(),
      meta_id_col_idx_(-1),
      meta_vector_col_idx_(-1)
  {}

  virtual ~ObIvfSq8MetaSliceStore() { reset(); }
  virtual int init(ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const ObIArray<ObColumnSchemaItem> &col_array,
      const int64_t context_id) override;
  virtual void reset() override;
  virtual int build_clusters(ObInsertMonitor *insert_monitor) override;
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) override;
  virtual int get_next_vector_data_row(
      const int64_t rowkey_cnt,
      const int64_t column_cnt,
      const int64_t snapshot_version,
      ObVectorIndexAlgorithmType index_type,
      blocksstable::ObDatumRow *&datum_row) override;
  virtual int is_empty(bool &empty) override;

private:
  int32_t meta_id_col_idx_;
  int32_t meta_vector_col_idx_;
};

class ObIvfPqSliceStore : public ObIvfSliceStore
{
public:
  ObIvfPqSliceStore()
    : ObIvfSliceStore(),
      pq_center_id_col_idx_(-1),
      pq_center_vector_col_idx_(-1)
  {}

  virtual ~ObIvfPqSliceStore() { reset(); }
  virtual int init(ObBaseTabletDirectLoadMgr *tablet_direct_load_mgr,
      const ObString vec_idx_param,
      const int64_t vec_dim,
      const ObIArray<ObColumnSchemaItem> &col_array,
      const int64_t context_id) override;
  virtual void reset() override;
  virtual int build_clusters(ObInsertMonitor *insert_monitor) override;
  virtual int append_row(const blocksstable::ObDatumRow &datum_row) override;
  virtual int get_next_vector_data_row(
      const int64_t rowkey_cnt,
      const int64_t column_cnt,
      const int64_t snapshot_version,
      ObVectorIndexAlgorithmType index_type,
      blocksstable::ObDatumRow *&datum_row) override;
  virtual int is_empty(bool &empty) override;

private:
  int32_t pq_center_id_col_idx_;
  int32_t pq_center_vector_col_idx_;
};

} // namespace storage
} // namespace oceanbase

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
