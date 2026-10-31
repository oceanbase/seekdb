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

#include "sql/engine/recursive_cte/ob_fake_cte_table_op.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using StoredRow = ObChunkDatumStore::StoredRow;
constexpr int64_t GUARD_SIZE = 16;
constexpr uint32_t EXTRA_SIZE = 13;

void fill_guard(std::vector<char> &bytes, int64_t row_size)
{
  for (int64_t i = 0; i < GUARD_SIZE; ++i) { bytes[row_size + i] = static_cast<char>(0x51 + i); }
}

void expect_guard(const std::vector<char> &bytes, int64_t row_size)
{
  ASSERT_EQ(row_size + GUARD_SIZE, bytes.size());
  for (int64_t i = 0; i < GUARD_SIZE; ++i) {
    EXPECT_EQ(static_cast<char>(0x51 + i), bytes[row_size + i]);
  }
}

class GuardAllocator final : public ObIAllocator
{
public:
  void *alloc(int64_t size) override
  {
    ++calls_;
    if (reject_) { return nullptr; }
    size_ = size;
    bytes_.assign(size + GUARD_SIZE, 0x39);
    fill_guard(bytes_, size);
    return bytes_.data();
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { EXPECT_EQ(bytes_.data(), pointer); ++frees_; }
  std::vector<char> bytes_;
  int64_t size_ = 0;
  int calls_ = 0;
  int frees_ = 0;
  bool reject_ = false;
};

// The input is a real encoded StoredRow. No execution Datum array aliases its
// cell directory, and its payload is deliberately not naturally aligned.
struct MixedStoredRow
{
  explicit MixedStoredRow(int64_t integer = -3000000001LL, char first = 'a')
  {
    char text[] = {first, '\0', 'b'};
    unsigned char coefficient[16];
    for (int i = 0; i < 16; ++i) { coefficient[i] = i + 1; }
    StoredDatumCell cells[5];
    cells[0].ptr_ = reinterpret_cast<const char *>(&integer); cells[0].len_ = sizeof(integer);
    cells[1].ptr_ = text; cells[1].len_ = sizeof(text);
    cells[2].set_null();
    cells[3].ptr_ = nullptr; cells[3].len_ = 0; // non-NULL empty value
    cells[4].ptr_ = reinterpret_cast<const char *>(coefficient); cells[4].len_ = sizeof(coefficient);
    cells[4].flag_ = 2;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(cells, 5, EXTRA_SIZE);
    bytes_.assign(size + GUARD_SIZE, 0);
    fill_guard(bytes_, size);
    row_ = new (bytes_.data()) StoredRow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_cells(cells, 5, row_->payload_,
        size - sizeof(StoredRow), size, EXTRA_SIZE));
    MEMSET(row_->get_extra_payload(), 0x62, EXTRA_SIZE);
  }

  StoredRow *row_ = nullptr;
  std::vector<char> bytes_;
};

int64_t integer_value(const ObDatum &datum)
{
  int64_t value = 0;
  EXPECT_FALSE(datum.is_null());
  EXPECT_EQ(sizeof(value), datum.len_);
  if (!datum.is_null() && datum.len_ == sizeof(value)) { MEMCPY(&value, datum.ptr_, sizeof(value)); }
  return value;
}

