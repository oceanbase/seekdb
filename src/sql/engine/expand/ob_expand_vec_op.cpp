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

#include "ob_expand_vec_op.h"
#include "sql/engine/ob_bit_vector.h"
#include "sql/engine/basic/ob_batch_result_holder.h"

namespace oceanbase
{
namespace sql
{
OB_SERIALIZE_MEMBER(ObExpandVecSpec::DupExprPair, org_expr_, dup_expr_);
OB_SERIALIZE_MEMBER((ObExpandVecSpec, ObOpSpec), expand_exprs_, gby_exprs_, grouping_id_expr_,
                    dup_expr_pairs_);

int ObExpandVecOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(init())) {
    LOG_WARN("init operator failed", K(ret));
  }
  return ret;
}

int ObExpandVecOp::init()
{
  int ret = OB_SUCCESS;
  void *holder_buf = allocator_.alloc(sizeof(ObBatchResultHolder));
  if (OB_ISNULL(holder_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to allocate memory", K(ret));
  } else {
    datum_holder_ = new (holder_buf) ObBatchResultHolder();
    if (OB_FAIL(datum_holder_->init(child_->get_spec().output_, eval_ctx_))) {
      LOG_WARN("init batch result holder failed", K(ret));
    }
  }
  if (OB_FAIL(ret)) {
  } else {
    reset_status();
    LOG_TRACE("expand open", K(MY_SPEC.expand_exprs_), K(MY_SPEC.gby_exprs_));
  }
  return ret;
}

int ObExpandVecOp::inner_close()
{
  int ret = OB_SUCCESS;
  destroy();
  return ret;
}

int ObExpandVecOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObOperator::inner_rescan())) {
    LOG_WARN("inner rescan failed", K(ret));
  }
  if (datum_holder_ != nullptr) {
    datum_holder_->reset();
  }
  if (OB_SUCC(ret)) {
    reset_status();
  }
  return ret;
}

int ObExpandVecOp::inner_get_next_batch(const int64_t max_row_cnt)
{
  int ret = OB_SUCCESS;
  bool do_output = false;
  while (OB_SUCC(ret) && !do_output) {
    switch (dup_status_) {
    case (DupStatus::END): {
      brs_.end_ = true;
      brs_.size_ = 0;
      do_output = true;
      break;
    }
    case DupStatus::Init: {
      const ObBatchRows *child_brs = nullptr;
      if (OB_FAIL(get_next_batch_from_child(MIN(MY_SPEC.max_batch_size_, max_row_cnt), child_brs))) {
        LOG_WARN("get next batch from child failed", K(ret));
      } else if (child_brs->end_
                 && (0 == child_brs->size_
                     || child_brs->size_ == child_brs->skip_->accumulate_bit_cnt(child_brs->size_))) {
        dup_status_ = DupStatus::END;
        brs_.end_ = true;
        brs_.size_ = 0;
        do_output = true;
      } else if (OB_FAIL(backup_child_input(child_brs))) {
        LOG_WARN("backup child input nulls failed", K(ret));
      } else if (OB_FAIL(duplicate_rollup_exprs())) {
        LOG_WARN("duplicate rollup exprs failed", K(ret));
      }
      break;
    }
    case DupStatus::ORIG_ALL: {
      if (OB_FAIL(setup_grouping_id())) {
        LOG_WARN("setup grouping id failed", K(ret));
      } else {
        copy_child_brs();
        do_output = true;
      }
      break;
    }
    case DupStatus::DUP_PARTIAL: {
      if (OB_FAIL(do_dup_partial())) {
        LOG_WARN("duplicate partial input failed", K(ret));
      } else if (OB_FAIL(setup_grouping_id())) {
        LOG_WARN("set grouping id failed", K(ret));
      } else {
        do_output = true;
      }
      break;
    }
    }
    next_status();
  }
  if (OB_SUCC(ret)) {
    clear_evaluated_flags();
  }
  if (OB_SUCC(ret) && dup_status_ == DupStatus::END) {
    if (OB_FAIL(restore_child_input())) {
      LOG_WARN("restore child input failed", K(ret));
    }
  }
  return ret;
}

