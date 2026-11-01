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
#include "ob_pushdown_aggregate.h"
#include "storage/lob/ob_lob_manager.h"
#include "common/sql_mode/ob_sql_mode_utils.h"
#include "data_plane/lob/ob_lob_value.h"
#include "query/engine/aggregate/ob_aggregate_util.h"
#include "query/engine/expr/ob_decimal_int_scale.h"
#include "query/engine/expr/ob_array_expr_utils.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
namespace
{
constexpr int64_t NATIVE_LEGACY_SCALAR_BYTES = 64;

int publish_storage_result(const sql::ObExpr &expr, sql::ObEvalCtx &ctx, const ObDatum &source,
                           int64_t row = -1)
{
  int ret = OB_SUCCESS;
  Datum value;
  const DatumCopyPlan plan = expr.get_datum_copy_plan();
  const int64_t index = row < 0 ? ctx.get_batch_idx() : row;
  if (OB_FAIL(plan.import_legacy(source, value))) {
  } else {
    int64_t bytes = 0;
    Datum &destination = expr.locate_expr_datum(ctx, index);
    if (OB_FAIL(plan.carrier_span_size(value, bytes))) {
    } else if (bytes == 0) { destination = value; }
    else {
      char *buffer = expr.get_reference_res_mem(ctx, bytes, index);
      int64_t position = 0;
      if (buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { ret = plan.copy_carrier_span_borrowing_resources(value, destination, buffer, bytes, position); }
    }
  }
  return ret;
}
}

ObAggCell::ObAggCell(const ObAggCellBasicInfo &basic_info, common::ObIAllocator &allocator)
    : ObAggCellBase(allocator),
      basic_info_(basic_info),
      def_datum_(),
      is_lob_col_(false),
      aggregated_(false),
      agg_datum_buf_(nullptr),
      col_datums_(nullptr),
      legacy_input_owner_(nullptr), native_eval_ctx_(nullptr),
      native_projection_offset_(0),
      group_by_result_datum_buf_(nullptr),
      group_by_result_cnt_(0),
      padding_allocator_("ObStorageAgg", OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  if (basic_info_.col_param_ != nullptr) {
    is_lob_col_ = basic_info_.col_param_->get_meta_type().is_lob_storage();
  }
}

void ObAggCell::reset()
{
  ObAggCellBase::reset();
  basic_info_.reset();
  is_lob_col_ = false;
  aggregated_ = false;
  if (nullptr != agg_datum_buf_) {
    agg_datum_buf_->reset();
    allocator_.free(agg_datum_buf_);
    agg_datum_buf_ = nullptr;
  }
  for (ObAggDatumBuf **owner : {&legacy_input_owner_}) {
    if (*owner != nullptr) { (*owner)->~ObAggDatumBuf(); allocator_.free(*owner); *owner = nullptr; }
  }
  native_eval_ctx_ = nullptr;
  col_datums_ = nullptr;
  free_group_by_buf(allocator_, group_by_result_datum_buf_);
  group_by_result_cnt_ = 0;
  padding_allocator_.reset();
}

void ObAggCell::reuse()
{
  ObAggCellBase::reuse();
  aggregated_ = false;
  native_projection_offset_ = 0;
  if (group_by_result_datum_buf_ != nullptr) { group_by_result_datum_buf_->reuse(); }
  clear_group_by_info();
  group_by_result_cnt_ = 0;
  if (nullptr != col_datums_) {
    for (int64_t i = 0; i < basic_info_.batch_size_; ++i) {
      col_datums_[i].set_null();
    }
  }
  padding_allocator_.reuse();
}

void ObAggCell::clear_group_by_info()
{
  if (nullptr != group_by_result_datum_buf_) {
    if (PD_COUNT != agg_type_) {
      group_by_result_datum_buf_->fill_datum_items(FillDatumType::NULL_DATUM);
    } else {
      group_by_result_datum_buf_->fill_datum_items(FillDatumType::ZERO_DATUM);
    }
  }
}

int ObAggCell::init_legacy_frame_buffers()
{
  int ret = ObAggDatumBuf::new_agg_datum_buf(basic_info_.batch_size_, false,
      allocator_, legacy_input_owner_, NATIVE_LEGACY_SCALAR_BYTES);
  if (OB_SUCC(ret)) { col_datums_ = legacy_input_owner_->get_datums(); }
  return ret;
}

int ObAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  native_eval_ctx_ = eval_ctx;
  if (basic_info_.agg_expr_ == nullptr || basic_info_.agg_expr_->sql_type_ == nullptr) { ret = OB_INVALID_ARGUMENT; }
  else {
    result_datum_.configure(&basic_info_.agg_expr_->get_sql_type(),
        PD_COUNT == agg_type_ ? ObAggNativeDomain::Signed : ObAggNativeDomain::Semantic);
    if (is_group_by) {
      if (OB_FAIL(init_legacy_frame_buffers())) {
      } else { ret = init_native_result_buffer(); }
    }
  }
  if (OB_SUCC(ret)) { is_inited_ = true; }
  return ret;
}

int ObAggCell::init_native_result_buffer()
{
  int ret = OB_SUCCESS;
  void *memory = allocator_.alloc(sizeof(ObNativeAggDatumBuf));
  if (memory == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  else {
    auto *candidate = new(memory) ObNativeAggDatumBuf(allocator_);
    ret = candidate->init(basic_info_.batch_size_, &basic_info_.agg_expr_->get_sql_type(),
        PD_COUNT == agg_type_ ? ObAggNativeDomain::Signed : ObAggNativeDomain::Semantic);
    if (OB_FAIL(ret)) { candidate->~ObNativeAggDatumBuf(); allocator_.free(candidate); }
    else { group_by_result_datum_buf_ = candidate; }
  }
  return ret;
}

int ObAggCell::import_native_inputs(sql::ObEvalCtx &ctx, int64_t begin, int64_t end)
{
  int ret = OB_SUCCESS;
  const sql::ObExpr &expr = *(PD_FIRST_ROW == agg_type_ ? basic_info_.agg_expr_
      : basic_info_.agg_expr_->args_[0]);
  if (begin < 0 || end > basic_info_.batch_size_) { ret = OB_INVALID_ARGUMENT; }
  if (begin == 0) { legacy_input_owner_->reuse(); }
  for (int64_t row = begin; OB_SUCC(ret) && row < end; ++row) {
    const Datum &source = expr.locate_expr_datum(ctx, row);
    // The owner already reserves enough space for storage scalar/Number/Decimal
    // output. References can borrow the existing frame for this synchronous use.
    char *buffer = legacy_input_owner_->get_view_scratch(row);
    ret = expr.get_datum_copy_plan().legacy_view(source, col_datums_[row], buffer,
                                                NATIVE_LEGACY_SCALAR_BYTES);
  }
  return ret;
}

int ObAggCell::publish_native_rows(sql::ObEvalCtx &ctx, int64_t count)
{
  int ret = OB_SUCCESS;
  if (count < 0 || group_by_result_datum_buf_ == nullptr
      || native_projection_offset_ > group_by_result_datum_buf_->get_capacity() - count) { return OB_INVALID_ARGUMENT; }
  for (int64_t row = 0; OB_SUCC(ret) && row < count; ++row) {
    ObDatum view;
    alignas(8) char scratch[64];
    if (OB_FAIL(group_by_result_datum_buf_->at(native_projection_offset_ + row).export_legacy(view, scratch, sizeof(scratch)))) {
    } else { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, view, row); }
  }
  return ret;
}

int ObAggCell::eval_micro_block(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t col_offset,
    blocksstable::ObIMicroBlockReader *reader,
    const int32_t *row_ids,
    const int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_info_.col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAggDatumBuf::new_agg_datum_buf(basic_info_.batch_size_, true, allocator_, agg_datum_buf_))) {
  } else if (OB_FAIL(reader->get_aggregate_result(iter_param, context, col_offset, *basic_info_.col_param_, row_ids, row_count, *agg_datum_buf_, *this))) {
  }
  return ret;
}

int ObAggCell::eval_index_info(const blocksstable::ObMicroIndexInfo &index_info)
{
  int ret = OB_SUCCESS;
  if (!index_info.can_blockscan() || index_info.is_left_border() || index_info.is_right_border()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(skip_index_datum_.is_null() || skip_index_datum_is_prefix_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(eval(skip_index_datum_))) {
  }
  return ret;
}

int ObAggCell::copy_output_rows(const int32_t start_offset, const int32_t end_offset)
{
  int ret = OB_SUCCESS;
  if (native_eval_ctx_ != nullptr) { ret = import_native_inputs(*native_eval_ctx_, start_offset, end_offset); }
  if (OB_FAIL(ret)) {
  } else if (end_offset <= start_offset || end_offset > group_by_result_datum_buf_->get_basic_count()) { ret = OB_INVALID_ARGUMENT; }
  else {
    native_projection_offset_ = 0;
    for (int64_t i = start_offset; OB_SUCC(ret) && i < end_offset; ++i) {
      ret = group_by_result_datum_buf_->at(i).capture_legacy(col_datums_[i]);
    }
  }
  return ret;
}

int ObAggCell::copy_single_output_row(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (basic_info_.agg_expr_->args_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
  else { ret = basic_info_.agg_expr_->deep_copy_datum(ctx,
      basic_info_.agg_expr_->args_[0]->locate_expr_datum(ctx)); }
  return ret;
}

int ObAggCell::collect_result(sql::ObEvalCtx &ctx)
{
  ObDatum view;
  alignas(8) char scratch[64];
  int ret = result_datum_.export_legacy(view, scratch, sizeof(scratch));
  if (OB_SUCC(ret)) { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, view); }
  if (OB_SUCC(ret)) { basic_info_.agg_expr_->get_eval_info(ctx).evaluated_ = true; }
  return ret;
}

int ObAggCell::collect_batch_result_in_group_by(const int64_t distinct_cnt)
{
  UNUSED(distinct_cnt);
  return OB_SUCCESS;
}

int ObAggCell::reserve_group_by_buf(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(group_by_result_datum_buf_->reserve(size))) {
  } else {
    clear_group_by_info();
  }
  return ret;
}

int ObAggCell::output_extra_group_by_result(const int64_t start, const int64_t count)
{
  if (start < group_by_result_datum_buf_->get_basic_count() || count < 0
      || start > group_by_result_datum_buf_->get_capacity() - count
      || !group_by_result_datum_buf_->is_use_extra_data()) { return OB_INVALID_ARGUMENT; }
  native_projection_offset_ = start;
  return OB_SUCCESS;
}

int ObAggCell::pad_column_in_group_by(const int64_t row_cap, common::ObIAllocator &allocator)
{
  return OB_SUCCESS;
}

