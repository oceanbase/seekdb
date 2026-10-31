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

#include "lib/charset/ob_charset.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_minus.h"
#include "sql/engine/expr/ob_expr_mul.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include <gtest/gtest.h>
#include <array>
#include <initializer_list>
#include <limits>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
enum class Operation { Minus, Multiply };
constexpr Operation OPERATIONS[] = {Operation::Minus, Operation::Multiply};
constexpr int64_t MIN_INT = std::numeric_limits<int64_t>::min();
constexpr int64_t MAX_INT = std::numeric_limits<int64_t>::max();

struct Expected
{
  int64_t value_;
  bool overflow_;
};

// A full signed-i64 product fits signed 128 bits. This oracle uses exact wide
// mathematics, not the production sign check or overflow builtins. The low
// bits also pin the callback's existing write-before-check result on error.
Expected oracle(Operation op, int64_t left, int64_t right)
{
  const __int128 l = left;
  const __int128 r = right;
  const __int128 exact = op == Operation::Minus ? l - r : l * r;
  const uint64_t low_bits = static_cast<uint64_t>(exact);
  Expected expected;
  MEMCPY(&expected.value_, &low_bits, sizeof(low_bits));
  expected.overflow_ = exact < static_cast<__int128>(MIN_INT)
      || exact > static_cast<__int128>(MAX_INT);
  return expected;
}

class SignedIntegerArithmetic : public ::testing::Test
{
protected:
  static constexpr int64_t BATCH = 35;
  static constexpr int64_t BLOCK = 2048;
  static constexpr int64_t LEFT = 0;
  static constexpr int64_t RIGHT = 1;
  static constexpr int64_t RESULT = 2;
  static constexpr int64_t SENTINEL = 9000;

  SignedIntegerArithmetic()
    : arena_("SgnArithTest"), exec_(arena_), frame_{}, frames_{frame_.data()},
      eval_(exec_), batch_(eval_), expressions_{}, args_{&expressions_[LEFT], &expressions_[RIGHT]},
      minus_(arena_), mul_(arena_), cg_(arena_, nullptr, nullptr), raw_(arena_), skip_memory_{} {}

  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH);
    batch_.set_batch_idx(4);
  }

  void select(Operation op, bool left_batch, bool right_batch)
  {
    const ObItemType type = op == Operation::Minus ? T_OP_MINUS : T_OP_MUL;
    for (int64_t col = 0; col < 3; ++col) {
      ObExpr &expr = expressions_[col];
      const bool is_batch = col == RESULT ? left_batch || right_batch
          : (col == LEFT ? left_batch : right_batch);
      expr.type_ = col == RESULT ? type : T_REF_COLUMN;
      expr.obj_meta_.set_int(); expr.obj_meta_.set_scale(0);
      expr.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 20);
      expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(20, 0), arena_));
      expr.frame_idx_ = 0;
      expr.datum_off_ = col * BLOCK;
      expr.eval_info_off_ = col * BLOCK + 640;
      expr.eval_flags_off_ = col * BLOCK + 704;
      expr.pvt_skip_off_ = col * BLOCK + 768;
      expr.res_buf_off_ = col * BLOCK + 1024;
      expr.res_buf_len_ = 0;
      expr.batch_result_ = is_batch;
      expr.batch_idx_mask_ = is_batch ? UINT64_MAX : 0;
      expr.eval_func_ = nullptr; expr.eval_batch_func_ = nullptr;
      for (int64_t row = 0; row < BATCH; ++row) {
        Datum *value = new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
        value->set_int(SENTINEL + row);
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo{};
      expr.get_eval_info(eval_).notnull_ = true;
      expr.get_eval_info(eval_).evaluated_ = col != RESULT;
      expr.get_evaluated_flags(eval_).reset(BATCH);
      expr.get_pvt_skip(eval_).reset(BATCH);
    }
    skip().reset(BATCH);
    result().args_ = args_; result().arg_cnt_ = 2;
    raw_.set_expr_type(type);
    if (op == Operation::Minus) {
      ASSERT_EQ(OB_SUCCESS, minus_.cg_expr(cg_, raw_, result()));
      ASSERT_EQ(&ObExprMinus::minus_int_int, result().eval_func_);
      ASSERT_EQ(&ObExprMinus::minus_int_int_batch, result().eval_batch_func_);
    } else {
      ASSERT_EQ(OB_SUCCESS, mul_.cg_expr(cg_, raw_, result()));
      ASSERT_EQ(&ObExprMul::mul_int_int, result().eval_func_);
      ASSERT_EQ(&ObExprMul::mul_int_int_batch, result().eval_batch_func_);
    }
  }

  ObExpr &result() { return expressions_[RESULT]; }
  Datum &datum(int64_t col, int64_t row = 0)
  { return expressions_[col].locate_batch_datums(eval_)[row]; }
  ObBitVector &skip() { return *to_bit_vector(skip_memory_.data()); }
  int callback(int64_t size) { return result().eval_batch_func_(result(), eval_, skip(), size); }
  void fill()
  {
    for (int64_t row = 0; row < BATCH; ++row) {
      datum(LEFT, row).set_int(row - 8); datum(RIGHT, row).set_int(row % 5 - 2);
    }
  }
  void expect_value(int64_t row, int64_t value)
  {
    ASSERT_FALSE(datum(RESULT, row).is_null());
    EXPECT_EQ(0, datum(RESULT, row).length());
    EXPECT_EQ(Datum::NoReferenceFlags, datum(RESULT, row).reference_flags());
    EXPECT_EQ(value, datum(RESULT, row).get_int());
    EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
  }
  void expect_calculated(Operation op, int64_t row, bool left_batch = true, bool right_batch = true)
  {
    const Expected expected = oracle(op, datum(LEFT, left_batch ? row : 0).get_int(),
                                        datum(RIGHT, right_batch ? row : 0).get_int());
    ASSERT_FALSE(expected.overflow_);
    ASSERT_NO_FATAL_FAILURE(expect_value(row, expected.value_));
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 3 * BLOCK> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExpr expressions_[3];
  ObExpr *args_[2];
  ObExprMinus minus_;
  ObExprMul mul_;
  ObExprCGCtx cg_;
  ObOpRawExpr raw_;
  alignas(8) std::array<char, 64> skip_memory_;
};

