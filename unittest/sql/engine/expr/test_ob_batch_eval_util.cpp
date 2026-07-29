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

#include <algorithm>
#include <vector>

#include "sql/engine/expr/ob_batch_eval_util.h"
#include "sql/engine/expr/ob_expr_cmp_func.h"
#include "sql/engine/expr/ob_expr_basic_funcs.h"
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

struct BatchHashFuncPair
{
  ObExprHashFuncType scalar_func_;
  ObBatchDatumHashFunc batch_func_;
};

static void verify_batch_hash_shapes(const ObExprBasicFuncs &basic_funcs,
                                     common::ObDatum *datums,
                                     const int64_t batch_size)
{
  static constexpr uint64_t HASH_SENTINEL = 0xDEADBEEFDEADBEEFULL;
  const BatchHashFuncPair hash_funcs[] = {
      {basic_funcs.default_hash_, basic_funcs.default_hash_batch_},
      {basic_funcs.murmur_hash_, basic_funcs.murmur_hash_batch_},
      {basic_funcs.xx_hash_, basic_funcs.xx_hash_batch_},
      {basic_funcs.wy_hash_, basic_funcs.wy_hash_batch_},
      {basic_funcs.murmur_hash_v2_, basic_funcs.murmur_hash_v2_batch_},
  };
  const bool shapes[][2] = {
      {false, false},
      {false, true},
      {true, false},
      {true, true},
  };
  std::vector<uint64_t> skip_buf(ObBitVector::word_count(batch_size));
  ObBitVector *skip = to_bit_vector(skip_buf.data());
  std::vector<uint64_t> seeds(batch_size);
  std::vector<uint64_t> hash_values(batch_size);
  std::vector<uint64_t> expected(batch_size);
  for (int64_t i = 0; i < batch_size; ++i) {
    seeds[i] = 0x9E3779B97F4A7C15ULL + static_cast<uint64_t>(i * 17);
  }

  for (const BatchHashFuncPair &hash_func : hash_funcs) {
    ASSERT_NE(nullptr, hash_func.scalar_func_);
    ASSERT_NE(nullptr, hash_func.batch_func_);

    skip->init(batch_size);
    skip->set_all(batch_size);
    hash_func.batch_func_(nullptr, nullptr, false, *skip, batch_size, nullptr, false);
    hash_func.batch_func_(nullptr, nullptr, false, *skip, 0, nullptr, false);

    skip->init(batch_size);
    const int64_t skipped_rows[] = {0, 2, 63, 64, 127};
    for (const int64_t idx : skipped_rows) {
      if (idx < batch_size) {
        skip->set(idx);
      }
    }
    for (const auto &shape : shapes) {
      std::fill(hash_values.begin(), hash_values.end(), HASH_SENTINEL);
      hash_func.batch_func_(hash_values.data(), datums, shape[0], *skip, batch_size,
                            seeds.data(), shape[1]);
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip->at(i)) {
          EXPECT_EQ(HASH_SENTINEL, hash_values[i]);
        } else {
          const int64_t datum_idx = shape[0] ? i : 0;
          const int64_t seed_idx = shape[1] ? i : 0;
          uint64_t scalar_hash = 0;
          ASSERT_EQ(OB_SUCCESS,
                    hash_func.scalar_func_(datums[datum_idx], seeds[seed_idx], scalar_hash));
          EXPECT_EQ(scalar_hash, hash_values[i]);
        }
      }
    }

    std::vector<uint64_t> in_place_hashes = seeds;
    expected.assign(batch_size, HASH_SENTINEL);
    for (int64_t i = 0; i < batch_size; ++i) {
      if (!skip->at(i)) {
        ASSERT_EQ(OB_SUCCESS,
                  hash_func.scalar_func_(datums[i], in_place_hashes[i], expected[i]));
      }
    }
    hash_func.batch_func_(in_place_hashes.data(), datums, true, *skip, batch_size,
                          in_place_hashes.data(), true);
    for (int64_t i = 0; i < batch_size; ++i) {
      EXPECT_EQ(skip->at(i) ? seeds[i] : expected[i], in_place_hashes[i]);
    }

    skip->init(batch_size);
    in_place_hashes = seeds;
    expected.assign(batch_size, HASH_SENTINEL);
    uint64_t scalar_seed = seeds[0];
    for (int64_t i = 0; i < batch_size; ++i) {
      ASSERT_EQ(OB_SUCCESS, hash_func.scalar_func_(datums[i], scalar_seed, expected[i]));
      if (0 == i) {
        scalar_seed = expected[i];
      }
    }
    hash_func.batch_func_(in_place_hashes.data(), datums, true, *skip, batch_size,
                          in_place_hashes.data(), false);
    for (int64_t i = 0; i < batch_size; ++i) {
      EXPECT_EQ(expected[i], in_place_hashes[i]);
    }
  }
}

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

TEST(ObDatumHash, batch_shapes_match_scalar_hash)
{
  static constexpr int64_t BATCH_SIZE = 129;
  int64_t int_values[BATCH_SIZE];
  common::ObDatum int_datums[BATCH_SIZE];
  common::ObDatum string_datums[BATCH_SIZE];
  const char *string_values[] = {"a", "OceanBase", "hash value", "trailing spaces   "};
  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    int_values[i] = 1000 + i;
    int_datums[i].ptr_ = reinterpret_cast<const char *>(&int_values[i]);
    int_datums[i].set_int(int_values[i]);
    string_datums[i].set_string(common::ObString::make_string(
        string_values[i % ARRAYSIZEOF(string_values)]));
  }
  int_datums[65].set_null();

  ObExprBasicFuncs *int_funcs = common::ObDatumFuncs::get_basic_func(
      common::ObIntType, common::CS_TYPE_BINARY);
  ObExprBasicFuncs *general_ci_funcs = common::ObDatumFuncs::get_basic_func(
      common::ObVarcharType, common::CS_TYPE_UTF8MB4_GENERAL_CI,
      common::SCALE_UNKNOWN_YET, false);
  ObExprBasicFuncs *utf8_bin_funcs = common::ObDatumFuncs::get_basic_func(
      common::ObVarcharType, common::CS_TYPE_UTF8MB4_BIN,
      common::SCALE_UNKNOWN_YET, false);
  ASSERT_NE(nullptr, int_funcs);
  ASSERT_NE(nullptr, general_ci_funcs);
  ASSERT_NE(nullptr, utf8_bin_funcs);

  verify_batch_hash_shapes(*int_funcs, int_datums, BATCH_SIZE);
  verify_batch_hash_shapes(*general_ci_funcs, string_datums, BATCH_SIZE);
  string_datums[65].set_null();
  verify_batch_hash_shapes(*utf8_bin_funcs, string_datums, BATCH_SIZE);
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
