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
#include "lib/allocator/ob_allocator.h"
#include <gtest/gtest.h>
#include <cstring>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
using StoredRow = ObChunkDatumStore::StoredRow;
constexpr int64_t TAIL_SIZE = 16;
constexpr uint32_t EXTRA_SIZE = 13;

char tail_byte(int64_t index) { return static_cast<char>(0x51 + index); }
char extra_byte(int64_t index) { return static_cast<char>(0x21 + index); }

void initialize_tail(std::vector<char> &memory, int64_t row_size)
{
  for (int64_t i = 0; i < TAIL_SIZE; ++i) { memory[row_size + i] = tail_byte(i); }
}

void expect_tail(const std::vector<char> &memory)
{
  ASSERT_GE(memory.size(), TAIL_SIZE);
  const int64_t start = memory.size() - TAIL_SIZE;
  for (int64_t i = 0; i < TAIL_SIZE; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(tail_byte(i), memory[start + i]);
  }
}

void initialize_extra(StoredRow &row, uint32_t size)
{
  char *extra = static_cast<char *>(row.get_extra_payload());
  for (int64_t i = 0; i < size; ++i) { extra[i] = extra_byte(i); }
}

void expect_extra(const StoredRow &row, uint32_t size)
{
  const char *extra = static_cast<const char *>(row.get_extra_payload());
  for (int64_t i = 0; i < size; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(extra_byte(i), extra[i]);
  }
}

StoredRow *copy_datums(const ObDatum *datums, int64_t count, uint32_t extra,
                      std::vector<char> &memory)
{
  const int64_t row_size = ObChunkDatumStore::Block::row_store_size(datums, count, extra);
  memory.assign(row_size + TAIL_SIZE, 0);
  initialize_tail(memory, row_size);
  auto *row = new (memory.data()) StoredRow;
  const int ret = row->copy_shadow_datums(datums, count, row->payload_,
      row_size - sizeof(StoredRow), row_size, extra);
  EXPECT_EQ(OB_SUCCESS, ret);
  if (ret == OB_SUCCESS) { initialize_extra(*row, extra); }
  return ret == OB_SUCCESS ? row : nullptr;
}

ObDatum decoded(const StoredRow &row, int64_t index)
{
  return StoredDatumCodec::decode_legacy_view(row.stored_cells()[index]);
}

int64_t integer_bytes(const ObDatum &datum)
{
  int64_t value = 0;
  EXPECT_FALSE(datum.is_null());
  EXPECT_EQ(sizeof(value), datum.len_);
  EXPECT_NE(nullptr, datum.ptr_);
  if (!datum.is_null() && datum.len_ == sizeof(value) && datum.ptr_ != nullptr) {
    // Extra bytes and preceding variable-width fields need not align payloads.
    MEMCPY(&value, datum.ptr_, sizeof(value));
  }
  return value;
}

struct MixedInput
{
  MixedInput() : integer_(-3000000001LL)
  {
    for (int64_t i = 0; i < 16; ++i) { decimal_[i] = static_cast<unsigned char>(i + 1); }
    datums_[0].ptr_ = reinterpret_cast<const char *>(&integer_);
    datums_[0].set_int(integer_);
    datums_[1].set_string(text_, sizeof(text_) - 1);
    datums_[2].set_null();
    datums_[3].set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(decimal_), sizeof(decimal_));
  }

  void overwrite()
  {
    integer_ = 0;
    MEMSET(text_, 'x', sizeof(text_) - 1);
    MEMSET(decimal_, 0, sizeof(decimal_));
    for (auto &datum : datums_) { datum.set_null(); }
  }

  int64_t integer_;
  char text_[21] = "customer row payload";
  alignas(16) unsigned char decimal_[16];
  ObDatum datums_[4];
};

void expect_mixed(const StoredRow &row)
{
  ASSERT_EQ(4, row.cnt_);
  EXPECT_EQ(-3000000001LL, integer_bytes(decoded(row, 0)));
  EXPECT_EQ(ObString("customer row payload"), decoded(row, 1).get_string());
  EXPECT_TRUE(decoded(row, 2).is_null());
  const ObDatum decimal = decoded(row, 3);
  ASSERT_FALSE(decimal.is_null());
  ASSERT_EQ(16, decimal.len_);
  ASSERT_NE(nullptr, decimal.ptr_);
  for (int64_t i = 0; i < 16; ++i) {
    SCOPED_TRACE(i);
    EXPECT_EQ(i + 1, static_cast<unsigned char>(decimal.ptr_[i]));
  }
}

