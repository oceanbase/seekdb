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
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "sql/engine/expr/ob_expr_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/system_variable/ob_system_variable_init.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "common/udt/ob_vector_type.h"
#include "share/ob_i_lob_read_service.h"
#include <initializer_list>
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
struct RollupSumTag {
  using Type = int (ObAggregateProcessor::*)(const Cell &, Cell &, const ObAggrInfo &);
  friend Type member(RollupSumTag);
};
template struct PrivateMember<RollupSumTag, &ObAggregateProcessor::rollup_add_calc>;
struct RollupAggregationTag {
  using Type = int (ObAggregateProcessor::*)(Cell &, Cell &, const ObExpr *, const ObAggrInfo &, int64_t, int64_t);
  friend Type member(RollupAggregationTag);
};
template struct PrivateMember<RollupAggregationTag, &ObAggregateProcessor::rollup_aggregation>;
struct SeparatorInitTag {
  using Type = int (ObAggregateProcessor::*)(Cell &, const ObAggrInfo &, const ObAggregateProcessor::ObSelector &);
  friend Type member(SeparatorInitTag);
};
template struct PrivateMember<SeparatorInitTag,
    &ObAggregateProcessor::init_group_extra_aggr_info<ObAggregateProcessor::ObSelector>>;
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

class FinalCacheAllocator : public ObIAllocator
{
public:
  void *alloc(int64_t size) override { ++calls_; return reject_ ? nullptr : arena_.alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { arena_.free(pointer); }
  ObArenaAllocator arena_;
  int64_t calls_ = 0;
  bool reject_ = false;
};

// Real in-row vector math must not invoke external LOB I/O. Supply the same
// explicit service/context ports as the native UDT-result standard fixture;
// every external read fails and is counted instead of replacing vector math.
class AggregateNoIoLobService final : public ObILobReadService {
public:
  int calls_ = 0;
  int get_outrow_lob_full_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  int get_delta_lob_full_data(ObLobTextIterCtx &, SqlValueType, ObCollationType,
      ObLobLocatorV2 &, ObIAllocator *, ObString &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_outrow_prefix_data(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *, uint32_t) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  int get_first_block(ObLobTextIterCtx &, ObCollationType, bool, bool, ObIAllocator *,
      ObString &, ObTextStringIterState &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_next_block_inner(ObLobTextIterCtx &, ObCollationType, bool, bool,
      ObString &, ObTextStringIterState &) override { ++calls_; return OB_NOT_SUPPORTED; }
  int get_outrow_char_len(ObLobTextIterCtx &, ObCollationType, ObIAllocator *, int64_t &) override
    { ++calls_; return OB_NOT_SUPPORTED; }
  void free_lob_query_iter(ObLobTextIterCtx &) override { ++calls_; }
};
class AggregateInlineLobContext final : public ObILobAccessContext {};
struct AggregateLobContextTag {
  using Type = ObILobAccessContext *ObExecContext::*;
  friend Type member(AggregateLobContextTag);
};
template struct PrivateMember<AggregateLobContextTag, &ObExecContext::lob_access_ctx_>;

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
      new (frame_.data() + expr.datum_off_) Datum;
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
    ASSERT_EQ(OB_SUCCESS, expr.prepare_scalar_type(expr.obj_meta_, expr.prepare_legacy_copy_plan().get_accuracy(), arena_));
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
  Datum &datum(int64_t i) { return exprs_[i].locate_expr_datum(eval_); }
  void put(int64_t i, const common::ObDatum &value)
  { ASSERT_EQ(OB_SUCCESS, exprs_[i].get_datum_copy_plan().import_legacy(value, datum(i))); }
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
  set_type(exprs_[4], ObVarcharType);
  put(0, integer(n)); put(1, bytes(text.data(), text.size()));
  put(2, bytes(decimal.data(), decimal.size())); datum(3).set_null();
  put(4, bytes(text.data(), text.size())); datum(4).set_has_lob_header();
  ObChunkDatumStore::ShadowStoredRow shadow;
  ASSERT_EQ(OB_SUCCESS, shadow.init(arena_, 5));
  ASSERT_EQ(OB_SUCCESS, info.eval_aggr(shadow, eval_));
  const StoredDatumCell *cells = shadow.get_store_row()->stored_cells();
  EXPECT_EQ(INT64_MIN, number(StoredDatumCodec::decode_legacy_view(cells[0])));
  EXPECT_EQ(datum(1).get_string().ptr(), cells[1].ptr_); EXPECT_EQ(3, cells[1].len_);
  EXPECT_EQ(16, cells[2].len_); EXPECT_EQ(PRECISION_UNKNOWN_YET, exprs_[2].datum_meta_.precision_);
  EXPECT_TRUE(cells[3].is_null()); EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, cells[4].flag_);
  n = 19;
  // Native integer inputs own their bits; the fixed shadow borrows its own reserved scratch.
  EXPECT_EQ(INT64_MIN, number(StoredDatumCodec::decode_legacy_view(cells[0])));
  datum(0).set_int(23);
  EXPECT_EQ(INT64_MIN, number(StoredDatumCodec::decode_legacy_view(cells[0])));
  DatumValue values[5];
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(ValuesTag{}))(info, *shadow.get_store_row(), values, 5));
  EXPECT_EQ(ObDecimalIntType, values[2].get_type()); EXPECT_EQ(16, values[2].get_int_bytes());
  EXPECT_EQ(2, values[2].get_scale()); EXPECT_TRUE(values[3].is_null());
}

