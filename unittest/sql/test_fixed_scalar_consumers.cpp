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

#include "sql/engine/basic/ob_limit_op.h"
#include "sql/engine/px/exchange/ob_px_ms_receive_op.h"
#include "sql/engine/recursive_cte/ob_search_method_op.h"
#include "sql/engine/dml/ob_table_replace_op.h"
#include "sql/das/iter/ob_das_group_fold_iter.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
struct FixedPxReceiveCompareTestAccess
{
  using Compare = ObPxMSReceiveOp::Compare;
};
} // namespace sql
} // namespace oceanbase

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

struct OwnedFixedRow
{
  explicit OwnedFixedRow(const std::vector<ObDatum> &values)
  {
    const uint32_t extra = (8 - (sizeof(FixedRow) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) FixedRow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(FixedRow), size, extra));
  }
  void relocate()
  {
    const void *old = row_;
    row_->unswizzling(row_->payload_);
    std::vector<char> moved(memory_);
    MEMSET(memory_.data(), 0xA5, memory_.size());
    memory_.swap(moved);
    row_ = reinterpret_cast<FixedRow *>(memory_.data());
    EXPECT_NE(old, row_);
    row_->swizzling(row_->payload_);
  }
  std::vector<char> memory_;
  FixedRow *row_ = nullptr;
};

// Reach the existing private LIMIT comparison boundary without adding a
// production friend or replacing the consumer's comparison logic.
template <typename Tag, typename Tag::Type Member>
struct PrivateMember
{
  friend typename Tag::Type member(Tag) { return Member; }
};
struct LimitRow
{
  using Type = ObChunkDatumStore::LastStoredRow ObLimitOp::*;
  friend Type member(LimitRow);
};
template struct PrivateMember<LimitRow, &ObLimitOp::pre_sort_columns_>;
struct LimitScalar
{
  using Type = int (ObLimitOp::*)(bool &);
  friend Type member(LimitScalar);
};
template struct PrivateMember<LimitScalar, &ObLimitOp::is_row_order_by_item_value_equal>;
struct LimitBatch
{
  using Type = int (ObLimitOp::*)(bool &, const ObBitVector &, int64_t, uint32_t &);
  friend Type member(LimitBatch);
};
template struct PrivateMember<LimitBatch, &ObLimitOp::compare_value_in_batch>;

class ReplaceConsumer final : public ObTableReplaceOp
{
public:
  using ObTableReplaceOp::ObTableReplaceOp;
  using ObTableReplaceOp::check_values;
};

struct CompareProbe
{
  static const char *left;
  static const char *right;
  static uint32_t left_pack;
  static uint32_t right_pack;
  static int calls;
  static int fail(const ObDatum &l, const ObDatum &r, int &result,
                  const ObDatumAccessContext *context)
  {
    ++calls;
    EXPECT_EQ(left, l.ptr_); EXPECT_EQ(right, r.ptr_);
    EXPECT_EQ(left_pack, l.pack_); EXPECT_EQ(right_pack, r.pack_);
    EXPECT_EQ(nullptr, context);
    result = 0;
    return OB_TIMEOUT;
  }
  static void expect(const StoredDatumCell &l, const ObDatum &r)
  { left = l.ptr_; right = r.ptr_; left_pack = l.pack_; right_pack = r.pack_; calls = 0; }
};
const char *CompareProbe::left = nullptr;
const char *CompareProbe::right = nullptr;
uint32_t CompareProbe::left_pack = 0;
uint32_t CompareProbe::right_pack = 0;
int CompareProbe::calls = 0;

