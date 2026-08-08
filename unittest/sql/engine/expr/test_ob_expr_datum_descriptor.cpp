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

#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{
namespace
{

int eval_unskipped_rows(const ObExpr &expr,
                        ObEvalCtx &ctx,
                        const ObBitVector &skip,
                        const int64_t size)
{
  ObDatum *datums = expr.locate_batch_datums(ctx);
  ObBitVector &evaluated_flags = expr.get_evaluated_flags(ctx);
  for (int64_t i = 0; i < size; ++i) {
    if (!skip.at(i)) {
      datums[i].set_int(i);
      evaluated_flags.set(i);
    }
  }
  return OB_SUCCESS;
}

TEST(ObExprDatumDescriptor, first_batch_eval_clears_stale_pack_for_skipped_rows)
{
  static constexpr int64_t BATCH_SIZE = 8;
  static constexpr int64_t RESULT_SIZE = sizeof(int64_t);
  static constexpr uint32_t STALE_PACK = UINT32_MAX;
  alignas(uint64_t) char frame[512] = {};
  alignas(uint64_t) char skip_buf[sizeof(uint64_t)] = {};

  const uint32_t datum_off = 0;
  const uint32_t eval_flags_off = sizeof(ObDatum) * BATCH_SIZE;
  const uint32_t eval_info_off = eval_flags_off + ObBitVector::memory_size(BATCH_SIZE);
  const uint32_t res_buf_off = (eval_info_off + sizeof(ObEvalInfo) + sizeof(uint64_t) - 1)
                               & ~(sizeof(uint64_t) - 1);
  ASSERT_LE(res_buf_off + RESULT_SIZE * BATCH_SIZE, sizeof(frame));

  ObDatum *datums = reinterpret_cast<ObDatum *>(frame + datum_off);
  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    datums[i].pack_ = STALE_PACK;
  }
  ObEvalInfo *eval_info = reinterpret_cast<ObEvalInfo *>(frame + eval_info_off);
  eval_info->flag_ = 0;
  eval_info->cnt_ = 0;
  ObBitVector *evaluated_flags = to_bit_vector(frame + eval_flags_off);
  evaluated_flags->init(BATCH_SIZE);

  ObBitVector *skip = to_bit_vector(skip_buf);
  skip->init(BATCH_SIZE);
  for (int64_t i = 1; i < BATCH_SIZE; i += 2) {
    skip->set(i);
  }

  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  char *frames[] = {frame};
  eval_ctx.frames_ = frames;
  eval_ctx.reuse(BATCH_SIZE);
  eval_ctx.set_max_batch_size(BATCH_SIZE);

  ObExpr expr;
  expr.batch_result_ = true;
  expr.batch_idx_mask_ = UINT64_MAX;
  expr.eval_batch_func_ = eval_unskipped_rows;
  expr.frame_idx_ = 0;
  expr.datum_off_ = datum_off;
  expr.eval_flags_off_ = eval_flags_off;
  expr.eval_info_off_ = eval_info_off;
  expr.res_buf_off_ = res_buf_off;
  expr.res_buf_len_ = RESULT_SIZE;

  ASSERT_EQ(OB_SUCCESS, expr.eval_batch(eval_ctx, *skip, BATCH_SIZE));
  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    EXPECT_EQ(frame + res_buf_off + RESULT_SIZE * i, datums[i].ptr_);
    if (skip->at(i)) {
      EXPECT_EQ(0U, datums[i].pack_);
    } else {
      EXPECT_EQ(sizeof(int64_t), datums[i].pack_);
      EXPECT_EQ(i, datums[i].get_int());
    }
  }
}

} // namespace
} // namespace sql
} // namespace oceanbase