TEST_F(FixedAggregateCells, native_selected_batch_stages_only_selected_values_and_keeps_failure_atomic)
{
  set_type(exprs_[0], ObNumberType);
  ObExpr &input = exprs_[1];
  input.batch_result_ = true; input.batch_idx_mask_ = UINT64_MAX;
  eval_.max_batch_size_ = 3; guard_.set_batch_size(3);
  for (int64_t i = 1; i < 3; ++i) {
    new (frame_.data() + input.datum_off_ + i * sizeof(Datum)) Datum;
  }
  Datum *values = input.locate_batch_datums(eval_);
  values[0].set_int(7); values[1].set_int(900); values[1].set_has_lob_header(); values[2].set_int(-2);
  ObAggrInfo info(arena_); info.real_aggr_type_ = T_FUN_SUM; info.expr_ = &exprs_[0];
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(1));
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&input));
  ASSERT_EQ(OB_SUCCESS, infos_.init(1)); ASSERT_EQ(OB_SUCCESS, infos_.push_back(info));
  Cell cell;
  ObAggregateProcessor::GroupRow group; group.aggr_cells_ = &cell; group.n_cells_ = 1;
  ObBatchRows batch; batch.size_ = 3;
  const uint16_t selected[] = {0, 2};
  ASSERT_EQ(OB_SUCCESS, processor_.process_batch(&batch, group, selected, 2));
  EXPECT_EQ(5, cell.get_tiny_num_int());
  // The unselected value has flags invalid in the signed integer domain. It
  // must not be exported. Once selected, preflight fails before accumulator writes.
  const uint16_t invalid[] = {1};
  EXPECT_EQ(OB_INVALID_ARGUMENT, processor_.process_batch(&batch, group, invalid, 1));
  EXPECT_EQ(5, cell.get_tiny_num_int());
  guard_.set_batch_idx(0);
  ASSERT_EQ(OB_SUCCESS, cell.collect_result(ObIntTC, eval_, infos_.at(0)));
  int64_t actual = 0;
  ASSERT_EQ(OB_SUCCESS, ObExprUtil::trunc_num2int64(datum(0), actual)); EXPECT_EQ(5, actual);
  values[0].set_int(1000); values[2].set_int(1000);
  ASSERT_EQ(OB_SUCCESS, ObExprUtil::trunc_num2int64(datum(0), actual)); EXPECT_EQ(5, actual);
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
  EXPECT_EQ(a, std::string(static_cast<const char *>(cell.native_iter_result().get_reference()), cell.native_iter_result().length()));
  ASSERT_EQ(OB_SUCCESS, update(*second.row_, cell, info)); second.poison();
  ASSERT_EQ(OB_SUCCESS, update(*skipped.row_, cell, info));
  EXPECT_EQ(z, std::string(static_cast<const char *>(cell.native_iter_result().get_reference()), cell.native_iter_result().length()));
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
  EXPECT_EQ(16, cell.native_iter_result().length());
  EXPECT_EQ(0, MEMCMP(larger.data(), static_cast<const char *>(cell.native_iter_result().get_reference()), larger.size()));
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
  ASSERT_EQ(OB_SUCCESS, storage_predicate_functions(exprs_[0])->murmur_hash_(first, expected, expected, nullptr));
  ASSERT_EQ(OB_SUCCESS, storage_predicate_functions(exprs_[1])->murmur_hash_(second, expected, expected, nullptr));
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
  ObSortCmpFunc cmp; cmp.cmp_func_ = exprs_[0].basic_funcs_->null_first_cmp_.legacy_function();
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

TEST_F(FixedAggregateCells, final_cache_keeps_inline_null_and_small_decimal_without_payload_allocation)
{
  FinalCacheAllocator owner;
  owner.reject_ = true;
  Cell cell;
  Datum value;
  value.set_int(INT64_MIN);
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_EQ(INT64_MIN, cell.get_advance_collect_result().get_int());
  EXPECT_EQ(0, cell.get_advance_collect_result().length());
  set_type(exprs_[1], ObUInt64Type);
  value.set_uint(UINT64_MAX);
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[1].get_datum_copy_plan(), owner));
  EXPECT_EQ(UINT64_MAX, cell.get_advance_collect_result().get_uint());
  value.set_null();
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[1].get_datum_copy_plan(), owner));
  EXPECT_TRUE(cell.get_advance_collect_result().is_null());
  value.set_nop();
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[1].get_datum_copy_plan(), owner));
  EXPECT_TRUE(cell.get_advance_collect_result().is_nop());
  value.set_min();
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[1].get_datum_copy_plan(), owner));
  EXPECT_TRUE(cell.get_advance_collect_result().is_min());
  value.set_max();
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[1].get_datum_copy_plan(), owner));
  EXPECT_TRUE(cell.get_advance_collect_result().is_max());

  set_type(exprs_[2], ObDecimalIntType);
  ObAccuracy accuracy;
  accuracy.set_precision(9); accuracy.set_scale(2);
  ASSERT_EQ(OB_SUCCESS, exprs_[2].prepare_scalar_type(exprs_[2].obj_meta_, accuracy, arena_));
  int32_t coefficient = -12345;
  ASSERT_EQ(OB_SUCCESS, exprs_[2].get_datum_copy_plan().import_legacy(bytes(&coefficient, sizeof(coefficient)), value));
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[2].get_datum_copy_plan(), owner));
  EXPECT_EQ(0, cell.get_advance_collect_result().length());
  alignas(8) char scratch[64]; ObDatum view;
  ASSERT_EQ(OB_SUCCESS, exprs_[2].get_datum_copy_plan().legacy_view(
      cell.get_advance_collect_result(), view, scratch, sizeof(scratch)));
  int32_t actual = 0;
  ASSERT_EQ(sizeof(actual), view.len_); MEMCPY(&actual, view.ptr_, sizeof(actual));
  EXPECT_EQ(coefficient, actual);
  EXPECT_EQ(0, owner.calls_);
}