TEST_F(SignedIntegerArithmetic, scalar_codegen_callbacks_match_exact_boundary_math_and_recompute_errors)
{
  const int64_t pairs[][2] = {
    {MIN_INT, -1}, {-1, MIN_INT}, {0, MIN_INT}, {MIN_INT, 0},
    {MIN_INT, 1}, {MAX_INT, -1}, {MAX_INT, 1}, {MAX_INT, 2}, {MIN_INT, 2},
    {MIN_INT, MIN_INT}, {MAX_INT, MAX_INT}, {-7, 3}, {-7, -3},
    {3037000499LL, 3037000499LL}, {3037000500LL, 3037000500LL}
  };
  for (Operation op : OPERATIONS) {
    SCOPED_TRACE(op == Operation::Minus ? "minus" : "multiply");
    for (const auto &pair : pairs) {
      SCOPED_TRACE(::testing::Message() << pair[0] << ", " << pair[1]);
      ASSERT_NO_FATAL_FAILURE(select(op, false, false));
      datum(LEFT).set_int(pair[0]); datum(RIGHT).set_int(pair[1]);
      const Expected expected = oracle(op, pair[0], pair[1]);
      Datum *value = nullptr;
      EXPECT_EQ(expected.overflow_ ? OB_OPERATE_OVERFLOW : OB_SUCCESS, result().eval(eval_, value));
      ASSERT_NE(nullptr, value);
      EXPECT_EQ(expected.overflow_, value->is_null());
      EXPECT_EQ(!expected.overflow_, result().get_eval_info(eval_).evaluated_);
      if (!expected.overflow_) { EXPECT_EQ(expected.value_, value->get_int()); }
      else {
        datum(LEFT).set_int(9); datum(RIGHT).set_int(3);
        ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
        EXPECT_EQ(oracle(op, 9, 3).value_, value->get_int());
        EXPECT_TRUE(result().get_eval_info(eval_).evaluated_);
      }
    }
    for (int nulls = 1; nulls < 4; ++nulls) {
      ASSERT_NO_FATAL_FAILURE(select(op, false, false));
      for (int col = 0; col < 2; ++col) {
        if (nulls & (1 << col)) { datum(col).set_null(); }
      }
      Datum *value = nullptr;
      ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
      ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
    }
  }
}

TEST_F(SignedIntegerArithmetic, real_batch_evaluation_covers_full_groups_tail_and_both_broadcasts)
{
  for (Operation op : OPERATIONS) {
    SCOPED_TRACE(op == Operation::Minus ? "minus" : "multiply");
    for (int shape = 0; shape < 3; ++shape) {
      const bool left_batch = shape != 1;
      const bool right_batch = shape != 2;
      ASSERT_NO_FATAL_FAILURE(select(op, left_batch, right_batch)); fill();
      if (!left_batch) { datum(LEFT).set_int(7); }
      if (!right_batch) { datum(RIGHT).set_int(-3); }
      ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), BATCH));
      for (int64_t row = 0; row < BATCH; ++row) {
        ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row, left_batch, right_batch));
      }
      EXPECT_EQ(BATCH, result().get_eval_info(eval_).cnt_);
      EXPECT_EQ(4, eval_.get_batch_idx());
      if (shape != 0) {
        ASSERT_NO_FATAL_FAILURE(select(op, left_batch, right_batch)); fill();
        const int64_t null_col = left_batch ? RIGHT : LEFT;
        datum(null_col).set_null();
        ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), BATCH));
        for (int64_t row = 0; row < BATCH; ++row) {
          EXPECT_TRUE(datum(RESULT, row).is_null());
          EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
        }
      }
    }
  }
}