void expect_selected(const StoredRow &row)
{
  ASSERT_EQ(6, row.cnt_);
  const StoredDatumCell *cells = row.stored_cells();
  ASSERT_EQ(16, cells[0].len_);
  EXPECT_EQ(2, cells[0].flag_);
  for (int i = 0; i < 16; ++i) { EXPECT_EQ(i + 1, static_cast<unsigned char>(cells[0].ptr_[i])); }
  EXPECT_TRUE(cells[1].is_null());
  EXPECT_EQ(-3000000001LL, integer_value(StoredDatumCodec::decode_legacy_view(cells[2])));
  const char text[] = {'a', '\0', 'b'};
  ASSERT_EQ(sizeof(text), cells[3].len_);
  EXPECT_EQ(0, MEMCMP(text, cells[3].ptr_, sizeof(text)));
  EXPECT_FALSE(cells[4].is_null());
  EXPECT_EQ(0, cells[4].len_);
  EXPECT_EQ(0, MEMCMP(text, cells[5].ptr_, sizeof(text)));
  EXPECT_NE(cells[3].ptr_, cells[5].ptr_); // duplicate selections own separate bytes
  const uintptr_t first = reinterpret_cast<uintptr_t>(row.get_extra_payload()) + EXTRA_SIZE;
  const uintptr_t end = reinterpret_cast<uintptr_t>(&row) + row.row_size_;
  for (int64_t i = 0; i < row.cnt_; ++i) {
    if (!cells[i].is_null()) {
      const uintptr_t pointer = reinterpret_cast<uintptr_t>(cells[i].ptr_);
      EXPECT_GE(pointer, first);
      EXPECT_LE(pointer + cells[i].len_, end);
    }
  }
}

class StoredFormatConsumers : public ::testing::Test
{
protected:
  StoredFormatConsumers() : arena_(), exec_(arena_), spec_(arena_, PHY_FAKE_CTE_TABLE),
      op_(exec_, spec_, nullptr), frame_{}, frames_{frame_.data()}, exprs_{}
  {}

  void SetUp() override
  {
    exec_.set_frames(frames_);
    op_.get_eval_ctx().frames_ = frames_;
  }

  void configure_expr(int64_t index, bool text, bool batch)
  {
    ObExpr &expr = exprs_[index];
    const int64_t base = index * 512;
    expr.frame_idx_ = 0;
    expr.datum_off_ = base + 16;
    expr.eval_info_off_ = base + 96;
    expr.eval_flags_off_ = base + 128;
    expr.res_buf_off_ = base + 160;
    expr.res_buf_len_ = 64;
    expr.type_ = T_COLUMN_REF;
    expr.batch_result_ = batch;
    expr.batch_idx_mask_ = batch ? UINT64_MAX : 0;
    expr.obj_datum_map_ = text ? OBJ_DATUM_STRING : OBJ_DATUM_8BYTE_DATA;
    if (text) { expr.obj_meta_.set_varchar(); } else { expr.obj_meta_.set_int(); }
    expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
    expr.datum_meta_ = ObDatumMeta(text ? ObVarcharType : ObIntType, CS_TYPE_BINARY, 0);
    ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
    for (int i = 0; i < 2; ++i) { new (frame_.data() + expr.datum_off_ + i * sizeof(Datum)) Datum; }
    new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
  }

