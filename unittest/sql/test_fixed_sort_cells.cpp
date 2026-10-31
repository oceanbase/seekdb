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

#include "sql/engine/sort/ob_sort_op_impl.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase { namespace sql {
// Bind real consumer methods to already-built fixed rows. This deliberately
// avoids work-area admission and file-service setup; comparison, prefix checks,
// unique iteration, row copying and expression output themselves are real.
class FixedSortCellsTestAccess
{
public:
  static void partition(ObSortOpImpl &sort, const ObIArray<ObSortFieldCollation> &collations,
                        const ObIArray<ObSortCmpFunc> &functions, int64_t count)
  {
    sort.sort_collations_ = &collations;
    sort.sort_cmp_funs_ = &functions;
    sort.part_cnt_ = count;
  }
  static int equal_partition(ObSortOpImpl &sort, const ObChunkDatumStore::StoredRow *a,
                             const ObChunkDatumStore::StoredRow *b, bool &same)
  { return sort.is_equal_part(a, b, same); }

  static void prefix(ObPrefixSortImpl &sort, const ObIArray<ObSortFieldCollation> &collations,
                     const ObIArray<ObSortCmpFunc> &functions, int64_t count, ObEvalCtx &eval)
  {
    sort.full_sort_collations_ = &collations;
    sort.full_sort_cmp_funs_ = &functions;
    sort.prefix_pos_ = count;
    sort.eval_ctx_ = &eval;
  }
  static int same_prefix(ObPrefixSortImpl &sort, const ObChunkDatumStore::StoredRow *row,
                         const ObIArray<ObExpr *> &expressions, int64_t index, bool &same)
  { return sort.is_same_prefix(row, expressions, index, same); }

  static int consumer(ObSortOpImpl &sort, ObIArray<ObChunkDatumStore::StoredRow *> &rows,
                       const ObIArray<ObSortFieldCollation> &collations,
                       const ObIArray<ObSortCmpFunc> &functions, ObEvalCtx &eval, ObExecContext &exec,
                       bool compact_single_row_supply = false)
  {
    int ret = OB_SUCCESS;
    lib::ContextParam param;
    param.set_mem_attr("FixedSortTest").set_properties(lib::USE_TL_PAGE_OPTIONAL);
    if (OB_FAIL(CURRENT_CONTEXT->CREATE_CONTEXT(sort.mem_context_, param))) {
    } else if (OB_FAIL(sort.comp_.init(&collations, &functions, &exec, false))) {
    } else if (OB_ISNULL(sort.stored_rows_ = static_cast<ObChunkDatumStore::StoredRow **>(
          sort.mem_context_->get_malloc_allocator().alloc(4 * sizeof(*sort.stored_rows_))))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      sort.sort_collations_ = &collations;
      sort.sort_cmp_funs_ = &functions;
      sort.eval_ctx_ = &eval;
      sort.exec_ctx_ = &exec;
      sort.rows_ = &rows;
      sort.need_rewind_ = true;
      // Normal init() sets the format explicitly. The default constructor
      // alone does not establish it, so the narrow fixture must do so too.
      sort.use_compact_format_ = compact_single_row_supply;
      sort.compress_type_ = NONE_COMPRESSOR;
      sort.inited_ = true;
    }
    return ret;
  }
  static int64_t consumed_rows(const ObSortOpImpl &sort) { return sort.row_idx_; }
};
} }

