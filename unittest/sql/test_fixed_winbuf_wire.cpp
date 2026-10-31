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

#include "sql/engine/px/datahub/components/ob_dh_winbuf.h"
#include "sql/engine/px/ob_dfo.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using Row = ObChunkDatumStore::StoredRow;
constexpr int64_t PAYLOAD_SIZE = 95;
constexpr int64_t ROW_SIZE = 103;
constexpr int64_t WIRE_PAYLOAD = 22;
constexpr int64_t INTEGER = INT64_C(0x0102030405060708);
constexpr uint32_t FLAGGED_PACK = UINT32_C(0x60000010);

// The historical little-endian payload is five 12B cells, 8B extra, then
// int64/string/16B coefficient bytes. Build its expected bytes independently
// of StoredDatumCodec and the message serializer, including the unused NULL
// address and the empty string's one-past-end pointer.
void append_le(std::vector<char> &bytes, uint64_t value, int count)
{
  for (int i = 0; i < count; ++i) { bytes.push_back(char(value >> (8 * i))); }
}

void set_le(std::vector<char> &bytes, int64_t position, uint64_t value, int count)
{
  for (int i = 0; i < count; ++i) { bytes[position + i] = char(value >> (8 * i)); }
}

void set_unis_length(std::vector<char> &bytes, int64_t position, uint64_t length)
{
  for (int i = 0; i < 5; ++i) {
    bytes[position + i] = char((length & 0x7f) | (i == 4 ? 0 : 0x80));
    length >>= 7;
  }
}

std::vector<char> historical_wire(uint32_t trailing = 0)
{
  std::vector<char> result = {
    1, 0, 0, 0, 0, 0, // Piece version and five-byte body length.
    1, char(0x85), char(0x80), char(0x80), char(0x80), 0,
    7, 11, 13, 17, 19, // Base op/source/thread/piece-count/target fields.
    0, 1, 5, char(ROW_SIZE + trailing), char(PAYLOAD_SIZE + trailing)
  };
  set_unis_length(result, 1, 111 + trailing);
  const uint64_t offsets[] = {68, 76, 7, 79, 95};
  const uint32_t packs[] = {8, 3, UINT32_C(0x80000000), FLAGGED_PACK, 0};
  for (int i = 0; i < 5; ++i) {
    append_le(result, offsets[i], 8);
    append_le(result, packs[i], 4);
  }
  const char extra[] = {'E', 'X', 'T', 'R', 'A', '1', '2', '3'};
  result.insert(result.end(), extra, extra + sizeof(extra));
  append_le(result, INTEGER, 8);
  result.push_back('a'); result.push_back('\0'); result.push_back('b');
  for (int i = 0; i < 16; ++i) { result.push_back(char(0x30 + i)); }
  for (uint32_t i = 0; i < trailing; ++i) { result.push_back(char(0x70 + i)); }
  return result;
}

struct OwnedRow
{
  explicit OwnedRow(uint32_t trailing = 0) : memory_(ROW_SIZE + trailing, 0)
  {
    row_ = new (memory_.data()) Row;
    row_->cnt_ = 5;
    row_->row_size_ = memory_.size();
    const uint64_t offsets[] = {68, 76, 7, 79, 95};
    const uint32_t packs[] = {8, 3, UINT32_C(0x80000000), FLAGGED_PACK, 0};
    for (int i = 0; i < 5; ++i) {
      auto &cell = *new (&row_->stored_cells()[i]) StoredDatumCell;
      cell.ptr_ = row_->payload_ + offsets[i];
      cell.pack_ = packs[i];
    }
    MEMCPY(row_->get_extra_payload(), "EXTRA123", 8);
    const int64_t value = INTEGER;
    MEMCPY(row_->payload_ + 68, &value, sizeof(value));
    MEMCPY(row_->payload_ + 76, "a\0b", 3);
    for (int i = 0; i < 16; ++i) { row_->payload_[79 + i] = char(0x30 + i); }
    for (uint32_t i = 0; i < trailing; ++i) { row_->payload_[95 + i] = char(0x70 + i); }
  }
  void bind(ObWinbufPieceMsg &piece)
  {
    piece.op_id_ = 7; piece.source_dfo_id_ = 11; piece.thread_id_ = 13;
    piece.piece_count_ = 17; piece.target_dfo_id_ = 19;
    piece.is_end_ = false; piece.is_datum_ = true; piece.col_count_ = 5;
    piece.datum_row_ = row_; piece.row_size_ = memory_.size();
    piece.payload_len_ = memory_.size() - sizeof(Row);
  }
  void poison() { MEMSET(memory_.data(), 0xa5, memory_.size()); }
  std::vector<char> memory_;
  Row *row_;
};

