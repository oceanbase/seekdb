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
#include <cmath>
#include <limits>
#include <vector>

#include "sql/engine/expr/ob_batch_eval_util.h"
#include "sql/engine/expr/ob_datum_cast.h"
#include "sql/engine/expr/ob_expr_cmp_func.h"
#include "sql/engine/ob_exec_context.h"

namespace oceanbase
{
namespace sql
{

extern ObExpr::EvalFunc EVAL_TC_CMP_FUNCS[common::ObMaxTC][common::ObMaxTC][common::CO_MAX];
extern ObExpr::EvalBatchFunc
    EVAL_BATCH_TC_CMP_FUNCS[common::ObMaxTC][common::ObMaxTC][common::CO_MAX];
extern common::ObDatumCmpFuncType
    DATUM_TC_CMP_FUNCS[common::ObMaxTC][common::ObMaxTC];

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
  alignas(64) char values_[DECINT_CAST_BATCH_SIZE][sizeof(common::int512_t)];
};

static constexpr int64_t FIXED_DOUBLE_CMP_BATCH_SIZE = 17;

struct FixedDoubleCmpFrame
{
  common::ObDatum datums_[FIXED_DOUBLE_CMP_BATCH_SIZE];
  uint64_t eval_flags_[1];
  uint64_t pvt_skip_[1];
  ObEvalInfo eval_info_;
};

struct FixedDoubleCmpScalarFrame
{
  common::ObDatum datum_;
  ObEvalInfo eval_info_;
  double value_;
};

static const common::ObCmpOp FIXED_DOUBLE_CMP_OPS[] = {
    common::CO_EQ,
    common::CO_LE,
    common::CO_LT,
    common::CO_GE,
    common::CO_GT,
    common::CO_NE,
};

static const ObExprOperatorType FIXED_DOUBLE_CMP_EXPR_TYPES[] = {
    T_OP_EQ,
    T_OP_LE,
    T_OP_LT,
    T_OP_GE,
    T_OP_GT,
    T_OP_NE,
};

static const common::ObPrecision DECIMAL_INT_CMP_PRECISIONS[] = {
    common::MAX_PRECISION_DECIMAL_INT_32,
    common::MAX_PRECISION_DECIMAL_INT_64,
    common::MAX_PRECISION_DECIMAL_INT_128,
    common::MAX_PRECISION_DECIMAL_INT_256,
    common::MAX_PRECISION_DECIMAL_INT_512,
};

static double get_fixed_double_tolerance(const common::ObScale scale)
{
  double tolerance = 0;
  switch (scale) {
    case 0:
      tolerance = 5.0 / 1e001;
      break;
    case 2:
      tolerance = 5.0 / 1e003;
      break;
    case 15:
      tolerance = 5.0 / 1e016;
      break;
    case 30:
      tolerance = 5.0 / 1e031;
      break;
    default:
      ADD_FAILURE() << "unexpected fixed-double scale " << scale;
      break;
  }
  return tolerance;
}

static int64_t get_expected_cmp_result(const common::ObCmpOp cmp_op, const int cmp_ret)
{
  int64_t result = 0;
  switch (cmp_op) {
    case common::CO_EQ:
      result = 0 == cmp_ret;
      break;
    case common::CO_LE:
      result = cmp_ret <= 0;
      break;
    case common::CO_LT:
      result = cmp_ret < 0;
      break;
    case common::CO_GE:
      result = cmp_ret >= 0;
      break;
    case common::CO_GT:
      result = cmp_ret > 0;
      break;
    case common::CO_NE:
      result = 0 != cmp_ret;
      break;
    default:
      ADD_FAILURE() << "unexpected comparison operation " << cmp_op;
      break;
  }
  return result;
}

static void set_tc_cmp_value(common::ObDatum &datum,
                             const common::ObObjType type,
                             const int64_t value)
{
  switch (type) {
    case common::ObNullType:
      datum.set_null();
      break;
    case common::ObUInt64Type:
      datum.set_uint(static_cast<uint64_t>(value));
      break;
    case common::ObFloatType:
      datum.set_float(static_cast<float>(value));
      break;
    case common::ObDateType:
      datum.set_date(static_cast<int32_t>(value));
      break;
    default:
      datum.set_int(value);
      break;
  }
}

static void init_fixed_double_batch_expr(ObExpr &expr,
                                         const int64_t frame_idx,
                                         FixedDoubleCmpFrame &frame,
                                         char *frame_ptr)
{
  expr.frame_idx_ = static_cast<uint32_t>(frame_idx);
  expr.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.datums_) - frame_ptr);
  expr.eval_flags_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.eval_flags_) - frame_ptr);
  expr.pvt_skip_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.pvt_skip_) - frame_ptr);
  expr.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.eval_info_) - frame_ptr);
  expr.batch_result_ = true;
  expr.batch_idx_mask_ = UINT64_MAX;
  frame.eval_info_.flag_ = 0;
  frame.eval_info_.cnt_ = 0;
}

static void set_decimal_int_value(common::ObDatum &datum,
                                  const common::ObPrecision precision,
                                  const int64_t value)
{
  switch (common::get_decimalint_type(precision)) {
    case common::DECIMAL_INT_32:
      datum.set_decimal_int(static_cast<int32_t>(value));
      break;
    case common::DECIMAL_INT_64:
      datum.set_decimal_int(value);
      break;
    case common::DECIMAL_INT_128: {
      const common::int128_t wide_value(value);
      datum.set_decimal_int(wide_value);
      break;
    }
    case common::DECIMAL_INT_256: {
      const common::int256_t wide_value(value);
      datum.set_decimal_int(wide_value);
      break;
    }
    case common::DECIMAL_INT_512: {
      const common::int512_t wide_value(value);
      datum.set_decimal_int(wide_value);
      break;
    }
    default:
      ADD_FAILURE() << "unexpected decimal-int precision " << precision;
      datum.set_null();
      break;
  }
}

static int64_t get_decimal_int_value(const common::ObDatum &datum,
                                     const common::ObPrecision precision)
{
  int64_t value = 0;
  switch (common::get_decimalint_type(precision)) {
    case common::DECIMAL_INT_32:
      value = datum.get_decimal_int32();
      break;
    case common::DECIMAL_INT_64:
      value = datum.get_decimal_int64();
      break;
    case common::DECIMAL_INT_128:
      value = static_cast<int64_t>(datum.get_decimal_int128());
      break;
    case common::DECIMAL_INT_256:
      value = static_cast<int64_t>(datum.get_decimal_int256());
      break;
    case common::DECIMAL_INT_512:
      value = static_cast<int64_t>(datum.get_decimal_int512());
      break;
    default:
      ADD_FAILURE() << "unexpected decimal-int precision " << precision;
      break;
  }
  return value;
}

static void init_decimal_int_batch_expr(ObExpr &expr,
                                        const int64_t frame_idx,
                                        DecimalIntCastBatchFrame &frame,
                                        char *frame_ptr)
{
  expr.frame_idx_ = static_cast<uint32_t>(frame_idx);
  expr.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.datums_) - frame_ptr);
  expr.eval_flags_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.eval_flags_) - frame_ptr);
  expr.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&frame.eval_info_) - frame_ptr);
  expr.batch_result_ = true;
  expr.batch_idx_mask_ = UINT64_MAX;
  frame.eval_info_.flag_ = 0;
  frame.eval_info_.cnt_ = 0;
}

