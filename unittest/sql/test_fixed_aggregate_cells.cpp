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

#include "sql/engine/aggregate/ob_aggregate_processor.h"
#include "sql/engine/basic/ob_material_op_impl.h"
#include "sql/optimizer/stat/ob_hybrid_hist_estimator.h"
#include "sql/optimizer/stat/ob_dbms_stats_utils.h"
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
using Row = ObChunkDatumStore::StoredRow;
using Cell = ObAggregateProcessor::AggrCell;
template <typename Tag, typename Tag::Type Member>
struct PrivateMember { friend typename Tag::Type member(Tag) { return Member; } };
struct PrepareTag {
  using Type = int (ObAggregateProcessor::*)(const Row &, const ObIArray<ObExpr *> *, Cell &, const ObAggrInfo &);
  friend Type member(PrepareTag);
};
template struct PrivateMember<PrepareTag, &ObAggregateProcessor::prepare_aggr_result>;
struct ProcessTag { using Type = PrepareTag::Type; friend Type member(ProcessTag); };
template struct PrivateMember<ProcessTag, &ObAggregateProcessor::process_aggr_result>;
struct TruncateTag {
  using Type = int (ObAggregateProcessor::*)(SqlTypeMeta, const Row &, int64_t);
  friend Type member(TruncateTag);
};
template struct PrivateMember<TruncateTag, &ObAggregateProcessor::shadow_truncate_stored_string_for_hist>;
struct EqualTag {
  using Type = int (ObAggregateProcessor::*)(const ObChunkDatumStore::LastStoredRow &, const Row &, const ObAggrInfo &, bool &);
  friend Type member(EqualTag);
};
template struct PrivateMember<EqualTag, &ObAggregateProcessor::check_rows_equal>;
struct ValuesTag {
  using Type = int (ObAggregateProcessor::*)(const ObAggrInfo &, const Row &, DatumValue *, int64_t);
  friend Type member(ValuesTag);
};
template struct PrivateMember<ValuesTag, &ObAggregateProcessor::convert_datum_to_values>;
struct HashTag {
  using Type = int (ObAggregateProcessor::*)(const Row &, const ObIArray<ObExpr *> &, bool &, uint64_t &);
  friend Type member(HashTag);
};
template struct PrivateMember<HashTag, &ObAggregateProcessor::llc_calc_hash_value>;
#define MEMBER_TAG(Name, TypeName, Field) \
  struct Name { using Type = TypeName ObMaterialOpImpl::*; friend Type member(Name); }; \
  template struct PrivateMember<Name, &ObMaterialOpImpl::Field>
MEMBER_TAG(MaterialContextTag, lib::MemoryContext, mem_context_);
MEMBER_TAG(MaterialStoreTag, ObChunkDatumStore, datum_store_);
MEMBER_TAG(MaterialInitedTag, bool, inited_);
MEMBER_TAG(MaterialFirstTag, bool, got_first_row_);
#undef MEMBER_TAG

ObDatum integer(const int64_t &n)
{ return ObDatum(reinterpret_cast<const char *>(&n), sizeof(n), false); }
ObDatum bytes(const void *p, uint32_t size)
{ return ObDatum(static_cast<const char *>(p), size, false); }
ObDatum null_value() { ObDatum value; value.set_null(); return value; }
int64_t number(const ObDatum &datum)
{
  int64_t n = 0;
  EXPECT_EQ(sizeof(n), datum.len_);
  if (datum.len_ == sizeof(n)) { MEMCPY(&n, datum.ptr_, sizeof(n)); }
  return n;
}
struct OwnedRow
{
  explicit OwnedRow(const std::vector<ObDatum> &values)
  {
    const uint32_t extra = (8 - (sizeof(Row) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) Row;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(Row), size, extra));
  }
  void poison() { MEMSET(memory_.data(), 0xA5, memory_.size()); }
  std::vector<char> memory_;
  Row *row_ = nullptr;
};

