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

#include <functional>
#include "data_plane/blocksstable/ob_storage_datum.h"
#include "sql/engine/px/exchange/ob_row_heap.h"
#include "sql/engine/set/ob_merge_set_op.h"
#include "sql/engine/aggregate/ob_merge_distinct_op.h"
#include "sql/engine/basic/ob_hash_partitioning_infrastructure_op.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/charset/ob_charset.h"
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
using FixedRow = ObChunkDatumStore::StoredRow;

ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum text(const std::string &value)
{ return ObDatum(value.data(), value.size(), false); }
ObDatum null_value()
{ ObDatum value; value.set_null(); return value; }

// The real StoredRow writer owns the payload; relocation uses the same
// unswizzle/swizzle operations as a moved block, without a file-service fixture.
struct OwnedFixedRow
{
  explicit OwnedFixedRow(const std::vector<ObDatum> &values, uint32_t extra = 0)
  {
    extra += (8 - (sizeof(FixedRow) + values.size() * sizeof(StoredDatumCell) + extra) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) FixedRow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(FixedRow), size, extra));
  }
  void relocate()
  {
    const void *original = row_;
    row_->unswizzling(row_->payload_);
    std::vector<char> moved(memory_);
    MEMSET(memory_.data(), 0xA5, memory_.size());
    memory_.swap(moved);
    row_ = reinterpret_cast<FixedRow *>(memory_.data());
    EXPECT_NE(original, row_);
    row_->swizzling(row_->payload_);
  }
  std::vector<char> memory_;
  FixedRow *row_ = nullptr;
};

struct CompareProbe
{
  static const char *left;
  static const char *right;
  static uint32_t left_pack;
  static uint32_t right_pack;
  static int calls;
  static int fail(const ObDatum &l, const ObDatum &r, int &cmp,
                  const ObDatumAccessContext *context)
  {
    ++calls;
    EXPECT_EQ(left, l.ptr_);
    EXPECT_EQ(right, r.ptr_);
    EXPECT_EQ(left_pack, l.pack_);
    EXPECT_EQ(right_pack, r.pack_);
    EXPECT_EQ(nullptr, context);
    cmp = 0;
    return OB_ERR_UNEXPECTED;
  }
  static void expect(const StoredDatumCell &l, const ObDatum &r)
  { left = l.ptr_; right = r.ptr_; left_pack = l.pack_; right_pack = r.pack_; calls = 0; }
};
const char *CompareProbe::left = nullptr;
const char *CompareProbe::right = nullptr;
uint32_t CompareProbe::left_pack = 0;
uint32_t CompareProbe::right_pack = 0;
int CompareProbe::calls = 0;

