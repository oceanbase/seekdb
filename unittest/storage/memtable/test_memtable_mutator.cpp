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

#include "storage/memtable/ob_memtable_mutator.h"
#include <cstddef>
#include <cstdint>
#include <gtest/gtest.h>
#include "lib/allocator/page_arena.h"
#include "lib/checksum/ob_crc64.h"

namespace oceanbase
{
namespace memtable
{
using namespace common;
using namespace serialization;

// The minimum metadata prefix is MutatorMetaV1 (28 bytes). Keep the fixture
// independent of the private fields in ObMemtableMutatorMeta.
struct MutatorMetaV1Wire
{
  uint32_t magic_;
  uint32_t meta_crc_;
  int16_t meta_size_;
  uint8_t version_;
  uint8_t flags_;
  uint32_t data_crc_;
  uint32_t data_size_;
  uint32_t row_count_;
  uint32_t unused_;
};

static_assert(sizeof(MutatorMetaV1Wire) == 28, "unexpected V1 metadata size");
static_assert(offsetof(MutatorMetaV1Wire, meta_size_) == 8, "unexpected CRC start");

class TestMemtableMutator : public ::testing::Test
{
public:
  static constexpr int64_t BUFFER_SIZE = 2048;
  static constexpr int64_t V1_META_SIZE = sizeof(MutatorMetaV1Wire);

  void SetUp() override { MEMSET(buf_, 0, sizeof(buf_)); }

  int build_mutator(const int64_t start, int64_t &end, const bool legacy_row = false,
      const int64_t key_value = 42, const char *new_payload = "new", const char *old_payload = "old")
  {
    int ret = OB_SUCCESS;
    int64_t capacity = sizeof(buf_);
    ObMemtableMutatorMeta meta;
    const int64_t body_start = start + meta.get_serialize_size();
    int64_t body_pos = body_start;
    int64_t meta_pos = start;
    ObArenaAllocator key_allocator;
    DatumValue key_cell;
    if (OB_FAIL(key_cell.set_int(key_value, key_allocator))) {
      return ret;
    }
    ObStoreRowkey key;
    ObRowData new_row;
    ObRowData old_row;
    new_row.set(new_payload, STRLEN(new_payload));
    old_row.set(old_payload, STRLEN(old_payload));
    ObMutatorRowHeader header;
    header.tablet_id_ = ObTabletID(ObTabletID::MIN_USER_TABLET_ID + 1);
    if (OB_FAIL(key.assign(&key_cell, 1))) {
    } else if (OB_FAIL(header.serialize(buf_, capacity, body_pos))) {
    } else if (legacy_row) {
      // The reader supports rows ending after old_row_, before the optional
      // checksum/version/flags/sequence/column-count suffix.
      const int64_t row_start = body_pos;
      int64_t row_pos = row_start + encoded_length_i32(0);
      if (OB_FAIL(encode_vi64(buf_, capacity, row_pos, 5001))
          || OB_FAIL(key.serialize(buf_, capacity, row_pos))
          || OB_FAIL(encode_vi64(buf_, capacity, row_pos, 123))
          || OB_FAIL(encode_i8(buf_, capacity, row_pos, blocksstable::ObDmlFlag::DF_UPDATE))
          || OB_FAIL(encode_vi32(buf_, capacity, row_pos, 2))
          || OB_FAIL(new_row.serialize(buf_, capacity, row_pos))
          || OB_FAIL(old_row.serialize(buf_, capacity, row_pos))
          || OB_FAIL(encode_i32(buf_, capacity, body_pos, row_pos - row_start))) {
      } else {
        body_pos = row_pos;
      }
    } else {
      ObMemtableMutatorRow row(5001, key, 123, new_row, old_row,
          blocksstable::ObDmlFlag::DF_UPDATE, 2, 456, 789, 0, transaction::ObTxSEQ(7, 0), 2);
      ret = row.serialize(buf_, capacity, body_pos);
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(meta.inc_row_count())
          || OB_FAIL(meta.fill_header(buf_ + body_start, body_pos - body_start))
          || OB_FAIL(meta.serialize(buf_, capacity, meta_pos))) {
      } else {
        end = body_pos;
      }
    }
    return ret;
  }

