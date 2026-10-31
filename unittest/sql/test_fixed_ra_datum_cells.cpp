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

#include "sql/engine/basic/ob_ra_datum_store.h"
#include "sql/engine/ob_exec_context.h"
#include "data_plane/tmp_file/ob_tmp_file.h"
#include "lib/charset/ob_charset.h"
#include "lib/worker.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using StoredRow = ObRADatumStore::StoredRow;
using Block = ObRADatumStore::Block;
constexpr uint32_t EXTRA_SIZE = 13;
constexpr int64_t GUARD_SIZE = 16;
static_assert(sizeof(StoredRow) == 8, "Keep the RA readable/count and row-size header");
static_assert(sizeof(StoredDatumCell) == 12, "RA spill cells use the fixed format");

void fill_guard(std::vector<char> &bytes, int64_t body_size, unsigned char seed)
{
  for (int64_t i = 0; i < GUARD_SIZE; ++i) { bytes[body_size + i] = static_cast<char>(seed + i); }
}

void expect_guard(const std::vector<char> &bytes, int64_t body_size, unsigned char seed)
{
  ASSERT_EQ(body_size + GUARD_SIZE, bytes.size());
  for (int64_t i = 0; i < GUARD_SIZE; ++i) {
    EXPECT_EQ(static_cast<char>(seed + i), bytes[body_size + i]);
  }
}

struct MixedInput
{
  explicit MixedInput(int64_t value = -3000000001LL, char first = 'a') : integer_(value)
  {
    text_[0] = first;
    for (int i = 0; i < 16; ++i) { opaque_[i] = static_cast<char>(i + 1); }
    ObDatum datum;
    datum.ptr_ = reinterpret_cast<const char *>(&integer_); datum.pack_ = sizeof(integer_);
    EXPECT_EQ(OB_SUCCESS, values_.push_back(datum));
    datum.set_string(text_, sizeof(text_));
    EXPECT_EQ(OB_SUCCESS, values_.push_back(datum));
    datum.set_null();
    EXPECT_EQ(OB_SUCCESS, values_.push_back(datum));
    datum.ptr_ = nullptr; datum.pack_ = 0; // non-NULL empty value
    EXPECT_EQ(OB_SUCCESS, values_.push_back(datum));
    datum.set_string(opaque_, sizeof(opaque_)); datum.flag_ = 2;
    EXPECT_EQ(OB_SUCCESS, values_.push_back(datum));
  }

  void overwrite()
  {
    integer_ = 0;
    MEMSET(text_, 'x', sizeof(text_));
    MEMSET(opaque_, 0, sizeof(opaque_));
    for (int64_t i = 0; i < values_.count(); ++i) { values_.at(i).set_null(); }
  }

  int64_t integer_;
  char text_[3] = {'a', '\0', 'b'};
  char opaque_[16];
  ObSEArray<ObDatum, 5> values_;
};

StoredRow *copy_row(const ObIArray<ObDatum> &values, std::vector<char> &bytes)
{
  int64_t quota = 0;
  EXPECT_EQ(OB_SUCCESS, Block::row_store_size(values, quota, EXTRA_SIZE));
  const int64_t body_size = quota - Block::ROW_INDEX_SIZE;
  bytes.assign(body_size + GUARD_SIZE, 0x39);
  fill_guard(bytes, body_size, 0x51);
  StoredRow *row = new (bytes.data()) StoredRow;
  const int ret = row->copy_datums(values, row->payload_, body_size - sizeof(StoredRow), quota, EXTRA_SIZE);
  EXPECT_EQ(OB_SUCCESS, ret);
  if (ret != OB_SUCCESS) { return nullptr; }
  char *extra = static_cast<char *>(row->get_extra_payload());
  for (uint32_t i = 0; i < EXTRA_SIZE; ++i) { extra[i] = static_cast<char>(0x20 + i); }
  return row;
}

int64_t integer_value(const StoredDatumCell &cell)
{
  int64_t value = 0;
  EXPECT_FALSE(cell.is_null());
  EXPECT_EQ(sizeof(value), cell.len_);
  if (!cell.is_null() && cell.len_ == sizeof(value)) { MEMCPY(&value, cell.ptr_, sizeof(value)); }
  return value;
}

