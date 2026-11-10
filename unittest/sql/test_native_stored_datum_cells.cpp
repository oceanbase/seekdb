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

#include "sql/engine/basic/ob_chunk_datum_store.h"
#include "sql/engine/basic/stored_datum_cell.h"
#include "common/datum/datum.h"
#include "common/datum/sql_type.h"
#include "lib/container/ob_array_helper.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
using StoredRow = ObChunkDatumStore::StoredRow;
constexpr int64_t COLUMN_COUNT = 7;
constexpr uint32_t EXTRA_SIZE = 11;
constexpr int64_t TAIL_SIZE = 16;
constexpr int64_t SMALL_COEFFICIENT = -123456789012345678LL;

SqlTypeMeta integer_type()
{
  SqlTypeMeta type;
  type.set_int();
  return type;
}

SqlTypeMeta string_type()
{
  SqlTypeMeta type;
  type.set_varchar();
  type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  return type;
}

SqlTypeMeta decimal_type(ObScale scale)
{
  SqlTypeMeta type;
  type.set_decimal_int(scale);
  return type;
}

ObAccuracy numeric_accuracy(ObPrecision precision, ObScale scale)
{
  ObAccuracy accuracy;
  accuracy.set_precision(precision);
  accuracy.set_scale(scale);
  return accuracy;
}

struct NativeInput
{
  NativeInput()
    : integer_type_(integer_type()), string_type_(string_type()),
      wide_type_(decimal_type(5)), small_type_(decimal_type(2)),
      wide_accuracy_(numeric_accuracy(20, 5)), small_accuracy_(numeric_accuracy(18, 2)),
      plan_values_{DatumCopyPlan(integer_type_, ObAccuracy{}),
          DatumCopyPlan(integer_type_, ObAccuracy{}), DatumCopyPlan(integer_type_, ObAccuracy{}),
          DatumCopyPlan(wide_type_, wide_accuracy_), DatumCopyPlan(string_type_, ObAccuracy{}),
          DatumCopyPlan(wide_type_, wide_accuracy_), DatumCopyPlan(small_type_, small_accuracy_)},
      plans_(COLUMN_COUNT, plan_values_, COLUMN_COUNT)
  {
    values_[0].set_int(INT64_MIN);
    values_[1].set_int(0);
    values_[2].set_int(INT64_MAX);
    values_[3].set_null();
    EXPECT_EQ(OB_SUCCESS, values_[4].set_reference(text_, sizeof(text_) - 1));
    MEMSET(wide_coefficient_, 0, sizeof(wide_coefficient_));
    const uint64_t low_word = 12345678901234567890ULL;
    MEMCPY(wide_coefficient_, &low_word, sizeof(low_word));
    EXPECT_EQ(OB_SUCCESS, values_[5].set_reference(wide_coefficient_, sizeof(wide_coefficient_)));
    values_[6].set_int(SMALL_COEFFICIENT);
  }

  int64_t row_size() const
  {
    return sizeof(StoredRow) + COLUMN_COUNT * sizeof(StoredDatumCell) + EXTRA_SIZE
        + 3 * sizeof(int64_t) + (values_[4].is_null() ? 0 : sizeof(text_) - 1)
        + sizeof(wide_coefficient_) + sizeof(int64_t);
  }

  void overwrite()
  {
    MEMSET(text_, 'x', sizeof(text_) - 1);
    MEMSET(wide_coefficient_, 0xa5, sizeof(wide_coefficient_));
    for (auto &value : values_) { value.set_null(); }
  }

  SqlTypeMeta integer_type_;
  SqlTypeMeta string_type_;
  SqlTypeMeta wide_type_;
  SqlTypeMeta small_type_;
  ObAccuracy wide_accuracy_;
  ObAccuracy small_accuracy_;
  DatumCopyPlan plan_values_[COLUMN_COUNT];
  ObArrayHelper<DatumCopyPlan> plans_;
  Datum values_[COLUMN_COUNT];
  char text_[16] = "native customer";
  alignas(16) unsigned char wide_coefficient_[16];
};