void expect_row(const Row &row, uint32_t trailing = 0)
{
  ASSERT_EQ(5, row.cnt_);
  EXPECT_EQ(ROW_SIZE + trailing, row.row_size_);
  const auto *cells = row.stored_cells();
  int64_t number = 0;
  ASSERT_EQ(8, cells[0].len_);
  MEMCPY(&number, cells[0].ptr_, sizeof(number));
  EXPECT_EQ(INTEGER, number);
  ASSERT_EQ(3, cells[1].len_); EXPECT_EQ(0, MEMCMP("a\0b", cells[1].ptr_, 3));
  EXPECT_TRUE(cells[2].is_null()); EXPECT_EQ(UINT32_C(0x80000000), cells[2].pack_);
  EXPECT_EQ(FLAGGED_PACK, cells[3].pack_);
  for (int i = 0; i < 16; ++i) { EXPECT_EQ(char(0x30 + i), cells[3].ptr_[i]); }
  EXPECT_FALSE(cells[4].is_null()); EXPECT_EQ(0, cells[4].len_);
  EXPECT_EQ(0, MEMCMP("EXTRA123", row.get_extra_payload(), 8));
  const uintptr_t base = reinterpret_cast<uintptr_t>(row.payload_);
  for (int i : {0, 1, 3, 4}) {
    const uintptr_t address = reinterpret_cast<uintptr_t>(cells[i].ptr_);
    ASSERT_GE(address, base);
    ASSERT_LE(address - base, PAYLOAD_SIZE + trailing);
    EXPECT_LE(cells[i].len_, PAYLOAD_SIZE + trailing - (address - base));
  }
  for (uint32_t i = 0; i < trailing; ++i) { EXPECT_EQ(char(0x70 + i), row.payload_[95 + i]); }
}

void expect_single_row(ObWinbufWholeMsg &whole)
{
  ObChunkDatumStore::Iterator iterator;
  ASSERT_EQ(OB_SUCCESS, whole.datum_store_.begin(iterator));
  const Row *row = nullptr;
  ASSERT_EQ(OB_SUCCESS, iterator.get_next_row(row));
  ASSERT_NE(nullptr, row);
  expect_row(*row);
  EXPECT_EQ(OB_ITER_END, iterator.get_next_row(row));
}

int fill_whole(ObWinbufWholeMsg &whole, const Row &row)
{
  int ret = OB_SUCCESS;
  whole.op_id_ = 7; whole.is_datum_ = true; whole.is_empty_ = false;
  if (OB_FAIL(whole.datum_store_.init(0, ObCtxIds::DEFAULT_CTX_ID, "FixedWinbuf", false, 8))) {
  } else if (OB_FAIL(whole.datum_store_.add_row(row))) {
  }
  return ret;
}
} // namespace

TEST(FixedWinbufWire, piece_serializer_matches_historical_bytes_and_preserves_source)
{
  OwnedRow source;
  ObWinbufPieceMsg piece; source.bind(piece);
  const auto original = source.memory_;
  const auto expected = historical_wire();
  ASSERT_EQ(expected.size(), piece.get_serialize_size());
  std::vector<char> wire(expected.size() + 16, char(0x5a));
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, piece.serialize(wire.data(), expected.size(), position));
  EXPECT_EQ(expected.size(), position);
  EXPECT_EQ(0, MEMCMP(expected.data(), wire.data(), expected.size()));
  EXPECT_EQ(original, source.memory_);
  for (size_t i = expected.size(); i < wire.size(); ++i) { EXPECT_EQ(char(0x5a), wire[i]); }
}

