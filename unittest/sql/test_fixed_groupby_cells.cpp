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
#include "sql/engine/aggregate/ob_merge_groupby_op.h"
#include "sql/engine/aggregate/ob_hash_groupby_op.h"
#include "lib/charset/ob_charset.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using Row = ObChunkDatumStore::StoredRow;
using Group = ObAggregateProcessor::GroupRow;
using Op = ObMergeGroupByOp;
using Keys = ObSEArray<ObExpr *, 4>;
using Indices = ObSEArray<int64_t, 4>;
template <typename Tag, typename Tag::Type Member>
struct PrivateMember { friend typename Tag::Type member(Tag) { return Member; } };
#define MEMBER_TAG(Name, FieldType, Field) \
  struct Name { using Type = FieldType Op::*; friend Type member(Name); }; \
  template struct PrivateMember<Name, &Op::Field>
MEMBER_TAG(KeysTag, Keys, all_groupby_exprs_);
MEMBER_TAG(MapTag, Indices, distinct_col_idx_in_output_);
MEMBER_TAG(LastTag, ObChunkDatumStore::LastStoredRow, last_child_output_);
MEMBER_TAG(GroupTag, Group *, cur_group_row_);
MEMBER_TAG(FirstTag, bool, is_first_calc_);
MEMBER_TAG(IndexTag, int64_t, cur_group_last_row_idx_);
#undef MEMBER_TAG

ObDatum integer(const int64_t &n)
{ return ObDatum(reinterpret_cast<const char *>(&n), sizeof(n), false); }
ObDatum bytes(const void *p, uint32_t size) { return ObDatum(static_cast<const char *>(p), size, false); }
ObDatum text(const std::string &s) { return bytes(s.data(), s.size()); }
ObDatum null_value() { ObDatum d; d.set_null(); return d; }
int64_t number(const StoredDatumCell &cell)
{
  int64_t value = 0;
  EXPECT_EQ(sizeof(value), cell.len_); EXPECT_FALSE(cell.is_null());
  if (!cell.is_null() && cell.len_ == sizeof(value)) { MEMCPY(&value, cell.ptr_, sizeof(value)); }
  return value;
}
struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values)
  {
    const uint32_t extra = (8 - (sizeof(Row) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size); row_ = new (memory_.data()) Row;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(Row), size, extra));
  }
  void poison() { MEMSET(memory_.data(), 0xA5, memory_.size()); }
  std::vector<char> memory_;
  Row *row_ = nullptr;
};
struct CompareProbe
{
  static int calls;
  static const char *left;
  static const char *right;
  static uint32_t left_pack;
  static uint32_t right_pack;
  static int fail(const ObDatum &l, const ObDatum &r, int &cmp, const ObDatumAccessContext *ctx)
  {
    ++calls;
    EXPECT_EQ(left, l.ptr_); EXPECT_EQ(right, r.ptr_);
    EXPECT_EQ(left_pack, l.pack_); EXPECT_EQ(right_pack, r.pack_); EXPECT_EQ(nullptr, ctx);
    cmp = 0; return OB_ERR_UNEXPECTED;
  }
  static void expect(const StoredDatumCell &l, const ObDatum &r)
  { calls = 0; left = l.ptr_; right = r.ptr_; left_pack = l.pack_; right_pack = r.pack_; }
};
int CompareProbe::calls = 0;
const char *CompareProbe::left = nullptr;
const char *CompareProbe::right = nullptr;
uint32_t CompareProbe::left_pack = 0;
uint32_t CompareProbe::right_pack = 0;

