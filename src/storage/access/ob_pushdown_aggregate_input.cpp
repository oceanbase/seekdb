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
#include "storage/access/ob_pushdown_aggregate_input.h"

#include "lib/ob_errno.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/blocksstable/index_block/ob_agg_row_struct.h"
#include "storage/blocksstable/index_block/ob_index_block_row_struct.h"
#include "storage/blocksstable/ob_datum_row.h"
#include "storage/blocksstable/ob_imicro_block_reader.h"
#include "storage/ob_storage_util.h"

namespace oceanbase
{
namespace storage
{

ObPushdownAggregateInput::ObPushdownAggregateInput(
    const ObTableIterParam &iter_param,
    blocksstable::ObIMicroBlockReader *reader,
    const int32_t *row_ids,
    const int64_t row_count,
    const bool is_padding_mode)
  : iter_param_(iter_param),
    kind_(INPUT_READER),
    reader_(reader),
    row_ids_(row_ids),
    row_(nullptr),
    index_info_(nullptr),
    selection_(),
    is_padding_mode_(is_padding_mode),
    value_allocator_("PushAggValue"),
    value_datums_(nullptr),
    min_datum_(),
    max_datum_()
{
  selection_.kind_ = nullptr == row_ids
      ? share::aggregate::AGG_SELECT_DENSE
      : share::aggregate::AGG_SELECT_ROW_IDS;
  selection_.count_ = row_count;
  selection_.row_ids_ = row_ids;
}

ObPushdownAggregateInput::ObPushdownAggregateInput(
    const ObTableIterParam &iter_param,
    const blocksstable::ObDatumRow &row,
    const bool is_padding_mode)
  : iter_param_(iter_param),
    kind_(INPUT_ROW),
    reader_(nullptr),
    row_ids_(nullptr),
    row_(&row),
    index_info_(nullptr),
    selection_(),
    is_padding_mode_(is_padding_mode),
    value_allocator_("PushAggValue"),
    value_datums_(nullptr),
    min_datum_(),
    max_datum_()
{
  selection_.kind_ = share::aggregate::AGG_SELECT_DENSE;
  selection_.count_ = 1;
}

ObPushdownAggregateInput::ObPushdownAggregateInput(
    const ObTableIterParam &iter_param,
    const blocksstable::ObMicroIndexInfo &index_info,
    const bool is_padding_mode)
  : iter_param_(iter_param),
    kind_(INPUT_INDEX),
    reader_(nullptr),
    row_ids_(nullptr),
    row_(nullptr),
    index_info_(&index_info),
    selection_(),
    is_padding_mode_(is_padding_mode),
    value_allocator_("PushAggValue"),
    value_datums_(nullptr),
    min_datum_(),
    max_datum_()
{
  selection_.kind_ = share::aggregate::AGG_SELECT_DENSE;
  selection_.count_ = index_info.get_row_count();
}

int ObPushdownAggregateInput::get_input_column(
    const share::aggregate::ObAggregateInputSlot slot,
    int32_t &col_offset,
    int32_t &col_index,
    const share::schema::ObColumnParam *&col_param) const
{
  int ret = OB_SUCCESS;
  col_offset = -1;
  col_index = -1;
  col_param = nullptr;
  const common::ObIArray<share::schema::ObColumnParam *> *col_params = iter_param_.get_col_params();
  const common::ObIArray<int32_t> *group_projector = iter_param_.group_by_cols_project_;
  const common::ObIArray<int32_t> *aggregate_projector = iter_param_.agg_cols_project_;
  const int64_t group_count = nullptr == group_projector ? 0 : group_projector->count();
  const int64_t aggregate_count = nullptr == aggregate_projector ? 0 : aggregate_projector->count();
  const int64_t input_count = group_count + aggregate_count;
  const common::ObIArray<int32_t> *projector = nullptr;
  int64_t projector_slot = -1;
  if (slot < 0 || slot >= input_count) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid aggregate input slot", K(ret), K(slot), K(group_count), K(aggregate_count));
  } else if (slot < group_count) {
    projector = group_projector;
    projector_slot = slot;
  } else {
    projector = aggregate_projector;
    projector_slot = slot - group_count;
  }
  if (OB_SUCC(ret) && OB_ISNULL(projector)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null canonical aggregate input projector", K(ret), K(slot), K(group_count),
             K(aggregate_count));
  } else if (OB_SUCC(ret) && FALSE_IT(col_offset = projector->at(projector_slot))) {
  } else if (OB_SUCC(ret) && OB_COUNT_AGG_PD_COLUMN_ID == col_offset) {
    // COUNT(*) is cardinality-only and intentionally has no value column.
  } else if (OB_SUCC(ret)
             && (OB_ISNULL(iter_param_.read_info_) || OB_ISNULL(col_params)
                 || col_offset < 0 || col_offset >= col_params->count()
                 || col_offset >= iter_param_.read_info_->get_columns_index().count())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid aggregate input projector", K(ret), K(slot), K(col_offset),
             KP(iter_param_.read_info_), KP(col_params));
  } else if (OB_SUCC(ret)) {
    col_index = iter_param_.read_info_->get_columns_index().at(col_offset);
    col_param = col_params->at(col_offset);
    if (OB_ISNULL(col_param)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("null aggregate column parameter", K(ret), K(slot), K(col_offset));
    }
  }
  return ret;
}