TEST_F(FixedAggregateCells, final_cache_reference_capacity_and_previous_value_survive_null_alias_and_failed_growth)
{
  set_type(exprs_[0], ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  FinalCacheAllocator owner;
  Cell cell; Datum value;
  std::string source = "retained";
  ASSERT_EQ(OB_SUCCESS, value.set_reference(source.data(), source.size(), Datum::OutRow));
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  const char *cached = cell.get_advance_collect_result().get_string().ptr();
  EXPECT_NE(source.data(), cached);
  EXPECT_EQ(Datum::OutRow, cell.get_advance_collect_result().reference_flags());
  source.assign(source.size(), 'x');
  EXPECT_EQ("retained", std::string(cached, cell.get_advance_collect_result().length()));
  EXPECT_EQ(1, owner.calls_);
  owner.reject_ = true;
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(
      cell.get_advance_collect_result(), exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_EQ(cached, cell.get_advance_collect_result().get_string().ptr());
  value.set_null();
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_TRUE(cell.get_advance_collect_result().is_null());
  ASSERT_EQ(OB_SUCCESS, value.set_string(nullptr, 0));
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_TRUE(cell.get_advance_collect_result().is_present());
  EXPECT_EQ(0, cell.get_advance_collect_result().length());
  EXPECT_EQ(nullptr, cell.get_advance_collect_result().get_reference());
  EXPECT_EQ(1, owner.calls_);
  ASSERT_EQ(OB_SUCCESS, value.set_string("next", 4));
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_EQ(cached, cell.get_advance_collect_result().get_string().ptr());
  EXPECT_EQ(1, owner.calls_);

  std::string large(129, 'L');
  ASSERT_EQ(OB_SUCCESS, value.set_string(large.data(), large.size()));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_EQ(2, owner.calls_);
  EXPECT_EQ(cached, cell.get_advance_collect_result().get_string().ptr());
  EXPECT_EQ(ObString::make_string("next"), cell.get_advance_collect_result().get_string());
  // A malformed semantic scalar must fail before allocation or publication.
  Datum malformed;
  ASSERT_EQ(OB_SUCCESS, malformed.set_reference(large.data(), 7));
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      cell.deep_copy_advance_collect_result(malformed, exprs_[1].get_datum_copy_plan(), owner));
  EXPECT_EQ(2, owner.calls_);
  EXPECT_EQ(ObString::make_string("next"), cell.get_advance_collect_result().get_string());
  cell.reuse();
  EXPECT_TRUE(cell.get_advance_collect_result().is_null());
  ASSERT_EQ(OB_SUCCESS, value.set_string("reuse", 5));
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_EQ(cached, cell.get_advance_collect_result().get_string().ptr());
  EXPECT_EQ(2, owner.calls_);
}

TEST_F(FixedAggregateCells, final_cache_preserves_unknown_decimal_actual_span_and_owns_coefficient)
{
  set_type(exprs_[0], ObDecimalIntType);
  ASSERT_EQ(PRECISION_UNKNOWN_YET, exprs_[0].get_sql_type().accuracy().get_precision());
  FinalCacheAllocator owner;
  Cell cell; Datum value;
  alignas(8) std::array<char, 16> coefficient{};
  coefficient[0] = 37;
  const auto expected = coefficient;
  ASSERT_EQ(OB_SUCCESS, exprs_[0].get_datum_copy_plan().import_legacy(
      bytes(coefficient.data(), coefficient.size()), value));
  ASSERT_EQ(OB_SUCCESS, cell.deep_copy_advance_collect_result(value, exprs_[0].get_datum_copy_plan(), owner));
  EXPECT_NE(coefficient.data(), cell.get_advance_collect_result().get_reference());
  EXPECT_EQ(coefficient.size(), cell.get_advance_collect_result().length());
  MEMSET(coefficient.data(), 0xA5, coefficient.size());
  EXPECT_EQ(0, MEMCMP(expected.data(), cell.get_advance_collect_result().get_reference(), expected.size()));
  EXPECT_EQ(1, owner.calls_);
}

TEST_F(FixedAggregateCells, actual_advance_collect_publishes_native_cache_and_keeps_failure_unevaluated)
{
  ObAggrInfo info(arena_); bind(info, T_FUN_MIN);
  ASSERT_EQ(OB_SUCCESS, infos_.init(1)); ASSERT_EQ(OB_SUCCESS, infos_.push_back(info));
  ASSERT_EQ(OB_SUCCESS, processor_.init_one_group());
  ObAggregateProcessor::GroupRow *group = nullptr;
  ASSERT_EQ(OB_SUCCESS, processor_.get_group_row(0, group)); ASSERT_NE(nullptr, group);
  Cell &cell = group->aggr_cells_[0];
  int64_t input = INT64_MIN;
  ASSERT_EQ(OB_SUCCESS, processor_.clone_aggr_cell(cell, integer(input), exprs_[0].get_sql_type()));
  cell.set_need_advance_collect();
  ASSERT_EQ(OB_SUCCESS, processor_.advance_collect_result(0));
  EXPECT_TRUE(cell.get_is_advance_evaluated());
  EXPECT_EQ(INT64_MIN, cell.get_advance_collect_result().get_int());
  EXPECT_EQ(0, cell.get_advance_collect_result().length());
  input = 0; ASSERT_EQ(OB_SUCCESS, processor_.clone_aggr_cell(cell, integer(input), exprs_[0].get_sql_type()));
  datum(0).set_int(17);
  ObSEArray<ObExpr *, 1> group_exprs;
  ObBatchRows output; int64_t group_id = 0;
  ASSERT_EQ(OB_SUCCESS, processor_.collect_result_batch(group_exprs, 1, output, group_id));
  EXPECT_EQ(INT64_MIN, datum(0).get_int());
  EXPECT_EQ(INT64_MIN, cell.get_advance_collect_result().get_int());
  EXPECT_EQ(1, output.size_); EXPECT_EQ(1, group_id);

  ASSERT_EQ(OB_SUCCESS, processor_.init_one_group(1));
  ASSERT_EQ(OB_SUCCESS, processor_.get_group_row(1, group)); ASSERT_NE(nullptr, group);
  Cell &invalid = group->aggr_cells_[0];
  const char malformed[7] = {};
  ASSERT_EQ(OB_INVALID_ARGUMENT, processor_.clone_aggr_cell(invalid,
      bytes(malformed, sizeof(malformed)), exprs_[0].get_sql_type()));
  EXPECT_TRUE(invalid.native_iter_result().is_null());
  // Preserve the existing final-stage malformed-state oracle after the checked
  // capture boundary has rejected the same seven-byte input.
  ASSERT_EQ(OB_SUCCESS, processor_.clone_aggr_cell(invalid, integer(input), exprs_[0].get_sql_type()));
  ASSERT_EQ(OB_SUCCESS, const_cast<Datum &>(invalid.native_iter_result()).set_reference(malformed, sizeof(malformed)));
  invalid.set_need_advance_collect();
  EXPECT_EQ(OB_INVALID_ARGUMENT, processor_.advance_collect_result(1));
  EXPECT_FALSE(invalid.get_is_advance_evaluated());
  EXPECT_TRUE(invalid.get_advance_collect_result().is_null());
}

TEST_F(FixedAggregateCells, native_min_max_capture_inline_has_no_payload_allocation_and_rejects_bad_input)
{
  FinalCacheAllocator owner;
  owner.reject_ = true;
  Cell cell;
  Datum input;
  input.set_int(INT64_MIN);
  ASSERT_EQ(OB_SUCCESS, cell.copy_semantic_iter(input, exprs_[0].get_sql_type(), owner));
  EXPECT_EQ(0, owner.calls_);
  EXPECT_EQ(INT64_MIN, cell.native_iter_result().get_int());
  EXPECT_EQ(0, cell.native_iter_result().length());
  double floating = 1.25;
  Cell double_cell;
  ASSERT_EQ(OB_SUCCESS, double_cell.copy_kernel_iter(bytes(&floating, sizeof(floating)),
      ObAggregateProcessor::IterKernelLayout(ObAggregateProcessor::IterValueDomain::Float64), owner));
  floating = 99.0;
  EXPECT_DOUBLE_EQ(1.25, double_cell.native_iter_result().get_double());
  EXPECT_EQ(0, owner.calls_);
  alignas(8) char scratch[64];
  ObDatum exported;
  ASSERT_EQ(OB_SUCCESS, cell.export_semantic_iter(exprs_[0].get_sql_type(), exported, scratch, sizeof(scratch)));
  EXPECT_EQ(INT64_MIN, exported.get_int());
  const char malformed[7] = {};
  EXPECT_EQ(OB_INVALID_ARGUMENT, processor_.clone_aggr_cell(
      cell, bytes(malformed, sizeof(malformed)), exprs_[0].get_sql_type()));
  EXPECT_EQ(INT64_MIN, cell.native_iter_result().get_int());
  ObAggrInfo info(arena_); bind(info, T_FUN_MIN);
  Cell actual;
  const int64_t high = INT64_MAX, low = INT64_MIN;
  OwnedRow first({integer(high)}), next({integer(low)});
  const int64_t before = processor_.get_aggr_used_size();
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, actual, info));
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, actual, info));
  first.poison(); next.poison();
  EXPECT_EQ(low, actual.native_iter_result().get_int());
  EXPECT_EQ(before, processor_.get_aggr_used_size());
}