class FixedCompareCells : public ::testing::Test
{
protected:
  static constexpr int64_t COLS = 3;
  static constexpr int64_t BATCH = 4;
  FixedCompareCells() : arena_(), exec_(arena_), frame_{}, frames_{frame_.data()},
      eval_(exec_), guard_(eval_), expressions_{} {}
  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    eval_.max_batch_size_ = BATCH;
    guard_.set_batch_size(BATCH);
    guard_.set_batch_idx(0);
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
      const SqlValueType type = col == 1 ? ObVarcharType : ObIntType;
      expr.obj_meta_.set_type(type);
      expr.datum_meta_ = ObDatumMeta(type, CS_TYPE_BINARY, 0);
      expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
      expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
      for (int64_t row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      expr.get_eval_info(eval_).evaluated_ = true;
      expr.get_evaluated_flags(eval_).set_all(BATCH);
      ASSERT_EQ(OB_SUCCESS, exprs_.push_back(&expr));
    }
  }
  void add_key(uint32_t column, bool ascending = true, ObCmpNullPos null_position = NULL_LAST,
               SqlValueType type = ObIntType, ObCollationType collation = CS_TYPE_BINARY)
  {
    ASSERT_EQ(OB_SUCCESS, collations_.push_back(ObSortFieldCollation(column, collation, ascending, null_position)));
    ObCmpFunc function;
    function.cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(type, type, null_position, collation,
                                                          SCALE_UNKNOWN_YET, false);
    ASSERT_NE(nullptr, function.cmp_func_);
    ASSERT_EQ(OB_SUCCESS, functions_.push_back(function));
  }
  void add_failing_key(uint32_t column)
  {
    ASSERT_EQ(OB_SUCCESS, collations_.push_back(ObSortFieldCollation(column, CS_TYPE_BINARY, true, NULL_LAST)));
    ObCmpFunc function;
    function.cmp_func_ = CompareProbe::fail;
    ASSERT_EQ(OB_SUCCESS, functions_.push_back(function));
  }
  Datum &datum(int64_t column, int64_t row = 0)
  { return expressions_[column].locate_batch_datums(eval_)[row]; }
  void set_datum(int64_t column, const ObDatum &value, int64_t row = 0)
  { ASSERT_EQ(OB_SUCCESS, expressions_[column].get_datum_copy_plan().import_legacy(value, datum(column, row))); }
  ObDatum legacy(int64_t column, int64_t row = 0)
  {
    ObDatum value;
    EXPECT_EQ(OB_SUCCESS, expressions_[column].get_datum_copy_plan().legacy_view(
        datum(column, row), value, legacy_scratch_[column][row], 64));
    return value;
  }
  alignas(8) char legacy_scratch_[3][4][64]{};

  void bind_row(int64_t row, const int64_t &first, const std::string &middle, const ObDatum &last)
  { set_datum(0, integer(first), row); set_datum(1, text(middle), row); set_datum(2, last, row); }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 4096> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard guard_;
  ObExpr expressions_[COLS];
  ObSEArray<ObExpr *, COLS> exprs_;
  ObSEArray<ObSortFieldCollation, COLS> collations_;
  ObSEArray<ObCmpFunc, COLS> functions_;
};

TEST_F(FixedCompareCells, real_heap_consumes_relocated_rows_with_mapped_keys_and_ci_ties)
{
  add_key(2); add_key(1, true, NULL_LAST, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI); add_key(0, false);
  int64_t a_id = 9, b_id = 1, c_id = 2, d_id = 5, key = 3;
  std::string beta = "beta", upper = "ALPHA", lower = "alpha", ignored = "unused";
  OwnedFixedRow a({integer(a_id), text(beta), integer(key)});
  OwnedFixedRow b({integer(b_id), text(upper), integer(key)});
  OwnedFixedRow c({integer(c_id), text(lower), integer(key)});
  OwnedFixedRow d({integer(d_id), text(ignored), null_value()});
  a_id = b_id = c_id = d_id = key = 0;
  beta.assign(beta.size(), 'x'); upper.assign(upper.size(), 'x'); lower.assign(lower.size(), 'x');
  a.relocate(); c.relocate();
  ObRowHeap<ObDatumRowCompare, FixedRow> heap;
  ASSERT_EQ(OB_SUCCESS, heap.init(4, &collations_, &functions_));
  for (const FixedRow *row : {a.row_, b.row_, d.row_, c.row_}) { ASSERT_EQ(OB_SUCCESS, heap.push(row)); }
  const FixedRow *expected[] = {c.row_, b.row_, a.row_, d.row_};
  for (const FixedRow *wanted : expected) {
    const FixedRow *actual = nullptr;
    ASSERT_EQ(OB_SUCCESS, heap.pop(actual));
    EXPECT_EQ(wanted, actual);
    heap.shrink();
  }
  EXPECT_EQ(0, heap.count());
}