int ObExpandVecOp::do_dup_partial()
{
  int ret = OB_SUCCESS;
  // if following condition matches, copy current batch of result
  //
  // For group by rollup(c1, 1, c2),
  // `1` is a remove const expr, and rollup result just as group by (c1, 1, NULL)
  //
  // if rollup expr is a const expr, just cp current batch.
  //
  // `group by c1, c2, rollup (c1, c2)`, result of rollup(c1) is same as group by (c1, NULL)
  //
  // For group by roll(c1, c1, c2), rollup result of last c1 is same as group by (c1, c1, NULL)
  // rollup result of first c1 is same as group by (NULL, NULL, NULL)
  bool is_real_static_const =
    MY_SPEC.expand_exprs_.at(expr_iter_idx_)->type_ == T_FUN_SYS_REMOVE_CONST
    && MY_SPEC.expand_exprs_.at(expr_iter_idx_)->args_[0]->is_static_const_;
  if (MY_SPEC.expand_exprs_.at(expr_iter_idx_)->is_const_expr()
      || has_exist_in_array(MY_SPEC.gby_exprs_, MY_SPEC.expand_exprs_.at(expr_iter_idx_))
      || exists_dup_expr(expr_iter_idx_)) {
    // do nothing
  } else {
    ObExpr *null_expr = MY_SPEC.expand_exprs_.at(expr_iter_idx_);
    ObDatumVector null_vec = null_expr->locate_expr_datumvector(eval_ctx_);
    for (int i = 0; i < child_input_size_; i++) {
      if (child_input_skip_->at(i)) {
      } else {
        null_vec.at(i)->set_null();
      }
    }
  }
  if (OB_SUCC(ret)) {
    copy_child_brs();
  }
  return ret;
}

int ObExpandVecOp::get_next_batch_from_child(int64_t max_row_cnt, const ObBatchRows *&child_brs)
{
  int ret = OB_SUCCESS;
  bool stop = false;
  if (OB_FAIL(restore_child_input())) {
    LOG_WARN("restore child input nulls failed", K(ret));
  }
  while (!stop && OB_SUCC(ret)) {
    clear_evaluated_flag();
    if (OB_FAIL(child_->get_next_batch(max_row_cnt, child_brs))) {
      LOG_WARN("get child next batch failed", K(ret));
    } else if (child_brs->end_) {
      stop = true;
    } else {
      stop = (child_brs->skip_->accumulate_bit_cnt(child_brs->size_) != child_brs->size_);
    }
  }
  return ret;
}

int ObExpandVecOp::backup_child_input(const ObBatchRows *child_brs)
{
  int ret = OB_SUCCESS;
  child_input_size_ = child_brs->size_;
  child_input_skip_ = child_brs->skip_;
  child_all_rows_active_ = child_brs->all_rows_active_;
  if (OB_UNLIKELY(child_brs->size_ <= 0)) {
  } else {
    if (OB_FAIL(datum_holder_->save(child_brs->size_))) {
      LOG_WARN("save result failed", K(ret));
    }
  }
  return ret;
}

int ObExpandVecOp::restore_child_input()
{
  int ret = OB_SUCCESS;
  if (child_input_size_ > 0) {
    if (OB_FAIL(datum_holder_->restore())) {
      LOG_WARN("restore results failed", K(ret));
    }
    if (OB_SUCC(ret)) {
      child_input_size_ = 0;
      child_all_rows_active_ = false;
      child_input_skip_ = nullptr;
    }
  }
  return ret;
}

