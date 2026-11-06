// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>
#include <cstdlib>
#include <limits>
#include <unordered_map>
#include <vector>
#include "sql/engine/expr/expression_program.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase::sql {
extern int eval_question_mark_func(const ObExpr &, ObEvalCtx &, common::ObDatum &);
}

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {

class CountingAllocator final : public ObIAllocator
{
public:
  ~CountingAllocator() override
  {
    for (const auto &allocation : live_) { std::free(allocation.first); }
  }
  void *alloc(int64_t bytes) override
  {
    if (fail_after_ == 0 || bytes <= 0) { return nullptr; }
    if (fail_after_ > 0) { --fail_after_; }
    void *memory = std::calloc(1, bytes);
    if (memory != nullptr) { live_.emplace(memory, bytes); }
    return memory;
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *memory) override
  {
    if (memory != nullptr) {
      EXPECT_EQ(1U, live_.erase(memory));
      std::free(memory);
    }
  }
  size_t live() const { return live_.size(); }
  int fail_after_ = -1;
private:
  std::unordered_map<void *, int64_t> live_;
};

int legacy_calls = 0;
int refuse_legacy(const ObExpr &, ObEvalCtx &, ObDatum &)
{
  ++legacy_calls;
  return OB_ERR_UNEXPECTED;
}

int make_binary(ObTempExpr &temp, ObItemType operation, bool parameter = false)
{
  int ret = temp.rt_exprs_.prepare_allocate(3);
  if (ret != OB_SUCCESS) { return ret; }
  for (int i = 0; i < 3; ++i) {
    temp.rt_exprs_.at(i).datum_meta_.type_ = ObIntType;
    temp.rt_exprs_.at(i).obj_meta_.set_int();
  }
  ObExpr &left = temp.rt_exprs_.at(0);
  ObExpr &right = temp.rt_exprs_.at(1);
  ObExpr &root = temp.rt_exprs_.at(2);
  left.type_ = T_REF_COLUMN;
  right.type_ = parameter ? T_QUESTIONMARK : T_REF_COLUMN;
  if (parameter) {
    right.eval_func_ = eval_question_mark_func;
    right.extra_ = 0;
  }
  root.type_ = operation;
  root.eval_func_ = refuse_legacy;
  root.arg_cnt_ = 2;
  root.args_ = static_cast<ObExpr **>(temp.allocator_.alloc(2 * sizeof(ObExpr *)));
  if (root.args_ == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
  root.args_[0] = &left;
  root.args_[1] = &right;
  temp.expr_idx_ = 2;
  ret = temp.idx_col_arr_.prepare_allocate(parameter ? 1 : 2);
  if (ret == OB_SUCCESS) {
    temp.idx_col_arr_.at(0) = RowIdxColumnPair(0, 0);
    if (!parameter) { temp.idx_col_arr_.at(1) = RowIdxColumnPair(1, 1); }
  }
  if (ret == OB_SUCCESS) { ret = temp.prepare_native_program(); }
  return ret;
}

class TemporaryExpressionTest : public ::testing::Test
{
protected:
  TemporaryExpressionTest() : context_(query_allocator_) {}
  void SetUp() override
  {
    legacy_calls = 0;
    context_.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, context_.create_physical_plan_ctx());
    context_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
    context_.set_frames(frames_);
    context_.set_frame_cnt(0);
    cells_[0].set_int(12);
    cells_[1].set_int(5);
    row_.cells_ = cells_;
    row_.count_ = 2;
  }
  void unchanged_context()
  {
    EXPECT_EQ(frames_, context_.get_frames());
    EXPECT_EQ(0U, context_.get_frame_cnt());
    EXPECT_EQ(0, legacy_calls);
  }
  CountingAllocator template_allocator_;
  CountingAllocator query_allocator_;
  ObSQLSessionInfo session_;
  ObExecContext context_;
  char *frames_[1] = {nullptr};
  ObObj cells_[2];
  ObNewRow row_;
};

TEST_F(TemporaryExpressionTest, CompleteRowsRebindWithoutLegacyFrames)
{
  ObTempExpr temp(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(temp, T_OP_ADD));
  ASSERT_TRUE(temp.has_native_program());
  context_.set_use_temp_expr_ctx_cache(true);
  ObObj result;
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  ASSERT_EQ(17, result.get_int());
  ObObj snapshot = result;
  ObTempExprCtx *state = nullptr;
  ASSERT_EQ(OB_SUCCESS, context_.get_temp_expr_eval_ctx(temp, state));
  ASSERT_NE(nullptr, state->native_program_);
  EXPECT_EQ(nullptr, state->frames_);
  EXPECT_EQ(nullptr, state->expr_op_ctx_store_);
  cells_[0].set_null();
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  ASSERT_TRUE(result.is_null());
  cells_[0].set_int(-20);
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  EXPECT_EQ(-15, result.get_int());
  EXPECT_EQ(17, snapshot.get_int());
  ObTempExprCtx *reused = nullptr;
  ASSERT_EQ(OB_SUCCESS, context_.get_temp_expr_eval_ctx(temp, reused));
  EXPECT_EQ(state, reused);
  unchanged_context();
}

TEST_F(TemporaryExpressionTest, ParameterRebindBoundsAndFailureDoNotPublish)
{
  ObTempExpr temp(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(temp, T_OP_ADD, true));
  context_.set_use_temp_expr_ctx_cache(true);
  auto &parameters = context_.get_physical_plan_ctx()->get_param_store_for_update();
  ObObjParam parameter;
  parameter.set_int(7);
  ASSERT_EQ(OB_SUCCESS, parameters.push_back(parameter));
  ObObj result;
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  ASSERT_EQ(19, result.get_int());
  parameters.at(0).set_null();
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  ASSERT_TRUE(result.is_null());
  parameters.at(0).set_int(-30);
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  ASSERT_EQ(-18, result.get_int());
  row_.count_ = 0;
  EXPECT_NE(OB_SUCCESS, temp.eval(context_, row_, result));
  EXPECT_EQ(-18, result.get_int());
  row_.count_ = 2;
  parameters.at(0).set_varchar("bad");
  EXPECT_NE(OB_SUCCESS, temp.eval(context_, row_, result));
  EXPECT_EQ(-18, result.get_int());
  parameters.at(0).set_int(1);
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  EXPECT_EQ(13, result.get_int());
  unchanged_context();
}

TEST_F(TemporaryExpressionTest, OwnedResourcesSurviveTemplateAndReleaseOnFailures)
{
  ObTempExpr temp(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(temp, T_OP_ADD));
  const size_t before = query_allocator_.live();
  for (int fail_after = 0; fail_after < 3; ++fail_after) {
    query_allocator_.fail_after_ = fail_after;
    TemporaryExpressionProgram *program = nullptr;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
        TemporaryExpressionProgram::create(context_, temp, program));
    EXPECT_EQ(nullptr, program);
    EXPECT_EQ(before, query_allocator_.live());
  }
  query_allocator_.fail_after_ = -1;
  TemporaryExpressionProgram *program = nullptr;
  ASSERT_EQ(OB_SUCCESS, TemporaryExpressionProgram::create(context_, temp, program));
  ASSERT_NE(nullptr, program);
  ObObj result;
  ASSERT_EQ(OB_SUCCESS, program->execute(temp, row_, result));
  EXPECT_EQ(17, result.get_int());
  program->~TemporaryExpressionProgram();
  query_allocator_.free(program);
  EXPECT_EQ(before, query_allocator_.live());
  EXPECT_EQ(17, result.get_int());
  unchanged_context();
}