  void refresh_meta_crc(char *buf, const int64_t meta_size)
  {
    const int64_t skip = offsetof(MutatorMetaV1Wire, meta_size_);
    ASSERT_GE(meta_size, skip);
    ASSERT_LE(meta_size, BUFFER_SIZE);
    const uint32_t crc = static_cast<uint32_t>(ob_crc64(buf + skip, meta_size - skip));
    MEMCPY(buf + offsetof(MutatorMetaV1Wire, meta_crc_), &crc, sizeof(crc));
  }

  void make_v1_header(const uint32_t data_size)
  {
    const MutatorMetaV1Wire header = {0x6174756d, 0, 28, 0, 0, 0, data_size, 1, 0};
    MEMCPY(buf_, &header, sizeof(header));
    refresh_meta_crc(buf_, sizeof(header));
  }

  void set_meta_size(const int16_t meta_size)
  {
    MEMCPY(buf_ + offsetof(MutatorMetaV1Wire, meta_size_), &meta_size, sizeof(meta_size));
  }

  void expect_one_row(ObMemtableMutatorIterator &iterator, const bool legacy = false,
      const int64_t key_value = 42, const char *new_payload = "new", const char *old_payload = "old")
  {
    ASSERT_EQ(OB_SUCCESS, iterator.iterate_next_row());
    const ObMemtableMutatorRow &row = iterator.get_mutator_row();
    EXPECT_EQ(5001, row.table_id_);
    EXPECT_EQ(123, row.table_version_);
    ASSERT_EQ(1, row.rowkey_.get_value_count());
    EXPECT_EQ(key_value, row.rowkey_.get_values()[0].get_int());
    EXPECT_EQ(blocksstable::ObDmlFlag::DF_UPDATE, row.dml_flag_);
    EXPECT_EQ(2, row.update_seq_);
    ASSERT_EQ(STRLEN(new_payload), row.new_row_.size_);
    ASSERT_EQ(STRLEN(old_payload), row.old_row_.size_);
    EXPECT_EQ(0, MEMCMP(new_payload, row.new_row_.data_, row.new_row_.size_));
    EXPECT_EQ(0, MEMCMP(old_payload, row.old_row_.data_, row.old_row_.size_));
    EXPECT_EQ(legacy ? 0 : 456, row.acc_checksum_);
    EXPECT_EQ(legacy ? 0 : 789, row.version_);
    EXPECT_EQ(legacy ? 0 : 7, iterator.get_row_seq_no().get_seq());
    EXPECT_EQ(legacy ? 0 : 2, row.get_column_cnt());
    EXPECT_TRUE(iterator.is_iter_end());
    EXPECT_EQ(OB_ITER_END, iterator.iterate_next_row());
  }

  char buf_[BUFFER_SIZE];
};

TEST_F(TestMemtableMutator, normal_meta_round_trip)
{
  int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(7, end));
  ObMemtableMutatorMeta meta;
  int64_t pos = 7;
  ASSERT_EQ(OB_SUCCESS, meta.deserialize(buf_, end, pos));
  EXPECT_EQ(7 + V1_META_SIZE, pos);
  EXPECT_EQ(1, meta.get_row_count());
  EXPECT_EQ(end - 7, meta.get_total_size());
  EXPECT_EQ(OB_SUCCESS, meta.check_data_integrity(buf_ + pos, end - pos));
}

TEST_F(TestMemtableMutator, normal_iterator_round_trip_with_offset)
{
  int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(9, end));
  int64_t pos = 9;
  ObMemtableMutatorIterator iterator;
  ASSERT_EQ(OB_SUCCESS, iterator.deserialize(buf_, end + 3, pos));
  EXPECT_EQ(end, pos);
  EXPECT_EQ(1, iterator.get_meta().get_row_count());
  expect_one_row(iterator);
}

TEST_F(TestMemtableMutator, normal_consecutive_mutators)
{
  int64_t first_end = 0;
  int64_t second_end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(5, first_end));
  ASSERT_EQ(OB_SUCCESS, build_mutator(first_end, second_end, false, 84, "second-new", "second-old"));
  int64_t pos = 5;
  ObMemtableMutatorIterator first;
  ASSERT_EQ(OB_SUCCESS, first.deserialize(buf_, second_end, pos));
  EXPECT_EQ(first_end, pos);
  expect_one_row(first);
  ObMemtableMutatorIterator second;
  ASSERT_EQ(OB_SUCCESS, second.deserialize(buf_, second_end, pos));
  EXPECT_EQ(second_end, pos);
  expect_one_row(second, false, 84, "second-new", "second-old");
}