TEST_F(FixedAggregateCells, native_kernel_coefficients_keep_actual_width_capacity_and_failed_replacement)
{
  using Domain = ObAggregateProcessor::IterValueDomain;
  using Layout = ObAggregateProcessor::IterKernelLayout;
  FinalCacheAllocator owner;
  Cell cell;
  for (int width : {4, 8, 16, 32, 64}) {
    alignas(8) char source[64] = {};
    source[0] = static_cast<char>(width);
    const Layout layout(Domain::DecimalCoefficient, width);
    ASSERT_EQ(OB_SUCCESS, cell.copy_kernel_iter(bytes(source, width), layout, owner));
    source[0] = 0;
    EXPECT_EQ(width, cell.native_iter_result().length());
    EXPECT_EQ(width, static_cast<const unsigned char *>(cell.native_iter_result().get_reference())[0]);
    EXPECT_EQ(width, cell.iter_layout().coefficient_bytes_);
    ObDatum view;
    ASSERT_EQ(OB_SUCCESS, cell.export_kernel_iter(layout, view, nullptr, 0));
    const void *owned = cell.native_iter_result().get_reference();
    const int64_t calls = owner.calls_;
    owner.reject_ = true;
    ASSERT_EQ(OB_SUCCESS, cell.copy_kernel_iter(view, layout, owner));
    EXPECT_EQ(owned, cell.native_iter_result().get_reference());
    EXPECT_EQ(calls, owner.calls_);
    cell.clear_iter_value();
    ASSERT_EQ(OB_SUCCESS, cell.copy_kernel_iter(view, layout, owner));
    EXPECT_EQ(owned, cell.native_iter_result().get_reference());
    EXPECT_EQ(calls, owner.calls_);
    owner.reject_ = false;
  }
  const void *previous = cell.native_iter_result().get_reference();
  const Layout previous_layout = cell.iter_layout();
  const char bad_number[7] = {};
  EXPECT_EQ(OB_INVALID_ARGUMENT, cell.copy_kernel_iter(bytes(bad_number, sizeof(bad_number)),
      Layout(Domain::Number), owner));
  EXPECT_EQ(previous, cell.native_iter_result().get_reference());
  ObDatum invalid = null_value();
  EXPECT_EQ(OB_INVALID_ARGUMENT, cell.copy_kernel_iter(invalid, Layout(Domain::DecimalCoefficient, 7), owner));
  EXPECT_EQ(previous, cell.native_iter_result().get_reference());
  EXPECT_TRUE(previous_layout == cell.iter_layout());
  const int64_t allocations = owner.calls_;
  EXPECT_EQ(OB_INVALID_ARGUMENT, cell.copy_kernel_iter(invalid, Layout(Domain::HllRegisters, 0, INT32_MAX), owner));
  EXPECT_EQ(allocations, owner.calls_);
  char *payload = nullptr; int64_t capacity = 0;
  ASSERT_EQ(OB_SUCCESS, cell.reserve_kernel_iter(previous_layout, 64, owner, payload, capacity));
  EXPECT_GE(capacity, 64);
  const int64_t old_capacity = capacity;
  owner.reject_ = true;
  char *unchanged = reinterpret_cast<char *>(uintptr_t(1)); int64_t unchanged_capacity = -1;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, cell.reserve_kernel_iter(
      Layout(Domain::DecimalCoefficient, 64), capacity + 1, owner, unchanged, unchanged_capacity));
  EXPECT_EQ(reinterpret_cast<char *>(uintptr_t(1)), unchanged);
  EXPECT_EQ(-1, unchanged_capacity);
  EXPECT_EQ(previous, cell.native_iter_result().get_reference());
  EXPECT_TRUE(previous_layout == cell.iter_layout());
  owner.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, cell.reserve_kernel_iter(previous_layout, old_capacity + 1, owner, payload, capacity));
  EXPECT_GT(capacity, old_capacity);
  EXPECT_EQ(64, static_cast<const unsigned char *>(cell.native_iter_result().get_reference())[0]);
  ObDatum foreign = bytes(&old_capacity, sizeof(old_capacity));
  EXPECT_EQ(OB_INVALID_ARGUMENT, cell.commit_owned_kernel_iter(foreign, Layout(Domain::DecimalCoefficient, 8)));
  EXPECT_EQ(64, cell.native_iter_result().length());
}