class FixedScalarConsumers : public ::testing::Test
{
protected:
  static constexpr int64_t COLS = 4;
  static constexpr int64_t BATCH = 4;
  FixedScalarConsumers() : arena_(), exec_(arena_), session_(), frame_{}, frames_{frame_.data()},
      eval_(exec_), guard_(eval_), expressions_{}, functions_{} {}
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    exec_.set_my_session(&session_);
    exec_.set_frames(frames_);
    prepare_eval(eval_);
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
      set_type(col, ObIntType);
      expr.basic_funcs_ = &functions_[col];
      for (int64_t row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      expr.get_eval_info(eval_).evaluated_ = true;
      expr.get_evaluated_flags(eval_).set_all(BATCH);
      ASSERT_EQ(OB_SUCCESS, exprs_.push_back(&expr));
    }
  }
  void prepare_eval(ObEvalCtx &eval)
  { eval.frames_ = frames_; eval.max_batch_size_ = BATCH; }
  void set_type(int64_t col, SqlValueType type, ObCollationType collation = CS_TYPE_BINARY)
  {
    ObExpr &expr = expressions_[col];
    expr.obj_meta_.set_type(type);
    expr.obj_meta_.set_collation_type(collation);
    expr.datum_meta_ = ObDatumMeta(type, collation, 0);
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
    ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
    ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(expr.get_sql_type(), functions_[col]));
    expr.basic_funcs_ = &functions_[col];
  }
  void set_comparator(int64_t col, ObDatumCmpFuncType selected)
  {
    const SqlType &type = expressions_[col].get_sql_type();
    ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::bind_legacy_cmp(type, type, selected,
        functions_[col].null_first_cmp_));
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
  alignas(8) char legacy_scratch_[4][4][64]{};

  void limit_columns(ObLimitSpec &spec, int64_t count)
  {
    ASSERT_EQ(OB_SUCCESS, spec.sort_columns_.init(count));
    for (int64_t col = 0; col < count; ++col) {
      ASSERT_EQ(OB_SUCCESS, spec.sort_columns_.push_back(&expressions_[col]));
    }
  }
  void replace_columns(ObTableReplaceSpec &spec, ObInsCtDef &ins, ObDelCtDef &del,
                         ObReplaceCtDef &replace, ObReplaceCtDef *&pointer)
  {
    replace.ins_ctdef_ = &ins; replace.del_ctdef_ = &del; pointer = &replace;
    ASSERT_EQ(OB_SUCCESS, spec.replace_ctdefs_.assign(ObArrayWrap<ObReplaceCtDef *>(&pointer, 1)));
    ASSERT_EQ(OB_SUCCESS, spec.all_saved_exprs_.init(COLS));
    ASSERT_EQ(OB_SUCCESS, ins.new_row_.init(COLS));
    ASSERT_EQ(OB_SUCCESS, del.old_row_.init(COLS));
    ASSERT_EQ(OB_SUCCESS, ins.column_ids_.init(COLS));
    for (int64_t col = 0; col < COLS; ++col) {
      ASSERT_EQ(OB_SUCCESS, spec.all_saved_exprs_.push_back(&expressions_[col]));
      ASSERT_EQ(OB_SUCCESS, ins.new_row_.push_back(&expressions_[col]));
      ASSERT_EQ(OB_SUCCESS, del.old_row_.push_back(&expressions_[col]));
      ASSERT_EQ(OB_SUCCESS, ins.column_ids_.push_back(col == 0 ? OB_HIDDEN_PK_INCREMENT_COLUMN_ID : 32 + col));
    }
  }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObSQLSessionInfo session_;
  alignas(8) std::array<char, 4096> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard guard_;
  ObExpr expressions_[COLS];
  DatumBasicFuncs functions_[COLS];
  ObSEArray<ObExpr *, COLS> exprs_;
};

