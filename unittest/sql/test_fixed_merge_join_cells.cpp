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
#include "sql/engine/join/ob_merge_join_op.h"
#include "sql/engine/ob_exec_context.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <array>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase { namespace sql {
// Bind the real merge-join comparison to already-prepared input frames and
// RA rows. This does not replace comparison, projection, NULL or direction logic.
class FixedMergeJoinCellsTestAccess
{
public:
  static int map(ObMergeJoinOp &join, std::initializer_list<int64_t> left,
                  std::initializer_list<int64_t> right)
  {
    int ret = OB_SUCCESS;
    if (OB_FAIL(join.left_brs_fetcher_.equal_param_idx_.init(left.size()))) {
    } else if (OB_FAIL(join.right_brs_fetcher_.equal_param_idx_.init(right.size()))) {
    }
    for (int64_t index : left) {
      if (OB_SUCC(ret)) { ret = join.left_brs_fetcher_.equal_param_idx_.push_back(index); }
    }
    for (int64_t index : right) {
      if (OB_SUCC(ret)) { ret = join.right_brs_fetcher_.equal_param_idx_.push_back(index); }
    }
    return ret;
  }
  static int compare(ObMergeJoinOp &join, bool left_stored,
                     const ObRADatumStore::StoredRow *row, int64_t batch_idx, int64_t &result)
  {
    // Production prepares these full-Type companions in inner_open; this
    // fixture binds the same selected functions without opening unrelated PX.
    const auto &spec = static_cast<const ObMergeJoinSpec &>(join.get_spec());
    join.native_equal_cmp_.reuse();
    for (int64_t i = 0; i < spec.equal_cond_infos_.count(); ++i) {
      const auto &condition = spec.equal_cond_infos_.at(i);
      const ObExpr *left = condition.expr_->args_[condition.is_opposite_ ? 1 : 0];
      const ObExpr *right = condition.expr_->args_[condition.is_opposite_ ? 0 : 1];
      DatumCmpOp operation;
      int ret = ObDatumFuncs::bind_legacy_cmp(left->get_sql_type(), right->get_sql_type(), condition.ns_cmp_func_, operation);
      if (ret != OB_SUCCESS) { return ret; }
      if ((ret = join.native_equal_cmp_.push_back(operation)) != OB_SUCCESS) { return ret; }
    }
    return left_stored ? join.calc_equal_conds_with_stored_row<true>(row, batch_idx, result)
                       : join.calc_equal_conds_with_stored_row<false>(row, batch_idx, result);
  }
};
} }

