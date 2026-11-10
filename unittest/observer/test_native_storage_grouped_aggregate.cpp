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

#include "data_plane/access/ob_table_param.h"
#include "share/schema/ob_col_desc.h"
#include "storage/access/ob_pushdown_aggregate.h"
#include "storage/access/ob_table_access_context.h"
#include "storage/access/ob_table_access_param.h"
#include "storage/access/ob_table_read_info.h"
#include "storage/blocksstable/encoding/ob_micro_block_decoder.h"
#include "storage/blocksstable/encoding/ob_micro_block_encoder.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/charset/ob_charset.h"
#include "lib/container/ob_bitmap.h"
#include "common/number/ob_number_v2.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <map>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::storage;
using namespace oceanbase::blocksstable;
using namespace oceanbase::share::schema;

namespace
{
// The factory and its cells use this real allocator. Expr/Type construction
// has a separate owner, so post-init allocation assertions cover cell state.
class GroupedOwner final : public ObIAllocator
{
public:
  ~GroupedOwner() override
  { for (const auto &entry : live_) { std::free(entry.first); } }
  void *alloc(int64_t bytes) override
  {
    ++calls_;
    if (reject_ || bytes <= 0) { return nullptr; }
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
      }
    }
  }
  int64_t calls_ = 0;
  int64_t invalid_frees_ = 0;
  bool reject_ = false;
  std::map<void *, int64_t> live_;
};

ObAccuracy text_accuracy()
{
  ObAccuracy accuracy;
  accuracy.set_full_length(1024, LS_BYTE);
  accuracy.set_scale(0);
  return accuracy;
}