TEST_F(FixedAggregateCells, actual_sum_overflow_number_inverse_and_rollup_keep_independent_native_owner)
{
  set_type(exprs_[1], ObNumberType);
  ObAggrInfo info(arena_); bind(info, T_FUN_SUM); info.expr_ = &exprs_[1];
  Cell source, rollup;
  const int64_t maximum = INT64_MAX, one = 1;
  OwnedRow first({integer(maximum)}), next({integer(one)});
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, source, info));
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, source, info));
  EXPECT_TRUE(source.iter_layout().domain_ == ObAggregateProcessor::IterValueDomain::Number);
  EXPECT_GT(source.native_iter_result().length(), 0);
  FinalCacheAllocator reject;
  reject.reject_ = true;
  char *number_payload = nullptr; int64_t number_capacity = 0;
  ASSERT_EQ(OB_SUCCESS, source.reserve_kernel_iter(source.iter_layout(), 0, reject, number_payload, number_capacity));
  EXPECT_GE(number_capacity, static_cast<int64_t>(number::ObNumber::MAX_BYTE_LEN));
  EXPECT_EQ(0, reject.calls_);
  ASSERT_EQ(OB_SUCCESS, source.collect_result(ObIntTC, eval_, info));
  number::ObNumber actual(datum(1).get_number());
  EXPECT_EQ(0, actual.compare(uint64_t(1) << 63));
  processor_.get_removal_info().is_inv_aggr_ = true;
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, source, info));
  processor_.get_removal_info().is_inv_aggr_ = false;
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(RollupSumTag{}))(source, rollup, info));
  EXPECT_NE(source.native_iter_result().get_reference(), rollup.native_iter_result().get_reference());
  source.clear_iter_value(); first.poison(); next.poison();
  ASSERT_EQ(OB_SUCCESS, rollup.collect_result(ObIntTC, eval_, info));
  int64_t result = 0;
  ASSERT_EQ(OB_SUCCESS, ObExprUtil::trunc_num2int64(datum(1), result));
  EXPECT_EQ(INT64_MAX, result);
}

