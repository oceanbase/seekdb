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

#include "common/datum/datum.h"
#include "lib/charset/ob_charset.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_batch_eval_util.h"
#include "sql/engine/expr/ob_expr_add.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include <gtest/gtest.h>
#include <array>
#include <limits>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
constexpr int64_t MIN_INT = std::numeric_limits<int64_t>::min();
constexpr int64_t MAX_INT = std::numeric_limits<int64_t>::max();

class SignedAddAccessors : public ::testing::Test
{
protected:
  static constexpr int64_t BATCH = 35;
  static constexpr int64_t BLOCK = 2048;
  static constexpr int64_t LEFT = 0;
  static constexpr int64_t RIGHT = 1;
  static constexpr int64_t RESULT = 2;
  static constexpr int64_t SENTINEL = 7000;

  SignedAddAccessors()
    : arena_("SgnAddTest"), exec_(arena_), frame_{}, frames_{frame_.data()},
      eval_(exec_), batch_(eval_), expressions_{}, args_{&expressions_[LEFT], &expressions_[RIGHT]},
      add_(arena_), cg_(arena_, nullptr, nullptr), raw_(arena_), skip_memory_{} {}

  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH);
    batch_.set_batch_idx(2);
    raw_.set_expr_type(T_OP_ADD);
    ASSERT_NO_FATAL_FAILURE(configure(false, false));
  }

  void configure(bool left_batch, bool right_batch)
  {
    for (int64_t col = 0; col < 3; ++col) {
      ObExpr &expr = expressions_[col];
      const bool is_batch = col == RESULT ? left_batch || right_batch
          : (col == LEFT ? left_batch : right_batch);
      expr.type_ = col == RESULT ? T_OP_ADD : T_REF_COLUMN;
      expr.obj_meta_.set_int();
      expr.obj_meta_.set_scale(0);
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
      expr.eval_func_ = nullptr;
      expr.eval_batch_func_ = nullptr;
      expr.extra_ = col;
      for (int64_t row = 0; row < BATCH; ++row) {
        auto *value = new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
        value->set_int(SENTINEL + row);
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo{};
      expr.get_eval_info(eval_).notnull_ = true;
      expr.get_eval_info(eval_).evaluated_ = col != RESULT;
      expr.get_evaluated_flags(eval_).reset(BATCH);
      expr.get_pvt_skip(eval_).reset(BATCH);
    }
    skip().reset(BATCH);
    result().args_ = args_;
    result().arg_cnt_ = 2;
    ASSERT_EQ(OB_SUCCESS, add_.cg_expr(cg_, raw_, result()));
    ASSERT_EQ(&ObExprAdd::add_int_int, result().eval_func_);
    ASSERT_EQ(&ObExprAdd::add_int_int_batch, result().eval_batch_func_);
    operand_calls_[0] = operand_calls_[1] = 0;
  }

  ObExpr &result() { return expressions_[RESULT]; }
  Datum &datum(int64_t col, int64_t row = 0)
  { return expressions_[col].locate_batch_datums(eval_)[row]; }
  ObBitVector &skip() { return *to_bit_vector(skip_memory_.data()); }
  void fill()
  {
    for (int64_t row = 0; row < BATCH; ++row) {
      datum(LEFT, row).set_int(row - 17);
      datum(RIGHT, row).set_int(2 * row + 3);
    }
  }
  int batch_callback(int64_t size)
  { return result().eval_batch_func_(result(), eval_, skip(), size); }
  void expect_integer(int64_t row, int64_t expected)
  {
    EXPECT_FALSE(datum(RESULT, row).is_null());
    EXPECT_EQ(0, datum(RESULT, row).length());
    EXPECT_EQ(Datum::NoReferenceFlags, datum(RESULT, row).reference_flags());
    EXPECT_EQ(expected, datum(RESULT, row).get_int());
    EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
  }
  static int scalar_operand(const ObExpr &expr, ObEvalCtx &, Datum &value)
  {
    ++operand_calls_[expr.extra_];
    value.set_int(23);
    return expr.extra_ == LEFT ? OB_TIMEOUT : OB_SUCCESS;
  }
  static int batch_operand(const ObExpr &expr, ObEvalCtx &, const ObBitVector &, int64_t)
  {
    ++operand_calls_[expr.extra_];
    return expr.extra_ == LEFT ? OB_TIMEOUT : OB_SUCCESS;
  }

  static int operand_calls_[2];
  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 3 * BLOCK> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExpr expressions_[3];
  ObExpr *args_[2];
  ObExprAdd add_;
  ObExprCGCtx cg_;
  ObOpRawExpr raw_;
  alignas(8) std::array<char, 64> skip_memory_;
};
int SignedAddAccessors::operand_calls_[2] = {0, 0};

