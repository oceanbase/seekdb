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

#include "ob_table_access_param.h"
#include "storage/ob_relative_table.h"
#include "storage/tablet/ob_tablet.h"
#include "storage/ob_table_dml_param.h"

namespace oceanbase
{
using namespace common;
using namespace blocksstable;
namespace storage
{
ObTableIterParam::ObTableIterParam()
    : table_id_(0),
      tablet_id_(),
      read_info_(nullptr),
      rowkey_read_info_(nullptr),
      tablet_handle_(nullptr),
      out_cols_project_(NULL),
      agg_cols_project_(NULL),
      group_by_cols_project_(NULL),
      pushdown_filter_(nullptr),
      op_(nullptr),
      sstable_index_filter_(nullptr),
      output_exprs_(nullptr),
      aggregate_exprs_(nullptr),
      output_sel_mask_(nullptr),
      is_multi_version_minor_merge_(false),
      need_scn_(false),
      need_trans_info_(false),
      is_same_schema_column_(false),
      vectorized_enabled_(false),
      has_virtual_columns_(false),
      has_lob_column_out_(false),
      is_for_foreign_check_(false),
      limit_prefetch_(false),
      is_mds_query_(false),
      is_non_unique_local_index_(false),
      is_advance_scan_(false),
      pd_storage_flag_(),
      table_scan_opt_(),
      need_update_tablet_param_(nullptr)
{}

ObTableIterParam::~ObTableIterParam()
{
  if (nullptr != pushdown_filter_) {
    pushdown_filter_->clear();
    pushdown_filter_ = nullptr;
  }
  ObSSTableIndexFilterFactory::destroy_sstable_index_filter(sstable_index_filter_);
}

void ObTableIterParam::reuse()
{
  is_advance_scan_ = false;
}

void ObTableIterParam::reset()
{
  table_id_ = 0;
  tablet_id_.reset();
  read_info_ = nullptr;
  rowkey_read_info_ = nullptr;
  tablet_handle_ = nullptr;
  out_cols_project_ = NULL;
  agg_cols_project_ = NULL;
  group_by_cols_project_ = NULL;
  is_multi_version_minor_merge_ = false;
  need_scn_ = false;
  need_trans_info_ = false;
  is_same_schema_column_ = false;
  pd_storage_flag_ = 0;
  if (nullptr != pushdown_filter_) {
    pushdown_filter_->clear();
    pushdown_filter_ = nullptr;
  }
  op_ = nullptr;
  output_exprs_ = nullptr;
  aggregate_exprs_ = nullptr;
  output_sel_mask_ = nullptr;
  vectorized_enabled_ = false;
  has_virtual_columns_ = false;
  has_lob_column_out_ = false;
  is_for_foreign_check_ = false;
  limit_prefetch_ = false;
  is_mds_query_ = false;
  is_non_unique_local_index_ = false;
  is_advance_scan_ = false;
  table_scan_opt_.reset();
  ObSSTableIndexFilterFactory::destroy_sstable_index_filter(sstable_index_filter_);
  need_update_tablet_param_ = nullptr;
}

int ObTableIterParam::refresh_lob_column_out_status()
{
  int ret = OB_SUCCESS;
  has_lob_column_out_ = false;
  if (OB_ISNULL(read_info_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null read info", K(ret));
  } else {
    const ObColDescIArray &out_cols = read_info_->get_columns_desc();
    for (int64_t i = 0; !has_lob_column_out_ && i < out_cols.count(); i++) {
      has_lob_column_out_ = (is_lob_storage(out_cols.at(i).col_type_.get_type()));
    }
  }
  return ret;
}

bool ObTableIterParam::enable_fuse_row_cache(const ObQueryFlag &query_flag) const
{
  bool bret = query_flag.is_use_fuse_row_cache() && !query_flag.is_read_latest() &&
              nullptr != rowkey_read_info_ && !need_scn_ &&
              is_same_schema_column_ && !has_virtual_columns_ && !has_lob_column_out_;
  return bret;
}

bool ObTableIterParam::need_trans_info() const
{
  bool bret = false;
  if (need_trans_info_ ||
      (OB_NOT_NULL(op_) && OB_NOT_NULL(op_->expr_spec_.trans_info_expr_))) {
    bret = true;
  }
  return bret;
}

int ObTableIterParam::build_index_filter_for_row_store(common::ObIAllocator *allocator)
{
  int ret = OB_SUCCESS;
  if (enable_pd_blockscan() && enable_pd_filter() && enable_skip_index() && nullptr != pushdown_filter_) {
    if (OB_FAIL(ObSSTableIndexFilterFactory::build_sstable_index_filter(
                  get_read_info(),
                  *pushdown_filter_,
                  allocator,
                  sstable_index_filter_))) {
      STORAGE_LOG(WARN, "Failed to build sstable index filter", K(ret), KPC(this));
    }
  }
  return ret;
}

DEF_TO_STRING(ObTableIterParam)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(table_id),
       K_(tablet_id),
       KPC_(read_info),
       KPC_(rowkey_read_info),
       KPC_(out_cols_project),
       KPC_(agg_cols_project),
       KPC_(group_by_cols_project),
       KPC_(pushdown_filter),
       KP_(op),
       KP_(sstable_index_filter),
       KPC_(output_exprs),
       KPC_(aggregate_exprs),
       KPC_(output_sel_mask),
       K_(is_multi_version_minor_merge),
       K_(need_scn),
       K_(is_same_schema_column),
       K_(pd_storage_flag),
       K_(vectorized_enabled),
       K_(has_virtual_columns),
       K_(has_lob_column_out),
       K_(is_for_foreign_check),
       K_(limit_prefetch),
       K_(is_mds_query),
       K_(is_non_unique_local_index),
       K_(is_advance_scan),
       K_(table_scan_opt),
       KP_(need_update_tablet_param));
  J_OBJ_END();
  return pos;
}

ObTableAccessParam::ObTableAccessParam()
    : iter_param_(),
      padding_cols_(NULL),
      projector_size_(0),
      output_exprs_(NULL),
      aggregate_exprs_(NULL),
      op_filters_(NULL),
      row2exprs_projector_(NULL),
      output_sel_mask_(NULL),
      is_inited_(false)
{
}

ObTableAccessParam::~ObTableAccessParam()
{
}

void ObTableAccessParam::reset()
{
  iter_param_.reset();
  padding_cols_ = NULL;
  projector_size_ = 0;
  output_exprs_ = NULL;
  op_filters_ = NULL;
  row2exprs_projector_ = NULL;
  output_sel_mask_ = NULL;
  is_inited_ = false;
}

void ObTableAccessParam::reuse()
{
  iter_param_.reuse();
}

int ObTableAccessParam::init(
    const ObTableScanParam &scan_param,
    const ObTabletHandle *tablet_handle,
    const ObITableReadInfo *rowkey_read_info)
{
  int ret = OB_SUCCESS;

  if(IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableAccessParam init twice", K(ret), K(*this));
  } else if (OB_ISNULL(scan_param.table_param_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(scan_param.table_param_));
  } else if (OB_UNLIKELY(nullptr == rowkey_read_info && nullptr == tablet_handle)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), KP(rowkey_read_info), KP(tablet_handle));
  } else if (OB_NOT_NULL(tablet_handle) && OB_FAIL(check_valid_before_query_init(*scan_param.table_param_, *tablet_handle))) {
    LOG_WARN("failed to check cs replica compat schema", K(ret), KPC(tablet_handle));
  } else {
    const share::schema::ObTableParam &table_param = *scan_param.table_param_;
    iter_param_.table_id_ = table_param.get_table_id();
    iter_param_.tablet_id_ = scan_param.tablet_id_;
    iter_param_.read_info_ = &table_param.get_read_info();

    if (nullptr == tablet_handle) {
      iter_param_.rowkey_read_info_ = rowkey_read_info;
      iter_param_.set_tablet_handle(nullptr);
    } else {
      iter_param_.rowkey_read_info_ = &tablet_handle->get_obj()->get_rowkey_read_info();
      iter_param_.set_tablet_handle(tablet_handle);
    }

    iter_param_.out_cols_project_ = &table_param.get_output_projector();
    iter_param_.agg_cols_project_ = &table_param.get_aggregate_projector();
    iter_param_.group_by_cols_project_ = &table_param.get_group_by_projector();
    iter_param_.need_scn_ = scan_param.need_scn_ || OB_INVALID_INDEX != table_param.get_read_info().get_trans_col_index();
    iter_param_.is_for_foreign_check_ = scan_param.is_for_foreign_check_;
    padding_cols_ = &table_param.get_pad_col_projector();
    projector_size_ = scan_param.projector_size_;

    output_exprs_ = scan_param.output_exprs_;
    aggregate_exprs_ = scan_param.aggregate_exprs_;
    iter_param_.op_ = scan_param.op_;
    op_filters_ = scan_param.op_filters_;
    row2exprs_projector_ = scan_param.row2exprs_projector_;
    output_sel_mask_ = &table_param.get_output_sel_mask();

    iter_param_.output_exprs_ = scan_param.output_exprs_;
    iter_param_.aggregate_exprs_ = scan_param.aggregate_exprs_;
    iter_param_.output_sel_mask_ = &table_param.get_output_sel_mask();

    iter_param_.is_same_schema_column_ =
        iter_param_.read_info_->get_schema_column_count() == iter_param_.rowkey_read_info_->get_schema_column_count();

    iter_param_.pd_storage_flag_ = scan_param.pd_storage_flag_;
    if (scan_param.table_scan_opt_.is_io_valid()) {
      iter_param_.table_scan_opt_.io_read_batch_size_ = scan_param.table_scan_opt_.io_read_batch_size_;
      iter_param_.table_scan_opt_.io_read_gap_size_ = scan_param.table_scan_opt_.io_read_gap_size_;
    } else {
      iter_param_.table_scan_opt_.io_read_batch_size_ = 0;
      iter_param_.table_scan_opt_.io_read_gap_size_ = 0;
    }
    if (scan_param.table_scan_opt_.is_rowsets_valid()) {
      iter_param_.table_scan_opt_.storage_rowsets_size_ = scan_param.table_scan_opt_.storage_rowsets_size_;
    } else {
      iter_param_.table_scan_opt_.storage_rowsets_size_ = 1;
    }
    iter_param_.pushdown_filter_ = scan_param.pd_storage_filters_;
     // disable blockscan if scan order is KeepOrder
    if (OB_UNLIKELY(ObQueryFlag::KeepOrder == scan_param.scan_flag_.scan_order_ ||
                    !scan_param.scan_flag_.is_use_block_cache())) {
      iter_param_.disable_blockscan();
    }
    iter_param_.has_virtual_columns_ = table_param.has_virtual_column();
    // vectorize requires blockscan is enabled(_pushdown_storage_level > 0)
    iter_param_.vectorized_enabled_ = nullptr != get_op() && get_op()->is_vectorized();
    iter_param_.limit_prefetch_ = (nullptr == op_filters_ || op_filters_->empty());
    iter_param_.is_mds_query_ = scan_param.is_mds_query_;

    if (scan_param.need_switch_param_) {
      iter_param_.set_use_stmt_iter_pool();
    }

    if (OB_FAIL(iter_param_.refresh_lob_column_out_status())) {
      STORAGE_LOG(WARN, "Failed to refresh lob column out status", K(ret), K(iter_param_));
    } else {
      iter_param_.need_update_tablet_param_ = &scan_param.need_update_tablet_param_;
      is_inited_ = true;
    }
  }

  return ret;
}

