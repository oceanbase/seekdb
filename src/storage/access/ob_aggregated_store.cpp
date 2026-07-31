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
#include "ob_aggregated_store.h"
#include "storage/access/ob_pushdown_aggregate_input.h"
#include "storage/blocksstable/ob_micro_block_row_scanner.h"
namespace oceanbase
{
namespace storage
{

ObAggRow::ObAggRow(common::ObIAllocator &allocator) :
    agg_cells_(allocator),
    dummy_agg_cells_(allocator),
    can_use_index_info_(false),
    need_access_data_(false),
    has_lob_column_out_(false),
    allocator_(allocator),
    agg_cell_factory_(allocator)
{
}

ObAggRow::~ObAggRow()
{
  reset();
}

void ObAggRow::reset()
{
  agg_cell_factory_.release(agg_cells_);
  agg_cells_.reset();
  agg_cell_factory_.release(dummy_agg_cells_);
  dummy_agg_cells_.reset();
  can_use_index_info_ = false;
  need_access_data_ = false;
  has_lob_column_out_ = false;
}

void ObAggRow::reuse()
{
  for (int i = 0; i < agg_cells_.count(); ++i) {
    if (agg_cells_.at(i)) {
      agg_cells_.at(i)->reuse();
    }
  }
}

int ObAggRow::init(const ObTableAccessParam &param, const ObTableAccessContext &context, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  const common::ObIArray<share::schema::ObColumnParam *> *out_cols_param = param.iter_param_.get_col_params();
  if (OB_ISNULL(out_cols_param)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null out cols param", K(ret), K_(param.iter_param));
  } else if (OB_FAIL(agg_cells_.init(param.aggregate_exprs_->count()))) {
    LOG_WARN("Failed to init agg cells array", K(ret), K(param.aggregate_exprs_->count()));
  } else if (OB_FAIL(dummy_agg_cells_.init(param.output_exprs_->count()))) {
    LOG_WARN("Failed to init first row agg cells array", K(ret), K(param.output_exprs_->count()));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < param.output_exprs_->count(); ++i) {
      // mysql compatibility, select a,count(a), output the first value of a
      // from 4.3, this non-standard scalar group by will not pushdown to storage
      // so we can just set an determined value to output_exprs_ as it's never be used
      if (T_PSEUDO_GROUP_ID == param.output_exprs_->at(i)->type_) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("Unexpected group idx expr", K(ret));
      } else if (nullptr == param.output_sel_mask_ || param.output_sel_mask_->at(i)) {
        ObAggCell *cell = nullptr;
        int32_t col_offset = param.iter_param_.out_cols_project_->at(i);
        int32_t col_index = param.iter_param_.read_info_->get_columns_index().at(col_offset);
        const share::schema::ObColumnParam *col_param = out_cols_param->at(col_offset);
        sql::ObExpr *expr = param.output_exprs_->at(i);
        ObAggCellBasicInfo basic_info(col_offset, col_index, col_param, expr, 
                                      batch_size, is_pad_char_to_full_length(context.sql_mode_));
        if (OB_FAIL(agg_cell_factory_.alloc_cell(basic_info, dummy_agg_cells_))) {
          LOG_WARN("Failed to alloc agg cell", K(ret), K(i));
        } else if (FALSE_IT(cell = dummy_agg_cells_.at(dummy_agg_cells_.count() - 1))) {
        } else if (OB_UNLIKELY(PD_FIRST_ROW != cell->get_type())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("Unexpected agg type", K(ret), KPC(cell));
        } else {
          static_cast<ObFirstRowAggCell*>(cell)->set_determined_value();
        }
      }
    }
    if (OB_SUCC(ret)) {
      has_lob_column_out_ = false;
      for (int64_t i = 0; OB_SUCC(ret) && i < param.aggregate_exprs_->count(); ++i) {
        int32_t col_offset = param.iter_param_.agg_cols_project_->at(i);
        int32_t col_index = OB_COUNT_AGG_PD_COLUMN_ID == col_offset ? -1 : param.iter_param_.read_info_->get_columns_index().at(col_offset);
        const share::schema::ObColumnParam *col_param = OB_COUNT_AGG_PD_COLUMN_ID == col_offset ? nullptr : out_cols_param->at(col_offset);
        bool exclude_null = false;
        sql::ObExpr *agg_expr = param.aggregate_exprs_->at(i);
        if (OB_ISNULL(agg_expr)) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("Unexpected null agg expr", K(ret));
        } else if (T_FUN_COUNT == agg_expr->type_ || T_FUN_SUM_OPNSIZE == agg_expr->type_) {
          if (OB_COUNT_AGG_PD_COLUMN_ID != col_offset) {
            exclude_null = col_param->is_nullable_for_write();
          }
          // T_FUN_SUM_OPNISZE need_access_data() depends on exclude_null and type,
          // so deferred judgment in ObAggRow::check_need_access_data()
          need_access_data_ = T_FUN_COUNT == agg_expr->type_ ? (need_access_data_ || exclude_null) : true;
        } else {
          need_access_data_ = true;
        }
        ObAggCellBasicInfo basic_info(col_offset, col_index, col_param, agg_expr, 
                                      batch_size, is_pad_char_to_full_length(context.sql_mode_));
        if (OB_FAIL(agg_cell_factory_.alloc_cell(basic_info, agg_cells_, exclude_null))) {
          LOG_WARN("Failed to alloc agg cell", K(ret), K(i));
        }
      }
    }
  }
  return ret;
}