int ObAggCell::prepare_def_datum()
{
  int ret = OB_SUCCESS;
  if (def_datum_.is_nop()) {
    def_datum_.reuse();
    common::DatumValue def_cell;
    alignas(8) char default_scratch[64];
    if (OB_FAIL(basic_info_.col_param_->export_orig_default_value(def_cell, default_scratch, sizeof(default_scratch)))) {
    } else if (!def_cell.is_nop_value()) {
      if (OB_FAIL(def_datum_.from_value(def_cell))) {
      } else if (OB_FAIL(pad_column_if_need(def_datum_, allocator_, false))) {
      } else if (def_cell.is_lob_storage() && !def_cell.is_null()) {
        // lob def value must have no lob header when not null, should add lob header for default value
        ObString data = def_datum_.get_string();
        ObString out;
        if (OB_FAIL(data_plane::fill_lob_header(allocator_, data, out))) {
        } else {
          def_datum_.set_string(out);
        }
      }
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObAggCell::fill_default_if_need(blocksstable::ObStorageDatum &datum)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(basic_info_.col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (datum.is_nop()) {
    if (OB_FAIL(prepare_def_datum())) {
    } else {
      datum.reuse();
      const SqlValueDatumMapType map_type = ObDatum::get_sql_value_datum_map_type(basic_info_.col_param_->get_meta_type().get_type());
      if (OB_FAIL(datum.from_storage_datum(def_datum_, map_type))) {
      }
    }
  }
  return ret;
}

int ObAggCell::pad_column_if_need(blocksstable::ObStorageDatum &datum, common::ObIAllocator &padding_allocator, bool alloc_need_reuse)
{
  int ret = OB_SUCCESS;
  if (alloc_need_reuse){
    padding_allocator.reuse();
  }
  if (OB_ISNULL(basic_info_.col_param_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!basic_info_.need_padding()) {
  } else if (OB_FAIL(pad_column(basic_info_.col_param_->get_meta_type(), basic_info_.col_param_->get_accuracy(), padding_allocator, datum))) {
  }
  return ret;
}

int ObAggCell::deep_copy_datum(const blocksstable::ObStorageDatum &src, common::ObIAllocator &tmp_alloc)
{
  UNUSED(tmp_alloc);
  return src.is_null() || src.is_nop() ? OB_ERR_UNEXPECTED : result_datum_.capture_legacy(src);
}

int ObAggCell::read_agg_datum(
    const blocksstable::ObMicroIndexInfo &index_info)
{
  int ret = OB_SUCCESS;
  if (nullptr == agg_row_reader_) {
    void *buf = nullptr;
    if (OB_ISNULL(buf = allocator_.alloc(sizeof(blocksstable::ObAggRowReader)))) {
      ret = common::OB_ALLOCATE_MEMORY_FAILED;
    } else {
      agg_row_reader_ = new (buf) blocksstable::ObAggRowReader();
    }
  }
  if (OB_SUCC(ret)) {
    skip_index_datum_.reuse();
    skip_index_datum_.set_null();
    skip_index_datum_is_prefix_ = false;
    blocksstable::ObSkipIndexColMeta meta;
    meta.col_idx_ = static_cast<uint32_t>(basic_info_.col_index_);
    switch (agg_type_) {
      case ObPDAggType::PD_SUM_OP_SIZE:
      case ObPDAggType::PD_COUNT: {
        meta.col_type_ = blocksstable::SK_IDX_NULL_COUNT;
        break;
      }
      case ObPDAggType::PD_MIN: {
        meta.col_type_ = blocksstable::SK_IDX_MIN;
        break;
      }
      case ObPDAggType::PD_MAX: {
        meta.col_type_ = blocksstable::SK_IDX_MAX;
        break;
      }
      case ObPDAggType::PD_SUM: {
        meta.col_type_ = blocksstable::SK_IDX_SUM;
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Unexpected agg type", K(agg_type_));
      }
    }
    agg_row_reader_->reset();
    if (OB_FAIL(agg_row_reader_->init(index_info.agg_row_buf_, index_info.agg_buf_size_))) {
    } else if (OB_FAIL(agg_row_reader_->read(meta, skip_index_datum_, skip_index_datum_is_prefix_))) {
    }
  }
  return ret;
}

ObCountAggCell::ObCountAggCell(
    const ObAggCellBasicInfo &basic_info,
    common::ObIAllocator &allocator,
    bool exclude_null)
    : ObAggCell(basic_info, allocator),
      exclude_null_(exclude_null),
      row_count_(0)
{
  agg_type_ = ObPDAggType::PD_COUNT;
}

void ObCountAggCell::reset()
{
  ObAggCell::reset();
  exclude_null_ = false;
  row_count_ = 0;
}

void ObCountAggCell::reuse()
{
  ObAggCell::reuse();
  row_count_ = 0;
}

int ObCountAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  return ObAggCell::init(is_group_by, eval_ctx);
}

int ObCountAggCell::eval(
    blocksstable::ObStorageDatum &datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!exclude_null_) {
    row_count_ += row_count;
  } else if (OB_FAIL(fill_default_if_need(datum))) {
  } else if (!datum.is_null()) {
    row_count_ += row_count;
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  return ret;
}

int ObCountAggCell::eval_batch(const common::ObDatum *datums, const int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == datums || row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(exclude_null_ || nullptr != datums)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    row_count_ += row_count;
    aggregated_ = true;
  }
  return ret;
}

int ObCountAggCell::eval_micro_block(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t col_offset,
    blocksstable::ObIMicroBlockReader *reader,
    const int32_t *row_ids,
    const int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (!exclude_null_) {
    row_count_ += row_count;
  } else {
    int64_t valid_row_count = 0;
    if (OB_ISNULL(row_ids)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(reader->get_row_count(col_offset, row_ids, row_count, false, basic_info_.col_param_, valid_row_count))) {
    } else {
      row_count_ += valid_row_count;
    }
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  return ret;
}

int ObCountAggCell::eval_index_info(const blocksstable::ObMicroIndexInfo &index_info)
{
  int ret = OB_SUCCESS;
  if (!index_info.can_blockscan() || index_info.is_left_border() || index_info.is_right_border()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!exclude_null_) {
    row_count_ += index_info.get_row_count();
  } else if (OB_UNLIKELY(skip_index_datum_.is_null() || skip_index_datum_is_prefix_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    row_count_ += index_info.get_row_count() - skip_index_datum_.get_int();
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  LOG_DEBUG("eval_index_info", K(ret), K(index_info.get_row_count()), K(row_count_));
  return ret;
}

template<bool EXCLUDE_NULL, bool GROUP_BY_COL>
void eval_batch_for_count(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    ObNativeAggDatumBuf *result_datums)
{}

template<>
void eval_batch_for_count<false, false>(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    ObNativeAggDatumBuf *result_datums)
{
  for (int64_t i = 0; i < count; ++i) {
    ObAggNativeCell &result_datum = result_datums->at(refs[i]);
    result_datum.set_int(result_datum.get_int() + 1);
  }
}

template<>
void eval_batch_for_count<true, false>(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    ObNativeAggDatumBuf *result_datums)
{
  for (int64_t i = 0; i < count; ++i) {
    ObAggNativeCell &result_datum = result_datums->at(refs[i]);
    if (!datums[i].is_null()) {
      result_datum.set_int(result_datum.get_int() + 1);
    }
  }
}

template<>
void eval_batch_for_count<true, true>(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    ObNativeAggDatumBuf *result_datums)
{
  for (int64_t i = 0; i < count; ++i) {
    ObAggNativeCell &result_datum = result_datums->at(refs[i]);
    if (!datums[refs[i]].is_null()) {
      result_datum.set_int(result_datum.get_int() + 1);
    }
  }
}

typedef void (*eval_batch_for_count_func) (
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    ObNativeAggDatumBuf *result_datums);
static eval_batch_for_count_func EVAL_BATCH_COUNT_FUNCS[2][2] = {
  {eval_batch_for_count<false, false>, eval_batch_for_count<false, false>},
  {eval_batch_for_count<true, false>, eval_batch_for_count<true, true>}
};

int ObCountAggCell::eval_batch_in_group_by(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    const int64_t distinct_cnt,
    const bool is_group_by_col,
    const bool is_default_datum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_default_datum)) {
    const bool should_inc = !exclude_null_ || !datums[0].is_null();
    if (should_inc) {
      for (int64_t i = 0; i < count; ++i) {
        ObAggNativeCell &result_datum = group_by_result_datum_buf_->at(refs[i]);
        result_datum.set_int(result_datum.get_int() + 1);
      }
    }
  } else {
    eval_batch_for_count_func eval_func = EVAL_BATCH_COUNT_FUNCS[exclude_null_][is_group_by_col];
    eval_func(datums, count, refs, group_by_result_datum_buf_);
  }
  return ret;
}

int ObCountAggCell::copy_output_rows(const int32_t start_offset, const int32_t end_offset)
{
  int ret = OB_SUCCESS;
  if (exclude_null_ && native_eval_ctx_ != nullptr) { ret = import_native_inputs(*native_eval_ctx_, start_offset, end_offset); }
  if (OB_FAIL(ret)) {
  } else if (OB_UNLIKELY(end_offset <= start_offset || end_offset > group_by_result_datum_buf_->get_basic_count())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObAggNativeCell *result_datums = group_by_result_datum_buf_->get_basic_buf();
    if (exclude_null_) {
      for (int64_t i = start_offset; i < end_offset; i++) {
        col_datums_[i].is_null() ? result_datums[i].set_int(0) : result_datums[i].set_int(1);
      }
    } else {
      for (int64_t i = start_offset; i < end_offset; i++) {
        result_datums[i].set_int(1);
      }
    }
    LOG_DEBUG("[GROUP BY PUSHDOWN]", K(ret), K(start_offset), K(end_offset),
        K(common::ObArrayWrap<common::ObDatum>(col_datums_ + start_offset, end_offset - start_offset)),
        K(common::ObArrayWrap<ObAggNativeCell>(result_datums + start_offset, end_offset - start_offset)));
  }
  return ret;
}

int ObCountAggCell::copy_single_output_row(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  common::Datum &result_datum = basic_info_.agg_expr_->locate_datum_for_write(ctx);
  if (exclude_null_) {
    if (OB_ISNULL(basic_info_.agg_expr_->args_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      common::Datum &datum = basic_info_.agg_expr_->args_[0]->locate_expr_datum(ctx);
      if (!datum.is_null()) {
        result_datum.set_int(1);
      } else {
        result_datum.set_int(0);
      }
    }
  } else {
    result_datum.set_int(1);
  }
  return ret;
}

int ObCountAggCell::collect_batch_result_in_group_by(const int64_t distinct_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(distinct_cnt > group_by_result_datum_buf_->get_capacity())) {
    ret = OB_INVALID_ARGUMENT;
  }
  return ret;
}

int ObCountAggCell::collect_result(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObDatum result;
  alignas(8) char result_buffer[NATIVE_LEGACY_SCALAR_BYTES];
  result.ptr_ = result_buffer;
  sql::ObEvalInfo &eval_info = basic_info_.agg_expr_->get_eval_info(ctx);
  result.set_int(row_count_);
  eval_info.evaluated_ = true;
  if (OB_SUCC(ret)) { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, result); }
  return ret;
}

namespace {
int update_native_extreme(ObAggNativeCell &result, const ObDatum &input,
    ObDatumCmpFuncType compare, const ObDatumAccessContext *access, bool minimum)
{
  if (input.is_null()) { return OB_SUCCESS; }
  ObDatum retained;
  alignas(8) char retained_scratch[64];
  int ret = result.export_legacy(retained, retained_scratch, sizeof(retained_scratch));
  int compared = 0;
  if (OB_FAIL(ret)) {
  } else if (!retained.is_null() && OB_FAIL(compare(retained, input, compared, access))) {
  } else if (retained.is_null() || (minimum ? compared > 0 : compared < 0)) {
    ret = result.capture_legacy(input);
  }
  return ret;
}
}

ObMinAggCell::ObMinAggCell(const ObAggCellBasicInfo &basic_info, common::ObIAllocator &allocator)
    : ObAggCell(basic_info, allocator),
      cmp_fun_(nullptr),
      datum_access_ctx_(nullptr),
      group_by_ref_array_(nullptr),
      datum_allocator_("ObStorageAgg", OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  agg_type_ =ObPDAggType::PD_MIN;
  result_datum_.set_null();
}

void ObMinAggCell::reset()
{
  ObAggCell::reset();
  cmp_fun_ = nullptr;
  datum_access_ctx_ = nullptr;
  if (nullptr != group_by_ref_array_) {
    allocator_.free(group_by_ref_array_);
    group_by_ref_array_ = nullptr;
  }
  datum_allocator_.reset();
}

void ObMinAggCell::reuse()
{
  ObAggCell::reuse();
  datum_allocator_.reuse();
}

int ObMinAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(eval_ctx) ||
      OB_ISNULL(sql::storage_predicate_functions(*basic_info_.agg_expr_)) ||
      OB_ISNULL(sql::storage_predicate_functions(*basic_info_.agg_expr_)->null_first_cmp_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAggCell::init(is_group_by, eval_ctx))) {
  } else if (OB_FAIL(eval_ctx->get_datum_access_ctx(datum_access_ctx_))) {
  } else {
    cmp_fun_ = sql::storage_predicate_functions(*basic_info_.agg_expr_)->null_first_cmp_;
  }
  return ret;
}

int ObMinAggCell::eval(
    blocksstable::ObStorageDatum &storage_datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = row_count < 0 ? OB_INVALID_ARGUMENT : fill_default_if_need(storage_datum);
  if (OB_SUCC(ret) && !storage_datum.is_null()) { ret = pad_column_if_need(storage_datum, padding_allocator_); }
  if (OB_SUCC(ret)) { ret = update_native_extreme(result_datum_, storage_datum, cmp_fun_, datum_access_ctx_, true); }
  if (OB_SUCC(ret)) { aggregated_ = true; }
  return ret;
}

int ObMinAggCell::eval_batch(const common::ObDatum *datums, const int64_t count)
{
  if (datums == nullptr || count < 0) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ret = update_native_extreme(result_datum_, datums[i], cmp_fun_, datum_access_ctx_, true);
  }
  if (OB_SUCC(ret)) { aggregated_ = true; }
  return ret;
}

int ObMinAggCell::eval_batch_in_group_by(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    const int64_t distinct_cnt,
    const bool is_group_by_col,
    const bool is_default_datum)
{
  if (datums == nullptr || refs == nullptr || count < 0 || count > basic_info_.batch_size_
      || distinct_cnt < 0 || distinct_cnt > group_by_result_datum_buf_->get_capacity()) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (refs[i] >= distinct_cnt) { ret = OB_INVALID_ARGUMENT; }
    else {
      const ObDatum &input = datums[is_default_datum ? 0 : is_group_by_col ? refs[i] : i];
      ret = update_native_extreme(group_by_result_datum_buf_->at(refs[i]), input, cmp_fun_, datum_access_ctx_, true);
    }
  }
  return ret;
}

int ObMinAggCell::pad_column_in_group_by(const int64_t row_cap, common::ObIAllocator &allocator)
{
  if (!basic_info_.need_padding()) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    ObDatum view;
    alignas(8) char scratch[64];
    ObAggNativeCell &cell = group_by_result_datum_buf_->at(native_projection_offset_ + i);
    ObDatum *view_ptr = &view;
    if (OB_FAIL(cell.export_legacy(view, scratch, sizeof(scratch)))) {
    } else if (OB_FAIL(storage::pad_on_datums(basic_info_.col_param_->get_accuracy(),
        basic_info_.col_param_->get_meta_type().get_collation_type(), allocator, 1, view_ptr))) {
    } else { ret = cell.capture_legacy(*view_ptr); }
  }
  return ret;
}

ObMaxAggCell::ObMaxAggCell(const ObAggCellBasicInfo &basic_info, common::ObIAllocator &allocator)
    : ObAggCell(basic_info, allocator),
      cmp_fun_(nullptr),
      datum_access_ctx_(nullptr),
      group_by_ref_array_(nullptr),
      datum_allocator_("ObStorageAgg", OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  agg_type_ = ObPDAggType::PD_MAX;
  result_datum_.set_null();
}

void ObMaxAggCell::reset()
{
  ObAggCell::reset();
  cmp_fun_ = nullptr;
  datum_access_ctx_ = nullptr;
  if (nullptr != group_by_ref_array_) {
    allocator_.free(group_by_ref_array_);
    group_by_ref_array_ = nullptr;
  }
  datum_allocator_.reset();
}

void ObMaxAggCell::reuse()
{
  ObAggCell::reuse();
  datum_allocator_.reuse();
}

int ObMaxAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(eval_ctx) ||
      OB_ISNULL(sql::storage_predicate_functions(*basic_info_.agg_expr_)) ||
      OB_ISNULL(sql::storage_predicate_functions(*basic_info_.agg_expr_)->null_first_cmp_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAggCell::init(is_group_by, eval_ctx))) {
  } else if (OB_FAIL(eval_ctx->get_datum_access_ctx(datum_access_ctx_))) {
  } else {
    cmp_fun_ = sql::storage_predicate_functions(*basic_info_.agg_expr_)->null_first_cmp_;
  }
  return ret;
}

int ObMaxAggCell::eval(
    blocksstable::ObStorageDatum &storage_datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = row_count < 0 ? OB_INVALID_ARGUMENT : fill_default_if_need(storage_datum);
  if (OB_SUCC(ret) && !storage_datum.is_null()) { ret = pad_column_if_need(storage_datum, padding_allocator_); }
  if (OB_SUCC(ret)) { ret = update_native_extreme(result_datum_, storage_datum, cmp_fun_, datum_access_ctx_, false); }
  if (OB_SUCC(ret)) { aggregated_ = true; }
  return ret;
}

int ObMaxAggCell::eval_batch(const common::ObDatum *datums, const int64_t count)
{
  if (datums == nullptr || count < 0) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    ret = update_native_extreme(result_datum_, datums[i], cmp_fun_, datum_access_ctx_, false);
  }
  if (OB_SUCC(ret)) { aggregated_ = true; }
  return ret;
}

int ObMaxAggCell::eval_batch_in_group_by(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    const int64_t distinct_cnt,
    const bool is_group_by_col,
    const bool is_default_datum)
{
  if (datums == nullptr || refs == nullptr || count < 0 || count > basic_info_.batch_size_
      || distinct_cnt < 0 || distinct_cnt > group_by_result_datum_buf_->get_capacity()) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (refs[i] >= distinct_cnt) { ret = OB_INVALID_ARGUMENT; }
    else {
      const ObDatum &input = datums[is_default_datum ? 0 : is_group_by_col ? refs[i] : i];
      ret = update_native_extreme(group_by_result_datum_buf_->at(refs[i]), input, cmp_fun_, datum_access_ctx_, false);
    }
  }
  return ret;
}

int ObMaxAggCell::pad_column_in_group_by(const int64_t row_cap, common::ObIAllocator &allocator)
{
  if (!basic_info_.need_padding()) { return OB_SUCCESS; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < row_cap; ++i) {
    ObDatum view;
    alignas(8) char scratch[64];
    ObAggNativeCell &cell = group_by_result_datum_buf_->at(native_projection_offset_ + i);
    ObDatum *view_ptr = &view;
    if (OB_FAIL(cell.export_legacy(view, scratch, sizeof(scratch)))) {
    } else if (OB_FAIL(storage::pad_on_datums(basic_info_.col_param_->get_accuracy(),
        basic_info_.col_param_->get_meta_type().get_collation_type(), allocator, 1, view_ptr))) {
    } else { ret = cell.capture_legacy(*view_ptr); }
  }
  return ret;
}

ObHyperLogLogAggCell::ObHyperLogLogAggCell(const ObAggCellBasicInfo &basic_info, common::ObIAllocator &allocator)
    : ObAggCell(basic_info, allocator),
      hash_func_(nullptr),
      datum_access_ctx_(nullptr),
      ndv_calculator_(),
      def_hash_value_(0)
{
  agg_type_ = ObPDAggType::PD_HLL;
}

void ObHyperLogLogAggCell::reset()
{
  hash_func_ = nullptr;
  if (OB_NOT_NULL(ndv_calculator_)) {
    ndv_calculator_->destroy();
    allocator_.free(ndv_calculator_);
    ndv_calculator_ = nullptr;
  }
  def_hash_value_ = 0;
  ObAggCell::reset();
}

void ObHyperLogLogAggCell::reuse()
{
  ObAggCell::reuse();
  if (OB_NOT_NULL(ndv_calculator_)) {
    ndv_calculator_->reuse();
  }
}

int ObHyperLogLogAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (OB_ISNULL(eval_ctx) ||
      OB_ISNULL(basic_info_.agg_expr_->args_) ||
      OB_ISNULL(basic_info_.agg_expr_->args_[0]) ||
      OB_ISNULL(sql::storage_predicate_functions(*basic_info_.agg_expr_->args_[0])) ||
      OB_ISNULL(sql::storage_predicate_functions(*basic_info_.agg_expr_->args_[0])->murmur_hash_) ||
      OB_UNLIKELY(T_REF_COLUMN != basic_info_.agg_expr_->args_[0]->type_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAggCell::init(is_group_by, eval_ctx))) {
  } else if (OB_FAIL(eval_ctx->get_datum_access_ctx(datum_access_ctx_))) {
  } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObHyperLogLogCalculator)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ndv_calculator_ = new (buf) ObHyperLogLogCalculator();
    if (OB_FAIL(ndv_calculator_->init(&allocator_, LLC_BUCKET_BITS))) {
    } else {
      hash_func_ = sql::storage_predicate_functions(*basic_info_.agg_expr_->args_[0])->murmur_hash_;
    }
  }

  if (OB_SUCC(ret) && nullptr != basic_info_.col_param_ &&
        !(basic_info_.col_param_->get_orig_default_datum().is_nop()
          || (basic_info_.col_param_->get_orig_default_datum().has_ext_value()
              && basic_info_.col_param_->get_orig_default_datum().get_ext() == ObActionFlag::OP_NOP))) {
    uint64_t hash_value = 0;
    if (OB_FAIL(prepare_def_datum())) {
    } else if (def_datum_.is_null()) {
    } else if (OB_FAIL(hash_func_(
                   def_datum_, hash_value, hash_value, datum_access_ctx_))) {
    } else {
      def_hash_value_ = hash_value;
    }
  }
  return ret;
}

// fuse padding datums in expr, storage_datum is not padding
int ObHyperLogLogAggCell::eval(
    blocksstable::ObStorageDatum &storage_datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = OB_SUCCESS;
  uint64_t hash_value = 0; // same as ObAggregateProcessor llc hash_value
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (storage_datum.is_nop()) {
    if (!def_datum_.is_null()) {
      ndv_calculator_->set(def_hash_value_);
    }
  } else if (storage_datum.is_null()) {
    // ndv does not consider null
  } else if (OB_FAIL(pad_column_if_need(storage_datum, padding_allocator_))) {
  } else if (OB_FAIL(hash_func_(
                 storage_datum, hash_value, hash_value, datum_access_ctx_))) {
  } else {
    ndv_calculator_->set(hash_value);
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  return ret;
}

// eval_batch() is invoked by ObAggCell::eval_micro_block()
// Like Min/Max aggregate, does not have the capability for batch processing now.
int ObHyperLogLogAggCell::eval_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == datums || count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      uint64_t hash_value = 0; // same as ObAggregateProcessor llc hash_value
      const common::ObDatum &datum = datums[i];
      if (datum.is_null()) {
        // ndv does not consider null
      } else if (OB_FAIL(hash_func_(
                     datum, hash_value, hash_value, datum_access_ctx_))) {
      } else {
        ndv_calculator_->set(hash_value);
      }
    }
    if (OB_SUCC(ret)) {
      aggregated_ = true;
    }
  }
  return ret;
}

int ObHyperLogLogAggCell::collect_result(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObDatum result;
  alignas(8) char result_buffer[NATIVE_LEGACY_SCALAR_BYTES];
  result.ptr_ = result_buffer;
  sql::ObEvalInfo &eval_info = basic_info_.agg_expr_->get_eval_info(ctx);
  result.set_string(ndv_calculator_->get_buckets(), ndv_calculator_->get_bucket_num());
  eval_info.evaluated_ = true;
  if (OB_SUCC(ret)) { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, result); }
  return ret;
}

ObSumOpSizeAggCell::ObSumOpSizeAggCell(
    const ObAggCellBasicInfo &basic_info,
    common::ObIAllocator &allocator,
    const bool exclude_null)
    : ObAggCell(basic_info, allocator),
      op_size_(0),
      def_op_size_(0),
      total_size_(0),
      exclude_null_(exclude_null)
{
  agg_type_ = ObPDAggType::PD_SUM_OP_SIZE;
}

void ObSumOpSizeAggCell::reset()
{
  op_size_ = 0;
  def_op_size_ = 0;
  total_size_ = 0;
  exclude_null_ = false;
  ObAggCell::reset();
}

void ObSumOpSizeAggCell::reuse()
{
  ObAggCell::reuse();
  total_size_ = 0;
}

int ObSumOpSizeAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObAggCell::init(is_group_by, eval_ctx))) {
  } else if (OB_FAIL(set_op_size())) {
  } else if (nullptr != basic_info_.col_param_ &&
             !(basic_info_.col_param_->get_orig_default_datum().is_nop()
               || (basic_info_.col_param_->get_orig_default_datum().has_ext_value()
                   && basic_info_.col_param_->get_orig_default_datum().get_ext() == ObActionFlag::OP_NOP))) {
    if (OB_FAIL(prepare_def_datum())) {
    } else if (OB_FAIL(get_datum_op_size(def_datum_, def_op_size_))) {
    }
  }
  return ret;
}