int ObTableAccessParam::check_valid_before_query_init(
    const ObTableParam &table_param,
    const ObTabletHandle &tablet_handle)
{
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  UNUSED(table_param);
  if (OB_UNLIKELY(!tablet_handle.is_valid() || OB_ISNULL(tablet = tablet_handle.get_obj()))) {
    ret = OB_INVALID_ARGUMENT;
    STORAGE_LOG(WARN, "invalid table handle", K(ret), K(tablet_handle), KPC(tablet));
  }
  return ret;
}

int ObTableAccessParam::init_merge_param(
    const uint64_t table_id,
    const common::ObTabletID &tablet_id,
    const ObITableReadInfo &read_info,
    const bool is_multi_version_minor_merge)
{
  int ret = OB_SUCCESS;

  if(IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObTableAccessParam init twice", K(ret), KPC(this));
  } else {
    iter_param_.table_id_ = table_id;
    iter_param_.tablet_id_ = tablet_id;
    iter_param_.is_multi_version_minor_merge_ = is_multi_version_minor_merge;
    iter_param_.read_info_ = &read_info;
    iter_param_.rowkey_read_info_ = &read_info;
    // merge_query will not goto ddl_merge_query, no need to pass tablet
    is_inited_ = true;
  }
  return ret;
}