TEST_F(FixedAggregateCells, actual_float_batch_keeps_inline_state_and_selected_prefix)
{
  set_type(exprs_[0], ObFloatType); set_type(exprs_[1], ObFloatType);
  ObExpr &input = exprs_[1];
  input.batch_result_ = true; input.batch_idx_mask_ = UINT64_MAX;
  eval_.max_batch_size_ = 3; guard_.set_batch_size(3);
  for (int64_t i = 1; i < 3; ++i) { new (frame_.data() + input.datum_off_ + i * sizeof(Datum)) Datum; }
  Datum *values = input.locate_batch_datums(eval_);
  values[0].set_float(1.25F); values[1].set_float(999.0F); values[2].set_float(-0.5F);
  ObAggrInfo info(arena_); info.real_aggr_type_ = T_FUN_SUM; info.expr_ = &exprs_[0];
  ASSERT_EQ(OB_SUCCESS, info.param_exprs_.init(1)); ASSERT_EQ(OB_SUCCESS, info.param_exprs_.push_back(&input));
  ASSERT_EQ(OB_SUCCESS, infos_.init(1)); ASSERT_EQ(OB_SUCCESS, infos_.push_back(info));
  Cell cell; ObAggregateProcessor::GroupRow group; group.aggr_cells_ = &cell; group.n_cells_ = 1;
  ObBatchRows batch; batch.size_ = 3; const uint16_t selected[] = {0, 2};
  const int64_t before = processor_.get_aggr_used_size();
  ASSERT_EQ(OB_SUCCESS, processor_.process_batch(&batch, group, selected, 2));
  EXPECT_FLOAT_EQ(0.75F, cell.native_iter_result().get_float());
  EXPECT_EQ(0, cell.native_iter_result().length());
  EXPECT_EQ(before, processor_.get_aggr_used_size());
  values[0].set_float(88.0F); values[2].set_float(88.0F);
  guard_.set_batch_idx(0);
  ASSERT_EQ(OB_SUCCESS, cell.collect_result(ObFloatTC, eval_, infos_.at(0)));
  EXPECT_FLOAT_EQ(0.75F, datum(0).get_float());
}

TEST_F(FixedAggregateCells, actual_synopsis_merge_owns_registers_preserves_tail_and_rollup)
{
  set_type(exprs_[0], ObVarcharType, CS_TYPE_BINARY);
  ObAggrInfo info(arena_); bind(info, T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE);
  const int64_t registers = ObAggregateProcessor::get_llc_size();
  std::string first(registers + 13, '\0'), next(registers + 13, '\0');
  first[1] = 2; first[7] = 9; first[registers] = 'A';
  next[1] = 5; next[7] = 3; next[registers] = 'B';
  OwnedRow a({bytes(first.data(), first.size())}), b({bytes(next.data(), next.size())});
  Cell source, rollup;
  ASSERT_EQ(OB_SUCCESS, prepare(*a.row_, source, info));
  ASSERT_EQ(OB_SUCCESS, update(*b.row_, source, info));
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(RollupAggregationTag{}))(
      source, rollup, nullptr, info, 0, INT64_MIN));
  a.poison(); b.poison(); first.assign(first.size(), 'x'); next.assign(next.size(), 'x');
  const char *data = static_cast<const char *>(rollup.native_iter_result().get_reference());
  ASSERT_EQ(registers + 13, rollup.native_iter_result().length());
  EXPECT_EQ(5, data[1]); EXPECT_EQ(9, data[7]); EXPECT_EQ('A', data[registers]);
  EXPECT_NE(source.native_iter_result().get_reference(), data);
  source.clear_iter_value();
  EXPECT_EQ(5, data[1]); EXPECT_EQ('A', data[registers]);
  OwnedRow too_short({bytes(data, registers - 1)});
  Cell invalid;
  EXPECT_EQ(OB_INVALID_ARGUMENT, prepare(*too_short.row_, invalid, info));
  EXPECT_TRUE(invalid.native_iter_result().is_null());
  // The normal HLL producer still creates precisely the register count.
  ObAggrInfo ordinary(arena_); bind(ordinary, T_FUN_APPROX_COUNT_DISTINCT);
  Cell exact; OwnedRow integer_input({integer(17)});
  set_type(exprs_[0], ObIntType);
  ASSERT_EQ(OB_SUCCESS, prepare(*integer_input.row_, exact, ordinary));
  EXPECT_EQ(registers, exact.native_iter_result().length());
}