class FixedGroupbyCells : public ::testing::Test
{
protected:
  static constexpr int64_t BATCH = 4;
  FixedGroupbyCells() : arena_(), exec_(arena_), spec_(arena_, PHY_MERGE_GROUP_BY),
      op_(exec_, spec_, nullptr), frame_{}, frames_{frame_.data()},
      eval_(op_.get_eval_ctx()), guard_(eval_), exprs_{} {}
  void SetUp() override
  {
    exec_.set_frames(frames_); eval_.frames_ = frames_; eval_.max_batch_size_ = BATCH;
    guard_.set_batch_size(BATCH); guard_.set_batch_idx(2);
    for (int64_t i = 0; i < 3; ++i) {
      ObExpr &e = exprs_[i]; e.type_ = T_REF_COLUMN; e.frame_idx_ = 0;
      e.datum_off_ = i * 1024; e.eval_info_off_ = i * 1024 + 128;
      e.eval_flags_off_ = i * 1024 + 192; e.res_buf_off_ = i * 1024 + 512; e.res_buf_len_ = 128;
      e.batch_result_ = true; e.batch_idx_mask_ = UINT64_MAX;
      const auto type = i == 0 ? ObVarcharType : i == 2 ? ObDecimalIntType : ObIntType;
      const auto cs = i == 0 ? CS_TYPE_UTF8MB4_GENERAL_CI : CS_TYPE_BINARY;
      e.obj_meta_.set_type(type); e.obj_meta_.set_collation_type(cs);
      if (type == ObDecimalIntType) { e.obj_meta_.set_scale(2); }
      e.datum_meta_ = ObDatumMeta(type, cs, type == ObDecimalIntType ? 2 : 0);
      e.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
      ASSERT_EQ(OB_SUCCESS, e.prepare_scalar_type(e.obj_meta_, e.prepare_legacy_copy_plan().get_accuracy(), arena_));
      for (int64_t j = 0; j < BATCH; ++j) {
        new (frame_.data() + e.datum_off_ + j * sizeof(Datum)) Datum;
      }
      new (frame_.data() + e.eval_info_off_) ObEvalInfo;
      e.get_eval_info(eval_).evaluated_ = true;
      e.get_evaluated_flags(eval_).set_all(BATCH);
    }
  }
  Datum &datum(int64_t i, int64_t row = 2) { return exprs_[i].locate_batch_datums(eval_)[row]; }
  void put(int64_t i, const common::ObDatum &value, int64_t row = 2)
  { ASSERT_EQ(OB_SUCCESS, exprs_[i].get_datum_copy_plan().import_legacy(value, datum(i, row))); }
  Keys &keys() { return op_.*member(KeysTag{}); }
  Indices &mapping() { return op_.*member(MapTag{}); }
  ObChunkDatumStore::LastStoredRow &last() { return op_.*member(LastTag{}); }
  void distinct(Group &group)
  {
    spec_.aggr_code_idx_ = 0;
    ASSERT_EQ(OB_SUCCESS, spec_.dist_aggr_group_idxes_.init(1));
    ASSERT_EQ(OB_SUCCESS, spec_.dist_aggr_group_idxes_.push_back(1));
    ASSERT_EQ(OB_SUCCESS, spec_.distinct_exprs_.init(3));
    for (auto *e : {&exprs_[0], &exprs_[1], &exprs_[2]}) {
      ASSERT_EQ(OB_SUCCESS, spec_.distinct_exprs_.push_back(e));
    }
    for (int64_t i : {1, -1, 0}) { ASSERT_EQ(OB_SUCCESS, mapping().push_back(i)); }
    op_.*member(GroupTag{}) = &group; op_.*member(FirstTag{}) = false; op_.*member(IndexTag{}) = -1;
  }
  void init_hash(ObGroupRowHashTable &table, const ObIArray<ObExpr *> &exprs, const ObIArray<ObCmpFunc> &funcs)
  {
    lib::ObMemAttr attr(ObModIds::OB_HASH_NODE_GROUP_ROWS, ObCtxIds::WORK_AREA);
    ASSERT_EQ(OB_SUCCESS, table.init(&arena_, attr, exprs, &eval_, &funcs, 16));
  }
  void hash_inputs(ObSEArray<ObExpr *, 3> &exprs, ObSEArray<ObCmpFunc, 3> &funcs,
                   std::initializer_list<int64_t> indices)
  {
    for (int64_t i : indices) {
      ASSERT_EQ(OB_SUCCESS, exprs.push_back(&exprs_[i]));
      ObCmpFunc f; f.cmp_func_ = exprs_[i].basic_funcs_->null_first_cmp_.legacy_function();
      ASSERT_EQ(OB_SUCCESS, funcs.push_back(f));
    }
  }
  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObMergeGroupBySpec spec_;
  Op op_;
  alignas(8) std::array<char, 4096> frame_;
  char *frames_[1];
  ObEvalCtx &eval_;
  ObEvalCtx::BatchInfoScopeGuard guard_;
  ObExpr exprs_[3];
};