class FixedAggregateCells : public ::testing::Test
{
protected:
  FixedAggregateCells() : arena_(), exec_(arena_), frame_{}, frames_{frame_.data()}, eval_(exec_),
      guard_(eval_), infos_(arena_), monitor_(), processor_(eval_, infos_, "FixedAggregate", monitor_), exprs_{} {}
  void SetUp() override
  {
    exec_.set_frames(frames_);
    eval_.frames_ = frames_;
    guard_.set_batch_size(1);
    for (int i = 0; i < 5; ++i) {
      ObExpr &expr = exprs_[i];
      expr.type_ = T_REF_COLUMN;
      expr.frame_idx_ = 0;
      expr.datum_off_ = i * 512;
      expr.eval_info_off_ = i * 512 + 64;
      expr.res_buf_off_ = i * 512 + 192;
      expr.res_buf_len_ = 128;
      set_type(expr, ObIntType);
      new (frame_.data() + expr.datum_off_) ObDatum;
      new (frame_.data() + expr.eval_info_off_) ObEvalInfo;
      expr.get_eval_info(eval_).evaluated_ = true;
    }
  }
  void set_type(ObExpr &expr, SqlValueType type, ObCollationType collation = CS_TYPE_BINARY)
  {
    expr.obj_meta_.set_type(type);
    expr.obj_meta_.set_collation_type(collation);
    expr.datum_meta_ = ObDatumMeta(type, collation, type == ObDecimalIntType ? 2 : 0);
    if (type == ObDecimalIntType) { expr.obj_meta_.set_scale(2); }
    expr.obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(type);
    expr.basic_funcs_ = ObDatumFuncs::get_basic_func(type, collation,
        expr.datum_meta_.scale_, expr.obj_meta_.has_lob_header(), expr.datum_meta_.precision_);
  }
  void bind(ObAggrInfo &info, ObItemType function, int64_t count = 1)
  {
    info.real_aggr_type_ = function;
    info.expr_ = &exprs_[0];
    ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(count));
    for (int64_t i = 0; i < count; ++i) { ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&exprs_[i])); }
  }
  int prepare(const Row &row, Cell &cell, const ObAggrInfo &info)
  { return (processor_.*member(PrepareTag{}))(row, nullptr, cell, info); }
  int update(const Row &row, Cell &cell, const ObAggrInfo &info)
  { return (processor_.*member(ProcessTag{}))(row, nullptr, cell, info); }
  int truncate(const Row &row, const SqlTypeMeta &meta, int64_t column = 0)
  { return (processor_.*member(TruncateTag{}))(meta, row, column); }
  ObDatum &datum(int64_t i) { return exprs_[i].locate_expr_datum(eval_); }
  void prepare_material(ObMaterialOpImpl &material)
  {
    // Bind the existing in-memory owner before its first add. Workarea
    // admission is outside this test; before_add_row/process_dump/copy and
    // iteration remain production methods, with no file or memory pressure.
    lib::ContextParam param;
    param.set_mem_attr("FixedAggrTest").set_properties(lib::USE_TL_PAGE_OPTIONAL);
    ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(material.*member(MaterialContextTag{}), param));
    auto &store = material.*member(MaterialStoreTag{});
    store.set_allocator(arena_);
    ASSERT_EQ(OB_SUCCESS, store.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "FixedAggrTest", false));
    material.*member(MaterialInitedTag{}) = true;
    material.*member(MaterialFirstTag{}) = true;
  }
  ObArenaAllocator arena_;
  ObExecContext exec_;
  alignas(8) std::array<char, 4096> frame_;
  char *frames_[1];
  ObEvalCtx eval_;
  ObEvalCtx::BatchInfoScopeGuard guard_;
  AggrInfoFixedArray infos_;
  ObMonitorNode monitor_;
  ObAggregateProcessor processor_;
  ObExpr exprs_[5];
};

