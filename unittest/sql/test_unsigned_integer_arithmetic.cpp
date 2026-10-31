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
#include "sql/engine/expr/ob_expr_add.h"
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
enum class Operation { Add, Minus, Multiply };
enum class Family { IntUInt, UIntInt, UIntUInt };
constexpr Operation OPERATIONS[] = {Operation::Add, Operation::Minus, Operation::Multiply};
constexpr Family FAMILIES[] = {Family::IntUInt, Family::UIntInt, Family::UIntUInt};
constexpr uint64_t MIN_INT_BITS = uint64_t{1} << 63;
constexpr uint64_t MAX_INT_BITS = MIN_INT_BITS - 1;
constexpr uint64_t MAX_UINT = std::numeric_limits<uint64_t>::max();

bool left_signed(Family family) { return family == Family::IntUInt; }
bool right_signed(Family family) { return family == Family::UIntInt; }
int64_t signed_value(uint64_t bits)
{
  int64_t value;
  MEMCPY(&value, &bits, sizeof(value));
  return value;
}
struct Pair { uint64_t left_; uint64_t right_; };
struct Expected { uint64_t value_; bool overflow_; };

Expected oracle(Operation op, Family family, Pair input)
{
  // U64 * U64 needs unsigned 128 bits; all mixed products fit signed 128.
  // Sums and differences of any two i64/u64 inputs also fit signed 128.
  if (op == Operation::Multiply && family == Family::UIntUInt) {
    const unsigned __int128 exact = static_cast<unsigned __int128>(input.left_) * input.right_;
    return {static_cast<uint64_t>(exact), exact > MAX_UINT};
  }
  const __int128 left = left_signed(family) ? static_cast<__int128>(signed_value(input.left_))
                                          : static_cast<__int128>(input.left_);
  const __int128 right = right_signed(family) ? static_cast<__int128>(signed_value(input.right_))
                                             : static_cast<__int128>(input.right_);
  const __int128 exact = op == Operation::Add ? left + right
      : op == Operation::Minus ? left - right : left * right;
  return {static_cast<uint64_t>(exact), exact < 0 || exact > static_cast<__int128>(MAX_UINT)};
}

Pair error_pair(Operation op, Family family, int index)
{
  if (op == Operation::Add) {
    if (family == Family::IntUInt) { return index == 0 ? Pair{MAX_UINT, 0} : Pair{MAX_INT_BITS, MAX_UINT}; }
    if (family == Family::UIntInt) { return index == 0 ? Pair{0, MAX_UINT} : Pair{MAX_UINT, MAX_INT_BITS}; }
    return index == 0 ? Pair{MAX_UINT, 1} : Pair{MAX_UINT, MAX_UINT};
  } else if (op == Operation::Minus) {
    if (family == Family::IntUInt) { return index == 0 ? Pair{0, 1} : Pair{MIN_INT_BITS, 0}; }
    if (family == Family::UIntInt) { return index == 0 ? Pair{0, 1} : Pair{MAX_UINT, MAX_UINT}; }
    return index == 0 ? Pair{0, 1} : Pair{0, MAX_UINT};
  } else {
    if (family == Family::IntUInt) { return index == 0 ? Pair{MAX_UINT, 1} : Pair{MAX_INT_BITS, MAX_UINT}; }
    if (family == Family::UIntInt) { return index == 0 ? Pair{1, MAX_UINT} : Pair{MAX_UINT, MAX_INT_BITS}; }
    return index == 0 ? Pair{MAX_UINT, 2} : Pair{MAX_UINT, MAX_UINT};
  }
}

class UnsignedIntegerArithmetic : public ::testing::Test
{
protected:
  static constexpr int64_t BATCH = 35;
  static constexpr int64_t BLOCK = 2048;
  static constexpr int64_t LEFT = 0;
  static constexpr int64_t RIGHT = 1;
  static constexpr int64_t RESULT = 2;
  static constexpr uint64_t SENTINEL = 9000;

