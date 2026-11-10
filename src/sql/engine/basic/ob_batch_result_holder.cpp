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

#include "ob_batch_result_holder.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"

namespace oceanbase
{
using namespace common;

namespace sql
{

namespace
{
// A holder is a shallow same-execution backup. In particular, inspecting an
// ordinary parameter must not invoke its callback or demand a Datum projection.
int holder_alias_plan(const ObExpr &expr, ObEvalCtx &ctx, ObPhysicalPlanCtx *&output)
{
  ObPhysicalPlanCtx *plan = ctx.exec_ctx_.get_physical_plan_ctx();
  if (plan == nullptr || expr.type_ != T_QUESTIONMARK || expr.is_batch_result() || expr.eval_func_ != nullptr
      || !plan->has_native_parameter(expr.extra_)) { return OB_INVALID_ARGUMENT; }
  int64_t page = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(plan->get_original_param_cnt(), expr.extra_, page, slot);
  if (expr.datum_off_ != ScalarFrameLayout::datum_offset(slot)
      || expr.eval_info_off_ != ScalarFrameLayout::eval_info_offset(slot)) {
    return OB_INVALID_ARGUMENT;
  }
  output = plan;
  return OB_SUCCESS;
}
} // namespace

int ObBatchResultHolder::init(const common::ObIArray<ObExpr *> &exprs, ObEvalCtx &eval_ctx,
                              common::ObIAllocator *allocator /*nullptr*/)
{
  if (inited_ || exprs.count() == 0) { return OB_SUCCESS; }
  exprs_ = &exprs;
  eval_ctx_ = &eval_ctx;
  allocator_ = allocator;
  const int64_t capacity = MAX(int64_t(1), eval_ctx.max_batch_size_);
  const int64_t count = exprs.count();
  if (count < 0 || count > INT64_MAX / int64_t(sizeof(Datum)) / capacity
      || count > INT64_MAX / int64_t(sizeof(NativeParameterSlotSnapshot))) {
    return OB_SIZE_OVERFLOW;
  }
  bool has_alias = false;
  for (int64_t i = 0; i < count; ++i) {
    if (exprs.at(i) == nullptr) { return OB_INVALID_ARGUMENT; }
    has_alias |= exprs.at(i)->native_parameter_alias_;
  }
  ObIAllocator &alloc = allocator == nullptr ? eval_ctx.exec_ctx_.get_allocator() : *allocator;
  Datum *datums = static_cast<Datum *>(alloc.alloc(capacity * count * sizeof(Datum)));
  if (datums == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  NativeParameterSlotSnapshot *parameters = has_alias
      ? static_cast<NativeParameterSlotSnapshot *>(alloc.alloc(count * sizeof(*parameters))) : nullptr;
  if (has_alias && parameters == nullptr) {
    alloc.free(datums);
    return OB_ALLOCATE_MEMORY_FAILED;
  }
  if (parameters != nullptr) {
    for (int64_t i = 0; i < count; ++i) { new (parameters + i) NativeParameterSlotSnapshot(); }
  }
  datums_ = datums;
  parameters_ = parameters;
  capacity_ = capacity;
  expr_count_ = count;
  saved_size_ = 0;
  inited_ = true;
  return OB_SUCCESS;
}

void ObBatchResultHolder::destroy()
{
  if (allocator_ != nullptr) {
    if (datums_ != nullptr) { allocator_->free(datums_); }
    if (parameters_ != nullptr) { allocator_->free(parameters_); }
  }
  datums_ = nullptr;
  parameters_ = nullptr;
  exprs_ = nullptr;
  eval_ctx_ = nullptr;
  allocator_ = nullptr;
  capacity_ = 0;
  expr_count_ = 0;
  inited_ = false;
  reset();
}

int ObBatchResultHolder::save(int64_t size)
{
  if (exprs_ == nullptr) { return OB_SUCCESS; }
  if (!inited_ || datums_ == nullptr) { return OB_NOT_INIT; }
  if (size < 0 || size > capacity_ || exprs_->count() != expr_count_) { return OB_INVALID_ARGUMENT; }
  if (size == 0) { return OB_SUCCESS; }
  // Validate the whole group before replacing any previous backup. The second
  // pass below has no allocation, callback or other source of execution change.
  for (int64_t i = 0; i < expr_count_; ++i) {
    const ObExpr *expr = exprs_->at(i);
    if (expr == nullptr) { return OB_INVALID_ARGUMENT; }
    if (expr->native_parameter_alias_) {
      ObPhysicalPlanCtx *plan = nullptr;
      NativeParameterSlotSnapshot snapshot;
      int ret = parameters_ == nullptr ? OB_STATE_NOT_MATCH : holder_alias_plan(*expr, *eval_ctx_, plan);
      if (OB_SUCC(ret)) { ret = plan->capture_runtime_parameter_slot(expr->extra_, snapshot); }
      if (OB_FAIL(ret)) { return ret; }
    } else if (expr->native_i64_layout_ != nullptr) {
      bool native = false;
      int ret = expr->get_native_i64_mode(*eval_ctx_, native);
      const int64_t count = expr->is_batch_result() ? size : 1;
      for (int64_t row = 0; OB_SUCC(ret) && native && row < count; ++row) {
        Datum value;
        ret = expr->read_value(*eval_ctx_, row, value);
      }
      if (OB_FAIL(ret)) { return ret; }
    }
  }
  int64_t off = 0;
  for (int64_t i = 0; i < expr_count_; ++i) {
    const ObExpr &expr = *exprs_->at(i);
    const int64_t count = expr.is_batch_result() ? size : 1;
    if (expr.native_parameter_alias_) {
      ObPhysicalPlanCtx *plan = eval_ctx_->exec_ctx_.get_physical_plan_ctx();
      const int ret = plan->capture_runtime_parameter_slot(expr.extra_, parameters_[i]);
      OB_ASSERT(ret == OB_SUCCESS);
    } else if (expr.native_i64_layout_ != nullptr && expr.uses_native_i64(*eval_ctx_)) {
      for (int64_t row = 0; row < count; ++row) {
        const int ret = expr.read_value(*eval_ctx_, row, datums_[off + row]);
        OB_ASSERT(ret == OB_SUCCESS);
      }
      if (parameters_ != nullptr) { parameters_[i] = NativeParameterSlotSnapshot(); }
    } else {
      MEMCPY(datums_ + off, expr.locate_batch_datums(*eval_ctx_), sizeof(Datum) * count);
      if (parameters_ != nullptr) { parameters_[i] = NativeParameterSlotSnapshot(); }
    }
    off += count;
  }
  saved_size_ = size;
  return OB_SUCCESS;
}

int ObBatchResultHolder::restore()
{
  if (exprs_ == nullptr) { return OB_SUCCESS; }
  if (!inited_ || datums_ == nullptr) { return OB_NOT_INIT; }
  if (saved_size_ == 0) { return OB_SUCCESS; }
  if (exprs_->count() != expr_count_) { return OB_INVALID_ARGUMENT; }
  // Every alias, owner and location must still be valid before a generic frame
  // or the first canonical parameter is written. Commit cannot allocate/fail.
  int64_t validated_off = 0;
  for (int64_t i = 0; i < expr_count_; ++i) {
    const ObExpr *expr = exprs_->at(i);
    if (expr == nullptr) { return OB_INVALID_ARGUMENT; }
    if (expr->native_parameter_alias_) {
      ObPhysicalPlanCtx *plan = nullptr;
      int ret = parameters_ == nullptr ? OB_STATE_NOT_MATCH : holder_alias_plan(*expr, *eval_ctx_, plan);
      if (OB_SUCC(ret) && parameters_[i].index() != static_cast<int64_t>(expr->extra_)) {
        ret = OB_STATE_NOT_MATCH;
      }
      if (OB_SUCC(ret)) { ret = plan->validate_runtime_parameter_slot(parameters_[i]); }
      if (OB_FAIL(ret)) { return ret; }
    } else if (parameters_ != nullptr && parameters_[i].index() >= 0) {
      return OB_STATE_NOT_MATCH;
    } else if (expr->native_i64_layout_ != nullptr) {
      bool native = false;
      int ret = expr->get_native_i64_mode(*eval_ctx_, native);
      const int64_t count = expr->is_batch_result() ? saved_size_ : 1;
      NativeI64Lane lane;
      if (OB_SUCC(ret) && native) { ret = expr->get_native_i64_lane(*eval_ctx_, lane); }
      if (OB_SUCC(ret) && native && count > lane.capacity) { ret = OB_INVALID_ARGUMENT; }
      for (int64_t row = 0; OB_SUCC(ret) && native && row < count; ++row) {
        const Datum &value = datums_[validated_off + row];
        int64_t span = 0;
        if (value.state() != Datum::State::Present && value.state() != Datum::State::Null) {
          ret = OB_ERR_UNEXPECTED;
        } else { ret = expr->get_datum_copy_plan().carrier_span_size(value, span); }
      }
      if (OB_FAIL(ret)) { return ret; }
    }
    validated_off += expr->is_batch_result() ? saved_size_ : 1;
  }
  int64_t off = 0;
  for (int64_t i = 0; i < expr_count_; ++i) {
    const ObExpr &expr = *exprs_->at(i);
    const int64_t count = expr.is_batch_result() ? saved_size_ : 1;
    if (expr.native_parameter_alias_) {
      eval_ctx_->exec_ctx_.get_physical_plan_ctx()->restore_runtime_parameter_slot(parameters_[i]);
    } else if (expr.native_i64_layout_ != nullptr && expr.uses_native_i64(*eval_ctx_)) {
      NativeI64Lane lane;
      const int ret = expr.get_native_i64_lane(*eval_ctx_, lane);
      OB_ASSERT(ret == OB_SUCCESS);
      for (int64_t row = 0; row < count; ++row) {
        const Datum &value = datums_[off + row];
        if (!value.is_null()) { lane.values[row] = value.get_int(); }
        lane.states[row] = static_cast<uint8_t>(value.state());
      }
    } else { MEMCPY(expr.locate_batch_datums(*eval_ctx_), datums_ + off, sizeof(Datum) * count); }
    off += count;
  }
  return OB_SUCCESS;
}

int ObBatchResultHolder::check_datum_modified()
{
  int ret = OB_SUCCESS;
  if (exprs_ != nullptr && datums_ != nullptr && saved_size_ > 0) {
    if (exprs_->count() != expr_count_) { ret = OB_INVALID_ARGUMENT; }
    int64_t off = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < expr_count_; ++i) {
      const ObExpr *expr = exprs_->at(i);
      if (expr == nullptr) { ret = OB_INVALID_ARGUMENT; }
      else {
        const int64_t count = expr->is_batch_result() ? saved_size_ : 1;
        if (expr->native_parameter_alias_) {
          ObPhysicalPlanCtx *plan = nullptr;
          ret = parameters_ == nullptr ? OB_STATE_NOT_MATCH : holder_alias_plan(*expr, *eval_ctx_, plan);
          if (OB_SUCC(ret) && parameters_[i].index() != static_cast<int64_t>(expr->extra_)) {
            ret = OB_STATE_NOT_MATCH;
          }
          if (OB_SUCC(ret)) { ret = plan->validate_runtime_parameter_slot(parameters_[i]); }
          Datum current;
          if (OB_SUCC(ret)) { ret = plan->read_native_parameter(expr->extra_, current); }
          if (OB_SUCC(ret) && MEMCMP(&current, &parameters_[i].value().datum_, sizeof(Datum)) != 0) {
            ret = OB_ERR_UNEXPECTED;
          }
        } else if (expr->native_i64_layout_ != nullptr) {
          bool native = false;
          ret = expr->get_native_i64_mode(*eval_ctx_, native);
          if (OB_SUCC(ret) && parameters_ != nullptr && parameters_[i].index() >= 0) {
            ret = OB_STATE_NOT_MATCH;
          }
          if (OB_SUCC(ret) && !native) {
            if (MEMCMP(expr->locate_batch_datums(*eval_ctx_), datums_ + off, sizeof(Datum) * count) != 0) {
              ret = OB_ERR_UNEXPECTED;
            }
          }
          for (int64_t row = 0; OB_SUCC(ret) && native && row < count; ++row) {
            Datum current;
            if (OB_FAIL(expr->read_value(*eval_ctx_, row, current))) {
            } else if (current.state() != datums_[off + row].state()
                || (!current.is_null() && current.get_int() != datums_[off + row].get_int())) {
              ret = OB_ERR_UNEXPECTED;
            }
          }
        } else if ((parameters_ != nullptr && parameters_[i].index() >= 0)
            || MEMCMP(expr->locate_batch_datums(*eval_ctx_), datums_ + off, sizeof(Datum) * count) != 0) {
          ret = OB_ERR_UNEXPECTED;
        }
        off += count;
      }
    }
  }
  reset(); // Preserve the original first-check consumption rule.
  return ret;
}

} // end namespace sql
} // end namespace oceanbase