TEST_F(FixedGroupbyCells, merge_boundary_and_restore_keep_saved_group_spelling_decimal_null_and_flags)
{
  for (int i : {0, 2, 1}) { ASSERT_EQ(OB_SUCCESS, keys().push_back(&exprs_[i])); }
  ASSERT_EQ(OB_SUCCESS, spec_.init_group_exprs(2));
  ASSERT_EQ(OB_SUCCESS, spec_.add_group_expr(&exprs_[0])); ASSERT_EQ(OB_SUCCESS, spec_.add_group_expr(&exprs_[2]));
  ASSERT_EQ(OB_SUCCESS, spec_.init_rollup_exprs(1)); ASSERT_EQ(OB_SUCCESS, spec_.add_rollup_expr(&exprs_[1]));
  std::string source = "Customer", current = "CUSTOMER";
  std::array<char, 16> coefficient{}, expected{}; coefficient.fill(0x11); expected = coefficient;
  OwnedRow row({text(source), bytes(coefficient.data(), coefficient.size()), null_value()});
  ObChunkDatumStore::LastStoredRow saved(arena_);
  ASSERT_EQ(OB_SUCCESS, saved.save_store_row(*row.row_)); row.poison();
  source.assign(source.size(), 'x'); coefficient.fill(0);
  Group group; group.groupby_store_row_ = saved.store_row_;
  put(0, text(current)); put(2, bytes(expected.data(), expected.size())); datum(1).set_null();
  int64_t diff = 99;
  ASSERT_EQ(OB_SUCCESS, op_.check_same_group(&group, diff)); EXPECT_EQ(OB_INVALID_INDEX, diff);
  int64_t n = 17; put(1, integer(n));
  ASSERT_EQ(OB_SUCCESS, op_.check_same_group(&group, diff)); EXPECT_EQ(2, diff);
  for (auto &e : exprs_) { e.get_eval_info(eval_).evaluated_ = false; e.get_eval_info(eval_).projected_ = false; }
  ASSERT_EQ(OB_SUCCESS, op_.restore_groupby_datum(&group, diff));
  EXPECT_EQ("Customer", std::string(datum(0).get_string().ptr(), datum(0).length()));
  EXPECT_EQ(saved.store_row_->stored_cells()[0].ptr_, datum(0).get_string().ptr());
  EXPECT_EQ(16, datum(2).length()); EXPECT_EQ(0, MEMCMP(expected.data(), datum(2).get_string().ptr(), expected.size()));
  EXPECT_TRUE(datum(1).is_null()); EXPECT_EQ(PRECISION_UNKNOWN_YET, exprs_[2].datum_meta_.precision_);
  for (auto &e : exprs_) { EXPECT_TRUE(e.get_eval_info(eval_).evaluated_); EXPECT_TRUE(e.get_eval_info(eval_).projected_); }
  EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(FixedGroupbyCells, scalar_distinct_uses_output_mapping_skips_unmapped_and_keeps_stage_code_readonly)
{
  int64_t code = 0; OwnedRow stage({integer(code)}); Group group; group.groupby_store_row_ = stage.row_;
  const auto stage_bytes = stage.memory_;
  distinct(group);
  std::string source = "customer", current = "CUSTOMER";
  std::array<char, 16> coefficient{}; coefficient.fill(0x22);
  OwnedRow previous({bytes(coefficient.data(), coefficient.size()), text(source)});
  ASSERT_EQ(OB_SUCCESS, last().save_store_row(*previous.row_)); previous.poison();
  source.assign(source.size(), 'x');
  put(0, text(current)); datum(1).set_null(); put(2, bytes(coefficient.data(), coefficient.size()));
  bool same = false; ASSERT_EQ(OB_SUCCESS, op_.check_unique_distinct_columns(&group, same)); EXPECT_TRUE(same);
  EXPECT_EQ(0, number(stage.row_->stored_cells()[0]));
  EXPECT_EQ(stage_bytes, stage.memory_);
  code = 1; OwnedRow non_distinct({integer(code)}); group.groupby_store_row_ = non_distinct.row_;
  ASSERT_EQ(OB_SUCCESS, op_.check_unique_distinct_columns(&group, same)); EXPECT_FALSE(same);
  EXPECT_EQ(1, number(non_distinct.row_->stored_cells()[0])); EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(FixedGroupbyCells, batch_distinct_crosses_saved_boundary_then_uses_batch_and_nonbatch_slots)
{
  int64_t code = 0; OwnedRow stage({integer(code)}); Group group; group.groupby_store_row_ = stage.row_;
  distinct(group);
  std::string source = "customer", current = "CUSTOMER";
  std::array<char, 16> coefficient{}; coefficient.fill(0x22);
  OwnedRow previous({bytes(coefficient.data(), coefficient.size()), text(source)});
  ASSERT_EQ(OB_SUCCESS, last().save_store_row(*previous.row_)); previous.poison();
  put(0, text(current), 2); put(0, text(source), 3);
  put(2, bytes(coefficient.data(), coefficient.size()), 2); datum(2, 3).set_null();
  bool same = false;
  ASSERT_EQ(OB_SUCCESS, op_.check_unique_distinct_columns_for_batch(same, 2)); EXPECT_TRUE(same);
  EXPECT_EQ(2, op_.*member(IndexTag{})); EXPECT_FALSE(op_.*member(FirstTag{}));
  guard_.set_batch_idx(3);
  ASSERT_EQ(OB_SUCCESS, op_.check_unique_distinct_columns_for_batch(same, 3)); EXPECT_FALSE(same);
  exprs_[2].batch_result_ = false; exprs_[2].batch_idx_mask_ = 0;
  put(2, bytes(coefficient.data(), coefficient.size()), 0); op_.*member(IndexTag{}) = 2;
  ASSERT_EQ(OB_SUCCESS, op_.check_unique_distinct_columns_for_batch(same, 3)); EXPECT_TRUE(same);
  op_.*member(FirstTag{}) = true;
  ASSERT_EQ(OB_SUCCESS, op_.check_unique_distinct_columns_for_batch(same, 3)); EXPECT_FALSE(same);
  EXPECT_EQ(3, eval_.get_batch_idx()); EXPECT_EQ(0, number(stage.row_->stored_cells()[0]));
}

TEST_F(FixedGroupbyCells, real_hash_table_resolves_collisions_with_fixed_ci_decimal_and_null_keys)
{
  ObSEArray<ObExpr *, 3> exprs; ObSEArray<ObCmpFunc, 3> funcs; hash_inputs(exprs, funcs, {0, 2, 1});
  std::string a = "customer", b = "other", upper_a = "CUSTOMER", upper_b = "OTHER", absent = "absent";
  std::array<char, 16> coefficient{}; coefficient.fill(0x21);
  OwnedRow first({text(a), bytes(coefficient.data(), coefficient.size()), null_value()});
  OwnedRow second({text(b), bytes(coefficient.data(), coefficient.size()), null_value()});
  Group g1, g2; g1.groupby_store_row_ = first.row_; g2.groupby_store_row_ = second.row_;
  ObGroupRowItem i1, i2, query; i1.group_row_ = &g1; i1.groupby_store_row_ = first.row_;
  i2.group_row_ = &g2; i2.groupby_store_row_ = second.row_;
  i1.hash_ = i2.hash_ = query.hash_ = 42; query.is_expr_row_ = true; query.batch_idx_ = 2;
  ObGroupRowHashTable table; init_hash(table, exprs, funcs);
  ASSERT_EQ(OB_SUCCESS, table.set(i1)); ASSERT_EQ(OB_SUCCESS, table.set(i2));
  a.assign(a.size(), 'x'); b.assign(b.size(), 'x');
  put(2, bytes(coefficient.data(), coefficient.size())); datum(1).set_null(); put(0, text(upper_a));
  EXPECT_EQ(&i1, table.get(query)); put(0, text(upper_b)); EXPECT_EQ(&i2, table.get(query));
  put(0, text(absent)); EXPECT_EQ(nullptr, table.get(query));
  int64_t n = 1; put(0, text(upper_a)); put(1, integer(n)); EXPECT_EQ(nullptr, table.get(query));
  EXPECT_EQ(2, eval_.get_batch_idx());
}

TEST_F(FixedGroupbyCells, hash_expression_side_keeps_nonbatch_index_and_null_shortcuts)
{
  ObSEArray<ObExpr *, 3> exprs; ObSEArray<ObCmpFunc, 3> funcs; hash_inputs(exprs, funcs, {0, 1, 2});
  std::string a = "customer", upper = "CUSTOMER"; int64_t n = 7;
  put(0, text(a), 1); put(0, text(upper), 2);
  exprs_[1].batch_result_ = false; exprs_[1].batch_idx_mask_ = 0; put(1, integer(n), 0);
  datum(2, 1).set_null(); datum(2, 2).set_null();
  ObGroupRowItem saved, query; saved.is_expr_row_ = query.is_expr_row_ = true;
  saved.batch_idx_ = 1; query.batch_idx_ = 2; saved.hash_ = query.hash_ = 42;
  ObGroupRowHashTable table; init_hash(table, exprs, funcs); ASSERT_EQ(OB_SUCCESS, table.set(saved));
  guard_.set_batch_idx(3); EXPECT_EQ(&saved, table.get(query));
  datum(0, 1).set_null(); EXPECT_EQ(nullptr, table.get(query)); EXPECT_EQ(3, eval_.get_batch_idx());
}

TEST_F(FixedGroupbyCells, real_consumers_keep_comparison_error_context_flags_and_payload_borrows)
{
  std::string a = "stored", b = "inputx";
  ObDatum stored = text(a), current = text(b); stored.set_has_lob_header(); current.set_has_lob_header();
  OwnedRow row({stored}); Group group; group.groupby_store_row_ = row.row_;
  ASSERT_EQ(OB_SUCCESS, keys().push_back(&exprs_[0])); put(0, current);
  DatumBasicFuncs functions = *exprs_[0].basic_funcs_;
  ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::bind_legacy_cmp(exprs_[0].get_sql_type(),
      exprs_[0].get_sql_type(), CompareProbe::fail, functions.null_first_cmp_));
  exprs_[0].basic_funcs_ = &functions; CompareProbe::expect(row.row_->stored_cells()[0], current);
  int64_t diff = 99; EXPECT_EQ(OB_ERR_UNEXPECTED, op_.check_same_group(&group, diff));
  EXPECT_EQ(1, CompareProbe::calls); EXPECT_EQ(OB_INVALID_INDEX, diff);
  ObSEArray<ObExpr *, 3> exprs; ObSEArray<ObCmpFunc, 3> funcs; hash_inputs(exprs, funcs, {0});
  ObGroupRowHashTable table; init_hash(table, exprs, funcs);
  ObGroupRowItem saved, query; saved.group_row_ = &group; saved.groupby_store_row_ = row.row_;
  saved.hash_ = query.hash_ = 42; query.is_expr_row_ = true; query.batch_idx_ = 2;
  ASSERT_EQ(OB_SUCCESS, table.set(saved)); CompareProbe::expect(row.row_->stored_cells()[0], current);
  EXPECT_EQ(nullptr, table.get(query)); EXPECT_EQ(1, CompareProbe::calls);
  EXPECT_EQ(CompareProbe::left, row.row_->stored_cells()[0].ptr_); EXPECT_EQ(current.ptr_, datum(0).get_string().ptr());
  EXPECT_EQ(stored.pack_, row.row_->stored_cells()[0].pack_); EXPECT_EQ(2, eval_.get_batch_idx());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
