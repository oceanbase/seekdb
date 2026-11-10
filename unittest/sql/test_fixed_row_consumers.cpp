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

#include "data_plane/blocksstable/ob_storage_datum.h"
#include "data_plane/access/ob_table_scan_param.h"
#include "sql/das/iter/ob_das_merge_iter.h"
#include "sql/das/iter/ob_das_group_fold_iter.h"
#include "sql/das/iter/ob_das_index_merge_iter.h"
#include "sql/engine/px/ob_px_row_store.h"
#include "sql/engine/basic/ob_temp_table_access_op.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using StoredRow = ObChunkDatumStore::StoredRow;
using IndexStore = ObDASIndexMergeIter::IndexMergeRowStore;
static_assert(std::is_same<decltype(std::declval<MergeStoreRows &>().cur_stored_cells()),
    const StoredDatumCell *>::value, "DAS exposes the fixed row format");
static_assert(std::is_same<decltype(std::declval<IndexStore &>().cur_stored_cells()),
    const StoredDatumCell *>::value, "Index merge exposes the fixed row format");

// Bind real existing consumers to owned rows/iterators. The tests do not mock
// decoding, comparison, frame output or the row-store iterator itself.
template <typename Tag, typename Tag::Type Member>
struct PrivateMember { friend typename Tag::Type member(Tag) { return Member; } };
#define PRIVATE_MEMBER_TAG(Name, Owner, MemberType, Field) \
  struct Name { using Type = MemberType Owner::*; friend Type member(Name); }; \
  template struct PrivateMember<Name, &Owner::Field>
using MergeRows = ObSEArray<MergeStoreRows, 8>;
using IndexRows = ObFixedArray<IndexStore, ObIAllocator>;
PRIVATE_MEMBER_TAG(MergeRowsTag, ObDASMergeIter, MergeRows, merge_store_rows_arr_);
PRIVATE_MEMBER_TAG(MergeGroupTag, ObDASMergeIter, int64_t, group_id_idx_);
PRIVATE_MEMBER_TAG(GroupExprTag, ObDASIter, const ObExpr *, group_id_expr_);
PRIVATE_MEMBER_TAG(IndexRowsTag, ObDASIndexMergeIter, IndexRows, child_stores_);
PRIVATE_MEMBER_TAG(IndexExprsTag, ObDASIndexMergeIter, const ExprFixedArray *, rowkey_exprs_);
PRIVATE_MEMBER_TAG(IndexReverseTag, ObDASIndexMergeIter, bool, is_reverse_);
PRIVATE_MEMBER_TAG(TempIteratorTag, ObTempTableAccessOp, ObChunkDatumStore::Iterator, datum_store_it_);
PRIVATE_MEMBER_TAG(TempStartedTag, ObTempTableAccessOp, bool, is_started_);
PRIVATE_MEMBER_TAG(TempRowsTag, ObTempTableAccessOp, const StoredRow **, stored_rows_);
#undef PRIVATE_MEMBER_TAG
struct MergeCompareTag {
  using Type = int (ObDASMergeIter::*)(int64_t, int64_t &);
  friend Type member(MergeCompareTag);
};
template struct PrivateMember<MergeCompareTag, &ObDASMergeIter::compare>;
struct IndexCompareTag {
  using Type = int (ObDASIndexMergeIter::*)(int64_t, int64_t &, int &);
  friend Type member(IndexCompareTag);
};
template struct PrivateMember<IndexCompareTag, &ObDASIndexMergeIter::compare>;

// DAS iterators are arena objects in production: release while their dynamic
// type is alive, then reclaim arena storage without invoking the base
// destructor (which calls release() again after derived virtuals are gone).
template <typename Iter>
class ArenaIter
{
public:
  explicit ArenaIter(ObIAllocator &arena) : iter_(nullptr)
  {
    void *memory = arena.alloc(sizeof(Iter));
    if (memory != nullptr) { iter_ = new (memory) Iter; }
  }
  ~ArenaIter()
  {
    if (iter_ != nullptr) { EXPECT_EQ(OB_SUCCESS, iter_->release()); }
  }
  Iter *get() const { return iter_; }
  ArenaIter(const ArenaIter &) = delete;
  ArenaIter &operator=(const ArenaIter &) = delete;
private:
  Iter *iter_;
};

ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum bytes(const void *value, uint32_t length)
{ return ObDatum(static_cast<const char *>(value), length, false); }
int64_t read_int(const Datum &value)
{ EXPECT_FALSE(value.is_null()); return value.is_null() ? 0 : value.get_int(); }
int64_t read_int(const ObDatum &value)
{
  int64_t number = 0;
  EXPECT_FALSE(value.is_null());
  EXPECT_EQ(sizeof(number), value.len_);
  if (!value.is_null() && value.len_ == sizeof(number)) { MEMCPY(&number, value.ptr_, sizeof(number)); }
  return number;
}
int64_t read_int(const StoredDatumCell &cell)
{ return read_int(StoredDatumCodec::decode_legacy_view(cell)); }

struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values)
  {
    const uint32_t extra = (8 - (sizeof(StoredRow) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) StoredRow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(StoredRow), size, extra));
  }
  std::vector<char> memory_;
  StoredRow *row_ = nullptr;
};

class FixedRowConsumers : public ::testing::Test
{
protected:
  static constexpr int64_t COLS = 5;
  static constexpr int64_t BATCH = 4;
  FixedRowConsumers() : arena_(), exec_(arena_), frame_{}, frames_{frame_.data()},
      eval_(exec_), eval_guard_(eval_), expressions_{}, exprs_(arena_), numbers_{}, text_{}, decimal_{}, flagged_{} {}

  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    eval_.max_batch_size_ = BATCH;
    eval_guard_.set_batch_size(BATCH);
    ASSERT_EQ(OB_SUCCESS, exprs_.init(COLS));
    for (int64_t col = 0; col < COLS; ++col) {
      ObExpr &expr = expressions_[col];
      expr.type_ = T_REF_COLUMN;
      expr.frame_idx_ = 0;
      expr.datum_off_ = col * 1024;
      expr.eval_info_off_ = col * 1024 + 128;
      expr.eval_flags_off_ = col * 1024 + 192;
      expr.res_buf_off_ = col * 1024 + 512;
      expr.res_buf_len_ = 128;
      expr.batch_result_ = true;
      expr.batch_idx_mask_ = UINT64_MAX;
      set_type(col, col == 1 || col == 4 ? ObVarcharType : col == 2 ? ObDecimalIntType : ObIntType);
      for (int64_t row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      ASSERT_EQ(OB_SUCCESS, exprs_.push_back(&expr));
    }
    for (int64_t row = 0; row < BATCH; ++row) {
      numbers_[row] = row == 0 ? INT64_MIN : row == 1 ? INT64_MAX : 17 + row;
      const char payload[4] = {'a', '\0', 'z', static_cast<char>('0' + row)};
      MEMCPY(text_[row].data(), payload, sizeof(payload));
      decimal_[row].fill(static_cast<char>(0x30 + row));
      flagged_[row].fill(static_cast<char>('L' + row));
      set_datum(0, integer(numbers_[row]), row);
      set_datum(1, bytes(text_[row].data(), text_[row].size()), row);
      set_datum(2, bytes(decimal_[row].data(), decimal_[row].size()), row);
      datum(3, row).set_null();
      set_datum(4, bytes(flagged_[row].data(), flagged_[row].size()), row);
      datum(4, row).set_has_lob_header();
    }
    evaluated();
  }
  void set_type(int64_t col, SqlValueType type)
  {
    ObExpr &expr = expressions_[col];
    expr.obj_meta_.set_type(type);
    expr.datum_meta_ = ObDatumMeta(type, CS_TYPE_BINARY, 0);
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
    if (type == ObDecimalIntType) {
      expr.obj_meta_.set_scale(2);
      expr.datum_meta_.scale_ = 2;
      expr.datum_meta_.precision_ = PRECISION_UNKNOWN_YET;
    }
    expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
    ObAccuracy accuracy;
    accuracy.set_precision(expr.datum_meta_.precision_);
    accuracy.set_scale(expr.datum_meta_.scale_);
    ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, accuracy, arena_));
  }
  Datum &datum(int64_t col, int64_t row = 0)
  { return expressions_[col].locate_batch_datums(eval_)[row]; }
  void set_datum(int64_t column, const ObDatum &value, int64_t row = 0)
  { ASSERT_EQ(OB_SUCCESS, expressions_[column].get_datum_copy_plan().import_legacy(value, datum(column, row))); }
  ObDatum legacy(int64_t column, int64_t row = 0)
  {
    ObDatum value;
    EXPECT_EQ(OB_SUCCESS, expressions_[column].get_datum_copy_plan().legacy_view(
        datum(column, row), value, legacy_scratch_[column][row], 64));
    return value;
  }
  alignas(8) char legacy_scratch_[5][4][64]{};

  void evaluated()
  {
    for (auto &expr : expressions_) {
      expr.get_eval_info(eval_).evaluated_ = true;
      expr.get_evaluated_flags(eval_).set_all(BATCH);
    }
  }
  std::vector<ObDatum> values(int64_t row = 0)
  {
    std::vector<ObDatum> result;
    for (int64_t col = 0; col < COLS; ++col) { result.push_back(legacy(col, row)); }
    return result;
  }
  void expect_mixed(const StoredDatumCell *cells, int64_t row)
  {
    ASSERT_NE(nullptr, cells);
    EXPECT_EQ(row == 0 ? INT64_MIN : row == 1 ? INT64_MAX : 17 + row, read_int(cells[0]));
    const char expected[4] = {'a', '\0', 'z', static_cast<char>('0' + row)};
    EXPECT_EQ(4, cells[1].len_);
    EXPECT_EQ(0, MEMCMP(expected, cells[1].ptr_, sizeof(expected)));
    EXPECT_EQ(16, cells[2].len_);
    for (int i = 0; i < 16; ++i) { EXPECT_EQ(0x30 + row, cells[2].ptr_[i]); }
    EXPECT_TRUE(cells[3].is_null());
    EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, cells[4].flag_);
  }
  void poison_sources()
  {
    numbers_.fill(0);
    for (auto &value : text_) { value.fill('x'); }
    for (auto &value : decimal_) { value.fill('y'); }
    for (auto &value : flagged_) { value.fill('z'); }
    for (int64_t col = 0; col < COLS; ++col) {
      for (int64_t row = 0; row < BATCH; ++row) { datum(col, row).set_null(); }
    }
  }
  void bind_temp(ObTempTableAccessOp &op, ObChunkDatumStore &store, const StoredRow **rows)
  {
    op.get_eval_ctx().frames_ = frames_;
    op.get_eval_ctx().max_batch_size_ = BATCH;
    op.*member(TempStartedTag{}) = true;
    op.*member(TempRowsTag{}) = rows;
    ASSERT_EQ(OB_SUCCESS, store.begin(op.*member(TempIteratorTag{})));
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 8192> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard eval_guard_;
  ObExpr expressions_[COLS];
  ExprFixedArray exprs_;
  std::array<int64_t, BATCH> numbers_;
  std::array<std::array<char, 4>, BATCH> text_;
  std::array<std::array<char, 16>, BATCH> decimal_;
  std::array<std::array<char, 8>, BATCH> flagged_;
};