struct DecimalIntBatchCastContext
{
  DecimalIntBatchCastContext()
      : frames_{reinterpret_cast<char *>(&child_frame_),
                reinterpret_cast<char *>(&result_frame_)},
        exec_ctx_(allocator_),
        eval_ctx_(exec_ctx_),
        skip_(to_bit_vector(skip_buf_))
  {
    eval_ctx_.frames_ = frames_;
    eval_ctx_.reuse(DECINT_CAST_BATCH_SIZE);
    eval_ctx_.set_max_batch_size(DECINT_CAST_BATCH_SIZE);
    init_decimal_int_batch_expr(child_, 0, child_frame_, frames_[0]);
    init_decimal_int_batch_expr(expr_, 1, result_frame_, frames_[1]);
    args_[0] = &child_;
    expr_.args_ = args_;
    expr_.arg_cnt_ = ARRAYSIZEOF(args_);
    for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
      child_frame_.datums_[i].ptr_ = child_frame_.values_[i];
      result_frame_.datums_[i].ptr_ = result_frame_.values_[i];
    }
  }

  void configure(const common::ObPrecision in_precision,
                 const common::ObScale in_scale,
                 const common::ObPrecision out_precision,
                 const common::ObScale out_scale,
                 const uint64_t cast_mode)
  {
    child_.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY,
                                    in_scale, in_precision);
    expr_.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY,
                                   out_scale, out_precision);
    expr_.extra_ = cast_mode;
  }

  void reset(const int64_t batch_size,
             const common::ObPrecision out_precision,
             const int64_t result_sentinel)
  {
    skip_->init(batch_size);
    expr_.get_evaluated_flags(eval_ctx_).init(batch_size);
    for (int64_t i = 0; i < batch_size; ++i) {
      child_frame_.datums_[i].ptr_ = child_frame_.values_[i];
      result_frame_.datums_[i].ptr_ = result_frame_.values_[i];
      set_decimal_int_value(result_frame_.datums_[i], out_precision, result_sentinel);
    }
  }

  template <typename T>
  void set_input(const int64_t idx, const T &value)
  {
    child_frame_.datums_[idx].ptr_ = child_frame_.values_[idx];
    child_frame_.datums_[idx].set_decimal_int(value);
  }

  DecimalIntCastBatchFrame child_frame_{};
  DecimalIntCastBatchFrame result_frame_{};
  char *frames_[2];
  common::ObArenaAllocator allocator_;
  ObExecContext exec_ctx_;
  ObEvalCtx eval_ctx_;
  ObExpr child_;
  ObExpr expr_;
  ObExpr *args_[1];
  alignas(uint64_t) uint64_t skip_buf_[2]{};
  ObBitVector *skip_;
};

static int init_decimal_int_const_values()
{
  static common::ObArenaAllocator allocator;
  static const int ret = common::wide::ObDecimalIntConstValue::init_const_values(allocator);
  return ret;
}

static int64_t round_decimal_int_down(const int64_t value, const int64_t scale_factor)
{
  const bool is_negative = value < 0;
  int64_t absolute = is_negative ? -value : value;
  const int64_t remainder = absolute % scale_factor;
  absolute /= scale_factor;
  if (remainder >= scale_factor / 2) {
    ++absolute;
  }
  return is_negative ? -absolute : absolute;
}

static void verify_batch_hash_shapes(const common::ObDatumBasicFuncs &basic_funcs,
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
    hash_func.batch_func_(nullptr, nullptr, false, *skip, batch_size, nullptr, false, nullptr);
    hash_func.batch_func_(nullptr, nullptr, false, *skip, 0, nullptr, false, nullptr);

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
                            seeds.data(), shape[1], nullptr);
      for (int64_t i = 0; i < batch_size; ++i) {
        if (skip->at(i)) {
          EXPECT_EQ(HASH_SENTINEL, hash_values[i]);
        } else {
          const int64_t datum_idx = shape[0] ? i : 0;
          const int64_t seed_idx = shape[1] ? i : 0;
          uint64_t scalar_hash = 0;
          ASSERT_EQ(OB_SUCCESS,
                    hash_func.scalar_func_(
                        datums[datum_idx], seeds[seed_idx], scalar_hash, nullptr));
          EXPECT_EQ(scalar_hash, hash_values[i]);
        }
      }
    }

    std::vector<uint64_t> in_place_hashes = seeds;
    expected.assign(batch_size, HASH_SENTINEL);
    for (int64_t i = 0; i < batch_size; ++i) {
      if (!skip->at(i)) {
        ASSERT_EQ(OB_SUCCESS,
                  hash_func.scalar_func_(
                      datums[i], in_place_hashes[i], expected[i], nullptr));
      }
    }
    hash_func.batch_func_(in_place_hashes.data(), datums, true, *skip, batch_size,
                          in_place_hashes.data(), true, nullptr);
    for (int64_t i = 0; i < batch_size; ++i) {
      EXPECT_EQ(skip->at(i) ? seeds[i] : expected[i], in_place_hashes[i]);
    }

    skip->init(batch_size);
    in_place_hashes = seeds;
    expected.assign(batch_size, HASH_SENTINEL);
    uint64_t scalar_seed = seeds[0];
    for (int64_t i = 0; i < batch_size; ++i) {
      ASSERT_EQ(OB_SUCCESS,
                hash_func.scalar_func_(datums[i], scalar_seed, expected[i], nullptr));
      if (0 == i) {
        scalar_seed = expected[i];
      }
    }
    hash_func.batch_func_(in_place_hashes.data(), datums, true, *skip, batch_size,
                          in_place_hashes.data(), false, nullptr);
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

  common::ObDatumBasicFuncs *int_funcs = common::ObDatumFuncs::get_basic_func(
      common::ObIntType, common::CS_TYPE_BINARY);
  common::ObDatumBasicFuncs *general_ci_funcs = common::ObDatumFuncs::get_basic_func(
      common::ObVarcharType, common::CS_TYPE_UTF8MB4_GENERAL_CI,
      common::SCALE_UNKNOWN_YET, false);
  common::ObDatumBasicFuncs *utf8_bin_funcs = common::ObDatumFuncs::get_basic_func(
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
    common::ObDatumBasicFuncs *basic_funcs = common::ObDatumFuncs::get_basic_func(
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

TEST(ObExprCmpFunc, fixed_double_runtime_scale_evaluators_are_shared)
{
  const common::ObScale scales[] = {0, 2, 15, 30};
  static_assert(ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS) ==
                ARRAYSIZEOF(FIXED_DOUBLE_CMP_EXPR_TYPES),
                "comparison operation arrays must stay aligned");

  for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
    const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
    ObExpr::EvalFunc shared_scalar = nullptr;
    ObExpr::EvalBatchFunc shared_batch = nullptr;
    for (const common::ObScale scale : scales) {
      ObExpr::EvalFunc scalar_func = ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
          common::ObDoubleType, common::ObDoubleType, 0, scale, 0, 0,
          cmp_op, common::CS_TYPE_BINARY, false);
      ObExpr::EvalBatchFunc batch_func =
          ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
              common::ObDoubleType, common::ObDoubleType, 0, scale, 0, 0,
              cmp_op, common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, scalar_func);
      ASSERT_NE(nullptr, batch_func);
      EXPECT_EQ(scalar_func, ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
          common::ObDoubleType, common::ObDoubleType, scale, 0, 0, 0,
          cmp_op, common::CS_TYPE_BINARY, false));
      EXPECT_EQ(batch_func, ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
          common::ObDoubleType, common::ObDoubleType, scale, 0, 0, 0,
          cmp_op, common::CS_TYPE_BINARY, false));
      if (nullptr == shared_scalar) {
        shared_scalar = scalar_func;
        shared_batch = batch_func;
      } else {
        EXPECT_EQ(shared_scalar, scalar_func) << "scale=" << scale;
        EXPECT_EQ(shared_batch, batch_func) << "scale=" << scale;
      }
    }
  }
}