  UnsignedIntegerArithmetic()
    : arena_("UIntArithTest"), exec_(arena_), frame_{}, frames_{frame_.data()},
      eval_(exec_), batch_(eval_), expressions_{}, args_{&expressions_[LEFT], &expressions_[RIGHT]},
      add_(arena_), minus_(arena_), mul_(arena_), cg_(arena_, nullptr, nullptr), raw_(arena_),
      skip_memory_{}, family_(Family::UIntUInt) {}

  void SetUp() override
  {
    exec_.set_frames(frames_); eval_.frames_ = frames_; eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH); batch_.set_batch_idx(4);
  }

  void select(Operation op, Family family, bool left_batch, bool right_batch)
  {
    family_ = family;
    const ObItemType type = op == Operation::Add ? T_OP_ADD
        : op == Operation::Minus ? T_OP_MINUS : T_OP_MUL;
    for (int64_t col = 0; col < 3; ++col) {
      ObExpr &expr = expressions_[col];
      const bool is_signed = col == LEFT ? left_signed(family)
          : col == RIGHT ? right_signed(family) : false;
      const bool is_batch = col == RESULT ? left_batch || right_batch
          : col == LEFT ? left_batch : right_batch;
      expr.type_ = col == RESULT ? type : T_REF_COLUMN;
      expr.obj_meta_.set_type(is_signed ? ObIntType : ObUInt64Type); expr.obj_meta_.set_scale(0);
      expr.datum_meta_ = ObDatumMeta(is_signed ? ObIntType : ObUInt64Type, CS_TYPE_BINARY, 0, 20);
      expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(20, 0), arena_));
      expr.frame_idx_ = 0; expr.datum_off_ = col * BLOCK;
      expr.eval_info_off_ = col * BLOCK + 640; expr.eval_flags_off_ = col * BLOCK + 704;
      expr.pvt_skip_off_ = col * BLOCK + 768;
      expr.res_buf_off_ = col * BLOCK + 1024; expr.res_buf_len_ = 0;
      expr.batch_result_ = is_batch; expr.batch_idx_mask_ = is_batch ? UINT64_MAX : 0;
      expr.eval_func_ = nullptr; expr.eval_batch_func_ = nullptr;
      for (int64_t row = 0; row < BATCH; ++row) {
        Datum *value = new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
        if (is_signed) { value->set_int(SENTINEL + row); }
        else { value->set_uint(SENTINEL + row); }
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo{};
      expr.get_eval_info(eval_).notnull_ = true;
      expr.get_eval_info(eval_).evaluated_ = col != RESULT;
      expr.get_evaluated_flags(eval_).reset(BATCH); expr.get_pvt_skip(eval_).reset(BATCH);
    }
    skip().reset(BATCH); result().args_ = args_; result().arg_cnt_ = 2;
    raw_.set_expr_type(type);
    if (op == Operation::Add) {
      ASSERT_EQ(OB_SUCCESS, add_.cg_expr(cg_, raw_, result()));
      ASSERT_EQ(family == Family::IntUInt ? &ObExprAdd::add_int_uint
          : family == Family::UIntInt ? &ObExprAdd::add_uint_int : &ObExprAdd::add_uint_uint,
          result().eval_func_);
      ASSERT_EQ(family == Family::IntUInt ? &ObExprAdd::add_int_uint_batch
          : family == Family::UIntInt ? &ObExprAdd::add_uint_int_batch : &ObExprAdd::add_uint_uint_batch,
          result().eval_batch_func_);
    } else if (op == Operation::Minus) {
      ASSERT_EQ(OB_SUCCESS, minus_.cg_expr(cg_, raw_, result()));
      ASSERT_EQ(family == Family::IntUInt ? &ObExprMinus::minus_int_uint
          : family == Family::UIntInt ? &ObExprMinus::minus_uint_int : &ObExprMinus::minus_uint_uint,
          result().eval_func_);
      ASSERT_EQ(family == Family::IntUInt ? &ObExprMinus::minus_int_uint_batch
          : family == Family::UIntInt ? &ObExprMinus::minus_uint_int_batch : &ObExprMinus::minus_uint_uint_batch,
          result().eval_batch_func_);
    } else {
      ASSERT_EQ(OB_SUCCESS, mul_.cg_expr(cg_, raw_, result()));
      ASSERT_EQ(family == Family::IntUInt ? &ObExprMul::mul_int_uint
          : family == Family::UIntInt ? &ObExprMul::mul_uint_int : &ObExprMul::mul_uint_uint,
          result().eval_func_);
      ASSERT_EQ(family == Family::IntUInt ? &ObExprMul::mul_int_uint_batch
          : family == Family::UIntInt ? &ObExprMul::mul_uint_int_batch : &ObExprMul::mul_uint_uint_batch,
          result().eval_batch_func_);
    }
  }

  ObExpr &result() { return expressions_[RESULT]; }
  Datum &datum(int64_t col, int64_t row = 0)
  { return expressions_[col].locate_batch_datums(eval_)[row]; }
  ObBitVector &skip() { return *to_bit_vector(skip_memory_.data()); }
  int callback(int64_t size) { return result().eval_batch_func_(result(), eval_, skip(), size); }
  void input(int64_t row, Pair pair)
  {
    if (left_signed(family_)) { datum(LEFT, row).set_int(signed_value(pair.left_)); }
    else { datum(LEFT, row).set_uint(pair.left_); }
    if (right_signed(family_)) { datum(RIGHT, row).set_int(signed_value(pair.right_)); }
    else { datum(RIGHT, row).set_uint(pair.right_); }
  }
  Pair input_at(int64_t row, bool left_batch = true, bool right_batch = true)
  {
    const Datum &left = datum(LEFT, left_batch ? row : 0);
    const Datum &right = datum(RIGHT, right_batch ? row : 0);
    return {left_signed(family_) ? static_cast<uint64_t>(left.get_int()) : left.get_uint(),
            right_signed(family_) ? static_cast<uint64_t>(right.get_int()) : right.get_uint()};
  }
  void fill()
  {
    for (int64_t row = 0; row < BATCH; ++row) { input(row, {static_cast<uint64_t>(40 + row), 3}); }
  }
  void expect_value(int64_t row, uint64_t expected)
  {
    EXPECT_FALSE(datum(RESULT, row).is_null());
    EXPECT_EQ(0, datum(RESULT, row).length());
    EXPECT_EQ(Datum::NoReferenceFlags, datum(RESULT, row).reference_flags());
    EXPECT_EQ(expected, datum(RESULT, row).get_uint());
    EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
  }
  void expect_calculated(Operation op, int64_t row, bool left_batch = true, bool right_batch = true)
  {
    const Expected expected = oracle(op, family_, input_at(row, left_batch, right_batch));
    ASSERT_FALSE(expected.overflow_); ASSERT_NO_FATAL_FAILURE(expect_value(row, expected.value_));
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 3 * BLOCK> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExpr expressions_[3];
  ObExpr *args_[2];
  ObExprAdd add_;
  ObExprMinus minus_;
  ObExprMul mul_;
  ObExprCGCtx cg_;
  ObOpRawExpr raw_;
  alignas(8) std::array<char, 64> skip_memory_;
  Family family_;
};