TEST_F(FixedAggregateCells, eval_aggr_publishes_fixed_shadow_cells_without_changing_borrow_contract)
{
  ObAggrInfo info(arena_);
  bind(info, T_FUN_COUNT, 5);
  int64_t n = INT64_MIN;
  const std::string text("a\0b", 3);
  std::array<char, 16> decimal{}; decimal.fill(0x31);
  set_type(exprs_[1], ObVarcharType);
  set_type(exprs_[2], ObDecimalIntType);
  datum(0) = integer(n); datum(1) = bytes(text.data(), text.size());
  datum(2) = bytes(decimal.data(), decimal.size()); datum(3).set_null();
  datum(4) = bytes(text.data(), text.size()); datum(4).set_has_lob_header();
  ObChunkDatumStore::ShadowStoredRow shadow;
  ASSERT_EQ(OB_SUCCESS, shadow.init(arena_, 5));
  ASSERT_EQ(OB_SUCCESS, info.eval_aggr(shadow, eval_));
  const StoredDatumCell *cells = shadow.get_store_row()->stored_cells();
  EXPECT_EQ(INT64_MIN, number(StoredDatumCodec::decode_legacy_view(cells[0])));
  EXPECT_EQ(datum(1).ptr_, cells[1].ptr_); EXPECT_EQ(3, cells[1].len_);
  EXPECT_EQ(16, cells[2].len_); EXPECT_EQ(PRECISION_UNKNOWN_YET, exprs_[2].datum_meta_.precision_);
  EXPECT_TRUE(cells[3].is_null()); EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, cells[4].flag_);
  n = 19;
  EXPECT_EQ(19, number(StoredDatumCodec::decode_legacy_view(cells[0])));
  DatumValue values[5];
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(ValuesTag{}))(info, *shadow.get_store_row(), values, 5));
  EXPECT_EQ(ObDecimalIntType, values[2].get_type()); EXPECT_EQ(16, values[2].get_int_bytes());
  EXPECT_EQ(2, values[2].get_scale()); EXPECT_TRUE(values[3].is_null());
}

TEST_F(FixedAggregateCells, count_prepare_and_update_keep_multicolumn_null_rules)
{
  ObAggrInfo info(arena_); bind(info, T_FUN_COUNT, 2);
  Cell cell;
  OwnedRow first({integer(7), integer(9)}), skipped({integer(7), null_value()});
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, cell, info));
  EXPECT_EQ(1, cell.get_row_count());
  ASSERT_EQ(OB_SUCCESS, update(*skipped.row_, cell, info)); EXPECT_EQ(1, cell.get_row_count());
  ASSERT_EQ(OB_SUCCESS, update(*first.row_, cell, info)); EXPECT_EQ(2, cell.get_row_count());
  processor_.get_removal_info().is_inv_aggr_ = true;
  ASSERT_EQ(OB_SUCCESS, update(*first.row_, cell, info)); EXPECT_EQ(1, cell.get_row_count());
}

TEST_F(FixedAggregateCells, integer_sum_prepare_update_and_inverse_consume_owned_rows)
{
  ObAggrInfo info(arena_); bind(info, T_FUN_SUM);
  Cell cell;
  OwnedRow first({integer(7)}), second({integer(-2)}), skipped({null_value()});
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, cell, info));
  ASSERT_EQ(OB_SUCCESS, update(*second.row_, cell, info)); EXPECT_EQ(5, cell.get_tiny_num_int());
  ASSERT_EQ(OB_SUCCESS, update(*skipped.row_, cell, info)); EXPECT_EQ(5, cell.get_tiny_num_int());
  processor_.get_removal_info().is_inv_aggr_ = true;
  ASSERT_EQ(OB_SUCCESS, update(*second.row_, cell, info)); EXPECT_EQ(7, cell.get_tiny_num_int());
  first.poison(); second.poison(); EXPECT_EQ(7, cell.get_tiny_num_int());
}

TEST_F(FixedAggregateCells, string_max_clones_prepare_and_update_inputs_before_source_reuse)
{
  set_type(exprs_[0], ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  ObAggrInfo info(arena_); bind(info, T_FUN_MAX);
  Cell cell;
  const std::string a = "customer_a", z = "customer_z";
  OwnedRow first({bytes(a.data(), a.size())}), second({bytes(z.data(), z.size())}), skipped({null_value()});
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, cell, info)); first.poison();
  EXPECT_EQ(a, std::string(cell.get_iter_result().ptr_, cell.get_iter_result().len_));
  ASSERT_EQ(OB_SUCCESS, update(*second.row_, cell, info)); second.poison();
  ASSERT_EQ(OB_SUCCESS, update(*skipped.row_, cell, info));
  EXPECT_EQ(z, std::string(cell.get_iter_result().ptr_, cell.get_iter_result().len_));
}

