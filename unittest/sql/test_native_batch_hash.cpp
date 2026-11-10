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

#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "share/datum/datum_hash_value_calculator.h"
#include "share/datum/ob_datum_funcs.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_func_partition_key.h"
#include "sql/engine/expr/ob_expr_hash.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include <gtest/gtest.h>
#include <array>
#include <cstdint>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
enum class Operation { Hash, Partition };
constexpr Operation OPERATIONS[] = {Operation::Hash, Operation::Partition};
constexpr int64_t BATCH = 35;
constexpr int64_t GUARD = BATCH * sizeof(uint64_t) + 16;

class GuardAllocator final : public ObIAllocator
{
public:
  GuardAllocator() : arena_("BatchHashTest"), allocations_() {}
  void *alloc(int64_t size) override
  {
    char *bytes = static_cast<char *>(arena_.alloc(size + GUARD));
    if (bytes != nullptr) {
      MEMSET(bytes + size, 0xa7, GUARD);
      allocations_.push_back({bytes, size});
    }
    return bytes;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *bytes) override { arena_.free(bytes); }
  void expect_guards() const
  {
    for (const Allocation &allocation : allocations_) {
      for (int64_t i = 0; i < GUARD; ++i) {
        ASSERT_EQ(0xa7, static_cast<unsigned char>(allocation.bytes_[allocation.size_ + i]));
      }
    }
  }
  size_t allocation_count() const { return allocations_.size(); }
private:
  struct Allocation { char *bytes_; int64_t size_; };
  ObArenaAllocator arena_;
  std::vector<Allocation> allocations_;
};

class HashBatch
{
public:
  explicit HashBatch(Operation operation)
    : owner_(), exec_(owner_), eval_(exec_), batch_(eval_), info_(owner_),
      cg_(owner_, nullptr, nullptr, 0, 0), factory_(owner_), operation_(operation),
      raw_(nullptr), argument_raw_{nullptr, nullptr}, tail_raw_(nullptr),
      result_(nullptr), arguments_{nullptr, nullptr}, tail_(nullptr),
      values_{}, nulls_{}, broadcast_{false, false}, decimal_(false), skip_words_{} {}