char tail_byte(int64_t index) { return static_cast<char>(0x61 + index); }
char extra_byte(int64_t index) { return static_cast<char>(0x31 + index); }

StoredRow *build_row(NativeInput &input, std::vector<char> &memory, bool offsets = false)
{
  const int64_t size = input.row_size();
  memory.assign(size + TAIL_SIZE, char(0x5a));
  for (int64_t i = 0; i < TAIL_SIZE; ++i) { memory[size + i] = tail_byte(i); }
  StoredRow *row = nullptr;
  const int ret = StoredRow::build_native(row, input.values_, COLUMN_COUNT, input.plans_,
      memory.data(), size, EXTRA_SIZE, offsets);
  EXPECT_EQ(OB_SUCCESS, ret);
  if (ret == OB_SUCCESS) {
    char *extra = static_cast<char *>(row->get_extra_payload());
    for (int64_t i = 0; i < EXTRA_SIZE; ++i) { extra[i] = extra_byte(i); }
  }
  return ret == OB_SUCCESS ? row : nullptr;
}

void expect_extra_and_tail(const StoredRow &row, const std::vector<char> &memory)
{
  const char *extra = static_cast<const char *>(row.get_extra_payload());
  for (int64_t i = 0; i < EXTRA_SIZE; ++i) { EXPECT_EQ(extra_byte(i), extra[i]); }
  for (int64_t i = 0; i < TAIL_SIZE; ++i) {
    EXPECT_EQ(tail_byte(i), memory[memory.size() - TAIL_SIZE + i]);
  }
}

void expect_native_row(const StoredRow &row, const ObIArray<DatumCopyPlan> &plans, bool null_text = false)
{
  ASSERT_EQ(COLUMN_COUNT, row.cnt_);
  Datum output[COLUMN_COUNT];
  for (int64_t i = 0; i < COLUMN_COUNT; ++i) {
    SCOPED_TRACE(i);
    ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::decode_native(row.stored_cells()[i], plans.at(i), output[i]));
  }
  EXPECT_EQ(INT64_MIN, output[0].get_int());
  EXPECT_EQ(0, output[1].get_int());
  EXPECT_EQ(INT64_MAX, output[2].get_int());
  EXPECT_TRUE(output[3].is_null());
  EXPECT_EQ(ObDecimalIntType, plans.at(3).get_type_meta().get_type());
  EXPECT_EQ(20, plans.at(3).get_accuracy().get_precision());
  EXPECT_EQ(5, plans.at(3).get_accuracy().get_scale());
  if (null_text) {
    EXPECT_TRUE(output[4].is_null());
  } else {
    ASSERT_TRUE(output[4].is_present());
    ASSERT_EQ(15, output[4].length());
    EXPECT_EQ(0, MEMCMP("native customer", output[4].get_reference(), output[4].length()));
    EXPECT_EQ(row.stored_cells()[4].ptr_, output[4].get_reference());
  }
  ASSERT_TRUE(output[5].is_present());
  ASSERT_EQ(16, output[5].length());
  EXPECT_EQ(row.stored_cells()[5].ptr_, output[5].get_reference());
  uint64_t low = 0, high = 1;
  MEMCPY(&low, output[5].get_reference(), sizeof(low));
  MEMCPY(&high, static_cast<const char *>(output[5].get_reference()) + sizeof(low), sizeof(high));
  EXPECT_EQ(12345678901234567890ULL, low);
  EXPECT_EQ(0, high);
  EXPECT_EQ(SMALL_COEFFICIENT, output[6].get_int());
  EXPECT_EQ(0, output[6].length());
  for (const int64_t i : {int64_t(0), int64_t(1), int64_t(2), int64_t(6)}) {
    EXPECT_EQ(0, output[i].length());
    EXPECT_EQ(Datum::NoReferenceFlags, output[i].reference_flags());
  }
}
} // namespace

