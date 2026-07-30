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
#include "sql/engine/expr/ob_datum_cast.h"
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

static constexpr int64_t DECINT_CAST_BATCH_SIZE = 70;

struct DecimalIntCastBatchFrame
{
  common::ObDatum datums_[DECINT_CAST_BATCH_SIZE];
  uint64_t eval_flags_[2];
  ObEvalInfo eval_info_;
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

TEST(ObDatumHash, fixed_double_batch_scales_match_scalar_hash)
{
  static constexpr int64_t BATCH_SIZE = 129;
  const double sample_values[] = {
      0.0,
      -0.0,
      1.234567890123456,
      -9.876543210987654,
      0.000000123456789,
      999999999999.125,
      -0.5,
  };
  double double_values[BATCH_SIZE];
  common::ObDatum datums[BATCH_SIZE];
  for (int64_t i = 0; i < BATCH_SIZE; ++i) {
    double_values[i] = sample_values[i % ARRAYSIZEOF(sample_values)];
    datums[i].ptr_ = reinterpret_cast<const char *>(&double_values[i]);
    datums[i].set_double(double_values[i]);
  }
  datums[65].set_null();

  for (int64_t scale = 0; scale < common::OB_NOT_FIXED_SCALE; ++scale) {
    SCOPED_TRACE(scale);
    ObExprBasicFuncs *basic_funcs = common::ObDatumFuncs::get_basic_func(
        common::ObDoubleType, common::CS_TYPE_BINARY, static_cast<common::ObScale>(scale));
    ASSERT_NE(nullptr, basic_funcs);
    verify_batch_hash_shapes(*basic_funcs, datums, BATCH_SIZE);
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

TEST(ObDatumCast, decimal_int_fast_cast_reuses_runtime_variants)
{
  ObExpr::EvalFunc scalar_up_implicit = nullptr;
  ObExpr::EvalFunc scalar_up_explicit = nullptr;
  ObExpr::EvalFunc scalar_down = nullptr;
  ObExpr::EvalBatchFunc batch_up_implicit = nullptr;
  ObExpr::EvalBatchFunc batch_up_explicit = nullptr;
  ObExpr::EvalBatchFunc batch_down = nullptr;

  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 2, 18, 4, false,
                               batch_up_implicit, scalar_up_implicit);
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 2, 18, 4, true,
                               batch_up_explicit, scalar_up_explicit);
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 4, 18, 2, false,
                               batch_down, scalar_down);

  EXPECT_EQ(scalar_up_implicit, scalar_up_explicit);
  EXPECT_EQ(batch_up_implicit, batch_up_explicit);
  EXPECT_EQ(batch_up_implicit, batch_down);
  EXPECT_NE(scalar_up_implicit, scalar_down);

  ObExpr::EvalFunc scalar_int32 = nullptr;
  ObExpr::EvalFunc scalar_int64 = nullptr;
  ObExpr::EvalBatchFunc batch_int32 = nullptr;
  ObExpr::EvalBatchFunc batch_int64 = nullptr;
  ObDatumCast::get_decint_cast(common::ObIntTC, 9, 0, 38, 2, false,
                               batch_int32, scalar_int32);
  ObDatumCast::get_decint_cast(common::ObIntTC, 18, 0, 38, 2, false,
                               batch_int64, scalar_int64);

  EXPECT_EQ(batch_int32, batch_int64);
  EXPECT_NE(nullptr, scalar_int32);
  EXPECT_NE(nullptr, scalar_int64);
}