  Datum *output(int64_t index)
  {
    return reinterpret_cast<Datum *>(frame_.data() + exprs_[index].datum_off_);
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObFakeCTETableSpec spec_;
  ObFakeCTETableOp op_;
  alignas(8) std::array<char, 4096> frame_;
  char *frames_[1];
  ObExpr exprs_[5];
};

TEST_F(StoredFormatConsumers, cte_chosen_cells_include_header_extra_and_relocate_as_a_complete_row)
{
  MixedStoredRow source;
  ObSEArray<int64_t, 6> chosen;
  for (int64_t index : {4, 2, 0, 1, 3, 1}) { ASSERT_EQ(OB_SUCCESS, chosen.push_back(index)); }
  GuardAllocator allocator;
  const StoredRow *copied = nullptr;
  ASSERT_EQ(OB_SUCCESS, op_.deep_copy_row(source.row_, copied, chosen, EXTRA_SIZE, allocator));
  ASSERT_NE(nullptr, copied);
  const int64_t expected_size = sizeof(StoredRow) + 6 * sizeof(StoredDatumCell)
      + EXTRA_SIZE + 16 + sizeof(int64_t) + 3 + 3;
  EXPECT_EQ(expected_size, allocator.size_);
  ASSERT_EQ(expected_size, copied->row_size_);
  expect_selected(*copied);
  expect_guard(allocator.bytes_, expected_size);
  // Chosen-row copy reserves, rather than imports, the destination extra bytes.
  const char *extra = static_cast<const char *>(copied->get_extra_payload());
  for (uint32_t i = 0; i < EXTRA_SIZE; ++i) { EXPECT_EQ(0x39, extra[i]); }
  for (uint32_t i = 0; i < EXTRA_SIZE; ++i) {
    static_cast<char *>(copied->get_extra_payload())[i] = static_cast<char>(0x20 + i);
  }
  MEMSET(source.bytes_.data(), 0, source.bytes_.size() - GUARD_SIZE);
  expect_selected(*copied);

  std::vector<char> moved(expected_size + GUARD_SIZE, 0x6d);
  fill_guard(moved, expected_size);
  StoredRow *relocated = new (moved.data()) StoredRow;
  ASSERT_EQ(OB_SUCCESS, relocated->assign(copied));
  MEMSET(allocator.bytes_.data(), 0, expected_size);
  expect_selected(*relocated);
  for (uint32_t i = 0; i < EXTRA_SIZE; ++i) {
    EXPECT_EQ(static_cast<char>(0x20 + i), static_cast<char *>(relocated->get_extra_payload())[i]);
  }
  expect_guard(moved, expected_size);
  expect_guard(allocator.bytes_, expected_size);
}

TEST_F(StoredFormatConsumers, cte_copy_rejects_invalid_selection_and_extra_without_allocating_or_publishing)
{
  MixedStoredRow source;
  GuardAllocator allocator;
  for (int64_t index : {-1, 5}) {
    ObSEArray<int64_t, 1> chosen;
    ASSERT_EQ(OB_SUCCESS, chosen.push_back(index));
    const StoredRow *output = source.row_;
    EXPECT_NE(OB_SUCCESS, op_.deep_copy_row(source.row_, output, chosen, EXTRA_SIZE, allocator));
    EXPECT_EQ(source.row_, output);
  }
  ObSEArray<int64_t, 1> chosen;
  ASSERT_EQ(OB_SUCCESS, chosen.push_back(0));
  for (int64_t extra : {int64_t(-1), INT64_MAX}) {
    const StoredRow *output = source.row_;
    EXPECT_NE(OB_SUCCESS, op_.deep_copy_row(source.row_, output, chosen, extra, allocator));
    EXPECT_EQ(source.row_, output);
  }
  EXPECT_EQ(0, allocator.calls_);
  allocator.reject_ = true;
  const StoredRow *output = source.row_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      op_.deep_copy_row(source.row_, output, chosen, EXTRA_SIZE, allocator));
  EXPECT_EQ(source.row_, output);
  EXPECT_EQ(1, allocator.calls_);
}

TEST_F(StoredFormatConsumers, cte_copy_payload_capacity_is_distinct_from_total_row_size)
{
  MixedStoredRow source;
  ObSEArray<int64_t, 1> chosen;
  ASSERT_EQ(OB_SUCCESS, chosen.push_back(1));
  const int64_t total = sizeof(StoredRow) + sizeof(StoredDatumCell) + EXTRA_SIZE + 3;
  std::vector<char> memory(total + GUARD_SIZE, 0x39);
  fill_guard(memory, total);
  StoredRow *row = new (memory.data()) StoredRow;
  const std::vector<char> before = memory;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, op_.copy_stored_cells(row, source.row_->stored_cells(), 5,
      chosen, row->payload_, total - sizeof(StoredRow) - 1, total, EXTRA_SIZE));
  EXPECT_EQ(before, memory);
  EXPECT_EQ(OB_INVALID_ARGUMENT, op_.copy_stored_cells(row, source.row_->stored_cells(), 5,
      chosen, row->payload_, total - sizeof(StoredRow), total - sizeof(StoredRow), EXTRA_SIZE));
  EXPECT_EQ(before, memory);
  ASSERT_EQ(OB_SUCCESS, op_.copy_stored_cells(row, source.row_->stored_cells(), 5,
      chosen, row->payload_, total - sizeof(StoredRow), total, EXTRA_SIZE));
  EXPECT_EQ(total, row->row_size_);
  EXPECT_EQ(0, MEMCMP("a\0b", row->stored_cells()[0].ptr_, 3));
  expect_guard(memory, total);
}