bool ObAggRow::check_need_access_data()
{
  if (!need_access_data_) {
  } else {
    need_access_data_ = false;
    for (int64_t i = 0; !need_access_data_ && i < agg_cells_.count(); ++i) {
      need_access_data_ = agg_cells_.at(i)->need_access_data();
    }
  }
  return need_access_data_;
}

ObAggregatedStore::ObAggregatedStore(const int64_t batch_size, sql::ObEvalCtx &eval_ctx, ObTableAccessContext &context)
    : ObBlockBatchedRowStore(batch_size, eval_ctx, context),
      agg_row_(*context_.stmt_allocator_),
      agg_flat_row_mode_(false),
      row_buf_(),
      aggregate_program_(nullptr)
{
}

ObAggregatedStore::~ObAggregatedStore()
{
  reset();
}

void ObAggregatedStore::reset()
{
  ObBlockBatchedRowStore::reset();
  agg_row_.reset();
  agg_flat_row_mode_ = false;
  row_buf_.reset();
  aggregate_program_ = nullptr;
}

void ObAggregatedStore::reuse()
{
  ObBlockBatchedRowStore::reuse();
  iter_end_flag_ = IterEndState::PROCESSING;
}

int ObAggregatedStore::on_scan_start()
{
  int ret = OB_SUCCESS;
  if (nullptr != aggregate_program_ && OB_FAIL(aggregate_program_->reset_scan())) {
    LOG_WARN("failed to reset pushdown aggregate program at scan boundary", K(ret));
  }
  return ret;
}

int ObAggregatedStore::reuse_capacity(const int64_t capacity)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(capacity <= 0 || capacity > batch_size_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(capacity), K(batch_size_));
  } else {
    agg_row_.reuse();
    row_capacity_ = capacity;
    eval_ctx_.reuse(capacity);
  }
  return ret;
}