TEST(ObDatumCast, decimal_int_fast_scalar_uses_runtime_explicit_flag)
{
  static common::ObArenaAllocator decint_const_allocator;
  static const int decint_const_init_ret =
      common::wide::ObDecimalIntConstValue::init_const_values(decint_const_allocator);
  ASSERT_EQ(OB_SUCCESS, decint_const_init_ret);

  struct ScalarFrame
  {
    common::ObDatum datum_;
    ObEvalInfo eval_info_;
    alignas(64) char value_[64];
  } child_frame{};
  char *frames[] = {reinterpret_cast<char *>(&child_frame)};
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;

  ObExpr child;
  child.frame_idx_ = 0;
  child.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&child_frame.datum_) - frames[0]);
  child.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&child_frame.eval_info_) - frames[0]);
  child_frame.eval_info_.flag_ = 0;
  child_frame.eval_info_.cnt_ = 0;
  child_frame.datum_.ptr_ = child_frame.value_;

  ObExpr expr;
  ObExpr *args[] = {&child};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);

  auto eval_scalar = [&](ObExpr::EvalFunc eval_func, const uint64_t cast_mode,
                         const int32_t input, common::ObDatum &result) {
    child_frame.datum_.ptr_ = child_frame.value_;
    child_frame.datum_.set_decimal_int(
        reinterpret_cast<const common::ObDecimalInt *>(&input), sizeof(input));
    expr.extra_ = cast_mode;
    return eval_func(expr, eval_ctx, result);
  };
  alignas(64) char result_value[64] = {};
  common::ObDatum result;
  ObExpr::EvalBatchFunc batch_func = nullptr;
  ObExpr::EvalFunc scalar_func = nullptr;

  child.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY, 0, 3);
  expr.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY, 0, 2);
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 3, 0, 2, 0, false,
                               batch_func, scalar_func);
  ASSERT_NE(nullptr, scalar_func);
  result.ptr_ = result_value;
  ASSERT_EQ(OB_SUCCESS, eval_scalar(scalar_func, CM_WARN_ON_FAIL, 999, result));
  EXPECT_EQ(999, result.get_decimal_int32());

  result.ptr_ = result_value;
  ASSERT_EQ(OB_SUCCESS,
            eval_scalar(scalar_func, CM_EXPLICIT_CAST | CM_WARN_ON_FAIL, 999, result));
  EXPECT_EQ(99, result.get_decimal_int32());

  child.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY, 2, 9);
  expr.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY, 4, 9);
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 2, 9, 4, false,
                               batch_func, scalar_func);
  ASSERT_NE(nullptr, scalar_func);
  result.ptr_ = result_value;
  ASSERT_EQ(OB_SUCCESS, eval_scalar(scalar_func, CM_NONE, 12345, result));
  EXPECT_EQ(1234500, result.get_decimal_int32());

  child.datum_meta_.scale_ = 4;
  expr.datum_meta_.scale_ = 2;
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 4, 9, 2, false,
                               batch_func, scalar_func);
  ASSERT_NE(nullptr, scalar_func);
  result.ptr_ = result_value;
  ASSERT_EQ(OB_SUCCESS, eval_scalar(scalar_func, CM_NONE, -123400, result));
  EXPECT_EQ(-1234, result.get_decimal_int32());

  child_frame.datum_.set_null();
  result.ptr_ = result_value;
  result.set_int(1);
  ASSERT_EQ(OB_SUCCESS, scalar_func(expr, eval_ctx, result));
  EXPECT_TRUE(result.is_null());
}