TEST(NativeStoredDatumCells, three_bigint_extremes_materialize_only_at_the_storage_boundary)
{
  const DatumCopyPlan plan(integer_type(), ObAccuracy{});
  const int64_t values[] = {INT64_MIN, 0, INT64_MAX};
  for (const int64_t value : values) {
    SCOPED_TRACE(value);
    Datum input;
    input.set_int(value);
    char bytes[32];
    MEMSET(bytes, 0x5a, sizeof(bytes));
    StoredDatumCell stored;
    int64_t position = 3;
    ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::encode_native_copy(input, stored, plan,
        bytes, sizeof(bytes), position));
    EXPECT_EQ(11, position);
    EXPECT_EQ(8, stored.len_);
    EXPECT_EQ(bytes + 3, stored.ptr_);
    input.set_null();
    Datum decoded;
    ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::decode_native(stored, plan, decoded));
    MEMSET(bytes, 0, sizeof(bytes));
    EXPECT_TRUE(decoded.is_present());
    EXPECT_EQ(value, decoded.get_int());
    EXPECT_EQ(0, decoded.length());
  }
}

TEST(NativeStoredDatumCells, complete_decimal_types_select_reference_or_inline_coefficient)
{
  NativeInput input;
  SqlType wide, small;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(input.wide_type_, input.wide_accuracy_, wide));
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(input.small_type_, input.small_accuracy_, small));
  EXPECT_EQ(DatumRepresentation::Reference, wide.canonical_representation());
  EXPECT_EQ(DatumRepresentation::Inline64, small.canonical_representation());
  const DatumCopyPlan wide_plan(wide), small_plan(small);
  char bytes[64];
  StoredDatumCell wide_cell, small_cell;
  int64_t position = 1;
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::encode_native_copy(input.values_[5], wide_cell,
      wide_plan, bytes, sizeof(bytes), position));
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::encode_native_copy(input.values_[6], small_cell,
      small_plan, bytes, sizeof(bytes), position));
  EXPECT_EQ(16, wide_cell.len_);
  EXPECT_EQ(8, small_cell.len_);
  input.overwrite();
  Datum wide_output, small_output;
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::decode_native(wide_cell, wide_plan, wide_output));
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::decode_native(small_cell, small_plan, small_output));
  EXPECT_EQ(wide_cell.ptr_, wide_output.get_reference());
  EXPECT_EQ(16, wide_output.length());
  EXPECT_EQ(SMALL_COEFFICIENT, small_output.get_int());
  EXPECT_EQ(0, small_output.length());
  uint64_t low = 0;
  MEMCPY(&low, wide_output.get_reference(), sizeof(low));
  EXPECT_EQ(12345678901234567890ULL, low);
}

TEST(NativeStoredDatumCells, build_keeps_twelve_byte_cells_typed_null_and_owned_payloads)
{
  for (const bool null_text : {false, true}) {
    SCOPED_TRACE(null_text);
    NativeInput input;
    if (null_text) { input.values_[4].set_null(); }
    const int64_t expected_size = input.row_size();
    std::vector<char> memory;
    StoredRow *row = build_row(input, memory);
    ASSERT_NE(nullptr, row);
    EXPECT_EQ(16, sizeof(Datum));
    EXPECT_EQ(12, sizeof(StoredDatumCell));
    EXPECT_EQ(expected_size, row->row_size_);
    EXPECT_EQ(row->payload_ + COLUMN_COUNT * 12, row->get_extra_payload());
    EXPECT_EQ(8, row->stored_cells()[0].len_);
    EXPECT_EQ(8, row->stored_cells()[1].len_);
    EXPECT_EQ(8, row->stored_cells()[2].len_);
    EXPECT_TRUE(row->stored_cells()[3].is_null());
    EXPECT_EQ(16, row->stored_cells()[5].len_);
    EXPECT_EQ(8, row->stored_cells()[6].len_);
    input.overwrite();
    expect_native_row(*row, input.plans_, null_text);
    expect_extra_and_tail(*row, memory);
  }
}