TEST_F(StoredFormatConsumers, cte_empty_selection_keeps_the_existing_empty_row_contract)
{
  MixedStoredRow source;
  ObSEArray<int64_t, 1> chosen;
  GuardAllocator allocator;
  const StoredRow *output = source.row_;
  EXPECT_EQ(OB_SUCCESS, op_.deep_copy_row(source.row_, output, chosen, EXTRA_SIZE, allocator));
  EXPECT_EQ(nullptr, output);
  EXPECT_EQ(0, allocator.calls_);
}

TEST_F(StoredFormatConsumers, cte_scalar_decode_preserves_fixed_buffers_variable_views_nulls_and_constants)
{
  MixedStoredRow source;
  ObSEArray<ObExpr *, 4> exprs;
  ObSEArray<int64_t, 4> chosen;
  for (int i = 0; i < 4; ++i) {
    configure_expr(i, i == 0, false);
    ASSERT_EQ(OB_SUCCESS, exprs.push_back(&exprs_[i]));
  }
  for (int64_t index : {1, 0, 2, -1}) { ASSERT_EQ(OB_SUCCESS, chosen.push_back(index)); }
  exprs_[3].is_static_const_ = true;
  output(3)->set_null();
  const Datum constant_descriptor = *output(3);

  // An invalid fixed integer must leave both its native slot and publication
  // flags untouched. The following valid call can then recompute that slot.
  ObSEArray<ObExpr *, 1> integer_expr;
  ObSEArray<int64_t, 1> integer_index;
  ASSERT_EQ(OB_SUCCESS, integer_expr.push_back(&exprs_[1]));
  ASSERT_EQ(OB_SUCCESS, integer_index.push_back(0));
  output(1)->set_int(91);
  source.row_->stored_cells()[0].len_ = sizeof(int64_t) - 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      op_.to_expr(integer_expr, integer_index, source.row_, op_.get_eval_ctx()));
  EXPECT_EQ(91, output(1)->get_int());
  EXPECT_FALSE(exprs_[1].get_eval_info(op_.get_eval_ctx()).evaluated_);
  EXPECT_FALSE(exprs_[1].get_eval_info(op_.get_eval_ctx()).projected_);
  source.row_->stored_cells()[0].len_ = sizeof(int64_t);

  ASSERT_EQ(OB_SUCCESS, op_.to_expr(exprs, chosen, source.row_, op_.get_eval_ctx()));
  EXPECT_EQ(source.row_->stored_cells()[1].ptr_, output(0)->get_reference());
  EXPECT_EQ(3, output(0)->length());
  EXPECT_EQ(0, MEMCMP("a\0b", output(0)->get_reference(), 3));
  EXPECT_EQ(-3000000001LL, output(1)->get_int());
  // The integer is now inline. Its explicit old ABI view borrows only this
  // caller's scratch, never the StoredRow payload.
  ObDatum integer_view;
  char integer_scratch[64];
  ASSERT_EQ(OB_SUCCESS, exprs_[1].get_datum_copy_plan().legacy_view(
      *output(1), integer_view, integer_scratch, sizeof(integer_scratch)));
  EXPECT_NE(source.row_->stored_cells()[0].ptr_, integer_view.ptr_);
  EXPECT_EQ(-3000000001LL, integer_value(integer_view));
  EXPECT_TRUE(output(2)->is_null());
  EXPECT_EQ(0, MEMCMP(&constant_descriptor, output(3), sizeof(constant_descriptor)));
  EXPECT_FALSE(exprs_[3].get_eval_info(op_.get_eval_ctx()).projected_);
  EXPECT_TRUE(exprs_[1].get_eval_info(op_.get_eval_ctx()).evaluated_);
  EXPECT_TRUE(exprs_[1].get_eval_info(op_.get_eval_ctx()).projected_);
  // Reference strings remain borrowed while inline integers survive reuse of
  // their source bytes; the fixed StoredRow descriptor is still a 12B cell.
  MEMSET(const_cast<char *>(source.row_->stored_cells()[0].ptr_), 0, sizeof(int64_t));
  EXPECT_EQ(-3000000001LL, output(1)->get_int());
}