namespace
{
using FixedRow = ObChunkDatumStore::StoredRow;

ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum unsigned_integer(const uint64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum text(const std::string &value)
{ return ObDatum(value.data(), static_cast<uint32_t>(value.size()), false); }
ObDatum null_value()
{ ObDatum value; value.set_null(); return value; }

struct OwnedFixedRow
{
  explicit OwnedFixedRow(const std::vector<ObDatum> &values)
  {
    // Align scalar payloads without assuming that the packed cell array has
    // execution-Datum stride or alignment. Extra bytes are opaque to sort.
    const uint32_t extra = (8 - (sizeof(FixedRow) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    bytes_.resize(size);
    row_ = new (bytes_.data()) FixedRow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(FixedRow), size, extra));
  }
  std::vector<char> bytes_;
  FixedRow *row_;
};

int64_t read_integer(const Datum &value)
{ EXPECT_FALSE(value.is_null()); return value.is_null() ? 0 : value.get_int(); }
int64_t read_integer(const ObDatum &value)
{
  int64_t output = 0;
  EXPECT_FALSE(value.is_null());
  EXPECT_EQ(sizeof(output), value.len_);
  if (!value.is_null() && value.len_ == sizeof(output)) { MEMCPY(&output, value.ptr_, sizeof(output)); }
  return output;
}
int64_t row_integer(const FixedRow &row, int64_t column)
{ return read_integer(StoredDatumCodec::decode_legacy_view(row.stored_cells()[column])); }

class FixedSortCells : public ::testing::Test
{
protected:
  FixedSortCells() : arena_(), exec_(arena_), frame_{}, frames_{frame_.data()}, eval_(exec_), expressions_{} {}
  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    for (int64_t column = 0; column < 3; ++column) {
      ObExpr &expr = expressions_[column];
      expr.frame_idx_ = 0;
      expr.datum_off_ = column * 512;
      expr.eval_info_off_ = column * 512 + 128;
      expr.res_buf_off_ = column * 512 + 192;
      expr.res_buf_len_ = 64;
      expr.type_ = T_COLUMN_REF;
      expr.obj_meta_.set_int();
      expr.datum_meta_.type_ = ObIntType;
      expr.obj_datum_map_ = OBJ_DATUM_8BYTE_DATA;
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
      for (int64_t row = 0; row < 4; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      ASSERT_EQ(OB_SUCCESS, exprs_.push_back(&expr));
    }
  }
  void add_key(uint32_t column, bool ascending = true, ObCmpNullPos null_position = NULL_LAST,
               SqlValueType type = ObIntType, ObCollationType collation = CS_TYPE_BINARY)
  {
    ASSERT_EQ(OB_SUCCESS, collations_.push_back(ObSortFieldCollation(column, collation, ascending, null_position)));
    ObSortCmpFunc function;
    function.cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(type, type, null_position, collation,
                                                          SCALE_UNKNOWN_YET, false);
    ASSERT_NE(nullptr, function.cmp_func_);
    ASSERT_EQ(OB_SUCCESS, functions_.push_back(function));
  }
  void bind(ObSortOpImpl::Compare &compare, bool encoded = false)
  { ASSERT_EQ(OB_SUCCESS, compare.init(&collations_, &functions_, &exec_, encoded)); }
  void batch_expressions()
  {
    eval_.max_batch_size_ = 4;
    for (auto &expr : expressions_) { expr.batch_result_ = true; expr.batch_idx_mask_ = UINT64_MAX; }
  }
  Datum &datum(int64_t column, int64_t index = 0)
  { return expressions_[column].locate_batch_datums(eval_)[index]; }
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


  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 2048> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObExpr expressions_[3];
  ObSEArray<ObExpr *, 3> exprs_;
  ObSEArray<ObSortFieldCollation, 3> collations_;
  ObSEArray<ObSortCmpFunc, 3> functions_;
};

TEST_F(FixedSortCells, in_memory_comparison_uses_mapped_columns_and_existing_direction)
{
  add_key(2, false); add_key(0, true);
  OwnedFixedRow a({integer(9), text("ignored!"), integer(INT64_MIN)});
  OwnedFixedRow b({integer(1), text("ignored!"), integer(INT64_MAX)});
  OwnedFixedRow c({integer(2), text("ignored!"), integer(INT64_MAX)});
  ObSortOpImpl::Compare compare; bind(compare);
  std::vector<FixedRow *> rows{a.row_, c.row_, b.row_};
  std::sort(rows.begin(), rows.end(), [&compare](const FixedRow *l, const FixedRow *r) { return compare(l, r); });
  EXPECT_EQ(b.row_, rows[0]); EXPECT_EQ(c.row_, rows[1]); EXPECT_EQ(a.row_, rows[2]);
  EXPECT_EQ(OB_SUCCESS, compare.ret_);
  compare.set_cmp_range(1, 2);
  EXPECT_TRUE(compare(b.row_, c.row_));
  EXPECT_GT(compare.with_ties_cmp(b.row_, c.row_), 0); // ascending inverts the tie comparison
}

TEST_F(FixedSortCells, bound_null_order_and_direction_are_preserved)
{
  OwnedFixedRow n({null_value()}), low({integer(-1)}), high({integer(1)});
  for (bool ascending : {false, true}) {
    for (ObCmpNullPos null_position : {NULL_FIRST, NULL_LAST}) {
      collations_.reuse(); functions_.reuse(); add_key(0, ascending, null_position);
      ObSortOpImpl::Compare compare; bind(compare);
      std::vector<FixedRow *> rows{high.row_, n.row_, low.row_};
      std::sort(rows.begin(), rows.end(), [&compare](const FixedRow *l, const FixedRow *r) { return compare(l, r); });
      const bool null_at_front = (null_position == NULL_FIRST) == ascending;
      EXPECT_EQ(n.row_, rows[null_at_front ? 0 : 2]);
      EXPECT_EQ(ascending ? low.row_ : high.row_, rows[null_at_front ? 1 : 0]);
      EXPECT_FALSE(compare(n.row_, n.row_));
      EXPECT_EQ(OB_SUCCESS, compare.ret_);
    }
  }
}

TEST_F(FixedSortCells, expression_and_with_ties_compare_borrow_only_one_decoded_cell)
{
  add_key(2, true); add_key(0, false);
  OwnedFixedRow row({integer(7), text("ignored!"), integer(19)});
  int64_t first = 9, last = 19;
  set_datum(0, integer(first)); set_datum(2, integer(last));
  ObSortOpImpl::Compare compare; bind(compare);
  EXPECT_TRUE(compare(&exprs_, row.row_, eval_));
  EXPECT_GT(compare.with_ties_cmp(&exprs_, row.row_, eval_), 0);
  first = 7;
  set_datum(0, integer(first));
  EXPECT_FALSE(compare(&exprs_, row.row_, eval_));
  EXPECT_EQ(0, compare.with_ties_cmp(&exprs_, row.row_, eval_));
  EXPECT_EQ(OB_SUCCESS, compare.ret_);
}

TEST_F(FixedSortCells, encoded_bytes_and_adaptive_sort_keep_the_raw_fast_path)
{
  add_key(0, false, NULL_LAST, ObVarcharType);
  OwnedFixedRow prefix({text("a")}), zero({text(std::string("a\0z", 3))}), longer({text("ab")}), last({text("b")});
  ObSortOpImpl::Compare compare; bind(compare, true);
  EXPECT_TRUE(compare(prefix.row_, zero.row_));
  EXPECT_TRUE(compare(zero.row_, longer.row_));
  EXPECT_TRUE(compare(longer.row_, last.row_)); // descending is already encoded in the key bytes
  ObSEArray<FixedRow *, 4> rows;
  for (auto *row : {last.row_, longer.row_, prefix.row_, zero.row_}) { ASSERT_EQ(OB_SUCCESS, rows.push_back(row)); }
  ObSortOpImpl::ObAdaptiveQS adaptive(rows, arena_);
  bool can_encode = false;
  ASSERT_EQ(OB_SUCCESS, adaptive.init(rows, arena_, 0, rows.count(), can_encode));
  ASSERT_TRUE(can_encode);
  adaptive.sort(0, rows.count());
  EXPECT_EQ(prefix.row_, rows.at(0)); EXPECT_EQ(zero.row_, rows.at(1));
  EXPECT_EQ(longer.row_, rows.at(2)); EXPECT_EQ(last.row_, rows.at(3));
  OwnedFixedRow null({null_value()});
  ASSERT_EQ(OB_SUCCESS, rows.push_back(null.row_));
  adaptive.reset();
  ASSERT_EQ(OB_SUCCESS, adaptive.init(rows, arena_, 0, rows.count(), can_encode));
  EXPECT_FALSE(can_encode);
}

TEST_F(FixedSortCells, partition_hash_and_collation_fallback_read_fixed_cells)
{
  add_key(2); add_key(0); add_key(1, true, NULL_LAST, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  OwnedFixedRow a({integer(5), text("customer"), unsigned_integer(17)});
  OwnedFixedRow same({integer(5), text("CUSTOMER"), unsigned_integer(17)});
  OwnedFixedRow collision({integer(6), text("customer"), unsigned_integer(17)});
  OwnedFixedRow other_hash({integer(5), text("customer"), unsigned_integer(19)});
  ObSortOpImpl sort;
  FixedSortCellsTestAccess::partition(sort, collations_, functions_, 2);
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::equal_partition(sort, a.row_, same.row_, equal)); EXPECT_TRUE(equal);
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::equal_partition(sort, a.row_, collision.row_, equal)); EXPECT_FALSE(equal);
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::equal_partition(sort, a.row_, other_hash.row_, equal)); EXPECT_FALSE(equal);
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::equal_partition(sort, nullptr, a.row_, equal)); EXPECT_FALSE(equal);
}

