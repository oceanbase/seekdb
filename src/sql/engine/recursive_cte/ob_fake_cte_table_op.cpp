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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/recursive_cte/ob_fake_cte_table_op.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObFakeCTETableSpec, ObOpSpec),
                      column_involved_offset_,
                      column_involved_exprs_,
                      is_bulk_search_,
                      is_union_distinct_);

int ObFakeCTETableOp::inner_get_next_row()
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_FAIL(try_check_status())) {
  } else if (empty_) {
    ret = OB_ITER_END;
  } else if (!MY_SPEC.is_bulk_search_ && OB_FAIL(get_next_single_row())) {
  } else if (MY_SPEC.is_bulk_search_ && OB_FAIL(get_next_bulk_row())) {
  }
  return ret;
}

int ObFakeCTETableOp::get_next_single_row()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pump_row_) && OB_UNLIKELY(MY_SPEC.column_involved_exprs_.count() != 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_NOT_NULL(pump_row_) &&
             OB_FAIL(pump_row_->to_expr(MY_SPEC.column_involved_exprs_, eval_ctx_))) {
  } else {
    empty_ = true;
  }
  return ret;
}

int ObFakeCTETableOp::get_next_bulk_row()
{
  int ret = OB_SUCCESS;
  int64_t expr_cnt = MY_SPEC.column_involved_exprs_.count();
  if (OB_UNLIKELY(bulk_rows_.empty() && expr_cnt != 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(bulk_rows_.empty())) {
    empty_ = true;
  } else if (OB_UNLIKELY(read_bluk_cnt_ >= bulk_rows_.count()
             || OB_ISNULL(bulk_rows_.at(read_bluk_cnt_)))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(to_expr(MY_SPEC.column_involved_exprs_, MY_SPEC.column_involved_offset_,
                             bulk_rows_.at(read_bluk_cnt_), eval_ctx_))) {
  }
  if (OB_SUCC(ret)) {
    read_bluk_cnt_++;
    if (OB_UNLIKELY(read_bluk_cnt_ == bulk_rows_.count())) {
      empty_ = true;
      bulk_rows_.reset();
    }
  }
  return ret;
}

int ObFakeCTETableOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  clear_evaluated_flag();
  if (OB_FAIL(try_check_status())) {
  } else if (empty_) {
    brs_.end_ = true;
    brs_.size_ = 0;
  } else if (!MY_SPEC.is_bulk_search_ && OB_FAIL(get_next_single_batch(max_row_cnt))) {
  } else if (MY_SPEC.is_bulk_search_ && OB_FAIL(get_next_bulk_batch(max_row_cnt))) {
  }
  return ret;
}

int ObFakeCTETableOp::get_next_single_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(get_next_single_row())) {
    if (ret == OB_ITER_END) {
      brs_.end_ = true;
      brs_.size_ = 0;
      ret = OB_SUCCESS;
    } else {
      LOG_INFO("Fail to get result from cte table", K(ret));
    }
  } else {
    brs_.size_ = 1;
  }
  return ret;
}

int ObFakeCTETableOp::get_next_bulk_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  int64_t read_rows = 0;
  int64_t batch_size = std::min(max_row_cnt, MY_SPEC.max_batch_size_);
  int64_t expr_cnt = MY_SPEC.column_involved_exprs_.count();
  if (OB_UNLIKELY(bulk_rows_.empty() && expr_cnt != 0)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(bulk_rows_.empty())) {
    empty_ = true;
  } else if (OB_UNLIKELY(read_bluk_cnt_ >= bulk_rows_.count())
             || OB_ISNULL(bulk_rows_.at(read_bluk_cnt_))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(read_rows = std::min(batch_size, bulk_rows_.count() - read_bluk_cnt_))) {
  } else if (OB_FAIL(attach_rows(MY_SPEC.column_involved_exprs_, MY_SPEC.column_involved_offset_,
                                 bulk_rows_, read_bluk_cnt_, eval_ctx_, read_rows))) {
  }
  if (OB_SUCC(ret)) {
    brs_.size_ = read_rows;
    brs_.end_ = (0 == read_rows);
    read_bluk_cnt_ += read_rows;
    if (OB_UNLIKELY(read_bluk_cnt_ == bulk_rows_.count())) {
      empty_ = true;
      bulk_rows_.reset();
    }
  }
  return ret;
}

void ObFakeCTETableOp::reuse()
{
  int ret = OB_SUCCESS;
  empty_ = true;
  read_bluk_cnt_ = 0;
  bulk_rows_.reset();
  if (OB_NOT_NULL(pump_row_)) {
    allocator_->free(const_cast<ObChunkDatumStore::StoredRow *>(pump_row_));
    pump_row_ = nullptr;
  }
}