int ObExpandVecOp::setup_grouping_id()
{
  int ret = OB_SUCCESS;
  ObExpr *grouping_id = MY_SPEC.grouping_id_expr_;
  int64_t seq = MY_SPEC.expand_exprs_.count() - expr_iter_idx_;
  if (OB_UNLIKELY(child_input_size_ <= 0)) {
  } else {
    ObDatum *datums = grouping_id->locate_datums_for_update(eval_ctx_, child_input_size_);
    for (int i = 0; i < child_input_size_; i++) {
      if (child_input_skip_->at(i)) {
      } else {
        datums[i].set_int(seq);
      }
    }
  }
  if (OB_SUCC(ret) && child_input_size_ > 0) {
    grouping_id->set_evaluated_projected(eval_ctx_);
  }
  return ret;
}

int ObExpandVecOp::duplicate_rollup_exprs()
{
  int ret = OB_SUCCESS;
  copy_child_brs();
  for (int i = 0; OB_SUCC(ret) && i < MY_SPEC.dup_expr_pairs_.count(); i++) {
    ObExpr *org_expr = MY_SPEC.dup_expr_pairs_.at(i).org_expr_;
    ObExpr *dup_expr = MY_SPEC.dup_expr_pairs_.at(i).dup_expr_;
    if (OB_FAIL(org_expr->eval_batch(eval_ctx_, *brs_.skip_, brs_.size_))) {
      LOG_WARN("eval batch failed", K(ret));
    } else {
      ObDatum *to_datums = dup_expr->locate_datums_for_update(eval_ctx_, brs_.size_);
      ObDatumVector src_datums = org_expr->locate_expr_datumvector(eval_ctx_);
      for (int j = 0; j < brs_.size_; j++) {
        if (brs_.skip_->at(j)) {
        } else {
          to_datums[j] = *src_datums.at(j);
        }
      }
    }
    if (OB_SUCC(ret)) { dup_expr->set_evaluated_projected(eval_ctx_); }
  }
  return ret;
}

void ObExpandVecOp::destroy()
{
  expr_iter_idx_ = -1;
  dup_status_ = DupStatus::Init;
  if (datum_holder_ != nullptr) {
    datum_holder_->reset();
    datum_holder_ = nullptr;
  }
  child_input_size_ = 0;
  child_input_skip_ = nullptr;
  child_all_rows_active_ = false;
  allocator_.reset();
}

void ObExpandVecOp::next_status()
{
  switch(dup_status_) {
  case DupStatus::Init: {
    dup_status_ = DupStatus::ORIG_ALL;
    expr_iter_idx_ = MY_SPEC.expand_exprs_.count();
    break;
  }
  case DupStatus::ORIG_ALL: {
    dup_status_ = DupStatus::DUP_PARTIAL;
    expr_iter_idx_--;
    break;
  }
  case DupStatus::DUP_PARTIAL: {
    expr_iter_idx_--;
    if (expr_iter_idx_ < 0) {
      dup_status_ = DupStatus::Init;
    }
    break;
  }
  case DupStatus::END: {
    break;
  }
  }
}

void ObExpandVecOp::clear_evaluated_flags()
{
  // we don't clear evaluated flags of expand exprs and duplicate exprs
  // expand exprs do not need re-calculation, just set null flags
  // duplicate exprs are copied once, and do not changed ever since
  for (int i = 0; i < eval_infos_.count(); i++) {
    bool is_expand_eval_info = false;
    bool is_dup_expr_eval_info = false;
    for (int j = 0; !is_expand_eval_info && j < MY_SPEC.expand_exprs_.count(); j++) {
      is_expand_eval_info = eval_infos_.at(i) == &(MY_SPEC.expand_exprs_.at(j)->get_eval_info(eval_ctx_));
    }
    for (int j = 0; !is_dup_expr_eval_info && j < MY_SPEC.dup_expr_pairs_.count(); j++) {
      is_dup_expr_eval_info =
        (eval_infos_.at(i) == &(MY_SPEC.dup_expr_pairs_.at(j).dup_expr_->get_eval_info(eval_ctx_)));
    }
    if (!is_dup_expr_eval_info && !is_expand_eval_info) {
      eval_infos_.at(i)->clear_evaluated_flag();
    }
  }
}
} // end sql
} // end oceanbase