TEST_F(FixedRowConsumers, das_store_returns_stable_fixed_cells_and_restores_owned_mixed_rows)
{
  MergeStoreRows store(&exprs_, &eval_, 0, BATCH);
  ASSERT_EQ(OB_SUCCESS, store.init(arena_));
  ASSERT_EQ(OB_SUCCESS, store.save(true, 3));
  const StoredDatumCell *borrowed = store.cur_stored_cells();
  EXPECT_EQ(store.store_rows_[0].store_row_->stored_cells(), borrowed);
  poison_sources();
  expect_mixed(borrowed, 0);
  ASSERT_EQ(OB_SUCCESS, store.to_expr(true, 2));
  EXPECT_EQ(INT64_MIN, read_int(datum(0, 0)));
  EXPECT_EQ(INT64_MAX, read_int(datum(0, 1)));
  EXPECT_EQ(16, legacy(2, 1).len_);
  EXPECT_TRUE(datum(3, 0).is_null());
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, legacy(4, 1).flag_);
  expect_mixed(store.cur_stored_cells(), 2);
  store.reuse();
  EXPECT_FALSE(store.have_data());
  store.reset();
}

TEST_F(FixedRowConsumers, das_group_selection_reads_group_and_order_from_fixed_cells)
{
  ArenaIter<ObDASMergeIter> owner(arena_);
  ASSERT_NE(nullptr, owner.get());
  ObDASMergeIter &merge = *owner.get();
  auto &stores = merge.*member(MergeRowsTag{});
  merge.*member(MergeGroupTag{}) = 0;
  merge.*member(GroupExprTag{}) = &expressions_[0];
  for (int64_t i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, stores.push_back(MergeStoreRows(&exprs_, &eval_, 0, 1)));
    ASSERT_EQ(OB_SUCCESS, stores.at(i).init(arena_));
  }
  int64_t first = (int64_t(3) << 32) | 9;
  set_datum(0, integer(first));
  ASSERT_EQ(OB_SUCCESS, stores.at(0).save(false, 1));
  int64_t second = (int64_t(2) << 32) | 99;
  set_datum(0, integer(second));
  ASSERT_EQ(OB_SUCCESS, stores.at(1).save(false, 1));
  int64_t output = 0;
  ASSERT_EQ(OB_SUCCESS, (merge.*member(MergeCompareTag{}))(1, output));
  EXPECT_EQ(1, output);
  EXPECT_EQ(3, stores.at(0).cur_group_idx());
  first = (int64_t(2) << 32) | 7;
  set_datum(0, integer(first));
  ASSERT_EQ(OB_SUCCESS, stores.at(0).save(false, 1));
  output = 1;
  ASSERT_EQ(OB_SUCCESS, (merge.*member(MergeCompareTag{}))(0, output));
  EXPECT_EQ(0, output);
}