TEST(ObExprCmpFunc, fixed_double_runtime_scale_scalar_semantics)
{
  struct ScalarCase
  {
    double left_;
    double right_;
    bool left_null_;
    bool right_null_;
    int cmp_ret_;
  };
  const common::ObScale scales[] = {0, 2, 15, 30};
  FixedDoubleCmpScalarFrame left_frame{};
  FixedDoubleCmpScalarFrame right_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&left_frame),
      reinterpret_cast<char *>(&right_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;

  ObExpr left;
  left.frame_idx_ = 0;
  left.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&left_frame.datum_) - frames[0]);
  left.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&left_frame.eval_info_) - frames[0]);
  left_frame.datum_.ptr_ = reinterpret_cast<const char *>(&left_frame.value_);

  ObExpr right;
  right.frame_idx_ = 1;
  right.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&right_frame.datum_) - frames[1]);
  right.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&right_frame.eval_info_) - frames[1]);
  right_frame.datum_.ptr_ = reinterpret_cast<const char *>(&right_frame.value_);

  ObExpr expr;
  ObExpr *args[] = {&left, &right};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);
  int64_t result_value = 0;
  common::ObDatum result;
  result.ptr_ = reinterpret_cast<const char *>(&result_value);

  for (const common::ObScale scale : scales) {
    const double tolerance = get_fixed_double_tolerance(scale);
    const double inside_tolerance = std::nextafter(tolerance, 0.0);
    const double outside_tolerance =
        std::nextafter(tolerance, std::numeric_limits<double>::infinity());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    const ScalarCase cases[] = {
        {42.0, 42.0, false, false, 0},
        {0.0, inside_tolerance, false, false, 0},
        {0.0, tolerance, false, false, -1},
        {0.0, outside_tolerance, false, false, -1},
        {outside_tolerance, 0.0, false, false, 1},
        {-0.0, 0.0, false, false, 0},
        {0.0, -0.0, false, false, 0},
        {nan, nan, false, false, 0},
        {nan, 0.0, false, false, 1},
        {0.0, nan, false, false, -1},
        {infinity, infinity, false, false, 0},
        {infinity, 0.0, false, false, 1},
        {-infinity, 0.0, false, false, -1},
        {0.0, 0.0, true, false, 0},
        {0.0, 0.0, false, true, 0},
    };
    left.datum_meta_ = ObDatumMeta(
        common::ObDoubleType, common::CS_TYPE_BINARY, 0, 0);
    right.datum_meta_ = ObDatumMeta(
        common::ObDoubleType, common::CS_TYPE_BINARY, scale, 0);

    for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
      const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
      expr.type_ = FIXED_DOUBLE_CMP_EXPR_TYPES[op_idx];
      ObExpr::EvalFunc eval_func = ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
          common::ObDoubleType, common::ObDoubleType, 0, scale, 0, 0,
          cmp_op, common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, eval_func);
      for (int64_t case_idx = 0; case_idx < ARRAYSIZEOF(cases); ++case_idx) {
        SCOPED_TRACE(testing::Message() << "scale=" << scale
                                       << ", op=" << cmp_op
                                       << ", case=" << case_idx);
        left_frame.datum_.set_double(cases[case_idx].left_);
        right_frame.datum_.set_double(cases[case_idx].right_);
        if (cases[case_idx].left_null_) {
          left_frame.datum_.set_null();
        }
        if (cases[case_idx].right_null_) {
          right_frame.datum_.set_null();
        }
        result.set_int(-1);
        ASSERT_EQ(OB_SUCCESS, eval_func(expr, eval_ctx, result));
        if (cases[case_idx].left_null_ || cases[case_idx].right_null_) {
          EXPECT_TRUE(result.is_null());
        } else {
          EXPECT_FALSE(result.is_null());
          EXPECT_EQ(get_expected_cmp_result(cmp_op, cases[case_idx].cmp_ret_),
                    result.get_int());
        }
      }
    }

    left.datum_meta_ = ObDatumMeta(
        common::ObDoubleType, common::CS_TYPE_BINARY, scale, 0);
    right.datum_meta_ = ObDatumMeta(
        common::ObDoubleType, common::CS_TYPE_BINARY, 0, 0);
    left_frame.datum_.set_double(0.0);
    right_frame.datum_.set_double(tolerance);
    expr.type_ = T_OP_EQ;
    ObExpr::EvalFunc reverse_scale_eval =
        ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
            common::ObDoubleType, common::ObDoubleType, scale, 0, 0, 0,
            common::CO_EQ, common::CS_TYPE_BINARY, false);
    ASSERT_NE(nullptr, reverse_scale_eval);
    ASSERT_EQ(OB_SUCCESS, reverse_scale_eval(expr, eval_ctx, result));
    EXPECT_FALSE(result.is_null());
    EXPECT_EQ(0, result.get_int()) << "scale=" << scale;
  }
}

TEST(ObExprCmpFunc, fixed_double_runtime_scale_batch_semantics)
{
  static constexpr int64_t SKIPPED_IDX = 10;
  static constexpr int64_t EVALUATED_IDX = 11;
  static constexpr int64_t LEFT_NULL_IDX = 8;
  static constexpr int64_t RIGHT_NULL_IDX = 9;
  static constexpr int64_t RESULT_SENTINEL = -777;
  const common::ObScale scales[] = {0, 2, 15, 30};
  FixedDoubleCmpFrame left_frame{};
  FixedDoubleCmpFrame right_frame{};
  FixedDoubleCmpFrame result_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&left_frame),
      reinterpret_cast<char *>(&right_frame),
      reinterpret_cast<char *>(&result_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;
  eval_ctx.reuse(FIXED_DOUBLE_CMP_BATCH_SIZE);
  eval_ctx.set_max_batch_size(FIXED_DOUBLE_CMP_BATCH_SIZE);

  ObExpr left;
  init_fixed_double_batch_expr(left, 0, left_frame, frames[0]);
  ObExpr right;
  init_fixed_double_batch_expr(right, 1, right_frame, frames[1]);
  ObExpr expr;
  init_fixed_double_batch_expr(expr, 2, result_frame, frames[2]);
  ObExpr *args[] = {&left, &right};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);

  double left_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  double right_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int64_t result_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int expected_cmp[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
    left_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&left_values[i]);
    right_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&right_values[i]);
    result_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
  }
  left.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
  right.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
  ASSERT_EQ(sizeof(result_frame.eval_flags_),
            ObBitVector::memory_size(FIXED_DOUBLE_CMP_BATCH_SIZE));
  ASSERT_EQ(sizeof(result_frame.pvt_skip_),
            ObBitVector::memory_size(FIXED_DOUBLE_CMP_BATCH_SIZE));
  alignas(uint64_t) uint64_t skip_buf[1] = {};
  ObBitVector *skip = to_bit_vector(skip_buf);

  for (const common::ObScale scale : scales) {
    const double tolerance = get_fixed_double_tolerance(scale);
    const double inside_tolerance = std::nextafter(tolerance, 0.0);
    const double outside_tolerance =
        std::nextafter(tolerance, std::numeric_limits<double>::infinity());
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    left.datum_meta_ = ObDatumMeta(
        common::ObDoubleType, common::CS_TYPE_BINARY, 0, 0);
    right.datum_meta_ = ObDatumMeta(
        common::ObDoubleType, common::CS_TYPE_BINARY, scale, 0);

    for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
      const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
      expr.type_ = FIXED_DOUBLE_CMP_EXPR_TYPES[op_idx];
      ObExpr::EvalBatchFunc eval_func =
          ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
              common::ObDoubleType, common::ObDoubleType, 0, scale, 0, 0,
              cmp_op, common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, eval_func);

      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        left_values[i] = static_cast<double>(i);
        right_values[i] = static_cast<double>(i);
        expected_cmp[i] = 0;
      }
      left_values[0] = 0.0;
      right_values[0] = inside_tolerance;
      expected_cmp[0] = 0;
      left_values[1] = 0.0;
      right_values[1] = tolerance;
      expected_cmp[1] = -1;
      left_values[2] = 0.0;
      right_values[2] = outside_tolerance;
      expected_cmp[2] = -1;
      left_values[3] = outside_tolerance;
      right_values[3] = 0.0;
      expected_cmp[3] = 1;
      left_values[4] = nan;
      right_values[4] = nan;
      expected_cmp[4] = 0;
      left_values[5] = nan;
      right_values[5] = 0.0;
      expected_cmp[5] = 1;
      left_values[6] = 0.0;
      right_values[6] = nan;
      expected_cmp[6] = -1;
      left_values[7] = -0.0;
      right_values[7] = 0.0;
      expected_cmp[7] = 0;
      left_values[12] = -100.0;
      right_values[12] = 100.0;
      expected_cmp[12] = -1;
      left_values[13] = 100.0;
      right_values[13] = -100.0;
      expected_cmp[13] = 1;
      left_values[14] = infinity;
      right_values[14] = infinity;
      expected_cmp[14] = 0;
      left_values[15] = infinity;
      right_values[15] = 0.0;
      expected_cmp[15] = 1;
      left_values[16] = -infinity;
      right_values[16] = 0.0;
      expected_cmp[16] = -1;

      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        left_frame.datums_[i].set_double(left_values[i]);
        right_frame.datums_[i].set_double(right_values[i]);
        result_frame.datums_[i].set_int(RESULT_SENTINEL);
      }
      left_frame.datums_[LEFT_NULL_IDX].set_null();
      right_frame.datums_[RIGHT_NULL_IDX].set_null();
      skip->init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      skip->set(SKIPPED_IDX);
      expr.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      expr.get_evaluated_flags(eval_ctx).set(EVALUATED_IDX);
      expr.get_pvt_skip(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      result_frame.eval_info_.flag_ = 0;
      result_frame.eval_info_.notnull_ = true;
      result_frame.eval_info_.cnt_ = 0;

      SCOPED_TRACE(testing::Message() << "scale=" << scale << ", op=" << cmp_op);
      ASSERT_EQ(OB_SUCCESS,
                eval_func(expr, eval_ctx, *skip, FIXED_DOUBLE_CMP_BATCH_SIZE));
      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        if (SKIPPED_IDX == i || EVALUATED_IDX == i) {
          EXPECT_EQ(RESULT_SENTINEL, result_frame.datums_[i].get_int()) << "row=" << i;
        } else if (LEFT_NULL_IDX == i || RIGHT_NULL_IDX == i) {
          EXPECT_TRUE(result_frame.datums_[i].is_null()) << "row=" << i;
        } else {
          EXPECT_FALSE(result_frame.datums_[i].is_null()) << "row=" << i;
          EXPECT_EQ(get_expected_cmp_result(cmp_op, expected_cmp[i]),
                    result_frame.datums_[i].get_int()) << "row=" << i;
        }
        EXPECT_EQ(SKIPPED_IDX != i,
                  expr.get_evaluated_flags(eval_ctx).at(i)) << "row=" << i;
      }
      EXPECT_FALSE(result_frame.eval_info_.notnull_);
    }
  }
}