ObDatum null_input() { ObDatum value; value.set_null(); return value; }
template <typename T> ObDatum physical(const T &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum physical_text(const std::string &value)
{ return ObDatum(value.data(), value.size(), false); }

struct CoefficientInput
{
  alignas(16) std::array<char, 64> bytes_{};
  void set(int64_t value, int64_t width)
  {
    bytes_.fill(value < 0 ? char(0xFF) : char(0));
    MEMCPY(bytes_.data(), &value, std::min<int64_t>(width, sizeof(value)));
  }
  ObDatum view(int64_t width) const { return ObDatum(bytes_.data(), width, false); }
  void poison() { bytes_.fill(char(0xA5)); }
};

class NativeStorageGroupedAggregate : public ::testing::Test
{
protected:
  enum { BATCH = 8, SLOTS = 6, STRIDE = 16384 };
  NativeStorageGroupedAggregate()
    : type_owner_("GroupedTypes"), exec_owner_("GroupedExec"), exec_(exec_owner_),
      eval_(exec_), batch_(eval_), frames_{frame_.data()}, factory_(cell_owner_) {}
  static void SetUpTestCase() { ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset()); }
  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    batch_.set_batch_size(BATCH);
    for (int i = 0; i < 2 * SLOTS; ++i) {
      ObExpr &expr = i < SLOTS ? outputs_[i] : inputs_[i - SLOTS];
      expr.frame_idx_ = 0;
      expr.datum_off_ = i * STRIDE;
      expr.eval_info_off_ = i * STRIDE + 256;
      expr.eval_flags_off_ = i * STRIDE + 320;
      expr.res_buf_off_ = i * STRIDE + 1024;
      expr.res_buf_len_ = 1024;
      expr.batch_idx_mask_ = UINT64_MAX;
      expr.batch_result_ = true;
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo();
      for (int row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum();
      }
    }
  }
  void TearDown() override
  {
    cell_owner_.reject_ = false;
    factory_.release(cells_);
    EXPECT_EQ(0, cell_owner_.invalid_frees_);
    EXPECT_TRUE(cell_owner_.live_.empty());
  }
  int prepare_type(ObExpr &expr, SqlValueType kind, const ObAccuracy &accuracy)
  {
    expr.obj_meta_.set_type(kind);
    expr.obj_meta_.set_collation_type(kind == ObVarcharType ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
    expr.obj_meta_.set_collation_level(kind == ObVarcharType ? CS_LEVEL_IMPLICIT : CS_LEVEL_NUMERIC);
    expr.obj_meta_.set_scale(accuracy.get_scale());
    expr.datum_meta_ = ObDatumMeta(kind, expr.obj_meta_.get_collation_type(), accuracy.get_scale());
    expr.datum_meta_.precision_ = accuracy.get_precision();
    if (kind == ObVarcharType) { expr.datum_meta_.length_semantics_ = LS_BYTE; }
    expr.max_length_ = accuracy.get_length();
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(kind);
    return expr.prepare_scalar_type(expr.obj_meta_, accuracy, type_owner_);
  }
  int configure(int slot, ObItemType operation, SqlValueType input_kind,
                const ObAccuracy &input_accuracy, SqlValueType output_kind,
                const ObAccuracy &output_accuracy)
  {
    int ret = prepare_type(inputs_[slot], input_kind, input_accuracy);
    inputs_[slot].type_ = T_REF_COLUMN;
    outputs_[slot].type_ = operation;
    arguments_[slot][0] = &inputs_[slot];
    outputs_[slot].args_ = arguments_[slot];
    outputs_[slot].arg_cnt_ = 1;
    if (OB_SUCC(ret)) { ret = prepare_type(outputs_[slot], output_kind, output_accuracy); }
    return ret;
  }
  int configure_same(int slot, ObItemType operation, SqlValueType kind, const ObAccuracy &accuracy)
  { return configure(slot, operation, kind, accuracy, kind, accuracy); }
  int create(int slot, bool exclude_null = false)
  {
    // No default-value read is requested: the physical inputs below are
    // already decoded. All expression declarations are prepared above.
    ObAggCellBasicInfo info(slot, slot, nullptr, &outputs_[slot], BATCH, false);
    int ret = factory_.alloc_cell(info, cells_, exclude_null, true, &eval_);
    if (OB_SUCC(ret)) { ret = cells_.at(cells_.count() - 1)->reserve_group_by_buf(BATCH); }
    return ret;
  }
  int publish(ObAggCell &cell, int64_t groups)
  {
    cell.set_group_by_result_cnt(groups);
    int ret = cell.collect_batch_result_in_group_by(groups);
    if (OB_SUCC(ret)) { ret = cell.publish_native_rows(eval_, groups); }
    return ret;
  }
  Datum &output(int slot, int row) { return outputs_[slot].locate_expr_datum(eval_, row); }
  void expect_text(int slot, int row, const std::string &expected)
  {
    ASSERT_FALSE(output(slot, row).is_null());
    ObString actual = output(slot, row).get_string();
    ASSERT_EQ(static_cast<int64_t>(expected.size()), actual.length());
    EXPECT_EQ(0, MEMCMP(expected.data(), actual.ptr(), expected.size()));
  }
  void expect_number(int slot, int row, const char *expected)
  {
    ASSERT_FALSE(output(slot, row).is_null());
    alignas(8) char scratch[64];
    ObDatum view;
    ASSERT_EQ(OB_SUCCESS, outputs_[slot].get_datum_copy_plan().legacy_view(
        output(slot, row), view, scratch, sizeof(scratch)));
    number::ObNumber wanted;
    ASSERT_EQ(OB_SUCCESS, wanted.from(expected, type_owner_));
    const number::ObNumber actual(view.get_number());
    EXPECT_EQ(0, actual.compare(wanted));
  }
  void expect_coefficient(int slot, int row, const void *expected, int64_t width)
  {
    ASSERT_FALSE(output(slot, row).is_null());
    alignas(8) char scratch[64];
    ObDatum view;
    ASSERT_EQ(OB_SUCCESS, outputs_[slot].get_datum_copy_plan().legacy_view(
        output(slot, row), view, scratch, sizeof(scratch)));
    ASSERT_EQ(width, view.len_);
    EXPECT_EQ(0, MEMCMP(expected, view.ptr_, width));
  }

  ObArenaAllocator type_owner_;
  ObArenaAllocator exec_owner_;
  GroupedOwner cell_owner_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard batch_;
  alignas(16) std::array<char, 2 * SLOTS * STRIDE> frame_{};
  char *frames_[1];
  ObExpr outputs_[SLOTS];
  ObExpr inputs_[SLOTS];
  ObExpr *arguments_[SLOTS][1];
  ObPDAggFactory factory_;
  ObSEArray<ObAggCell *, SLOTS> cells_;
};

TEST_F(NativeStorageGroupedAggregate, integer_count_min_max_two_batches_have_no_scalar_payload_allocation)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(1, T_FUN_MAX, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(2, T_FUN_COUNT, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(3, T_FUN_COUNT, ObIntType, ObAccuracy(19, 0)));
  for (int i = 0; i < 4; ++i) { ASSERT_EQ(OB_SUCCESS, create(i, i == 3)); }
  int64_t first[] = {10, 0, -5, 7}, second[] = {3, 19, 0, -11};
  ObDatum a[] = {physical(first[0]), null_input(), physical(first[2]), physical(first[3])};
  ObDatum b[] = {physical(second[0]), physical(second[1]), null_input(), physical(second[3])};
  const uint32_t ra[] = {0, 1, 0, 2}, rb[] = {1, 0, 2, 2};
  const int64_t calls = cell_owner_.calls_;
  cell_owner_.reject_ = true;
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(OB_SUCCESS, cells_.at(i)->eval_batch_in_group_by(a, 4, ra, 4));
  }
  MEMSET(first, 0xA5, sizeof(first));
  for (int i = 0; i < 4; ++i) {
    ASSERT_EQ(OB_SUCCESS, cells_.at(i)->eval_batch_in_group_by(b, 4, rb, 4));
  }
  MEMSET(second, 0xA5, sizeof(second));
  for (int i = 0; i < 4; ++i) { ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(i), 4)); }
  EXPECT_EQ(calls, cell_owner_.calls_);
  const int64_t minima[] = {-5, 3, -11}, maxima[] = {19, 3, 7};
  const int64_t all[] = {3, 2, 3, 0}, nonnull[] = {3, 1, 2, 0};
  for (int group = 0; group < 4; ++group) {
    if (group == 3) { EXPECT_TRUE(output(0, group).is_null()); EXPECT_TRUE(output(1, group).is_null()); }
    else { EXPECT_EQ(minima[group], output(0, group).get_int()); EXPECT_EQ(maxima[group], output(1, group).get_int()); }
    EXPECT_EQ(all[group], output(2, group).get_int());
    EXPECT_EQ(nonnull[group], output(3, group).get_int());
  }
  for (int i = 0; i < 4; ++i) { cells_.at(i)->reuse(); ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(i), 4)); }
  EXPECT_TRUE(output(0, 0).is_null()); EXPECT_TRUE(output(1, 0).is_null());
  EXPECT_EQ(0, output(2, 0).get_int()); EXPECT_EQ(0, output(3, 0).get_int());
}