void expect_mixed(const StoredRow &row, int64_t expected = -3000000001LL, char first = 'a')
{
  ASSERT_TRUE(row.readable_);
  ASSERT_EQ(5, row.cnt_);
  const StoredDatumCell *cells = row.stored_cells();
  EXPECT_EQ(expected, integer_value(cells[0]));
  const char text[] = {first, '\0', 'b'};
  ASSERT_EQ(sizeof(text), cells[1].len_);
  EXPECT_EQ(0, MEMCMP(text, cells[1].ptr_, sizeof(text)));
  EXPECT_TRUE(cells[2].is_null());
  EXPECT_FALSE(cells[3].is_null());
  EXPECT_EQ(0, cells[3].len_);
  ASSERT_EQ(16, cells[4].len_);
  EXPECT_EQ(2, cells[4].flag_);
  for (int i = 0; i < 16; ++i) { EXPECT_EQ(i + 1, static_cast<unsigned char>(cells[4].ptr_[i])); }
  EXPECT_EQ(sizeof(StoredRow) + 5 * sizeof(StoredDatumCell) + EXTRA_SIZE
      + sizeof(int64_t) + sizeof(text) + 16 + Block::ROW_INDEX_SIZE, row.row_size_);
  const char *extra = static_cast<const char *>(row.get_extra_payload());
  for (uint32_t i = 0; i < EXTRA_SIZE; ++i) { EXPECT_EQ(static_cast<char>(0x20 + i), extra[i]); }
  const uintptr_t payload = reinterpret_cast<uintptr_t>(extra) + EXTRA_SIZE;
  const uintptr_t end = reinterpret_cast<uintptr_t>(&row) + row.body_size();
  for (int64_t i = 0; i < row.cnt_; ++i) {
    if (!cells[i].is_null()) {
      const uintptr_t pointer = reinterpret_cast<uintptr_t>(cells[i].ptr_);
      EXPECT_GE(pointer, payload);
      EXPECT_LE(pointer + cells[i].len_, end);
    }
  }
}

struct ExprFrame
{
  ExprFrame() : arena_(), exec_(arena_), eval_(exec_), bytes_{}, frames_{bytes_.data()}, exprs_{}
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    eval_.set_max_batch_size(2);
    for (int64_t i = 0; i < 5; ++i) {
      ObExpr &expr = exprs_[i];
      expr.frame_idx_ = 0;
      expr.datum_off_ = i * 512 + 16;
      expr.eval_info_off_ = i * 512 + 96;
      expr.res_buf_off_ = i * 512 + 160;
      expr.res_buf_len_ = 64;
      expr.batch_result_ = true;
      expr.batch_idx_mask_ = UINT64_MAX;
      expr.type_ = T_COLUMN_REF;
      expr.obj_datum_map_ = i == 0 ? OBJ_DATUM_8BYTE_DATA : OBJ_DATUM_STRING;
      if (i == 0) { expr.obj_meta_.set_int(); } else { expr.obj_meta_.set_varchar(); }
      expr.datum_meta_.type_ = i == 0 ? ObIntType : ObVarcharType;
      expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
      EXPECT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
      for (int j = 0; j < 2; ++j) { new (bytes_.data() + expr.datum_off_ + sizeof(Datum) * j) Datum; }
      EXPECT_EQ(OB_SUCCESS, expressions_.push_back(&expr));
    }
  }

  Datum *datums(int64_t index)
  {
    return reinterpret_cast<Datum *>(bytes_.data() + exprs_[index].datum_off_);
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  alignas(8) std::array<char, 4096> bytes_;
  char *frames_[1];
  ObExpr exprs_[5];
  ObSEArray<ObExpr *, 5> expressions_;
};

TEST(FixedRaDatumCells, legacy_input_is_owned_with_fixed_cells_extra_null_empty_and_flags)
{
  MixedInput input;
  std::vector<char> memory;
  StoredRow *row = copy_row(input.values_, memory);
  ASSERT_NE(nullptr, row);
  input.overwrite();
  expect_mixed(*row);
  expect_guard(memory, row->body_size(), 0x51);
  const StoredDatumCell *cells = row->stored_cells();
  const ObDatum integer = StoredDatumCodec::decode_legacy_view(cells[0]);
  const ObDatum null = StoredDatumCodec::decode_legacy_view(cells[2]);
  const ObDatum empty = StoredDatumCodec::decode_legacy_view(cells[3]);
  const ObDatum flagged = StoredDatumCodec::decode_legacy_view(cells[4]);
  EXPECT_EQ(-3000000001LL, integer.get_int());
  EXPECT_TRUE(null.is_null());
  EXPECT_FALSE(empty.is_null()); EXPECT_EQ(0, empty.len_);
  EXPECT_EQ(2, flagged.flag_); EXPECT_EQ(16, flagged.len_);
  EXPECT_EQ(cells[4].ptr_, flagged.ptr_);
  for (int i = 0; i < 16; ++i) { EXPECT_EQ(i + 1, static_cast<unsigned char>(flagged.ptr_[i])); }
}