TEST(ObExprCmpFunc, decimal_int_runtime_width_evaluators_are_shared)
{
  static_assert(ARRAYSIZEOF(DECIMAL_INT_CMP_PRECISIONS) == common::DECIMAL_INT_MAX,
                "decimal-int precisions must cover every storage width");
  for (const common::ObCmpOp cmp_op : FIXED_DOUBLE_CMP_OPS) {
    ObExpr::EvalFunc shared_scalar = nullptr;
    ObExpr::EvalBatchFunc shared_batch = nullptr;
    for (const common::ObPrecision left_precision : DECIMAL_INT_CMP_PRECISIONS) {
      for (const common::ObPrecision right_precision : DECIMAL_INT_CMP_PRECISIONS) {
        ObExpr::EvalFunc scalar_func = ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
            common::ObDecimalIntType, common::ObDecimalIntType, 0, 0,
            left_precision, right_precision, cmp_op, common::CS_TYPE_BINARY, false);
        ObExpr::EvalBatchFunc batch_func =
            ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
                common::ObDecimalIntType, common::ObDecimalIntType, 0, 0,
                left_precision, right_precision, cmp_op, common::CS_TYPE_BINARY, false);
        ASSERT_NE(nullptr, scalar_func);
        ASSERT_NE(nullptr, batch_func);
        EXPECT_NE(nullptr, ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
            common::ObDecimalIntType, common::ObDecimalIntType, 0, 0,
            left_precision, right_precision, common::CS_TYPE_BINARY, false));
        if (nullptr == shared_scalar) {
          shared_scalar = scalar_func;
          shared_batch = batch_func;
        } else {
          EXPECT_EQ(shared_scalar, scalar_func)
              << "left_precision=" << left_precision
              << ", right_precision=" << right_precision;
          EXPECT_EQ(shared_batch, batch_func)
              << "left_precision=" << left_precision
              << ", right_precision=" << right_precision;
        }
      }
    }
  }
}

TEST(ObExprCmpFunc, decimal_int_runtime_width_scalar_semantics)
{
  struct ScalarCase
  {
    common::ObPrecision left_precision_;
    common::ObPrecision right_precision_;
    int64_t left_;
    int64_t right_;
    int cmp_ret_;
    bool left_null_;
    bool right_null_;
  };
  const ScalarCase cases[] = {
      {common::MAX_PRECISION_DECIMAL_INT_32, common::MAX_PRECISION_DECIMAL_INT_512,
       -123, 456, -1, false, false},
      {common::MAX_PRECISION_DECIMAL_INT_512, common::MAX_PRECISION_DECIMAL_INT_32,
       456, -123, 1, false, false},
      {common::MAX_PRECISION_DECIMAL_INT_128, common::MAX_PRECISION_DECIMAL_INT_256,
       -77, -77, 0, false, false},
      {common::MAX_PRECISION_DECIMAL_INT_32, common::MAX_PRECISION_DECIMAL_INT_512,
       0, 0, 0, true, false},
      {common::MAX_PRECISION_DECIMAL_INT_512, common::MAX_PRECISION_DECIMAL_INT_32,
       0, 0, 0, false, true},
  };
  FixedDoubleCmpFrame left_frame{};
  FixedDoubleCmpFrame right_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&left_frame),
      reinterpret_cast<char *>(&right_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;

  ObExpr left;
  left.frame_idx_ = 0;
  left.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&left_frame.datums_) - frames[0]);
  left.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&left_frame.eval_info_) - frames[0]);
  ObExpr right;
  right.frame_idx_ = 1;
  right.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&right_frame.datums_) - frames[1]);
  right.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&right_frame.eval_info_) - frames[1]);
  alignas(64) char left_value[sizeof(common::int512_t)] = {};
  alignas(64) char right_value[sizeof(common::int512_t)] = {};
  left_frame.datums_[0].ptr_ = left_value;
  right_frame.datums_[0].ptr_ = right_value;

  ObExpr expr;
  ObExpr *args[] = {&left, &right};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);
  int64_t result_value = 0;
  common::ObDatum result;
  result.ptr_ = reinterpret_cast<const char *>(&result_value);

  for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
    const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
    expr.type_ = FIXED_DOUBLE_CMP_EXPR_TYPES[op_idx];
    for (int64_t case_idx = 0; case_idx < ARRAYSIZEOF(cases); ++case_idx) {
      const ScalarCase &test_case = cases[case_idx];
      SCOPED_TRACE(testing::Message() << "op=" << cmp_op << ", case=" << case_idx);
      left.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY,
                                     0, test_case.left_precision_);
      right.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY,
                                      0, test_case.right_precision_);
      set_decimal_int_value(left_frame.datums_[0],
                            test_case.left_precision_, test_case.left_);
      set_decimal_int_value(right_frame.datums_[0],
                            test_case.right_precision_, test_case.right_);
      if (test_case.left_null_) {
        left_frame.datums_[0].set_null();
      }
      if (test_case.right_null_) {
        right_frame.datums_[0].set_null();
      }
      ObExpr::EvalFunc eval_func = ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
          common::ObDecimalIntType, common::ObDecimalIntType, 0, 0,
          test_case.left_precision_, test_case.right_precision_, cmp_op,
          common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, eval_func);
      result.set_int(-1);
      ASSERT_EQ(OB_SUCCESS, eval_func(expr, eval_ctx, result));
      if (test_case.left_null_ || test_case.right_null_) {
        EXPECT_TRUE(result.is_null());
      } else {
        EXPECT_FALSE(result.is_null());
        EXPECT_EQ(get_expected_cmp_result(cmp_op, test_case.cmp_ret_), result.get_int());
      }
    }
  }
}