void ObFakeCTETableOp::destroy()
{
  if (OB_NOT_NULL(mem_context_)) {
    DESTROY_CONTEXT(mem_context_);
    mem_context_ = nullptr;
  }
  ObOperator::destroy();
}

int ObFakeCTETableOp::add_single_row(ObChunkDatumStore::StoredRow *row)
{
  int ret = OB_SUCCESS;
  /**
   * R union returns the data according to all base columns of the fake cte table
   * The base columns of the fake cte table are not necessarily all used, so the used cells of StoredRow need to be copied to a new StoredRow
   */
  const ObChunkDatumStore::StoredRow *new_row = nullptr;
  ObChunkDatumStore::StoredRow *old_row = nullptr;
  if (OB_UNLIKELY(0 == MY_SPEC.column_involved_offset_.count())) {
    empty_ = false;
  } else if (OB_ISNULL(row)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(deep_copy_row(row, new_row, MY_SPEC.column_involved_offset_,
                                    ObSearchMethodOp::ROW_EXTRA_SIZE, *allocator_))) {
  } else {
    old_row = const_cast<ObChunkDatumStore::StoredRow *>(pump_row_);
    pump_row_ = new_row;
    empty_ = false;
    if (nullptr != old_row) {
      allocator_->free(old_row);
      old_row = nullptr;
    }
  }
  return ret;
}

int ObFakeCTETableOp::inner_rescan()
{
  int ret = ObOperator::inner_rescan();
  if (!MY_SPEC.is_bulk_search_ && pump_row_ != nullptr) {
    empty_ = false;
  } else if (MY_SPEC.is_bulk_search_ && !bulk_rows_.empty()) {
    empty_ = false;
  }
  return ret;
}

int ObFakeCTETableOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (MY_SPEC.column_involved_exprs_.count() != MY_SPEC.column_involved_offset_.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    lib::ContextParam param;
    param.set_mem_attr("FakeCteTable", ObCtxIds::WORK_AREA);
    if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(mem_context_, param))) {
    } else if (OB_ISNULL(mem_context_)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      allocator_ = &mem_context_->get_malloc_allocator();
    }
  }
  return ret;
}

int ObFakeCTETableOp::inner_close()
{
  int ret = OB_SUCCESS;
  reuse();
  return ret;
}

namespace
{
int chosen_row_size(const StoredDatumCell *cells, int64_t count,
                   const ObIArray<int64_t> &chosen, uint32_t extra_size, int64_t &size)
{
  int ret = OB_SUCCESS;
  int64_t required = sizeof(ObChunkDatumStore::StoredRow) + int64_t(extra_size);
  if (cells == nullptr || count < 0 || chosen.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (required > UINT32_MAX
      || chosen.count() > (UINT32_MAX - required) / int64_t(sizeof(StoredDatumCell))) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    required += sizeof(StoredDatumCell) * chosen.count();
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < chosen.count(); ++i) {
    const int64_t index = chosen.at(i);
    if (index < 0 || index >= count) {
      ret = OB_INVALID_ARGUMENT;
    } else if (!cells[index].is_null()) {
      if (cells[index].len_ > 0 && cells[index].ptr_ == nullptr) {
        ret = OB_INVALID_ARGUMENT;
      } else if (cells[index].len_ > UINT32_MAX - required) {
        ret = OB_SIZE_OVERFLOW;
      } else {
        required += cells[index].len_;
      }
    }
  }
  if (OB_SUCC(ret)) { size = required; }
  return ret;
}
}

int ObFakeCTETableOp::copy_stored_cells(ObChunkDatumStore::StoredRow *row, const StoredDatumCell *cells,
  int64_t cnt, const common::ObIArray<int64_t> &chosen_datums, char *buf, const int64_t size,
  const int64_t row_size, const uint32_t row_extend_size)
{
  int ret = OB_SUCCESS;
  int64_t required = 0;
  if (OB_UNLIKELY(nullptr == row || row->payload_ != buf || size < 0)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(chosen_row_size(cells, cnt, chosen_datums, row_extend_size, required))) {
  } else if (row_size != required) {
    ret = OB_INVALID_ARGUMENT;
  } else if (required - int64_t(sizeof(*row)) > size) {
    ret = OB_BUF_NOT_ENOUGH;
  } else {
    row->cnt_ = static_cast<uint32_t>(chosen_datums.count());
    int64_t pos = sizeof(StoredDatumCell) * row->cnt_ + row_extend_size;
    // size is the payload capacity, whereas row_size_ includes the header and
    // reserved extra bytes. StoredRow::assign() copies exactly row_size_ bytes.
    row->row_size_ = static_cast<uint32_t>(required);
    for (int64_t i = 0; OB_SUCC(ret) && i < row->cnt_; ++i) {
      ret = StoredDatumCodec::copy_stored_payload(cells[chosen_datums.at(i)],
          row->stored_cells()[i], buf, size, pos);
    }
  }
  return ret;
}