TEST_F(SignedAddAccessors, scalar_callback_selected_by_codegen_preserves_extreme_values)
{
  const int64_t cases[][3] = {
    {MAX_INT, 0, MAX_INT}, {MIN_INT, 0, MIN_INT},
    {MAX_INT, MIN_INT, -1}, {MIN_INT, MAX_INT, -1}, {-7, 3, -4}, {23, 19, 42}
  };
  for (const auto &item : cases) {
    datum(LEFT).set_int(item[0]); datum(RIGHT).set_int(item[1]);
    result().get_eval_info(eval_).clear_evaluated_flag();
    Datum *value = nullptr;
    ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
    ASSERT_EQ(&datum(RESULT), value);
    EXPECT_EQ(item[2], value->get_int());
    EXPECT_EQ(0, value->length());
    EXPECT_EQ(Datum::NoReferenceFlags, value->reference_flags());
    EXPECT_TRUE(result().get_eval_info(eval_).evaluated_);
    EXPECT_EQ(item[0], datum(LEFT).get_int()); EXPECT_EQ(item[1], datum(RIGHT).get_int());
  }
}

TEST_F(SignedAddAccessors, scalar_nulls_do_not_read_payload_and_still_evaluate_right_operand)
{
  for (int nulls = 1; nulls < 4; ++nulls) {
    ASSERT_NO_FATAL_FAILURE(configure(false, false));
    for (int col = 0; col < 2; ++col) {
      if (nulls & (1 << col)) { datum(col).set_null(); }
      else { datum(col).set_int(8); }
    }
    Datum *value = nullptr;
    ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
    EXPECT_TRUE(value->is_null());
  }
  ASSERT_NO_FATAL_FAILURE(configure(false, false));
  datum(LEFT).set_null();
  expressions_[RIGHT].eval_func_ = scalar_operand;
  expressions_[RIGHT].get_eval_info(eval_).clear_evaluated_flag();
  Datum *value = nullptr;
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
  EXPECT_TRUE(value->is_null()); EXPECT_EQ(1, operand_calls_[RIGHT]);
}

TEST_F(SignedAddAccessors, scalar_positive_overflow_is_not_published_as_evaluated)
{
  datum(LEFT).set_int(MAX_INT); datum(RIGHT).set_int(1);
  Datum *value = nullptr;
  EXPECT_EQ(OB_OPERATE_OVERFLOW, result().eval(eval_, value));
  ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
  EXPECT_FALSE(result().get_eval_info(eval_).evaluated_);
  datum(LEFT).set_int(41);
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
  EXPECT_EQ(42, value->get_int());
  EXPECT_TRUE(result().get_eval_info(eval_).evaluated_);
}

TEST_F(SignedAddAccessors, scalar_negative_overflow_is_not_published_as_evaluated)
{
  datum(LEFT).set_int(MIN_INT); datum(RIGHT).set_int(-1);
  Datum *value = nullptr;
  EXPECT_EQ(OB_OPERATE_OVERFLOW, result().eval(eval_, value));
  ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
  EXPECT_FALSE(result().get_eval_info(eval_).evaluated_);
  datum(RIGHT).set_int(1);
  ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
  EXPECT_EQ(MIN_INT + 1, value->get_int());
  EXPECT_TRUE(result().get_eval_info(eval_).evaluated_);
}

TEST_F(SignedAddAccessors, real_batch_evaluator_handles_two_full_groups_and_tail)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, true)); fill();
  ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), BATCH));
  for (int64_t row = 0; row < BATCH; ++row) {
    ASSERT_NO_FATAL_FAILURE(expect_integer(row, 3 * row - 14));
  }
  EXPECT_EQ(BATCH, result().get_eval_info(eval_).cnt_);
  EXPECT_TRUE(result().get_eval_info(eval_).evaluated_);
  EXPECT_EQ(2, eval_.get_batch_idx()); EXPECT_EQ(BATCH, eval_.get_batch_size());
}