TEST_F(FixedAggregateCells, actual_group_separator_capture_distinguishes_null_unset_and_failed_replacement)
{
  set_type(exprs_[1], ObVarcharType, CS_TYPE_UTF8MB4_BIN);
  FinalCacheAllocator owner, rollup_owner;
  ObAggregateProcessor::GroupConcatExtraResult extra(owner, monitor_), rollup(rollup_owner, monitor_);
  Cell cell;
  struct Detach { Cell &cell_; ~Detach() { cell_.set_extra(nullptr); } } detach{cell};
  cell.set_extra(&extra);
  ObAggrInfo info(arena_); bind(info, T_FUN_GROUP_CONCAT); info.separator_expr_ = &exprs_[1];
  ObBatchRows batch; batch.size_ = 1; const uint16_t selected[] = {0};
  ObAggregateProcessor::ObSelector selector(&batch, selected, 1);
  const DatumCopyPlan plan = exprs_[1].get_datum_copy_plan();
  EXPECT_FALSE(extra.has_separator());
  alignas(8) char scratch[64]; ObDatum view;
  EXPECT_EQ(OB_NOT_INIT, extra.export_separator(plan, view, scratch, sizeof(scratch)));
  std::string separator("|\0|", 3);
  put(1, bytes(separator.data(), separator.size()));
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(SeparatorInitTag{}))(cell, info, selector));
  ASSERT_TRUE(extra.has_separator());
  EXPECT_NE(separator.data(), extra.separator_value().get_reference());
  ASSERT_EQ(OB_SUCCESS, rollup.copy_separator(extra.separator_value(), plan));
  EXPECT_NE(extra.separator_value().get_reference(), rollup.separator_value().get_reference());
  separator.assign(separator.size(), 'x'); datum(1).set_null();
  EXPECT_EQ(std::string("|\0|", 3), std::string(rollup.separator_value().get_string().ptr(), 3));
  const void *previous = extra.separator_value().get_reference();
  std::string replacement(100, 'r'); put(1, bytes(replacement.data(), replacement.size()));
  owner.reject_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, (processor_.*member(SeparatorInitTag{}))(cell, info, selector));
  EXPECT_TRUE(extra.has_separator()); EXPECT_EQ(previous, extra.separator_value().get_reference());
  owner.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(SeparatorInitTag{}))(cell, info, selector));
  replacement.assign(replacement.size(), 'x'); datum(1).set_null();
  EXPECT_EQ(std::string(100, 'r'), std::string(extra.separator_value().get_string().ptr(), 100));
  // A real evaluated constant SQL NULL is present, unlike an unset separator.
  exprs_[1].is_static_const_ = true; exprs_[1].get_eval_info(eval_).evaluated_ = true;
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(SeparatorInitTag{}))(cell, info, selector));
  EXPECT_TRUE(extra.has_separator()); EXPECT_TRUE(extra.separator_value().is_null());
  extra.clear_separator(); EXPECT_FALSE(extra.has_separator());
  ASSERT_EQ(OB_SUCCESS, datum(1).set_string(nullptr, 0));
  exprs_[1].get_eval_info(eval_).evaluated_ = true;
  owner.reject_ = true;
  const int64_t calls = owner.calls_;
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(SeparatorInitTag{}))(cell, info, selector));
  EXPECT_TRUE(extra.has_separator()); EXPECT_FALSE(extra.separator_value().is_null());
  EXPECT_EQ(0, extra.separator_value().length()); EXPECT_EQ(calls, owner.calls_);
  owner.reject_ = false;
  ASSERT_EQ(OB_SUCCESS, extra.copy_separator(rollup.separator_value(), plan));
  EXPECT_EQ(std::string("|\0|", 3), std::string(extra.separator_value().get_string().ptr(), 3));
}

TEST_F(FixedAggregateCells, actual_decimal_sum_widens_coefficient_and_preserves_inverse_rollup)
{
  ASSERT_EQ(OB_SUCCESS, share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  struct SessionScope {
    ObExecContext &exec_; ObSQLSessionInfo &session_;
    ~SessionScope() { ObSQLSessionInfo::ExecCtxSessionRegister clear(session_, nullptr); exec_.set_my_session(nullptr); }
  } session_scope{exec_, session};
  exec_.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec_);
  set_type(exprs_[0], ObDecimalIntType); set_type(exprs_[1], ObDecimalIntType);
  exprs_[0].datum_meta_.precision_ = 65; exprs_[1].datum_meta_.precision_ = 85;
  ASSERT_EQ(OB_SUCCESS, exprs_[0].prepare_scalar_type(exprs_[0].obj_meta_, ObAccuracy(65, 2), arena_));
  ASSERT_EQ(OB_SUCCESS, exprs_[1].prepare_scalar_type(exprs_[1].obj_meta_, ObAccuracy(85, 2), arena_));
  ObAggrInfo info(arena_); bind(info, T_FUN_SUM); info.expr_ = &exprs_[1];
  ASSERT_EQ(OB_SUCCESS, infos_.init(1)); ASSERT_EQ(OB_SUCCESS, infos_.push_back(info));
  ASSERT_EQ(OB_SUCCESS, processor_.init());
  const ObAggrInfo &actual_info = infos_.at(0);
  alignas(8) std::array<char, 32> a{}, b{}; a[0] = 37; b[0] = 13;
  OwnedRow first({bytes(a.data(), a.size())}), next({bytes(b.data(), b.size())});
  Cell source, rollup;
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, source, actual_info));
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, source, actual_info));
  EXPECT_TRUE(source.iter_layout().domain_ == ObAggregateProcessor::IterValueDomain::DecimalCoefficient);
  EXPECT_EQ(64, source.iter_layout().coefficient_bytes_);
  ASSERT_EQ(64, source.native_iter_result().length());
  EXPECT_EQ(50, static_cast<const unsigned char *>(source.native_iter_result().get_reference())[0]);
  processor_.get_removal_info().is_inv_aggr_ = true;
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, source, actual_info));
  processor_.get_removal_info().is_inv_aggr_ = false;
  ASSERT_EQ(OB_SUCCESS, (processor_.*member(RollupSumTag{}))(source, rollup, actual_info));
  EXPECT_NE(source.native_iter_result().get_reference(), rollup.native_iter_result().get_reference());
  source.clear_iter_value(); first.poison(); next.poison();
  ASSERT_EQ(OB_SUCCESS, rollup.collect_result(ObDecimalIntTC, eval_, actual_info));
  ASSERT_EQ(64, datum(1).length());
  std::array<char, 64> expected{}; expected[0] = 37;
  EXPECT_EQ(0, MEMCMP(expected.data(), datum(1).get_reference(), expected.size()));
}