int ObAggregatedStore::init(const ObTableAccessParam &param, common::hash::ObHashSet<int32_t> *agg_col_mask)
{
  UNUSED(agg_col_mask);
  int ret = OB_SUCCESS;
  aggregate_program_ = nullptr == param.get_op()
      ? nullptr
      : param.get_op()->get_pushdown_aggregate_program();
  if (nullptr != aggregate_program_) {
    if (OB_ISNULL(param.iter_param_.agg_cols_project_)
        || param.iter_param_.agg_cols_project_->count() <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected aggregate input projector", K(ret), KP(param.iter_param_.agg_cols_project_));
    } else if (OB_FAIL(ObBlockBatchedRowStore::init(param))) {
      LOG_WARN("failed to initialize aggregate program row store", K(ret));
    } else if (OB_FAIL(aggregate_program_->reset_scan())) {
      LOG_WARN("failed to reset pushdown aggregate program", K(ret));
    } else {
      LOG_DEBUG("selected query-owned scalar aggregate program",
                KP(aggregate_program_), K(param.iter_param_.agg_cols_project_->count()));
    }
  } else if (OB_ISNULL(param.output_exprs_) ||
      OB_ISNULL(param.iter_param_.out_cols_project_) ||
      OB_ISNULL(param.aggregate_exprs_) ||
      OB_ISNULL(param.iter_param_.agg_cols_project_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected aggregate pushdown expr and projector", K(ret), K(param.output_exprs_),
        K(param.iter_param_.out_cols_project_),
        K(param.aggregate_exprs_), K(param.iter_param_.agg_cols_project_));
  } else if (param.output_exprs_->count() != param.iter_param_.out_cols_project_->count() ||
      param.aggregate_exprs_->count() != param.iter_param_.agg_cols_project_->count() ||
      param.aggregate_exprs_->count() <= 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected aggregate count", K(ret), K(param.output_exprs_->count()),
        K(param.iter_param_.out_cols_project_->count()),
        K(param.aggregate_exprs_->count()), K(param.iter_param_.agg_cols_project_->count()));
  } else if (OB_FAIL(ObBlockBatchedRowStore::init(param))) {
    LOG_WARN("Failed to init ObBlockBatchedRowStore", K(ret));
  } else if (OB_FAIL(agg_row_.init(param, context_, batch_size_))) {
    LOG_WARN("Failed to init agg cells", K(ret));
  } else if (OB_FAIL(check_agg_in_row_mode(param.iter_param_))) {
    LOG_WARN("Failed to check agg in row mode", K(ret));
  } else if (agg_flat_row_mode_ &&
             OB_FAIL(row_buf_.init(*context_.stmt_allocator_, param.iter_param_.get_max_out_col_cnt()))) {
    LOG_WARN("Fail to init datum row buf", K(ret));
  }
  if (OB_FAIL(ret)) {
    reset();
  }
  return ret;
}

int ObAggregatedStore::check_agg_in_row_mode(const ObTableIterParam &iter_param)
{
  int ret = OB_SUCCESS;
  int64_t agg_cnt = 0;
  ObAggCell *cell = nullptr;
  const ObITableReadInfo *read_info = nullptr;
  if (OB_ISNULL(read_info = iter_param.get_read_info())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null read info", K(ret), K(iter_param));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_agg_count(); ++i) {
    if (OB_ISNULL(cell = agg_row_.at(i))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpecte null agg cell", K(ret), K(i));
    } else if (OB_COUNT_AGG_PD_COLUMN_ID == cell->get_col_offset()) {
    } else if (cell->get_col_offset() >= read_info->get_request_count()) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Unexpected col idx", K(ret), K(i), KPC(cell), K(read_info->get_request_count()));
    } else if (ObPDAggType::PD_FIRST_ROW != cell->get_type()) {
      agg_cnt++;
    }
  }
  if (OB_SUCC(ret)) {
    agg_flat_row_mode_ =
        agg_cnt > AGG_ROW_MODE_COUNT_THRESHOLD ||
        (double) agg_cnt/read_info->get_request_count() > AGG_ROW_MODE_RATIO_THRESHOLD;
  }
  return ret;
}