TEST(NativeStoredDatumCells, assigned_native_row_decodes_after_the_original_buffer_is_overwritten)
{
  NativeInput input;
  std::vector<char> original;
  StoredRow *row = build_row(input, original);
  ASSERT_NE(nullptr, row);
  std::vector<char> moved = original;
  auto *assigned = new (moved.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, assigned->assign(row));
  EXPECT_NE(row->stored_cells()[4].ptr_, assigned->stored_cells()[4].ptr_);
  EXPECT_NE(row->stored_cells()[5].ptr_, assigned->stored_cells()[5].ptr_);
  input.overwrite();
  MEMSET(original.data(), 0xa6, original.size());
  expect_native_row(*assigned, input.plans_);
  expect_extra_and_tail(*assigned, moved);
}

TEST(NativeStoredDatumCells, native_offset_image_moves_and_reloads_under_the_same_column_plans)
{
  NativeInput input;
  std::vector<char> original;
  StoredRow *row = build_row(input, original, true);
  ASSERT_NE(nullptr, row);
  for (int64_t i = 0; i < COLUMN_COUNT; ++i) {
    const StoredDatumCell &cell = row->stored_cells()[i];
    if (!cell.is_null()) {
      const uintptr_t offset = reinterpret_cast<uintptr_t>(cell.ptr_);
      EXPECT_GE(offset, sizeof(StoredRow) + COLUMN_COUNT * sizeof(StoredDatumCell) + EXTRA_SIZE);
      EXPECT_LE(offset + cell.len_, row->row_size_);
    }
  }
  std::vector<char> moved = original;
  MEMSET(original.data(), 0xa7, original.size());
  input.overwrite();
  auto *reloaded = reinterpret_cast<StoredRow *>(moved.data());
  reloaded->swizzling();
  expect_native_row(*reloaded, input.plans_);
  expect_extra_and_tail(*reloaded, moved);
}

TEST(NativeStoredDatumCells, zero_length_string_and_typed_null_need_no_payload_but_remain_distinct)
{
  const DatumCopyPlan plan(string_type(), ObAccuracy{});
  Datum empty, null_value;
  ASSERT_EQ(OB_SUCCESS, empty.set_reference(nullptr, 0));
  null_value.set_null();
  StoredDatumCell empty_cell, null_cell;
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::encode_native_copy(empty, empty_cell,
      plan, nullptr, 0, position));
  EXPECT_EQ(0, position);
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::encode_native_copy(null_value, null_cell,
      plan, nullptr, 0, position));
  EXPECT_EQ(0, position);
  EXPECT_FALSE(empty_cell.is_null());
  EXPECT_EQ(0, empty_cell.len_);
  EXPECT_TRUE(null_cell.is_null());
  Datum decoded_empty, decoded_null;
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::decode_native(empty_cell, plan, decoded_empty));
  ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::decode_native(null_cell, plan, decoded_null));
  EXPECT_TRUE(decoded_empty.is_present());
  EXPECT_EQ(0, decoded_empty.length());
  EXPECT_TRUE(decoded_null.is_null());
}

TEST(NativeStoredDatumCells, short_native_codec_copies_do_not_publish_bytes_cursor_or_cell)
{
  NativeInput input;
  ObDatum marker;
  marker.set_string("unchanged", 9);
  const StoredDatumCell original = StoredDatumCodec::encode_legacy_view(marker);
  for (const int64_t column : {int64_t(0), int64_t(4), int64_t(5), int64_t(6)}) {
    SCOPED_TRACE(column);
    int64_t payload_size = -1;
    ASSERT_EQ(OB_SUCCESS, input.plans_.at(column).legacy_payload_size(input.values_[column], payload_size));
    for (int64_t available = 0; available < payload_size; ++available) {
      SCOPED_TRACE(available);
      std::vector<char> bytes(128, char(0x5b));
      const std::vector<char> before = bytes;
      StoredDatumCell destination = original;
      int64_t position = 3;
      EXPECT_EQ(OB_BUF_NOT_ENOUGH, StoredDatumCodec::encode_native_copy(input.values_[column],
          destination, input.plans_.at(column), bytes.data(), position + available, position));
      EXPECT_EQ(3, position);
      EXPECT_EQ(0, MEMCMP(&original, &destination, sizeof(destination)));
      EXPECT_EQ(before, bytes);
    }
  }
}