TEST_F(FixedAggregateCells, actual_collection_sum_prepare_update_and_inverse_use_caller_owned_views)
{
  ASSERT_EQ(OB_SUCCESS, share::ObSysVariables::init_default_values());
  ObSQLSessionInfo session;
  ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  ASSERT_EQ(OB_SUCCESS, session.load_default_sys_variable(false, false));
  struct SessionScope {
    ObExecContext &exec_; ObSQLSessionInfo &session_;
    ~SessionScope() { ObSQLSessionInfo::ExecCtxSessionRegister clear(session_, nullptr); exec_.set_my_session(nullptr); }
  } session_scope{exec_, session};
  exec_.set_my_session(&session);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &exec_);
  AggregateNoIoLobService service;
  AggregateInlineLobContext inline_context;
  struct LobScope {
    ObExecContext &exec_;
    ~LobScope() { exec_.*member(AggregateLobContextTag{}) = nullptr; exec_.set_lob_read_service(nullptr); }
  } lob_scope{exec_};
  exec_.set_lob_read_service(&service);
  exec_.*member(AggregateLobContextTag{}) = &inline_context;
  ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
  uint16_t subschema_id = 0;
  ASSERT_EQ(OB_SUCCESS, exec_.get_subschema_id_by_type_string(ObString::make_string("VECTOR(3)"), subschema_id));
  ObSubSchemaValue definition;
  ASSERT_EQ(OB_SUCCESS, exec_.get_sqludt_meta_by_subschema_id(subschema_id, definition));
  ASSERT_EQ(OB_SUBSCHEMA_COLLECTION_TYPE, definition.type_);
  const auto *collection = static_cast<const ObSqlCollectionInfo *>(definition.value_);
  ASSERT_NE(nullptr, collection); ASSERT_NE(nullptr, collection->collection_meta_);
  SqlTypeMeta meta;
  meta.set_type(ObCollectionSQLType); meta.set_collation_type(CS_TYPE_BINARY);
  meta.set_subschema_id(subschema_id); meta.set_has_lob_header();
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, ObExprUtil::resolve_sql_type(exec_, meta, ObAccuracy(), subschema_id, arena_, type));
  ASSERT_NE(nullptr, type); EXPECT_TRUE(type->form() == SqlTypeForm::Vector);
  for (int i : {0, 1}) {
    exprs_[i].obj_meta_ = meta;
    exprs_[i].datum_meta_ = ObDatumMeta(ObCollectionSQLType, CS_TYPE_BINARY, 0);
    exprs_[i].obj_datum_map_ = ObDatum::get_sql_value_datum_map_type(ObCollectionSQLType);
    ASSERT_EQ(OB_SUCCESS, exprs_[i].bind_sql_type(*type, arena_));
  }
  auto make_vector = [&](std::initializer_list<float> values, ObString &lob) -> int {
    ObIArrayType *array = nullptr;
    int ret = ObArrayTypeObjFactory::construct(arena_, *collection->collection_meta_, array, false);
    if (OB_SUCC(ret)) { ret = array->init(); }
    for (float value : values) {
      if (OB_SUCC(ret)) { ret = static_cast<ObVectorF32Data *>(array)->push_back(value); }
    }
    if (OB_SUCC(ret)) { ret = array->init(); }
    if (OB_SUCC(ret)) {
      ret = ObArrayExprUtils::set_array_res(array, array->get_raw_binary_len(), arena_, lob);
    }
    return ret;
  };
  ObString first_blob, next_blob;
  ASSERT_EQ(OB_SUCCESS, make_vector({1.25F, 2.0F, 3.0F}, first_blob));
  ASSERT_EQ(OB_SUCCESS, make_vector({0.5F, -0.5F, 0.5F}, next_blob));
  ObDatum first_value = bytes(first_blob.ptr(), first_blob.length()); first_value.set_has_lob_header();
  ObDatum next_value = bytes(next_blob.ptr(), next_blob.length()); next_value.set_has_lob_header();
  Cell rejected;
  ObDatum rejected_view;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, processor_.clone_vector_cell(
      first_value, rejected, *type, &rejected_view, nullptr, 0));
  EXPECT_TRUE(rejected.native_iter_result().is_null());
  ObAggrInfo info(arena_); bind(info, T_FUN_SUM); info.expr_ = &exprs_[1];
  OwnedRow first({first_value}), next({next_value});
  Cell cell;
  ASSERT_EQ(OB_SUCCESS, prepare(*first.row_, cell, info));
  EXPECT_NE(first_blob.ptr(), cell.native_iter_result().get_reference());
  first.poison(); MEMSET(first_blob.ptr(), 0xA5, first_blob.length());
  auto expect_vector = [&](const std::array<float, 3> &expected) {
    ObIArrayType *array = nullptr;
    ASSERT_EQ(OB_SUCCESS, ObArrayExprUtils::get_type_vector(
        exprs_[0], cell.native_iter_result(), eval_, arena_, array));
    ASSERT_NE(nullptr, array); ASSERT_EQ(3U, array->cardinality());
    const auto *vector = static_cast<const ObVectorF32Data *>(array);
    for (int64_t i = 0; i < 3; ++i) { EXPECT_FLOAT_EQ(expected[i], (*vector)[i]); }
  };
  ASSERT_NO_FATAL_FAILURE(expect_vector({1.25F, 2.0F, 3.0F}));
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, cell, info));
  ASSERT_NO_FATAL_FAILURE(expect_vector({1.75F, 1.5F, 3.5F}));
  processor_.get_removal_info().is_inv_aggr_ = true;
  ASSERT_EQ(OB_SUCCESS, update(*next.row_, cell, info));
  processor_.get_removal_info().is_inv_aggr_ = false;
  next.poison(); MEMSET(next_blob.ptr(), 0xA5, next_blob.length());
  ASSERT_NO_FATAL_FAILURE(expect_vector({1.25F, 2.0F, 3.0F}));
  EXPECT_EQ(0, service.calls_);
}

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
