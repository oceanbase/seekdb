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

#include <gtest/gtest.h>

#include "sql/engine/expr/ob_batch_eval_util.h"
#include "sql/engine/expr/ob_expr_cmp_func.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{

struct BatchIndexDatumOp : public ObArithOpBase
{
  static int datum_op(ObDatum &res,
                      const ObDatum &left,
                      const ObDatum &right,
                      ObEvalCtx &ctx)
  {
    UNUSED(left);
    UNUSED(right);
    res.set_int(ctx.get_batch_idx());
    return OB_SUCCESS;
  }
};

struct AddDatumOp : public ObArithOpBase
{
  static int datum_op(ObDatum &res,
                      const ObDatum &left,
                      const ObDatum &right,
                      ObEvalCtx &ctx)
  {
    UNUSED(ctx);
    res.set_int(left.get_int() + right.get_int());
    return OB_SUCCESS;
  }
};

TEST(ObBatchEvalUtil, datum_op_uses_current_batch_index)
{
  static constexpr int64_t BATCH_SIZE = 17;
  alignas(uint64_t) char frame[32] = {};
  alignas(uint64_t) char skip_buf[sizeof(uint64_t)] = {};
  char *frames[] = {frame};
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  ObExpr expr;
  ObDatum results[BATCH_SIZE];
  ObDatum left_datums[BATCH_SIZE];
  ObDatum right_datums[BATCH_SIZE];
  int64_t result_values[BATCH_SIZE] = {};
  int64_t left_values[BATCH_SIZE] = {};
  int64_t right_values[BATCH_SIZE] = {};

  expr.frame_idx_ = 0;
  expr.eval_flags_off_ = 0;
  expr.eval_info_off_ = sizeof(uint64_t);
  eval_ctx.frames_ = frames;
  eval_ctx.reuse(BATCH_SIZE);
  eval_ctx.set_max_batch_size(BATCH_SIZE);
  expr.get_evaluated_flags(eval_ctx).init(BATCH_SIZE);
  expr.get_eval_info(eval_ctx).flag_ = 0;
  expr.get_eval_info(eval_ctx).cnt_ = 0;
  ObBitVector *skip = to_bit_vector(skip_buf);
  skip->init(BATCH_SIZE);
  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    results[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
    left_datums[i].ptr_ = reinterpret_cast<const char *>(&left_values[i]);
    right_datums[i].ptr_ = reinterpret_cast<const char *>(&right_values[i]);
    left_datums[i].set_int(i);
    right_datums[i].set_int(i);
  }

  ObDatumVector result_iter;
  ObDatumVector left_iter;
  ObDatumVector right_iter;
  result_iter.datums_ = results;
  left_iter.datums_ = left_datums;
  right_iter.datums_ = right_datums;
  result_iter.set_batch(true);
  left_iter.set_batch(true);
  right_iter.set_batch(true);
  ObEvalCtx::BatchInfoScopeGuard outer_guard(eval_ctx);
  outer_guard.set_batch_idx(BATCH_SIZE + 1);
  ASSERT_EQ(OB_SUCCESS,
            ObDoArithBatchEval<BatchIndexDatumOp>()(expr,
                                                    eval_ctx,
                                                    *skip,
                                                    BATCH_SIZE,
                                                    result_iter,
                                                    left_iter,
                                                    right_iter,
                                                    eval_ctx));

  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    EXPECT_EQ(i, results[i].get_int());
  }
  EXPECT_EQ(BATCH_SIZE + 1, eval_ctx.get_batch_idx());
}