TEST_F(UnsignedIntegerArithmetic, scalar_and_single_row_batch_match_exact_boundary_math)
{
  const Pair pairs[] = {
    {0, 0}, {1, 0}, {0, 1}, {7, 3}, {static_cast<uint64_t>(-7), 3},
    {3, static_cast<uint64_t>(-7)}, {MIN_INT_BITS, 0}, {MIN_INT_BITS, 1},
    {MAX_INT_BITS, 1}, {MAX_UINT, 1}, {MAX_UINT, MAX_UINT}, {MIN_INT_BITS, MAX_UINT},
    {MAX_INT_BITS, MAX_UINT}, {MAX_UINT, MIN_INT_BITS}, {0, MAX_UINT}
  };
  for (Operation op : OPERATIONS) for (Family family : FAMILIES) {
    for (Pair pair : pairs) {
      SCOPED_TRACE(::testing::Message() << static_cast<int>(op) << "/" << static_cast<int>(family)
          << ":" << pair.left_ << "," << pair.right_);
      const Expected expected = oracle(op, family, pair);
      ASSERT_NO_FATAL_FAILURE(select(op, family, false, false)); input(0, pair);
      Datum *value = nullptr;
      EXPECT_EQ(expected.overflow_ ? OB_OPERATE_OVERFLOW : OB_SUCCESS, result().eval(eval_, value));
      ASSERT_NE(nullptr, value); EXPECT_EQ(expected.overflow_, value->is_null());
      EXPECT_EQ(!expected.overflow_, result().get_eval_info(eval_).evaluated_);
      if (!expected.overflow_) { EXPECT_EQ(expected.value_, value->get_uint()); }
      else {
        input(0, {9, 3}); ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
        EXPECT_EQ(oracle(op, family, {9, 3}).value_, value->get_uint());
      }
      ASSERT_NO_FATAL_FAILURE(select(op, family, true, true)); input(0, pair);
      EXPECT_EQ(expected.overflow_ ? OB_OPERATE_OVERFLOW : OB_SUCCESS, callback(1));
      EXPECT_EQ(expected.value_, datum(RESULT).get_uint()); // Existing write-before-check result.
      EXPECT_EQ(!expected.overflow_, result().get_evaluated_flags(eval_).at(0));
    }
  }
}