TEST_F(FixedScalarConsumers, limit_scalar_ties_read_owned_relocated_cells_and_keep_null_collation)
{
  set_type(1, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  ObLimitSpec spec(arena_, PHY_LIMIT); limit_columns(spec, 2);
  ObLimitOp limit(exec_, spec, nullptr); prepare_eval(limit.get_eval_ctx());
  int64_t key = INT64_MIN, current = INT64_MIN;
  std::string source = "alpha", upper = "ALPHA", other = "beta";
  OwnedFixedRow saved({integer(key), text(source)});
  key = 0; source.assign(source.size(), 'x'); saved.relocate();
  (limit.*member(LimitRow{})).store_row_ = saved.row_;
  set_datum(0, integer(current)); set_datum(1, text(upper));
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, (limit.*member(LimitScalar{}))(equal)); EXPECT_TRUE(equal);
  set_datum(1, text(other));
  ASSERT_EQ(OB_SUCCESS, (limit.*member(LimitScalar{}))(equal)); EXPECT_FALSE(equal);
  OwnedFixedRow nulls({null_value(), null_value()}); nulls.relocate();
  (limit.*member(LimitRow{})).store_row_ = nulls.row_;
  set_datum(0, null_value()); set_datum(1, null_value());
  ASSERT_EQ(OB_SUCCESS, (limit.*member(LimitScalar{}))(equal)); EXPECT_TRUE(equal);
  set_datum(0, integer(current));
  ASSERT_EQ(OB_SUCCESS, (limit.*member(LimitScalar{}))(equal)); EXPECT_FALSE(equal);
}

TEST_F(FixedScalarConsumers, limit_constant_order_needs_no_saved_row)
{
  ObLimitSpec spec(arena_, PHY_LIMIT);
  ObLimitOp limit(exec_, spec, nullptr); prepare_eval(limit.get_eval_ctx());
  bool equal = false;
  EXPECT_EQ(OB_SUCCESS, (limit.*member(LimitScalar{}))(equal)); EXPECT_TRUE(equal);
  uint32_t matched = 7;
  bool keep = false;
  auto *skip = static_cast<ObBitVector *>(arena_.alloc(ObBitVector::memory_size(BATCH)));
  skip->reset(BATCH);
  EXPECT_EQ(OB_SUCCESS, (limit.*member(LimitBatch{}))(keep, *skip, BATCH, matched));
  EXPECT_TRUE(keep); EXPECT_EQ(7, matched);
}