int ObAggregatedStore::fill_index_info(const blocksstable::ObMicroIndexInfo &index_info)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObAggregatedStore is not inited", K(ret), K(*this));
  } else if (nullptr != aggregate_program_) {
    ObPushdownAggregateInput input(
        *iter_param_, index_info, is_pad_char_to_full_length(context_.sql_mode_));
    bool can_consume = false;
    set_aggregated_in_prefetch();
    if (OB_FAIL(aggregate_program_->can_consume(input, can_consume))) {
      LOG_WARN("failed to probe aggregate index summary", K(ret), K(index_info));
    } else if (OB_UNLIKELY(!can_consume)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("aggregate index summary no longer satisfies probed capability", K(ret), K(index_info));
    } else if (OB_FAIL(aggregate_program_->consume(input))) {
      LOG_WARN("failed to consume aggregate index summary", K(ret), K(index_info));
    }
  } else {
    set_aggregated_in_prefetch();
    for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_agg_count(); ++i) {
       ObAggCell *cell = agg_row_.at(i);
       if (OB_FAIL(cell->eval_index_info(index_info))) {
         LOG_WARN("Failed to eval index info", K(ret), K(i), K(*cell));
       }
    }
  }
  return ret;
}

int ObAggregatedStore::can_use_index_info(
    const blocksstable::ObMicroIndexInfo &index_info,
    bool &can_agg)
{
  int ret = OB_SUCCESS;
  can_agg = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObAggregatedStore is not inited", K(ret));
  } else if (nullptr != aggregate_program_) {
    if (filter_is_null()
        && index_info.can_blockscan()
        && !index_info.is_left_border()
        && !index_info.is_right_border()) {
      ObPushdownAggregateInput input(
          *iter_param_, index_info, is_pad_char_to_full_length(context_.sql_mode_));
      if (OB_FAIL(aggregate_program_->can_consume(input, can_agg))) {
        LOG_WARN("failed to probe aggregate index summary", K(ret), K(index_info));
      }
    }
  } else {
    // Legacy scalar aggregation only consumes index cardinality.  The new
    // program path above additionally accepts exact nullable COUNT summaries.
    can_agg = filter_is_null()
        && !agg_row_.check_need_access_data()
        && index_info.can_blockscan()
        && (!agg_row_.has_lob_column_out() || !index_info.has_lob_out_row())
        && !index_info.is_left_border()
        && !index_info.is_right_border();
  }
  return ret;
}