TEST(FixedRaDatumCells, readable_assign_relocates_only_the_body_without_copying_the_block_index_quota)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *row = copy_row(input.values_, source);
  ASSERT_NE(nullptr, row);
  const int64_t body_size = row->body_size();
  std::vector<char> memory(body_size + GUARD_SIZE, 0x39);
  fill_guard(memory, body_size, 0x71); // different from source: old extra 4B copy is observable
  StoredRow *copy = new (memory.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, copy->assign(row));
  MEMSET(source.data(), 0, body_size);
  input.overwrite();
  expect_mixed(*copy);
  expect_guard(memory, body_size, 0x71);
  const std::vector<char> before = memory;
  ASSERT_EQ(OB_SUCCESS, copy->assign(copy));
  EXPECT_EQ(before, memory);
}

TEST(FixedRaDatumCells, copyable_assign_and_raw_relocation_preserve_offsets_until_readable_again)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *row = copy_row(input.values_, source);
  ASSERT_NE(nullptr, row);
  const int64_t body_size = row->body_size();
  ASSERT_EQ(OB_SUCCESS, row->to_copyable());
  ASSERT_FALSE(row->readable_);
  const std::vector<char> offsets = source;
  ASSERT_EQ(OB_SUCCESS, row->to_copyable());
  EXPECT_EQ(offsets, source);
  std::vector<char> raw = source;
  StoredRow *raw_row = reinterpret_cast<StoredRow *>(raw.data());
  ASSERT_EQ(OB_SUCCESS, raw_row->to_readable());
  expect_mixed(*raw_row);
  std::vector<char> moved(body_size + GUARD_SIZE, 0x39);
  fill_guard(moved, body_size, 0x71);
  StoredRow *copy = new (moved.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, copy->assign(row));
  EXPECT_FALSE(copy->readable_);
  EXPECT_EQ(0, MEMCMP(source.data(), moved.data(), body_size));
  MEMSET(source.data(), 0, body_size);
  input.overwrite();
  ASSERT_EQ(OB_SUCCESS, copy->to_readable());
  expect_mixed(*copy);
  const std::vector<char> readable = moved;
  ASSERT_EQ(OB_SUCCESS, copy->to_readable());
  EXPECT_EQ(readable, moved);
  expect_guard(moved, body_size, 0x71);
}

TEST(FixedRaDatumCells, invalid_input_or_payload_capacity_fails_before_modifying_the_row)
{
  MixedInput input;
  int64_t quota = 0;
  ASSERT_EQ(OB_SUCCESS, Block::row_store_size(input.values_, quota, EXTRA_SIZE));
  const int64_t body_size = quota - Block::ROW_INDEX_SIZE;
  std::vector<char> memory(body_size + GUARD_SIZE, 0x39);
  StoredRow *row = new (memory.data()) StoredRow;
  const std::vector<char> before = memory;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, row->copy_datums(input.values_, row->payload_,
      body_size - sizeof(StoredRow) - 1, quota, EXTRA_SIZE));
  EXPECT_EQ(before, memory);
  input.values_.at(4).ptr_ = nullptr;
  EXPECT_EQ(OB_INVALID_ARGUMENT, row->copy_datums(input.values_, row->payload_,
      body_size - sizeof(StoredRow), quota, EXTRA_SIZE));
  EXPECT_EQ(before, memory);
  int64_t previous = 97;
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObRADatumStore::row_copy_size(input.values_, previous));
  EXPECT_EQ(97, previous);
}

TEST(FixedRaDatumCells, real_block_copy_checks_both_body_and_tail_index_capacity)
{
  MixedInput input;
  std::vector<char> source;
  StoredRow *row = copy_row(input.values_, source);
  ASSERT_NE(nullptr, row);
  for (int enough = 0; enough < 2; ++enough) {
    const int64_t capacity = sizeof(Block) + row->row_size_ + (enough ? 16 : -1);
    std::vector<char> bytes(capacity + GUARD_SIZE, 0x39);
    fill_guard(bytes, capacity, 0x51);
    Block *block = new (bytes.data()) Block;
    ObRADatumStore::ShrinkBuffer buffer;
    ASSERT_EQ(OB_SUCCESS, buffer.init(bytes.data(), capacity));
    ASSERT_EQ(OB_SUCCESS, buffer.fill_head(sizeof(Block)));
    block->idx_off_ = buffer.tail() - block->payload_;
    const std::vector<char> before = bytes;
    const int64_t head_before = buffer.head_size(), tail_before = buffer.tail_size();
    StoredRow *output = row;
    if (!enough) {
      EXPECT_EQ(OB_BUF_NOT_ENOUGH, block->copy_stored_row(buffer, *row, &output));
      EXPECT_EQ(row, output);
      EXPECT_EQ(head_before, buffer.head_size());
      EXPECT_EQ(tail_before, buffer.tail_size());
      EXPECT_EQ(before, bytes);
    } else {
      ASSERT_EQ(OB_SUCCESS, block->copy_stored_row(buffer, *row, &output));
      ASSERT_NE(row, output);
      EXPECT_EQ(row->body_size(), buffer.head_size() - head_before);
      EXPECT_EQ(int64_t(Block::ROW_INDEX_SIZE), buffer.tail_size() - tail_before);
      ASSERT_EQ(OB_SUCCESS, block->compact(buffer));
      ASSERT_EQ(OB_SUCCESS, block->to_copyable());
      const StoredRow *restored = nullptr;
      ASSERT_EQ(OB_SUCCESS, block->get_store_row(0, restored));
      ASSERT_EQ(output, restored);
      expect_mixed(*restored);
    }
    expect_guard(bytes, capacity, 0x51);
  }
}