int ObPushdownAggregateInput::get_null_count(
    const int32_t col_offset,
    const int32_t col_index,
    const share::schema::ObColumnParam &col_param,
    int64_t &null_count) const
{
  int ret = OB_SUCCESS;
  null_count = 0;
  if (!col_param.is_nullable_for_write()) {
    // An exact schema fact avoids both decoder access and skip-index reads.
  } else if (INPUT_ROW == kind_) {
    if (OB_ISNULL(row_) || col_offset < 0 || col_offset >= row_->get_column_count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid row aggregate input", K(ret), K(col_offset), KP(row_));
    } else {
      const blocksstable::ObStorageDatum &datum = row_->storage_datums_[col_offset];
      if (!datum.is_nop()) {
        null_count = datum.is_null() ? 1 : 0;
      } else {
        const common::ObObj &default_value = col_param.get_orig_default_value();
        if (default_value.is_nop_value()) {
          ret = OB_NOT_SUPPORTED;
        } else {
          null_count = default_value.is_null() ? 1 : 0;
        }
      }
    }
  } else if (INPUT_READER == kind_) {
    if (OB_ISNULL(reader_) || (selection_.count_ > 0 && OB_ISNULL(row_ids_))) {
      ret = OB_NOT_SUPPORTED;
    } else {
      int64_t valid_row_count = 0;
      if (OB_FAIL(reader_->get_row_count(
              col_offset,
              row_ids_,
              selection_.count_,
              false,
              &col_param,
              valid_row_count))) {
        LOG_WARN("failed to count non-null aggregate input rows", K(ret), K(col_offset),
                 K(selection_.count_));
      } else if (OB_UNLIKELY(valid_row_count < 0 || valid_row_count > selection_.count_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid non-null aggregate input count", K(ret), K(valid_row_count),
                 K(selection_.count_));
      } else {
        null_count = selection_.count_ - valid_row_count;
      }
    }
  } else if (INPUT_INDEX == kind_) {
    if (OB_ISNULL(index_info_)
        || !index_info_->can_blockscan()
        || index_info_->is_left_border()
        || index_info_->is_right_border()
        || !index_info_->has_agg_data()) {
      ret = OB_NOT_SUPPORTED;
    } else {
      blocksstable::ObAggRowReader agg_reader;
      blocksstable::ObSkipIndexColMeta meta;
      common::ObDatum datum;
      bool is_prefix = false;
      meta.col_idx_ = col_index;
      meta.col_type_ = blocksstable::SK_IDX_NULL_COUNT;
      if (OB_FAIL(agg_reader.init(index_info_->agg_row_buf_, index_info_->agg_buf_size_))) {
        LOG_WARN("failed to initialize aggregate row reader", K(ret), K(col_index));
      } else if (OB_FAIL(agg_reader.read(meta, datum, is_prefix))) {
        LOG_WARN("failed to read aggregate null count", K(ret), K(col_index));
      } else if (OB_UNLIKELY(datum.is_null() || is_prefix)) {
        ret = OB_NOT_SUPPORTED;
      } else {
        null_count = datum.get_int();
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPushdownAggregateInput::can_read_values(
    const share::aggregate::ObAggregateInputSlot slot,
    bool &can_read) const
{
  int ret = OB_SUCCESS;
  int32_t col_offset = -1;
  int32_t col_index = -1;
  const share::schema::ObColumnParam *col_param = nullptr;
  can_read = false;
  if (OB_FAIL(get_input_column(slot, col_offset, col_index, col_param))) {
    LOG_WARN("failed to map aggregate value slot", K(ret), K(slot));
  } else {
    if (OB_COUNT_AGG_PD_COLUMN_ID == col_offset || nullptr == col_param) {
    } else if (INPUT_ROW == kind_ && nullptr != row_
               && col_offset >= 0 && col_offset < row_->get_column_count()) {
      // Capability describes the row representation, not the current value.
      // A physical NOP without a schema default is diagnosed uniformly by
      // normalize_value as a hard storage invariant violation.
      can_read = true;
    } else if (INPUT_READER == kind_ && nullptr != reader_
               && selection_.count_ >= 0
               && (share::aggregate::AGG_SELECT_DENSE == selection_.kind_
                   || 0 == selection_.count_
                   || nullptr != row_ids_)) {
      // Reader capability means that the projected physical column can be
      // decoded.  Most ordinary columns intentionally have no schema default;
      // rejecting those here would make every normal MIN/MAX segment miss.
      // An actual decoded NOP without a default is a corrupt/inconsistent
      // storage invariant and is reported as a hard error by normalize_value.
      can_read = true;
    }
  }
  return ret;
}

int ObPushdownAggregateInput::read_index_extreme(
    const int32_t col_offset,
    const int32_t col_index,
    const share::schema::ObColumnParam &col_param,
    const bool read_min,
    common::ObDatum &datum,
    bool &is_prefix)
{
  int ret = OB_SUCCESS;
  blocksstable::ObAggRowReader agg_reader;
  blocksstable::ObSkipIndexColMeta meta;
  const common::ObIArray<share::schema::ObColExtend> *column_extend = nullptr;
  datum.reset();
  datum.set_null();
  is_prefix = false;
  meta.col_idx_ = col_index;
  meta.col_type_ = read_min ? blocksstable::SK_IDX_MIN : blocksstable::SK_IDX_MAX;
  if (nullptr != iter_param_.read_info_) {
    column_extend = iter_param_.read_info_->get_columns_extend();
  }
  if (OB_ISNULL(index_info_)
      || !index_info_->can_blockscan()
      || index_info_->is_left_border()
      || index_info_->is_right_border()
      || !index_info_->has_agg_data()
      || OB_ISNULL(index_info_->row_header_)
      || !index_info_->row_header_->is_major_node()
      || OB_ISNULL(column_extend)
      || col_offset < 0
      || col_offset >= column_extend->count()
      || !column_extend->at(col_offset).skip_index_attr_.has_min_max()) {
    // MIN/MAX collected from minor data is deliberately loose: NOP values
    // are ignored and a non-prefix payload is still only a bound.  Only a
    // major-node summary from a schema-declared exact MIN/MAX index is an
    // exact SQL candidate.  A major node that carries only loose bounds, or
    // any minor summary, must fall back to decoded values.
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(agg_reader.init(index_info_->agg_row_buf_, index_info_->agg_buf_size_))) {
    LOG_WARN("failed to initialize aggregate row reader", K(ret), K(col_index), K(read_min));
  } else if (OB_FAIL(agg_reader.read(meta, datum, is_prefix))) {
    LOG_WARN("failed to read aggregate extreme", K(ret), K(col_index), K(read_min));
  } else if (datum.is_null() && selection_.count_ > 0) {
    // A missing serialized MIN/MAX can mean either "not collected" (NOP) or
    // "all selected values are NULL".  Prove the latter with an exact NULL
    // count; otherwise report a capability miss instead of a false SQL NULL.
    int64_t null_count = 0;
    const int null_ret = get_null_count(col_offset, col_index, col_param, null_count);
    if (OB_SUCCESS != null_ret || null_count != selection_.count_) {
      ret = OB_NOT_SUPPORTED;
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(normalize_value(col_param, datum))) {
    LOG_WARN("failed to normalize aggregate index extreme", K(ret), K(col_index), K(read_min));
  }
  return ret;
}

int ObPushdownAggregateInput::try_reduce(
    const share::aggregate::ObAggregateInputSlot slot,
    const uint32_t requested,
    share::aggregate::ObAggregateReduction &reduction)
{
  int ret = OB_SUCCESS;
  int32_t col_offset = -1;
  int32_t col_index = -1;
  const share::schema::ObColumnParam *col_param = nullptr;
  share::aggregate::ObAggregateReduction staged_reduction;
  const uint32_t supported = share::aggregate::AGG_REDUCE_ROW_COUNT
                           | share::aggregate::AGG_REDUCE_NULL_COUNT
                           | share::aggregate::AGG_REDUCE_MIN
                           | share::aggregate::AGG_REDUCE_MAX;
  if (OB_UNLIKELY(0 != (requested & ~supported))) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(get_input_column(slot, col_offset, col_index, col_param))) {
    LOG_WARN("failed to map aggregate input slot", K(ret), K(slot));
  } else if (INPUT_INDEX == kind_
             && (OB_ISNULL(index_info_)
                 || !index_info_->can_blockscan()
                 || index_info_->is_left_border()
                 || index_info_->is_right_border())) {
    ret = OB_NOT_SUPPORTED;
  } else if (0 != (requested & (share::aggregate::AGG_REDUCE_MIN
                                | share::aggregate::AGG_REDUCE_MAX))
             && INPUT_INDEX != kind_) {
    // Row and decoder segments expose exact values instead.  Keeping this a
    // capability miss makes the query program take one uniform fallback.
    ret = OB_NOT_SUPPORTED;
  } else if (0 != (requested & (share::aggregate::AGG_REDUCE_MIN
                                | share::aggregate::AGG_REDUCE_MAX))
             && (OB_COUNT_AGG_PD_COLUMN_ID == col_offset || OB_ISNULL(col_param))) {
    ret = OB_NOT_SUPPORTED;
  } else {
    if (0 != (requested & (share::aggregate::AGG_REDUCE_MIN
                           | share::aggregate::AGG_REDUCE_MAX))) {
      // A new reduction call is the protocol boundary at which previously
      // returned views may be invalidated.  Reuse once, then retain both MIN
      // and MAX if this request asks for them together.
      value_allocator_.reuse();
      value_datums_ = nullptr;
    }
    if (0 != (requested & share::aggregate::AGG_REDUCE_ROW_COUNT)) {
      staged_reduction.present_ |= share::aggregate::AGG_REDUCE_ROW_COUNT;
      staged_reduction.row_count_ = selection_.count_;
    }
    if (0 != (requested & share::aggregate::AGG_REDUCE_NULL_COUNT)) {
      int64_t null_count = 0;
      if (OB_COUNT_AGG_PD_COLUMN_ID == col_offset) {
        // Cardinality-only input has no NULL values.
      } else if (OB_ISNULL(col_param)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(get_null_count(col_offset, col_index, *col_param, null_count))) {
        if (OB_NOT_SUPPORTED != ret) {
          LOG_WARN("failed to get aggregate null count", K(ret), K(slot), K(col_offset));
        }
      }
      if (OB_SUCC(ret)) {
        staged_reduction.present_ |= share::aggregate::AGG_REDUCE_NULL_COUNT;
        staged_reduction.null_count_ = null_count;
      }
    }
    if (OB_SUCC(ret) && 0 != (requested & share::aggregate::AGG_REDUCE_MIN)) {
      bool is_prefix = false;
      if (OB_FAIL(read_index_extreme(
              col_offset, col_index, *col_param, true, min_datum_, is_prefix))) {
      } else {
        staged_reduction.present_ |= share::aggregate::AGG_REDUCE_MIN;
        staged_reduction.min_ = &min_datum_;
        staged_reduction.min_is_prefix_ = is_prefix;
      }
    }
    if (OB_SUCC(ret) && 0 != (requested & share::aggregate::AGG_REDUCE_MAX)) {
      bool is_prefix = false;
      if (OB_FAIL(read_index_extreme(
              col_offset, col_index, *col_param, false, max_datum_, is_prefix))) {
      } else {
        staged_reduction.present_ |= share::aggregate::AGG_REDUCE_MAX;
        staged_reduction.max_ = &max_datum_;
        staged_reduction.max_is_prefix_ = is_prefix;
      }
    }
  }
  if (OB_SUCC(ret)) {
    reduction = staged_reduction;
  }
  return ret;
}

int ObPushdownAggregateInput::prepare_value_buffer(const int64_t value_count)
{
  int ret = OB_SUCCESS;
  value_allocator_.reuse();
  value_datums_ = nullptr;
  if (OB_UNLIKELY(value_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (value_count > 0) {
    void *buf = value_allocator_.alloc(sizeof(common::ObDatum) * value_count);
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      value_datums_ = static_cast<common::ObDatum *>(buf);
      for (int64_t i = 0; i < value_count; ++i) {
        new (&value_datums_[i]) common::ObDatum();
      }
    }
  }
  return ret;
}

int ObPushdownAggregateInput::normalize_value(
    const share::schema::ObColumnParam &col_param,
    common::ObDatum &datum)
{
  int ret = OB_SUCCESS;
  blocksstable::ObStorageDatum storage_datum;
  if (datum.is_nop()) {
    const common::ObObj &default_value = col_param.get_orig_default_value();
    if (default_value.is_nop_value()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("decoded NOP aggregate value has no schema default", K(ret), K(col_param));
    } else if (OB_FAIL(storage_datum.from_obj_enhance(default_value))) {
      LOG_WARN("failed to materialize aggregate default value", K(ret));
    }
  } else {
    storage_datum.shallow_copy_from_datum(datum);
  }
  if (OB_SUCC(ret)
      && is_padding_mode_
      && col_param.get_meta_type().is_fixed_len_char_type()
      && OB_FAIL(storage::pad_column(
          col_param.get_meta_type(), col_param.get_accuracy(), value_allocator_, storage_datum))) {
    LOG_WARN("failed to pad aggregate value", K(ret), K(col_param));
  } else if (OB_SUCC(ret) && OB_FAIL(datum.deep_copy(storage_datum, value_allocator_))) {
    LOG_WARN("failed to own normalized aggregate value", K(ret));
  }
  return ret;
}

int ObPushdownAggregateInput::read_values(
    const share::aggregate::ObAggregateInputSlot slot,
    share::aggregate::ObAggregateValueBatchView &values)
{
  int ret = OB_SUCCESS;
  int32_t col_offset = -1;
  int32_t col_index = -1;
  const share::schema::ObColumnParam *col_param = nullptr;
  values = share::aggregate::ObAggregateValueBatchView();
  bool can_read = false;
  if (OB_FAIL(can_read_values(slot, can_read))) {
    LOG_WARN("failed to probe aggregate value slot", K(ret), K(slot));
  } else if (!can_read) {
    ret = OB_NOT_SUPPORTED;
  } else if (OB_FAIL(get_input_column(slot, col_offset, col_index, col_param))) {
    LOG_WARN("failed to map aggregate value slot", K(ret), K(slot));
  } else if (OB_FAIL(prepare_value_buffer(selection_.count_))) {
    LOG_WARN("failed to prepare aggregate value buffer", K(ret), K(selection_.count_));
  } else if (0 == selection_.count_) {
  } else if (INPUT_ROW == kind_) {
    if (OB_ISNULL(row_)
        || col_offset < 0
        || col_offset >= row_->get_column_count()
        || OB_FAIL(value_datums_[0].deep_copy(
            row_->storage_datums_[col_offset], value_allocator_))) {
      if (OB_SUCC(ret)) {
        ret = OB_ERR_UNEXPECTED;
      }
      LOG_WARN("failed to own row aggregate value", K(ret), K(col_offset), KP(row_));
    } else if (OB_FAIL(normalize_value(*col_param, value_datums_[0]))) {
      LOG_WARN("failed to normalize row aggregate value", K(ret), K(col_offset));
    }
  } else if (INPUT_READER == kind_) {
    const int64_t dense_begin = selection_.begin_;
    if (OB_ISNULL(reader_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(reader_->read_column_values(
        col_offset, dense_begin, row_ids_, selection_.count_, value_allocator_, value_datums_))) {
      LOG_WARN("failed to decode aggregate values", K(ret), K(col_offset), K(selection_.count_));
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < selection_.count_; ++i) {
      if (OB_FAIL(normalize_value(*col_param, value_datums_[i]))) {
        LOG_WARN("failed to normalize decoded aggregate value", K(ret), K(i), K(col_offset));
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  if (OB_SUCC(ret)) {
    values.datums_ = value_datums_;
    values.count_ = selection_.count_;
  }
  return ret;
}

int ObPushdownAggregateInput::try_dictionary(
    const share::aggregate::ObAggregateInputSlot slot,
    share::aggregate::ObAggregateDictionaryView &dictionary)
{
  UNUSED(slot);
  dictionary = share::aggregate::ObAggregateDictionaryView();
  return OB_NOT_SUPPORTED;
}

} // namespace storage
} // namespace oceanbase