TEST_F(FixedAggregateCells, unknown_decimal_precision_preserves_coefficient_width_through_init_and_update)
{
  set_type(exprs_[0], ObDecimalIntType);
  ObAggrInfo info(arena_); bind(info, T_FUN_MAX);
  Cell cell;
  std::array<char, 16> coefficient{}, larger{};
  coefficient.fill(0x23); larger.fill(0x24);
  OwnedRow first({bytes(coefficient.data(), coefficient.size())}),
      second({bytes(larger.data(), larger.size())}), skipped({null_value()});
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, cell, info)); first.poison();
  ASSERT_EQ(OB_SUCCESS, update(*second.row_, cell, info)); second.poison();
  ASSERT_EQ(OB_SUCCESS, update(*skipped.row_, cell, info));
  EXPECT_EQ(16, cell.get_iter_result().len_);
  EXPECT_EQ(0, MEMCMP(larger.data(), cell.get_iter_result().ptr_, larger.size()));
  EXPECT_EQ(PRECISION_UNKNOWN_YET, exprs_[0].datum_meta_.precision_);
}

TEST_F(FixedAggregateCells, stored_hash_consumer_preserves_null_short_circuit_and_hash_functions)
{
  set_type(exprs_[1], ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  ObAggrInfo info(arena_); bind(info, T_FUN_COUNT, 2);
  const std::string text = "customer";
  OwnedRow row({integer(7), bytes(text.data(), text.size())}), skipped({integer(7), null_value()});
  bool has_null = true; uint64_t hash = 0, expected = 0;
  const ObDatum first = StoredDatumCodec::decode_legacy_view(row.row_->stored_cells()[0]);
  const ObDatum second = StoredDatumCodec::decode_legacy_view(row.row_->stored_cells()[1]);
  ASSERT_EQ(OB_SUCCESS, exprs_[0].basic_funcs_->murmur_hash_(first, expected, expected, nullptr));
  ASSERT_EQ(OB_SUCCESS, exprs_[1].basic_funcs_->murmur_hash_(second, expected, expected, nullptr));
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(HashTag{}))(*row.row_, info.param_exprs_, has_null, hash));
  EXPECT_FALSE(has_null); EXPECT_EQ(expected, hash);
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(HashTag{}))(*skipped.row_, info.param_exprs_, has_null, hash));
  EXPECT_TRUE(has_null);
}