TEST_F(SignedAddAccessors, batch_callback_broadcasts_scalar_left)
{
  ASSERT_NO_FATAL_FAILURE(configure(false, true)); fill(); datum(LEFT).set_int(9);
  ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), BATCH));
  for (int64_t row = 0; row < BATCH; ++row) {
    ASSERT_NO_FATAL_FAILURE(expect_integer(row, 2 * row + 12));
  }
  EXPECT_EQ(9, datum(LEFT).get_int()); EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(SignedAddAccessors, batch_callback_broadcasts_scalar_right)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, false)); fill(); datum(RIGHT).set_int(-4);
  ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), BATCH));
  for (int64_t row = 0; row < BATCH; ++row) {
    ASSERT_NO_FATAL_FAILURE(expect_integer(row, row - 21));
  }
  EXPECT_EQ(-4, datum(RIGHT).get_int()); EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(SignedAddAccessors, batch_callback_marks_nulls_without_reading_their_payloads)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, true)); fill();
  datum(LEFT, 1).set_null();
  datum(RIGHT, 2).set_null();
  datum(LEFT, 3).set_null();
  datum(RIGHT, 3).set_null();
  ASSERT_EQ(OB_SUCCESS, batch_callback(BATCH));
  for (int64_t row = 0; row < BATCH; ++row) {
    if (row >= 1 && row <= 3) {
      EXPECT_TRUE(datum(RESULT, row).is_null());
      EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
    } else { ASSERT_NO_FATAL_FAILURE(expect_integer(row, 3 * row - 14)); }
  }
  EXPECT_FALSE(result().get_eval_info(eval_).notnull_);
  EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(SignedAddAccessors, batch_callback_preserves_skip_evaluated_and_slots_beyond_tail)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, true)); fill();
  const int64_t size = 19;
  skip().set(1); skip().set(16);
  result().get_evaluated_flags(eval_).set(3);
  // These would overflow if skipped or already-evaluated operands were read.
  datum(LEFT, 1).set_int(MAX_INT); datum(RIGHT, 1).set_int(1);
  datum(LEFT, 3).set_int(MIN_INT); datum(RIGHT, 3).set_int(-1);
  ASSERT_EQ(OB_SUCCESS, batch_callback(size));
  for (int64_t row = 0; row < BATCH; ++row) {
    if (row == 1 || row == 3 || row == 16 || row >= size) {
      EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int());
      EXPECT_EQ(row == 3, result().get_evaluated_flags(eval_).at(row));
    } else { ASSERT_NO_FATAL_FAILURE(expect_integer(row, 3 * row - 14)); }
  }
  EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(SignedAddAccessors, batch_callback_skips_whole_bitmap_groups)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, true)); fill();
  for (int64_t row = 0; row < 16; ++row) { skip().set(row); }
  for (int64_t row = 16; row < 32; ++row) { result().get_evaluated_flags(eval_).set(row); }
  ASSERT_EQ(OB_SUCCESS, batch_callback(BATCH));
  for (int64_t row = 0; row < 32; ++row) {
    EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int());
    EXPECT_EQ(row >= 16, result().get_evaluated_flags(eval_).at(row));
  }
  for (int64_t row = 32; row < BATCH; ++row) {
    ASSERT_NO_FATAL_FAILURE(expect_integer(row, 3 * row - 14));
  }
}

TEST_F(SignedAddAccessors, batch_positive_overflow_does_not_mark_an_incomplete_fast_group)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, true)); fill();
  datum(LEFT, 1).set_int(MAX_INT); datum(RIGHT, 1).set_int(1);
  EXPECT_EQ(OB_OPERATE_OVERFLOW, batch_callback(18));
  EXPECT_EQ(-14, datum(RESULT, 0).get_int());
  EXPECT_EQ(MIN_INT, datum(RESULT, 1).get_int()); // Old write-before-check contract.
  for (int64_t row = 0; row < BATCH; ++row) {
    EXPECT_FALSE(result().get_evaluated_flags(eval_).at(row));
    if (row > 1) { EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int()); }
  }
  EXPECT_EQ(2, eval_.get_batch_idx());
  datum(LEFT, 0).set_int(100);
  datum(LEFT, 1).set_int(10);
  ASSERT_EQ(OB_SUCCESS, batch_callback(18));
  ASSERT_NO_FATAL_FAILURE(expect_integer(0, 103));
  ASSERT_NO_FATAL_FAILURE(expect_integer(1, 11));
  for (int64_t row = 2; row < 18; ++row) {
    ASSERT_NO_FATAL_FAILURE(expect_integer(row, 3 * row - 14));
  }
}