TEST_F(NativeStorageGroupedAggregate, floating_sum_owns_inline_kernel_and_reuse_starts_a_new_scan)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_FUN_SUM, ObDoubleType, ObAccuracy(22, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(1, T_FUN_SUM, ObFloatType, ObAccuracy(12, 0)));
  ASSERT_EQ(OB_SUCCESS, create(0));
  ASSERT_EQ(OB_SUCCESS, create(1));
  double first[] = {1.5, 7.25}, second[] = {2.25, -7.25};
  float first_float[] = {1.5f, 7.25f}, second_float[] = {2.25f, -7.25f};
  ObDatum a[] = {physical(first[0]), physical(first[1]), null_input()};
  ObDatum b[] = {physical(second[0]), physical(second[1]), null_input()};
  ObDatum af[] = {physical(first_float[0]), physical(first_float[1]), null_input()};
  ObDatum bf[] = {physical(second_float[0]), physical(second_float[1]), null_input()};
  const uint32_t refs[] = {0, 1, 2};
  const int64_t calls = cell_owner_.calls_;
  cell_owner_.reject_ = true;
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(a, 3, refs, 3));
  ASSERT_EQ(OB_SUCCESS, cells_.at(1)->eval_batch_in_group_by(af, 3, refs, 3));
  MEMSET(first, 0xA5, sizeof(first));
  MEMSET(first_float, 0xA5, sizeof(first_float));
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(b, 3, refs, 3));
  ASSERT_EQ(OB_SUCCESS, cells_.at(1)->eval_batch_in_group_by(bf, 3, refs, 3));
  MEMSET(second, 0xA5, sizeof(second));
  MEMSET(second_float, 0xA5, sizeof(second_float));
  ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(0), 3));
  ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(1), 3));
  EXPECT_EQ(3.75, output(0, 0).get_double()); EXPECT_EQ(0.0, output(0, 1).get_double());
  EXPECT_EQ(3.75f, output(1, 0).get_float()); EXPECT_EQ(0.0f, output(1, 1).get_float());
  EXPECT_TRUE(output(1, 2).is_null());
  EXPECT_TRUE(output(0, 2).is_null()); EXPECT_EQ(calls, cell_owner_.calls_);
  cells_.at(0)->reuse();
  cells_.at(1)->reuse();
  double next = -9.5; ObDatum input = physical(next); const uint32_t group = 1;
  float next_float = -9.5f; ObDatum float_input = physical(next_float);
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(&input, 1, &group, 3));
  ASSERT_EQ(OB_SUCCESS, cells_.at(1)->eval_batch_in_group_by(&float_input, 1, &group, 3));
  next = 1000;
  next_float = 1000;
  ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(0), 3));
  ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(1), 3));
  EXPECT_TRUE(output(0, 0).is_null()); EXPECT_EQ(-9.5, output(0, 1).get_double());
  EXPECT_TRUE(output(1, 0).is_null()); EXPECT_EQ(-9.5f, output(1, 1).get_float());
  EXPECT_TRUE(output(1, 2).is_null());
  EXPECT_TRUE(output(0, 2).is_null()); EXPECT_EQ(calls, cell_owner_.calls_);
}