  void prepare(bool left_broadcast = false, bool right_broadcast = false,
               bool decimal = false, bool reference_neighbor = false)
  {
    decimal_ = decimal;
    broadcast_[0] = left_broadcast; broadcast_[1] = right_broadcast;
    ASSERT_FALSE(decimal && left_broadcast);
    ASSERT_FALSE(left_broadcast && right_broadcast);
    const ObItemType operation_type = operation_ == Operation::Hash ? T_FUN_SYS_HASH : T_FUN_SYS_PART_KEY;
    ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(operation_type, raw_));
    ASSERT_EQ(OB_SUCCESS, raw_->init_param_exprs(2));
    set_type(*raw_, operation_ == Operation::Hash ? ObUInt64Type : ObIntType, 20);
    ASSERT_EQ(OB_SUCCESS, raw_->add_flag(CNT_COLUMN));
    for (int64_t col = 0; col < 2; ++col) {
      const SqlValueType kind = decimal && col == 0 ? ObDecimalIntType : ObIntType;
      if (broadcast_[col]) {
        ObConstRawExpr *constant = nullptr;
        ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_INT, constant));
        DatumValue value;
        ASSERT_EQ(OB_SUCCESS, value.set_int(col == 0 ? -11 : 17, owner_));
        ASSERT_EQ(OB_SUCCESS, constant->set_value_borrowed(value));
        argument_raw_[col] = constant;
      } else {
        ObColumnRefRawExpr *column = nullptr;
        ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_REF_COLUMN, column));
        ASSERT_EQ(OB_SUCCESS, column->add_flag(IS_COLUMN));
        ASSERT_EQ(OB_SUCCESS, column->add_flag(CNT_COLUMN));
        argument_raw_[col] = column;
      }
      set_type(*argument_raw_[col], kind, kind == ObDecimalIntType ? 30 : 20);
      ASSERT_EQ(OB_SUCCESS, raw_->add_param_expr(argument_raw_[col]));
    }
    // CG owns the IS_MARKED traversal state while flattening these roots.
    // A unique input set would mark them here and make CG omit the roots.
    ObRawExprUniqueSet roots(false);
    ASSERT_EQ(OB_SUCCESS, roots.append(raw_));
    if (reference_neighbor) {
      ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_REF_COLUMN, tail_raw_));
      ObRawExprResType type;
      type.set_varchar(); type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      type.set_collation_level(CS_LEVEL_IMPLICIT); type.set_length(32);
      tail_raw_->set_result_type(type);
      ASSERT_EQ(OB_SUCCESS, tail_raw_->add_flag(IS_COLUMN));
      ASSERT_EQ(OB_SUCCESS, tail_raw_->add_flag(CNT_COLUMN));
      ASSERT_EQ(OB_SUCCESS, roots.append(tail_raw_));
    }
    // Public production CG installs the actual operator callback, binds hash
    // semantics and arranges the zero-payload native output frame.
    ASSERT_FALSE(raw_->has_flag(IS_MARKED));
    if (reference_neighbor) {
      ASSERT_FALSE(tail_raw_->has_flag(IS_MARKED));
    }
    cg_.set_batch_size(BATCH);
    ASSERT_EQ(OB_SUCCESS, cg_.generate(roots, info_));
    ASSERT_EQ(reference_neighbor ? 4 : 3, info_.rt_exprs_.count());
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    ASSERT_EQ(OB_SUCCESS, info_.pre_alloc_exec_memory(exec_));
    eval_.frames_ = exec_.get_frames(); eval_.max_batch_size_ = BATCH;
    batch_.set_batch_size(BATCH); batch_.set_batch_idx(3);
    result_ = runtime(*raw_);
    ASSERT_NE(nullptr, result_);
    ASSERT_TRUE(result_->is_batch_result());
    ASSERT_EQ(0U, result_->res_buf_len_);
    ASSERT_EQ(operation_ == Operation::Hash ? &ObExprHash::calc_hash_value_expr_batch
        : &ObExprFuncPartKey::calc_partition_key_batch, result_->eval_batch_func_);
    for (int64_t col = 0; col < 2; ++col) {
      arguments_[col] = runtime(*argument_raw_[col]);
      ASSERT_NE(nullptr, arguments_[col]);
      ASSERT_EQ(!broadcast_[col], arguments_[col]->is_batch_result());
      for (int64_t row = 0; row < BATCH; ++row) {
        values_[col][row] = broadcast_[col] ? (col == 0 ? -11 : 17)
            : row == 0 ? INT64_MIN : row == 1 ? INT64_MAX : col == 0 ? row - 12 : 91 - row;
        nulls_[col][row] = !broadcast_[col] && row == 4;
      }
      if (!broadcast_[col]) {
        for (int64_t row = 0; row < BATCH; ++row) {
          if (decimal && col == 0) {
            // Known precision 30 keeps legal actual 0/4/8-byte native spans.
            // Every chosen coefficient fits signed32; the partition oracle
            // hashes precisely its mathematical minimal four-byte encoding.
            values_[col][row] = row % 3 == 0 ? 0 : row % 3 == 1 ? -7 : 19;
            nulls_[col][row] = false;
            const int32_t width = row % 3 == 0 ? 0 : row % 3 == 1 ? 4 : 8;
            ObDatum legacy;
            legacy.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(&values_[col][row]), width);
            ASSERT_EQ(OB_SUCCESS, arguments_[col]->get_datum_copy_plan().import_legacy(legacy, input(col, row)));
          } else if (nulls_[col][row]) {
            input(col, row).set_null();
          } else {
            input(col, row).set_int(values_[col][row]);
          }
        }
      }
    }
    if (reference_neighbor) {
      tail_ = runtime(*tail_raw_);
      ASSERT_NE(nullptr, tail_);
      ASSERT_EQ(result_->frame_idx_, tail_->frame_idx_);
      ASSERT_EQ(result_->res_buf_off_, tail_->res_buf_off_);
      ASSERT_GT(tail_->res_buf_len_, 0U);
      char *bytes = eval_.frames_[tail_->frame_idx_] + tail_->res_buf_off_;
      MEMSET(bytes, 0x6b, tail_->res_buf_len_ * BATCH);
    } else if (!decimal) {
      // The old uint64[batch] scratch began exactly after this allocation.
      bool found = false;
      for (int64_t i = 0; i < info_.datum_frame_.count(); ++i) {
        const ObFrameInfo &frame = info_.datum_frame_.at(i);
        if (frame.frame_idx_ == result_->frame_idx_) {
          EXPECT_EQ(frame.frame_size_, result_->res_buf_off_); found = true;
        }
      }
      ASSERT_TRUE(found);
    }
    skip().reset(BATCH);
    // Separate optional one-time request service setup from the callback's
    // no-scratch-allocation assertion below.
    const ObDatumAccessContext *access_context = nullptr;
    ASSERT_EQ(OB_SUCCESS, eval_.get_datum_access_ctx(access_context));
  }

  static void set_type(ObRawExpr &raw, SqlValueType kind, ObPrecision precision)
  {
    ObRawExprResType type;
    type.set_type(kind); type.set_collation_type(CS_TYPE_BINARY);
    type.set_collation_level(CS_LEVEL_NUMERIC); type.set_scale(0);
    type.set_accuracy(ObAccuracy(precision, 0)); raw.set_result_type(type);
  }
  ObExpr *runtime(const ObRawExpr &raw)
  { return static_cast<ObExpr *>(cg_.get_left_value_rt_expr(raw)); }
  Datum &input(int64_t col, int64_t row)
  { return arguments_[col]->locate_batch_datums(eval_)[row]; }
  Datum &output(int64_t row) { return result_->locate_batch_datums(eval_)[row]; }
  ObBitVector &skip() { return *to_bit_vector(skip_words_.data()); }
  int evaluate(int64_t count = BATCH) { return result_->eval_batch(eval_, skip(), count); }

  void expected(int64_t row, uint64_t &result) const
  {
    result = operation_ == Operation::Hash ? 16777213 : 0;
    for (int64_t col = 0; col < 2; ++col) {
      ObDatum legacy;
      alignas(8) char wide_coefficient[16];
      const bool decimal = decimal_ && col == 0;
      if (decimal) {
        MEMSET(wide_coefficient, values_[col][row] < 0 ? 0xff : 0, sizeof(wide_coefficient));
        MEMCPY(wide_coefficient, &values_[col][row], sizeof(int64_t));
        legacy.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(wide_coefficient), 16);
      } else {
        legacy.ptr_ = reinterpret_cast<const char *>(&values_[col][row]); legacy.pack_ = 8;
        if (nulls_[col][row]) { legacy.set_null(); }
      }
      if (operation_ == Operation::Partition && decimal) {
        const int32_t minimal_coefficient = static_cast<int32_t>(values_[col][row]);
        result = DatumMurmurHash::hash(&minimal_coefficient, sizeof(minimal_coefficient), result);
      } else {
        const ObDatumBasicFuncs *legacy_functions = ObDatumFuncs::get_basic_func(
            decimal ? ObDecimalIntType : ObIntType, CS_TYPE_BINARY, 0, false, decimal ? 30 : 20);
        ASSERT_NE(nullptr, legacy_functions);
        const ObDatumHashFuncType function = operation_ == Operation::Hash
            ? legacy_functions->murmur_hash_v2_ : legacy_functions->murmur_hash_;
        ASSERT_NE(nullptr, function);
        ASSERT_EQ(OB_SUCCESS, function(legacy, result, result, nullptr));
      }
    }
    if (operation_ == Operation::Partition) {
      // Unsigned arithmetic describes the existing signed absolute hash bits
      // without signed-negation overflow in the test oracle.
      if ((result >> 63) != 0) { result = uint64_t(0) - result; }
    }
  }
  uint64_t result_bits(int64_t row)
  { return operation_ == Operation::Hash ? output(row).get_uint() : static_cast<uint64_t>(output(row).get_int()); }
  void expect_row(int64_t row)
  {
    uint64_t value = 0;
    ASSERT_NO_FATAL_FAILURE(expected(row, value));
    ASSERT_FALSE(output(row).is_null());
    EXPECT_EQ(0U, output(row).length());
    EXPECT_EQ(Datum::NoReferenceFlags, output(row).reference_flags());
    EXPECT_EQ(value, result_bits(row));
    EXPECT_TRUE(result_->get_evaluated_flags(eval_).at(row));
  }
  void expect_guards()
  {
    ASSERT_NO_FATAL_FAILURE(owner_.expect_guards());
    if (tail_ != nullptr) {
      const char *bytes = eval_.frames_[tail_->frame_idx_] + tail_->res_buf_off_;
      for (int64_t i = 0; i < tail_->res_buf_len_ * BATCH; ++i) {
        ASSERT_EQ(0x6b, static_cast<unsigned char>(bytes[i]));
      }
    }
  }

  GuardAllocator owner_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  ObExprFrameInfo info_;
  ObStaticEngineExprCG cg_;
  ObRawExprFactory factory_;
  Operation operation_;
  ObSysFunRawExpr *raw_;
  ObRawExpr *argument_raw_[2];
  ObColumnRefRawExpr *tail_raw_;
  ObExpr *result_;
  ObExpr *arguments_[2];
  ObExpr *tail_;
  std::array<std::array<int64_t, BATCH>, 2> values_;
  std::array<std::array<bool, BATCH>, 2> nulls_;
  bool broadcast_[2];
  bool decimal_;
  std::array<uint64_t, (BATCH + 63) / 64> skip_words_;
};