TEST(ObBatchEvalUtil, datum_vector_broadcasts_batch_and_scalar_operands)
{
  static constexpr int64_t BATCH_SIZE = 17;
  static constexpr int64_t SKIPPED_IDX = 2;
  static constexpr int64_t EVALUATED_IDX = 5;
  const bool operand_shapes[][2] = {
      {true, true},
      {true, false},
      {false, true},
  };
  alignas(uint64_t) char frame[32] = {};
  alignas(uint64_t) char skip_buf[sizeof(uint64_t)] = {};
  char *frames[] = {frame};
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  ObExpr expr;
  ObDatum results[BATCH_SIZE];
  ObDatum left_datums[BATCH_SIZE];
  ObDatum right_datums[BATCH_SIZE];
  int64_t result_values[BATCH_SIZE] = {};
  int64_t left_values[BATCH_SIZE] = {};
  int64_t right_values[BATCH_SIZE] = {};

  expr.frame_idx_ = 0;
  expr.eval_flags_off_ = 0;
  expr.eval_info_off_ = sizeof(uint64_t);
  eval_ctx.frames_ = frames;
  eval_ctx.set_max_batch_size(BATCH_SIZE);
  ObBitVector *skip = to_bit_vector(skip_buf);

  for (const auto &shape : operand_shapes) {
    MEMSET(frame, 0, sizeof(frame));
    MEMSET(skip_buf, 0, sizeof(skip_buf));
    eval_ctx.reuse(BATCH_SIZE);
    expr.get_evaluated_flags(eval_ctx).init(BATCH_SIZE);
    expr.get_eval_info(eval_ctx).flag_ = 0;
    expr.get_eval_info(eval_ctx).notnull_ = true;
    expr.get_eval_info(eval_ctx).cnt_ = 0;
    skip->init(BATCH_SIZE);
    skip->bit_or_assign(SKIPPED_IDX, true);
    expr.get_evaluated_flags(eval_ctx).bit_or_assign(EVALUATED_IDX, true);
    for (int64_t i = 0; i < BATCH_SIZE; ++i) {
      results[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
      left_datums[i].ptr_ = reinterpret_cast<const char *>(&left_values[i]);
      right_datums[i].ptr_ = reinterpret_cast<const char *>(&right_values[i]);
      results[i].set_int(-1);
      left_datums[i].set_int(100 + i);
      right_datums[i].set_int(1000 + i);
    }

    ObDatumVector result_iter;
    ObDatumVector left_iter;
    ObDatumVector right_iter;
    result_iter.datums_ = results;
    left_iter.datums_ = left_datums;
    right_iter.datums_ = right_datums;
    result_iter.set_batch(true);
    left_iter.set_batch(shape[0]);
    right_iter.set_batch(shape[1]);
    ASSERT_EQ(OB_SUCCESS,
              ObDoArithBatchEval<AddDatumOp>()(expr,
                                               eval_ctx,
                                               *skip,
                                               BATCH_SIZE,
                                               result_iter,
                                               left_iter,
                                               right_iter,
                                               eval_ctx));

    for (int64_t i = 0; i < BATCH_SIZE; ++i) {
      if (SKIPPED_IDX == i || EVALUATED_IDX == i) {
        EXPECT_EQ(-1, results[i].get_int());
      } else {
        const int64_t expected_left = 100 + (shape[0] ? i : 0);
        const int64_t expected_right = 1000 + (shape[1] ? i : 0);
        EXPECT_EQ(expected_left + expected_right, results[i].get_int());
        EXPECT_TRUE(expr.get_evaluated_flags(eval_ctx).at(i));
      }
    }
    EXPECT_TRUE(expr.get_eval_info(eval_ctx).notnull_);
  }
}

TEST(ObExprCmpFunc, string_initializer_shards_cover_supported_collations)
{
  const common::ObCollationType collations[] = {
      common::CS_TYPE_BINARY,
      common::CS_TYPE_UTF8MB4_GENERAL_CI,
      common::CS_TYPE_UTF8MB4_BIN,
  };
  const common::ObObjType type_pairs[][2] = {
      {common::ObVarcharType, common::ObVarcharType},
      {common::ObLongTextType, common::ObLongTextType},
      {common::ObLongTextType, common::ObVarcharType},
      {common::ObVarcharType, common::ObLongTextType},
  };
  const common::ObCollationType shard_collations[] = {
      common::CS_TYPE_GBK_CHINESE_CI,
      common::CS_TYPE_LATIN1_DANISH_CI,
      common::CS_TYPE_SJIS_JAPANESE_CI,
      common::CS_TYPE_UTF8MB4_PERSIAN_UCA_CI,
      common::CS_TYPE_UTF16_SPANISH2_UCA_CI,
      common::CS_TYPE_UTF8MB4_CS_0900_AI_CI,
      common::CS_TYPE_UTF8MB4_CS_0900_AS_CS,
      common::CS_TYPE_UTF8MB4_SR_LATN_0900_AI_CI,
  };

  for (const common::ObCollationType cs_type : collations) {
    for (int64_t pair_idx = 0; pair_idx < ARRAYSIZEOF(type_pairs); ++pair_idx) {
      const bool has_lob_header = pair_idx > 0;
      ObExpr::EvalFunc scalar_func = NULL;
      ObExpr::EvalBatchFunc batch_func = NULL;
      for (int64_t op = common::CO_EQ; op < common::CO_MAX; ++op) {
        const common::ObCmpOp cmp_op = static_cast<common::ObCmpOp>(op);
        ObExpr::EvalFunc current_scalar = ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
            type_pairs[pair_idx][0], type_pairs[pair_idx][1], 0, 0, 0, 0,
            cmp_op, cs_type, has_lob_header);
        ObExpr::EvalBatchFunc current_batch =
            ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
                type_pairs[pair_idx][0], type_pairs[pair_idx][1], 0, 0, 0, 0,
                cmp_op, cs_type, has_lob_header);
        ASSERT_NE(nullptr, current_scalar);
        ASSERT_NE(nullptr, current_batch);
        if (common::CO_EQ == cmp_op) {
          scalar_func = current_scalar;
          batch_func = current_batch;
        } else {
          EXPECT_EQ(scalar_func, current_scalar);
          EXPECT_EQ(batch_func, current_batch);
        }
      }
      EXPECT_NE(nullptr, ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
          type_pairs[pair_idx][0], type_pairs[pair_idx][1], 0, 0, 0, 0,
          cs_type, has_lob_header));
    }
  }

  for (const common::ObCollationType cs_type : shard_collations) {
    EXPECT_NE(nullptr, ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
        common::ObVarcharType, common::ObVarcharType, 0, 0, 0, 0,
        common::CO_EQ, cs_type, false));
  }
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