TEST(NativeStoredDatumCells, invalid_native_shapes_and_stored_cells_leave_outputs_unchanged)
{
  NativeInput input;
  Datum invalid;
  ASSERT_EQ(OB_SUCCESS, invalid.set_reference(input.wide_coefficient_, 7));
  ObDatum marker;
  marker.set_string("unchanged", 9);
  const StoredDatumCell original = StoredDatumCodec::encode_legacy_view(marker);
  StoredDatumCell destination = original;
  std::vector<char> bytes(128, char(0x5c));
  const std::vector<char> before = bytes;
  int64_t position = 7;
  EXPECT_NE(OB_SUCCESS, StoredDatumCodec::encode_native_copy(invalid, destination,
      input.plans_.at(5), bytes.data(), bytes.size(), position));
  EXPECT_EQ(7, position);
  EXPECT_EQ(0, MEMCMP(&original, &destination, sizeof(destination)));
  EXPECT_EQ(before, bytes);
  position = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredDatumCodec::encode_native_copy(input.values_[0], destination,
      input.plans_.at(0), bytes.data(), bytes.size(), position));
  EXPECT_EQ(-1, position);
  EXPECT_EQ(0, MEMCMP(&original, &destination, sizeof(destination)));
  EXPECT_EQ(before, bytes);
  position = 7;
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredDatumCodec::encode_native_copy(input.values_[0], destination,
      input.plans_.at(0), nullptr, bytes.size(), position));
  EXPECT_EQ(7, position);
  EXPECT_EQ(0, MEMCMP(&original, &destination, sizeof(destination)));
  EXPECT_EQ(before, bytes);
  Datum decoded;
  decoded.set_int(77);
  const Datum decoded_before = decoded;
  StoredDatumCell malformed;
  malformed.ptr_ = bytes.data();
  malformed.pack_ = 7;
  EXPECT_NE(OB_SUCCESS, StoredDatumCodec::decode_native(malformed, input.plans_.at(0), decoded));
  EXPECT_EQ(77, decoded.get_int());
  EXPECT_EQ(0, MEMCMP(&decoded_before, &decoded, sizeof(decoded)));
  malformed.ptr_ = nullptr;
  malformed.pack_ = 8;
  EXPECT_NE(OB_SUCCESS, StoredDatumCodec::decode_native(malformed, input.plans_.at(0), decoded));
  EXPECT_EQ(77, decoded.get_int());
  EXPECT_EQ(0, MEMCMP(&decoded_before, &decoded, sizeof(decoded)));
}

TEST(NativeStoredDatumCells, every_short_build_capacity_preserves_existing_output_and_all_buffer_bytes)
{
  NativeInput input;
  const int64_t required = input.row_size();
  StoredRow existing;
  for (int64_t capacity = 0; capacity < required; ++capacity) {
    SCOPED_TRACE(capacity);
    std::vector<char> bytes(required + TAIL_SIZE, char(0x5d));
    const std::vector<char> before = bytes;
    StoredRow *output = &existing;
    EXPECT_EQ(OB_BUF_NOT_ENOUGH, StoredRow::build_native(output, input.values_, COLUMN_COUNT,
        input.plans_, bytes.data(), capacity, EXTRA_SIZE));
    EXPECT_EQ(&existing, output);
    EXPECT_EQ(before, bytes);
  }
}