TEST_F(UnsignedIntegerArithmetic, null_scalar_operands_and_real_batches_preserve_both_broadcasts)
{
  for (Operation op : OPERATIONS) for (Family family : FAMILIES) {
    for (int nulls = 1; nulls < 4; ++nulls) {
      ASSERT_NO_FATAL_FAILURE(select(op, family, false, false));
      for (int col = 0; col < 2; ++col) if (nulls & (1 << col)) {
        datum(col).set_null();
      }
      Datum *value = nullptr; ASSERT_EQ(OB_SUCCESS, result().eval(eval_, value));
      ASSERT_NE(nullptr, value); EXPECT_TRUE(value->is_null());
    }
    for (int shape = 0; shape < 3; ++shape) {
      const bool left_batch = shape != 1;
      const bool right_batch = shape != 2;
      ASSERT_NO_FATAL_FAILURE(select(op, family, left_batch, right_batch)); fill();
      ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), BATCH));
      for (int64_t row = 0; row < BATCH; ++row) {
        ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row, left_batch, right_batch));
      }
      EXPECT_EQ(BATCH, result().get_eval_info(eval_).cnt_); EXPECT_EQ(4, eval_.get_batch_idx());
      if (shape != 0) {
        ASSERT_NO_FATAL_FAILURE(select(op, family, left_batch, right_batch)); fill();
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

TEST_F(UnsignedIntegerArithmetic, masks_nulls_and_tail_slots_do_not_evaluate_invalid_inputs)
{
  for (Operation op : OPERATIONS) for (Family family : FAMILIES) {
    ASSERT_NO_FATAL_FAILURE(select(op, family, true, true)); fill();
    skip().set(1); skip().set(16); result().get_evaluated_flags(eval_).set(3);
    input(1, error_pair(op, family, 0)); input(3, error_pair(op, family, 1));
    datum(LEFT, 2).set_null();
    datum(RIGHT, 4).set_null();
    ASSERT_EQ(OB_SUCCESS, callback(19));
    for (int64_t row = 0; row < BATCH; ++row) {
      if (row == 1 || row == 3 || row == 16 || row >= 19) {
        EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_uint());
        EXPECT_EQ(row == 3, result().get_evaluated_flags(eval_).at(row));
      } else if (row == 2 || row == 4) {
        EXPECT_TRUE(datum(RESULT, row).is_null());
        EXPECT_TRUE(result().get_evaluated_flags(eval_).at(row));
      } else { ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row)); }
    }
    EXPECT_FALSE(result().get_eval_info(eval_).notnull_); EXPECT_EQ(4, eval_.get_batch_idx());
    ASSERT_NO_FATAL_FAILURE(select(op, family, true, true)); fill();
    for (int64_t row = 0; row < 16; ++row) { skip().set(row); }
    for (int64_t row = 16; row < 32; ++row) { result().get_evaluated_flags(eval_).set(row); }
    ASSERT_EQ(OB_SUCCESS, callback(BATCH));
    for (int64_t row = 0; row < 32; ++row) {
      EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_uint());
      EXPECT_EQ(row >= 16, result().get_evaluated_flags(eval_).at(row));
    }
    for (int64_t row = 32; row < BATCH; ++row) { ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row)); }
  }
}

