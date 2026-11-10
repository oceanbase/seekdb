/*
 * Copyright (c) 2026 OceanBase.
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

#include "sql/engine/aggregate/ob_pushdown_aggregate_program.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::aggregate;

namespace
{
class ProgramOwner final : public ObIAllocator
{
public:
  ~ProgramOwner() override
  {
    for (const auto &allocation : live_) { std::free(allocation.first); }
  }
  void *alloc(int64_t bytes) override
  {
    ++calls_;
    if (budget_ == 0 || bytes <= 0) { return nullptr; }
    if (budget_ > 0) { --budget_; }
    void *memory = std::malloc(static_cast<size_t>(bytes));
    if (memory != nullptr) { live_[memory] = bytes; }
    return memory;
  }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *memory) override
  {
    if (memory != nullptr) {
      const auto found = live_.find(memory);
      if (found == live_.end()) { ++invalid_frees_; }
      else {
        MEMSET(memory, 0xA5, found->second);
        std::free(memory);
        live_.erase(found);
        ++frees_;
      }
    }
  }
  int64_t calls_ = 0;
  int64_t frees_ = 0;
  int64_t budget_ = -1;
  int64_t invalid_frees_ = 0;
  std::map<void *, int64_t> live_;
};

// A physical segment provider only: it supplies explicit bytes or a supplied
// exact summary. It contains no comparison, accumulator, or owner algorithm.
// Each non-const protocol call poisons the preceding returned borrowed view.
class BorrowedSegment final : public ObIAggregateInputSegment
{
public:
  struct Cell {
    bool null_ = false;
    std::vector<char> bytes_;
  };
  explicit BorrowedSegment(int64_t columns, int64_t rows)
    : columns_(columns, std::vector<Cell>(rows))
  { selection_.count_ = rows; }
  template <typename T> void scalar(int64_t column, int64_t row, T value)
  { bytes(column, row, &value, sizeof(value)); }
  void bytes(int64_t column, int64_t row, const void *data, int64_t size)
  {
    Cell &cell = columns_.at(column).at(row);
    cell.null_ = false;
    cell.bytes_.resize(size);
    if (size > 0) { MEMCPY(cell.bytes_.data(), data, size); }
  }
  void text(int64_t column, int64_t row, const std::string &value)
  { bytes(column, row, value.data(), value.size()); }
  void null(int64_t column, int64_t row) { columns_.at(column).at(row).null_ = true; }
  void poison()
  {
    invalidate();
    for (auto &column : columns_) {
      for (auto &cell : column) { std::fill(cell.bytes_.begin(), cell.bytes_.end(), char(0xA5)); }
    }
  }
  const ObAggregateSelectionView &selection() const override { return selection_; }
  int can_read_values(ObAggregateInputSlot slot, bool &can_read) const override
  {
    can_read = slot >= 0 && slot < static_cast<int64_t>(columns_.size());
    return OB_SUCCESS;
  }
  int try_reduce(ObAggregateInputSlot slot, uint32_t requested, ObAggregateReduction &out) override
  {
    ++reduce_calls_;
    invalidate();
    if (!summaries_) { return OB_NOT_SUPPORTED; }
    if (slot < 0 || slot >= static_cast<int64_t>(columns_.size())) { return OB_INVALID_ARGUMENT; }
    load(slot);
    out = ObAggregateReduction();
    out.present_ = requested;
    out.row_count_ = selection_.count_;
    for (const Cell &cell : columns_.at(slot)) { out.null_count_ += cell.null_; }
    if (!views_.empty()) {
      out.min_ = &views_[0];
      out.max_ = &views_[0];
    }
    out.min_is_prefix_ = prefix_;
    out.max_is_prefix_ = prefix_;
    return OB_SUCCESS;
  }
  int read_values(ObAggregateInputSlot slot, ObAggregateValueBatchView &out) override
  {
    ++read_calls_;
    invalidate();
    if (slot < 0 || slot >= static_cast<int64_t>(columns_.size())) { return OB_INVALID_ARGUMENT; }
    load(slot);
    out.datums_ = views_.empty() ? nullptr : views_.data();
    out.count_ = selection_.count_;
    return OB_SUCCESS;
  }
  int try_dictionary(ObAggregateInputSlot, ObAggregateDictionaryView &) override
  { invalidate(); return OB_NOT_SUPPORTED; }
  bool summaries_ = false;
  bool prefix_ = false;
  int64_t read_calls_ = 0;
  int64_t reduce_calls_ = 0;
private:
  void invalidate()
  {
    std::fill(scratch_.begin(), scratch_.end(), char(0xA5));
    views_.clear();
  }
  void load(int64_t slot)
  {
    size_t bytes = 0;
    for (const Cell &cell : columns_.at(slot)) { bytes += cell.bytes_.size(); }
    scratch_.resize(bytes);
    views_.resize(selection_.count_);
    size_t offset = 0;
    for (int64_t i = 0; i < selection_.count_; ++i) {
      const Cell &cell = columns_.at(slot).at(i);
      ObDatum &view = views_.at(i);
      if (cell.null_) { view.set_null(); }
      else {
        view.pack_ = cell.bytes_.size();
        view.ptr_ = cell.bytes_.empty() ? nullptr : scratch_.data() + offset;
        if (!cell.bytes_.empty()) { MEMCPY(scratch_.data() + offset, cell.bytes_.data(), cell.bytes_.size()); }
      }
      offset += cell.bytes_.size();
    }
  }
  std::vector<std::vector<Cell>> columns_;
  ObAggregateSelectionView selection_;
  std::vector<char> scratch_;
  std::vector<ObDatum> views_;
};

class NativePushdownMinMax : public ::testing::Test
{
protected:
  NativePushdownMinMax()
    : expr_owner_("PdMinMaxType"), exec_owner_("PdMinMaxExec"), exec_(exec_owner_),
      eval_(exec_), batch_(eval_), frames_{frame_.data()} {}
  static void SetUpTestCase() { ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset()); }
  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    batch_.set_batch_size(1);
    for (int i = 0; i < 4; ++i) {
      ObExpr &expr = expressions_[i];
      expr.frame_idx_ = 0;
      expr.datum_off_ = i * 4096;
      expr.eval_info_off_ = i * 4096 + 64;
      expr.res_buf_off_ = i * 4096 + 256;
      expr.res_buf_len_ = 2048;
      new (frame_.data() + expr.datum_off_) Datum();
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo();
      arguments_[i][0] = &inputs_[i];
      expr.args_ = arguments_[i];
    }
  }
  void TearDown() override
  {
    destroy_programs();
    EXPECT_EQ(0, program_owner_.invalid_frees_);
    EXPECT_TRUE(program_owner_.live_.empty());
  }
  int configure(ObItemType aggregate, SqlValueType kind, ObAccuracy accuracy,
                ObCollationType collation = CS_TYPE_BINARY)
  {
    int ret = OB_SUCCESS;
    const int64_t slot = outputs_.count();
    if (slot >= 4) { return OB_INVALID_ARGUMENT; }
    ObExpr &expr = expressions_[slot];
    expr.type_ = aggregate;
    expr.arg_cnt_ = aggregate == T_FUN_COUNT ? 0 : 1;
    expr.obj_meta_.set_type(kind);
    expr.obj_meta_.set_collation_type(collation);
    expr.obj_meta_.set_scale(accuracy.get_scale());
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(kind);
    expr.datum_meta_ = ObDatumMeta(kind, collation, accuracy.get_scale());
    expr.datum_meta_.precision_ = accuracy.get_precision();
    expr.max_length_ = accuracy.get_length();
    inputs_[slot].obj_meta_ = expr.obj_meta_;
    if (OB_FAIL(expr.prepare_scalar_type(expr.obj_meta_, accuracy, expr_owner_))) {
    } else if (OB_FAIL(inputs_[slot].prepare_scalar_type(expr.obj_meta_, accuracy, expr_owner_))) {
    } else if (OB_FAIL(outputs_.push_back(&expr))) {
    }
    return ret;
  }
  int configure_text(ObItemType aggregate, SqlValueType kind = ObVarcharType)
  {
    ObAccuracy accuracy;
    accuracy.set_full_length(1024, LS_BYTE);
    accuracy.set_scale(0);
    return configure(aggregate, kind, accuracy);
  }
  int create()
  {
    int ret = create_pushdown_aggregate_plan(eval_, outputs_, false, program_owner_, plan_);
    if (OB_SUCC(ret) && plan_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
    if (OB_SUCC(ret)) { ret = plan_->create_program(program_); }
    if (OB_SUCC(ret) && program_ == nullptr) { ret = OB_ERR_UNEXPECTED; }
    return ret;
  }
  void destroy_programs()
  {
    if (other_ != nullptr) { other_->destroy(); other_ = nullptr; }
    if (program_ != nullptr) { program_->destroy(); program_ = nullptr; }
    destroy_pushdown_aggregate_plan(plan_);
  }
  int emit(ObIPushdownAggregateProgram &program)
  {
    ObAggregateEmitResult result;
    int ret = program.seal();
    if (OB_SUCC(ret)) { ret = program.emit(1, result); }
    if (OB_SUCC(ret)) {
      EXPECT_EQ(1, result.row_count_);
      EXPECT_TRUE(result.end_);
      EXPECT_EQ(AGG_PROGRAM_END, program.state());
      ret = program.emit(1, result);
    }
    if (OB_SUCC(ret)) {
      EXPECT_EQ(0, result.row_count_);
      EXPECT_TRUE(result.end_);
    }
    return ret;
  }
  Datum &value(int64_t slot) { return expressions_[slot].locate_expr_datum(eval_); }
  std::string text(int64_t slot)
  {
    const Datum &datum = value(slot);
    return datum.length() == 0 ? std::string()
        : std::string(static_cast<const char *>(datum.get_reference()), datum.length());
  }
  ObArenaAllocator expr_owner_;
  ObArenaAllocator exec_owner_;
  ProgramOwner program_owner_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  alignas(8) std::array<char, 4 * 4096> frame_{};
  char *frames_[1];
  ObExpr expressions_[4];
  ObExpr inputs_[4];
  ObExpr *arguments_[4][1];
  ObSEArray<ObExpr *, 4> outputs_;
  ObIPushdownAggregatePlan *plan_ = nullptr;
  ObIPushdownAggregateProgram *program_ = nullptr;
  ObIPushdownAggregateProgram *other_ = nullptr;
};

TEST_F(NativePushdownMinMax, scalar_segments_own_inline_values_without_payload_allocations)
{
  ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MAX, ObUInt64Type, ObAccuracy(20, 0)));
  ASSERT_EQ(OB_SUCCESS, configure(T_FUN_COUNT, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, create());
  ASSERT_EQ(OB_SUCCESS, plan_->create_program(other_));
  BorrowedSegment first(3, 2);
  first.scalar<int64_t>(0, 0, 19); first.scalar<int64_t>(0, 1, -7);
  first.scalar<uint64_t>(1, 0, 11); first.scalar<uint64_t>(1, 1, UINT64_MAX - 1);
  BorrowedSegment second(3, 2);
  second.scalar<int64_t>(0, 0, INT64_MIN); second.null(0, 1);
  second.scalar<uint64_t>(1, 0, UINT64_MAX); second.null(1, 1);
  bool can_consume = false;
  const int64_t calls = program_owner_.calls_;
  program_owner_.budget_ = 0;
  ASSERT_EQ(OB_SUCCESS, program_->can_consume(first, can_consume));
  ASSERT_TRUE(can_consume);
  EXPECT_EQ(AGG_PROGRAM_NEW, program_->state());
  ASSERT_EQ(OB_SUCCESS, program_->consume(first));
  ASSERT_EQ(OB_SUCCESS, other_->consume(first));
  first.poison();
  ASSERT_EQ(OB_SUCCESS, program_->consume(second));
  second.poison();
  EXPECT_EQ(calls, program_owner_.calls_);
  ASSERT_EQ(OB_SUCCESS, emit(*program_));
  EXPECT_EQ(INT64_MIN, value(0).get_int());
  EXPECT_EQ(UINT64_MAX, value(1).get_uint());
  EXPECT_EQ(4, value(2).get_int());
  ASSERT_EQ(OB_SUCCESS, emit(*other_));
  EXPECT_EQ(-7, value(0).get_int());
  EXPECT_EQ(UINT64_MAX - 1, value(1).get_uint());
  EXPECT_EQ(2, value(2).get_int());
  ASSERT_EQ(OB_SUCCESS, program_->reset_scan());
  ASSERT_EQ(OB_SUCCESS, emit(*program_));
  EXPECT_TRUE(value(0).is_null()); EXPECT_TRUE(value(1).is_null());
  EXPECT_EQ(0, value(2).get_int());
  EXPECT_EQ(calls, program_owner_.calls_);
}

TEST_F(NativePushdownMinMax, reference_capacity_reuse_and_emitted_owner_outlive_segment_and_program)
{
  ASSERT_EQ(OB_SUCCESS, configure_text(T_FUN_MIN));
  ASSERT_EQ(OB_SUCCESS, configure_text(T_FUN_MAX));
  ASSERT_EQ(OB_SUCCESS, create());
  const int64_t initial_calls = program_owner_.calls_;
  const std::string embedded("a\0tail", 6);
  const std::string high(400, 'z');
  BorrowedSegment first(2, 1); first.text(0, 0, "m"); first.text(1, 0, "m");
  ASSERT_EQ(OB_SUCCESS, program_->consume(first)); first.poison();
  EXPECT_GT(program_owner_.calls_, initial_calls);
  BorrowedSegment second(2, 1); second.text(0, 0, embedded); second.text(1, 0, high);
  ASSERT_EQ(OB_SUCCESS, program_->consume(second)); second.poison();
  BorrowedSegment third(2, 1); third.text(0, 0, ""); third.text(1, 0, std::string(400, '~'));
  ASSERT_EQ(OB_SUCCESS, program_->consume(third)); third.poison();
  ASSERT_EQ(OB_SUCCESS, emit(*program_)); EXPECT_EQ("", text(0)); EXPECT_EQ(std::string(400, '~'), text(1));
  const int64_t warmed_calls = program_owner_.calls_;
  ASSERT_EQ(OB_SUCCESS, program_->reset_scan());
  program_owner_.budget_ = 0;
  BorrowedSegment retry(2, 1); retry.text(0, 0, embedded); retry.text(1, 0, high);
  ASSERT_EQ(OB_SUCCESS, program_->consume(retry)); retry.poison();
  ASSERT_EQ(OB_SUCCESS, emit(*program_));
  EXPECT_EQ(warmed_calls, program_owner_.calls_);
  EXPECT_EQ(embedded, text(0)); EXPECT_EQ(high, text(1));
  destroy_programs();
  EXPECT_TRUE(program_owner_.live_.empty());
  EXPECT_EQ(embedded, text(0)); EXPECT_EQ(high, text(1));
}

TEST_F(NativePushdownMinMax, later_reference_oom_poison_state_reset_retry_and_cleanup)
{
  ASSERT_EQ(OB_SUCCESS, configure_text(T_FUN_MIN));
  ASSERT_EQ(OB_SUCCESS, configure_text(T_FUN_MAX));
  ASSERT_EQ(OB_SUCCESS, create());
  ASSERT_EQ(OB_SUCCESS, value(0).set_reference("sentinel", 8));
  ASSERT_EQ(OB_SUCCESS, value(1).set_reference("sentinel", 8));
  BorrowedSegment input(2, 1); input.text(0, 0, std::string(300, 'a')); input.text(1, 0, std::string(500, 'z'));
  const int64_t calls = program_owner_.calls_;
  program_owner_.budget_ = 1;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, program_->consume(input));
  EXPECT_EQ(calls + 2, program_owner_.calls_);
  EXPECT_EQ(AGG_PROGRAM_FAILED, program_->state());
  EXPECT_EQ("sentinel", text(0)); EXPECT_EQ("sentinel", text(1));
  EXPECT_FALSE(expressions_[0].get_eval_info(eval_).evaluated_);
  EXPECT_FALSE(expressions_[1].get_eval_info(eval_).evaluated_);
  EXPECT_EQ(OB_STATE_NOT_MATCH, program_->consume(input));
  ObAggregateEmitResult result;
  EXPECT_EQ(OB_STATE_NOT_MATCH, program_->emit(1, result));
  EXPECT_EQ(0, result.row_count_);
  program_owner_.budget_ = -1;
  ASSERT_EQ(OB_SUCCESS, program_->reset_scan());
  ASSERT_EQ(OB_SUCCESS, program_->consume(input)); input.poison();
  ASSERT_EQ(OB_SUCCESS, emit(*program_));
  EXPECT_EQ(std::string(300, 'a'), text(0)); EXPECT_EQ(std::string(500, 'z'), text(1));
  destroy_programs();
  EXPECT_TRUE(program_owner_.live_.empty());
  EXPECT_EQ(0, program_owner_.invalid_frees_);
}

TEST_F(NativePushdownMinMax, exact_summaries_all_null_and_prefix_fallback_keep_protocol_behavior)
{
  ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MAX, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, create());
  BorrowedSegment exact(2, 1); exact.summaries_ = true;
  exact.scalar<int64_t>(0, 0, -18); exact.scalar<int64_t>(1, 0, 23);
  bool can_consume = false;
  ASSERT_EQ(OB_SUCCESS, program_->can_consume(exact, can_consume)); ASSERT_TRUE(can_consume);
  ASSERT_EQ(OB_SUCCESS, program_->consume(exact)); exact.poison();
  EXPECT_EQ(0, exact.read_calls_);
  BorrowedSegment all_null(2, 1); all_null.summaries_ = true; all_null.null(0, 0); all_null.null(1, 0);
  ASSERT_EQ(OB_SUCCESS, program_->consume(all_null));
  ASSERT_EQ(OB_SUCCESS, emit(*program_)); EXPECT_EQ(-18, value(0).get_int()); EXPECT_EQ(23, value(1).get_int());
  ASSERT_EQ(OB_SUCCESS, program_->reset_scan());
  ASSERT_EQ(OB_SUCCESS, program_->consume(all_null)); ASSERT_EQ(OB_SUCCESS, emit(*program_));
  EXPECT_TRUE(value(0).is_null()); EXPECT_TRUE(value(1).is_null());
  EXPECT_EQ(ObIntType, expressions_[0].get_sql_type().kind());
  EXPECT_EQ(19, expressions_[0].get_sql_type().precision());
  EXPECT_EQ(0, expressions_[0].get_sql_type().scale());
  ASSERT_EQ(OB_SUCCESS, program_->reset_scan());
  BorrowedSegment prefix(2, 2); prefix.summaries_ = true; prefix.prefix_ = true;
  prefix.scalar<int64_t>(0, 0, 9); prefix.scalar<int64_t>(0, 1, -29);
  prefix.scalar<int64_t>(1, 0, 9); prefix.scalar<int64_t>(1, 1, 71);
  ASSERT_EQ(OB_SUCCESS, program_->can_consume(prefix, can_consume)); ASSERT_TRUE(can_consume);
  EXPECT_EQ(0, prefix.read_calls_);
  ASSERT_EQ(OB_SUCCESS, program_->consume(prefix)); prefix.poison();
  EXPECT_EQ(2, prefix.read_calls_);
  ASSERT_EQ(OB_SUCCESS, emit(*program_)); EXPECT_EQ(-29, value(0).get_int()); EXPECT_EQ(71, value(1).get_int());
}

TEST_F(NativePushdownMinMax, declared_decimal_widths_import_inline_or_retain_actual_reference)
{
  const int widths[] = {4, 8, 16, 32, 64};
  const ObPrecision precisions[] = {9, 18, 30, 60, 100};
  for (int index = 0; index < 5; ++index) {
    SCOPED_TRACE(widths[index]);
    destroy_programs(); outputs_.reuse();
    ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MIN, ObDecimalIntType, ObAccuracy(precisions[index], 2)));
    ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MAX, ObDecimalIntType, ObAccuracy(precisions[index], 2)));
    ASSERT_EQ(OB_SUCCESS, create());
    const int width = widths[index];
    std::vector<char> negative(width, char(0xFF)), positive(width, 0);
    const int32_t minus_seven = -7, nineteen = 19;
    MEMCPY(negative.data(), &minus_seven, sizeof(minus_seven));
    MEMCPY(positive.data(), &nineteen, sizeof(nineteen));
    BorrowedSegment input(2, 2);
    for (int column = 0; column < 2; ++column) {
      input.bytes(column, 0, negative.data(), width);
      input.bytes(column, 1, positive.data(), width);
    }
    const int64_t calls = program_owner_.calls_;
    ASSERT_EQ(OB_SUCCESS, program_->consume(input)); input.poison();
    ASSERT_EQ(OB_SUCCESS, emit(*program_));
    if (width <= 8) {
      EXPECT_EQ(calls, program_owner_.calls_);
      EXPECT_EQ(0, value(0).length()); EXPECT_EQ(-7, value(0).get_int());
      EXPECT_EQ(19, value(1).get_int());
    } else {
      EXPECT_GT(program_owner_.calls_, calls);
      EXPECT_EQ(width, value(0).length()); EXPECT_EQ(width, value(1).length());
      EXPECT_EQ(0, MEMCMP(negative.data(), value(0).get_reference(), width));
      EXPECT_EQ(0, MEMCMP(positive.data(), value(1).get_reference(), width));
    }
    EXPECT_EQ(2, expressions_[0].get_sql_type().scale());
    destroy_programs();
    alignas(8) char scratch[64]; ObDatum legacy;
    ASSERT_EQ(OB_SUCCESS, expressions_[0].get_datum_copy_plan().legacy_view(value(0), legacy, scratch, sizeof(scratch)));
    EXPECT_EQ(width, legacy.len_); EXPECT_EQ(0, MEMCMP(negative.data(), legacy.ptr_, width));
  }
}

TEST_F(NativePushdownMinMax, malformed_scalar_import_and_factory_admission_do_not_publish)
{
  ASSERT_EQ(OB_SUCCESS, configure(T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, create());
  value(0).set_int(101);
  BorrowedSegment malformed(1, 1); const char short_integer = 7;
  malformed.bytes(0, 0, &short_integer, 1);
  EXPECT_EQ(OB_INVALID_ARGUMENT, program_->consume(malformed));
  EXPECT_EQ(AGG_PROGRAM_FAILED, program_->state()); EXPECT_EQ(101, value(0).get_int());
  ASSERT_EQ(OB_SUCCESS, program_->reset_scan());
  BorrowedSegment valid(1, 1); valid.scalar<int64_t>(0, 0, -11);
  ASSERT_EQ(OB_SUCCESS, program_->consume(valid)); valid.poison(); ASSERT_EQ(OB_SUCCESS, emit(*program_));
  EXPECT_EQ(-11, value(0).get_int());
  destroy_programs();
  const SqlType *saved_type = expressions_[0].sql_type_;
  expressions_[0].sql_type_ = nullptr;
  EXPECT_EQ(OB_ERR_UNEXPECTED, create_pushdown_aggregate_plan(eval_, outputs_, false, program_owner_, plan_));
  EXPECT_EQ(nullptr, plan_);
  expressions_[0].sql_type_ = saved_type;
  outputs_.reuse();
  ASSERT_EQ(OB_SUCCESS, configure_text(T_FUN_MIN, ObLongTextType));
  EXPECT_EQ(ObLongTextType, expressions_[0].get_sql_type().kind());
  EXPECT_EQ(OB_NOT_SUPPORTED, create_pushdown_aggregate_plan(eval_, outputs_, false, program_owner_, plan_));
  EXPECT_EQ(nullptr, plan_);
}
} // namespace