namespace
{
using RARow = ObRADatumStore::StoredRow;
using RABlock = ObRADatumStore::Block;
ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum text(const std::string &value)
{ return ObDatum(value.data(), value.size(), false); }
ObDatum null_value()
{ ObDatum value; value.set_null(); return value; }

struct OwnedRARow
{
  explicit OwnedRARow(std::initializer_list<ObDatum> values)
  {
    ObSEArray<ObDatum, 3> input;
    for (const ObDatum &value : values) { EXPECT_EQ(OB_SUCCESS, input.push_back(value)); }
    const uint32_t extra = (8 - (sizeof(RARow) + input.count() * sizeof(StoredDatumCell)) % 8) % 8;
    int64_t quota = 0;
    EXPECT_EQ(OB_SUCCESS, RABlock::row_store_size(input, quota, extra));
    // The separate row-index quota is not contiguous with the RA row body.
    memory_.resize(quota - RABlock::ROW_INDEX_SIZE);
    row_ = new (memory_.data()) RARow;
    EXPECT_EQ(OB_SUCCESS, row_->copy_datums(input, row_->payload_,
        memory_.size() - sizeof(RARow), quota, extra));
  }
  void relocate()
  {
    EXPECT_EQ(OB_SUCCESS, row_->to_copyable());
    const void *old = row_;
    std::vector<char> moved(memory_);
    MEMSET(memory_.data(), 0xA5, memory_.size());
    memory_.swap(moved);
    row_ = reinterpret_cast<RARow *>(memory_.data());
    EXPECT_NE(old, row_);
    EXPECT_EQ(OB_SUCCESS, row_->to_readable());
  }
  std::vector<char> memory_;
  RARow *row_ = nullptr;
};

struct CompareProbe
{
  static int calls;
  static const char *left;
  static const char *right;
  static uint32_t left_pack;
  static uint32_t right_pack;
  static int fail(const ObDatum &l, const ObDatum &r, int &cmp,
                  const ObDatumAccessContext *context)
  {
    ++calls;
    EXPECT_EQ(left, l.ptr_); EXPECT_EQ(right, r.ptr_);
    EXPECT_EQ(left_pack, l.pack_); EXPECT_EQ(right_pack, r.pack_);
    EXPECT_EQ(nullptr, context);
    cmp = 0;
    return OB_ERR_UNEXPECTED;
  }
  static void expect(const ObDatum &l, const ObDatum &r)
  { calls = 0; left = l.ptr_; right = r.ptr_; left_pack = l.pack_; right_pack = r.pack_; }
};
int CompareProbe::calls = 0;
const char *CompareProbe::left = nullptr;
const char *CompareProbe::right = nullptr;
uint32_t CompareProbe::left_pack = 0;
uint32_t CompareProbe::right_pack = 0;

class FixedMergeJoinCells : public ::testing::Test
{
protected:
  static constexpr int64_t BATCH = 4;
  FixedMergeJoinCells() : arena_(), exec_(arena_), spec_(arena_, PHY_MERGE_JOIN),
      frame_{}, frames_{frame_.data()}, join_(exec_, spec_, nullptr), guard_(join_.get_eval_ctx()),
      columns_{}, conditions_{}, args_{} {}
  void SetUp() override
  {
    spec_.max_batch_size_ = BATCH;
    spec_.join_type_ = INNER_JOIN;
    exec_.set_frames(frames_);
    ObEvalCtx &eval = join_.get_eval_ctx();
    eval.frames_ = frames_;
    eval.max_batch_size_ = BATCH;
    guard_.set_batch_size(BATCH);
    guard_.set_batch_idx(1);
    for (int col = 0; col < 6; ++col) {
      ObExpr &expr = columns_[col];
      expr.type_ = T_REF_COLUMN;
      expr.frame_idx_ = 0;
      expr.datum_off_ = col * 1024;
      expr.eval_info_off_ = col * 1024 + 128;
      expr.eval_flags_off_ = col * 1024 + 192;
      expr.res_buf_off_ = col * 1024 + 512;
      expr.res_buf_len_ = 128;
      expr.batch_result_ = true;
      expr.batch_idx_mask_ = UINT64_MAX;
      const SqlValueType type = col % 3 == 1 ? ObVarcharType : ObIntType;
      expr.obj_meta_.set_type(type);
      expr.datum_meta_ = ObDatumMeta(type, CS_TYPE_BINARY, 0);
      expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
      expr.obj_meta_.set_collation_type(CS_TYPE_BINARY);
      ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, ObAccuracy(), arena_));
      for (int row = 0; row < BATCH; ++row) {
        new (frame_.data() + expr.datum_off_ + row * sizeof(Datum)) Datum;
      }
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      expr.get_eval_info(eval).evaluated_ = true;
      expr.get_evaluated_flags(eval).set_all(BATCH);
    }
    ASSERT_EQ(OB_SUCCESS, spec_.equal_cond_infos_.init(2));
    ASSERT_EQ(OB_SUCCESS, spec_.merge_directions_.init(2));
    for (int key = 0; key < 2; ++key) {
      conditions_[key].type_ = T_OP_EQ;
      conditions_[key].arg_cnt_ = 2;
      conditions_[key].args_ = args_[key].data();
      // The second predicate deliberately has its operand order reversed.
      args_[key][0] = &columns_[key == 1 ? 4 : 0];
      args_[key][1] = &columns_[key == 1 ? 1 : 3];
      ObMergeJoinSpec::EqualConditionInfo info;
      info.expr_ = &conditions_[key];
      info.is_opposite_ = key == 1;
      info.ns_cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(
          key == 1 ? ObVarcharType : ObIntType, key == 1 ? ObVarcharType : ObIntType,
          NULL_FIRST, key == 1 ? CS_TYPE_UTF8MB4_GENERAL_CI : CS_TYPE_BINARY, SCALE_UNKNOWN_YET, false);
      ASSERT_NE(nullptr, info.ns_cmp_func_);
      ASSERT_EQ(OB_SUCCESS, spec_.equal_cond_infos_.push_back(info));
      ASSERT_EQ(OB_SUCCESS, spec_.merge_directions_.push_back(key == 0 ? 1 : -1));
    }
    ASSERT_EQ(OB_SUCCESS, FixedMergeJoinCellsTestAccess::map(join_, {2, 1}, {0, 2}));
  }
  Datum &datum(bool left, int key, int row = 2)
  { return columns_[(left ? 0 : 3) + key].locate_batch_datums(join_.get_eval_ctx())[row]; }
  void bind(bool left, int row, const ObDatum &first, const ObDatum &second)
  {
    ASSERT_EQ(OB_SUCCESS, columns_[(left ? 0 : 3)].get_datum_copy_plan().import_legacy(first, datum(left, 0, row)));
    ASSERT_EQ(OB_SUCCESS, columns_[(left ? 0 : 3) + 1].get_datum_copy_plan().import_legacy(second, datum(left, 1, row)));
  }
  int compare(bool left_stored, const RARow *row, int64_t &result, int64_t batch_idx = 2)
  { return FixedMergeJoinCellsTestAccess::compare(join_, left_stored, row, batch_idx, result); }

  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObMergeJoinSpec spec_;
  alignas(8) std::array<char, 6144> frame_;
  char *frames_[1];
  ObMergeJoinOp join_;
  ObEvalCtx::BatchInfoScopeGuard guard_;
  ObExpr columns_[6];
  ObExpr conditions_[2];
  std::array<std::array<ObExpr *, 2>, 2> args_;
};