TEST_F(FixedSortCells, prefix_checks_the_fixed_row_against_batch_expression_values)
{
  add_key(2); add_key(0);
  batch_expressions();
  ObMonitorNode monitor;
  ObPrefixSortImpl prefix(monitor);
  FixedSortCellsTestAccess::prefix(prefix, collations_, functions_, 2, eval_);
  OwnedFixedRow row({integer(-7), text("ignored!"), null_value()});
  int64_t same = -7, different = -8;
  datum(2, 0).set_null(); set_datum(0, integer(same), 0);
  datum(2, 1).set_null(); set_datum(0, integer(different), 1);
  set_datum(2, integer(same), 2); set_datum(0, integer(same), 2);
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::same_prefix(prefix, row.row_, exprs_, 0, equal)); EXPECT_TRUE(equal);
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::same_prefix(prefix, row.row_, exprs_, 1, equal)); EXPECT_FALSE(equal);
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::same_prefix(prefix, row.row_, exprs_, 2, equal)); EXPECT_FALSE(equal);
}

TEST_F(FixedSortCells, unique_stored_row_iteration_preserves_null_and_scalar_keys)
{
  add_key(0);
  OwnedFixedRow n({null_value()}), one({integer(1)}), two({integer(2)});
  ObSEArray<FixedRow *, 8> rows;
  for (auto *row : {n.row_, n.row_, one.row_, one.row_, two.row_}) { ASSERT_EQ(OB_SUCCESS, rows.push_back(row)); }
  ObUniqueSortImpl unique;
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::consumer(unique, rows, collations_, functions_, eval_, exec_));
  const FixedRow *next = nullptr;
  ASSERT_EQ(OB_SUCCESS, unique.get_next_stored_row(next)); EXPECT_TRUE(next->stored_cells()[0].is_null());
  ASSERT_EQ(OB_SUCCESS, unique.get_next_stored_row(next)); EXPECT_EQ(1, row_integer(*next, 0));
  ASSERT_EQ(OB_SUCCESS, unique.get_next_stored_row(next)); EXPECT_EQ(2, row_integer(*next, 0));
  EXPECT_EQ(OB_ITER_END, unique.get_next_stored_row(next));
}