TEST(FixedRaDatumCells, actual_store_add_finish_and_random_access_do_not_borrow_inputs)
{
  ObArenaAllocator arena;
  ObRADatumStore store(&arena);
  ASSERT_EQ(OB_SUCCESS, store.init(0, ObCtxIds::DEFAULT_CTX_ID, "FixedRaTest", EXTRA_SIZE));
  for (int64_t i = 0; i < 4; ++i) {
    MixedInput input(100 + i, static_cast<char>('a' + i));
    StoredRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, store.add_row(input.values_, &row));
    ASSERT_NE(nullptr, row);
    for (uint32_t j = 0; j < EXTRA_SIZE; ++j) {
      static_cast<char *>(row->get_extra_payload())[j] = static_cast<char>(0x20 + j);
    }
    input.overwrite();
  }
  ASSERT_EQ(OB_SUCCESS, store.finish_add_row());
  ASSERT_EQ(4, store.get_row_cnt());
  ObRADatumStore::Reader reader(store);
  for (int64_t index : {3, 1, 0, 2}) {
    const StoredRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, reader.get_row(index, row));
    ASSERT_NE(nullptr, row);
    expect_mixed(*row, 100 + index, static_cast<char>('a' + index));
  }
  EXPECT_FALSE(store.is_file_open()); // this test exercises the actual in-memory RA API
}

TEST(FixedRaDatumCells, expression_copy_and_output_decode_honor_batch_index_and_skip_constants)
{
  MixedInput input;
  ExprFrame frame;
  // The fixed-only cases preserve arbitrary descriptor bits. Here the fifth
  // column is a VARCHAR Expr, so opaque bytes cannot carry the EXT flag.
  Datum unchanged;
  unchanged.set_int(73);
  ASSERT_TRUE(input.values_.at(4).is_ext());
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      frame.exprs_[4].get_datum_copy_plan().import_legacy(input.values_.at(4), unchanged));
  EXPECT_EQ(73, unchanged.get_int());
  input.values_.at(4).set_outrow();
  ObEvalCtx::BatchInfoScopeGuard batch(frame.eval_);
  batch.set_batch_size(2);
  batch.set_batch_idx(1);
  for (int i = 0; i < 5; ++i) {
    ASSERT_EQ(OB_SUCCESS, frame.exprs_[i].get_datum_copy_plan().import_legacy(input.values_.at(i), frame.datums(i)[1]));
  }
  int64_t quota = 0;
  ASSERT_EQ(OB_SUCCESS, Block::row_store_size<false>(frame.expressions_, frame.eval_, quota, EXTRA_SIZE));
  const int64_t body_size = quota - Block::ROW_INDEX_SIZE;
  std::vector<char> bytes(body_size + GUARD_SIZE, 0x39);
  fill_guard(bytes, body_size, 0x51);
  StoredRow *row = new (bytes.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, row->copy_datums(frame.expressions_, frame.eval_, row->payload_,
      body_size - sizeof(StoredRow), quota, EXTRA_SIZE));
  EXPECT_EQ(ObDatumDesc::OUTROW, row->stored_cells()[4].flag_);
  input.overwrite();
  for (int i = 0; i < 5; ++i) {
    frame.datums(i)[0].set_null();
    frame.datums(i)[1].set_null();
  }
  frame.exprs_[4].is_static_const_ = true;
  ASSERT_EQ(OB_SUCCESS, row->to_expr(frame.expressions_, frame.eval_));
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(frame.datums(i)[0].is_null());
    alignas(8) char scratch[64]; ObDatum legacy;
    ASSERT_EQ(OB_SUCCESS, frame.exprs_[i].get_datum_copy_plan().legacy_view(frame.datums(i)[1], legacy, scratch, 64));
    if (!legacy.is_null() && i != 0) { EXPECT_EQ(row->stored_cells()[i].ptr_, legacy.ptr_); }
    if (i == 0) { EXPECT_EQ(-3000000001LL, frame.datums(i)[1].get_int()); }
    EXPECT_EQ(row->stored_cells()[i].pack_, legacy.pack_);
    EXPECT_TRUE(frame.exprs_[i].get_eval_info(frame.eval_).projected_);
  }
  EXPECT_TRUE(frame.datums(4)[1].is_null());
  EXPECT_FALSE(frame.exprs_[4].get_eval_info(frame.eval_).projected_);
  const auto previous = frame.bytes_;
  ASSERT_EQ(OB_SUCCESS, row->to_copyable());
  EXPECT_NE(OB_SUCCESS, row->to_expr(frame.expressions_, frame.eval_));
  EXPECT_EQ(previous, frame.bytes_);
  expect_guard(bytes, body_size, 0x51);
}