void expect_owned_payloads(const StoredRow &row)
{
  const uintptr_t begin = reinterpret_cast<uintptr_t>(&row);
  const uintptr_t end = begin + row.row_size_;
  for (uint32_t i = 0; i < row.cnt_; ++i) {
    SCOPED_TRACE(i);
    const StoredDatumCell &cell = row.stored_cells()[i];
    if (!cell.is_null()) {
      const uintptr_t payload = reinterpret_cast<uintptr_t>(cell.ptr_);
      EXPECT_GE(payload, begin + sizeof(StoredRow) + row.cnt_ * sizeof(StoredDatumCell));
      EXPECT_LE(payload, end);
      EXPECT_LE(cell.len_, end - payload);
    }
  }
}
} // namespace

TEST(StoredDatumCells, mixed_row_owns_payloads_extra_and_fixed_cell_layout)
{
  MixedInput input;
  std::vector<char> memory;
  StoredRow *row = copy_datums(input.datums_, 4, EXTRA_SIZE, memory);
  ASSERT_NE(nullptr, row);
  EXPECT_EQ(12, sizeof(StoredDatumCell));
  EXPECT_EQ(8, sizeof(StoredRow));
  EXPECT_EQ(reinterpret_cast<const char *>(row->get_extra_payload()),
      row->payload_ + 4 * sizeof(StoredDatumCell));
  const uint32_t expected_size = sizeof(StoredRow) + 4 * sizeof(StoredDatumCell)
      + EXTRA_SIZE + sizeof(input.integer_) + sizeof(input.text_) - 1 + sizeof(input.decimal_);
  EXPECT_EQ(expected_size, row->row_size_);
  EXPECT_NE(input.datums_[0].ptr_, row->stored_cells()[0].ptr_);
  EXPECT_NE(input.datums_[1].ptr_, row->stored_cells()[1].ptr_);
  EXPECT_NE(input.datums_[3].ptr_, row->stored_cells()[3].ptr_);
  input.overwrite();
  expect_mixed(*row);
  expect_owned_payloads(*row);
  expect_extra(*row, EXTRA_SIZE);
  expect_tail(memory);
}

TEST(StoredDatumCells, assign_relocates_to_another_address_and_survives_source_overwrite)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *original = copy_datums(input.datums_, 4, EXTRA_SIZE, source);
  ASSERT_NE(nullptr, original);
  std::vector<char> destination(original->row_size_ + TAIL_SIZE, 0);
  initialize_tail(destination, original->row_size_);
  auto *copy = new (destination.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, copy->assign(original));
  ASSERT_NE(original, copy);
  EXPECT_NE(original->stored_cells()[0].ptr_, copy->stored_cells()[0].ptr_);
  EXPECT_NE(original->stored_cells()[1].ptr_, copy->stored_cells()[1].ptr_);
  EXPECT_NE(original->stored_cells()[3].ptr_, copy->stored_cells()[3].ptr_);
  input.overwrite();
  MEMSET(source.data(), 0xa5, source.size());
  expect_mixed(*copy);
  expect_owned_payloads(*copy);
  expect_extra(*copy, EXTRA_SIZE);
  expect_tail(destination);
}

TEST(StoredDatumCells, shadow_cell_copy_owns_payloads_independently_of_the_source_row)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *original = copy_datums(input.datums_, 4, EXTRA_SIZE, source);
  ASSERT_NE(nullptr, original);
  std::vector<char> destination(original->row_size_ + TAIL_SIZE, 0);
  initialize_tail(destination, original->row_size_);
  auto *copy = new (destination.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, copy->copy_shadow_cells(original->stored_cells(), original->cnt_,
      copy->payload_, original->row_size_ - sizeof(StoredRow), original->row_size_, EXTRA_SIZE));
  initialize_extra(*copy, EXTRA_SIZE);
  input.overwrite();
  MEMSET(source.data(), 0xa9, source.size());
  expect_mixed(*copy);
  expect_owned_payloads(*copy);
  expect_extra(*copy, EXTRA_SIZE);
  expect_tail(destination);
}