void evaluate_all(HashBatch &test)
{
  const size_t allocations = test.owner_.allocation_count();
  ASSERT_EQ(OB_SUCCESS, test.evaluate());
  EXPECT_EQ(allocations, test.owner_.allocation_count());
  for (int64_t row = 0; row < BATCH; ++row) { ASSERT_NO_FATAL_FAILURE(test.expect_row(row)); }
  ASSERT_NO_FATAL_FAILURE(test.expect_guards());
}

TEST(NativeBatchHash, hash_uses_cg_inline_result_without_payload)
{
  HashBatch test(Operation::Hash);
  ASSERT_NO_FATAL_FAILURE(test.prepare());
  ASSERT_NO_FATAL_FAILURE(evaluate_all(test));
}

TEST(NativeBatchHash, partition_uses_cg_inline_result_without_payload)
{
  HashBatch test(Operation::Partition);
  ASSERT_NO_FATAL_FAILURE(test.prepare());
  ASSERT_NO_FATAL_FAILURE(evaluate_all(test));
}

TEST(NativeBatchHash, broadcast_on_either_side_preserves_per_row_seed_chain)
{
  for (Operation operation : OPERATIONS) {
    for (int64_t broadcast_column = 0; broadcast_column < 2; ++broadcast_column) {
      HashBatch test(operation);
      ASSERT_NO_FATAL_FAILURE(test.prepare(broadcast_column == 0, broadcast_column == 1));
      ASSERT_NO_FATAL_FAILURE(evaluate_all(test));
    }
  }
}