int ObFakeCTETableOp::deep_copy_row(const ObChunkDatumStore::StoredRow *src_row,
                                    const ObChunkDatumStore::StoredRow *&dst_row,
                                    const ObIArray<int64_t> &chosen_index,
                                    int64_t extra_size,
                                    ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src_row)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (extra_size < 0 || extra_size > UINT32_MAX) {
    ret = OB_INVALID_ARGUMENT;
  } else if (chosen_index.empty()) {
    dst_row = nullptr;
  } else {
    char *buf = nullptr;
    int64_t row_size = 0;
    ret = chosen_row_size(src_row->stored_cells(), src_row->cnt_, chosen_index,
                          static_cast<uint32_t>(extra_size), row_size);
    if (OB_SUCC(ret)) {
      const int64_t head_size = sizeof(ObChunkDatumStore::StoredRow);
      ObChunkDatumStore::StoredRow *new_row = nullptr;
      if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator.alloc(row_size)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_ISNULL(new_row = new(buf)ObChunkDatumStore::StoredRow())) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(copy_stored_cells(new_row, src_row->stored_cells(),
                                    src_row->cnt_, chosen_index, buf + head_size,
                                    row_size - head_size, row_size, static_cast<uint32_t>(extra_size)))) {
      } else {
        dst_row = new_row;
      }
      if (ret != OB_SUCCESS && buf != nullptr) { allocator.free(buf); }
    }
  }
  return ret;
}

int ObFakeCTETableOp::to_expr(
  const common::ObIArray<ObExpr*> &exprs,
  const common::ObIArray<int64_t> &chosen_index,
  ObChunkDatumStore::StoredRow *row, ObEvalCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(row) || exprs.count() != chosen_index.count()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < exprs.count(); ++i) {
      if (exprs.at(i) == nullptr || (!exprs.at(i)->is_const_expr()
          && (chosen_index.at(i) < 0 || chosen_index.at(i) >= row->cnt_))) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
    for (int64_t i = 0;  OB_SUCC(ret) && i < exprs.count(); i++) {
      ObExpr *expr = exprs.at(i);
      if (expr->is_const_expr()) {
        continue;
      } else {
        ret = restore_expr_cell(row->stored_cells()[chosen_index.at(i)], *expr, ctx, true);
        if (OB_SUCC(ret)) { expr->set_evaluated_projected(ctx); }
      }
    } 
  }
  return ret;
}

int ObFakeCTETableOp::attach_rows(
    const common::ObIArray<ObExpr*> &exprs,
    const common::ObIArray<int64_t > &chosen_index,
    const common::ObArray<ObChunkDatumStore::StoredRow *> &srows,
    const int64_t rows_offset, ObEvalCtx &ctx, const int64_t read_rows)
{
  int ret = OB_SUCCESS;
  if (exprs.count() != chosen_index.count() || rows_offset < 0 || read_rows < 0
      || rows_offset > srows.count() || read_rows > srows.count() - rows_offset) {
    ret = OB_INVALID_ARGUMENT;
  }
  // Preflight every accessed row before publishing any expression descriptor.
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < exprs.count(); ++col_idx) {
    const ObExpr *expr = exprs.at(col_idx);
    if (expr == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!expr->is_const_expr()) {
      const int64_t index = chosen_index.at(col_idx);
      const int64_t count = expr->is_batch_result() ? read_rows : std::min(int64_t(1), read_rows);
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        const ObChunkDatumStore::StoredRow *row = srows.at(rows_offset + i);
        if (row == nullptr || index < 0 || index >= row->cnt_) { ret = OB_ERR_UNEXPECTED; }
      }
    }
  }
  if (OB_SUCC(ret) && read_rows == 0) { return ret; }
  for (int64_t col_idx = 0; OB_SUCC(ret) && col_idx < exprs.count(); col_idx++) {
    ObExpr *e = exprs.at(col_idx);
    int64_t idx = chosen_index.at(col_idx);
    if (e->is_const_expr()) {
      continue;
    } else {
      ObEvalCtx::BatchInfoScopeGuard guard(ctx);
      const int64_t count = e->is_batch_result() ? read_rows : 1;
      for (int64_t row = 0; OB_SUCC(ret) && row < count; ++row) {
        guard.set_batch_idx(row);
        ret = restore_expr_cell(srows.at(rows_offset + row)->stored_cells()[idx], *e, ctx, true);
      }
    }
    if (OB_SUCC(ret)) {
      e->set_evaluated_projected(ctx);
      ObEvalInfo &info = e->get_eval_info(ctx);
      info.notnull_ = false;
      info.point_to_frame_ = false;
    }
  }
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
