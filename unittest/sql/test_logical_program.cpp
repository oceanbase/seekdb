/*
 * Copyright (c) 2026 OceanBase.
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


#include <gtest/gtest.h>
#include <array>
#include <vector>
#include "sql/engine/expr/logical_program.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace {
struct InputState {
  int64_t value = 0;
  bool null = false;
  int error = OB_SUCCESS;
  int calls = 0;
};
thread_local InputState inputs[4];
thread_local ObExpr *reentry_leaf = nullptr;
thread_local ObExpr *reentry_shared = nullptr;
int mutate_leaf_and_demand_shared(const ObExpr &, ObEvalCtx &ctx, ObDatum &result)
{
  inputs[0].value = 0;
  reentry_leaf->get_eval_info(ctx).clear_evaluated_flag();
  ObDatum *cached = nullptr;
  const int ret = reentry_shared->eval(ctx, cached);
  if (ret == OB_SUCCESS) { result.set_int(cached->get_int()); }
  return ret;
}
int invalidate_shared_and_restore_leaf(const ObExpr &, ObEvalCtx &ctx, ObDatum &result)
{
  inputs[0].value = 0;
  reentry_leaf->get_eval_info(ctx).clear_evaluated_flag();
  reentry_shared->get_eval_info(ctx).clear_evaluated_flag();
  result.set_int(1);
  return OB_SUCCESS;
}
int input_scalar(const ObExpr &expr, ObEvalCtx &, ObDatum &result)
{
  auto &input = inputs[expr.extra_];
  ++input.calls;
  if (input.error != OB_SUCCESS) { return input.error; }
  if (input.null) { result.set_null(); } else { result.set_int(input.value); }
  return OB_SUCCESS;
}
int input_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, int64_t rows)
{
  auto &input = inputs[expr.extra_];
  ObDatum *result = expr.locate_datums_for_update(ctx, rows);
  auto &flags = expr.get_evaluated_flags(ctx);
  for (int64_t row = 0; row < rows; ++row) {
    if (skip.at(row) || flags.at(row)) { continue; }
    ++input.calls;
    if (input.error != OB_SUCCESS) { return input.error; }
    if (input.null) { result[row].set_null(); } else { result[row].set_int(input.value + row % 2); }
    flags.set(row);
  }
  return OB_SUCCESS;
}
class LogicalProgramTest : public ::testing::Test {
protected:
  LogicalProgramTest() : exec_(allocator_) {}
  void SetUp() override {
    for (auto &input : inputs) { input = InputState{}; }
    reentry_leaf = nullptr;
    reentry_shared = nullptr;
    exec_.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    exec_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
    for (size_t i = 0; i < frames_.size(); ++i) {
      frames_[i] = reinterpret_cast<char *>(memory_[i].data());
      memory_[i].fill(0);
      auto &expr = exprs_[i];
      expr.type_ = T_REF_COLUMN;
      expr.datum_meta_.type_ = ObIntType;
      expr.obj_meta_.set_int();
      expr.frame_idx_ = i;
      expr.datum_off_ = 0;
      expr.eval_info_off_ = 128;
      expr.eval_flags_off_ = 256;
      expr.pvt_skip_off_ = 320;
      expr.res_buf_off_ = 512;
      expr.res_buf_len_ = 8;
      expr.batch_idx_mask_ = 0;
      expr.eval_func_ = input_scalar;
      expr.eval_batch_func_ = input_batch;
      expr.extra_ = i % 4;
    }
    exec_.set_frames(frames_.data());
    exec_.set_frame_cnt(frames_.size());
  }
  void logical(int index, ObItemType operation, std::initializer_list<int> children) {
    auto &expr = exprs_[index];
    expr.type_ = operation;
    expr.arg_cnt_ = children.size();
    args_[index].clear();
    for (int child : children) { args_[index].push_back(&exprs_[child]); }
    expr.args_ = args_[index].data();
    ASSERT_EQ(OB_SUCCESS, prepare_logical_program(expr, allocator_));
  }
  ObArenaAllocator allocator_;
  ObSQLSessionInfo session_;
  ObExecContext exec_;
  std::array<ObExpr, 8> exprs_;
  std::array<std::array<uint64_t, 256>, 8> memory_;
  std::array<char *, 8> frames_;
  std::array<std::vector<ObExpr *>, 8> args_;
};
TEST_F(LogicalProgramTest, FusedScalarControlSkipsErrorAndRebinds) {
  inputs[0].value = 0;
  inputs[1].error = OB_DIVISION_BY_ZERO;
  inputs[2].value = 1;
  logical(3, T_OP_OR, {1, 2});
  logical(4, T_OP_AND, {0, 3});
  ObEvalCtx ctx(exec_);
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, exprs_[4].eval(ctx, result));
  EXPECT_EQ(0, result->get_int());
  EXPECT_EQ(0, inputs[1].calls);
  EXPECT_EQ(0, inputs[2].calls);
  exprs_[4].get_eval_info(ctx).clear_evaluated_flag();
  exprs_[0].get_eval_info(ctx).clear_evaluated_flag();
  inputs[0].value = 1;
  EXPECT_EQ(OB_DIVISION_BY_ZERO, exprs_[4].eval(ctx, result));
  EXPECT_EQ(1, inputs[1].calls);
}
TEST_F(LogicalProgramTest, MaterializedLogicalFrontierDoesNotReadOrEvaluateChildren) {
  logical(2, T_OP_AND, {0, 1});
  logical(3, T_OP_NOT, {2});
  ObEvalCtx ctx(exec_);
  exprs_[2].locate_datum_for_write(ctx).set_int(1);
  exprs_[2].get_eval_info(ctx).projected_ = true;
  exprs_[0].locate_expr_datum(ctx).ptr_ = reinterpret_cast<char *>(1);
  exprs_[1].locate_expr_datum(ctx).ptr_ = reinterpret_cast<char *>(1);
  exprs_[0].get_eval_info(ctx).evaluated_ = true;
  exprs_[1].get_eval_info(ctx).evaluated_ = true;
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, exprs_[3].eval(ctx, result));
  EXPECT_EQ(0, result->get_int());
  EXPECT_EQ(0, inputs[0].calls);
  EXPECT_EQ(0, inputs[1].calls);
}
TEST_F(LogicalProgramTest, PublishesSharedInternalNodeForLaterIndependentDemand) {
  inputs[0].value = inputs[1].value = 1;
  inputs[2].value = 0;
  logical(3, T_OP_AND, {0, 1});
  logical(4, T_OP_OR, {3, 2});
  logical(5, T_OP_NOT, {3});
  ObEvalCtx ctx(exec_);
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, exprs_[4].eval(ctx, result));
  EXPECT_EQ(1, result->get_int());
  EXPECT_TRUE(exprs_[3].get_eval_info(ctx).evaluated_);
  exprs_[0].get_eval_info(ctx).clear_evaluated_flag();
  inputs[0].value = 0;
  ASSERT_EQ(OB_SUCCESS, exprs_[5].eval(ctx, result));
  EXPECT_EQ(0, result->get_int());
  EXPECT_EQ(1, inputs[0].calls);
  EXPECT_EQ(1, inputs[1].calls);
}
TEST_F(LogicalProgramTest, ExternalReentrySeesAlreadyCompletedSharedLogicalSnapshot) {
  inputs[0].value = inputs[1].value = 1;
  logical(3, T_OP_AND, {0, 1});
  exprs_[2].eval_func_ = mutate_leaf_and_demand_shared;
  logical(4, T_OP_AND, {3, 2});
  reentry_leaf = &exprs_[0];
  reentry_shared = &exprs_[3];
  ObEvalCtx ctx(exec_);
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, exprs_[4].eval(ctx, result));
  EXPECT_EQ(1, result->get_int());
  EXPECT_EQ(1, inputs[0].calls);
  EXPECT_EQ(1, inputs[1].calls);
  EXPECT_EQ(0, inputs[0].value);
  EXPECT_EQ(1, exprs_[3].locate_expr_datum(ctx).get_int());
}
TEST_F(LogicalProgramTest, ExternalInvalidationRefreshesLaterSharedLogicalDemand) {
  inputs[0].value = inputs[1].value = 1;
  logical(3, T_OP_AND, {0, 1});
  exprs_[2].eval_func_ = invalidate_shared_and_restore_leaf;
  logical(4, T_OP_AND, {3, 2, 3});
  reentry_leaf = &exprs_[0];
  reentry_shared = &exprs_[3];
  ObEvalCtx ctx(exec_);
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, exprs_[4].eval(ctx, result));
  EXPECT_EQ(0, result->get_int());
  EXPECT_EQ(2, inputs[0].calls);
  EXPECT_EQ(1, inputs[1].calls);
  EXPECT_EQ(0, exprs_[3].locate_expr_datum(ctx).get_int());
}
TEST_F(LogicalProgramTest, BatchMasksShortCircuitAndAllSkipAvoidsInputs) {
  inputs[0].value = 0;
  inputs[1].value = 1;
  for (int i = 0; i < 3; ++i) {
    exprs_[i].batch_result_ = true;
    exprs_[i].batch_idx_mask_ = UINT64_MAX;
  }
  logical(2, T_OP_AND, {0, 1});
  ObEvalCtx ctx(exec_);
  ObEvalCtx::BatchInfoScopeGuard batch(ctx);
  batch.set_batch_size(4);
  uint64_t skip_storage = 0;
  auto &skip = *to_bit_vector(&skip_storage);
  skip.set_all(4);
  ASSERT_EQ(OB_SUCCESS, eval_logical_program_batch(exprs_[2], ctx, skip, 4));
  EXPECT_EQ(0, inputs[0].calls);
  EXPECT_EQ(0, inputs[1].calls);
  skip.reset(4);
  skip.set(3);
  ASSERT_EQ(OB_SUCCESS, exprs_[2].eval_batch(ctx, skip, 4));
  EXPECT_EQ(3, inputs[0].calls);
  EXPECT_EQ(1, inputs[1].calls);
  EXPECT_EQ(0, exprs_[2].locate_expr_datum(ctx, 0).get_int());
  EXPECT_EQ(1, exprs_[2].locate_expr_datum(ctx, 1).get_int());
  EXPECT_EQ(0, exprs_[2].locate_expr_datum(ctx, 2).get_int());
  EXPECT_FALSE(exprs_[2].get_evaluated_flags(ctx).at(3));
}
TEST_F(LogicalProgramTest, CompletedInternalSnapshotSurvivesLaterRootFailure) {
  inputs[0].value = inputs[1].value = 1;
  inputs[2].error = OB_DIVISION_BY_ZERO;
  logical(3, T_OP_AND, {0, 1});
  logical(4, T_OP_AND, {3, 2});
  logical(5, T_OP_NOT, {3});
  ObEvalCtx ctx(exec_);
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_DIVISION_BY_ZERO, exprs_[4].eval(ctx, result));
  EXPECT_FALSE(exprs_[4].get_eval_info(ctx).evaluated_);
  EXPECT_TRUE(exprs_[3].get_eval_info(ctx).evaluated_);
  exprs_[0].get_eval_info(ctx).clear_evaluated_flag();
  inputs[0].value = 0;
  ASSERT_EQ(OB_SUCCESS, exprs_[5].eval(ctx, result));
  EXPECT_EQ(0, result->get_int());
  EXPECT_EQ(1, inputs[0].calls);
}
TEST_F(LogicalProgramTest, BatchFailureInvalidatesPreviouslyCachedRootRows) {
  inputs[0].value = 1;
  inputs[1].error = OB_DIVISION_BY_ZERO;
  for (int i = 0; i < 3; ++i) {
    exprs_[i].batch_result_ = true;
    exprs_[i].batch_idx_mask_ = UINT64_MAX;
  }
  logical(2, T_OP_AND, {0, 1});
  ObEvalCtx ctx(exec_);
  ObEvalCtx::BatchInfoScopeGuard batch(ctx);
  batch.set_batch_size(2);
  exprs_[2].locate_datum_for_write(ctx).set_int(1);
  exprs_[2].get_eval_info(ctx).evaluated_ = true;
  exprs_[2].get_evaluated_flags(ctx).set(0);
  uint64_t skip_storage = 0;
  const auto &skip = *to_bit_vector(&skip_storage);
  EXPECT_EQ(OB_DIVISION_BY_ZERO, exprs_[2].eval_batch(ctx, skip, 2));
  EXPECT_FALSE(exprs_[2].get_evaluated_flags(ctx).at(0));
  EXPECT_FALSE(exprs_[2].get_evaluated_flags(ctx).at(1));
}
TEST_F(LogicalProgramTest, ScalarLogicalDryBatchDoesNotDemandErrorLeaf) {
  inputs[0].error = OB_DIVISION_BY_ZERO;
  logical(1, T_OP_NOT, {0});
  ObEvalCtx ctx(exec_);
  ObEvalCtx::BatchInfoScopeGuard batch(ctx);
  batch.set_batch_size(4);
  uint64_t skip_storage = 0;
  auto &skip = *to_bit_vector(&skip_storage);
  skip.set_all(4);
  EXPECT_EQ(OB_SUCCESS, exprs_[1].eval_batch(ctx, skip, 4));
  EXPECT_EQ(0, inputs[0].calls);
}
TEST_F(LogicalProgramTest, WideProgramHasNoPrototypeNodeLimit) {
  constexpr int count = 300;
  std::vector<ObExpr> nodes(count + 1);
  std::vector<std::array<uint64_t, 256>> memory(count + 1);
  std::vector<char *> frames(count + 1);
  std::vector<ObExpr *> args(count);
  for (int i = 0; i <= count; ++i) {
    frames[i] = reinterpret_cast<char *>(memory[i].data());
    auto &expr = nodes[i];
    expr.type_ = T_REF_COLUMN;
    expr.datum_meta_.type_ = ObIntType;
    expr.obj_meta_.set_int();
    expr.frame_idx_ = i;
    expr.datum_off_ = 0;
    expr.eval_info_off_ = 128;
    expr.eval_flags_off_ = 256;
    expr.res_buf_off_ = 512;
    expr.res_buf_len_ = 8;
    expr.eval_func_ = input_scalar;
    expr.extra_ = 0;
    if (i < count) { args[i] = &expr; }
  }
  nodes[count].type_ = T_OP_OR;
  nodes[count].arg_cnt_ = count;
  nodes[count].args_ = args.data();
  ASSERT_EQ(OB_SUCCESS, prepare_logical_program(nodes[count], allocator_));
  exec_.set_frames(frames.data());
  exec_.set_frame_cnt(frames.size());
  {
    ObEvalCtx ctx(exec_);
    ObDatum *result = nullptr;
    ASSERT_EQ(OB_SUCCESS, nodes[count].eval(ctx, result));
    EXPECT_EQ(0, result->get_int());
    EXPECT_EQ(count, inputs[0].calls);
  }
  exec_.set_frames(frames_.data());
  exec_.set_frame_cnt(frames_.size());
}
TEST_F(LogicalProgramTest, DeepCopyAndSerializationRetainNoRuntimePointers) {
  logical(2, T_OP_AND, {0, 1});
  const auto &original = *static_cast<LogicalProgramInfo *>(exprs_[2].extra_info_);
  ObIExprExtraInfo *copied = nullptr;
  ASSERT_EQ(OB_SUCCESS, original.deep_copy(allocator_, T_OP_AND, copied));
  ASSERT_NE(nullptr, copied);
  exprs_[2].extra_info_ = copied;
  auto &info = *static_cast<LogicalProgramInfo *>(copied);
  std::vector<char> encoded(info.get_serialize_size());
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, info.serialize(encoded.data(), encoded.size(), pos));
  LogicalProgramInfo decoded(allocator_, T_OP_AND);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(encoded.data(), encoded.size(), pos));
  exprs_[2].extra_info_ = &decoded;
  inputs[0].value = inputs[1].value = 1;
  ObEvalCtx ctx(exec_);
  ObDatum *result = nullptr;
  ASSERT_EQ(OB_SUCCESS, exprs_[2].eval(ctx, result));
  EXPECT_EQ(1, result->get_int());
}
}
