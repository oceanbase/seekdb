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
  char result_raw[BATCH_SIZE] = {};
  char left_raw[BATCH_SIZE] = {};
  char right_raw[BATCH_SIZE] = {};

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

  ObResBatchRawIter<char> result_iter(results, result_raw);
  ObArgBatchRawIter<char> left_iter(left_datums, left_raw);
  ObArgBatchRawIter<char> right_iter(right_datums, right_raw);
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
                                                    false,
                                                    eval_ctx));

  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    EXPECT_EQ(i, results[i].get_int());
  }
  EXPECT_EQ(BATCH_SIZE + 1, eval_ctx.get_batch_idx());
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
