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

#include "ob_basic_nested_loop_join_op.h"
#include "sql/engine/join/ob_nested_loop_join_op.h"
#include "sql/engine/ob_physical_plan.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

OB_SERIALIZE_MEMBER((ObBasicNestedLoopJoinSpec, ObJoinSpec),
                    rescan_params_,
                    gi_partition_id_expr_,
                    enable_gi_partition_pruning_,
                    enable_px_batch_rescan_);

ObBasicNestedLoopJoinOp::ObBasicNestedLoopJoinOp(ObExecContext &exec_ctx,
                                                 const ObOpSpec &spec,
                                                 ObOpInput *input)
  : ObJoinOp(exec_ctx, spec, input)
  {}

int ObBasicNestedLoopJoinOp::inner_open()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObJoinOp::inner_open())) {
  }
  return ret;
}

int ObBasicNestedLoopJoinOp::inner_rescan()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObJoinOp::inner_rescan())) {
  }
  return ret;
}

int ObBasicNestedLoopJoinOp::inner_close()
{
  return OB_SUCCESS;
}


int ObBasicNestedLoopJoinOp::get_next_left_row()
{
  if (!get_spec().rescan_params_.empty()) {
    // Reset exec param before get left row, because the exec param still reference
    // to the previous row, when get next left row, it may become wild pointer.
    // The exec parameter may be accessed by the under PX execution by serialization, which
    // serialize whole parameters store.
    set_param_null();
  }
  return ObJoinOp::get_next_left_row();
}

int ObBasicNestedLoopJoinOp::prepare_rescan_params(bool is_group)
{
  int ret = OB_SUCCESS;
  int64_t param_cnt = get_spec().rescan_params_.count();
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx_);
  CK(OB_NOT_NULL(plan_ctx));
  ObBatchRescanCtl *batch_rescan_ctl = (get_spec().enable_px_batch_rescan_ && is_group)
      ? &static_cast<ObNestedLoopJoinOp*>(this)->batch_rescan_ctl_ : NULL;
  if (batch_rescan_ctl == nullptr) {
    for (int64_t i = 0; OB_SUCC(ret) && i < param_cnt; ++i) {
      ret = get_spec().rescan_params_.at(i).set_dynamic_param(eval_ctx_);
    }
  } else {
    ObBatchRescanParams::RowBuilder builder(batch_rescan_ctl->params_);
    common::ObSEArray<int64_t, 8> param_idxs, param_expr_idxs;
    for (int64_t i = 0; OB_SUCC(ret) && i < param_cnt; ++i) {
      const ObDynamicParamSetter &setter = get_spec().rescan_params_.at(i);
      DatumParameter value;
      int64_t expr_idx = 0;
      OZ(setter.set_dynamic_param(eval_ctx_, value));
      OZ(builder.append(value));
      OZ(param_idxs.push_back(setter.param_idx_));
      CK(OB_NOT_NULL(plan_ctx->get_phy_plan()));
      OZ(plan_ctx->get_phy_plan()->get_expr_frame_info().get_expr_idx_in_frame(setter.dst_, expr_idx));
      OZ(param_expr_idxs.push_back(expr_idx));
    }
    if (OB_SUCC(ret)) {
      batch_rescan_ctl->param_version_ += 1;
      ret = builder.finish(param_idxs, param_expr_idxs);
    }
  }
  // After each row comes out from the left side, notify the right-side GI to implement part id filtering, avoid unnecessary partition scanning in PKEY NLJ scenario
  if (OB_SUCC(ret) && !get_spec().enable_px_batch_rescan_ && get_spec().enable_gi_partition_pruning_) {
    ObDatum *datum = nullptr;
    if (OB_FAIL(get_spec().gi_partition_id_expr_->eval(eval_ctx_, datum))) {
    } else {
      // NOTE: If the right side corresponds to multiple tables, the logic here is also correct
      // Like A REPART TO NLJ (B JOIN C) scenario
      // At this time, GI is above B and C
      int64_t part_id = datum->get_int();
      ctx_.get_gi_pruning_info().set_part_id(part_id);
    }
  }

  return ret;
}

void ObBasicNestedLoopJoinOp::set_param_null()
{
  set_pushdown_param_null(get_spec().rescan_params_);
}

} // end namespace sql
} // end namespace oceanbase