TEST_F(StoredFormatConsumers, cte_batch_decode_uses_the_requested_offset_for_batch_and_nonbatch_results)
{
  MixedStoredRow first(10, 'x'), second(20, 'y'), third(30, 'z');
  ObArray<StoredRow *> rows;
  ASSERT_EQ(OB_SUCCESS, rows.push_back(first.row_));
  ASSERT_EQ(OB_SUCCESS, rows.push_back(second.row_));
  ASSERT_EQ(OB_SUCCESS, rows.push_back(third.row_));
  ObSEArray<ObExpr *, 5> exprs;
  ObSEArray<int64_t, 5> chosen;
  for (int i = 0; i < 5; ++i) {
    configure_expr(i, i % 2 == 0, i < 2);
    ASSERT_EQ(OB_SUCCESS, exprs.push_back(&exprs_[i]));
  }
  for (int64_t index : {1, 0, 1, 0, -1}) { ASSERT_EQ(OB_SUCCESS, chosen.push_back(index)); }
  exprs_[4].is_static_const_ = true;
  output(4)->set_null();
  op_.get_eval_ctx().set_max_batch_size(2);
  ASSERT_EQ(OB_SUCCESS, op_.attach_rows(exprs, chosen, rows, 1, op_.get_eval_ctx(), 2));
  EXPECT_EQ(second.row_->stored_cells()[1].ptr_, output(0)[0].get_reference());
  EXPECT_EQ(third.row_->stored_cells()[1].ptr_, output(0)[1].get_reference());
  EXPECT_EQ(20, output(1)[0].get_int());
  EXPECT_EQ(30, output(1)[1].get_int());
  EXPECT_EQ(second.row_->stored_cells()[1].ptr_, output(2)[0].get_reference());
  EXPECT_EQ(20, output(3)[0].get_int());
  EXPECT_TRUE(output(4)->is_null());
  EXPECT_FALSE(exprs_[4].get_eval_info(op_.get_eval_ctx()).projected_);
}

TEST_F(StoredFormatConsumers, cte_invalid_scalar_or_batch_indices_leave_all_outputs_unpublished)
{
  MixedStoredRow source;
  ObSEArray<ObExpr *, 2> exprs;
  ObSEArray<int64_t, 2> chosen;
  for (int i = 0; i < 2; ++i) {
    configure_expr(i, true, true);
    ASSERT_EQ(OB_SUCCESS, exprs.push_back(&exprs_[i]));
  }
  ASSERT_EQ(OB_SUCCESS, chosen.push_back(1));
  ASSERT_EQ(OB_SUCCESS, chosen.push_back(-1));
  const std::array<char, 4096> before = frame_;
  EXPECT_NE(OB_SUCCESS, op_.to_expr(exprs, chosen, source.row_, op_.get_eval_ctx()));
  EXPECT_EQ(before, frame_);
  ObArray<StoredRow *> rows;
  ASSERT_EQ(OB_SUCCESS, rows.push_back(source.row_));
  ASSERT_EQ(OB_SUCCESS, rows.push_back(nullptr));
  EXPECT_NE(OB_SUCCESS, op_.attach_rows(exprs, chosen, rows, 0, op_.get_eval_ctx(), 2));
  EXPECT_EQ(before, frame_);
  chosen.at(1) = 0;
  EXPECT_NE(OB_SUCCESS, op_.attach_rows(exprs, chosen, rows, 0, op_.get_eval_ctx(), 2));
  EXPECT_EQ(before, frame_);
  EXPECT_NE(OB_SUCCESS, op_.attach_rows(exprs, chosen, rows, -1, op_.get_eval_ctx(), 1));
  EXPECT_NE(OB_SUCCESS, op_.attach_rows(exprs, chosen, rows, 1, op_.get_eval_ctx(), 2));
  EXPECT_EQ(before, frame_);
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