int ObSumOpSizeAggCell::set_op_size()
{
  int ret = OB_SUCCESS;
  SqlValueDatumMapType type = OBJ_DATUM_MAPPING_MAX;
  if (OB_ISNULL(basic_info_.agg_expr_->args_) ||
      OB_ISNULL(basic_info_.agg_expr_->args_[0]) ||
      OB_UNLIKELY(T_REF_COLUMN != basic_info_.agg_expr_->args_[0]->type_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(type = basic_info_.agg_expr_->args_[0]->obj_datum_map_)) {
  } else if (OB_UNLIKELY(type >= common::OBJ_DATUM_MAPPING_MAX || type <= common::OBJ_DATUM_NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (is_fixed_length_type()) {
    switch (type) {
      case common::OBJ_DATUM_8BYTE_DATA : {
        op_size_ = sizeof(ObDatum) + 8;
        break;
      }
      case common::OBJ_DATUM_4BYTE_DATA : {
        op_size_ = sizeof(ObDatum) + 4;
        break;
      }
      case common::OBJ_DATUM_1BYTE_DATA : {
        op_size_ = sizeof(ObDatum) + 1;
        break;
      }
      case common::OBJ_DATUM_4BYTE_LEN_DATA : {
        op_size_ = sizeof(ObDatum) + 12;
        break;
      }
      case common::OBJ_DATUM_2BYTE_LEN_DATA : {
        op_size_ = sizeof(ObDatum) + 10;
        break;
      }
      case common::OBJ_DATUM_EXTEND : {
        op_size_ = sizeof(ObDatum) + 16;
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObSumOpSizeAggCell::get_datum_op_size(const ObDatum &datum, int64_t &length)
{
  int ret = OB_SUCCESS;
  if (!is_lob_col() || datum.is_null()) {
    length = sizeof(ObDatum) + datum.len_;
  } else {
    ObLobLocatorV2 locator(datum.get_string(), basic_info_.agg_expr_->args_[0]->obj_meta_.has_lob_header());
    int64_t lob_data_byte_len = 0;
    if (OB_FAIL(locator.get_lob_data_byte_len(lob_data_byte_len))) {
    } else {
      length = sizeof(ObDatum) + lob_data_byte_len;
    }
  }
  return ret;
}

int ObSumOpSizeAggCell::eval(
    blocksstable::ObStorageDatum &storage_datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = OB_SUCCESS;
  int64_t length = 0;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (storage_datum.is_nop()) {
    total_size_ += def_op_size_ * row_count;
  } else if (OB_FAIL(pad_column_if_need(storage_datum, padding_allocator_))) {
  } else if (OB_FAIL(get_datum_op_size(storage_datum, length))) {
  } else {
    total_size_ += length * row_count; // row_count is not always 1
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  return ret;
}

// eval_batch() is invoked by ObAggCell::eval_micro_block()
// Like Min/Max aggregate, does not have the capability for batch processing now.
int ObSumOpSizeAggCell::eval_batch(const common::ObDatum *datums, const int64_t row_count)
{
  int ret = OB_SUCCESS;
  int64_t length = 0;
  if (OB_UNLIKELY(nullptr == datums || row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row_count; ++i) {
      // consider lob
      const ObDatum datum = datums[i];
      if (OB_FAIL(get_datum_op_size(datum, length))) {
      } else {
        total_size_ += length;
      }
    }
    if (OB_SUCC(ret)) {
      aggregated_ = true;
    }
  }
  return ret;
}

int ObSumOpSizeAggCell::eval_micro_block(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t col_offset,
    blocksstable::ObIMicroBlockReader *reader,
    const int32_t *row_ids,
    const int64_t row_count)
{
  int ret = OB_SUCCESS;
  int64_t size = 0;
  if (need_access_data()) {
    if (is_fixed_length_type()) {
      int64_t valid_row_count = 0;
      if (OB_ISNULL(row_ids)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(reader->get_row_count(col_offset, row_ids, row_count, false, basic_info_.col_param_, valid_row_count))) {
      } else {
        total_size_ += (row_count - valid_row_count) * sizeof(ObDatum) + valid_row_count * op_size_;
      }
    } else if (OB_FAIL(ObAggCell::eval_micro_block(iter_param, context, col_offset, reader, row_ids, row_count))) {
    }
  } else {
    total_size_ += row_count * op_size_;
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  return ret;
}

int ObSumOpSizeAggCell::eval_index_info(const blocksstable::ObMicroIndexInfo &index_info)
{
  int ret = OB_SUCCESS;
  // consider the judge condition
  if (!index_info.can_blockscan() || index_info.is_left_border() || index_info.is_right_border()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (!exclude_null_) {
    total_size_ += index_info.get_row_count() * op_size_;
  } else if (OB_UNLIKELY(skip_index_datum_.is_null() || skip_index_datum_is_prefix_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t null_count = skip_index_datum_.get_int();
    total_size_ += (index_info.get_row_count() - null_count) * op_size_ + null_count * sizeof(ObDatum);
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  LOG_DEBUG("eval_index_info", K(ret), K(index_info.get_row_count()), K_(op_size), K_(total_size));
  return ret;
}

int ObSumOpSizeAggCell::collect_result(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObDatum result;
  alignas(8) char result_buffer[NATIVE_LEGACY_SCALAR_BYTES];
  result.ptr_ = result_buffer;
  sql::ObEvalInfo &eval_info = basic_info_.agg_expr_->get_eval_info(ctx);
  {
    result.set_uint(total_size_);
    eval_info.evaluated_ = true;
  }
 if (OB_SUCC(ret)) { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, result); }
 return ret;
}

ObSumAggCell::ObSumAggCell(const ObAggCellBasicInfo &basic_info, common::ObIAllocator &allocator)
    : ObAggCell(basic_info, allocator),
      obj_tc_(ObNullTC),
      sum_use_int_flag_(false),
      is_sum_use_temp_buf_(false),
      num_int_(0),
      sum_use_int_flag_buf_(nullptr),
      num_buf_(nullptr),
      eval_func_(nullptr),
      eval_batch_func_(nullptr),
      copy_datum_func_(nullptr),
      exec_ctx_(nullptr),
      cast_datum_(),
      cast_temp_buffer_(nullptr),
      datum_allocator_("ObStorageAgg", OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  agg_type_ = ObPDAggType::PD_SUM;
  result_datum_.set_null();
}

void ObSumAggCell::reset()
{
  obj_tc_ = ObNullTC;
  sum_use_int_flag_ = false;
  num_int_ = 0;
  free_group_by_buf(allocator_, sum_use_int_flag_buf_);
  free_group_by_buf(allocator_, num_int_buf_);
  eval_func_ = nullptr;
  eval_batch_func_ = nullptr;
  copy_datum_func_ = nullptr;
  exec_ctx_ = nullptr;
  if (is_sum_use_temp_buf_) {
    if (nullptr != cast_temp_buffer_) {
      allocator_.free(cast_temp_buffer_);
      cast_temp_buffer_ = nullptr;
    }
    is_sum_use_temp_buf_ = false;
  }
  cast_datum_.reset();
  ObAggCell::reset();
  datum_allocator_.reset();
}

void ObSumAggCell::reuse()
{
  ObAggCell::reuse();
  if (is_sum_use_temp_buf_) {
    cast_datum_.ptr_ = cast_temp_buffer_;
  }
  sum_use_int_flag_ = false;
  num_int_ = 0;
  clear_group_by_info();
  datum_allocator_.reset();
}

void ObSumAggCell::clear_group_by_info()
{
  if (nullptr != sum_use_int_flag_buf_) {
    sum_use_int_flag_buf_->fill_items(false);
  }
  if (nullptr != num_int_buf_) {
    num_int_buf_->fill_items(0);
  }
}


int ObSumAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(eval_ctx) ||
      OB_UNLIKELY(nullptr == basic_info_.agg_expr_->args_ ||
      nullptr == basic_info_.agg_expr_->args_[0] ||
      T_REF_COLUMN != basic_info_.agg_expr_->args_[0]->type_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAggCell::init(is_group_by, eval_ctx))) {
  } else {
    exec_ctx_ = &eval_ctx->exec_ctx_;
    obj_tc_ = sql_type_class(basic_info_.agg_expr_->args_[0]->datum_meta_.type_);
    const SqlTypeClass res_tc = sql_type_class(basic_info_.agg_expr_->datum_meta_.type_);
    const int16_t precision = basic_info_.agg_expr_->datum_meta_.precision_;
    eval_skip_index_func_ = nullptr;
    switch (obj_tc_) {
      case SqlTypeClass::ObIntTC: {
        switch (res_tc) {
          case SqlTypeClass::ObNumberTC: {
            eval_func_ = &ObSumAggCell::eval_int<number::ObNumber>;
            eval_batch_func_ = &ObSumAggCell::eval_int_batch<number::ObNumber>;
            copy_datum_func_ = &ObSumAggCell::copy_int_to_number;
            eval_skip_index_func_ = &ObSumAggCell::eval_number;
            break;
          }
          case SqlTypeClass::ObDecimalIntTC: {
            if (OB_UNLIKELY(precision < OB_DECIMAL_LONGLONG_DIGITS ||
                              precision > MAX_PRECISION_DECIMAL_INT_256)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (precision <= MAX_PRECISION_DECIMAL_INT_128) {
              eval_func_ = &ObSumAggCell::eval_int<int128_t>;
              eval_batch_func_ = &ObSumAggCell::eval_int_batch<int128_t>;
              copy_datum_func_ = &ObSumAggCell::copy_int_to_decimal_int<int128_t>;
            } else {
              eval_func_ = &ObSumAggCell::eval_int<int256_t>;
              eval_batch_func_ = &ObSumAggCell::eval_int_batch<int256_t>;
              copy_datum_func_ = &ObSumAggCell::copy_int_to_decimal_int<int256_t>;
            }
            break;
          }
          default: {
            ret = OB_ERR_UNEXPECTED;
            break;
          }
        }
        break;
      }
      case SqlTypeClass::ObUIntTC: {
        switch (res_tc) {
          case SqlTypeClass::ObNumberTC: {
            eval_func_ = &ObSumAggCell::eval_uint<number::ObNumber>;
            eval_batch_func_ = &ObSumAggCell::eval_uint_batch<number::ObNumber>;
            copy_datum_func_ = &ObSumAggCell::copy_uint_to_number;
            eval_skip_index_func_ = &ObSumAggCell::eval_number;
            break;
          }
          case SqlTypeClass::ObDecimalIntTC: {
            if (OB_UNLIKELY(precision < OB_DECIMAL_LONGLONG_DIGITS ||
                              precision > MAX_PRECISION_DECIMAL_INT_256)) {
              ret = OB_ERR_UNEXPECTED;
            } else if (precision <= MAX_PRECISION_DECIMAL_INT_128) {
              eval_func_ = &ObSumAggCell::eval_uint<int128_t>;
              eval_batch_func_ = &ObSumAggCell::eval_uint_batch<int128_t>;
              copy_datum_func_ = &ObSumAggCell::copy_uint_to_decimal_int<int128_t>;
            } else {
              eval_func_ = &ObSumAggCell::eval_uint<int256_t>;
              eval_batch_func_ = &ObSumAggCell::eval_uint_batch<int256_t>;
              copy_datum_func_ = &ObSumAggCell::copy_uint_to_decimal_int<int256_t>;
            }
            break;
          }
          default: {
            ret = OB_ERR_UNEXPECTED;
            break;
          }
        }
        break;
      }
      case SqlTypeClass::ObFloatTC: {
        eval_func_ = &ObSumAggCell::eval_float;
        eval_batch_func_ = &ObSumAggCell::eval_float_batch;
        copy_datum_func_ = &ObSumAggCell::copy_float;
        eval_skip_index_func_ = &ObSumAggCell::eval_float;
        break;
      }
      case SqlTypeClass::ObDoubleTC: {
        eval_func_ = &ObSumAggCell::eval_double;
        eval_batch_func_ = &ObSumAggCell::eval_double_batch;
        copy_datum_func_ = &ObSumAggCell::copy_double;
        eval_skip_index_func_ = &ObSumAggCell::eval_double;
        break;
      }
      case SqlTypeClass::ObNumberTC: {
        eval_func_ = &ObSumAggCell::eval_number;
        eval_batch_func_ = &ObSumAggCell::eval_number_batch;
        copy_datum_func_ = &ObSumAggCell::copy_number;
        eval_skip_index_func_ = &ObSumAggCell::eval_number;
        break;
      }
      case SqlTypeClass::ObDecimalIntTC: {
        ret = ObSumAggCell::init_decimal_int_func();
        break;
      }
      case SqlTypeClass::ObCollectionSQLTC: {
        eval_func_ = &ObSumAggCell::eval_collection;
        eval_batch_func_ = &ObSumAggCell::eval_collection_batch;
        copy_datum_func_ = &ObSumAggCell::copy_vector;
        eval_skip_index_func_ = &ObSumAggCell::eval_collection;
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (eval_skip_index_func_ == nullptr && OB_FAIL(init_eval_skip_index_func_for_decimal())) {
  } else if (is_group_by) {
    const int64_t datum_size = is_sum_use_temp_buf_ ? common::OBJ_DATUM_DECIMALINT_MAX_RES_SIZE : common::OBJ_DATUM_NUMBER_RES_SIZE;
    UNUSED(datum_size); // native cells carry their actual kernel span independently
    if (SqlTypeClass::ObIntTC == obj_tc_ || SqlTypeClass::ObUIntTC == obj_tc_) {
      if (OB_FAIL(new_group_by_buf((bool*)nullptr, 0, sizeof(bool), allocator_, sum_use_int_flag_buf_))) {
      } else if (OB_FAIL(sum_use_int_flag_buf_->reserve(basic_info_.batch_size_))) {
      } else if (OB_FAIL(new_group_by_buf((int64_t*)(nullptr), 0, sizeof(int64_t), allocator_, num_int_buf_))) {
      } else if (OB_FAIL(num_int_buf_->reserve(basic_info_.batch_size_))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    const SqlTypeClass result_class = sql_type_class(basic_info_.agg_expr_->datum_meta_.type_);
    ObAggNativeDomain domain = result_class == ObFloatTC ? ObAggNativeDomain::Float32
        : result_class == ObDoubleTC ? ObAggNativeDomain::Float64
        : result_class == ObNumberTC ? ObAggNativeDomain::Number
        : result_class == ObDecimalIntTC ? ObAggNativeDomain::DecimalCoefficient
        : ObAggNativeDomain::Semantic;
    const int32_t width = domain == ObAggNativeDomain::DecimalCoefficient
        ? wide::ObDecimalIntConstValue::get_int_bytes_by_precision(basic_info_.agg_expr_->datum_meta_.precision_) : 0;
    result_datum_.configure(&basic_info_.agg_expr_->get_sql_type(), domain, width);
    if (group_by_result_datum_buf_ != nullptr) {
      group_by_result_datum_buf_->configure(&basic_info_.agg_expr_->get_sql_type(), domain, width);
    }
  }
  return ret;
}

int ObSumAggCell::init_decimal_int_func()
{
  int ret = OB_SUCCESS;
// The sum aggregation function will increase the precision by 22. Based on this rule,
// we can get the mapping of each input to output:
//   a. int32_t [0 - 9] => result precision range [22 - 31], which always be int128_t.
//   b. int64_t [10 - 18] => result P range [32, 40], which can be int128_t or int256_t.
//   c. int128_t [19 - 38] => result P range [41, 60], which always be int256.
//   d. int256_t [39 - 76] => result P range [61, 98], which can be int256_t or int512_t.
  const static ObSumEvalAggFuncType AGG_FUNCS[][3] = {
    // int32_t
    {
      &ObSumAggCell::eval_decimal_int<int128_t, int32_t>,
      nullptr,
      &ObSumAggCell::eval_decimal_int_number<int32_t>
    },
    // int64_t
    {
      &ObSumAggCell::eval_decimal_int<int128_t, int64_t>,
      &ObSumAggCell::eval_decimal_int<int256_t, int64_t>,
      &ObSumAggCell::eval_decimal_int_number<int64_t>
    },
    // int128_t
    {
      &ObSumAggCell::eval_decimal_int<int256_t, int128_t>,
      nullptr,
      &ObSumAggCell::eval_decimal_int_number<int128_t>
    },
    // int256_t
    {
      &ObSumAggCell::eval_decimal_int<int256_t, int256_t>,
      &ObSumAggCell::eval_decimal_int<int512_t, int256_t>,
      &ObSumAggCell::eval_decimal_int_number<int256_t>
    }
  };

  const static ObSumCopyDatumFuncType COPY_FUNCS[][3] = {
    // int32_t
    {
      &ObSumAggCell::copy_decimal_int<int128_t, int32_t>,
      nullptr,
      &ObSumAggCell::copy_decimal_int_to_number<int32_t>
    },
    // int64_t
    {
      &ObSumAggCell::copy_decimal_int<int128_t, int64_t>,
      &ObSumAggCell::copy_decimal_int<int256_t, int64_t>,
      &ObSumAggCell::copy_decimal_int_to_number<int64_t>
    },
    // int128_t
    {
      &ObSumAggCell::copy_decimal_int<int256_t, int128_t>,
      nullptr,
      &ObSumAggCell::copy_decimal_int_to_number<int128_t>
    },
    // int256_t
    {
      &ObSumAggCell::copy_decimal_int<int256_t, int256_t>,
      &ObSumAggCell::copy_decimal_int<int512_t, int256_t>,
      &ObSumAggCell::copy_decimal_int_to_number<int256_t>
    }
  };

  const static ObSumEvalBatchAggFuncType AGG_BATCH_FUNCS[][3][2] = {
    // int32_t
    {
      { &ObSumAggCell::eval_decimal_int_batch<int128_t, int32_t, int32_t>,
        &ObSumAggCell::eval_decimal_int_batch<int128_t, int64_t, int32_t> },
      { nullptr, nullptr,},
      { &ObSumAggCell::eval_decimal_int_number_batch<int32_t, int32_t>,
        &ObSumAggCell::eval_decimal_int_number_batch<int64_t, int32_t> }
    },
    // int64_t
    {
      { &ObSumAggCell::eval_decimal_int_batch<int128_t, int64_t, int64_t>,
        &ObSumAggCell::eval_decimal_int_batch<int128_t, int128_t, int64_t> },
      { &ObSumAggCell::eval_decimal_int_batch<int256_t, int64_t, int64_t>,
        &ObSumAggCell::eval_decimal_int_batch<int256_t, int128_t, int64_t> },
      { &ObSumAggCell::eval_decimal_int_number_batch<int64_t, int64_t>,
        &ObSumAggCell::eval_decimal_int_number_batch<int128_t, int64_t> }
    },
    // int128_t
    {
      { &ObSumAggCell::eval_decimal_int_batch<int256_t, int128_t, int128_t>,
        &ObSumAggCell::eval_decimal_int_batch<int256_t, int256_t, int128_t> },
      { nullptr, nullptr,},
      { &ObSumAggCell::eval_decimal_int_number_batch<int128_t, int128_t>,
        &ObSumAggCell::eval_decimal_int_number_batch<int256_t, int128_t> }
    },
    // int256_t
    {
      { &ObSumAggCell::eval_decimal_int_batch<int256_t, int256_t, int256_t>, nullptr },
      { &ObSumAggCell::eval_decimal_int_batch<int512_t, int256_t, int256_t>, nullptr },
      { &ObSumAggCell::eval_decimal_int_number_batch<int256_t, int256_t>, nullptr }
    }
  };

  const SqlTypeClass res_tc = sql_type_class(basic_info_.agg_expr_->datum_meta_.type_);
  const int16_t arg_prec = basic_info_.agg_expr_->args_[0]->datum_meta_.precision_;
  const int16_t res_prec = basic_info_.agg_expr_->datum_meta_.precision_;
  const int16_t buffer_prec = get_max_decimalint_precision(arg_prec) - arg_prec;
  const int arg_type = static_cast<int>(get_decimalint_type(arg_prec));
  const bool need_cast = (buffer_prec < MAX_PRECISION_DECIMAL_INT_64) &&
                           get_scale_factor<int64_t>(buffer_prec) < basic_info_.batch_size_;
  if (OB_UNLIKELY(arg_type > 3 || arg_type < 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (SqlTypeClass::ObNumberTC == res_tc) {
    eval_func_ = AGG_FUNCS[arg_type][2];
    eval_batch_func_ = AGG_BATCH_FUNCS[arg_type][2][need_cast];
    copy_datum_func_ = COPY_FUNCS[arg_type][2];
    eval_skip_index_func_ = &ObSumAggCell::eval_number;
  } else {
    const int res_type = static_cast<int>(get_decimalint_type(res_prec));
    int res_func_idx = 0;
    if (OB_UNLIKELY(res_prec < arg_prec || res_prec - arg_prec > OB_DECIMAL_LONGLONG_DIGITS)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      switch (arg_type) {
        case DECIMAL_INT_32:
        case DECIMAL_INT_128:
          res_func_idx = 0;
          break;
        case DECIMAL_INT_64:
          res_func_idx = res_type == DECIMAL_INT_128 ? 0 : 1;
          break;
        case DECIMAL_INT_256:
          res_func_idx = res_type == DECIMAL_INT_256 ? 0 : 1;
          break;
        default:
          ret = OB_ERR_UNEXPECTED;
      }
      if (OB_SUCC(ret)) {
        eval_func_ = AGG_FUNCS[arg_type][res_func_idx];
        eval_batch_func_ = AGG_BATCH_FUNCS[arg_type][res_func_idx][need_cast];
        copy_datum_func_ = COPY_FUNCS[arg_type][res_func_idx];
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(eval_func_) || OB_ISNULL(eval_batch_func_) || OB_ISNULL(copy_datum_func_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (res_prec > MAX_PRECISION_DECIMAL_INT_256) {
      // Native cells own the 64-byte coefficient. This buffer serves only
      // synchronous storage input conversion, independently of retained state.
      is_sum_use_temp_buf_ = true;
      if (OB_ISNULL(cast_temp_buffer_ = static_cast<char*>(allocator_.alloc(sizeof(int512_t))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        cast_datum_.ptr_ = cast_temp_buffer_;
      }

      if (OB_FAIL(ret)) {
        if (nullptr != cast_temp_buffer_) {
          allocator_.free(cast_temp_buffer_);
          cast_temp_buffer_ = nullptr;
        }
      }
    }
  }
  return ret;
}

int ObSumAggCell::eval(
    blocksstable::ObStorageDatum &datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(fill_default_if_need(datum))) {
  } else if (datum.is_null()) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < row_count; ++i) {
      if (OB_FAIL((this->*eval_func_)(datum, DEFAULT_DATUM_OFFSET))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    aggregated_ = true;
  }
  return ret;
}

int ObSumAggCell::eval_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == datums || count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL((this->*eval_batch_func_)(datums, count))) {
  } else {
    aggregated_ = true;
  }
  return ret;
}

int ObSumAggCell::eval_index_info(const blocksstable::ObMicroIndexInfo &index_info)
{
  int ret = OB_SUCCESS;
  if (!index_info.can_blockscan() || index_info.is_left_border() || index_info.is_right_border()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(skip_index_datum_.is_null() || skip_index_datum_is_prefix_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL((this->*eval_skip_index_func_)(skip_index_datum_, DEFAULT_DATUM_OFFSET))) {
  } else {
    aggregated_ = true;
  }
  return ret;
}

int ObSumAggCell::eval_batch_in_group_by(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    const int64_t distinct_cnt,
    const bool is_group_by_col,
    const bool is_default_datum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(nullptr == datums || count <= 0 || nullptr == refs || distinct_cnt <= 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const bool read_distinct_val = is_group_by_col || is_default_datum;
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      const uint32_t dictinct_datum_offset = is_default_datum ? 0 : refs[i];
      const common::ObDatum &datum = read_distinct_val ? datums[dictinct_datum_offset] : datums[i];
      if (datum.is_null()) {
      } else if (OB_FAIL((this->*eval_func_)(datum, refs[i]))) {
      }
    }
  }
  return ret;
}

bool ObSumAggCell::can_use_index_info() const
{
  bool res = false;
  res = nullptr != basic_info_.col_param_ && basic_info_.col_param_->get_meta_type().is_numeric_type();
  return res;
}

int ObSumAggCell::copy_output_rows(const int32_t start_offset, const int32_t end_offset)
{
  int ret = native_eval_ctx_ == nullptr ? OB_ERR_UNEXPECTED
      : import_native_inputs(*native_eval_ctx_, start_offset, end_offset);
  if (OB_FAIL(ret)) {
  } else if (start_offset < 0 || end_offset <= start_offset || end_offset > group_by_result_datum_buf_->get_basic_count()) { ret = OB_INVALID_ARGUMENT; }
  else {
    native_projection_offset_ = 0;
    for (int64_t i = start_offset; OB_SUCC(ret) && i < end_offset; ++i) {
      ObAggNativeCell &cell = group_by_result_datum_buf_->at(i);
      ObDatum converted;
      alignas(8) char scratch[64];
      converted.ptr_ = scratch;
      if (col_datums_[i].is_null()) { cell.set_null(); }
      else if (OB_FAIL((this->*copy_datum_func_)(col_datums_[i], converted))) {
      } else { ret = cell.capture_legacy(converted); }
    }
  }
  return ret;
}

int ObSumAggCell::copy_single_output_row(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (basic_info_.agg_expr_->args_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
  else {
    const sql::ObExpr &arg = *basic_info_.agg_expr_->args_[0];
    const Datum &native = arg.locate_expr_datum(ctx);
    ObDatum source, result;
    alignas(8) char input_buffer[64], output_buffer[64];
    result.ptr_ = output_buffer;
    if (OB_FAIL(arg.get_datum_copy_plan().legacy_view(native, source, input_buffer, sizeof(input_buffer)))) {
    } else if (source.is_null()) { result.set_null(); }
    else { ret = (this->*copy_datum_func_)(source, result); }
    if (OB_SUCC(ret)) { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, result); }
  }
  return ret;
}

int ObSumAggCell::copy_int_to_number(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  sql::ObNumStackAllocator<1> tmp_alloc;
  common::number::ObNumber nmb;
  if (OB_FAIL(nmb.from(datum.get_int(), tmp_alloc))) {
  } else {
    result_datum.set_number(nmb);
  }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::copy_int_to_decimal_int(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  if (datum.is_null()) {
    result_datum.set_null();
  } else {
    DATUM_TO_DECIMAL_INT(result_datum, RES_T) = datum.get_int();
    result_datum.pack_ = sizeof(RES_T);
  }
  return ret;
}

int ObSumAggCell::copy_uint_to_number(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  sql::ObNumStackAllocator<1> tmp_alloc;
  common::number::ObNumber nmb;
  if (OB_FAIL(nmb.from(datum.get_uint(), tmp_alloc))) {
  } else {
    result_datum.set_number(nmb);
  }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::copy_uint_to_decimal_int(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  DATUM_TO_DECIMAL_INT(result_datum, RES_T) = datum.get_uint();
  result_datum.pack_ = sizeof(RES_T);
  return ret;
}

int ObSumAggCell::copy_float(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  result_datum.set_float(datum.get_float());
  return ret;
}

int ObSumAggCell::copy_double(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  result_datum.set_double(datum.get_double());
  return ret;
}

int ObSumAggCell::copy_number(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  result_datum.set_number(datum.get_number());
  return ret;
}

int ObSumAggCell::copy_vector(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  result_datum.set_string(datum.get_string());
  return ret;
}

template<typename RES_T, typename ARG_T>
int ObSumAggCell::copy_decimal_int(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  DATUM_TO_DECIMAL_INT(result_datum, RES_T) = DATUM_TO_CONST_DECIMAL_INT(datum, ARG_T);
  result_datum.pack_ = sizeof(RES_T);
  return ret;
}

template<typename ARG_T>
int ObSumAggCell::copy_decimal_int_to_number(const ObDatum &datum, ObDatum &result_datum)
{
  int ret = OB_SUCCESS;
  sql::ObNumStackAllocator<2> tmp_alloc;
  number::ObNumber right_nmb;
  if (OB_FAIL(wide::to_number(DATUM_TO_CONST_DECIMAL_INT(datum, ARG_T),
                              child_scale(), tmp_alloc, right_nmb))) {
  } else {
    result_datum.set_number(right_nmb);
  }
  return ret;
}

int ObSumAggCell::collect_batch_result_in_group_by(const int64_t distinct_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(distinct_cnt > group_by_result_datum_buf_->get_capacity())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < distinct_cnt; ++i) {
      if (OB_FAIL(collect_result_in_group_by(i))) {
      }
    }
  }
  return ret;
}

int ObSumAggCell::collect_result(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  ObDatum accumulator;
  alignas(8) char accumulator_scratch[64];
  if (OB_FAIL(result_datum_.export_legacy(accumulator, accumulator_scratch, sizeof(accumulator_scratch)))) { return ret; }
  ObDatum result;
  alignas(8) char result_buffer[NATIVE_LEGACY_SCALAR_BYTES];
  result.ptr_ = result_buffer;
  sql::ObEvalInfo &eval_info = basic_info_.agg_expr_->get_eval_info(ctx);
  if (!sum_use_int_flag_) {
    return ObAggCell::collect_result(ctx);
  } else if (ObIntTC != obj_tc_ && ObUIntTC != obj_tc_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ob_is_decimal_int(basic_info_.agg_expr_->datum_meta_.type_)) {
    int128_t right_nmb = 0;
    if (ObIntTC == obj_tc_) {
      right_nmb = num_int_;
    } else {
      right_nmb = num_uint_;
    }
    if (OB_FAIL(collect_result_to_decimal_int(right_nmb, accumulator, result))) {
    } else {
      eval_info.evaluated_ = true;
    }
  } else {
    sql::ObNumStackAllocator<2> tmp_alloc;
    common::number::ObNumber right_nmb;
    const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
    if (ObIntTC == obj_tc_) {
      if (OB_FAIL(right_nmb.from(num_int_, tmp_alloc))) {
      }
    } else if (OB_FAIL(right_nmb.from(num_uint_, tmp_alloc))) {
    }
    if (OB_SUCC(ret)) {
      if (accumulator.is_null()) {
        result.set_number(right_nmb);
      } else {
        common::number::ObNumber left_nmb(accumulator.get_number());
        common::number::ObNumber result_nmb;
        if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, strict_mode))) {
        } else {
          result.set_number(result_nmb);
        }
      }
      eval_info.evaluated_ = true;
    }
  }
  if (OB_SUCC(ret)) { ret = publish_storage_result(*basic_info_.agg_expr_, ctx, result); }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::eval_int(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  char buf_alloc[common::number::ObNumber::MAX_CALC_BYTE_LEN];
  ObDataBuffer allocator(buf_alloc, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  if (OB_FAIL(eval_int_inner<RES_T>(datum, allocator, datum_offset))) {
  }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::eval_uint(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  char buf_alloc[common::number::ObNumber::MAX_CALC_BYTE_LEN];
  ObDataBuffer allocator(buf_alloc, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  if (OB_FAIL(eval_uint_inner<RES_T>(datum, allocator, datum_offset))) {
  }
  return ret;
}

int ObSumAggCell::eval_float(const common::ObDatum &datum, const int32_t datum_offset)
{
  return eval_float_inner(datum, datum_offset);
}

int ObSumAggCell::eval_double(const common::ObDatum &datum, const int32_t datum_offset)
{
  return eval_double_inner(datum, datum_offset);
}

int ObSumAggCell::eval_number(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = get_group_by_result_datum(datum_offset);
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  if (datum.is_null()) {
  } else if (result_datum.is_null()) {
    result_datum.set_number(datum.get_number());
  } else {
    common::number::ObNumber left_nmb(result_datum.get_number());
    common::number::ObNumber right_nmb(datum.get_number());
    char buf_alloc[common::number::ObNumber::MAX_CALC_BYTE_LEN];
    ObDataBuffer allocator(buf_alloc, common::number::ObNumber::MAX_CALC_BYTE_LEN);
    const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
    common::number::ObNumber result_nmb;
    if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, allocator, strict_mode))) {
    } else {
      result_datum.set_number(result_nmb);
    }
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

int ObSumAggCell::eval_collection(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = get_group_by_result_datum(datum_offset);
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  if (datum.is_null()) {
  } else if (result_datum.is_null()) {
    if (OB_FAIL(result_datum.deep_copy(datum, datum_allocator_))) {
    }
  } else {
    Datum native_result, native_input;
    const DatumCopyPlan plan = basic_info_.agg_expr_->get_datum_copy_plan();
    if (OB_FAIL(plan.import_legacy(result_datum, native_result))) {
    } else if (OB_FAIL(plan.import_legacy(datum, native_input))) {
    } else if (OB_FAIL(sql::ObArrayExprUtils::vector_datum_add(
        *exec_ctx_, native_result, native_input, datum_allocator_))) {
    } else { ret = plan.borrow_legacy(native_result, result_datum); }
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

int ObSumAggCell::init_eval_skip_index_func_for_decimal()
{
  int ret = OB_SUCCESS;
  const SqlTypeClass res_tc = sql_type_class(basic_info_.agg_expr_->datum_meta_.type_);
  const int16_t res_prec = basic_info_.agg_expr_->datum_meta_.precision_;
  const int res_type = static_cast<int>(get_decimalint_type(res_prec));
  switch (res_type) {
    case DECIMAL_INT_128:
      eval_skip_index_func_ = &ObSumAggCell::eval_number_decimal_int<int128_t>;
      break;
    case DECIMAL_INT_256:
      eval_skip_index_func_ = &ObSumAggCell::eval_number_decimal_int<int256_t>;
      break;
    case DECIMAL_INT_512:
      eval_skip_index_func_ = &ObSumAggCell::eval_number_decimal_int<int512_t>;
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::eval_number_decimal_int(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  //cast number to decimal
  int16_t out_scale = basic_info_.agg_expr_->datum_meta_.scale_;
  ObDecimalIntBuilder tmp_alloc;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  const number::ObNumber nmb(datum.get_number());
  const ObScale in_scale = nmb.get_scale();
  const ObPrecision out_prec = basic_info_.agg_expr_->datum_meta_.precision_;
  int32_t out_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
  if (OB_FAIL(wide::from_number(nmb, tmp_alloc, in_scale, decint, int_bytes))) {
  } else if (query::ObDecimalIntScale::is_needed(in_scale, int_bytes, out_scale, out_bytes)) {
    // upcasting
    ObDecimalIntBuilder res_val;
    if (OB_FAIL(query::ObDecimalIntScale::scale(
            decint, int_bytes, in_scale, out_scale, out_prec,
            basic_info_.agg_expr_->extra_, res_val))) {
    } else {
      cast_datum_.set_decimal_int(res_val.get_decimal_int(), res_val.get_int_bytes());
    }
  } else {
    cast_datum_.set_decimal_int(decint, int_bytes);
  }
  if (FAILEDx((eval_decimal_int<RES_T, RES_T>(cast_datum_, datum_offset)))) {
  }
  return ret;
}

template<typename RES_T, typename ARG_T>
int ObSumAggCell::eval_decimal_int(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = get_group_by_result_datum(datum_offset);
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  if (datum.is_null()) {
  } else if (result_datum.is_null()) {
    DATUM_TO_DECIMAL_INT(result_datum, RES_T) = DATUM_TO_CONST_DECIMAL_INT(datum, ARG_T);
    result_datum.pack_ = sizeof(RES_T);
  } else {
    DATUM_TO_DECIMAL_INT(result_datum, RES_T) += DATUM_TO_CONST_DECIMAL_INT(datum, ARG_T);
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

template<typename ARG_T>
int ObSumAggCell::eval_decimal_int_number(const common::ObDatum &datum, const int32_t datum_offset)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = get_group_by_result_datum(datum_offset);
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  if (!datum.is_null()) {
    sql::ObNumStackAllocator<2> tmp_alloc;
    number::ObNumber right_nmb;
    if (OB_FAIL(wide::to_number(DATUM_TO_CONST_DECIMAL_INT(datum, ARG_T),
                                child_scale(), tmp_alloc, right_nmb))) {
    } else if (result_datum.is_null()) {
      result_datum.set_number(right_nmb);
    } else {
      number::ObNumber left_nmb(result_datum.get_number());
      number::ObNumber result_nmb;
      if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, false))) {
      } else {
        result_datum.set_number(result_nmb);
      }
    }
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::eval_int_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  char buf_alloc[common::number::ObNumber::MAX_CALC_BYTE_LEN];
  ObDataBuffer allocator(buf_alloc, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(eval_int_inner<RES_T>(datums[i], allocator))) {
    }
  }
  return ret;
}

template<typename RES_T>
int ObSumAggCell::eval_uint_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  char buf_alloc[common::number::ObNumber::MAX_CALC_BYTE_LEN];
  ObDataBuffer allocator(buf_alloc, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(eval_uint_inner<RES_T>(datums[i], allocator))) {
    }
  }
  return ret;
}

int ObSumAggCell::eval_float_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(eval_float_inner(datums[i]))) {
    }
  }
  return ret;
}

int ObSumAggCell::eval_double_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(eval_double_inner(datums[i]))) {
    }
  }
  return ret;
}

int ObSumAggCell::eval_number_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = result_datum_;
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  common::number::ObNumber result_nmb;
  char buf_alloc1[common::number::ObNumber::MAX_CALC_BYTE_LEN];
  char buf_alloc2[common::number::ObNumber::MAX_CALC_BYTE_LEN];
  uint32_t sum_digits_buf[common::number::ObNumber::OB_CALC_BUFFER_SIZE];
  MEMSET(sum_digits_buf, 0, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  ObDataBuffer allocator1(buf_alloc1, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  ObDataBuffer allocator2(buf_alloc2, common::number::ObNumber::MAX_CALC_BYTE_LEN);
  bool all_skip = true;
  if (!result_datum.is_null()) {
      result_nmb.assign(result_datum.get_number_desc().desc_,
                        const_cast<uint32_t *>(result_datum.get_number_digits()));
  }
  ObAggSource src(datums);
  ObAggSelector selector(count);
  if (OB_FAIL(sql::number_accumulator(src, allocator1, allocator2, result_nmb,
                                      sum_digits_buf, all_skip, selector))) {
  } else if (!all_skip) {
    result_datum.set_number(result_nmb);
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

int ObSumAggCell::eval_collection_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    if (OB_FAIL(eval_collection(datums[i], -1))) {
    }
  }
  return ret;
}

template<typename RES_T, typename CALC_T, typename ARG_T>
int ObSumAggCell::eval_decimal_int_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = result_datum_;
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  CALC_T accum = 0;
  bool accumulated = false;
  for (int64_t i = 0; i < count; ++i) {
    if (datums[i].is_null()) {
      continue;
    }
    accum += DATUM_TO_CONST_DECIMAL_INT(datums[i], ARG_T);
    accumulated = true;
  }
  if (accumulated) {
    if (result_datum.is_null()) {
      DATUM_TO_DECIMAL_INT(result_datum, RES_T) = accum;
      result_datum.pack_ = sizeof(RES_T);
    } else {
      DATUM_TO_DECIMAL_INT(result_datum, RES_T) += accum;
    }
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

template<typename CALC_T, typename ARG_T>
int ObSumAggCell::eval_decimal_int_number_batch(const common::ObDatum *datums, const int64_t count)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = result_datum_;
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  CALC_T accum = 0;
  bool accumulated = false;
  for (int64_t i = 0; i < count; ++i) {
    if (datums[i].is_null()) {
      continue;
    }
    accum += DATUM_TO_CONST_DECIMAL_INT(datums[i], ARG_T);
    accumulated = true;
  }
  if (accumulated) {
    sql::ObNumStackAllocator<2> tmp_alloc;
    number::ObNumber right_nmb;
    if (OB_FAIL(wide::to_number(accum, child_scale(), tmp_alloc, right_nmb))) {
    } else if (result_datum.is_null()) {
      result_datum.set_number(right_nmb);
    } else {
      number::ObNumber left_nmb(result_datum.get_number());
      number::ObNumber result_nmb;
      if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, false))) {
      } else {
        result_datum.set_number(result_nmb);
      }
    }
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

int ObSumAggCell::collect_result_in_group_by(const int64_t datum_offset)
{
  int ret = OB_SUCCESS;
  ObAggNativeCell &native_result = get_group_by_result_datum(datum_offset);
  ObDatum result_datum;
  alignas(8) char native_scratch[64];
  if (OB_FAIL(native_result.edit_view(result_datum, native_scratch, sizeof(native_scratch)))) { return ret; }
  if (OB_UNLIKELY(datum_offset > group_by_result_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (SqlTypeClass::ObIntTC != obj_tc_ && SqlTypeClass::ObUIntTC != obj_tc_) {
  } else if (ob_is_decimal_int(basic_info_.agg_expr_->datum_meta_.type_)) {

    if (sum_use_int_flag_buf_->at(datum_offset)) {
      int128_t right_nmb = 0;
      if (ObIntTC == obj_tc_) {
        right_nmb = num_int_buf_->at(datum_offset);
      } else {
        right_nmb = num_uint_buf_->at(datum_offset);
      }
      if (OB_FAIL(collect_result_to_decimal_int(right_nmb, result_datum, result_datum))) {
      }
    }
  } else {

    if (sum_use_int_flag_buf_->at(datum_offset)) {
      sql::ObNumStackAllocator<2> tmp_alloc;
      common::number::ObNumber right_nmb;
      const bool strict_mode = false; //this is tmp allocator, so we can ues non-strinct mode
      if (ObIntTC == obj_tc_) {
        int64_t *num = nullptr;
        if (OB_FAIL(right_nmb.from(num_int_buf_->at(datum_offset), tmp_alloc))) {
        }
      } else {
        uint64_t *num = nullptr;
        if (OB_FAIL(right_nmb.from(num_uint_buf_->at(datum_offset), tmp_alloc))) {
        }
      }
      if (OB_SUCC(ret)) {
        if (result_datum.is_null()) {
          result_datum.set_number(right_nmb);
        } else {
          common::number::ObNumber left_nmb(result_datum.get_number());
          common::number::ObNumber result_nmb;
          if (OB_FAIL(left_nmb.add_v3(right_nmb, result_nmb, tmp_alloc, strict_mode))) {
          } else {
            result_datum.set_number(result_nmb);
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) { ret = native_result.commit_view(result_datum); }
  return ret;
}

int ObSumAggCell::reserve_group_by_buf(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(group_by_result_datum_buf_->reserve(size))) {
  } else if (nullptr != sum_use_int_flag_buf_ && OB_FAIL(sum_use_int_flag_buf_->reserve(size))) {
  } else if (nullptr != num_int_buf_ && OB_FAIL(num_int_buf_->reserve(size))) {
  } else {
    ObAggCell::clear_group_by_info();
    clear_group_by_info();
  }
  return ret;
}

int ObSumAggCell::output_extra_group_by_result(const int64_t start, const int64_t count)
{
  return ObAggCell::output_extra_group_by_result(start, count);
}

int ObSumAggCell::collect_result_to_decimal_int(
    const int128_t &right_nmb,
    const common::ObDatum &datum,
    common::ObDatum &result)
{
  int ret = OB_SUCCESS;
  if (basic_info_.agg_expr_->datum_meta_.precision_ <= MAX_PRECISION_DECIMAL_INT_128) {
    if (datum.is_null()) {
      DATUM_TO_DECIMAL_INT(result, int128_t) = right_nmb;
    } else {
      DATUM_TO_DECIMAL_INT(result, int128_t) =
        DATUM_TO_CONST_DECIMAL_INT(datum, int128_t) + right_nmb;
    }
    result.pack_ = sizeof(int128_t);
  } else { // int256;
    if (datum.is_null()) {
      DATUM_TO_DECIMAL_INT(result, int256_t) = right_nmb;
    } else {
      DATUM_TO_DECIMAL_INT(result, int256_t) =
        DATUM_TO_CONST_DECIMAL_INT(datum, int256_t) + right_nmb;
    }
    result.pack_ = sizeof(int256_t);
  }
  return ret;
}

ObFirstRowAggCell::ObFirstRowAggCell(const ObAggCellBasicInfo &basic_info, common::ObIAllocator &allocator)
    : ObAggCell(basic_info, allocator),
      is_determined_value_(false),
      aggregated_flag_cnt_(0),
      aggregated_flag_buf_(),
      datum_allocator_("ObStorageAgg", OB_MALLOC_NORMAL_BLOCK_SIZE)
{
  agg_type_ = ObPDAggType::PD_FIRST_ROW;
}

void ObFirstRowAggCell::reset()
{
  is_determined_value_ = false;
  aggregated_flag_cnt_ = 0;
  free_group_by_buf(allocator_, aggregated_flag_buf_);
  if (nullptr != agg_datum_buf_) {
    agg_datum_buf_->reset();
    allocator_.free(agg_datum_buf_);
    agg_datum_buf_ = nullptr;
  }
  datum_allocator_.reset();
  ObAggCell::reset();
}

void ObFirstRowAggCell::reuse()
{
  ObAggCell::reuse();
  aggregated_flag_cnt_ = 0;
  clear_group_by_info();
  datum_allocator_.reuse();
  if (is_determined_value_) {
    set_determined_value();
  }
}

void ObFirstRowAggCell::clear_group_by_info()
{
  if (nullptr != aggregated_flag_buf_) {
    aggregated_flag_buf_->fill_items(false);
  }
}

int ObFirstRowAggCell::init(const bool is_group_by, sql::ObEvalCtx *eval_ctx)
{
  int ret = ObAggCell::init(is_group_by, eval_ctx);
  if (OB_SUCC(ret) && is_group_by) {
    if (OB_FAIL(new_group_by_buf((bool *)nullptr, 0, sizeof(bool), allocator_, aggregated_flag_buf_))) {
    } else { ret = aggregated_flag_buf_->reserve(basic_info_.batch_size_); }
  }
  return ret;
}

int ObFirstRowAggCell::eval(
    blocksstable::ObStorageDatum &datum,
    const int64_t row_count,
    const int64_t agg_row_idx)
{
  UNUSED(agg_row_idx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(row_count < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (!aggregated_) {
    if (OB_FAIL(fill_default_if_need(datum))) {
    } else if (OB_FAIL(pad_column_if_need(datum, padding_allocator_))) {
    } else if (OB_FAIL(result_datum_.capture_legacy(datum))) {
    } else {
      aggregated_ = true;
    }
  }
  return ret;
}

int ObFirstRowAggCell::eval_micro_block(
    const ObTableIterParam &iter_param,
    const ObTableAccessContext &context,
    const int32_t col_offset,
    blocksstable::ObIMicroBlockReader *reader,
    const int32_t *row_ids,
    const int64_t row_count)
{
  int ret = OB_SUCCESS;
  if (!aggregated_) {
    if (OB_UNLIKELY(nullptr == row_ids || 0 == row_count || nullptr == basic_info_.col_param_)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      blocksstable::ObStorageDatum datum;
      if (OB_FAIL(reader->get_column_datum(iter_param, context, *basic_info_.col_param_, col_offset, row_ids[0], datum))) {
      } else if (OB_FAIL(fill_default_if_need(datum))) {
      } else if (OB_FAIL(pad_column_if_need(datum, padding_allocator_))) {
      } else if (OB_FAIL(result_datum_.capture_legacy(datum))) {
      } else {
        aggregated_ = true;
      }
    }
  }
  return ret;
}

int ObFirstRowAggCell::eval_index_info(const blocksstable::ObMicroIndexInfo &index_info)
{
  UNUSED(index_info);
  int ret = OB_SUCCESS;
  if (!aggregated_) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObFirstRowAggCell::eval_batch_in_group_by(
    const common::ObDatum *datums,
    const int64_t count,
    const uint32_t *refs,
    const int64_t distinct_cnt,
    const bool is_group_by_col,
    const bool is_default_datum)
{
  if (datums == nullptr || refs == nullptr || count < 0 || count > basic_info_.batch_size_
      || distinct_cnt > group_by_result_datum_buf_->get_capacity()) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
    const uint32_t group = refs[i];
    if (group >= distinct_cnt) { ret = OB_INVALID_ARGUMENT; }
    else if (!aggregated_flag_buf_->at(group)) {
      const ObDatum &input = datums[is_default_datum ? 0 : is_group_by_col ? group : i];
      if (input.is_null()) {
      } else if (OB_FAIL(group_by_result_datum_buf_->at(group).capture_legacy(input))) {
      } else { aggregated_flag_buf_->at(group) = true; ++aggregated_flag_cnt_; }
    }
  }
  if (OB_SUCC(ret) && aggregated_flag_cnt_ == distinct_cnt) { aggregated_ = true; }
  return ret;
}

int ObFirstRowAggCell::collect_result(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (!aggregated_) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObAggCell::collect_result(ctx))) {
  }
  return ret;
}

int ObFirstRowAggCell::collect_batch_result_in_group_by(const int64_t distinct_cnt)
{
  return distinct_cnt < 0 || distinct_cnt > group_by_result_datum_buf_->get_capacity()
      ? OB_INVALID_ARGUMENT : OB_SUCCESS;
}

int ObFirstRowAggCell::reserve_group_by_buf(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(group_by_result_datum_buf_->reserve(size))) {
  } else if (nullptr != aggregated_flag_buf_ && OB_FAIL(aggregated_flag_buf_->reserve(size))) {
  } else {
    ObAggCell::clear_group_by_info();
    clear_group_by_info();
    aggregated_flag_cnt_ = 0;
  }
  return ret;
}

int ObFirstRowAggCell::output_extra_group_by_result(const int64_t start, const int64_t count)
{
  return ObAggCell::output_extra_group_by_result(start, count);
}

int ObPDAggFactory::alloc_cell(
    const ObAggCellBasicInfo &basic_info,
    common::ObIArray<ObAggCell*> &agg_cells,
    const bool exclude_null,
    const bool is_group_by,
    sql::ObEvalCtx *eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!basic_info.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    void *buf = nullptr;
    ObAggCell *cell = nullptr;
    switch (basic_info.agg_expr_->type_) {
      case T_REF_COLUMN:
      case T_ORA_ROWSCN: {
        if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObFirstRowAggCell))) ||
            OB_ISNULL(cell = new(buf) ObFirstRowAggCell(basic_info, allocator_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        break;
      }
      case T_FUN_COUNT: {
        if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObCountAggCell))) ||
            OB_ISNULL(cell = new(buf) ObCountAggCell(basic_info, allocator_, exclude_null))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        break;
      }
      case T_FUN_INNER_PREFIX_MIN:
      case T_FUN_MIN: {
        const ObDatumCmpFuncType cmp_fun = sql::storage_predicate_functions(*basic_info.agg_expr_)->null_first_cmp_;
        if (OB_ISNULL(cmp_fun)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObMinAggCell))) ||
            OB_ISNULL(cell = new(buf) ObMinAggCell(basic_info, allocator_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        break;
      }
      case T_FUN_INNER_PREFIX_MAX:
      case T_FUN_MAX: {
        const ObDatumCmpFuncType cmp_fun = sql::storage_predicate_functions(*basic_info.agg_expr_)->null_first_cmp_;
        if (OB_ISNULL(cmp_fun)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObMaxAggCell))) ||
            OB_ISNULL(cell = new(buf) ObMaxAggCell(basic_info, allocator_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        break;
      }
      case T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS: {
        if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObHyperLogLogAggCell))) ||
            OB_ISNULL(cell = new(buf) ObHyperLogLogAggCell(basic_info, allocator_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        break;
      }
      case T_FUN_SUM_OPNSIZE: {
        if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObSumOpSizeAggCell))) ||
            OB_ISNULL(cell = new(buf) ObSumOpSizeAggCell(basic_info, allocator_, exclude_null))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
         }
         break;
       }
      case T_FUN_SUM: {
        if (OB_ISNULL(buf = allocator_.alloc(sizeof(ObSumAggCell))) ||
            OB_ISNULL(cell = new(buf) ObSumAggCell(basic_info, allocator_))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        }
        break;
      }
      default: {
        ret = OB_NOT_SUPPORTED;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(cell->init(is_group_by, eval_ctx))) {
    } else if (OB_FAIL(agg_cells.push_back(cell))) {
    }
    if (OB_FAIL(ret) && cell != nullptr) { cell->reset(); allocator_.free(cell); }
  }
  return ret;
}

void ObPDAggFactory::release(common::ObIArray<ObAggCell*> &agg_cells)
{
  for (int64_t i = 0; i < agg_cells.count(); ++i) {
    if (OB_NOT_NULL(agg_cells.at(i))) {
      agg_cells.at(i)->reset();
      allocator_.free(agg_cells.at(i));
    }
  }
  agg_cells.reset();
}

ObGroupByCell::ObGroupByCell(const int64_t batch_size, common::ObIAllocator &allocator)
  : ObGroupByCellBase(batch_size, allocator),
    group_by_col_datum_buf_(nullptr), legacy_group_owner_(nullptr),
    native_group_keys_(allocator), native_dictionary_keys_(allocator),
    native_key_projection_(0), key_input_captured_(false), native_eval_ctx_(nullptr),
    tmp_group_by_datum_buf_(nullptr),
    agg_datum_buf_(nullptr),
    agg_cell_factory_(allocator)
{
}

void ObGroupByCell::reset()
{
  native_group_keys_.reset(); native_dictionary_keys_.reset();
  native_key_projection_ = 0; key_input_captured_ = false;
  ObGroupByCellBase::reset();
  agg_cell_factory_.release(agg_cells_);
  free_group_by_buf(allocator_, group_by_col_datum_buf_);
  if (legacy_group_owner_ != nullptr) {
    legacy_group_owner_->~ObAggDatumBuf(); allocator_.free(legacy_group_owner_); legacy_group_owner_ = nullptr;
  }
  free_group_by_buf(allocator_, tmp_group_by_datum_buf_);
  if (nullptr != agg_datum_buf_) {
    agg_datum_buf_->reset();
    allocator_.free(agg_datum_buf_);
    agg_datum_buf_ = nullptr;
  }
}

void ObGroupByCell::reuse()
{
  release_group_key_views();
  native_group_keys_.reuse(); native_dictionary_keys_.reuse();
  native_key_projection_ = 0; key_input_captured_ = false;
  ObGroupByCellBase::reuse();
  for (int64_t i = 0; i < agg_cells_.count(); ++i) {
    agg_cells_.at(i)->reuse();
  }
}

int ObGroupByCell::init(const ObTableAccessParam &param, const ObTableAccessContext &context, sql::ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  native_eval_ctx_ = &eval_ctx;
  const common::ObIArray<share::schema::ObColumnParam *> *out_cols_param = param.iter_param_.get_col_params();
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(nullptr == param.iter_param_.group_by_cols_project_ ||
                  0 == param.iter_param_.group_by_cols_project_->count() ||
                  nullptr == out_cols_param)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    common::ObDatum *group_by_col_datums = nullptr;
    group_by_col_offset_ = param.iter_param_.group_by_cols_project_->at(0);
    for (int64_t i = 0; OB_SUCC(ret) && i < param.output_exprs_->count(); ++i) {
      if (T_PSEUDO_GROUP_ID == param.output_exprs_->at(i)->type_) {
        continue;
      } else if (nullptr == param.output_sel_mask_ || param.output_sel_mask_->at(i)) {
        int32_t col_offset = param.iter_param_.out_cols_project_->at(i);
        int32_t col_index = param.iter_param_.read_info_->get_columns_index().at(col_offset);
        const share::schema::ObColumnParam *col_param = out_cols_param->at(col_offset);
        sql::ObExpr *expr = param.output_exprs_->at(i);
        ObAggCellBasicInfo basic_info(col_offset, col_index, col_param, expr,
                                      batch_size_, is_pad_char_to_full_length(context.sql_mode_));
        if (group_by_col_offset_ == col_offset) {
          ret = ObAggDatumBuf::new_agg_datum_buf(batch_size_, false, allocator_, legacy_group_owner_, NATIVE_LEGACY_SCALAR_BYTES);
          group_by_col_datums = OB_SUCC(ret) ? legacy_group_owner_->get_datums() : nullptr;
          if (OB_FAIL(ret)) {
          } else if (OB_ISNULL(group_by_col_datums)) {
            ret = OB_ERR_UNEXPECTED;
            STORAGE_LOG(WARN, "Unexpected null datums", K(ret), K(i), KPC(expr));
          } else if (OB_FAIL(new_group_by_buf(group_by_col_datums, batch_size_,
              NATIVE_LEGACY_SCALAR_BYTES, allocator_, group_by_col_datum_buf_))) {
          } else {
            const common::SqlTypeMeta &obj_meta = col_param->get_meta_type();
            if (is_pad_char_to_full_length(context.sql_mode_) && obj_meta.is_fixed_len_char_type()) {
              group_by_col_param_ = col_param;
            }
            group_by_col_expr_ = expr;
            native_group_keys_.configure(&expr->get_sql_type(), ObAggNativeDomain::Semantic);
            native_dictionary_keys_.configure(&expr->get_sql_type(), ObAggNativeDomain::Semantic);
            ret = native_group_keys_.init(batch_size_, &expr->get_sql_type());
          }
        } else if (OB_FAIL(agg_cell_factory_.alloc_cell(basic_info, agg_cells_, false, true, &eval_ctx))) {
        }
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_ISNULL(group_by_col_datums)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(init_agg_cells(param, context, eval_ctx, false))) {
    } else {
      if (agg_cells_.count() > 2) {
        lib::ob_sort(agg_cells_.begin(), agg_cells_.end(),
                  [](ObAggCell *a, ObAggCell *b) { return a->get_col_offset() < b->get_col_offset(); });
      }
      void *buf = nullptr;
      if (OB_ISNULL(buf = allocator_.alloc(sizeof(uint32_t) * batch_size_))) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else {
        refs_buf_ = reinterpret_cast<uint32_t*>(buf);
        is_inited_ = true;
      }
    }
  }
  return ret;
}

int ObGroupByCell::init_for_single_row(const ObTableAccessParam &param, const ObTableAccessContext &context, sql::ObEvalCtx &eval_ctx)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
  } else if (OB_UNLIKELY(nullptr == param.iter_param_.group_by_cols_project_ ||
                  0 == param.iter_param_.group_by_cols_project_->count() ||
                  nullptr ==  param.iter_param_.get_col_params())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(init_agg_cells(param, context, eval_ctx, true))) {
  } else {
    is_inited_ = true;
  }
  return ret;
}

int ObGroupByCell::eval_batch(
    common::ObDatum *datums,
    const int64_t count,
    const int32_t agg_idx,
    const bool is_group_by_col,
    const bool is_default_datum,
    const uint32_t ref_offset)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_UNLIKELY(agg_idx >= agg_cells_.count())) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(0 == distinct_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (agg_cells_.at(agg_idx)->finished()) {
  } else if (OB_FAIL(agg_cells_.at(agg_idx)->eval_batch_in_group_by(
      datums, count, refs_buf_ + ref_offset, distinct_cnt_, is_group_by_col, is_default_datum))) {
  } else {
    agg_cells_.at(agg_idx)->set_group_by_result_cnt(distinct_cnt_);
  }
  return ret;
}

int ObGroupByCell::capture_group_key_input()
{
  if (!is_inited_ || group_by_col_expr_ == nullptr || distinct_cnt_ < 0
      || distinct_cnt_ > group_by_col_datum_buf_->get_capacity()) { return OB_INVALID_ARGUMENT; }
  ObNativeAggDatumBuf candidate(allocator_);
  int ret = OB_SUCCESS;
  if (distinct_cnt_ > 0) {
    ret = candidate.init(distinct_cnt_, &group_by_col_expr_->get_sql_type());
  } else { candidate.configure(&group_by_col_expr_->get_sql_type(), ObAggNativeDomain::Semantic); }
  ObDatum *input = get_group_by_col_datums_to_fill();
  for (int64_t i = 0; OB_SUCC(ret) && i < distinct_cnt_; ++i) { ret = candidate.at(i).capture_legacy(input[i]); }
  // References in these synchronous decoder views point at candidate payload.
  // Moving its directory into the owner does not move those payload spans.
  ObAggGroupByDatumBuf *views = need_extract_distinct_ ? tmp_group_by_datum_buf_ : group_by_col_datum_buf_;
  for (int64_t i = 0; OB_SUCC(ret) && i < distinct_cnt_; ++i) {
    char *scratch = views->is_use_extra_buf() ? views->get_view_scratch(i)
        : (need_extract_distinct_ ? agg_datum_buf_->get_view_scratch(i) : legacy_group_owner_->get_view_scratch(i));
    ret = candidate.at(i).export_legacy(input[i], scratch, NATIVE_LEGACY_SCALAR_BYTES);
  }
  if (OB_SUCC(ret)) {
    if (need_extract_distinct_) { native_dictionary_keys_.swap(candidate); }
    else { native_group_keys_.swap(candidate); }
    key_input_captured_ = true;
    native_key_projection_ = 0;
  }
  return ret;
}

void ObGroupByCell::release_group_key_views()
{
  for (ObAggGroupByDatumBuf *views : {group_by_col_datum_buf_, tmp_group_by_datum_buf_}) {
    if (views == nullptr) { continue; }
    ObDatum *basic = views->get_sql_result_datums();
    for (int64_t i = 0; basic != nullptr && i < batch_size_; ++i) { basic[i].set_null(); }
    ObDatum *extra = views->get_extra_result_datums();
    for (int64_t i = 0; extra != nullptr && i < views->get_extra_buf_size(); ++i) { extra[i].set_null(); }
  }
  for (int64_t i = 0; i < agg_cells_.count(); ++i) { agg_cells_.at(i)->release_input_views(); }
}

int ObGroupByCell::import_native_keys(int64_t begin, int64_t end)
{
  if (native_eval_ctx_ == nullptr || group_by_col_expr_ == nullptr || begin < 0 || end < begin || end > batch_size_) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  if (OB_FAIL(native_group_keys_.reserve(std::max<int64_t>(end, 1)))) { return ret; }
  for (int64_t row = begin; OB_SUCC(ret) && row < end; ++row) {
    ret = native_group_keys_.at(row).capture_native(group_by_col_expr_->locate_expr_datum(*native_eval_ctx_, row));
  }
  if (OB_SUCC(ret)) { key_input_captured_ = true; native_key_projection_ = 0; }
  return ret;
}

int ObGroupByCell::copy_output_row(const int64_t batch_idx, const ObTableIterParam &iter_param)
{
  UNUSED(iter_param);
  int ret = OB_SUCCESS;
  ret = import_native_keys(batch_idx - 1, batch_idx);
  // just shallow copy output datum to agg
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) {
    agg_cells_.at(i)->set_group_by_result_cnt(batch_idx);
    if (OB_FAIL(agg_cells_.at(i)->copy_output_rows(batch_idx - 1, batch_idx))) {
    }
  }
  if (OB_SUCC(ret)) {
    set_distinct_cnt(batch_idx);
  }
  return ret;
}

int ObGroupByCell::copy_output_rows(const int64_t batch_idx, const ObTableIterParam &iter_param)
{
  UNUSED(iter_param);
  int ret = OB_SUCCESS;
  ret = import_native_keys(0, batch_idx);
  // just shallow copy output datums to agg
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) {
    agg_cells_.at(i)->set_group_by_result_cnt(batch_idx);
    if (OB_FAIL(agg_cells_.at(i)->copy_output_rows(0, batch_idx))) {
    }
  }
  if (OB_SUCC(ret)) {
    set_distinct_cnt(batch_idx);
  }
  return ret;
}

int ObGroupByCell::copy_single_output_row(sql::ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  // just shallow copy output datum to agg
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) {
    if (OB_FAIL(agg_cells_.at(i)->copy_single_output_row(ctx))) {
    }
  }
  return ret;
}

int ObGroupByCell::collect_result()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) {
    ret = agg_cells_.at(i)->collect_batch_result_in_group_by(distinct_cnt_);
  }
  return ret;
}