TEST_F(FixedRowConsumers, das_batch_group_count_is_independent_of_the_low_order_bits)
{
  MergeStoreRows store(&exprs_, &eval_, 0, BATCH);
  ASSERT_EQ(OB_SUCCESS, store.init(arena_));
  const int64_t groups[] = {(int64_t(2) << 32) | 3, (int64_t(2) << 32) | 8, int64_t(3) << 32};
  for (int i = 0; i < 3; ++i) { set_datum(0, integer(groups[i]), i); }
  ASSERT_EQ(OB_SUCCESS, store.save(true, 3));
  EXPECT_EQ(2, store.cur_group_idx());
  EXPECT_EQ(2, store.row_cnt_with_cur_group_idx());
  ASSERT_EQ(OB_SUCCESS, store.to_expr(true, 2));
  EXPECT_EQ(3, store.cur_group_idx());
  EXPECT_EQ(1, store.row_cnt_with_cur_group_idx());
  store.reset();
}

TEST_F(FixedRowConsumers, index_store_never_returns_a_temporary_descriptor_array)
{
  IndexStore store;
  ASSERT_EQ(OB_SUCCESS, store.init(arena_, &exprs_, &eval_, BATCH));
  EXPECT_EQ(nullptr, store.cur_stored_cells());
  ASSERT_EQ(OB_SUCCESS, store.save(true, 2));
  const StoredDatumCell *borrowed = store.cur_stored_cells();
  poison_sources();
  expect_mixed(borrowed, 0);
  EXPECT_EQ(store.store_rows_[0].store_row_->stored_cells(), borrowed);
  ASSERT_EQ(OB_SUCCESS, store.to_expr());
  EXPECT_EQ(INT64_MIN, read_int(datum(0)));
  expect_mixed(store.cur_stored_cells(), 1);
  store.reuse();
  EXPECT_EQ(nullptr, store.cur_stored_cells());
  store.reset();
}