TEST_F(TestMemtableMutator, reset_and_retry_after_invalid_input)
{
  int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(0, end));
  ASSERT_GT(end, V1_META_SIZE);
  int64_t pos = 0;
  ObMemtableMutatorIterator iterator;
  ASSERT_EQ(OB_SUCCESS, iterator.deserialize(buf_, end, pos));
  EXPECT_EQ(end, pos);
  expect_one_row(iterator);

  iterator.reset();
  EXPECT_EQ(OB_NOT_INIT, iterator.iterate_next_row());
  pos = 0;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, iterator.deserialize(buf_, end - 1, pos));
  EXPECT_EQ(0, pos);
  EXPECT_EQ(OB_NOT_INIT, iterator.iterate_next_row());

  iterator.reset();
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, iterator.deserialize(buf_, end, pos));
  EXPECT_EQ(end, pos);
  expect_one_row(iterator);
}

TEST_F(TestMemtableMutator, compatible_v1_meta_prefix)
{
  make_v1_header(17);
  ObMemtableMutatorMeta meta;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.deserialize(buf_, V1_META_SIZE, pos));
  EXPECT_EQ(V1_META_SIZE, pos);
  EXPECT_EQ(17, meta.get_data_size());
  EXPECT_EQ(1, meta.get_row_count());
}

TEST_F(TestMemtableMutator, compatible_extended_meta_header)
{
  int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(0, end));
  MEMMOVE(buf_ + V1_META_SIZE + 4, buf_ + V1_META_SIZE, end - V1_META_SIZE);
  MEMSET(buf_ + V1_META_SIZE, 0x5a, 4);
  set_meta_size(V1_META_SIZE + 4);
  refresh_meta_crc(buf_, V1_META_SIZE + 4);
  end += 4;
  int64_t pos = 0;
  ObMemtableMutatorIterator iterator;
  ASSERT_EQ(OB_SUCCESS, iterator.deserialize(buf_, end, pos));
  EXPECT_EQ(end, pos);
  EXPECT_EQ(V1_META_SIZE + 4, iterator.get_meta().get_meta_size());
  expect_one_row(iterator);
}

TEST_F(TestMemtableMutator, compatible_row_without_optional_suffix)
{
  int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(0, end, true));
  int64_t pos = 0;
  ObMemtableMutatorIterator iterator;
  ASSERT_EQ(OB_SUCCESS, iterator.deserialize(buf_, end, pos));
  EXPECT_EQ(end, pos);
  expect_one_row(iterator, true);
}

TEST_F(TestMemtableMutator, invalid_arguments)
{
  make_v1_header(0);
  const int64_t bad_positions[] = {-1, INT64_MIN, INT64_MAX, V1_META_SIZE, V1_META_SIZE + 1};
  for (const int64_t bad_pos : bad_positions) {
    ObMemtableMutatorMeta meta;
    ObMemtableMutatorIterator iterator;
    int64_t pos = bad_pos;
    EXPECT_EQ(OB_INVALID_ARGUMENT, meta.deserialize(buf_, V1_META_SIZE, pos));
    EXPECT_EQ(bad_pos, pos);
    EXPECT_EQ(OB_INVALID_ARGUMENT, iterator.deserialize(buf_, V1_META_SIZE, pos));
    EXPECT_EQ(bad_pos, pos);
  }
  ObMemtableMutatorMeta meta;
  ObMemtableMutatorIterator iterator;
  int64_t pos = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.deserialize(nullptr, V1_META_SIZE, pos));
  EXPECT_EQ(OB_INVALID_ARGUMENT, iterator.deserialize(nullptr, V1_META_SIZE, pos));
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.deserialize(buf_, -1, pos));
  EXPECT_EQ(OB_INVALID_ARGUMENT, iterator.deserialize(buf_, -1, pos));
  EXPECT_EQ(0, pos);
}

TEST_F(TestMemtableMutator, truncated_minimum_header)
{
  make_v1_header(0);
  for (int64_t length = 0; length < V1_META_SIZE; ++length) {
    ObMemtableMutatorMeta meta;
    ObMemtableMutatorIterator iterator;
    int64_t pos = 0;
    EXPECT_EQ(OB_INVALID_ARGUMENT, meta.deserialize(buf_, length, pos));
    EXPECT_EQ(OB_INVALID_ARGUMENT, iterator.deserialize(buf_, length, pos));
    EXPECT_EQ(0, pos);
  }
}