TEST_F(TemporaryExpressionTest, UncachedErrorsAndCancellationReleaseNativeStorage)
{
  ObTempExpr temp(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(temp, T_OP_ADD));
  context_.set_use_temp_expr_ctx_cache(false);
  ObObj result;
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  const size_t before = query_allocator_.live();
  cells_[0].set_int(INT64_MAX);
  EXPECT_EQ(OB_OPERATE_OVERFLOW, temp.eval(context_, row_, result));
  EXPECT_EQ(17, result.get_int());
  EXPECT_EQ(before, query_allocator_.live());
  cells_[0].set_int(12);
  context_.get_physical_plan_ctx()->set_timeout_timestamp(1);
  EXPECT_EQ(OB_TIMEOUT, temp.eval(context_, row_, result));
  EXPECT_EQ(before, query_allocator_.live());
  context_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
  ASSERT_EQ(OB_SUCCESS, temp.eval(context_, row_, result));
  EXPECT_EQ(17, result.get_int());
  EXPECT_EQ(before, query_allocator_.live());
  unchanged_context();
}

TEST_F(TemporaryExpressionTest, CopySerializationAndReusedTemplateAddress)
{
  context_.set_use_temp_expr_ctx_cache(true);
  alignas(ObTempExpr) unsigned char memory[sizeof(ObTempExpr)];
  auto *original = new (memory) ObTempExpr(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(*original, T_OP_ADD));
  ObObj result;
  ASSERT_EQ(OB_SUCCESS, original->eval(context_, row_, result));
  EXPECT_EQ(17, result.get_int());
  const auto old_incarnation = original->incarnation_;

  ObTempExpr *copy = nullptr;
  ASSERT_EQ(OB_SUCCESS, original->deep_copy(template_allocator_, copy));
  ASSERT_NE(nullptr, copy);
  EXPECT_NE(old_incarnation, copy->incarnation_);
  ASSERT_TRUE(copy->has_native_program());
  ASSERT_EQ(OB_SUCCESS, copy->eval(context_, row_, result));
  EXPECT_EQ(17, result.get_int());

  std::vector<char> serialized(original->get_serialize_size());
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, original->serialize(serialized.data(), serialized.size(), position));
  ASSERT_EQ(serialized.size(), position);
  ObTempExpr restored(template_allocator_);
  position = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(serialized.data(), serialized.size(), position));
  EXPECT_NE(old_incarnation, restored.incarnation_);
  ASSERT_TRUE(restored.has_native_program());
  ASSERT_EQ(OB_SUCCESS, restored.eval(context_, row_, result));
  EXPECT_EQ(17, result.get_int());

  original->~ObTempExpr();
  auto *replacement = new (memory) ObTempExpr(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(*replacement, T_OP_MINUS));
  EXPECT_NE(old_incarnation, replacement->incarnation_);
  ASSERT_EQ(OB_SUCCESS, replacement->eval(context_, row_, result));
  EXPECT_EQ(7, result.get_int());
  cells_[0].set_int(-12);
  ASSERT_EQ(OB_SUCCESS, copy->eval(context_, row_, result));
  EXPECT_EQ(-7, result.get_int());
  replacement->~ObTempExpr();
  copy->~ObTempExpr();
  template_allocator_.free(copy);
  unchanged_context();
}

TEST_F(TemporaryExpressionTest, RejectsImageAndResultMetadataDisagreement)
{
  ObTempExpr temp(template_allocator_);
  ASSERT_EQ(OB_SUCCESS, make_binary(temp, T_OP_ADD));
  ASSERT_TRUE(temp.has_native_program());
  temp.program_root_ = 0;
  temp.program_nodes_.at(0) = temp.expr_idx_;
  EXPECT_EQ(OB_INVALID_ARGUMENT, temp.validate_native_program());
  std::vector<char> serialized(temp.get_serialize_size());
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, temp.serialize(serialized.data(), serialized.size(), position));
  ObTempExpr restored(template_allocator_);
  position = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      restored.deserialize(serialized.data(), serialized.size(), position));
  EXPECT_FALSE(restored.has_native_program());
}

}