TEST(NativeBatchHash, skipped_rows_reenter_without_overwriting_evaluated_results)
{
  for (Operation operation : OPERATIONS) {
    HashBatch test(operation);
    ASSERT_NO_FATAL_FAILURE(test.prepare());
    test.skip().set(0); test.skip().set(2);
    ASSERT_EQ(OB_SUCCESS, test.evaluate());
    std::array<uint64_t, BATCH> completed{};
    for (int64_t row = 0; row < BATCH; ++row) {
      if (row != 0 && row != 2) {
        ASSERT_NO_FATAL_FAILURE(test.expect_row(row)); completed[row] = test.result_bits(row);
      } else {
        EXPECT_TRUE(test.output(row).is_null());
        EXPECT_FALSE(test.result_->get_evaluated_flags(test.eval_).at(row));
      }
      test.values_[0][row] = row + 100;
      test.nulls_[0][row] = false;
      test.input(0, row).set_int(test.values_[0][row]);
    }
    test.skip().unset(0);
    ASSERT_EQ(OB_SUCCESS, test.evaluate());
    ASSERT_NO_FATAL_FAILURE(test.expect_row(0));
    EXPECT_TRUE(test.output(2).is_null());
    EXPECT_FALSE(test.result_->get_evaluated_flags(test.eval_).at(2));
    for (int64_t row = 1; row < BATCH; ++row) {
      if (row != 2) { EXPECT_EQ(completed[row], test.result_bits(row)); }
    }
    ASSERT_NO_FATAL_FAILURE(test.expect_guards());
  }
}