TEST_F(FixedMergeJoinCells, both_stored_sides_use_independent_mapping_and_opposite_ci_predicate)
{
  int64_t source_key = INT64_MIN, current_key = INT64_MIN, ignored = 17, greater = INT64_MAX;
  std::string source = "customer", current = "CUSTOMER";
  OwnedRARow left({integer(ignored), text(source), integer(source_key)});
  OwnedRARow right({integer(source_key), integer(ignored), text(source)});
  source_key = 0; source.assign(source.size(), 'x'); left.relocate(); right.relocate();
  bind(false, 2, integer(current_key), text(current)); bind(true, 2, integer(current_key), text(current));
  int64_t result = 0;
  ASSERT_EQ(OB_SUCCESS, compare(true, left.row_, result)); EXPECT_EQ(0, result);
  ASSERT_EQ(OB_SUCCESS, compare(false, right.row_, result)); EXPECT_EQ(0, result);
  datum(false, 0).set_int(greater); datum(true, 0).set_int(greater);
  ASSERT_EQ(OB_SUCCESS, compare(true, left.row_, result)); EXPECT_LT(result, 0);
  result = 0; ASSERT_EQ(OB_SUCCESS, compare(false, right.row_, result)); EXPECT_GT(result, 0);
  spec_.merge_directions_.at(0) = -1;
  result = 0; ASSERT_EQ(OB_SUCCESS, compare(true, left.row_, result)); EXPECT_GT(result, 0);
  result = 0; ASSERT_EQ(OB_SUCCESS, compare(false, right.row_, result)); EXPECT_LT(result, 0);
  EXPECT_EQ(1, join_.get_eval_ctx().get_batch_idx());
}

TEST_F(FixedMergeJoinCells, second_mapped_key_retains_descending_collation_order)
{
  int64_t key = 19, ignored = 0;
  std::string alpha = "alpha", beta = "beta";
  OwnedRARow left({integer(ignored), text(alpha), integer(key)});
  OwnedRARow right({integer(key), integer(ignored), text(alpha)});
  bind(false, 2, integer(key), text(beta)); bind(true, 2, integer(key), text(beta));
  int64_t result = 0;
  ASSERT_EQ(OB_SUCCESS, compare(true, left.row_, result)); EXPECT_GT(result, 0);
  result = 0; ASSERT_EQ(OB_SUCCESS, compare(false, right.row_, result)); EXPECT_LT(result, 0);
}

TEST_F(FixedMergeJoinCells, ordinary_and_nullsafe_equal_keep_the_both_null_special_case)
{
  int64_t ignored = 0;
  std::string lower = "alpha", upper = "ALPHA";
  OwnedRARow left({integer(ignored), text(lower), null_value()});
  OwnedRARow right({null_value(), integer(ignored), text(lower)});
  bind(false, 2, null_value(), text(upper)); bind(true, 2, null_value(), text(upper));
  spec_.equal_cond_infos_.at(0).ns_cmp_func_ = CompareProbe::fail;
  CompareProbe::calls = 0;
  for (bool left_stored : {false, true}) {
    const RARow *row = left_stored ? left.row_ : right.row_;
    spec_.merge_directions_.at(0) = -1;
    conditions_[0].type_ = T_OP_EQ;
    int64_t result = 0;
    ASSERT_EQ(OB_SUCCESS, compare(left_stored, row, result)); EXPECT_EQ(-1, result);
    conditions_[0].type_ = T_OP_NSEQ;
    result = 0; ASSERT_EQ(OB_SUCCESS, compare(left_stored, row, result)); EXPECT_EQ(0, result);
  }
  EXPECT_EQ(0, CompareProbe::calls);
}