TEST(StoredDatumCells, offset_image_restores_at_a_new_base_with_null_and_extra)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *row = copy_datums(input.datums_, 4, EXTRA_SIZE, source);
  ASSERT_NE(nullptr, row);
  row->unswizzling();
  for (uint32_t i = 0; i < row->cnt_; ++i) {
    if (!row->stored_cells()[i].is_null()) {
      const uintptr_t offset = reinterpret_cast<uintptr_t>(row->stored_cells()[i].ptr_);
      EXPECT_GE(offset, sizeof(StoredRow) + row->cnt_ * sizeof(StoredDatumCell) + EXTRA_SIZE);
      EXPECT_LE(offset + row->stored_cells()[i].len_, row->row_size_);
    }
  }
  std::vector<char> moved = source;
  MEMSET(source.data(), 0xa6, source.size());
  input.overwrite();
  auto *restored = reinterpret_cast<StoredRow *>(moved.data());
  restored->swizzling();
  expect_mixed(*restored);
  expect_owned_payloads(*restored);
  expect_extra(*restored, EXTRA_SIZE);
  expect_tail(moved);
}

TEST(StoredDatumCells, copied_pointer_image_encodes_relative_to_the_original_base)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *row = copy_datums(input.datums_, 4, EXTRA_SIZE, source);
  ASSERT_NE(nullptr, row);
  std::vector<char> copied = source;
  auto *image = reinterpret_cast<StoredRow *>(copied.data());
  image->unswizzling(source.data());
  row->unswizzling();
  EXPECT_EQ(source, copied);
  MEMSET(source.data(), 0xa7, source.size());
  image->swizzling();
  expect_mixed(*image);
  expect_owned_payloads(*image);
  expect_extra(*image, EXTRA_SIZE);
  expect_tail(copied);
}

TEST(StoredDatumCells, nulling_first_middle_or_last_short_column_keeps_extra_and_tail_canary)
{
  for (int64_t removed = 0; removed < 3; ++removed) {
    SCOPED_TRACE(removed);
    char payloads[] = {'a', 'b', 'c'};
    ObDatum datums[3];
    for (int64_t i = 0; i < 3; ++i) { datums[i].set_string(payloads + i, 1); }
    std::vector<char> memory;
    StoredRow *row = copy_datums(datums, 3, EXTRA_SIZE, memory);
    ASSERT_NE(nullptr, row);
    const uint32_t original_size = row->row_size_;
    ASSERT_EQ(OB_SUCCESS, row->set_null(removed));
    EXPECT_EQ(original_size - 1, row->row_size_);
    for (int64_t i = 0; i < 3; ++i) {
      SCOPED_TRACE(i);
      const ObDatum value = decoded(*row, i);
      if (i == removed) {
        EXPECT_TRUE(value.is_null());
      } else {
        EXPECT_FALSE(value.is_null());
        EXPECT_EQ(ObString(1, payloads + i), value.get_string());
      }
    }
    expect_owned_payloads(*row);
    expect_extra(*row, EXTRA_SIZE);
    expect_tail(memory);
    ASSERT_EQ(OB_SUCCESS, row->set_null(removed));
    EXPECT_EQ(original_size - 1, row->row_size_);
    expect_extra(*row, EXTRA_SIZE);
    expect_tail(memory);
  }
}

TEST(StoredDatumCells, nulling_mixed_payloads_preserves_remaining_integer_decimal_and_extra)
{
  for (int64_t removed = 0; removed < 4; ++removed) {
    SCOPED_TRACE(removed);
    MixedInput input;
    std::vector<char> memory;
    StoredRow *row = copy_datums(input.datums_, 4, EXTRA_SIZE, memory);
    ASSERT_NE(nullptr, row);
    const uint32_t original_size = row->row_size_;
    const uint32_t removed_size = row->stored_cells()[removed].len_;
    ASSERT_EQ(OB_SUCCESS, row->set_null(removed));
    EXPECT_EQ(original_size - removed_size, row->row_size_);
    EXPECT_TRUE(decoded(*row, removed).is_null());
    if (removed != 0) { EXPECT_EQ(input.integer_, integer_bytes(decoded(*row, 0))); }
    if (removed != 1) { EXPECT_EQ(ObString(input.text_), decoded(*row, 1).get_string()); }
    EXPECT_TRUE(decoded(*row, 2).is_null());
    if (removed != 3) {
      const ObDatum decimal = decoded(*row, 3);
      ASSERT_EQ(16, decimal.len_);
      EXPECT_EQ(0, MEMCMP(input.decimal_, decimal.ptr_, sizeof(input.decimal_)));
    }
    expect_owned_payloads(*row);
    expect_extra(*row, EXTRA_SIZE);
    expect_tail(memory);
  }
}