TEST_F(UnsignedIntegerArithmetic, underflow_and_overflow_keep_fast_and_slow_error_slots_recomputable)
{
  for (Operation op : OPERATIONS) for (Family family : FAMILIES) {
    for (int error = 0; error < 2; ++error) for (bool fast : {true, false}) {
      ASSERT_NO_FATAL_FAILURE(select(op, family, true, true)); fill();
      const int64_t size = fast ? 18 : 5;
      const int64_t failure = fast ? 1 : 2;
      const uint64_t first = oracle(op, family, input_at(0)).value_;
      const Pair pair = error_pair(op, family, error);
      const Expected expected = oracle(op, family, pair); ASSERT_TRUE(expected.overflow_);
      input(failure, pair);
      EXPECT_EQ(OB_OPERATE_OVERFLOW, callback(size));
      EXPECT_EQ(first, datum(RESULT).get_uint()); EXPECT_EQ(expected.value_, datum(RESULT, failure).get_uint());
      for (int64_t row = 0; row < BATCH; ++row) {
        EXPECT_EQ(!fast && row < failure, result().get_evaluated_flags(eval_).at(row));
        if (row > failure) { EXPECT_EQ(SENTINEL + row, datum(RESULT, row).get_uint()); }
      }
      input(failure, {9, 3}); input(0, {100, 3});
      ASSERT_EQ(OB_SUCCESS, callback(size));
      for (int64_t row = 0; row < size; ++row) {
        if (!fast && row == 0) { ASSERT_NO_FATAL_FAILURE(expect_value(row, first)); }
        else { ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row)); }
      }
      EXPECT_EQ(4, eval_.get_batch_idx());
    }
  }
}

TEST_F(UnsignedIntegerArithmetic, outer_batch_failure_clears_values_and_new_batch_recalculates)
{
  for (Operation op : OPERATIONS) for (Family family : FAMILIES) {
    ASSERT_NO_FATAL_FAILURE(select(op, family, true, true)); fill();
    input(2, error_pair(op, family, 0));
    EXPECT_EQ(OB_OPERATE_OVERFLOW, result().eval_batch(eval_, skip(), 5));
    for (int64_t row = 0; row < 5; ++row) { EXPECT_TRUE(datum(RESULT, row).is_null()); }
    EXPECT_FALSE(result().get_evaluated_flags(eval_).at(2));
    result().get_eval_info(eval_).clear_evaluated_flag(); input(2, {9, 3});
    ASSERT_EQ(OB_SUCCESS, result().eval_batch(eval_, skip(), 5));
    for (int64_t row = 0; row < 5; ++row) { ASSERT_NO_FATAL_FAILURE(expect_calculated(op, row)); }
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