int ObGroupByCell::add_distinct_null_value()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (distinct_cnt_ + 1 > group_by_col_datum_buf_->get_capacity()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    common::ObDatum *datums = get_group_by_col_datums_to_fill();
    datums[distinct_cnt_].set_null();
    distinct_cnt_++;
  }
  return ret;
}

int ObGroupByCell::prepare_tmp_group_by_buf(const int64_t size)
{
  UNUSED(size);
  int ret = OB_SUCCESS;
  void *buf = nullptr;
  if (OB_ISNULL(distinct_projector_buf_)) {
    if (OB_FAIL(new_group_by_buf((int16_t*)nullptr, 0, sizeof(int16_t), allocator_, distinct_projector_buf_))) {
    } else if (OB_FAIL(distinct_projector_buf_->reserve(batch_size_))) {
    } else if (OB_FAIL(ObAggDatumBuf::new_agg_datum_buf(batch_size_, false, allocator_, agg_datum_buf_, NATIVE_LEGACY_SCALAR_BYTES))) {
    } else if (OB_FAIL(new_group_by_buf(agg_datum_buf_->get_datums(), batch_size_,
        NATIVE_LEGACY_SCALAR_BYTES, allocator_, tmp_group_by_datum_buf_))) {
    } else {
      distinct_projector_buf_->fill_items(-1);
    }
  }
  if (OB_SUCC(ret)) {
    need_extract_distinct_ = true;
  }
  return ret;
}