// The bundled old gtest has no GTEST_SKIP. Keep this integration-only case
// visibly disabled in an ordinary unit process, rather than counting a missing
// tmp-file service as a passing spill test. Enabling it requires a fully
// initialized tmp-file service in THIS process's server_service slot.
TEST(FixedRaDatumCells, DISABLED_actual_tmp_service_spill_roundtrip)
{
  int64_t directory = -1;
  ASSERT_EQ(OB_SUCCESS, data_plane::tmp_file_alloc_dir(directory))
      << "Requires this process's initialized/running Storage tmp-file manager; an external server is insufficient";
  struct TimeoutGuard {
    TimeoutGuard() : previous(THIS_WORKER.get_timeout_ts()) { THIS_WORKER.set_timeout_ts(INT64_MAX); }
    ~TimeoutGuard() { THIS_WORKER.set_timeout_ts(previous); }
    int64_t previous;
  } timeout;
  ObArenaAllocator arena;
  ObRADatumStore store(&arena);
  ASSERT_EQ(OB_SUCCESS, store.init(0, ObCtxIds::DEFAULT_CTX_ID, "FixedRaSpill", EXTRA_SIZE));
  const int64_t count = 96;
  for (int64_t i = 0; i < count; ++i) {
    int64_t value = i * 101 - 77;
    std::string payload(6003, static_cast<char>('a' + i % 26));
    ObSEArray<ObDatum, 3> values;
    ObDatum datum;
    datum.ptr_ = reinterpret_cast<const char *>(&value); datum.pack_ = sizeof(value);
    ASSERT_EQ(OB_SUCCESS, values.push_back(datum));
    datum.set_string(payload.data(), payload.size());
    ASSERT_EQ(OB_SUCCESS, values.push_back(datum));
    datum.set_null();
    ASSERT_EQ(OB_SUCCESS, values.push_back(datum));
    StoredRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, store.add_row(values, &row));
    MEMSET(row->get_extra_payload(), static_cast<unsigned char>(i), EXTRA_SIZE);
    payload.assign(payload.size(), 'x'); value = 0;
  }
  ASSERT_EQ(OB_SUCCESS, store.finish_add_row());
  ASSERT_EQ(OB_SUCCESS, store.dump(true));
  ASSERT_TRUE(store.is_all_dumped());
  ASSERT_TRUE(store.is_file_open());
  ASSERT_GT(store.get_file_size(), 0);
  ObRADatumStore::Reader reader(store);
  for (int64_t i = 0; i < count; ++i) {
    const int64_t index = (i * 37) % count;
    const StoredRow *row = nullptr;
    ASSERT_EQ(OB_SUCCESS, reader.get_row(index, row));
    ASSERT_NE(nullptr, row);
    ASSERT_TRUE(row->readable_);
    EXPECT_EQ(index * 101 - 77, integer_value(row->stored_cells()[0]));
    const StoredDatumCell &text = row->stored_cells()[1];
    ASSERT_EQ(6003, text.len_);
    const std::string expected(6003, static_cast<char>('a' + index % 26));
    EXPECT_EQ(0, MEMCMP(expected.data(), text.ptr_, expected.size()));
    EXPECT_TRUE(row->stored_cells()[2].is_null());
    const unsigned char *extra = static_cast<const unsigned char *>(row->get_extra_payload());
    for (uint32_t j = 0; j < EXTRA_SIZE; ++j) { EXPECT_EQ(index, extra[j]); }
  }
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