TEST_F(FixedSortCells, unique_batch_compares_previous_saved_row_across_batch_boundaries)
{
  add_key(0, true, NULL_FIRST); batch_expressions();
  OwnedFixedRow n({null_value()}), one({integer(1)}), two({integer(2)}), three({integer(3)});
  ObSEArray<FixedRow *, 8> rows;
  for (auto *row : {n.row_, n.row_, one.row_, one.row_, one.row_, two.row_, two.row_, three.row_}) {
    ASSERT_EQ(OB_SUCCESS, rows.push_back(row));
  }
  ObSEArray<ObExpr *, 1> output; ASSERT_EQ(OB_SUCCESS, output.push_back(&expressions_[0]));
  RecordProperty("reader_modes", "noncompact:max3;compact-single-row-supply:max1");
  RecordProperty("noncompact_input_batches", "NULL,NULL,1|1,1,2|2,3");
  for (bool compact_single_row_supply : {false, true}) {
    SCOPED_TRACE(compact_single_row_supply ? "compact single-row supply" : "noncompact max3 supply");
    ObUniqueSortImpl unique;
    ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::consumer(unique, rows, collations_, functions_,
        eval_, exec_, compact_single_row_supply));
    std::vector<std::string> actual;
    std::vector<int64_t> batch_counts, consumed;
    bool ended = false;
    // max_cnt bounds raw rows per read, not distinct output. Compact mode
    // further restricts that read to one row; duplicate-only reads are skipped
    // internally. This uses the real gate/consumer, not a compact file writer.
    for (int64_t call = 0; call <= rows.count(); ++call) {
      int64_t count = 0;
      const int ret = unique.get_next_batch(output, 3, count);
      if (ret == OB_ITER_END) { ended = true; break; }
      ASSERT_EQ(OB_SUCCESS, ret);
      ASSERT_GT(count, 0);
      ASSERT_LE(count, 3);
      batch_counts.push_back(count);
      consumed.push_back(FixedSortCellsTestAccess::consumed_rows(unique));
      for (int64_t i = 0; i < count; ++i) {
        const Datum &value = datum(0, i);
        actual.push_back(value.is_null() ? "NULL" : std::to_string(read_integer(value)));
      }
    }
    EXPECT_TRUE(ended);
    const std::vector<std::string> expected{"NULL", "1", "2", "3"};
    EXPECT_EQ(expected, actual); // complete, ordered oracle; no duplicate or omitted value
    EXPECT_EQ(rows.count(), FixedSortCellsTestAccess::consumed_rows(unique));
    // Noncompact boundaries repeat 1 across the first/second raw batch and 2
    // across the second/third. Compact supply also repeats NULL across calls,
    // then consumes duplicate-only reads before publishing 1, 2 and 3.
    const std::vector<int64_t> expected_counts = compact_single_row_supply
        ? std::vector<int64_t>{1, 1, 1, 1} : std::vector<int64_t>{2, 1, 1};
    const std::vector<int64_t> expected_consumed = compact_single_row_supply
        ? std::vector<int64_t>{1, 3, 6, 8} : std::vector<int64_t>{3, 6, 8};
    EXPECT_EQ(expected_counts, batch_counts);
    EXPECT_EQ(expected_consumed, consumed);
  }
}