TEST_F(NativeStorageGroupedAggregate, signed_and_unsigned_sum_overflow_use_number_or_coefficient_kernel)
{
  for (int slot = 0; slot < 4; ++slot) {
    const SqlValueType input = slot < 2 ? ObIntType : ObUInt64Type;
    const SqlValueType result = slot % 2 == 0 ? ObNumberType : ObDecimalIntType;
    ASSERT_EQ(OB_SUCCESS, configure(slot, T_FUN_SUM, input,
        ObAccuracy(slot < 2 ? 19 : 20, 0), result, ObAccuracy(38, 0)));
    ASSERT_EQ(OB_SUCCESS, create(slot));
  }
  int64_t sa[] = {INT64_MAX, INT64_MIN}, sb[] = {1, -1};
  uint64_t ua[] = {UINT64_MAX, 2}, ub[] = {1, 1};
  ObDatum signed_a[] = {physical(sa[0]), physical(sa[1]), null_input()};
  ObDatum signed_b[] = {physical(sb[0]), physical(sb[1]), null_input()};
  ObDatum unsigned_a[] = {physical(ua[0]), physical(ua[1]), null_input()};
  ObDatum unsigned_b[] = {physical(ub[0]), physical(ub[1]), null_input()};
  const uint32_t refs[] = {0, 1, 2};
  for (int slot = 0; slot < 4; ++slot) {
    ASSERT_EQ(OB_SUCCESS, cells_.at(slot)->eval_batch_in_group_by(slot < 2 ? signed_a : unsigned_a, 3, refs, 3));
  }
  MEMSET(sa, 0xA5, sizeof(sa)); MEMSET(ua, 0xA5, sizeof(ua));
  for (int slot = 0; slot < 4; ++slot) {
    ASSERT_EQ(OB_SUCCESS, cells_.at(slot)->eval_batch_in_group_by(slot < 2 ? signed_b : unsigned_b, 3, refs, 3));
  }
  MEMSET(sb, 0xA5, sizeof(sb)); MEMSET(ub, 0xA5, sizeof(ub));
  for (int slot = 0; slot < 4; ++slot) { ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(slot), 3)); }
  expect_number(0, 0, "9223372036854775808"); expect_number(0, 1, "-9223372036854775809");
  expect_number(2, 0, "18446744073709551616"); expect_number(2, 1, "3");
  const int128_t signed_high = static_cast<int128_t>(INT64_MAX) + 1;
  const int128_t signed_low = static_cast<int128_t>(INT64_MIN) - 1;
  const int128_t unsigned_high = static_cast<int128_t>(UINT64_MAX) + 1;
  const int128_t three = 3;
  expect_coefficient(1, 0, &signed_high, 16); expect_coefficient(1, 1, &signed_low, 16);
  expect_coefficient(3, 0, &unsigned_high, 16); expect_coefficient(3, 1, &three, 16);
  for (int slot = 0; slot < 4; ++slot) { EXPECT_TRUE(output(slot, 2).is_null()); }
  factory_.release(cells_);
  EXPECT_TRUE(cell_owner_.live_.empty());
  expect_number(0, 0, "9223372036854775808");
  expect_coefficient(3, 0, &unsigned_high, 16);
}

TEST_F(NativeStorageGroupedAggregate, decimal_sum_uses_actual_widened_coefficient_not_input_or_number_layout)
{
  const int input_precision[] = {9, 18, 38, 75};
  const int result_precision[] = {31, 40, 60, 97};
  const int input_width[] = {4, 8, 16, 32};
  const int result_width[] = {16, 32, 32, 64};
  for (int slot = 0; slot < 4; ++slot) {
    ASSERT_EQ(OB_SUCCESS, configure(slot, T_FUN_SUM, ObDecimalIntType,
        ObAccuracy(input_precision[slot], 2), ObDecimalIntType, ObAccuracy(result_precision[slot], 2)));
    ASSERT_EQ(OB_SUCCESS, create(slot));
    CoefficientInput a0, a1, b0, b1;
    a0.set(-123, input_width[slot]); a1.set(5000, input_width[slot]);
    b0.set(23, input_width[slot]); b1.set(-5001, input_width[slot]);
    ObDatum a[] = {a0.view(input_width[slot]), a1.view(input_width[slot]), null_input()};
    ObDatum b[] = {b0.view(input_width[slot]), b1.view(input_width[slot]), null_input()};
    const uint32_t refs[] = {0, 1, 2};
    ASSERT_EQ(OB_SUCCESS, cells_.at(slot)->eval_batch_in_group_by(a, 3, refs, 3));
    a0.poison(); a1.poison();
    ASSERT_EQ(OB_SUCCESS, cells_.at(slot)->eval_batch_in_group_by(b, 3, refs, 3));
    b0.poison(); b1.poison();
    ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(slot), 3));
    CoefficientInput expected0, expected1;
    expected0.set(-100, result_width[slot]); expected1.set(-1, result_width[slot]);
    expect_coefficient(slot, 0, expected0.bytes_.data(), result_width[slot]);
    expect_coefficient(slot, 1, expected1.bytes_.data(), result_width[slot]);
    EXPECT_TRUE(output(slot, 2).is_null());
    EXPECT_EQ(2, outputs_[slot].get_sql_type().scale());
    EXPECT_EQ(result_precision[slot], outputs_[slot].get_sql_type().precision());
  }
}