TEST_F(SignedIntegerArithmetic, callbacks_preserve_masks_nulls_and_slots_outside_the_tail)
{
  for (Operation op : OPERATIONS) {
    SCOPED_TRACE(op == Operation::Minus ? "minus" : "multiply");
    ASSERT_NO_FATAL_FAILURE(select(op, true, true)); fill();
    skip().set(1); skip().set(16); result().get_evaluated_flags(eval_).set(3);
    datum(LEFT, 1).set_int(MIN_INT); datum(RIGHT, 1).set_int(op == Operation::Minus ? 1 : -1);
    datum(LEFT, 3).set_int(MAX_INT); datum(RIGHT, 3).set_int(op == Operation::Minus ? -1 : 2);
    datum(LEFT, 2).set_null();
    datum(RIGHT, 4).set_null();
    ASSERT_EQ(OB_SUCCESS, callback(19));
    for (int64_t row = 0; row < BATCH; ++row) {
      if (row == 1 || row == 3 || row == 16 || row >= 19) {
        EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int());
        EXPECT_EQ(row == 3, result().get_evaluated_flags(eval_).at(row));
      } else if (row == 2 || row == 4) {
        EXPECT_TRUE(datum(RESULT, row).is_null());
        EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
      } else { ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row)); }
    }
    EXPECT_FALSE(result().get_eval_info(eval_).notnull_);
    EXPECT_EQ(4, eval_.get_batch_idx());
    ASSERT_NO_FATAL_FAILURE(select(op, true, true)); fill();
    for (int64_t row = 0; row < 16; ++row) { skip().set(row); }
    for (int64_t row = 16; row < 32; ++row) { result().get_evaluated_flags(eval_).set(row); }
    ASSERT_EQ(OB_SUCCESS, callback(BATCH));
    for (int64_t row = 0; row < 32; ++row) {
      EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int());
      EXPECT_EQ(row >= 16, result().get_evaluated_flags(eval_).at(row));
    }
    for (int64_t row = 32; row < BATCH; ++row) {
      ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row));
    }
  }
}

TEST_F(SignedIntegerArithmetic, positive_and_negative_batch_overflow_keep_fast_and_slow_slots_recomputable)
{
  for (Operation op : OPERATIONS) {
    SCOPED_TRACE(op == Operation::Minus ? "minus" : "multiply");
    const int64_t errors[2][2] = {
      {op == Operation::Minus ? MAX_INT : MIN_INT, -1},
      {MIN_INT, op == Operation::Minus ? 1 : 2}
    };
    for (const auto &pair : errors) {
      for (bool fast : {true, false}) {
        ASSERT_NO_FATAL_FAILURE(select(op, true, true)); fill();
        const int64_t size = fast ? 18 : 5;
        const int64_t failure = fast ? 1 : 2;
        const int64_t first = oracle(op, datum(LEFT).get_int(), datum(RIGHT).get_int()).value_;
        datum(LEFT, failure).set_int(pair[0]); datum(RIGHT, failure).set_int(pair[1]);
        const Expected expected = oracle(op, pair[0], pair[1]);
        ASSERT_TRUE(expected.overflow_);
        EXPECT_EQ(OB_OPERATE_OVERFLOW, callback(size));
        EXPECT_EQ(first, datum(RESULT).get_int());
        EXPECT_EQ(expected.value_, datum(RESULT, failure).get_int());
        for (int64_t row = 0; row < BATCH; ++row) {
          EXPECT_EQ(!fast && row < failure, result().get_evaluated_flags(eval_).at(row));
          if (row > failure) { EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int()); }
        }
        EXPECT_EQ(4, eval_.get_batch_idx());
        datum(LEFT, failure).set_int(9); datum(RIGHT, failure).set_int(3);
        datum(LEFT).set_int(100); // Slow successful slots remain evaluated; fast group retries them.
        ASSERT_EQ(OB_SUCCESS, callback(size));
        for (int64_t row = 0; row < size; ++row) {
          if (!fast && row == 0) { ASSERT_NO_FATAL_FAILURE(expect_value(row, first)); }
          else { ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row)); }
        }
      }
    }
  }
}

TEST_F(SignedIntegerArithmetic, outer_batch_error_clears_visible_values_and_a_new_batch_recalculates)
{
  for (Operation op : OPERATIONS) {
    ASSERT_NO_FATAL_FAILURE(select(op, true, true)); fill();
    datum(LEFT, 2).set_int(MIN_INT); datum(RIGHT, 2).set_int(op == Operation::Minus ? 1 : -1);
    EXPECT_EQ(OB_OPERATE_OVERFLOW, result().eval_batch(eval_, skip(), 5));
    for (int64_t row = 0; row < 5; ++row) { EXPECT_TRUE(datum(RESULT, row).is_null()); }
    EXPECT_FALSE(result().get_evaluated_flags(eval_).at(2));
    // Start a fresh expression batch as the caller does; do not treat the
    // previous partial flags as valid after the outer evaluator cleared data.
    result().get_eval_info(eval_).clear_evaluated_flag();
    datum(LEFT, 2).set_int(9); datum(RIGHT, 2).set_int(3);
    ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), 5));
    for (int64_t row = 0; row < 5; ++row) {
      ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row));
    }
    EXPECT_EQ(4, eval_.get_batch_idx());
  }
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