TEST_F(FixedRowConsumers, index_merge_compares_fixed_cells_with_existing_direction_and_collation)
{
  ExprFixedArray keys(arena_);
  ASSERT_EQ(OB_SUCCESS, keys.init(2));
  ASSERT_EQ(OB_SUCCESS, keys.push_back(&expressions_[0]));
  ASSERT_EQ(OB_SUCCESS, keys.push_back(&expressions_[1]));
  expressions_[1].obj_meta_.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  ASSERT_EQ(OB_SUCCESS, expressions_[1].prepare_scalar_type(expressions_[1].obj_meta_,
      expressions_[1].get_sql_type().accuracy(), arena_));
  ArenaIter<ObDASIndexMergeIter> owner(arena_);
  ASSERT_NE(nullptr, owner.get());
  ObDASIndexMergeIter &merge = *owner.get();
  merge.*member(IndexExprsTag{}) = &keys;
  auto &stores = merge.*member(IndexRowsTag{});
  stores.set_allocator(&arena_);
  ASSERT_EQ(OB_SUCCESS, stores.prepare_allocate(2));
  int64_t number = 7;
  const char a[] = "customer", b[] = "CUSTOMER";
  for (int64_t i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, stores.at(i).init(arena_, &keys, &eval_, 1));
    set_datum(0, integer(number));
    set_datum(1, bytes(i == 0 ? a : b, 8));
    ASSERT_EQ(OB_SUCCESS, stores.at(i).save(false, 1));
  }
  int64_t output = 0;
  int cmp = 17;
  ASSERT_EQ(OB_SUCCESS, (merge.*member(IndexCompareTag{}))(1, output, cmp));
  EXPECT_EQ(0, cmp);
  number = INT64_MIN;
  set_datum(0, integer(number));
  ASSERT_EQ(OB_SUCCESS, stores.at(1).save(false, 1));
  output = 0;
  ASSERT_EQ(OB_SUCCESS, (merge.*member(IndexCompareTag{}))(1, output, cmp));
  EXPECT_LT(cmp, 0); EXPECT_EQ(1, output);
  merge.*member(IndexReverseTag{}) = true;
  output = 0;
  ASSERT_EQ(OB_SUCCESS, (merge.*member(IndexCompareTag{}))(1, output, cmp));
  EXPECT_LT(cmp, 0); EXPECT_EQ(0, output);
  datum(0).set_null();
  ASSERT_EQ(OB_SUCCESS, stores.at(1).save(false, 1));
  output = 0;
  ASSERT_EQ(OB_SUCCESS, (merge.*member(IndexCompareTag{}))(1, output, cmp));
  EXPECT_NE(0, cmp);
  number = 7;
  set_datum(0, integer(number));
  ASSERT_EQ(OB_SUCCESS, stores.at(1).save(false, 1));
  keys.at(1) = nullptr;
  output = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, (merge.*member(IndexCompareTag{}))(1, output, cmp));
  EXPECT_EQ(0, output);
  keys.at(1) = &expressions_[1];
}

TEST_F(FixedRowConsumers, px_scalar_output_preserves_flags_unknown_decimal_and_dynamic_constant_ownership)
{
  OwnedRow row(values());
  ObSEArray<ObExpr *, 1> dynamic;
  ASSERT_EQ(OB_SUCCESS, dynamic.push_back(&expressions_[1]));
  for (auto &expr : expressions_) { expr.batch_result_ = false; expr.batch_idx_mask_ = 0; }
  expressions_[0].is_static_const_ = true;
  int64_t constant = 71;
  set_datum(0, integer(constant));
  ASSERT_EQ(OB_SUCCESS, ObReceiveRowReader::to_expr(row.row_, dynamic, exprs_, eval_));
  EXPECT_EQ(71, read_int(datum(0)));
  EXPECT_EQ(16, legacy(2).len_);
  EXPECT_EQ(PRECISION_UNKNOWN_YET, expressions_[2].datum_meta_.precision_);
  EXPECT_TRUE(datum(3).is_null());
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, legacy(4).flag_);
  EXPECT_EQ(row.row_->stored_cells()[2].ptr_, legacy(2).ptr_);
  EXPECT_NE(row.row_->stored_cells()[1].ptr_, legacy(1).ptr_);
  MEMSET(const_cast<char *>(row.row_->stored_cells()[1].ptr_), 'x', 4);
  const char expected[4] = {'a', '\0', 'z', '0'};
  EXPECT_EQ(0, MEMCMP(expected, legacy(1).ptr_, sizeof(expected)));
}

TEST_F(FixedRowConsumers, px_batch_output_uses_first_row_for_nonbatch_values_and_skips_static_constants)
{
  OwnedRow first(values(0)), second(values(1));
  const StoredRow *rows[] = {first.row_, second.row_};
  ObSEArray<ObExpr *, 1> dynamic;
  expressions_[0].is_static_const_ = true;
  int64_t constant = 89;
  set_datum(0, integer(constant));
  expressions_[2].batch_result_ = false;
  expressions_[2].batch_idx_mask_ = 0;
  ASSERT_EQ(OB_SUCCESS, ObReceiveRowReader::attach_rows(exprs_, dynamic, eval_, rows, 2));
  EXPECT_EQ(89, read_int(datum(0)));
  EXPECT_EQ(first.row_->stored_cells()[2].ptr_, legacy(2).ptr_);
  EXPECT_EQ(second.row_->stored_cells()[1].ptr_, legacy(1, 1).ptr_);
  EXPECT_TRUE(datum(3, 1).is_null());
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, legacy(4, 1).flag_);
  EXPECT_FALSE(expressions_[1].get_eval_info(eval_).point_to_frame_);
  EXPECT_TRUE(expressions_[1].get_eval_info(eval_).projected_);
}