int ObGroupByCell::reserve_group_by_buf(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(native_group_keys_.reserve(size))) {
  } else if (OB_FAIL(group_by_col_datum_buf_->reserve(size))) {
  } else if (nullptr != distinct_projector_buf_) {
    if (OB_FAIL(distinct_projector_buf_->reserve(size))) {
    } else if (FALSE_IT(distinct_projector_buf_->fill_items(-1))) {
    } else if (OB_FAIL(tmp_group_by_datum_buf_->reserve(size))) {
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) {
    if (OB_FAIL(agg_cells_.at(i)->reserve_group_by_buf(size))) {
    }
  }
  return ret;
}

int ObGroupByCell::output_extra_group_by_result(int64_t &count, const ObTableIterParam &iter_param)
{
  UNUSED(iter_param);
  count = 0;
  if (!is_inited_) { return OB_NOT_INIT; }
  if (!group_by_col_datum_buf_->is_use_extra_buf() || (projected_cnt_ == 0 && row_capacity_ != batch_size_)) { return OB_ERR_UNEXPECTED; }
  if (projected_cnt_ >= distinct_cnt_) { return OB_ITER_END; }
  count = std::min(row_capacity_, distinct_cnt_ - projected_cnt_);
  int ret = OB_SUCCESS;
  if (projected_cnt_ >= batch_size_) {
    for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) { ret = agg_cells_.at(i)->output_extra_group_by_result(projected_cnt_, count); }
  }
  if (OB_SUCC(ret)) {
    native_key_projection_ = projected_cnt_;
    projected_cnt_ += count;
    if (projected_cnt_ >= distinct_cnt_) { ret = OB_ITER_END; }
  }
  return ret;
}