TEST_F(NativeStorageGroupedAggregate, number_inputs_are_owned_between_batches_and_after_cell_release)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_FUN_SUM, ObNumberType, ObAccuracy(38, 2)));
  ASSERT_EQ(OB_SUCCESS, create(0));
  number::ObNumber first, second;
  ASSERT_EQ(OB_SUCCESS, first.from("1.25", type_owner_));
  ASSERT_EQ(OB_SUCCESS, second.from("-0.50", type_owner_));
  alignas(8) char first_bytes[256]{}, second_bytes[256]{};
  ObDatum a, b;
  a.ptr_ = first_bytes; a.set_number(first);
  b.ptr_ = second_bytes; b.set_number(second);
  const uint32_t group = 0;
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(&a, 1, &group, 2));
  MEMSET(first_bytes, 0xA5, sizeof(first_bytes));
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(&b, 1, &group, 2));
  MEMSET(second_bytes, 0xA5, sizeof(second_bytes));
  ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(0), 2));
  expect_number(0, 0, "0.75"); EXPECT_TRUE(output(0, 1).is_null());
  factory_.release(cells_);
  EXPECT_TRUE(cell_owner_.live_.empty());
  expect_number(0, 0, "0.75");
}

TEST_F(NativeStorageGroupedAggregate, reference_min_max_capture_failure_keeps_previous_cell_and_allows_retry)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_FUN_MIN, ObVarcharType, text_accuracy()));
  ASSERT_EQ(OB_SUCCESS, configure_same(1, T_FUN_MAX, ObVarcharType, text_accuracy()));
  ASSERT_EQ(OB_SUCCESS, create(0)); ASSERT_EQ(OB_SUCCESS, create(1));
  std::string first("m\0z", 3);
  ObDatum input = physical_text(first); const uint32_t group = 0;
  for (int slot = 0; slot < 2; ++slot) {
    ASSERT_EQ(OB_SUCCESS, cells_.at(slot)->eval_batch_in_group_by(&input, 1, &group, 2));
  }
  std::fill(first.begin(), first.end(), char(0xA5));
  for (int slot = 0; slot < 2; ++slot) { ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(slot), 2)); }
  expect_text(0, 0, std::string("m\0z", 3)); expect_text(1, 0, std::string("m\0z", 3));
  std::string low(768, 'a'), high(768, 'z');
  for (int slot = 0; slot < 2; ++slot) {
    ObDatum next = physical_text(slot == 0 ? low : high);
    const int64_t calls = cell_owner_.calls_;
    cell_owner_.reject_ = true;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
        cells_.at(slot)->eval_batch_in_group_by(&next, 1, &group, 2));
    EXPECT_GT(cell_owner_.calls_, calls);
    cell_owner_.reject_ = false;
    ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(slot), 2));
    expect_text(slot, 0, std::string("m\0z", 3));
    ASSERT_EQ(OB_SUCCESS, cells_.at(slot)->eval_batch_in_group_by(&next, 1, &group, 2));
  }
  std::fill(low.begin(), low.end(), char(0xA5)); std::fill(high.begin(), high.end(), char(0xA5));
  for (int slot = 0; slot < 2; ++slot) { ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(slot), 2)); }
  factory_.release(cells_);
  EXPECT_TRUE(cell_owner_.live_.empty());
  expect_text(0, 0, std::string(768, 'a')); expect_text(1, 0, std::string(768, 'z'));
  EXPECT_TRUE(output(0, 1).is_null()); EXPECT_TRUE(output(1, 1).is_null());
}

TEST_F(NativeStorageGroupedAggregate, failed_directory_growth_preserves_old_group_then_success_resets_and_expands)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, create(0));
  int64_t n = -17; ObDatum input = physical(n); const uint32_t old_group = 1;
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(&input, 1, &old_group, 3));
  n = 91;
  const int64_t calls = cell_owner_.calls_;
  cell_owner_.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, cells_.at(0)->reserve_group_by_buf(600));
  EXPECT_GT(cell_owner_.calls_, calls);
  cell_owner_.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, publish(*cells_.at(0), 3));
  EXPECT_TRUE(output(0, 0).is_null()); EXPECT_EQ(-17, output(0, 1).get_int());
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->reserve_group_by_buf(600));
  n = 41; const uint32_t high_group = 599;
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->eval_batch_in_group_by(&input, 1, &high_group, 600));
  n = 0;
  cells_.at(0)->set_group_by_result_cnt(600);
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->collect_batch_result_in_group_by(600));
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->output_extra_group_by_result(599, 1));
  ASSERT_EQ(OB_SUCCESS, cells_.at(0)->publish_native_rows(eval_, 1));
  EXPECT_EQ(41, output(0, 0).get_int());
}