TEST(FixedWinbufWire, piece_deserializes_historical_bytes_at_another_address_and_owns_payload)
{
  auto wire = historical_wire();
  ObWinbufPieceMsg decoded;
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
  ASSERT_EQ(wire.size(), position);
  EXPECT_EQ(7, decoded.op_id_); EXPECT_EQ(11, decoded.source_dfo_id_);
  EXPECT_EQ(13, decoded.thread_id_); EXPECT_EQ(17, decoded.piece_count_);
  EXPECT_EQ(19, decoded.target_dfo_id_); EXPECT_EQ(5, decoded.col_count_);
  EXPECT_TRUE(decoded.is_datum_); EXPECT_FALSE(decoded.is_end_);
  EXPECT_EQ(ROW_SIZE, decoded.row_size_); EXPECT_EQ(PAYLOAD_SIZE, decoded.payload_len_);
  ASSERT_NE(nullptr, decoded.datum_row_);
  MEMSET(wire.data(), 0xcc, wire.size());
  expect_row(*decoded.datum_row_);
}

TEST(FixedWinbufWire, piece_roundtrip_keeps_existing_sender_trailing_four_bytes)
{
  // c2's window sender publishes four trailing bytes. They belong to the
  // declared Chunk payload here; only the separate window fix removes them.
  OwnedRow source(4);
  ObWinbufPieceMsg piece; source.bind(piece);
  const auto expected = historical_wire(4);
  std::vector<char> wire(piece.get_serialize_size());
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, piece.serialize(wire.data(), wire.size(), position));
  EXPECT_EQ(expected, wire);
  source.poison();
  ObWinbufPieceMsg decoded;
  position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
  MEMSET(wire.data(), 0xcc, wire.size());
  ASSERT_NE(nullptr, decoded.datum_row_);
  expect_row(*decoded.datum_row_, 4);
}

TEST(FixedWinbufWire, null_unused_address_bits_survive_unsigned_relocation)
{
  auto wire = historical_wire();
  set_le(wire, WIRE_PAYLOAD + 2 * 12, UINT64_MAX - 2, 8);
  const auto expected = wire;
  ObWinbufPieceMsg decoded;
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
  ASSERT_NE(nullptr, decoded.datum_row_);
  expect_row(*decoded.datum_row_);
  std::vector<char> encoded(decoded.get_serialize_size());
  position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.serialize(encoded.data(), encoded.size(), position));
  EXPECT_EQ(expected, encoded);
}

TEST(FixedWinbufWire, serialize_capacity_and_shape_errors_do_not_copy_payload)
{
  OwnedRow source;
  ObWinbufPieceMsg piece; source.bind(piece);
  const auto original = source.memory_;
  std::vector<char> wire(piece.get_serialize_size() + 16, char(0x5a));
  int64_t position = 0;
  EXPECT_EQ(OB_SIZE_OVERFLOW, piece.serialize(wire.data(), piece.get_serialize_size() - 1, position));
  for (size_t i = WIRE_PAYLOAD; i < wire.size(); ++i) { EXPECT_EQ(char(0x5a), wire[i]); }
  EXPECT_EQ(original, source.memory_);
  const char *empty_pointer = source.row_->stored_cells()[4].ptr_;
  for (int bad = 0; bad < 4; ++bad) {
    source.bind(piece);
    if (bad == 0) { --piece.payload_len_; }
    if (bad == 1) { piece.col_count_ = 20; }
    if (bad == 2) { piece.datum_row_ = nullptr; }
    if (bad == 3) {
      source.row_->stored_cells()[4].ptr_ = reinterpret_cast<const char *>(
          reinterpret_cast<uintptr_t>(source.row_->payload_) + PAYLOAD_SIZE + 1);
    }
    std::fill(wire.begin(), wire.end(), char(0x5a));
    position = 0;
    EXPECT_EQ(OB_ERR_UNEXPECTED, piece.serialize(wire.data(), wire.size(), position));
    for (size_t i = WIRE_PAYLOAD; i < wire.size(); ++i) { EXPECT_EQ(char(0x5a), wire[i]); }
    source.row_->stored_cells()[4].ptr_ = empty_pointer;
  }
  EXPECT_EQ(original, source.memory_);
}