int ObTableAccessParam::init_dml_access_param(
    const ObRelativeTable &table,
    const ObITableReadInfo &rowkey_read_info,
    const share::schema::ObTableSchemaParam &schema_param,
    const ObIArray<int32_t> *out_cols_project)
{
  int ret = OB_SUCCESS;
  const ObTablet *tablet = nullptr;
  if(IS_INIT) {
    ret = OB_INIT_TWICE;
    STORAGE_LOG(WARN, "ObTableAccessParam init twice", K(ret), K(*this));
  } else {
    iter_param_.table_id_ = table.get_table_id();
    iter_param_.tablet_id_ = table.get_tablet_id();
    if (nullptr != table.tablet_iter_.get_tablet()) {
    }
    iter_param_.read_info_ = &schema_param.get_read_info();
    iter_param_.rowkey_read_info_ = &rowkey_read_info;
    iter_param_.set_tablet_handle(table.tablet_iter_.get_tablet_handle_ptr());
    iter_param_.is_same_schema_column_ =
        iter_param_.read_info_->get_schema_column_count() == iter_param_.rowkey_read_info_->get_schema_column_count();
    iter_param_.out_cols_project_ = out_cols_project;
    iter_param_.need_scn_ = OB_INVALID_INDEX != schema_param.get_read_info().get_trans_col_index();
    for (int64_t i = 0; i < schema_param.get_columns().count(); i++) {
      if (schema_param.get_columns().at(i)->is_virtual_gen_col()) {
        iter_param_.has_virtual_columns_ = true;
        break;
      }
    }
    if (OB_FAIL(iter_param_.refresh_lob_column_out_status())) {
      STORAGE_LOG(WARN, "Failed to refresh lob column out status", K(ret), K(iter_param_));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

DEF_TO_STRING(ObTableAccessParam)
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(iter_param),
      KPC_(padding_cols),
      K_(projector_size),
      KPC_(output_exprs),
      KP_(op_filters),
      KP_(row2exprs_projector),
      KPC_(output_sel_mask),
      K_(is_inited));
  J_OBJ_END();
  return pos;
}

int set_row_scn(
    const bool use_fuse_row_cache,
    const ObTableIterParam &iter_param,
    const ObDatumRow *store_row)
{
  int ret = OB_SUCCESS;
  const ObColDescIArray *out_cols = nullptr;
  const ObITableReadInfo *read_info = iter_param.get_read_info(use_fuse_row_cache);
  if (OB_UNLIKELY(nullptr == read_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null read info", K(ret));
  } else {
    int64_t trans_idx = read_info->get_trans_col_index();
    if (OB_UNLIKELY(trans_idx < 0 || trans_idx >= store_row->count_ ||
                    store_row->storage_datums_[trans_idx].is_nop())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected trans_idx", K(ret), KPC(store_row), KPC(read_info));
    } else {
      int64_t version = -store_row->storage_datums_[trans_idx].get_int();
      if (version == share::SCN::max_scn().get_val_for_tx()) {
        // TODO(handora.qc): remove it as if we confirmed no problem according to row_scn
        LOG_INFO("use max row scn", KPC(store_row));
      }

      if (version > 0) {
        store_row->storage_datums_[trans_idx].reuse();
        store_row->storage_datums_[trans_idx].set_int(version);
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("scn should be greater than 0", K(ret), K(version), KPC(store_row), KPC(read_info));
      }
    }
  }
  return ret;
}

} // namespace storage
} // namespace oceanbase