TEST_F(FixedCompareCells, both_heap_comparators_preserve_null_order_direction_and_extremes)
{
  int64_t low = INT64_MIN, high = INT64_MAX;
  OwnedFixedRow n({null_value()}), l({integer(low)}), h({integer(high)});
  ObSEArray<const FixedRow *, 3> rows;
  ASSERT_EQ(OB_SUCCESS, rows.push_back(n.row_)); ASSERT_EQ(OB_SUCCESS, rows.push_back(l.row_));
  ASSERT_EQ(OB_SUCCESS, rows.push_back(h.row_));
  ObChunkDatumStore::LastStoredRow last_n(arena_), last_l(arena_), last_h(arena_);
  last_n.store_row_ = n.row_; last_l.store_row_ = l.row_; last_h.store_row_ = h.row_;
  ObSEArray<const ObChunkDatumStore::LastStoredRow *, 3> last;
  ASSERT_EQ(OB_SUCCESS, last.push_back(&last_n)); ASSERT_EQ(OB_SUCCESS, last.push_back(&last_l));
  ASSERT_EQ(OB_SUCCESS, last.push_back(&last_h));
  for (bool ascending : {false, true}) {
    for (ObCmpNullPos null_position : {NULL_FIRST, NULL_LAST}) {
      collations_.reuse(); functions_.reuse(); add_key(0, ascending, null_position);
      ObDatumRowCompare compare; ObMaxDatumRowCompare max_compare;
      ASSERT_EQ(OB_SUCCESS, compare.init(&collations_, &functions_, rows, nullptr));
      ASSERT_EQ(OB_SUCCESS, max_compare.init(&collations_, &functions_, last, nullptr));
      const bool null_after = (null_position == NULL_FIRST) != ascending;
      EXPECT_EQ(null_after, compare(0, 1)); EXPECT_EQ(null_after, max_compare(0, 1));
      EXPECT_EQ(!ascending, compare(1, 2)); EXPECT_EQ(!ascending, max_compare(1, 2));
      EXPECT_FALSE(compare(0, 0)); EXPECT_FALSE(max_compare(0, 0));
      EXPECT_EQ(OB_SUCCESS, compare.get_ret()); EXPECT_EQ(OB_SUCCESS, max_compare.get_ret());
    }
  }
}

TEST_F(FixedCompareCells, heap_comparator_error_is_sticky_and_preserves_borrowed_flags)
{
  add_failing_key(0);
  std::string left = "left", right = "right";
  ObDatum l = text(left), r = text(right); l.set_has_lob_header(); r.set_has_lob_header();
  OwnedFixedRow a({l}), b({r}); a.relocate();
  ObSEArray<const FixedRow *, 2> rows;
  ASSERT_EQ(OB_SUCCESS, rows.push_back(a.row_)); ASSERT_EQ(OB_SUCCESS, rows.push_back(b.row_));
  CompareProbe::expect(a.row_->stored_cells()[0], StoredDatumCodec::decode_legacy_view(b.row_->stored_cells()[0]));
  ObDatumRowCompare compare;
  ASSERT_EQ(OB_SUCCESS, compare.init(&collations_, &functions_, rows, nullptr));
  EXPECT_FALSE(compare(0, 1)); EXPECT_EQ(OB_ERR_UNEXPECTED, compare.get_ret());
  EXPECT_FALSE(compare(0, 1)); EXPECT_EQ(1, CompareProbe::calls);
  ObChunkDatumStore::LastStoredRow last_a(arena_), last_b(arena_);
  last_a.store_row_ = a.row_; last_b.store_row_ = b.row_;
  ObSEArray<const ObChunkDatumStore::LastStoredRow *, 2> last;
  ASSERT_EQ(OB_SUCCESS, last.push_back(&last_a)); ASSERT_EQ(OB_SUCCESS, last.push_back(&last_b));
  ObMaxDatumRowCompare max_compare;
  ASSERT_EQ(OB_SUCCESS, max_compare.init(&collations_, &functions_, last, nullptr));
  CompareProbe::calls = 0;
  EXPECT_FALSE(max_compare(0, 1)); EXPECT_EQ(OB_ERR_UNEXPECTED, max_compare.get_ret());
  EXPECT_FALSE(max_compare(0, 1)); EXPECT_EQ(1, CompareProbe::calls);
}