TEST_F(FixedMergeJoinCells, single_null_uses_existing_comparator_and_direction_on_both_sides)
{
  int64_t value = 7, ignored = 0;
  std::string name = "same";
  OwnedRARow left({integer(ignored), text(name), null_value()});
  OwnedRARow right({null_value(), integer(ignored), text(name)});
  bind(false, 2, integer(value), text(name)); bind(true, 2, integer(value), text(name));
  for (int direction : {1, -1}) {
    spec_.merge_directions_.at(0) = direction;
    int64_t result = 0;
    ASSERT_EQ(OB_SUCCESS, compare(true, left.row_, result)); EXPECT_EQ(direction == 1, result < 0);
    result = 0; ASSERT_EQ(OB_SUCCESS, compare(false, right.row_, result)); EXPECT_EQ(direction == -1, result < 0);
  }
}

TEST_F(FixedMergeJoinCells, nonbatch_cast_frames_use_zero_index_without_changing_current_batch)
{
  int64_t key = 7, wrong = 8, ignored = 0;
  std::string alpha = "alpha", beta = "beta";
  OwnedRARow left({integer(ignored), text(alpha), integer(key)});
  OwnedRARow right({integer(key), integer(ignored), text(alpha)});
  for (bool side : {false, true}) {
    bind(side, 0, integer(key), text(alpha)); bind(side, 2, integer(wrong), text(beta));
  }
  for (int col : {0, 1, 3, 4}) {
    // Merge join receives the already-evaluated cast/column frame, not its input.
    columns_[col].type_ = T_FUN_SYS_CAST;
    columns_[col].batch_result_ = false; columns_[col].batch_idx_mask_ = 0;
  }
  int64_t result = 0;
  ASSERT_EQ(OB_SUCCESS, compare(true, left.row_, result)); EXPECT_EQ(0, result);
  ASSERT_EQ(OB_SUCCESS, compare(false, right.row_, result)); EXPECT_EQ(0, result);
  EXPECT_EQ(1, join_.get_eval_ctx().get_batch_idx());
}

TEST_F(FixedMergeJoinCells, errors_stop_before_the_next_key_and_never_publish_a_stack_pointer)
{
  int64_t ignored = 0;
  for (int col : {0, 3}) {
    columns_[col].obj_meta_.set_varchar(); columns_[col].obj_meta_.set_collation_type(CS_TYPE_BINARY);
    columns_[col].obj_datum_map_ = OBJ_DATUM_STRING;
    ASSERT_EQ(OB_SUCCESS, columns_[col].prepare_scalar_type(columns_[col].obj_meta_, ObAccuracy(), arena_));
  }
  std::string source = "source", current = "current", name = "name";
  ObDatum stored = text(source), input = text(current);
  stored.set_has_lob_header(); input.set_has_lob_header();
  OwnedRARow left({integer(ignored), text(name), stored});
  OwnedRARow right({stored, integer(ignored), text(name)});
  left.relocate(); right.relocate(); source.assign(source.size(), 'x');
  bind(false, 2, input, text(name)); bind(true, 2, input, text(name));
  spec_.equal_cond_infos_.at(0).ns_cmp_func_ = CompareProbe::fail;
  spec_.equal_cond_infos_.at(1).ns_cmp_func_ = CompareProbe::fail;
  const char *left_payload = left.row_->stored_cells()[2].ptr_;
  const char *right_payload = right.row_->stored_cells()[0].ptr_;
  for (bool left_stored : {false, true}) {
    const RARow *row = left_stored ? left.row_ : right.row_;
    const ObDatum decoded = StoredDatumCodec::decode_legacy_view(row->stored_cells()[left_stored ? 2 : 0]);
    CompareProbe::expect(left_stored ? decoded : input, left_stored ? input : decoded);
    int64_t result = 0;
    EXPECT_EQ(OB_ERR_UNEXPECTED, compare(left_stored, row, result));
    EXPECT_EQ(1, CompareProbe::calls); EXPECT_EQ(0, result);
    result = 99;
    EXPECT_EQ(OB_SUCCESS, compare(left_stored, row, result)); EXPECT_EQ(99, result);
    EXPECT_EQ(1, CompareProbe::calls);
  }
  EXPECT_EQ(left_payload, left.row_->stored_cells()[2].ptr_);
  EXPECT_EQ(right_payload, right.row_->stored_cells()[0].ptr_);
  EXPECT_EQ(input.ptr_, datum(false, 0).get_string().ptr()); EXPECT_EQ(input.ptr_, datum(true, 0).get_string().ptr());
  EXPECT_EQ(1, join_.get_eval_ctx().get_batch_idx());
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