TEST_F(FixedAggregateCells, histogram_truncation_is_persisted_for_real_save_equality_and_material_copy)
{
  set_type(exprs_[0], ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  ObAggrInfo info(arena_); bind(info, T_FUN_HYBRID_HIST);
  ASSERT_EQ(OB_SUCCESS, info.sort_collations_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.sort_cmp_funcs_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.sort_collations_.push_back(ObSortFieldCollation(0, CS_TYPE_UTF8MB4_BIN, true, NULL_LAST)));
  ObSortCmpFunc cmp; cmp.cmp_func_ = exprs_[0].basic_funcs_->null_first_cmp_;
  ASSERT_EQ(OB_SUCCESS, info.sort_cmp_funcs_.push_back(cmp));
  std::string a(2000, 'a'), b(2000, 'a'); a.back() = 'x'; b.back() = 'y';
  OwnedRow first({bytes(a.data(), a.size())}), second({bytes(b.data(), b.size())});
  const auto before = first.memory_;
  const char *payload = first.row_->stored_cells()[0].ptr_;
  ASSERT_EQ(OB_SUCCESS, truncate(*first.row_, exprs_[0].obj_meta_));
  ASSERT_LT(first.row_->stored_cells()[0].len_, a.size());
  EXPECT_EQ(payload, first.row_->stored_cells()[0].ptr_);
  EXPECT_EQ(0, MEMCMP(a.data(), payload, a.size()));
  EXPECT_EQ(before.size(), first.memory_.size());
  ObChunkDatumStore::LastStoredRow saved(arena_);
  ASSERT_EQ(OB_SUCCESS, saved.save_store_row(*first.row_));
  ASSERT_EQ(OB_SUCCESS, truncate(*second.row_, exprs_[0].obj_meta_));
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(EqualTag{}))(saved, *second.row_, info, equal));
  EXPECT_TRUE(equal);
  ObAggregateProcessor::HybridHistExtraResult extra(arena_, monitor_);
  extra.mat_op_ = OB_NEWx(ObMaterialOpImpl, &arena_, monitor_);
  ASSERT_NE(nullptr, extra.mat_op_);
  const Row *material = first.row_;
  EXPECT_EQ(OB_NOT_INIT, extra.add_material_row(*saved.store_row_, sizeof(BucketDesc), material));
  EXPECT_EQ(first.row_, material); EXPECT_EQ(0, extra.material_row_count_);
  prepare_material(*extra.mat_op_);
  OwnedRow invalid({integer(7), integer(9)});
  EXPECT_EQ(OB_INVALID_ARGUMENT, extra.add_material_row(*invalid.row_, sizeof(BucketDesc), material));
  EXPECT_EQ(first.row_, material); EXPECT_EQ(0, extra.material_row_count_);
  material = nullptr;
  ASSERT_EQ(OB_SUCCESS, extra.add_material_row(*saved.store_row_, sizeof(BucketDesc), material));
  ASSERT_NE(nullptr, material); EXPECT_EQ(1, extra.material_row_count_);
  const auto expected_length = saved.store_row_->stored_cells()[0].len_;
  BucketDesc desc; desc.ep_count_ = 7; desc.is_pop_ = true;
  MEMCPY(material->get_extra_payload(), &desc, sizeof(desc));
  first.poison(); second.poison();
  saved.store_row_->stored_cells()[0].set_null();
  ASSERT_EQ(OB_SUCCESS, extra.finish_add_material_row());
  ASSERT_EQ(OB_SUCCESS, extra.get_next_row_from_material(material));
  EXPECT_EQ(expected_length, material->stored_cells()[0].len_);
  for (uint32_t i = 0; i < expected_length; ++i) { EXPECT_EQ('a', material->stored_cells()[0].ptr_[i]); }
  MEMCPY(&desc, material->get_extra_payload(), sizeof(desc));
  EXPECT_EQ(7, desc.ep_count_); EXPECT_TRUE(desc.is_pop_);
}

TEST_F(FixedAggregateCells, histogram_gates_and_errors_leave_nonapplicable_fixed_cells_unchanged)
{
  set_type(exprs_[0], ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  const std::string text = "short";
  OwnedRow short_row({bytes(text.data(), text.size())}), null_row({null_value()}), lob_row({bytes(text.data(), text.size())});
  SqlTypeMeta lob; lob.set_type(ObLongTextType); lob.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  for (auto *row : {short_row.row_, null_row.row_, lob_row.row_}) {
    const StoredDatumCell original = row->stored_cells()[0];
    const auto meta = row == lob_row.row_ ? lob : exprs_[0].obj_meta_;
    ASSERT_EQ(OB_SUCCESS, truncate(*row, meta));
    EXPECT_EQ(original.ptr_, row->stored_cells()[0].ptr_); EXPECT_EQ(original.pack_, row->stored_cells()[0].pack_);
    EXPECT_EQ(OB_INVALID_ARGUMENT, truncate(*row, meta, -1));
    EXPECT_EQ(OB_INVALID_ARGUMENT, truncate(*row, meta, 1));
    EXPECT_EQ(original.ptr_, row->stored_cells()[0].ptr_); EXPECT_EQ(original.pack_, row->stored_cells()[0].pack_);
  }
  ObAggregateProcessor::HybridHistExtraResult absent_material(arena_, monitor_);
  const Row *published = short_row.row_;
  OwnedRow invalid({integer(7), integer(9)});
  EXPECT_EQ(OB_SUCCESS, absent_material.add_material_row(*invalid.row_, sizeof(BucketDesc), published));
  EXPECT_EQ(short_row.row_, published); EXPECT_EQ(0, absent_material.material_row_count_);
}
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