TEST(ObExprCmpFunc, decimal_int_runtime_width_batch_semantics)
{
  struct WidthPair
  {
    common::ObPrecision left_;
    common::ObPrecision right_;
  };
  const WidthPair width_pairs[] = {
      {common::MAX_PRECISION_DECIMAL_INT_32, common::MAX_PRECISION_DECIMAL_INT_512},
      {common::MAX_PRECISION_DECIMAL_INT_512, common::MAX_PRECISION_DECIMAL_INT_32},
      {common::MAX_PRECISION_DECIMAL_INT_128, common::MAX_PRECISION_DECIMAL_INT_256},
  };
  static constexpr int64_t LEFT_NULL_IDX = 8;
  static constexpr int64_t RIGHT_NULL_IDX = 9;
  static constexpr int64_t SKIPPED_IDX = 10;
  static constexpr int64_t EVALUATED_IDX = 11;
  static constexpr int64_t RESULT_SENTINEL = -777;
  FixedDoubleCmpFrame left_frame{};
  FixedDoubleCmpFrame right_frame{};
  FixedDoubleCmpFrame result_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&left_frame),
      reinterpret_cast<char *>(&right_frame),
      reinterpret_cast<char *>(&result_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;
  eval_ctx.reuse(FIXED_DOUBLE_CMP_BATCH_SIZE);
  eval_ctx.set_max_batch_size(FIXED_DOUBLE_CMP_BATCH_SIZE);

  ObExpr left;
  init_fixed_double_batch_expr(left, 0, left_frame, frames[0]);
  ObExpr right;
  init_fixed_double_batch_expr(right, 1, right_frame, frames[1]);
  ObExpr expr;
  init_fixed_double_batch_expr(expr, 2, result_frame, frames[2]);
  ObExpr *args[] = {&left, &right};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);

  alignas(64) char left_values[FIXED_DOUBLE_CMP_BATCH_SIZE][sizeof(common::int512_t)] = {};
  alignas(64) char right_values[FIXED_DOUBLE_CMP_BATCH_SIZE][sizeof(common::int512_t)] = {};
  int64_t result_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int64_t left_inputs[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int64_t right_inputs[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int expected_cmp[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
    left_frame.datums_[i].ptr_ = left_values[i];
    right_frame.datums_[i].ptr_ = right_values[i];
    result_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
  }
  left.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
  right.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
  alignas(uint64_t) uint64_t skip_buf[1] = {};
  ObBitVector *skip = to_bit_vector(skip_buf);

  for (const WidthPair &width_pair : width_pairs) {
    left.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY,
                                   0, width_pair.left_);
    right.datum_meta_ = ObDatumMeta(common::ObDecimalIntType, common::CS_TYPE_BINARY,
                                    0, width_pair.right_);
    for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
      const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
      expr.type_ = FIXED_DOUBLE_CMP_EXPR_TYPES[op_idx];
      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        left_inputs[i] = i - FIXED_DOUBLE_CMP_BATCH_SIZE / 2;
        right_inputs[i] = left_inputs[i];
        expected_cmp[i] = 0;
      }
      left_inputs[0] = -123;
      right_inputs[0] = 456;
      expected_cmp[0] = -1;
      left_inputs[1] = 456;
      right_inputs[1] = -123;
      expected_cmp[1] = 1;
      left_inputs[2] = -77;
      right_inputs[2] = -77;

      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        set_decimal_int_value(left_frame.datums_[i], width_pair.left_, left_inputs[i]);
        set_decimal_int_value(right_frame.datums_[i], width_pair.right_, right_inputs[i]);
        result_frame.datums_[i].set_int(RESULT_SENTINEL);
      }
      left_frame.datums_[LEFT_NULL_IDX].set_null();
      right_frame.datums_[RIGHT_NULL_IDX].set_null();
      skip->init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      skip->set(SKIPPED_IDX);
      expr.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      expr.get_evaluated_flags(eval_ctx).set(EVALUATED_IDX);
      expr.get_pvt_skip(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      result_frame.eval_info_.flag_ = 0;
      result_frame.eval_info_.notnull_ = true;
      result_frame.eval_info_.cnt_ = 0;

      ObExpr::EvalBatchFunc eval_func =
          ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
              common::ObDecimalIntType, common::ObDecimalIntType, 0, 0,
              width_pair.left_, width_pair.right_, cmp_op,
              common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, eval_func);
      SCOPED_TRACE(testing::Message() << "left_precision=" << width_pair.left_
                                     << ", right_precision=" << width_pair.right_
                                     << ", op=" << cmp_op);
      ASSERT_EQ(OB_SUCCESS,
                eval_func(expr, eval_ctx, *skip, FIXED_DOUBLE_CMP_BATCH_SIZE));
      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        if (SKIPPED_IDX == i || EVALUATED_IDX == i) {
          EXPECT_EQ(RESULT_SENTINEL, result_frame.datums_[i].get_int()) << "row=" << i;
        } else if (LEFT_NULL_IDX == i || RIGHT_NULL_IDX == i) {
          EXPECT_TRUE(result_frame.datums_[i].is_null()) << "row=" << i;
        } else {
          EXPECT_FALSE(result_frame.datums_[i].is_null()) << "row=" << i;
          EXPECT_EQ(get_expected_cmp_result(cmp_op, expected_cmp[i]),
                    result_frame.datums_[i].get_int()) << "row=" << i;
        }
        EXPECT_EQ(SKIPPED_IDX != i,
                  expr.get_evaluated_flags(eval_ctx).at(i)) << "row=" << i;
      }
    }
  }
}

TEST(ObExprCmpFunc, tc_evaluators_share_scalar_and_split_batch_paths)
{
  ObExpr::EvalFunc shared_scalar = nullptr;
  ObExpr::EvalBatchFunc special_batch = nullptr;
  ObExpr::EvalBatchFunc regular_batch = nullptr;
  int64_t defined_pair_count = 0;
  int64_t special_pair_count = 0;
  int64_t regular_pair_count = 0;
  for (int64_t left_tc = 0; left_tc < common::ObMaxTC; ++left_tc) {
    for (int64_t right_tc = 0; right_tc < common::ObMaxTC; ++right_tc) {
      const bool has_null = common::ObNullTC == left_tc || common::ObNullTC == right_tc;
      const bool has_extend = common::ObExtendTC == left_tc || common::ObExtendTC == right_tc;
      common::ObDatumCmpFuncType datum_func = DATUM_TC_CMP_FUNCS[left_tc][right_tc];
      if (has_null || has_extend) {
        ASSERT_NE(nullptr, datum_func);
      }
      if (nullptr != datum_func) {
        ++defined_pair_count;
        if (has_null || has_extend) {
          ++special_pair_count;
        } else {
          ++regular_pair_count;
        }
        for (int64_t cmp_op = common::CO_EQ; cmp_op < common::CO_MAX; ++cmp_op) {
          ObExpr::EvalFunc scalar_func = EVAL_TC_CMP_FUNCS[left_tc][right_tc][cmp_op];
          ASSERT_NE(nullptr, scalar_func);
          if (nullptr == shared_scalar) {
            shared_scalar = scalar_func;
          } else {
            EXPECT_EQ(shared_scalar, scalar_func)
                << "left_tc=" << left_tc << ", right_tc=" << right_tc;
          }
        }
        for (const common::ObCmpOp cmp_op : FIXED_DOUBLE_CMP_OPS) {
          ObExpr::EvalBatchFunc batch_func =
              EVAL_BATCH_TC_CMP_FUNCS[left_tc][right_tc][cmp_op];
          ASSERT_NE(nullptr, batch_func);
          ObExpr::EvalBatchFunc &shared_batch =
              (has_null || has_extend) ? special_batch : regular_batch;
          if (nullptr == shared_batch) {
            shared_batch = batch_func;
          } else {
            EXPECT_EQ(shared_batch, batch_func)
                << "left_tc=" << left_tc << ", right_tc=" << right_tc;
          }
        }
      }
    }
  }
  EXPECT_EQ(131, defined_pair_count);
  EXPECT_EQ(112, special_pair_count);
  EXPECT_EQ(19, regular_pair_count);
  EXPECT_NE(regular_batch, special_batch);
}