TEST(StoredDatumCells, invalid_null_column_does_not_modify_the_row_or_tail)
{
  MixedInput input;
  std::vector<char> memory;
  StoredRow *row = copy_datums(input.datums_, 4, EXTRA_SIZE, memory);
  ASSERT_NE(nullptr, row);
  const std::vector<char> before = memory;
  EXPECT_EQ(OB_INVALID_ARGUMENT, row->set_null(-1));
  EXPECT_EQ(before, memory);
  EXPECT_EQ(OB_INVALID_ARGUMENT, row->set_null(4));
  EXPECT_EQ(before, memory);
  expect_mixed(*row);
}

TEST(StoredDatumCells, zero_columns_empty_strings_and_exact_capacity_are_distinct)
{
  ObDatum datums[2];
  datums[0].set_string("", 0);
  datums[1].set_null();
  const uint32_t extras[] = {0, 1, 7, 8, 13, 32};
  for (uint32_t extra : extras) {
    SCOPED_TRACE(extra);
    for (int64_t count = 0; count <= 2; ++count) {
      SCOPED_TRACE(count);
      std::vector<char> memory;
      StoredRow *row = copy_datums(datums, count, extra, memory);
      ASSERT_NE(nullptr, row);
      EXPECT_EQ(count, row->cnt_);
      EXPECT_EQ(sizeof(StoredRow) + count * sizeof(StoredDatumCell) + extra, row->row_size_);
      if (count > 0) {
        EXPECT_FALSE(decoded(*row, 0).is_null());
        EXPECT_EQ(0, decoded(*row, 0).len_);
      }
      if (count > 1) { EXPECT_TRUE(decoded(*row, 1).is_null()); }
      expect_extra(*row, extra);
      expect_tail(memory);
    }
  }
}

TEST(StoredDatumCells, last_row_double_buffer_preserves_the_previous_row_until_its_next_reuse)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *row = copy_datums(input.datums_, 4, EXTRA_SIZE, source);
  ASSERT_NE(nullptr, row);
  ObArenaAllocator owner;
  ObChunkDatumStore::LastStoredRow last(owner);
  ASSERT_EQ(OB_SUCCESS, last.save_store_row(*row));
  StoredRow *first = last.store_row_;
  ASSERT_NE(nullptr, first);
  ASSERT_EQ(OB_SUCCESS, last.save_store_row(*first));
  StoredRow *second = last.store_row_;
  ASSERT_NE(nullptr, second);
  ASSERT_NE(first, second);
  input.overwrite();
  MEMSET(source.data(), 0xa8, source.size());
  expect_mixed(*first);
  expect_mixed(*second);
  expect_extra(*first, EXTRA_SIZE);
  expect_extra(*second, EXTRA_SIZE);
  ASSERT_EQ(OB_SUCCESS, second->set_null(1));
  EXPECT_FALSE(decoded(*first, 1).is_null());
  ASSERT_EQ(OB_SUCCESS, last.save_store_row(*second));
  EXPECT_EQ(first, last.store_row_);
  EXPECT_TRUE(decoded(*last.store_row_, 1).is_null());
  EXPECT_TRUE(decoded(*second, 1).is_null());
  EXPECT_EQ(-3000000001LL, integer_bytes(decoded(*last.store_row_, 0)));
  expect_extra(*last.store_row_, EXTRA_SIZE);
  expect_owned_payloads(*last.store_row_);
  ASSERT_EQ(OB_SUCCESS, last.save_store_row(*last.store_row_));
  EXPECT_EQ(second, last.store_row_);
  EXPECT_TRUE(decoded(*last.store_row_, 1).is_null());
  expect_extra(*last.store_row_, EXTRA_SIZE);
  expect_owned_payloads(*last.store_row_);
}