TEST_F(FixedScalarConsumers, limit_batch_skips_rows_and_stops_at_first_non_tie)
{
  set_type(1, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  ObLimitSpec spec(arena_, PHY_LIMIT); limit_columns(spec, 2);
  ObLimitOp limit(exec_, spec, nullptr); prepare_eval(limit.get_eval_ctx());
  ObEvalCtx::BatchInfoScopeGuard scope(limit.get_eval_ctx()); scope.set_batch_size(BATCH); scope.set_batch_idx(1);
  int64_t low = INT64_MIN, high = INT64_MAX;
  std::string lower = "alpha", upper = "ALPHA", other = "beta";
  OwnedFixedRow saved({integer(low), text(lower)}); saved.relocate();
  (limit.*member(LimitRow{})).store_row_ = saved.row_;
  for (int64_t row = 0; row < BATCH; ++row) {
    set_datum(0, integer(row == 1 ? high : low), row);
    set_datum(1, text(row == 3 ? other : upper), row);
  }
  auto *skip = static_cast<ObBitVector *>(arena_.alloc(ObBitVector::memory_size(BATCH)));
  skip->reset(BATCH); skip->set(1);
  bool keep = true; uint32_t matched = 5;
  ASSERT_EQ(OB_SUCCESS, (limit.*member(LimitBatch{}))(keep, *skip, BATCH, matched));
  EXPECT_FALSE(keep); EXPECT_EQ(7, matched);
  EXPECT_EQ(1, limit.get_eval_ctx().get_batch_idx());
}

TEST_F(FixedScalarConsumers, limit_scalar_and_batch_keep_comparison_error_and_payload_flags)
{
  set_type(0, ObVarcharType);
  ObLimitSpec spec(arena_, PHY_LIMIT); limit_columns(spec, 1);
  ObLimitOp limit(exec_, spec, nullptr); prepare_eval(limit.get_eval_ctx());
  ObEvalCtx::BatchInfoScopeGuard scope(limit.get_eval_ctx()); scope.set_batch_size(BATCH); scope.set_batch_idx(0);
  std::string left = "left", right = "right";
  ObDatum l = text(left), r = text(right); l.set_has_lob_header(); r.set_has_lob_header();
  OwnedFixedRow saved({l}); saved.relocate();
  (limit.*member(LimitRow{})).store_row_ = saved.row_;
  set_datum(0, r); set_comparator(0, CompareProbe::fail);
  CompareProbe::expect(saved.row_->stored_cells()[0], r);
  bool equal = false;
  EXPECT_EQ(OB_TIMEOUT, (limit.*member(LimitScalar{}))(equal)); EXPECT_EQ(1, CompareProbe::calls);
  auto *skip = static_cast<ObBitVector *>(arena_.alloc(ObBitVector::memory_size(BATCH)));
  skip->reset(BATCH);
  bool keep = true; uint32_t matched = 9;
  EXPECT_EQ(OB_TIMEOUT, (limit.*member(LimitBatch{}))(keep, *skip, BATCH, matched));
  EXPECT_EQ(2, CompareProbe::calls);
}

TEST_F(FixedScalarConsumers, px_compare_keeps_mapped_keys_direction_and_null_order)
{
  set_type(1, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  int64_t low = INT64_MIN, high = INT64_MAX, same = 7;
  std::string source = "alpha", upper = "ALPHA";
  OwnedFixedRow stored({integer(low), text(source), integer(same)}); stored.relocate();
  set_datum(0, integer(high)); set_datum(1, text(upper)); set_datum(2, integer(same));
  ObSEArray<ObSortFieldCollation, 3> collations;
  ObSEArray<ObSortCmpFunc, 3> compares;
  for (int64_t col : {2, 1, 0}) {
    ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(col,
        col == 1 ? CS_TYPE_UTF8MB4_GENERAL_CI : CS_TYPE_BINARY, true, NULL_LAST)));
    ObSortCmpFunc cmp;
    cmp.cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(col == 1 ? ObVarcharType : ObIntType,
        col == 1 ? ObVarcharType : ObIntType, NULL_LAST,
        col == 1 ? CS_TYPE_UTF8MB4_GENERAL_CI : CS_TYPE_BINARY, SCALE_UNKNOWN_YET, false);
    ASSERT_EQ(OB_SUCCESS, compares.push_back(cmp));
  }
  FixedPxReceiveCompareTestAccess::Compare compare;
  ASSERT_EQ(OB_SUCCESS, compare.init(&collations, &compares));
  EXPECT_FALSE(compare(stored.row_, &exprs_, eval_));
  collations.at(2).is_ascending_ = false;
  EXPECT_TRUE(compare(stored.row_, &exprs_, eval_));
  set_datum(2, null_value());
  // NULL_LAST makes the non-NULL stored key smaller; ascending heap comparison is false.
  EXPECT_FALSE(compare(stored.row_, &exprs_, eval_));
  EXPECT_EQ(OB_SUCCESS, compare.get_error_code());
}

TEST_F(FixedScalarConsumers, px_compare_error_is_sticky_and_borrows_the_original_payload)
{
  set_type(0, ObVarcharType);
  std::string left = "left", right = "right";
  ObDatum l = text(left), r = text(right); l.set_has_lob_header(); r.set_has_lob_header();
  OwnedFixedRow stored({l}); stored.relocate(); set_datum(0, r);
  ObSEArray<ObSortFieldCollation, 1> collations;
  ObSEArray<ObSortCmpFunc, 1> compares;
  ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(0, CS_TYPE_BINARY, true, NULL_FIRST)));
  ObSortCmpFunc cmp; cmp.cmp_func_ = CompareProbe::fail;
  ASSERT_EQ(OB_SUCCESS, compares.push_back(cmp));
  FixedPxReceiveCompareTestAccess::Compare compare;
  ASSERT_EQ(OB_SUCCESS, compare.init(&collations, &compares));
  CompareProbe::expect(stored.row_->stored_cells()[0], r);
  EXPECT_FALSE(compare(stored.row_, &exprs_, eval_)); EXPECT_EQ(OB_TIMEOUT, compare.get_error_code());
  EXPECT_FALSE(compare(nullptr, nullptr, eval_));
  EXPECT_EQ(OB_TIMEOUT, compare.get_error_code()); EXPECT_EQ(1, CompareProbe::calls);
}