TEST(ObExprCmpFunc, tc_scalar_semantics)
{
  struct ScalarCase
  {
    common::ObObjType left_type_;
    common::ObObjType right_type_;
    int64_t left_;
    int64_t right_;
    bool left_null_;
    bool right_null_;
  };
  const ScalarCase cases[] = {
      {common::ObIntType, common::ObIntType, -3, 7, false, false},
      {common::ObIntType, common::ObUInt64Type, -1, 0, false, false},
      {common::ObUInt64Type, common::ObIntType, 0, -1, false, false},
      {common::ObFloatType, common::ObFloatType, -3, 4, false, false},
      {common::ObDateType, common::ObDateType, 20240101, 20240102, false, false},
      {common::ObNullType, common::ObIntType, 0, 7, true, false},
      {common::ObIntType, common::ObNullType, 7, 0, false, true},
      {common::ObExtendType, common::ObIntType,
       common::ObObj::MIN_OBJECT_VALUE, 0, false, false},
      {common::ObExtendType, common::ObIntType,
       common::ObObj::MAX_OBJECT_VALUE, 0, false, false},
      {common::ObIntType, common::ObExtendType,
       0, common::ObObj::MIN_OBJECT_VALUE, false, false},
      {common::ObIntType, common::ObExtendType,
       0, common::ObObj::MAX_OBJECT_VALUE, false, false},
      {common::ObExtendType, common::ObExtendType,
       common::ObObj::MIN_OBJECT_VALUE, common::ObObj::MAX_OBJECT_VALUE, false, false},
      {common::ObExtendType, common::ObExtendType,
       common::ObObj::MAX_OBJECT_VALUE, common::ObObj::MIN_OBJECT_VALUE, false, false},
      {common::ObExtendType, common::ObExtendType,
       common::ObObj::MIN_OBJECT_VALUE, common::ObObj::MIN_OBJECT_VALUE, false, false},
  };
  FixedDoubleCmpFrame left_frame{};
  FixedDoubleCmpFrame right_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&left_frame),
      reinterpret_cast<char *>(&right_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;

  ObExpr left;
  left.frame_idx_ = 0;
  left.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&left_frame.datums_) - frames[0]);
  left.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&left_frame.eval_info_) - frames[0]);
  ObExpr right;
  right.frame_idx_ = 1;
  right.datum_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&right_frame.datums_) - frames[1]);
  right.eval_info_off_ = static_cast<uint32_t>(
      reinterpret_cast<char *>(&right_frame.eval_info_) - frames[1]);
  int64_t left_value = 0;
  int64_t right_value = 0;
  left_frame.datums_[0].ptr_ = reinterpret_cast<const char *>(&left_value);
  right_frame.datums_[0].ptr_ = reinterpret_cast<const char *>(&right_value);

  ObExpr expr;
  ObExpr *args[] = {&left, &right};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);
  int64_t result_value = 0;
  common::ObDatum result;
  result.ptr_ = reinterpret_cast<const char *>(&result_value);

  for (int64_t case_idx = 0; case_idx < ARRAYSIZEOF(cases); ++case_idx) {
    const ScalarCase &test_case = cases[case_idx];
    left.datum_meta_ = ObDatumMeta(test_case.left_type_, common::CS_TYPE_BINARY, 0, 0);
    right.datum_meta_ = ObDatumMeta(test_case.right_type_, common::CS_TYPE_BINARY, 0, 0);
    set_tc_cmp_value(left_frame.datums_[0], test_case.left_type_, test_case.left_);
    set_tc_cmp_value(right_frame.datums_[0], test_case.right_type_, test_case.right_);
    if (test_case.left_null_) {
      left_frame.datums_[0].set_null();
    }
    if (test_case.right_null_) {
      right_frame.datums_[0].set_null();
    }
    DatumCmpFunc datum_func = ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
        test_case.left_type_, test_case.right_type_, 0, 0, 0, 0,
        common::CS_TYPE_BINARY, false);
    ASSERT_NE(nullptr, datum_func);
    int cmp_ret = 0;
    if (!test_case.left_null_ && !test_case.right_null_) {
      ASSERT_EQ(OB_SUCCESS,
                datum_func(
                    left_frame.datums_[0], right_frame.datums_[0], cmp_ret, nullptr));
    }
    for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
      const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
      expr.type_ = FIXED_DOUBLE_CMP_EXPR_TYPES[op_idx];
      ObExpr::EvalFunc eval_func = ObExprCmpFuncsHelper::get_eval_expr_cmp_func(
          test_case.left_type_, test_case.right_type_, 0, 0, 0, 0,
          cmp_op, common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, eval_func);
      SCOPED_TRACE(testing::Message() << "case=" << case_idx << ", op=" << cmp_op);
      result.set_int(-1);
      ASSERT_EQ(OB_SUCCESS, eval_func(expr, eval_ctx, result));
      if (test_case.left_null_ || test_case.right_null_) {
        EXPECT_TRUE(result.is_null());
      } else {
        EXPECT_FALSE(result.is_null());
        EXPECT_EQ(get_expected_cmp_result(cmp_op, cmp_ret), result.get_int());
      }
    }
  }
}

