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

#define USING_LOG_PREFIX SQL_EXE

#include "ob_execute_result.h"
#include "common/datum/datum_value_projection.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

int ObExecuteResult::open(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  ret = open();
  return ret;
}

int ObExecuteResult::get_next_row(ObExecContext &ctx, const common::ObNewRow *&row)
{
  int ret = OB_SUCCESS;
  row = nullptr;
  if (OB_ISNULL(static_engine_root_)) {
    ret = OB_NOT_INIT;
  } else {
    const int64_t count = static_engine_root_->get_spec().output_.count();
    if (count > 0 && row_.cells_ == nullptr) {
      if (OB_FAIL(DatumValueBuffer::allocate(ctx.get_allocator(), count, row_.cells_))) {
      } else {
        row_.count_ = count;
        row_.projector_size_ = 0;
        row_.projector_ = nullptr;
      }
    }
    ResultRowView view;
    if (OB_FAIL(ret)) {
    } else if (count != row_.count_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(get_next_native_result_row(ctx, view))) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        DatumValue value;
        if (OB_FAIL(view.get_value(i, value))) {
        } else if (OB_FAIL(DatumValueBuffer::assign_view(value, row_.cells_, count, i))) {
        }
      }
      if (OB_SUCC(ret)) { row = &row_; }
    }
  }
  return ret;
}

int ObExecuteResult::get_next_result_row(ObExecContext &ctx, ResultRowView &view)
{
  return get_next_native_result_row(ctx, view);
}

int ResultRowView::project_value(const ObDatum &datum, const SqlTypeMeta &meta,
                                 SqlValueDatumMapType mapping, const SqlTypeMeta &previous_meta,
                                 DatumValue &value, bool keep_null_collation)
{
  return common::project_datum_value_view(datum,meta,mapping,previous_meta,value,keep_null_collation);
}

int ResultRowView::get_value(int64_t i, DatumValue &value) const
{
  int ret = OB_SUCCESS;
  if (!is_datum_row() && legacy_ == nullptr) {
    ret = OB_NOT_INIT;
  } else if (i < 0 || i >= count()) {
    ret = OB_SIZE_OVERFLOW;
  } else if (!is_datum_row()) {
    if (legacy_ == nullptr || legacy_->cells_ == nullptr) {
      ret = OB_NOT_INIT;
    } else {
      const int64_t physical = legacy_->projector_size_ > 0
          ? (legacy_->projector_ == nullptr ? -1 : legacy_->projector_[i]) : i;
      if (physical < 0 || physical >= legacy_->count_) { ret = OB_ERR_UNEXPECTED; }
      else { value = legacy_->cells_[physical]; }
    }
  } else if (i >= count_) {
    ret = OB_SIZE_OVERFLOW;
  } else if (nullptr != cell_metas_) {
    DatumValue candidate(datum(i), cell_metas_[i]);
    if (candidate.meta_.is_null()) {
      candidate.datum_.set_null();
    } else if (candidate.meta_.is_ext_sql_array()) {
      candidate.datum_.ptr_ += candidate.datum_.is_native_extend() ? 0 : 8;
      candidate.datum_.pack_ = sizeof(int64_t);
    }
    value = candidate;
  } else {
    const ObExpr &e = expr(i);
    ret = project_value(datum(i), e.obj_meta_, e.obj_datum_map_, SqlTypeMeta(), value);
  }
  return ret;
}

int ObExecuteResult::get_next_native_result_row(ObExecContext &ctx, ResultRowView &view)
{
  int ret = OB_SUCCESS;
  view = ResultRowView();
  if (OB_ISNULL(static_engine_root_)) {
    ret = OB_NOT_INIT;
  } else {
    const ObOpSpec &spec = static_engine_root_->get_spec();
    const int64_t count = spec.output_.count();
    if (count > 0 && nullptr == native_cell_metas_) {
      if (OB_ISNULL(native_cell_metas_ = static_cast<SqlTypeMeta *>(
                       ctx.get_allocator().alloc(sizeof(SqlTypeMeta) * count)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        for (int64_t i = 0; i < count; ++i) { new (&native_cell_metas_[i]) SqlTypeMeta(); }
        native_cell_count_ = count;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (count != native_cell_count_) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(spec.is_vectorized() ? br_it_.get_next_row() : get_next_row())) {
    } else {
      ObEvalCtx &eval_ctx = static_engine_root_->get_eval_ctx();
      const int64_t idx = spec.is_vectorized() ? br_it_.cur_idx() : 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
        ObExpr &expr = *spec.output_.at(i);
        ObDatum *datum = nullptr;
        if (!spec.is_vectorized()) {
          ret = expr.eval(eval_ctx, datum);
        } else {
          datum = expr.locate_batch_datums(eval_ctx) + (expr.is_batch_result() ? idx : 0);
        }
        DatumValue cell;
        if (OB_SUCC(ret) && OB_FAIL(ResultRowView::project_value(
               *datum, expr.obj_meta_, expr.obj_datum_map_, native_cell_metas_[i], cell))) {
        } else if (OB_SUCC(ret)) {
          native_cell_metas_[i] = cell.meta_;
        }
      }
      if (OB_SUCC(ret)) {
        view.set_datums(spec.output_, eval_ctx, idx, spec.is_vectorized(), native_cell_metas_);
      }
    }
  }
  return ret;
}

int ObExecuteResult::close(ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  UNUSED(ctx);
  ret = close();
  return ret;
}

int ObExecuteResult::open() const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(static_engine_root_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(static_engine_root_->open())) {
    if (OB_TRY_LOCK_ROW_CONFLICT != ret && OB_TRANSACTION_SET_VIOLATION != ret) {
    }
  } else if (!static_engine_root_->get_spec().plan_->var_init_exprs_.empty()) {
    // Evaluate the var init expr in generated table, This is to be compatible with some of mysql's uses of variables
    // Such as "select c1,(@rownum:= @rownum+1) as CCBH from t1,(SELECT@rownum:=0) B"
    const ExprFixedArray &var_init_exprs = static_engine_root_->get_spec().plan_->var_init_exprs_;
    for (int64_t i = 0; OB_SUCC(ret) && i < var_init_exprs.count(); i++) {
      ObDatum *datum = NULL;
      ObExpr *expr = var_init_exprs.at(i);
      if (OB_FAIL(expr->eval(static_engine_root_->get_eval_ctx(), datum))) {
      }
    }
  }
  return ret;
}

int ObExecuteResult::get_next_row() const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(static_engine_root_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(static_engine_root_->get_next_row())
             && OB_ITER_END != ret
             && OB_TRY_LOCK_ROW_CONFLICT != ret) {
  }
  return ret;
}

int ObExecuteResult::close() const
{
  int ret = OB_SUCCESS;
  if (NULL != static_engine_root_) {
    if (OB_FAIL(static_engine_root_->close())) {
    }
  }
  return ret;
}

}/* ns sql*/
}/* ns oceanbase */