TEST_F(FixedScalarConsumers, recursive_cycle_compares_all_fields_with_nulls_and_ci_collation)
{
  set_type(1, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  ExprFixedArray output(arena_); ASSERT_EQ(OB_SUCCESS, output.init(3));
  for (int64_t col = 0; col < 3; ++col) { ASSERT_EQ(OB_SUCCESS, output.push_back(&expressions_[col])); }
  ObBreadthFirstSearchOp search(arena_, output);
  int64_t low = INT64_MIN, high = INT64_MAX;
  std::string lower = "alpha", upper = "ALPHA";
  OwnedFixedRow first({integer(low), text(lower), null_value()});
  OwnedFixedRow equal({integer(low), text(upper), null_value()});
  OwnedFixedRow different({integer(high), text(upper), null_value()});
  first.relocate(); equal.relocate();
  bool cycle = false;
  EXPECT_EQ(OB_ERR_CYCLE_FOUND_IN_RECURSIVE_CTE, search.is_same_row(*first.row_, *equal.row_, cycle));
  EXPECT_TRUE(cycle);
  EXPECT_EQ(OB_SUCCESS, search.is_same_row(*first.row_, *different.row_, cycle)); EXPECT_FALSE(cycle);
}

TEST_F(FixedScalarConsumers, recursive_comparison_failure_is_not_replaced_by_a_cycle_error)
{
  set_type(0, ObVarcharType);
  ExprFixedArray output(arena_); ASSERT_EQ(OB_SUCCESS, output.init(1));
  ASSERT_EQ(OB_SUCCESS, output.push_back(&expressions_[0]));
  ObBreadthFirstSearchOp search(arena_, output);
  std::string left = "left", right = "right";
  ObDatum l = text(left), r = text(right); l.set_has_lob_header(); r.set_has_lob_header();
  OwnedFixedRow first({l}), second({r}); first.relocate(); second.relocate();
  set_comparator(0, CompareProbe::fail);
  CompareProbe::expect(first.row_->stored_cells()[0], StoredDatumCodec::decode_legacy_view(second.row_->stored_cells()[0]));
  bool cycle = false;
  // The previous implementation set cycle=true before comparison, received
  // OB_TIMEOUT, then overwrote it with the cycle error solely because of that flag.
  EXPECT_EQ(OB_TIMEOUT, search.is_same_row(*first.row_, *second.row_, cycle));
  EXPECT_EQ(1, CompareProbe::calls);
}

TEST_F(FixedScalarConsumers, replace_skips_hidden_pk_and_current_doc_id_but_compares_visible_fields)
{
  set_type(2, ObVarcharType, CS_TYPE_UTF8MB4_GENERAL_CI);
  ObTableReplaceSpec spec(arena_, PHY_REPLACE);
  ObInsCtDef ins(arena_); ObDelCtDef del(arena_); ObReplaceCtDef replace(arena_);
  ObReplaceCtDef *pointer = nullptr; replace_columns(spec, ins, del, replace, pointer);
  spec.doc_id_col_id_ = 33;
  ReplaceConsumer consumer(exec_, spec, nullptr); prepare_eval(consumer.get_eval_ctx());
  int64_t low = INT64_MIN, high = INT64_MAX;
  std::string lower = "alpha", upper = "ALPHA", other = "beta";
  OwnedFixedRow inserted({integer(low), integer(low), text(lower), null_value()});
  OwnedFixedRow deleted({integer(high), integer(high), text(upper), null_value()});
  OwnedFixedRow changed({integer(high), integer(high), text(other), null_value()});
  inserted.relocate(); deleted.relocate();
  bool equal = false;
  EXPECT_EQ(OB_SUCCESS, consumer.check_values(equal, inserted.row_, deleted.row_)); EXPECT_TRUE(equal);
  EXPECT_EQ(OB_SUCCESS, consumer.check_values(equal, inserted.row_, changed.row_)); EXPECT_FALSE(equal);
  spec.doc_id_col_id_ = OB_INVALID_ID;
  EXPECT_EQ(OB_SUCCESS, consumer.check_values(equal, inserted.row_, deleted.row_)); EXPECT_FALSE(equal);
}

TEST_F(FixedScalarConsumers, replace_keeps_comparison_error_and_lob_descriptor_flags)
{
  set_type(2, ObVarcharType);
  ObTableReplaceSpec spec(arena_, PHY_REPLACE);
  ObInsCtDef ins(arena_); ObDelCtDef del(arena_); ObReplaceCtDef replace(arena_);
  ObReplaceCtDef *pointer = nullptr; replace_columns(spec, ins, del, replace, pointer);
  spec.doc_id_col_id_ = 33;
  ReplaceConsumer consumer(exec_, spec, nullptr); prepare_eval(consumer.get_eval_ctx());
  int64_t value = 1;
  std::string left = "left", right = "right";
  ObDatum l = text(left), r = text(right); l.set_has_lob_header(); r.set_has_lob_header();
  OwnedFixedRow inserted({integer(value), integer(value), l, null_value()});
  OwnedFixedRow deleted({integer(value), integer(value), r, null_value()});
  inserted.relocate(); deleted.relocate(); set_comparator(2, CompareProbe::fail);
  CompareProbe::expect(inserted.row_->stored_cells()[2], StoredDatumCodec::decode_legacy_view(deleted.row_->stored_cells()[2]));
  bool equal = false;
  EXPECT_EQ(OB_TIMEOUT, consumer.check_values(equal, inserted.row_, deleted.row_));
  EXPECT_EQ(1, CompareProbe::calls);
}

TEST_F(FixedScalarConsumers, group_fold_reads_saved_group_id_at_its_mapped_column_and_cursor)
{
  int64_t zero = 0, first = (int64_t(17) << 32) | 0xffffffffLL;
  int64_t second = INT64_MAX;
  for (int64_t col = 0; col < COLS; ++col) {
    for (int64_t row = 0; row < BATCH; ++row) { set_datum(col, integer(zero), row); }
  }
  set_datum(2, integer(first), 1); set_datum(2, integer(second), 2);
  ObGroupResultSaveRows saved;
  ASSERT_EQ(OB_SUCCESS, saved.init(exprs_, eval_, BATCH, 2, false, arena_));
  ASSERT_EQ(OB_SUCCESS, saved.save(true, 1, 2));
  first = second = 0;
  // Move the first real LastDASStoreRow after its writer has owned the bytes.
  FixedRow *original = saved.store_rows_[0].store_row_;
  const uint32_t size = original->row_size_;
  original->unswizzling(original->payload_);
  char *moved = static_cast<char *>(arena_.alloc(size)); ASSERT_NE(nullptr, moved);
  MEMCPY(moved, original, size); MEMSET(original, 0xA5, size);
  saved.store_rows_[0].store_row_ = reinterpret_cast<FixedRow *>(moved);
  saved.store_rows_[0].store_row_->swizzling(saved.store_rows_[0].store_row_->payload_);
  EXPECT_EQ(17, saved.cur_group_idx());
  saved.next_start_pos(); EXPECT_EQ(INT32_MAX, saved.cur_group_idx());
  saved.next_start_pos(); EXPECT_EQ(OB_INVALID_INDEX, saved.cur_group_idx());
  saved.reuse(); EXPECT_EQ(OB_INVALID_INDEX, saved.cur_group_idx());
  saved.reset();
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