int ObGroupByCell::publish_native_rows(sql::ObEvalCtx &ctx, int64_t count)
{
  if (count < 0 || native_key_projection_ > native_group_keys_.get_capacity() - count) { return OB_INVALID_ARGUMENT; }
  int ret = OB_SUCCESS;
  for (int64_t row = 0; OB_SUCC(ret) && row < count; ++row) {
    ObDatum view;
    alignas(8) char scratch[64];
    if (OB_FAIL(native_group_keys_.at(native_key_projection_ + row).export_legacy(view, scratch, sizeof(scratch)))) {
    } else { ret = publish_storage_result(*group_by_col_expr_, ctx, view, row); }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) { ret = agg_cells_.at(i)->publish_native_rows(ctx, count); }
  return ret;
}

int ObGroupByCell::pad_column_in_group_by(const int64_t row_cap)
{
  if (!is_inited_) { return OB_NOT_INIT; }
  int ret = OB_SUCCESS;
  if (group_by_col_param_ != nullptr && group_by_col_param_->get_meta_type().is_fixed_len_char_type()) {
    for (int64_t row = 0; OB_SUCC(ret) && row < row_cap; ++row) {
      ObDatum view;
      alignas(8) char scratch[64];
      ObAggNativeCell &cell = native_group_keys_.at(native_key_projection_ + row);
      ObDatum *view_ptr = &view;
      if (OB_FAIL(cell.export_legacy(view, scratch, sizeof(scratch)))) {
      } else if (OB_FAIL(storage::pad_on_datums(group_by_col_param_->get_accuracy(),
          group_by_col_param_->get_meta_type().get_collation_type(), padding_allocator_, 1, view_ptr))) {
      } else { ret = cell.capture_legacy(*view_ptr); }
    }
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) { ret = agg_cells_.at(i)->pad_column_in_group_by(row_cap, padding_allocator_); }
  return ret;
}