TEST_F(FixedSortCells, unique_scalar_expression_output_consumes_the_same_fixed_rows)
{
  add_key(0);
  OwnedFixedRow one({integer(1)}), two({integer(2)});
  ObSEArray<FixedRow *, 4> rows;
  for (auto *row : {one.row_, one.row_, two.row_, two.row_}) { ASSERT_EQ(OB_SUCCESS, rows.push_back(row)); }
  ObSEArray<ObExpr *, 1> output; ASSERT_EQ(OB_SUCCESS, output.push_back(&expressions_[0]));
  ObUniqueSortImpl unique;
  ASSERT_EQ(OB_SUCCESS, FixedSortCellsTestAccess::consumer(unique, rows, collations_, functions_, eval_, exec_));
  ASSERT_EQ(OB_SUCCESS, unique.get_next_row(output)); EXPECT_EQ(1, read_integer(datum(0)));
  ASSERT_EQ(OB_SUCCESS, unique.get_next_row(output)); EXPECT_EQ(2, read_integer(datum(0)));
  EXPECT_EQ(OB_ITER_END, unique.get_next_row(output));
}

TEST_F(FixedSortCells, spill_relocation_reaches_the_real_external_and_inmemory_merge_comparators)
{
  add_key(0); add_key(1, true, NULL_LAST, ObVarcharType);
  OwnedFixedRow first({integer(INT64_MIN), text("a")}), second({integer(INT64_MAX), text("b")});
  first.row_->unswizzling();
  std::vector<char> readback(first.bytes_);
  MEMSET(first.bytes_.data(), 0xA5, first.bytes_.size());
  auto *restored = reinterpret_cast<FixedRow *>(readback.data());
  restored->swizzling();
  ObSortOpImpl::Compare compare; bind(compare);
  EXPECT_TRUE(compare(restored, second.row_));
  EXPECT_EQ(INT64_MIN, row_integer(*restored, 0));
  ObSortOpChunk left(0), right(0);
  left.row_ = restored; right.row_ = second.row_;
  EXPECT_FALSE(compare(&left, &right)); EXPECT_TRUE(compare(&right, &left));
  FixedRow *lptr = restored, *rptr = second.row_;
  EXPECT_FALSE(compare(&lptr, &rptr)); EXPECT_TRUE(compare(&rptr, &lptr));
  EXPECT_EQ(OB_SUCCESS, compare.ret_);
}

int observed_left_flag = -1, observed_right_flag = -1;
const ObDatumAccessContext *observed_access = nullptr;
int observe_lob_flags(const ObDatum &left, const ObDatum &right, int &, const ObDatumAccessContext *access)
{
  observed_left_flag = left.flag_; observed_right_flag = right.flag_; observed_access = access;
  return OB_NOT_SUPPORTED;
}

TEST_F(FixedSortCells, legacy_lob_flags_context_and_error_ordering_are_preserved)
{
  add_key(0);
  functions_.at(0).cmp_func_ = observe_lob_flags;
  const std::string left = "left", right = "right";
  ObDatum outrow = text(left), header = text(right);
  outrow.set_outrow(); header.set_has_lob_header();
  OwnedFixedRow a({outrow}), b({header});
  ObSortOpImpl::Compare compare; bind(compare);
  EXPECT_FALSE(compare(a.row_, b.row_));
  EXPECT_EQ(OB_NOT_SUPPORTED, compare.ret_);
  EXPECT_EQ(ObDatumDesc::OUTROW, observed_left_flag); EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, observed_right_flag);
  EXPECT_EQ(compare.access_ctx_, observed_access);
  EXPECT_EQ(a.row_ < b.row_, compare(a.row_, b.row_));
  EXPECT_EQ(b.row_ < a.row_, compare(b.row_, a.row_));
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