TEST(ObExprCmpFunc, tc_batch_semantics)
{
  struct TypePair
  {
    common::ObObjType left_;
    common::ObObjType right_;
  };
  const TypePair type_pairs[] = {
      {common::ObIntType, common::ObUInt64Type},
      {common::ObNullType, common::ObIntType},
      {common::ObExtendType, common::ObIntType},
      {common::ObExtendType, common::ObExtendType},
  };
  static constexpr int64_t NULL_IDX = 8;
  static constexpr int64_t SKIPPED_IDX = 10;
  static constexpr int64_t EVALUATED_IDX = 11;
  static constexpr int64_t RESULT_SENTINEL = -777;
  FixedDoubleCmpFrame left_frame{};
  FixedDoubleCmpFrame right_frame{};
  FixedDoubleCmpFrame result_frame{};
  char *frames[] = {
      reinterpret_cast<char *>(&left_frame),
      reinterpret_cast<char *>(&right_frame),
      reinterpret_cast<char *>(&result_frame),
  };
  common::ObArenaAllocator allocator;
  ObExecContext exec_ctx(allocator);
  ObEvalCtx eval_ctx(exec_ctx);
  eval_ctx.frames_ = frames;
  eval_ctx.reuse(FIXED_DOUBLE_CMP_BATCH_SIZE);
  eval_ctx.set_max_batch_size(FIXED_DOUBLE_CMP_BATCH_SIZE);

  ObExpr left;
  init_fixed_double_batch_expr(left, 0, left_frame, frames[0]);
  ObExpr right;
  init_fixed_double_batch_expr(right, 1, right_frame, frames[1]);
  ObExpr expr;
  init_fixed_double_batch_expr(expr, 2, result_frame, frames[2]);
  ObExpr *args[] = {&left, &right};
  expr.args_ = args;
  expr.arg_cnt_ = ARRAYSIZEOF(args);
  int64_t left_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int64_t right_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  int64_t result_values[FIXED_DOUBLE_CMP_BATCH_SIZE] = {};
  for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
    left_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&left_values[i]);
    right_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&right_values[i]);
    result_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
  }
  left.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
  right.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
  alignas(uint64_t) uint64_t skip_buf[1] = {};
  ObBitVector *skip = to_bit_vector(skip_buf);

  for (const TypePair &type_pair : type_pairs) {
    left.datum_meta_ = ObDatumMeta(type_pair.left_, common::CS_TYPE_BINARY, 0, 0);
    right.datum_meta_ = ObDatumMeta(type_pair.right_, common::CS_TYPE_BINARY, 0, 0);
    DatumCmpFunc datum_func = ObExprCmpFuncsHelper::get_datum_expr_cmp_func(
        type_pair.left_, type_pair.right_, 0, 0, 0, 0,
        common::CS_TYPE_BINARY, false);
    ASSERT_NE(nullptr, datum_func);
    for (int64_t op_idx = 0; op_idx < ARRAYSIZEOF(FIXED_DOUBLE_CMP_OPS); ++op_idx) {
      const common::ObCmpOp cmp_op = FIXED_DOUBLE_CMP_OPS[op_idx];
      expr.type_ = FIXED_DOUBLE_CMP_EXPR_TYPES[op_idx];
      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        left_values[i] = 0;
        right_values[i] = 0;
      }
      if (common::ObIntType == type_pair.left_ &&
          common::ObUInt64Type == type_pair.right_) {
        left_values[0] = -1;
        left_values[1] = 7;
        left_values[2] = 9;
        right_values[0] = 0;
        right_values[1] = 7;
        right_values[2] = 2;
      } else {
        left_values[0] = common::ObObj::MIN_OBJECT_VALUE;
        left_values[1] = common::ObObj::MAX_OBJECT_VALUE;
        left_values[2] = common::ObObj::MIN_OBJECT_VALUE;
        left_values[3] = common::ObObj::MAX_OBJECT_VALUE;
        right_values[0] = common::ObObj::MIN_OBJECT_VALUE;
        right_values[1] = common::ObObj::MIN_OBJECT_VALUE;
        right_values[2] = common::ObObj::MAX_OBJECT_VALUE;
        right_values[3] = common::ObObj::MAX_OBJECT_VALUE;
      }
      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        set_tc_cmp_value(left_frame.datums_[i], type_pair.left_, left_values[i]);
        set_tc_cmp_value(right_frame.datums_[i], type_pair.right_, right_values[i]);
        result_frame.datums_[i].set_int(RESULT_SENTINEL);
      }
      left_frame.datums_[NULL_IDX].set_null();
      skip->init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      skip->set(SKIPPED_IDX);
      expr.get_evaluated_flags(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      expr.get_evaluated_flags(eval_ctx).set(EVALUATED_IDX);
      expr.get_pvt_skip(eval_ctx).init(FIXED_DOUBLE_CMP_BATCH_SIZE);
      result_frame.eval_info_.flag_ = 0;
      result_frame.eval_info_.notnull_ = true;
      result_frame.eval_info_.cnt_ = 0;

      ObExpr::EvalBatchFunc eval_func =
          ObExprCmpFuncsHelper::get_eval_batch_expr_cmp_func(
              type_pair.left_, type_pair.right_, 0, 0, 0, 0,
              cmp_op, common::CS_TYPE_BINARY, false);
      ASSERT_NE(nullptr, eval_func);
      SCOPED_TRACE(testing::Message() << "left_type=" << type_pair.left_
                                     << ", right_type=" << type_pair.right_
                                     << ", op=" << cmp_op);
      ASSERT_EQ(OB_SUCCESS,
                eval_func(expr, eval_ctx, *skip, FIXED_DOUBLE_CMP_BATCH_SIZE));
      for (int64_t i = 0; i < FIXED_DOUBLE_CMP_BATCH_SIZE; ++i) {
        if (SKIPPED_IDX == i || EVALUATED_IDX == i) {
          EXPECT_EQ(RESULT_SENTINEL, result_frame.datums_[i].get_int()) << "row=" << i;
        } else if (left_frame.datums_[i].is_null() || right_frame.datums_[i].is_null()) {
          EXPECT_TRUE(result_frame.datums_[i].is_null()) << "row=" << i;
        } else {
          int cmp_ret = 0;
          ASSERT_EQ(OB_SUCCESS,
                    datum_func(
                        left_frame.datums_[i], right_frame.datums_[i], cmp_ret, nullptr));
          EXPECT_EQ(get_expected_cmp_result(cmp_op, cmp_ret),
                    result_frame.datums_[i].get_int()) << "row=" << i;
        }
        EXPECT_EQ(SKIPPED_IDX != i,
                  expr.get_evaluated_flags(eval_ctx).at(i)) << "row=" << i;
      }
      EXPECT_FALSE(result_frame.eval_info_.notnull_);
    }
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
  ASSERT_EQ(OB_SUCCESS, init_decimal_int_const_values());

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
  for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
    child_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&input_values[i]);
    result_frame.datums_[i].ptr_ = reinterpret_cast<const char *>(&result_values[i]);
  }

  auto prepare_batch = [&](const int32_t scale_factor, const bool sparse) {
    skip->init(DECINT_CAST_BATCH_SIZE);
    expr.get_evaluated_flags(eval_ctx).init(DECINT_CAST_BATCH_SIZE);
    if (sparse) {
      skip->set(SKIPPED_IDX);
      skip->set(HIGH_SKIPPED_IDX);
      expr.get_evaluated_flags(eval_ctx).set(EVALUATED_IDX);
    }
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
  prepare_batch(1, true);
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
  prepare_batch(100, true);
  input_values[0] = 12350;
  input_values[1] = -12350;
  input_values[3] = 12349;
  input_values[4] = -12349;
  ASSERT_EQ(OB_SUCCESS, down_batch_func(expr, eval_ctx, *skip, DECINT_CAST_BATCH_SIZE));
  for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
    if (SKIPPED_IDX == i || HIGH_SKIPPED_IDX == i || EVALUATED_IDX == i) {
      EXPECT_EQ(777, result_frame.datums_[i].get_decimal_int32());
    } else if (NULL_IDX == i) {
      EXPECT_TRUE(result_frame.datums_[i].is_null());
    } else {
      EXPECT_EQ(round_decimal_int_down(input_values[i], 100),
                result_frame.datums_[i].get_decimal_int32());
    }
    EXPECT_EQ(SKIPPED_IDX != i && HIGH_SKIPPED_IDX != i,
              expr.get_evaluated_flags(eval_ctx).at(i));
  }

  ObExpr::EvalBatchFunc equal_scale_batch_func = nullptr;
  child.datum_meta_.scale_ = 2;
  expr.datum_meta_.scale_ = 2;
  ObDatumCast::get_decint_cast(common::ObDecimalIntTC, 9, 2, 9, 2, false,
                               equal_scale_batch_func, scalar_func);
  ASSERT_NE(nullptr, equal_scale_batch_func);
  EXPECT_EQ(batch_func, equal_scale_batch_func);
  prepare_batch(1, false);
  ASSERT_EQ(OB_SUCCESS,
            equal_scale_batch_func(expr, eval_ctx, *skip, DECINT_CAST_BATCH_SIZE));
  for (int64_t i = 0; i < DECINT_CAST_BATCH_SIZE; ++i) {
    if (NULL_IDX == i) {
      EXPECT_TRUE(result_frame.datums_[i].is_null());
    } else {
      EXPECT_EQ(input_values[i], result_frame.datums_[i].get_decimal_int32());
    }
    EXPECT_TRUE(expr.get_evaluated_flags(eval_ctx).at(i));
  }
}