TEST_F(FixedCompareCells, merge_set_scalar_and_batch_keep_mapping_direction_and_batch_scope)
{
  add_key(2); add_key(1, false, NULL_LAST, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI); add_key(0, false);
  int64_t id = 9, stored_key = 19, next_key = 20;
  std::string name = "customer", current_name = "CUSTOMER";
  OwnedFixedRow row({integer(id), text(name), integer(stored_key)}); row.relocate();
  bind_row(0, id, current_name, integer(stored_key)); bind_row(2, id, current_name, integer(next_key));
  name.assign(name.size(), 'x');
  ObMergeSetOp::Compare compare; ASSERT_EQ(OB_SUCCESS, compare.init(&collations_, &functions_));
  int cmp = 77;
  ASSERT_EQ(OB_SUCCESS, compare(*row.row_, exprs_, eval_, cmp)); EXPECT_EQ(0, cmp);
  ASSERT_EQ(OB_SUCCESS, compare(*row.row_, exprs_, 2, eval_, cmp)); EXPECT_LT(cmp, 0);
  EXPECT_EQ(0, eval_.get_batch_idx());
  collations_.at(0).is_ascending_ = false;
  ASSERT_EQ(OB_SUCCESS, compare(*row.row_, exprs_, 2, eval_, cmp)); EXPECT_GT(cmp, 0);
  expressions_[2].batch_result_ = false; expressions_[2].batch_idx_mask_ = 0;
  ASSERT_EQ(OB_SUCCESS, compare(*row.row_, exprs_, 2, eval_, cmp)); EXPECT_EQ(0, cmp);
  EXPECT_EQ(0, eval_.get_batch_idx());
}

TEST_F(FixedCompareCells, merge_distinct_saved_row_handles_ci_null_nonbatch_and_constant_distinct)
{
  add_key(0); add_key(1, true, NULL_LAST, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI); add_key(2);
  int64_t id = INT64_MIN, different = INT64_MAX;
  std::string source = "customer", current = "CUSTOMER";
  OwnedFixedRow row({integer(id), text(source), null_value()}); row.relocate();
  source.assign(source.size(), 'x');
  bind_row(0, id, current, null_value()); bind_row(2, id, current, null_value());
  ObMergeDistinctOp::Compare compare; ASSERT_EQ(OB_SUCCESS, compare.init(&eval_, &functions_));
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, compare.equal(&exprs_, row.row_, equal)); EXPECT_TRUE(equal);
  ASSERT_EQ(OB_SUCCESS, compare.equal_in_batch(&exprs_, row.row_, 2, equal)); EXPECT_TRUE(equal);
  set_datum(0, integer(different), 2);
  ASSERT_EQ(OB_SUCCESS, compare.equal_in_batch(&exprs_, row.row_, 2, equal)); EXPECT_FALSE(equal);
  expressions_[0].batch_result_ = false; expressions_[0].batch_idx_mask_ = 0;
  ASSERT_EQ(OB_SUCCESS, compare.equal_in_batch(&exprs_, row.row_, 2, equal)); EXPECT_TRUE(equal);
  ObSEArray<ObExpr *, 1> empty;
  ASSERT_EQ(OB_SUCCESS, compare.equal(&empty, nullptr, equal)); EXPECT_TRUE(equal);
  ASSERT_EQ(OB_SUCCESS, compare.equal_in_batch(&empty, static_cast<const FixedRow *>(nullptr), 2, equal));
  EXPECT_TRUE(equal);
  EXPECT_EQ(OB_ERR_UNEXPECTED, compare.equal(&exprs_, nullptr, equal)); EXPECT_FALSE(equal);
}

TEST_F(FixedCompareCells, merge_set_saved_null_keeps_bound_null_order_and_direction)
{
  int64_t value = INT64_MAX;
  OwnedFixedRow row({null_value()}); row.relocate();
  set_datum(0, integer(value), 0); set_datum(0, integer(value), 2);
  ObSEArray<ObExpr *, 1> one;
  ASSERT_EQ(OB_SUCCESS, one.push_back(&expressions_[0]));
  for (bool ascending : {false, true}) {
    for (ObCmpNullPos null_position : {NULL_FIRST, NULL_LAST}) {
      collations_.reuse(); functions_.reuse(); add_key(0, ascending, null_position);
      ObMergeSetOp::Compare compare; ASSERT_EQ(OB_SUCCESS, compare.init(&collations_, &functions_));
      const bool negative = (null_position == NULL_FIRST) == ascending;
      int cmp = 0;
      ASSERT_EQ(OB_SUCCESS, compare(*row.row_, one, eval_, cmp)); EXPECT_EQ(negative, cmp < 0);
      ASSERT_EQ(OB_SUCCESS, compare(*row.row_, one, 2, eval_, cmp)); EXPECT_EQ(negative, cmp < 0);
      EXPECT_EQ(0, eval_.get_batch_idx());
    }
  }
}