TEST_F(FixedRowConsumers, temp_scalar_iterator_decodes_reordered_duplicate_columns)
{
  OwnedRow source(values());
  ObChunkDatumStore store("FixedRowCells", &arena_);
  ASSERT_EQ(OB_SUCCESS, store.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "FixedRowCells", false));
  ASSERT_EQ(OB_SUCCESS, store.add_row(*source.row_));
  ASSERT_EQ(OB_SUCCESS, store.finish_add_row(false));
  ObTempTableAccessOpSpec spec(arena_, PHY_TEMP_TABLE_ACCESS);
  ASSERT_EQ(OB_SUCCESS, spec.init_output_index(3));
  ASSERT_EQ(OB_SUCCESS, spec.init_access_exprs(3));
  const int64_t selected[] = {2, 0, 2};
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, spec.add_output_index(selected[i]));
    ASSERT_EQ(OB_SUCCESS, spec.add_access_expr(&expressions_[i]));
    set_type(i, i == 1 ? ObIntType : ObDecimalIntType);
    expressions_[i].batch_result_ = false;
    expressions_[i].batch_idx_mask_ = 0;
  }
  ObTempTableAccessOp op(exec_, spec, nullptr);
  const StoredRow *rows[BATCH] = {};
  bind_temp(op, store, rows);
  ObEvalCtx::BatchInfoScopeGuard op_guard(op.get_eval_ctx());
  op_guard.set_batch_size(BATCH);
  MEMSET(source.memory_.data(), 0xA5, source.memory_.size());
  ASSERT_EQ(OB_SUCCESS, op.inner_get_next_row());
  EXPECT_EQ(INT64_MIN, read_int(datum(1)));
  EXPECT_EQ(16, legacy(0).len_);
  EXPECT_EQ(legacy(0).ptr_, legacy(2).ptr_);
  EXPECT_EQ(PRECISION_UNKNOWN_YET, expressions_[0].datum_meta_.precision_);
  for (int i = 0; i < 16; ++i) { EXPECT_EQ(0x30, legacy(0).ptr_[i]); }
  EXPECT_EQ(OB_ITER_END, op.inner_get_next_row());
}

TEST_F(FixedRowConsumers, temp_batch_iterator_decodes_fixed_rows_with_nonbatch_and_null_columns)
{
  OwnedRow first(values(0)), second(values(1));
  ObChunkDatumStore store("FixedRowCells", &arena_);
  ASSERT_EQ(OB_SUCCESS, store.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "FixedRowCells", false));
  ASSERT_EQ(OB_SUCCESS, store.add_row(*first.row_));
  ASSERT_EQ(OB_SUCCESS, store.add_row(*second.row_));
  ASSERT_EQ(OB_SUCCESS, store.finish_add_row(false));
  ObTempTableAccessOpSpec spec(arena_, PHY_TEMP_TABLE_ACCESS);
  spec.max_batch_size_ = BATCH;
  ASSERT_EQ(OB_SUCCESS, spec.init_output_index(COLS));
  ASSERT_EQ(OB_SUCCESS, spec.init_access_exprs(COLS));
  for (int i = 0; i < COLS; ++i) {
    ASSERT_EQ(OB_SUCCESS, spec.add_output_index(i));
    ASSERT_EQ(OB_SUCCESS, spec.add_access_expr(&expressions_[i]));
  }
  expressions_[2].batch_result_ = false;
  expressions_[2].batch_idx_mask_ = 0;
  ObTempTableAccessOp op(exec_, spec, nullptr);
  const StoredRow *rows[BATCH] = {};
  bind_temp(op, store, rows);
  ObEvalCtx::BatchInfoScopeGuard op_guard(op.get_eval_ctx());
  op_guard.set_batch_size(BATCH);
  ASSERT_EQ(OB_SUCCESS, op.inner_get_next_batch(BATCH));
  EXPECT_EQ(INT64_MIN, read_int(datum(0, 0)));
  EXPECT_EQ(INT64_MAX, read_int(datum(0, 1)));
  EXPECT_EQ(16, legacy(2).len_);
  EXPECT_EQ(0x30, legacy(2).ptr_[0]);
  EXPECT_TRUE(datum(3, 1).is_null());
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, legacy(4, 1).flag_);
  EXPECT_EQ(OB_SUCCESS, op.inner_get_next_batch(BATCH));
  EXPECT_TRUE(op.get_brs().end_);
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