TEST(FixedWinbufWire, deserialize_payload_errors_leave_previous_row_and_metadata_published)
{
  ObChunkDatumStore::StoredRow sentinel;
  for (int bad = 0; bad < 5; ++bad) {
    auto wire = historical_wire();
    if (bad == 0) { wire.pop_back(); set_unis_length(wire, 1, wire.size() - 6); }
    if (bad == 1) { wire[19] = 100; } // Cell array does not fit.
    if (bad == 2) { wire[20] = ROW_SIZE - 1; } // Header/payload disagreement.
    if (bad == 3) { set_le(wire, WIRE_PAYLOAD, UINT64_MAX - 7, 8); }
    if (bad == 4) { set_le(wire, WIRE_PAYLOAD + 8, UINT32_C(0x1fffffff), 4); }
    ObWinbufPieceMsg decoded;
    decoded.datum_row_ = &sentinel; decoded.row_size_ = 7; decoded.payload_len_ = 9;
    decoded.col_count_ = 11; decoded.is_end_ = true; decoded.is_datum_ = false;
    int64_t position = 0;
    EXPECT_NE(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
    EXPECT_EQ(&sentinel, decoded.datum_row_); EXPECT_EQ(7, decoded.row_size_);
    EXPECT_EQ(9, decoded.payload_len_); EXPECT_EQ(11, decoded.col_count_);
    EXPECT_TRUE(decoded.is_end_); EXPECT_FALSE(decoded.is_datum_);
  }
}

TEST(FixedWinbufWire, empty_piece_and_empty_whole_keep_control_message_behavior)
{
  for (bool datum : {false, true}) {
    ObWinbufPieceMsg source; source.is_end_ = true; source.is_datum_ = datum;
    std::vector<char> wire(source.get_serialize_size());
    int64_t position = 0;
    ASSERT_EQ(OB_SUCCESS, source.serialize(wire.data(), wire.size(), position));
    ObWinbufPieceMsg decoded;
    position = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
    EXPECT_TRUE(decoded.is_end_); EXPECT_EQ(datum, decoded.is_datum_);
    EXPECT_EQ(0, decoded.col_count_); EXPECT_EQ(nullptr, decoded.datum_row_);
  }
  ObWinbufWholeMsg source; source.is_datum_ = true;
  std::vector<char> wire(source.get_serialize_size());
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(wire.data(), wire.size(), position));
  ObWinbufWholeMsg decoded;
  position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
  EXPECT_TRUE(decoded.is_empty_); EXPECT_TRUE(decoded.is_datum_);
  EXPECT_EQ(0, decoded.datum_store_.get_row_cnt());
}

TEST(FixedWinbufWire, whole_roundtrip_and_assign_own_cells_after_source_reset)
{
  OwnedRow source;
  ObWinbufWholeMsg whole;
  ASSERT_EQ(OB_SUCCESS, fill_whole(whole, *source.row_));
  source.poison();
  expect_single_row(whole);
  std::vector<char> wire(whole.get_serialize_size());
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, whole.serialize(wire.data(), wire.size(), position));
  ObWinbufWholeMsg decoded;
  position = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), position));
  whole.reset(); MEMSET(wire.data(), 0xcc, wire.size());
  expect_single_row(decoded);
  ObWinbufWholeMsg assigned;
  ASSERT_EQ(OB_SUCCESS, assigned.assign(decoded));
  decoded.reset();
  expect_single_row(assigned);
}

TEST(FixedWinbufWire, listener_copies_piece_before_its_owner_is_released)
{
  auto wire = historical_wire();
  ObWinbufPieceMsg piece;
  int64_t position = 0;
  ASSERT_EQ(OB_SUCCESS, piece.deserialize(wire.data(), wire.size(), position));
  ObWinbufPieceMsgCtx context(7, 2, INT64_MAX);
  ObSEArray<ObPxSqcMeta, 1> sqcs;
  // One of two pieces: invoke the real listener/copy without reaching send.
  ASSERT_EQ(OB_SUCCESS, ObWinbufPieceMsgListener::on_message(context, sqcs, piece));
  EXPECT_EQ(1, context.received_);
  piece.reset(); MEMSET(wire.data(), 0xcc, wire.size());
  expect_single_row(context.whole_msg_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