TEST_F(NativeStorageGroupedAggregate, real_group_by_owns_dictionary_keys_and_publishes_nullable_groups)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_REF_COLUMN, ObVarcharType, text_accuracy()));
  ASSERT_EQ(OB_SUCCESS, configure_same(1, T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(2, T_FUN_COUNT, ObIntType, ObAccuracy(19, 0)));
  ObColumnParam key_column(type_owner_), value_column(type_owner_);
  key_column.set_column_id(16); key_column.set_meta_type(outputs_[0].obj_meta_);
  key_column.set_accuracy(text_accuracy()); key_column.set_nullable_for_write(true);
  value_column.set_column_id(17); value_column.set_meta_type(inputs_[1].obj_meta_);
  value_column.set_accuracy(ObAccuracy(19, 0)); value_column.set_nullable_for_write(true);
  ObSEArray<ObColDesc, 2> columns;
  ObSEArray<ObColumnParam *, 2> parameters;
  for (ObColumnParam *column : {&key_column, &value_column}) {
    ObColDesc descriptor; descriptor.col_id_ = column->get_column_id(); descriptor.col_type_ = column->get_meta_type();
    ASSERT_EQ(OB_SUCCESS, columns.push_back(descriptor));
    ASSERT_EQ(OB_SUCCESS, parameters.push_back(column));
  }
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(type_owner_, 2, 1, columns, nullptr, &parameters));
  ObSEArray<int32_t, 2> out_project, group_project, agg_project;
  ObSEArray<ObExpr *, 2> out_exprs, agg_exprs;
  ASSERT_EQ(OB_SUCCESS, out_project.push_back(0)); ASSERT_EQ(OB_SUCCESS, group_project.push_back(0));
  ASSERT_EQ(OB_SUCCESS, agg_project.push_back(1)); ASSERT_EQ(OB_SUCCESS, agg_project.push_back(1));
  ASSERT_EQ(OB_SUCCESS, out_exprs.push_back(&outputs_[0]));
  ASSERT_EQ(OB_SUCCESS, agg_exprs.push_back(&outputs_[1])); ASSERT_EQ(OB_SUCCESS, agg_exprs.push_back(&outputs_[2]));
  ObTableAccessParam access;
  access.iter_param_.read_info_ = &read_info;
  access.iter_param_.out_cols_project_ = &out_project;
  access.iter_param_.group_by_cols_project_ = &group_project;
  access.iter_param_.agg_cols_project_ = &agg_project;
  access.output_exprs_ = &out_exprs; access.aggregate_exprs_ = &agg_exprs;
  ObTableAccessContext context;
  context.sql_mode_ = 0;
  ObGroupByCell grouped(BATCH, cell_owner_);
  ASSERT_EQ(OB_SUCCESS, grouped.init(access, context, eval_));
  bool use_group_by = false;
  ASSERT_EQ(OB_SUCCESS, grouped.decide_use_group_by(16, 16, 2,
      static_cast<const ObBitmap *>(nullptr), use_group_by));
  ASSERT_TRUE(use_group_by);
  std::string first_key("a\0b", 3), second_key("c");
  ObDatum *keys = grouped.get_group_by_col_datums_to_fill();
  ASSERT_NE(nullptr, keys);
  keys[0] = physical_text(first_key); keys[1] = physical_text(second_key); keys[2].set_null();
  grouped.set_distinct_cnt(3);
  ASSERT_EQ(OB_SUCCESS, grouped.capture_group_key_input());
  grouped.release_group_key_views();
  std::fill(first_key.begin(), first_key.end(), char(0xA5));
  std::fill(second_key.begin(), second_key.end(), char(0xA5));
  int64_t a[] = {9, 4, 0, -1}, b[] = {3, 7, 5, 2};
  ObDatum first[] = {physical(a[0]), physical(a[1]), null_input(), physical(a[3])};
  ObDatum second[] = {physical(b[0]), physical(b[1]), physical(b[2]), physical(b[3])};
  const uint32_t refs_a[] = {0, 1, 2, 0}, refs_b[] = {1, 2, 0, 2};
  grouped.set_ref_cnt(4);
  MEMCPY(grouped.get_refs_buf(), refs_a, sizeof(refs_a));
  ASSERT_EQ(OB_SUCCESS, grouped.eval_batch(first, 4, 0));
  ASSERT_EQ(OB_SUCCESS, grouped.eval_batch(first, 4, 1));
  MEMSET(a, 0xA5, sizeof(a));
  MEMCPY(grouped.get_refs_buf(), refs_b, sizeof(refs_b));
  ASSERT_EQ(OB_SUCCESS, grouped.eval_batch(second, 4, 0));
  ASSERT_EQ(OB_SUCCESS, grouped.eval_batch(second, 4, 1));
  MEMSET(b, 0xA5, sizeof(b));
  ASSERT_EQ(OB_SUCCESS, grouped.collect_result());
  ASSERT_EQ(OB_SUCCESS, grouped.publish_native_rows(eval_, 3));
  expect_text(0, 0, std::string("a\0b", 3)); expect_text(0, 1, "c"); EXPECT_TRUE(output(0, 2).is_null());
  const int64_t minima[] = {-1, 3, 2}, nonnull[] = {3, 2, 2};
  for (int row = 0; row < 3; ++row) {
    EXPECT_EQ(minima[row], output(1, row).get_int()); EXPECT_EQ(nonnull[row], output(2, row).get_int());
  }
  std::string replacement(768, 'z');
  keys = grouped.get_group_by_col_datums_to_fill();
  keys[0] = physical_text(replacement); keys[1] = ObDatum("c", 1, false); keys[2].set_null();
  const int64_t calls = cell_owner_.calls_;
  cell_owner_.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, grouped.capture_group_key_input());
  EXPECT_GT(cell_owner_.calls_, calls);
  cell_owner_.reject_ = false;
  grouped.release_group_key_views();
  ASSERT_EQ(OB_SUCCESS, grouped.publish_native_rows(eval_, 3));
  expect_text(0, 0, std::string("a\0b", 3));
  keys = grouped.get_group_by_col_datums_to_fill();
  keys[0] = physical_text(replacement); keys[1] = ObDatum("c", 1, false); keys[2].set_null();
  ASSERT_EQ(OB_SUCCESS, grouped.capture_group_key_input());
  grouped.release_group_key_views();
  std::fill(replacement.begin(), replacement.end(), char(0xA5));
  ASSERT_EQ(OB_SUCCESS, grouped.publish_native_rows(eval_, 3));
  grouped.reset();
  EXPECT_TRUE(cell_owner_.live_.empty());
  expect_text(0, 0, std::string(768, 'z')); expect_text(0, 1, "c"); EXPECT_TRUE(output(0, 2).is_null());
}