TEST(ObDatumCast, decimal_int_generic_batch_explicit_uses_runtime_scale_direction)
{
  ASSERT_EQ(OB_SUCCESS, init_decimal_int_const_values());
  static constexpr common::ObPrecision INT32_PRECISION =
      common::MAX_PRECISION_DECIMAL_INT_32;
  static constexpr common::ObPrecision INT128_PRECISION =
      common::MAX_PRECISION_DECIMAL_INT_128;
  DecimalIntBatchCastContext ctx;
  ObBatchCast::batch_func_ batch_func =
      ObBatchCast::get_implicit_cast_func(common::ObDecimalIntTC, common::ObDecimalIntTC);
  ASSERT_NE(nullptr, batch_func);

  // The common dispatcher selects batch_explicit_scale from the cast mode.  Calling the
  // dispatcher directly keeps these assertions focused on scaling, before the outer CAST
  // accuracy checker converts the exclusive boundary values to their final SQL values.
  ctx.configure(INT32_PRECISION, 2, INT128_PRECISION, 4,
                CM_EXPLICIT_CAST | CM_WARN_ON_FAIL);
  ctx.reset(3, INT128_PRECISION, 777);
  ctx.set_input(0, int32_t(12345));
  ctx.set_input(1, int32_t(-12345));
  ctx.set_input(2, int32_t(0));
  ctx.child_frame_.datums_[2].set_null();
  ASSERT_EQ(OB_SUCCESS, batch_func(ctx.expr_, ctx.eval_ctx_, *ctx.skip_, 3));
  EXPECT_EQ(1234500, get_decimal_int_value(ctx.result_frame_.datums_[0], INT128_PRECISION));
  EXPECT_EQ(-1234500, get_decimal_int_value(ctx.result_frame_.datums_[1], INT128_PRECISION));
  EXPECT_TRUE(ctx.result_frame_.datums_[2].is_null());

  ctx.configure(INT128_PRECISION, 2, 3, 0, CM_EXPLICIT_CAST | CM_WARN_ON_FAIL);
  ctx.reset(4, 3, 77);
  ctx.set_input(0, common::int128_t(12350));
  ctx.set_input(1, common::int128_t(-12350));
  ctx.set_input(2, common::int128_t(99999));
  ctx.set_input(3, common::int128_t(-99999));
  ASSERT_EQ(OB_SUCCESS, batch_func(ctx.expr_, ctx.eval_ctx_, *ctx.skip_, 4));
  EXPECT_EQ(124, get_decimal_int_value(ctx.result_frame_.datums_[0], 3));
  EXPECT_EQ(-124, get_decimal_int_value(ctx.result_frame_.datums_[1], 3));
  EXPECT_EQ(1000, get_decimal_int_value(ctx.result_frame_.datums_[2], 3));
  EXPECT_EQ(-1000, get_decimal_int_value(ctx.result_frame_.datums_[3], 3));

  ctx.configure(INT128_PRECISION, 0, INT128_PRECISION, 1,
                CM_EXPLICIT_CAST | CM_WARN_ON_FAIL);
  ctx.reset(1, INT128_PRECISION, 0);
  ctx.set_input(0, common::wide::Limits<common::int128_t>::max());
  ASSERT_EQ(OB_SUCCESS, batch_func(ctx.expr_, ctx.eval_ctx_, *ctx.skip_, 1));
  EXPECT_EQ(sizeof(common::int128_t), ctx.result_frame_.datums_[0].get_int_bytes());
  EXPECT_EQ(0,
            MEMCMP(ctx.result_frame_.datums_[0].get_decimal_int(),
                   common::wide::ObDecimalIntConstValue::get_max_upper(INT128_PRECISION),
                   sizeof(common::int128_t)));
}

TEST(ObDatumCast, decimal_int_generic_batch_const_modes_use_runtime_scale_direction)
{
  ASSERT_EQ(OB_SUCCESS, init_decimal_int_const_values());
  static constexpr common::ObPrecision INT32_PRECISION =
      common::MAX_PRECISION_DECIMAL_INT_32;
  static constexpr common::ObPrecision INT64_PRECISION =
      common::MAX_PRECISION_DECIMAL_INT_64;
  static constexpr int64_t BATCH_SIZE = 6;
  static constexpr int64_t NULL_IDX = 3;
  static constexpr int64_t SKIPPED_IDX = 4;
  static constexpr int64_t EVALUATED_IDX = 5;
  static constexpr int64_t RESULT_SENTINEL = 77;
  struct ConstModeCase
  {
    uint64_t mode_;
    int32_t positive_expected_;
    int32_t negative_expected_;
  };
  const ConstModeCase cases[] = {
      {CM_CONST_TO_DECIMAL_INT_UP, 124, -123},
      {CM_CONST_TO_DECIMAL_INT_DOWN, 123, -124},
      {CM_CONST_TO_DECIMAL_INT_EQ, 1000, -1000},
  };
  DecimalIntBatchCastContext ctx;
  ObBatchCast::batch_func_ batch_func =
      ObBatchCast::get_implicit_cast_func(common::ObDecimalIntTC, common::ObDecimalIntTC);
  ASSERT_NE(nullptr, batch_func);

  for (const ConstModeCase &test_case : cases) {
    SCOPED_TRACE(testing::Message() << "cast_mode=" << test_case.mode_);
    ctx.configure(INT32_PRECISION, 2, 3, 0, test_case.mode_);
    ctx.reset(BATCH_SIZE, 3, RESULT_SENTINEL);
    ctx.set_input(0, int32_t(12345));
    ctx.set_input(1, int32_t(-12345));
    ctx.set_input(2, int32_t(12300));
    ctx.set_input(NULL_IDX, int32_t(45678));
    ctx.set_input(SKIPPED_IDX, int32_t(56789));
    ctx.set_input(EVALUATED_IDX, int32_t(67891));
    ctx.child_frame_.datums_[NULL_IDX].set_null();
    ctx.skip_->set(SKIPPED_IDX);
    ctx.expr_.get_evaluated_flags(ctx.eval_ctx_).set(EVALUATED_IDX);
    ASSERT_EQ(OB_SUCCESS,
              batch_func(ctx.expr_, ctx.eval_ctx_, *ctx.skip_, BATCH_SIZE));
    EXPECT_EQ(test_case.positive_expected_,
              get_decimal_int_value(ctx.result_frame_.datums_[0], 3));
    EXPECT_EQ(test_case.negative_expected_,
              get_decimal_int_value(ctx.result_frame_.datums_[1], 3));
    EXPECT_EQ(123, get_decimal_int_value(ctx.result_frame_.datums_[2], 3));
    EXPECT_TRUE(ctx.result_frame_.datums_[NULL_IDX].is_null());
    EXPECT_EQ(RESULT_SENTINEL,
              get_decimal_int_value(ctx.result_frame_.datums_[SKIPPED_IDX], 3));
    EXPECT_EQ(RESULT_SENTINEL,
              get_decimal_int_value(ctx.result_frame_.datums_[EVALUATED_IDX], 3));
    for (int64_t i = 0; i < BATCH_SIZE; ++i) {
      EXPECT_EQ(SKIPPED_IDX != i,
                ctx.expr_.get_evaluated_flags(ctx.eval_ctx_).at(i)) << "row=" << i;
    }
  }

  ctx.configure(INT32_PRECISION, 0, INT64_PRECISION, 2,
                CM_CONST_TO_DECIMAL_INT_UP);
  ctx.reset(3, INT64_PRECISION, RESULT_SENTINEL);
  ctx.set_input(0, int32_t(123));
  ctx.set_input(1, int32_t(-456));
  ctx.set_input(2, int32_t(0));
  ctx.child_frame_.datums_[2].set_null();
  ASSERT_EQ(OB_SUCCESS, batch_func(ctx.expr_, ctx.eval_ctx_, *ctx.skip_, 3));
  EXPECT_EQ(12300, get_decimal_int_value(ctx.result_frame_.datums_[0], INT64_PRECISION));
  EXPECT_EQ(-45600, get_decimal_int_value(ctx.result_frame_.datums_[1], INT64_PRECISION));
  EXPECT_TRUE(ctx.result_frame_.datums_[2].is_null());
}

} // namespace sql
} // namespace oceanbase