int ObGroupByCell::extract_distinct()
{
  if (!is_inited_) { return OB_NOT_INIT; }
  if (ref_cnt_ <= 0 || !key_input_captured_) { return OB_ERR_UNEXPECTED; }
  int ret = OB_SUCCESS;
  ObDatum *views = group_by_col_datum_buf_->get_group_by_datums();
  for (int64_t i = 0; OB_SUCC(ret) && i < ref_cnt_; ++i) {
    uint32_t &ref = refs_buf_[i];
    if (ref >= native_dictionary_keys_.get_capacity()) { ret = OB_ERR_UNEXPECTED; }
    else {
      int16_t &projected = distinct_projector_buf_->at(ref);
      if (projected == -1) {
        if (OB_FAIL(native_group_keys_.at(distinct_cnt_).capture_native(native_dictionary_keys_.at(ref).native_value()))) {
        } else if (OB_FAIL(native_group_keys_.at(distinct_cnt_).export_legacy(views[distinct_cnt_],
            group_by_col_datum_buf_->is_use_extra_buf() ? group_by_col_datum_buf_->get_view_scratch(distinct_cnt_)
                : legacy_group_owner_->get_view_scratch(distinct_cnt_), 64))) {
        } else { projected = distinct_cnt_; ref = distinct_cnt_++; }
      } else { ref = projected; }
    }
  }
  return ret;
}

int ObGroupByCell::assign_agg_cells(const sql::ObExpr *col_expr, common::ObIArray<int32_t> &agg_idxs)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else 
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_cells_.count(); ++i) {
    ObAggCell *agg_cell = agg_cells_.at(i);
    if (agg_cell->is_assigned_to_group_by_processor()) {
    } else if ((ObPDAggType::PD_FIRST_ROW == agg_cell->get_type() && col_expr == agg_cell->get_agg_expr()) ||
        (ObPDAggType::PD_COUNT == agg_cell->get_type() && !agg_cell->need_access_data()) ||
        (ObPDAggType::PD_FIRST_ROW != agg_cell->get_type() &&col_expr == agg_cell->get_agg_expr()->args_[0])) {
      if (OB_FAIL(agg_idxs.push_back(i))) {
      } else {
        agg_cell->set_assigned_to_group_by_processor();
      }
    }
  }
  return ret;
}

int ObGroupByCell::init_agg_cells(const ObTableAccessParam &param, const ObTableAccessContext &context, sql::ObEvalCtx &eval_ctx, const bool is_for_single_row)
{
  int ret = OB_SUCCESS;
  const common::ObIArray<share::schema::ObColumnParam *> *out_cols_param = param.iter_param_.get_col_params();
  for (int64_t i = 0; OB_SUCC(ret) && i < param.aggregate_exprs_->count(); ++i) {
    int32_t col_offset = param.iter_param_.agg_cols_project_->at(i);
    int32_t col_index = OB_COUNT_AGG_PD_COLUMN_ID == col_offset ? -1 : param.iter_param_.read_info_->get_columns_index().at(col_offset);
    const share::schema::ObColumnParam *col_param = OB_COUNT_AGG_PD_COLUMN_ID == col_offset ? nullptr : out_cols_param->at(col_offset);
    sql::ObExpr *agg_expr = param.aggregate_exprs_->at(i);
    bool exclude_null = false;
    if (T_FUN_COUNT == agg_expr->type_) {
      if (OB_COUNT_AGG_PD_COLUMN_ID != col_offset) {
        exclude_null = col_param->is_nullable_for_write();
      }
    }
    ObAggCellBasicInfo basic_info(col_offset, col_index, col_param, agg_expr,
                                  batch_size_, is_pad_char_to_full_length(context.sql_mode_));
    if (OB_FAIL(agg_cell_factory_.alloc_cell(basic_info, agg_cells_, exclude_null, !is_for_single_row, &eval_ctx))) {
    }
  }
  return ret;
}

int64_t ObGroupByCell::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_OBJ_START();
  J_KV(K_(batch_size),
       K_(row_capacity),
       K_(group_by_col_offset),
       KP_(group_by_col_expr),
       K_(agg_cells),
       K_(distinct_cnt),
       K_(ref_cnt),
       K_(is_processing),
       K_(projected_cnt),
       KPC_(group_by_col_datum_buf));
  J_COMMA();
  J_KV(K(ObArrayWrap<uint32_t>(refs_buf_, ref_cnt_)));
  J_OBJ_END();
  return pos;
}

}
}