TEST_F(SignedAddAccessors, batch_negative_overflow_keeps_only_completed_slow_slots_evaluated)
{
  ASSERT_NO_FATAL_FAILURE(configure(true, true)); fill();
  datum(LEFT, 2).set_int(MIN_INT); datum(RIGHT, 2).set_int(-1);
  EXPECT_EQ(OB_OPERATE_OVERFLOW, batch_callback(5));
  ASSERT_NO_FATAL_FAILURE(expect_integer(0, -14));
  ASSERT_NO_FATAL_FAILURE(expect_integer(1, -11));
  EXPECT_EQ(MAX_INT, datum(RESULT, 2).get_int());
  for (int64_t row = 2; row < BATCH; ++row) {
    EXPECT_FALSE(result().get_evaluated_flags(eval_).at(row));
    if (row > 2) { EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int()); }
  }
  EXPECT_EQ(2, eval_.get_batch_idx());
  datum(LEFT, 0).set_int(100); // Completed slow slots remain evaluated.
  datum(RIGHT, 2).set_int(1);
  ASSERT_EQ(OB_SUCCESS, batch_callback(5));
  ASSERT_NO_FATAL_FAILURE(expect_integer(0, -14));
  ASSERT_NO_FATAL_FAILURE(expect_integer(1, -11));
  ASSERT_NO_FATAL_FAILURE(expect_integer(2, MIN_INT + 1));
  ASSERT_NO_FATAL_FAILURE(expect_integer(3, -5));
  ASSERT_NO_FATAL_FAILURE(expect_integer(4, -2));
}

TEST_F(SignedAddAccessors, operand_error_stops_scalar_and_batch_before_the_addition)
{
  for (bool is_batch : {false, true}) {
    ASSERT_NO_FATAL_FAILURE(configure(is_batch, is_batch));
    for (int64_t col = 0; col < 2; ++col) {
      expressions_[col].get_eval_info(eval_).clear_evaluated_flag();
      expressions_[col].eval_func_ = scalar_operand;
      expressions_[col].eval_batch_func_ = batch_operand;
    }
    if (is_batch) { EXPECT_EQ(OB_TIMEOUT, batch_callback(5)); }
    else {
      // Call the selected scalar callback directly to observe its publication
      // boundary; ObExpr::eval additionally nulls the result after an error.
      EXPECT_EQ(OB_TIMEOUT, result().eval_func_(result(), eval_, datum(RESULT)));
    }
    EXPECT_EQ(1, operand_calls_[LEFT]); EXPECT_EQ(0, operand_calls_[RIGHT]);
    for (int64_t row = 0; row < BATCH; ++row) {
      EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_int());
      EXPECT_FALSE(result().get_evaluated_flags(eval_).at(row));
    }
  }
}

// Supplement only: instantiate the production accessor boundary with a
// carrier that has no ptr_/pack_. Real add/overflow policy is exercised above
// through the callbacks chosen by ObExprAdd::cg_expr, not by this small policy.
struct AccessorPolicy : public ObArithOpRawType<int64_t, int64_t, int64_t>
{
  static void raw_op(int64_t &res, int64_t left, int64_t right) { res = left + right; }
  static int raw_check(int64_t, int64_t, int64_t) { return OB_SUCCESS; }
};

TEST(SignedAddCarrierCompatibility, inline_carrier_uses_the_same_accessors_and_canonical_result)
{
  Datum left, right, result;
  left.set_int(9); right.set_int(-4);
  const char previous[] = "reference";
  ASSERT_EQ(OB_SUCCESS, result.set_reference(previous, sizeof(previous), Datum::OutRow));
  EXPECT_EQ(OB_SUCCESS, ObSignedIntArithOpWrap<AccessorPolicy>::datum_op(result, left, right));
  EXPECT_EQ(5, result.get_int()); EXPECT_TRUE(result.is_present());
  EXPECT_EQ(0, result.length()); EXPECT_EQ(Datum::NoReferenceFlags, result.reference_flags());
  left.set_null();
  EXPECT_EQ(OB_SUCCESS, ObSignedIntArithOpWrap<AccessorPolicy>::datum_op(result, left, right));
  EXPECT_TRUE(result.is_null()); EXPECT_EQ(0, result.length());
  EXPECT_EQ(Datum::NoReferenceFlags, result.reference_flags());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