TEST(StoredDatumCells, typed_codec_offsets_preserve_descriptors_and_copy_payloads)
{
  MixedInput input;
  SqlTypeMeta types[4];
  types[0].set_int();
  types[1].set_varchar();
  types[1].set_collation_type(CS_TYPE_UTF8MB4_BIN);
  types[2].set_decimal_int(5);
  types[3].set_decimal_int(5);
  ObAccuracy accuracy;
  accuracy.set_precision(20);
  accuracy.set_scale(5);
  input.datums_[1].set_has_lob_header();
  uint32_t expected_pack[4];
  for (int64_t i = 0; i < 4; ++i) { expected_pack[i] = input.datums_[i].pack_; }
  StoredDatumCell cells[4];
  char payload[128];
  MEMSET(payload, 0x5a, sizeof(payload));
  int64_t position = 7;
  for (int64_t i = 0; i < 4; ++i) {
    SCOPED_TRACE(i);
    const int64_t before = position;
    DatumCopyPlan plan(types[i], accuracy);
    ASSERT_EQ(OB_SUCCESS, StoredDatumCodec::encode_legacy_copy(input.datums_[i], cells[i],
        plan, payload, sizeof(payload), position, true));
    EXPECT_EQ(expected_pack[i], cells[i].pack_);
    if (!cells[i].is_null()) {
      EXPECT_EQ(before, reinterpret_cast<uintptr_t>(cells[i].ptr_));
      cells[i].ptr_ = payload + before;
      EXPECT_EQ(before + cells[i].len_, position);
    } else {
      EXPECT_EQ(before, position);
    }
  }
  input.overwrite();
  EXPECT_EQ(-3000000001LL, integer_bytes(StoredDatumCodec::decode_legacy_view(cells[0])));
  EXPECT_EQ(ObString("customer row payload"), StoredDatumCodec::decode_legacy_view(cells[1]).get_string());
  EXPECT_TRUE(StoredDatumCodec::decode_legacy_view(cells[1]).has_lob_header());
  EXPECT_TRUE(StoredDatumCodec::decode_legacy_view(cells[2]).is_null());
  ASSERT_EQ(16, cells[3].len_);
  for (int64_t i = 0; i < 16; ++i) {
    EXPECT_EQ(i + 1, static_cast<unsigned char>(cells[3].ptr_[i]));
  }
  for (int64_t i = 0; i < 7; ++i) { EXPECT_EQ(0x5a, static_cast<unsigned char>(payload[i])); }
  for (int64_t i = position; i < static_cast<int64_t>(sizeof(payload)); ++i) {
    EXPECT_EQ(0x5a, static_cast<unsigned char>(payload[i]));
  }
}

TEST(StoredDatumCells, failed_codec_copy_preserves_destination_cursor_and_buffer)
{
  MixedInput input;
  ObDatum marker;
  marker.set_string("unchanged", 9);
  const StoredDatumCell original = StoredDatumCodec::encode_legacy_view(marker);
  SqlTypeMeta type;
  type.set_int();
  const DatumCopyPlan plan(type, ObAccuracy{});
  char buffer[16];
  MEMSET(buffer, 0x5b, sizeof(buffer));
  StoredDatumCell output = original;
  int64_t position = 3;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, StoredDatumCodec::encode_legacy_copy(input.datums_[0], output,
      plan, buffer, 10, position));
  EXPECT_EQ(3, position);
  EXPECT_EQ(0, MEMCMP(&original, &output, sizeof(output)));
  ObDatum invalid = input.datums_[0];
  invalid.ptr_ = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredDatumCodec::encode_legacy_copy(invalid, output,
      plan, buffer, sizeof(buffer), position));
  EXPECT_EQ(3, position);
  EXPECT_EQ(0, MEMCMP(&original, &output, sizeof(output)));
  position = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, StoredDatumCodec::encode_legacy_copy(input.datums_[0], output,
      plan, buffer, sizeof(buffer), position));
  EXPECT_EQ(-1, position);
  EXPECT_EQ(0, MEMCMP(&original, &output, sizeof(output)));
  for (char value : buffer) { EXPECT_EQ(0x5b, static_cast<unsigned char>(value)); }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