TEST_F(NativeStorageGroupedAggregate, real_microblock_same_column_min_max_sum_count_share_decoded_input)
{
  ASSERT_EQ(OB_SUCCESS, configure_same(0, T_REF_COLUMN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(1, T_FUN_MIN, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(2, T_FUN_MAX, ObIntType, ObAccuracy(19, 0)));
  ASSERT_EQ(OB_SUCCESS, configure(3, T_FUN_SUM, ObIntType, ObAccuracy(19, 0),
      ObNumberType, ObAccuracy(38, 0)));
  ASSERT_EQ(OB_SUCCESS, configure_same(4, T_FUN_COUNT, ObIntType, ObAccuracy(19, 0)));
  ObColumnParam key_column(type_owner_), value_column(type_owner_);
  key_column.set_column_id(16); key_column.set_meta_type(outputs_[0].obj_meta_);
  key_column.set_accuracy(ObAccuracy(19, 0)); key_column.set_nullable_for_write(true);
  value_column.set_column_id(17); value_column.set_meta_type(inputs_[1].obj_meta_);
  value_column.set_accuracy(ObAccuracy(19, 0)); value_column.set_nullable_for_write(true);
  ObSEArray<ObColDesc, 2> requested_columns;
  ObSEArray<ObColumnParam *, 2> parameters;
  for (ObColumnParam *column : {&key_column, &value_column}) {
    ObColDesc descriptor;
    descriptor.col_id_ = column->get_column_id(); descriptor.col_type_ = column->get_meta_type();
    ASSERT_EQ(OB_SUCCESS, requested_columns.push_back(descriptor));
    ASSERT_EQ(OB_SUCCESS, parameters.push_back(column));
  }
  ObTableReadInfo read_info;
  ASSERT_EQ(OB_SUCCESS, read_info.init(type_owner_, 2, 1, requested_columns, nullptr, &parameters));
  // Match the real MVCC stored layout: the two hidden rowkey columns precede
  // the requested value. The reader maps requested column 1 to stored slot 3.
  ASSERT_EQ(0, read_info.get_columns_index().at(0));
  ASSERT_EQ(3, read_info.get_columns_index().at(1));
  ObSEArray<ObColDesc, 4> stored_columns;
  ASSERT_EQ(OB_SUCCESS, stored_columns.push_back(requested_columns.at(0)));
  ObColDesc hidden = requested_columns.at(0);
  hidden.col_id_ = OB_HIDDEN_TRANS_VERSION_COLUMN_ID;
  ASSERT_EQ(OB_SUCCESS, stored_columns.push_back(hidden));
  hidden.col_id_ = OB_HIDDEN_SQL_SEQUENCE_COLUMN_ID;
  ASSERT_EQ(OB_SUCCESS, stored_columns.push_back(hidden));
  ASSERT_EQ(OB_SUCCESS, stored_columns.push_back(requested_columns.at(1)));
  ObMicroBlockEncodingCtx encoding;
  encoding.macro_block_size_ = OB_DEFAULT_MACRO_BLOCK_SIZE;
  encoding.micro_block_size_ = 16 * 1024;
  encoding.rowkey_column_cnt_ = 3;
  encoding.column_cnt_ = 4;
  encoding.col_descs_ = &stored_columns;
  encoding.compressor_type_ = NONE_COMPRESSOR;
  // Select the production raw column encoder deterministically. This test
  // exercises the real block reader, not the encoder-selection heuristics.
  bool encodings[ObColumnHeader::MAX_TYPE]{};
  encodings[ObColumnHeader::RAW] = true;
  encoding.encoder_opt_.encodings_ = encodings;
  ObMicroBlockEncoder encoder;
  ASSERT_EQ(OB_SUCCESS, encoder.init(encoding));
  ObDatumRow stored_row;
  ASSERT_EQ(OB_SUCCESS, stored_row.init(type_owner_, 4));
  stored_row.row_flag_.set_flag(ObDmlFlag::DF_INSERT);
  stored_row.set_last_multi_version_row();
  const int64_t values[] = {9, 4, 0, -1, 3, 7, 5, 2};
  const uint32_t groups[] = {0, 1, 2, 0, 1, 2, 0, 2};
  for (int i = 0; i < BATCH; ++i) {
    stored_row.storage_datums_[0].set_int(groups[i] + 10);
    stored_row.storage_datums_[1].set_int(-100);
    stored_row.storage_datums_[2].set_int(0);
    if (i == 2) { stored_row.storage_datums_[3].set_null(); }
    else { stored_row.storage_datums_[3].set_int(values[i]); }
    ASSERT_EQ(OB_SUCCESS, encoder.append_row(stored_row));
  }
  char *block = nullptr;
  int64_t block_bytes = 0;
  ASSERT_EQ(OB_SUCCESS, encoder.build_block(block, block_bytes));
  ASSERT_NE(nullptr, block); ASSERT_GT(block_bytes, 0);
  std::vector<char> encoded(block, block + block_bytes);
  encoder.reset();
  for (int i = 0; i < 4; ++i) { stored_row.storage_datums_[i].set_int(INT64_MIN); }

  ObSEArray<int32_t, 4> out_project, group_project, agg_project;
  ObSEArray<ObExpr *, 4> out_exprs, agg_exprs;
  ASSERT_EQ(OB_SUCCESS, out_project.push_back(0)); ASSERT_EQ(OB_SUCCESS, group_project.push_back(0));
  ASSERT_EQ(OB_SUCCESS, out_exprs.push_back(&outputs_[0]));
  for (int i = 1; i <= 4; ++i) {
    ASSERT_EQ(OB_SUCCESS, agg_project.push_back(1));
    ASSERT_EQ(OB_SUCCESS, agg_exprs.push_back(&outputs_[i]));
  }
  ObTableAccessParam access;
  access.iter_param_.read_info_ = &read_info;
  access.iter_param_.out_cols_project_ = &out_project;
  access.iter_param_.group_by_cols_project_ = &group_project;
  access.iter_param_.agg_cols_project_ = &agg_project;
  access.output_exprs_ = &out_exprs; access.aggregate_exprs_ = &agg_exprs;
  ObTableAccessContext context;
  context.sql_mode_ = 0;
  ObGroupByCell grouped(BATCH, cell_owner_);
  ASSERT_EQ(OB_SUCCESS, grouped.init(access, context, eval_));
  bool use_group_by = false;
  ASSERT_EQ(OB_SUCCESS, grouped.decide_use_group_by(16, 16, 2,
      static_cast<const ObBitmap *>(nullptr), use_group_by));
  ASSERT_TRUE(use_group_by);
  int64_t key_values[] = {10, 11, 12};
  ObDatum *keys = grouped.get_group_by_col_datums_to_fill();
  for (int i = 0; i < 3; ++i) { keys[i] = physical(key_values[i]); }
  grouped.set_distinct_cnt(3);
  ASSERT_EQ(OB_SUCCESS, grouped.capture_group_key_input());
  grouped.release_group_key_views();
  MEMSET(key_values, 0xA5, sizeof(key_values));

  ObMicroBlockDecoder decoder;
  const ObMicroBlockData block_data(encoded.data(), encoded.size());
  ASSERT_EQ(OB_SUCCESS, decoder.init(block_data, read_info));
  const char *cell_datas[BATCH]{};
  const int32_t first_ids[] = {0, 1, 2, 3}, second_ids[] = {4, 5, 6, 7};
  grouped.set_ref_cnt(4);
  MEMCPY(grouped.get_refs_buf(), groups, 4 * sizeof(uint32_t));
  ASSERT_EQ(OB_SUCCESS, decoder.get_group_by_aggregate_result(
      access.iter_param_, context, first_ids, cell_datas, 4, grouped));
  MEMCPY(grouped.get_refs_buf(), groups + 4, 4 * sizeof(uint32_t));
  ASSERT_EQ(OB_SUCCESS, decoder.get_group_by_aggregate_result(
      access.iter_param_, context, second_ids, cell_datas, 4, grouped));
  decoder.reset();
  std::fill(encoded.begin(), encoded.end(), char(0xA5));
  ASSERT_EQ(OB_SUCCESS, grouped.collect_result());
  ASSERT_EQ(OB_SUCCESS, grouped.publish_native_rows(eval_, 3));
  grouped.reset();
  EXPECT_TRUE(cell_owner_.live_.empty());
  const int64_t minima[] = {-1, 3, 2}, maxima[] = {9, 4, 7}, nonnull[] = {3, 2, 2};
  const char *sums[] = {"13", "7", "9"};
  for (int i = 0; i < 3; ++i) {
    EXPECT_EQ(i + 10, output(0, i).get_int());
    EXPECT_EQ(minima[i], output(1, i).get_int()); EXPECT_EQ(maxima[i], output(2, i).get_int());
    expect_number(3, i, sums[i]); EXPECT_EQ(nonnull[i], output(4, i).get_int());
  }
}
} // namespace