TEST(NativeBatchHash, empty_and_all_skipped_batches_publish_no_rows)
{
  for (Operation operation : OPERATIONS) {
    HashBatch test(operation);
    ASSERT_NO_FATAL_FAILURE(test.prepare());
    ASSERT_EQ(OB_SUCCESS, test.evaluate(0));
    for (int64_t row = 0; row < BATCH; ++row) { test.skip().set(row); }
    ASSERT_EQ(OB_SUCCESS, test.evaluate());
    for (int64_t row = 0; row < BATCH; ++row) {
      EXPECT_TRUE(test.output(row).is_null());
      EXPECT_FALSE(test.result_->get_evaluated_flags(test.eval_).at(row));
    }
    ASSERT_NO_FATAL_FAILURE(test.expect_guards());
  }
}

int fail_late_argument(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, int64_t count)
{
  EXPECT_EQ(1U, expr.parent_cnt_);
  if (expr.parent_cnt_ == 1) {
    const ObExpr &parent = *expr.parents_[0];
    for (int64_t row = 0; row < count; ++row) {
      if (!skip.at(row)) {
        // The first argument really ran and accumulated into native slots,
        // but no output row has been published when the second one fails.
        EXPECT_FALSE(parent.locate_batch_datums(ctx)[row].is_null());
        EXPECT_FALSE(parent.get_evaluated_flags(ctx).at(row));
      }
    }
  }
  return OB_TIMEOUT;
}

TEST(NativeBatchHash, late_argument_failure_clears_partial_results_in_generic_eval)
{
  for (Operation operation : OPERATIONS) {
    HashBatch test(operation);
    ASSERT_NO_FATAL_FAILURE(test.prepare());
    test.arguments_[1]->eval_batch_func_ = fail_late_argument;
    ASSERT_EQ(OB_TIMEOUT, test.evaluate());
    for (int64_t row = 0; row < BATCH; ++row) {
      EXPECT_TRUE(test.output(row).is_null());
      EXPECT_FALSE(test.result_->get_evaluated_flags(test.eval_).at(row));
    }
    ASSERT_NO_FATAL_FAILURE(test.expect_guards());
  }
}

TEST(NativeBatchHash, wide_decimal_short_and_zero_spans_match_legacy_domain_hash)
{
  for (Operation operation : OPERATIONS) {
    HashBatch test(operation);
    ASSERT_NO_FATAL_FAILURE(test.prepare(false, false, true));
    ASSERT_NO_FATAL_FAILURE(evaluate_all(test));
  }
}

TEST(NativeBatchHash, following_reference_payload_remains_unchanged)
{
  for (Operation operation : OPERATIONS) {
    HashBatch test(operation);
    ASSERT_NO_FATAL_FAILURE(test.prepare(false, false, false, true));
    ASSERT_NO_FATAL_FAILURE(evaluate_all(test));
  }
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  ObCharset::init_charset();
  ObExprOperatorFactory::register_expr_operators();
  return RUN_ALL_TESTS();
}