TEST(ObDatumCast, decimal_int_fast_batch_uses_runtime_scale_direction)
{
  static constexpr int64_t SKIPPED_IDX = 2;
  static constexpr int64_t HIGH_SKIPPED_IDX = 65;
  static constexpr int64_t EVALUATED_IDX = 5;
  static constexpr int64_t NULL_IDX = 66;
  DecimalIntCastBatchFrame child_frame{};
  DecimalIntCastBatchFrame result_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&child_frame),
      reinterpret_cast<char *>(&result_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;
  eval_ctx.reuse(DECINT_CAST_BATCH_SIZE);
  eval_ctx.set_max_batch_size(DECINT_CAST_BATCH_SIZE);

  ObExpr child;
  child.frame_idx_ = 0;
  child.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&child_frame.datums_) - frames[0]);
  child.eval_flags_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&child_frame.eval_flags_) - frames[0]);
  child.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&child_frame.eval_info_) - frames[0]);
  child.batch_result_ = true;
  child.batch_idx_mask_ = UINT64_MAX;
  child_frame.eval_info_.flag_ = 0;
  child_frame.eval_info_.cnt_ = 0;

  ObExpr expr;
  ObExpr *args[] = {&child};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);
  expr.frame_idx_ = 1;
  expr.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&result_frame.datums_) - frames[1]);
  expr.eval_flags_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&result_frame.eval_flags_) - frames[1]);
  expr.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&result_frame.eval_info_) - frames[1]);
  expr.batch_result_ = true;
  expr.batch_idx_mask_ = UINT64_MAX;
  expr.extra_ = CM_NONE;
  result_frame.eval_info_.flag_ = 0;
  result_frame.eval_info_.cnt_ = 0;

  int32_t input_values[DECINT_CAST_BATCH_SIZE] = {};
  int32_t result_values[DECINT_CAST_BATCH_SIZE] = {};
  alignas(uint64_t) uint64_t skip_buf[2] = {};
  ObBitVector *skip = to_bit_vector(skip_buf);
  ASSERT_EQ(sizeof(skip_buf), ObBitVector::memory_size(DECINT_CAST_BATCH_SIZE));
  ASSERT_EQ(sizeof(result_frame.eval_flags_),
            ObBitVector::memory_size(DECINT_CAST_BATCH_SIZE));
  skip->init(DECINT_CAST_BATCH_SIZE);
  skip->set(SKIPPED_IDX);
  skip->set(HIGH_SKIPPED_IDX);
  for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
    child_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&input_values[i]);
    result_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
  }

  auto prepare_batch = [&](const int32_t scale_factor) {
    expr.get_evaluated_flags(eval_ctx).init(DECINT_CAST_BATCH_SIZE);
    expr.get_evaluated_flags(eval_ctx).set(EVALUATED_IDX);
    for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
      input_values[i] = static_cast<int32_t>(i - DECINT_CAST_BATCH_SIZE / 2) * scale_factor;
      child_frame.datums_[i].set_decimal_int(
          reinterpret_cast<const common::ObDecimalInt *>(&input_values[i]), sizeof(input_values[i]));
      result_values[i] = 777;
      result_frame.datums_[i].set_decimal_int(
          reinterpret_cast<const common::ObDecimalInt *>(&result_values[i]), sizeof(result_values[i]));
    }
    child_frame.datums_[NULL_IDX].set_null();
  };

  ObExpr::EvalBatchFunc batch_func = nullptr;
  ObExpr::EvalFunc scalar_func = nullptr;
  child.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY, 2, 9);
  expr.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY, 4, 9);
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 2, 9, 4, false,
                               batch_func, scalar_func);
  ASSERT_NE(nullptr, batch_func);
  prepare_batch(1);
  ASSERT_EQ(OB_SUCCESS, batch_func(expr, eval_ctx, *skip, DECINT_CAST_BATCH_SIZE));
  for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
    if (SKIPPED_IDX == i || HIGH_SKIPPED_IDX == i || EVALUATED_IDX == i) {
      EXPECT_EQ(777, result_frame.datums_[i].get_decimal_int32());
    } else if (NULL_IDX == i) {
      EXPECT_TRUE(result_frame.datums_[i].is_null());
    } else {
      EXPECT_EQ(input_values[i] * 100, result_frame.datums_[i].get_decimal_int32());
    }
    EXPECT_EQ(SKIPPED_IDX != i && HIGH_SKIPPED_IDX != i,
              expr.get_evaluated_flags(eval_ctx).at(i));
  }

  ObExpr::EvalBatchFunc down_batch_func = nullptr;
  child.datum_meta_.scale_ = 4;
  expr.datum_meta_.scale_ = 2;
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 4, 9, 2, false,
                               down_batch_func, scalar_func);
  ASSERT_NE(nullptr, down_batch_func);
  EXPECT_EQ(batch_func, down_batch_func);
  prepare_batch(100);
  ASSERT_EQ(OB_SUCCESS, down_batch_func(expr, eval_ctx, *skip, DECINT_CAST_BATCH_SIZE));
  for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
    if (SKIPPED_IDX == i || HIGH_SKIPPED_IDX == i || EVALUATED_IDX == i) {
      EXPECT_EQ(777, result_frame.datums_[i].get_decimal_int32());
    } else if (NULL_IDX == i) {
      EXPECT_TRUE(result_frame.datums_[i].is_null());
    } else {
      EXPECT_EQ(input_values[i] / 100, result_frame.datums_[i].get_decimal_int32());
    }
    EXPECT_EQ(SKIPPED_IDX != i && HIGH_SKIPPED_IDX != i,
              expr.get_evaluated_flags(eval_ctx).at(i));
  }
}

} // namespace sql
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