int ObAggregatedStore::fill_rows(
    const int64_t group_idx,
    blocksstable::ObIMicroBlockRowScanner &scanner,
    int64_t &begin_index,
    const int64_t end_index,
    const ObFilterResult &res)
{
  UNUSED(group_idx);
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObAggregatedStore is not inited", K(ret), K(*this));
  } else if (nullptr != aggregate_program_) {
    blocksstable::ObIMicroBlockReader *reader = scanner.get_reader();
    const bool is_reverse = begin_index > end_index;
    const int64_t covered_row_count = is_reverse
        ? begin_index - end_index
        : end_index - begin_index;
    bool can_consume_dense = false;
    int64_t micro_row_count = 0;
    if (OB_ISNULL(reader)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null aggregate input reader", K(ret));
    } else if (OB_FAIL(reader->get_row_count(micro_row_count))) {
      LOG_WARN("failed to get aggregate input row count", K(ret));
    } else if (nullptr == res.bitmap_ && micro_row_count == covered_row_count) {
      ObPushdownAggregateInput dense_input(
          *iter_param_, reader, nullptr, covered_row_count,
          is_pad_char_to_full_length(context_.sql_mode_));
      if (OB_FAIL(aggregate_program_->can_consume(dense_input, can_consume_dense))) {
        LOG_WARN("failed to probe dense aggregate input", K(ret), K(covered_row_count));
      } else if (can_consume_dense) {
        if (OB_FAIL(aggregate_program_->consume(dense_input))) {
          LOG_WARN("failed to consume dense aggregate input", K(ret), K(covered_row_count));
        } else {
          begin_index = end_index;
        }
      }
    }
    if (OB_SUCC(ret) && !can_consume_dense) {
        while (OB_SUCC(ret)) {
          int64_t row_count = 0;
          if (OB_FAIL(get_row_ids(reader, begin_index, end_index, row_count, false, res))) {
            if (OB_UNLIKELY(OB_ITER_END != ret)) {
              LOG_WARN("failed to get aggregate input row ids", K(ret), K(begin_index), K(end_index));
            }
          } else if (0 == row_count) {
          } else {
            ObPushdownAggregateInput selected_input(
                *iter_param_, reader, row_ids_, row_count,
                is_pad_char_to_full_length(context_.sql_mode_));
            bool can_consume_selected = false;
            if (OB_FAIL(aggregate_program_->can_consume(
                    selected_input, can_consume_selected))) {
              LOG_WARN("failed to probe selected aggregate input", K(ret), K(row_count));
            } else if (OB_UNLIKELY(!can_consume_selected)) {
              ret = OB_ERR_UNEXPECTED;
              LOG_WARN("selected aggregate input lacks a required exact value capability",
                       K(ret), K(row_count));
            } else if (OB_FAIL(aggregate_program_->consume(selected_input))) {
              LOG_WARN("failed to consume selected aggregate input", K(ret), K(row_count));
            }
          }
        }
    }
  } else {
    int64_t row_count = 0;
    bool is_reverse = begin_index > end_index;
    int64_t covered_row_count = is_reverse ? begin_index - end_index : end_index - begin_index;
    // if should check null or not whole block is covered
    // must get valid rows
     bool need_get_row_ids = false;
    int64_t micro_row_count = 0;
    blocksstable::ObIMicroBlockReader *reader = scanner.get_reader();
    if (OB_FAIL(reader->get_row_count(micro_row_count))) {
      LOG_WARN("Failed to get micro row count", K(ret));
    } else if(FALSE_IT(need_get_row_ids = agg_row_.check_need_access_data() || micro_row_count != covered_row_count)) {
    } else if (!need_get_row_ids) {
      row_count = nullptr == res.bitmap_ ? covered_row_count : res.bitmap_->popcnt();
      if (0 == row_count) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_agg_count(); ++i) {
          ObAggCell *cell = agg_row_.at(i);
          if (OB_FAIL(cell->eval_micro_block(*iter_param_, context_, cell->get_col_offset(), reader, nullptr, row_count))) {
            LOG_WARN("Failed to eval micro", K(ret), K(i), K(*cell), K(begin_index), K(end_index));
          }
        }
      }
      if (OB_SUCC(ret)) {
        begin_index = end_index;
      }
    } else {
      while (OB_SUCC(ret)) {
        if (OB_FAIL(get_row_ids(reader, begin_index, end_index, row_count, false, res))) {
          if (OB_UNLIKELY(OB_ITER_END != ret)) {
            LOG_WARN("Failed to get row ids", K(ret), K(begin_index), K(end_index));
          }
        } else if (0 == row_count) {
        } else if (agg_flat_row_mode_ && blocksstable::ObIMicroBlockReader::Reader == reader->get_type()) {
          // for flat block, do aggregate in row mode in some case
           blocksstable::ObMicroBlockReader *block_reader = static_cast<blocksstable::ObMicroBlockReader*>(reader);
           if (OB_FAIL(block_reader->get_aggregate_result(*iter_param_, context_, row_ids_, row_count, row_buf_, agg_row_.get_agg_cells()))) {
             LOG_WARN("Failed to get aggregate", K(ret));
           }
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_agg_count(); ++i) {
            ObAggCell *cell = agg_row_.at(i);
            if (OB_FAIL(cell->eval_micro_block(*iter_param_, context_, cell->get_col_offset(), reader, row_ids_, row_count))) {
              LOG_WARN("Failed to eval micro", K(ret), K(i), K(*cell), K(begin_index), K(end_index));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObAggregatedStore::fill_rows(const int64_t group_idx, const int64_t row_count)
{
  UNUSEDx(group_idx, row_count);
  return OB_SUCCESS;
}

int ObAggregatedStore::fill_row(blocksstable::ObDatumRow &row)
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObAggregatedStore is not inited", K(ret), K(*this));
  } else if (nullptr != aggregate_program_) {
    ObPushdownAggregateInput input(
        *iter_param_, row, is_pad_char_to_full_length(context_.sql_mode_));
    bool can_consume = false;
    if (OB_FAIL(aggregate_program_->can_consume(input, can_consume))) {
      LOG_WARN("failed to probe aggregate row input", K(ret), K(row));
    } else if (OB_UNLIKELY(!can_consume)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("aggregate row input lacks a required exact value capability", K(ret), K(row));
    } else if (OB_FAIL(aggregate_program_->consume(input))) {
      LOG_WARN("failed to consume aggregate row input", K(ret), K(row));
    }
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_agg_count(); ++i) {
      ObAggCell *cell = agg_row_.at(i);
      if (OB_FAIL(cell->eval(row.storage_datums_[cell->get_col_offset()]))) {
        LOG_WARN("Failed to eval agg cell", K(ret), K(i), K(row), K(*cell));
      }
    }
  }
  return ret;
}

int ObAggregatedStore::collect_aggregated_result()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObAggregatedStore is not inited", K(ret), K(*this));
  } else if (nullptr != aggregate_program_) {
    share::aggregate::ObAggregateEmitResult emit_result;
    if (OB_FAIL(aggregate_program_->seal())) {
      LOG_WARN("failed to seal pushdown aggregate program", K(ret));
    } else if (OB_FAIL(aggregate_program_->emit(1, emit_result))) {
      LOG_WARN("failed to materialize pushdown aggregate result", K(ret));
    } else if (OB_UNLIKELY(1 != emit_result.row_count_ || !emit_result.end_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected scalar aggregate emit result", K(ret),
               K(emit_result.row_count_), K(emit_result.end_));
    }
  } else if (!has_data()) {
    // just ret OB_ITER_END if no row aggregated
    ret = OB_ITER_END;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_agg_count(); ++i) {
      ObAggCell *cell = agg_row_.at(i);
      if (OB_FAIL(cell->collect_result(eval_ctx_))) {
        LOG_WARN("Failed to fill agg result", K(ret), K(i), K(*cell));
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < agg_row_.get_dummy_agg_count(); ++i) {
      ObAggCell *cell = agg_row_.at_dummy(i);
      if (OB_FAIL(cell->collect_result(eval_ctx_))) {
        LOG_WARN("Failed to fill agg result", K(ret), K(i), K(*cell));
      }
    }
  }
  return ret;
}

bool ObAggregatedStore::has_data()
{
  bool has_data = nullptr != aggregate_program_;
  for (int64_t i = 0; !has_data && i < agg_row_.get_agg_count(); ++i) {
    has_data = agg_row_.at(i)->is_aggregated();
  }
  return has_data;
}

int ObAggregatedStore::get_agg_cell(const sql::ObExpr *expr, ObAggCell *&agg_cell)
{
  int ret = OB_SUCCESS;
  agg_cell = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObAggregatedStore is not inited", K(ret), K(*this));
  } else {
    for (int64_t i = 0; i < agg_row_.get_agg_count(); ++i) {
      ObAggCell *cell = agg_row_.at(i);
      if (cell->get_agg_expr() == expr) {
        agg_cell = cell;
        break;
      }
    }
  }
  if (OB_SUCC(ret) && nullptr == agg_cell) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Unexpected null agg cell", K(ret), KPC(expr));
  }
  return ret;
}

} /* namespace storage */
} /* namespace oceanbase */