TEST_F(FixedCompareCells, stored_hash_distinct_uses_mapped_keys_and_restores_expression_batch_scope)
{
  add_key(2); add_key(1, true, NULL_LAST, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  int64_t ignored = INT64_MIN, current_ignored = INT64_MAX, scalar = 7, different = 8;
  std::string source = "customer", current = "CUSTOMER", other = "other";
  OwnedFixedRow row({integer(ignored), text(source), integer(scalar)}, sizeof(ObHashPartStoredRow::ExtraInfo));
  ObHashPartCols saved, probe(2);
  saved.use_expr_ = false; saved.store_row_ = static_cast<ObHashPartStoredRow *>(row.row_);
  ASSERT_FALSE(saved.use_expr_);
  ASSERT_EQ(OB_SUCCESS, saved.set_hash_value(UINT64_MAX));
  saved.store_row_->set_is_match(true);
  row.relocate(); saved.store_row_ = static_cast<ObHashPartStoredRow *>(row.row_);
  source.assign(source.size(), 'x');
  EXPECT_EQ(uint64_t(ObHashPartCols::HASH_VAL_MASK), saved.hash());
  EXPECT_EQ(uint64_t(ObHashPartCols::HASH_VAL_MASK), saved.store_row_->get_hash_value());
  EXPECT_TRUE(saved.store_row_->is_match());
  bind_row(0, current_ignored, other, integer(scalar)); bind_row(2, current_ignored, current, integer(different));
  expressions_[2].batch_result_ = false; expressions_[2].batch_idx_mask_ = 0;
  guard_.set_batch_idx(2);
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, saved.equal_distinct(&exprs_, probe, &collations_, &functions_, &eval_, equal, guard_));
  EXPECT_TRUE(equal); EXPECT_EQ(2, eval_.get_batch_idx());
  set_datum(1, text(other), 2);
  ASSERT_EQ(OB_SUCCESS, saved.equal_distinct(&exprs_, probe, &collations_, &functions_, &eval_, equal, guard_));
  EXPECT_FALSE(equal); EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(FixedCompareCells, real_hash_table_resolves_equal_hash_collisions_using_saved_fixed_rows)
{
  add_key(2); add_key(1, true, NULL_LAST, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  int64_t ignored = 0, key = 10;
  std::string first = "customer", second = "other", upper = "CUSTOMER", absent = "absent";
  OwnedFixedRow a({integer(ignored), text(first), integer(key)}, sizeof(ObHashPartStoredRow::ExtraInfo));
  OwnedFixedRow b({integer(ignored), text(second), integer(key)}, sizeof(ObHashPartStoredRow::ExtraInfo));
  a.relocate(); b.relocate(); first.assign(first.size(), 'x'); second.assign(second.size(), 'x');
  ObHashPartCols saved_a, saved_b, duplicate(2);
  saved_a.use_expr_ = false; saved_a.store_row_ = static_cast<ObHashPartStoredRow *>(a.row_);
  saved_b.use_expr_ = false; saved_b.store_row_ = static_cast<ObHashPartStoredRow *>(b.row_);
  ASSERT_EQ(OB_SUCCESS, saved_a.set_hash_value(42)); ASSERT_EQ(OB_SUCCESS, saved_b.set_hash_value(42));
  ObHashPartitionExtendHashTable<ObHashPartCols> table;
  ASSERT_EQ(OB_SUCCESS, table.init(&arena_, 128, []() { return int64_t(1 << 20); }, 128, 1024));
  table.set_funcs(nullptr, &collations_, &functions_, &eval_); table.exprs_ = &exprs_;
  ASSERT_EQ(OB_SUCCESS, table.set_distinct(saved_a, 42));
  const std::string actual_second = "other";
  bind_row(2, ignored, actual_second, integer(key)); guard_.set_batch_idx(2);
  ASSERT_EQ(OB_SUCCESS, table.set_distinct(saved_b, 42)); EXPECT_EQ(2, table.size());
  bind_row(2, ignored, upper, integer(key));
  const ObHashPartCols *found = nullptr;
  ASSERT_EQ(OB_SUCCESS, table.get(42, duplicate, found)); EXPECT_EQ(&saved_a, found);
  EXPECT_EQ(OB_HASH_EXIST, table.set_distinct(duplicate, 42)); EXPECT_EQ(2, table.size());
  bind_row(2, ignored, absent, integer(key)); found = &saved_a;
  ASSERT_EQ(OB_SUCCESS, table.get(42, duplicate, found)); EXPECT_EQ(nullptr, found);
  EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(FixedCompareCells, compare_errors_preserve_flags_borrowed_payloads_and_return_codes)
{
  add_failing_key(0);
  expressions_[0].obj_meta_.set_varchar();
  expressions_[0].obj_meta_.set_collation_type(CS_TYPE_BINARY);
  expressions_[0].obj_datum_map_ = OBJ_DATUM_STRING;
  ASSERT_EQ(OB_SUCCESS, expressions_[0].prepare_scalar_type(
      expressions_[0].obj_meta_, ObAccuracy(), arena_));
  std::string source = "source", current = "current";
  ObDatum src = text(source), input = text(current); src.set_has_lob_header(); input.set_has_lob_header();
  OwnedFixedRow row({src}, sizeof(ObHashPartStoredRow::ExtraInfo)); row.relocate();
  set_datum(0, input, 0); set_datum(0, input, 2);
  ObSEArray<ObExpr *, 1> one;
  ASSERT_EQ(OB_SUCCESS, one.push_back(&expressions_[0]));
  CompareProbe::expect(row.row_->stored_cells()[0], input);
  ObMergeSetOp::Compare set; ASSERT_EQ(OB_SUCCESS, set.init(&collations_, &functions_));
  int cmp = 9;
  EXPECT_EQ(OB_ERR_UNEXPECTED, set(*row.row_, one, eval_, cmp));
  EXPECT_EQ(OB_ERR_UNEXPECTED, set(*row.row_, one, 2, eval_, cmp));
  EXPECT_EQ(0, eval_.get_batch_idx()); EXPECT_EQ(2, CompareProbe::calls);
  ObMergeDistinctOp::Compare distinct; ASSERT_EQ(OB_SUCCESS, distinct.init(&eval_, &functions_));
  // Distinct passes the expression on the left and the stored cell on the right.
  CompareProbe::left = input.ptr_; CompareProbe::left_pack = input.pack_;
  CompareProbe::right = row.row_->stored_cells()[0].ptr_;
  CompareProbe::right_pack = row.row_->stored_cells()[0].pack_;
  bool equal = true;
  EXPECT_EQ(OB_ERR_UNEXPECTED, distinct.equal(&one, row.row_, equal)); EXPECT_FALSE(equal);
  EXPECT_EQ(OB_ERR_UNEXPECTED, distinct.equal_in_batch(&one, row.row_, 2, equal));
  EXPECT_EQ(4, CompareProbe::calls);
  ObHashPartCols saved, probe(2);
  saved.use_expr_ = false; saved.store_row_ = static_cast<ObHashPartStoredRow *>(row.row_);
  CompareProbe::expect(row.row_->stored_cells()[0], input);
  guard_.set_batch_idx(2);
  EXPECT_EQ(OB_ERR_UNEXPECTED, saved.equal_distinct(&one, probe, &collations_, &functions_, &eval_, equal, guard_));
  EXPECT_EQ(1, CompareProbe::calls); EXPECT_EQ(2, eval_.get_batch_idx());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