TEST(NativeStoredDatumCells, final_column_preflight_and_shape_errors_do_not_publish_a_partial_row)
{
  NativeInput input;
  StoredRow existing;
  StoredRow *output = &existing;
  std::vector<char> bytes(512, char(0x5e));
  const std::vector<char> before = bytes;
  // The first six values are valid. Failure at the final column must happen
  // before an earlier integer or referenced payload reaches the output buffer.
  ASSERT_EQ(OB_SUCCESS, input.values_[6].set_reference(input.wide_coefficient_, 16));
  EXPECT_NE(OB_SUCCESS, StoredRow::build_native(output, input.values_, COLUMN_COUNT,
      input.plans_, bytes.data(), bytes.size(), EXTRA_SIZE));
  EXPECT_EQ(&existing, output);
  EXPECT_EQ(before, bytes);
  input.values_[6].set_int(SMALL_COEFFICIENT);
  ObArrayHelper<DatumCopyPlan> missing_plan(COLUMN_COUNT - 1, input.plan_values_, COLUMN_COUNT - 1);
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredRow::build_native(output, input.values_, COLUMN_COUNT,
      missing_plan, bytes.data(), bytes.size(), EXTRA_SIZE));
  EXPECT_EQ(&existing, output);
  EXPECT_EQ(before, bytes);
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredRow::build_native(output, nullptr, COLUMN_COUNT,
      input.plans_, bytes.data(), bytes.size(), EXTRA_SIZE));
  EXPECT_EQ(&existing, output);
  EXPECT_EQ(before, bytes);
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredRow::build_native(output, input.values_, -1,
      input.plans_, bytes.data(), bytes.size(), EXTRA_SIZE));
  EXPECT_EQ(&existing, output);
  EXPECT_EQ(before, bytes);
}

TEST(NativeStoredDatumCells, wide_decimal_zero_is_materialized_before_legacy_row_consumption)
{
  const ObPrecision precisions[] = {20, 40, 80};
  const int64_t widths[] = {16, 32, 64};
  for (int i = 0; i < 3; ++i) {
    SCOPED_TRACE(precisions[i]);
    Datum values[2];
    ASSERT_EQ(OB_SUCCESS, values[0].set_reference(nullptr, 0));
    values[1].set_int(77);
    DatumCopyPlan plan_values[] = {
        DatumCopyPlan(decimal_type(2), numeric_accuracy(precisions[i], 2)),
        DatumCopyPlan(integer_type(), ObAccuracy{})};
    ObArrayHelper<DatumCopyPlan> plans(2, plan_values, 2);
    const int64_t required = sizeof(StoredRow) + 2 * sizeof(StoredDatumCell) + EXTRA_SIZE
        + widths[i] + sizeof(int64_t);
    std::vector<char> bytes(required + TAIL_SIZE, char(0x5a));
    StoredRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, StoredRow::build_native(row, values, 2, plans,
        bytes.data(), required, EXTRA_SIZE));
    ASSERT_NE(nullptr, row);
    EXPECT_EQ(required, row->row_size_);
    const ObDatum legacy = StoredDatumCodec::decode_legacy_view(row->stored_cells()[0]);
    ASSERT_FALSE(legacy.is_null());
    ASSERT_EQ(widths[i], legacy.len_);
    // The existing precision-selected readers consume this complete width.
    // Payloads need not be aligned, so the test reads through memcpy.
    char coefficient[64];
    MEMCPY(coefficient, legacy.ptr_, widths[i]);
    for (int64_t j = 0; j < widths[i]; ++j) { EXPECT_EQ(0, coefficient[j]); }
    const ObDatum next = StoredDatumCodec::decode_legacy_view(row->stored_cells()[1]);
    int64_t integer = 0;
    MEMCPY(&integer, next.ptr_, sizeof(integer));
    EXPECT_EQ(77, integer);
    for (int64_t j = required; j < static_cast<int64_t>(bytes.size()); ++j) {
      EXPECT_EQ(char(0x5a), bytes[j]);
    }
  }
}