TEST_F(TestMemtableMutator, invalid_meta_size)
{
  const int16_t bad_sizes[] = {INT16_MIN, -1, 0, 7, 27};
  for (const int16_t size : bad_sizes) {
    make_v1_header(0);
    set_meta_size(size);
    ObMemtableMutatorMeta meta;
    int64_t pos = 0;
    EXPECT_EQ(OB_INVALID_LOG, meta.deserialize(buf_, V1_META_SIZE, pos));
    EXPECT_EQ(0, pos);
  }
}

TEST_F(TestMemtableMutator, short_meta_with_valid_crc)
{
  make_v1_header(0);
  const int16_t short_size = V1_META_SIZE - 1;
  set_meta_size(short_size);
  refresh_meta_crc(buf_, short_size);
  MutatorMetaV1Wire header;
  MEMCPY(&header, buf_, sizeof(header));
  const int64_t skip = offsetof(MutatorMetaV1Wire, meta_size_);
  ASSERT_EQ(short_size, header.meta_size_);
  ASSERT_EQ(header.meta_crc_, static_cast<uint32_t>(ob_crc64(buf_ + skip, short_size - skip)));

  ObMemtableMutatorMeta meta;
  int64_t pos = 0;
  EXPECT_EQ(OB_INVALID_LOG, meta.deserialize(buf_, V1_META_SIZE, pos));
  EXPECT_EQ(0, pos);
  ObMemtableMutatorIterator iterator;
  EXPECT_EQ(OB_INVALID_LOG, iterator.deserialize(buf_, V1_META_SIZE, pos));
  EXPECT_EQ(0, pos);
}

TEST_F(TestMemtableMutator, declared_meta_size_checked_before_crc)
{
  const int16_t declared_sizes[] = {32, INT16_MAX};
  const int64_t starts[] = {0, 7};
  for (const int16_t size : declared_sizes) {
    make_v1_header(0);
    set_meta_size(size);
    // The declared extension is absent and the CRC is deliberately unchanged.
    // Length rejection must happen before the CRC can read that extension.
    for (const int64_t start : starts) {
      char header[V1_META_SIZE + 7] = {};
      MEMCPY(header + start, buf_, V1_META_SIZE);
      ObMemtableMutatorMeta meta;
      ObMemtableMutatorIterator iterator;
      int64_t pos = start;
      EXPECT_EQ(OB_BUF_NOT_ENOUGH, meta.deserialize(header, start + V1_META_SIZE, pos));
      EXPECT_EQ(OB_BUF_NOT_ENOUGH, iterator.deserialize(header, start + V1_META_SIZE, pos));
      EXPECT_EQ(start, pos);
    }
  }
}

TEST_F(TestMemtableMutator, invalid_magic_and_crc)
{
  make_v1_header(0);
  buf_[0] ^= 1;
  ObMemtableMutatorMeta meta;
  int64_t pos = 0;
  EXPECT_EQ(OB_INVALID_LOG, meta.deserialize(buf_, V1_META_SIZE, pos));
  make_v1_header(0);
  buf_[offsetof(MutatorMetaV1Wire, meta_crc_)] ^= 1;
  EXPECT_EQ(OB_INVALID_LOG, meta.deserialize(buf_, V1_META_SIZE, pos));
  EXPECT_EQ(0, pos);
}

TEST_F(TestMemtableMutator, truncated_body_does_not_advance_position)
{
  int64_t end = 0;
  ASSERT_EQ(OB_SUCCESS, build_mutator(7, end));
  for (int64_t length = 7 + V1_META_SIZE; length < end; ++length) {
    ObMemtableMutatorIterator iterator;
    int64_t pos = 7;
    EXPECT_EQ(OB_BUF_NOT_ENOUGH, iterator.deserialize(buf_, length, pos));
    EXPECT_EQ(7, pos);
  }
}

TEST_F(TestMemtableMutator, large_data_size_does_not_wrap_total)
{
  make_v1_header(UINT32_MAX);
  ObMemtableMutatorMeta meta;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.deserialize(buf_, V1_META_SIZE, pos));
  EXPECT_EQ(V1_META_SIZE + static_cast<int64_t>(UINT32_MAX), meta.get_total_size());
  ObMemtableMutatorIterator iterator;
  pos = 0;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, iterator.deserialize(buf_, V1_META_SIZE, pos));
  EXPECT_EQ(0, pos);
}

} // namespace memtable
} // namespace oceanbase

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
