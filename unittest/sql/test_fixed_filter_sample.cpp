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
#include "sql/engine/px/p2p_datahub/ob_pushdown_topn_filter_msg.h"
#include "sql/engine/px/datahub/components/ob_dh_sample.h"
#include "sql/engine/px/ob_px_coord_op.h"
#include "sql/engine/px/ob_slice_calc.h"
#include "sql/optimizer/stat/ob_hybrid_hist_estimator.h"
#include "sql/engine/basic/ob_material_op_impl.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/charset/ob_charset.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/ob_datum_funcs.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/join/ob_join_filter_op.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
using Row = ObChunkDatumStore::StoredRow;

ObDatum integer(const int64_t &value)
{ return ObDatum(reinterpret_cast<const char *>(&value), sizeof(value), false); }
ObDatum text(const std::string &value)
{ return ObDatum(value.data(), value.size(), false); }
ObDatum null_value()
{ ObDatum value; value.set_null(); return value; }
std::string bytes(const ObDatum &value)
{ return std::string(value.ptr_, value.len_); }

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
  void relocate()
  {
    row_->unswizzling(row_->payload_);
    std::vector<char> moved(memory_);
    MEMSET(memory_.data(), 0xA5, memory_.size());
    memory_.swap(moved);
    row_ = reinterpret_cast<Row *>(memory_.data());
    row_->swizzling(row_->payload_);
  }
  void poison() { MEMSET(memory_.data(), 0xA5, memory_.size()); }
  std::vector<char> memory_;
  Row *row_ = nullptr;
};

// Bind existing private consumer boundaries without a production test hook.
template <typename Tag, typename Tag::Type Member>
struct PrivateMember
{
  friend typename Tag::Type member(Tag) { return Member; }
};
struct TopNValues
{
  using Type = ObFixedArray<Datum, ObIAllocator> ObPushDownTopNFilterMsg::*;
  friend Type member(TopNValues);
};
template struct PrivateMember<TopNValues, &ObPushDownTopNFilterMsg::heap_top_datums_>;
struct TopNComparisons
{
  using Type = ObTopNFilterCompares ObPushDownTopNFilterMsg::*;
  friend Type member(TopNComparisons);
};
template struct PrivateMember<TopNComparisons, &ObPushDownTopNFilterMsg::compares_>;
std::string bytes(const Datum &value)
{ return std::string(value.get_string().ptr(), value.get_string().length()); }

struct SampleAppend
{
  using Type = int (ObDynamicSamplePieceMsgCtx::*)(const ObDynamicSamplePieceMsg &, int64_t, ObChunkDatumStore *);
  friend Type member(SampleAppend);
};
template struct PrivateMember<SampleAppend, &ObDynamicSamplePieceMsgCtx::append_object_sample_data>;
// Only the unused tmp-directory bootstrap is isolated for this five-row,
// in-memory sort fixture. -1 is no acquired directory, not a production
// tmp-file service or a claimed spill-capable mode.
struct SampleSortMemory
{
  using Type = ObSqlMemMgrProcessor ObSortOpImpl::*;
  friend Type member(SampleSortMemory);
};
template struct PrivateMember<SampleSortMemory, &ObSortOpImpl::sql_mem_processor_>;
struct SampleSortDirectory
{
  using Type = int64_t ObSqlMemMgrProcessor::*;
  friend Type member(SampleSortDirectory);
};
template struct PrivateMember<SampleSortDirectory, &ObSqlMemMgrProcessor::dir_id_>;
struct SampleSortStore
{
  using Type = ObChunkDatumStore ObSortOpImpl::*;
  friend Type member(SampleSortStore);
};
template struct PrivateMember<SampleSortStore, &ObSortOpImpl::datum_store_>;

#define MATERIAL_MEMBER(Name, TypeName, Field) \
  struct Name { using Type = TypeName ObMaterialOpImpl::*; friend Type member(Name); }; \
  template struct PrivateMember<Name, &ObMaterialOpImpl::Field>
MATERIAL_MEMBER(MaterialContext, lib::MemoryContext, mem_context_);
MATERIAL_MEMBER(MaterialStore, ObChunkDatumStore, datum_store_);
MATERIAL_MEMBER(MaterialInited, bool, inited_);
MATERIAL_MEMBER(MaterialFirst, bool, got_first_row_);
#undef MATERIAL_MEMBER

class SampleCoordinator final : public ObPxCoordOp
{
public:
  using ObPxCoordOp::ObPxCoordOp;
  int inner_get_next_row() override { return OB_NOT_SUPPORTED; }
private:
  class Listener final : public ObIPxCoordEventListener
  {
    int on_root_data_channel_setup() override { return OB_SUCCESS; }
  } listener_;
  ObIPxCoordEventListener &get_listenner() override { return listener_; }
  void clean_dfos_dtl_interm_result() override {}
};

// The sample boundary borrows this coordinator but does not open PX. Reset
// the shadow while its arena is alive, including on a fatal test assertion.
struct SampleShadowCleanup
{
  explicit SampleShadowCleanup(ObDynamicSamplePieceMsgCtx &ctx) : ctx_(ctx) {}
  ~SampleShadowCleanup() { ctx_.last_store_row_.reset(); }
  ObDynamicSamplePieceMsgCtx &ctx_;
};

class FixedFilterSample : public ::testing::Test
{
protected:
  FixedFilterSample() : arena_(), exec_(arena_), session_(), monitor_() {}
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    exec_.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    exec_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
  }
  void init_topn(ObPushDownTopNFilterMsg &message)
  {
    ObPushDownTopNFilterInfo info(arena_);
    info.p2p_dh_id_ = 17; info.effective_sk_cnt_ = 2; info.total_sk_cnt_ = 2;
    ASSERT_EQ(OB_SUCCESS, info.cmp_metas_.init(2));
    ObSEArray<ObSortFieldCollation, 2> collations;
    for (int64_t col = 0; col < 2; ++col) {
      ObTopNFilterCmpMeta meta;
      const SqlValueType type = col == 0 ? ObVarcharType : ObIntType;
      meta.obj_meta_.set_type(type);
      meta.cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(type, type, NULL_LAST,
          CS_TYPE_BINARY, SCALE_UNKNOWN_YET, false);
      ASSERT_EQ(OB_SUCCESS, info.cmp_metas_.push_back(meta));
      ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(col, CS_TYPE_BINARY, true, NULL_LAST)));
    }
    ASSERT_EQ(OB_SUCCESS, message.init(&info, &collations, &exec_, 1, false));
  }
  void prepare_material(ObAggregateProcessor::HybridHistExtraResult &extra)
  {
    extra.mat_op_ = OB_NEWx(ObMaterialOpImpl, &arena_, monitor_);
    ASSERT_NE(nullptr, extra.mat_op_);
    lib::ContextParam param;
    param.set_mem_attr("FixedFilter").set_properties(lib::USE_TL_PAGE_OPTIONAL);
    ASSERT_EQ(OB_SUCCESS, CURRENT_CONTEXT->CREATE_CONTEXT(extra.mat_op_->*member(MaterialContext{}), param));
    auto &store = extra.mat_op_->*member(MaterialStore{});
    store.set_allocator(arena_);
    ASSERT_EQ(OB_SUCCESS, store.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "FixedFilter", false));
    extra.mat_op_->*member(MaterialInited{}) = true;
    extra.mat_op_->*member(MaterialFirst{}) = true;
  }
  void add_material(ObAggregateProcessor::HybridHistExtraResult &extra, const Row &source,
                      int64_t count, bool popular = false)
  {
    const Row *material = nullptr;
    ASSERT_EQ(OB_SUCCESS, extra.add_material_row(source, sizeof(BucketDesc), material));
    ASSERT_NE(nullptr, material);
    BucketDesc desc; desc.ep_count_ = count; desc.is_pop_ = popular;
    MEMCPY(material->get_extra_payload(), &desc, sizeof(desc));
  }
  void add_range(ObDynamicSamplePieceMsg &piece, int64_t tablet,
                   const std::vector<std::vector<ObDatum>> &values)
  {
    ObPxTabletRange range; range.tablet_id_ = tablet;
    for (const auto &row : values) {
      ObPxTabletRange::DatumKey key;
      for (const auto &value : row) {
        ObPxRangeCell cell;
        ASSERT_EQ(OB_SUCCESS, cell.assign_stored_key_borrowed(value));
        ASSERT_EQ(OB_SUCCESS, key.push_back(cell));
      }
      ASSERT_EQ(OB_SUCCESS, range.range_cut_.push_back(key));
    }
    ASSERT_EQ(OB_SUCCESS, piece.part_ranges_.push_back(range));
  }
  ObArenaAllocator arena_;
  ObExecContext exec_;
  ObSQLSessionInfo session_;
  ObMonitorNode monitor_;
};

TEST_F(FixedFilterSample, topn_updates_own_values_and_keep_flags_after_source_row_overwrite)
{
  ObPushDownTopNFilterMsg message; init_topn(message);
  int64_t low = INT64_MIN;
  std::string source("a\0b", 3);
  ObDatum flagged = text(source); flagged.set_has_lob_header();
  OwnedRow row({flagged, integer(low)}); row.relocate();
  const auto expected_pack = row.row_->stored_cells()[0].pack_;
  const char *source_pointer = row.row_->stored_cells()[0].ptr_;
  bool updated = false;
  ASSERT_EQ(OB_SUCCESS, message.update_filter_data(row.row_, updated)); EXPECT_TRUE(updated);
  EXPECT_FALSE(message.is_empty()); EXPECT_EQ(1, message.get_current_data_version());
  auto &values = message.*member(TopNValues{});
  ASSERT_EQ(2, values.count()); EXPECT_NE(source_pointer, values.at(0).get_reference());
  source.assign(source.size(), 'x'); low = 0; row.poison();
  alignas(8) char topn_scratch[64]; ObDatum topn_physical;
  const SqlTypeMeta &topn_meta = (message.*member(TopNComparisons{})).at(0).build_meta_.obj_meta_;
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::export_native_value(values.at(0), topn_meta,
      topn_physical, topn_scratch, sizeof(topn_scratch)));
  EXPECT_EQ(std::string("a\0b", 3), bytes(values.at(0))); EXPECT_EQ(expected_pack, topn_physical.pack_);
  EXPECT_EQ(INT64_MIN, values.at(1).get_int());
  int64_t high = INT64_MAX; std::string shorter = "z";
  OwnedRow replacement({text(shorter), integer(high)});
  ASSERT_EQ(OB_SUCCESS, message.update_filter_data(replacement.row_, updated)); EXPECT_TRUE(updated);
  replacement.poison();
  ASSERT_EQ(OB_SUCCESS, DatumParameterCodec::export_native_value(values.at(0), topn_meta,
      topn_physical, topn_scratch, sizeof(topn_scratch)));
  EXPECT_EQ("z", bytes(values.at(0))); EXPECT_EQ(ObDatumDesc::NONE, topn_physical.flag_);
  EXPECT_EQ(INT64_MAX, values.at(1).get_int()); EXPECT_EQ(2, message.get_current_data_version());
}

TEST_F(FixedFilterSample, topn_null_in_any_effective_key_does_not_publish_an_update)
{
  ObPushDownTopNFilterMsg message; init_topn(message);
  int64_t key = 19; std::string source = "original";
  OwnedRow initial({text(source), integer(key)}), first_null({null_value(), integer(key)});
  OwnedRow second_null({text(source), null_value()});
  bool updated = false;
  ASSERT_EQ(OB_SUCCESS, message.update_filter_data(first_null.row_, updated));
  EXPECT_FALSE(updated); EXPECT_TRUE(message.is_empty()); EXPECT_EQ(0, message.get_current_data_version());
  ASSERT_EQ(OB_SUCCESS, message.update_filter_data(initial.row_, updated)); ASSERT_TRUE(updated);
  for (Row *row : {first_null.row_, second_null.row_}) {
    updated = true;
    ASSERT_EQ(OB_SUCCESS, message.update_filter_data(row, updated)); EXPECT_FALSE(updated);
    EXPECT_EQ(1, message.get_current_data_version());
    EXPECT_EQ("original", bytes((message.*member(TopNValues{})).at(0)));
    EXPECT_EQ(19, (message.*member(TopNValues{})).at(1).get_int());
  }
}

TEST_F(FixedFilterSample, histogram_material_endpoints_outlive_both_source_and_material_owners)
{
  ObHybridHistograms histogram;
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  {
    ObAggregateProcessor::HybridHistExtraResult extra(arena_, monitor_); prepare_material(extra);
    std::string a("a\0b", 3), b = "beta";
    OwnedRow first({text(a)}), second({text(b)}), last({null_value()}); first.relocate();
    add_material(extra, *first.row_, 2); add_material(extra, *second.row_, 4, true); add_material(extra, *last.row_, 1);
    first.poison(); second.poison(); last.poison(); a.assign(a.size(), 'x'); b.assign(b.size(), 'x');
    ASSERT_EQ(OB_SUCCESS, extra.finish_add_material_row());
    ASSERT_EQ(OB_SUCCESS, histogram.build_hybrid_hist(&extra, &arena_, 3, 7, 3, 1, 4, meta, nullptr));
    ASSERT_EQ(3, histogram.get_bucket_size());
    ObChunkDatumStore::Iterator iterator;
    ASSERT_EQ(OB_SUCCESS, (extra.mat_op_->*member(MaterialStore{})).begin(iterator));
    const Row *material = nullptr;
    for (int64_t i = 0; i < 3; ++i) {
      ASSERT_EQ(OB_SUCCESS, iterator.get_next_row(material));
      const StoredDatumCell &cell = material->stored_cells()[0];
      if (!cell.is_null()) { MEMSET(const_cast<char *>(cell.ptr_), 0xA5, cell.len_); }
    }
    EXPECT_EQ(OB_ITER_END, iterator.get_next_row(material));
  }
  const auto &buckets = histogram.get_buckets();
  EXPECT_EQ(std::string("a\0b", 3), std::string(buckets.at(0).get_endpoint_datum().get_string().ptr(), 3));
  EXPECT_EQ("beta", std::string(buckets.at(1).get_endpoint_datum().get_string().ptr(), 4));
  EXPECT_TRUE(buckets.at(2).get_endpoint_datum().is_null());
  EXPECT_EQ(2, buckets.at(0).endpoint_repeat_count_); EXPECT_EQ(6, buckets.at(1).endpoint_num_);
  EXPECT_EQ(7, buckets.at(2).endpoint_num_); EXPECT_EQ(7, histogram.get_total_count());
  EXPECT_EQ(3, histogram.get_num_distinct()); EXPECT_EQ(1, histogram.get_pop_count());
}

TEST_F(FixedFilterSample, histogram_lob_reuse_reads_the_fixed_endpoint)
{
  ObAggregateProcessor::HybridHistExtraResult extra(arena_, monitor_); prepare_material(extra);
  std::string source = "short inrow text";
  OwnedRow row({text(source)}); add_material(extra, *row.row_, 1); row.poison();
  ASSERT_EQ(OB_SUCCESS, extra.finish_add_material_row());
  SqlTypeMeta meta; meta.set_type(ObLongTextType); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObHybridHistograms histogram;
  ASSERT_EQ(OB_SUCCESS, histogram.build_hybrid_hist(&extra, &arena_, 1, 1, 1, 0, 0, meta, nullptr));
  ASSERT_EQ(1, histogram.get_bucket_size());
  EXPECT_EQ(source, std::string(histogram.get_buckets().at(0).get_endpoint_datum().get_string().ptr(), source.size()));
}

TEST_F(FixedFilterSample, histogram_first_endpoint_failure_does_not_publish_bucket_or_statistics)
{
  ObAggregateProcessor::HybridHistExtraResult extra(arena_, monitor_); prepare_material(extra);
  int64_t coefficient = 7; OwnedRow row({integer(coefficient)}); add_material(extra, *row.row_, 1);
  ASSERT_EQ(OB_SUCCESS, extra.finish_add_material_row());
  SqlTypeMeta meta; meta.set_decimal_int(-1);
  ObHybridHistograms histogram;
  EXPECT_EQ(OB_ERR_UNEXPECTED, histogram.build_hybrid_hist(&extra, &arena_, 1, 1, 1, 0, 0, meta, nullptr));
  EXPECT_EQ(0, histogram.get_bucket_size()); EXPECT_EQ(0, histogram.get_total_count());
  EXPECT_EQ(0, histogram.get_num_distinct());
}

TEST_F(FixedFilterSample, histogram_lob_prefix_without_service_does_not_publish_first_bucket)
{
  ObAggregateProcessor::HybridHistExtraResult extra(arena_, monitor_); prepare_material(extra);
  std::string source(2000, 'a'); OwnedRow row({text(source)}); add_material(extra, *row.row_, 1);
  ASSERT_EQ(OB_SUCCESS, extra.finish_add_material_row());
  SqlTypeMeta meta; meta.set_type(ObLongTextType); meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ObHybridHistograms histogram;
  EXPECT_EQ(OB_NOT_INIT, histogram.build_hybrid_hist(&extra, &arena_, 1, 1, 1, 0, 0, meta, nullptr));
  EXPECT_EQ(0, histogram.get_bucket_size()); EXPECT_EQ(0, histogram.get_total_count());
}

TEST_F(FixedFilterSample, sample_encodes_borrowed_shadow_and_deep_copies_each_matching_range)
{
  ObPxCoordSpec spec(arena_, PHY_PX_FIFO_COORD);
  SampleCoordinator coordinator(exec_, spec, nullptr);
  ObDynamicSamplePieceMsgCtx::SortDef sort;
  ObDynamicSamplePieceMsgCtx context(1, 1, INT64_MAX, exec_, coordinator, sort);
  SampleShadowCleanup cleanup(context);
  ObChunkDatumStore store("FixedSampleTest"); store.set_allocator(arena_);
  ASSERT_EQ(OB_SUCCESS, store.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "FixedSampleTest", false));
  ObDynamicSamplePieceMsg piece;
  ASSERT_EQ(OB_SUCCESS, piece.tablet_ids_.push_back(17));
  int64_t low = INT64_MIN, high = INT64_MAX;
  std::string first("a\0b", 3), second = "z", ignored = "ignored";
  ObDatum flagged = text(first); flagged.set_has_lob_header();
  add_range(piece, 99, {{integer(low), text(ignored), null_value()}});
  add_range(piece, 17, {{integer(low), flagged, null_value()}, {integer(high), text(second), null_value()}});
  ASSERT_EQ(OB_SUCCESS, (context.*member(SampleAppend{}))(piece, 0, &store));
  ASSERT_EQ(2, store.get_row_cnt());
  const Row *shadow = context.last_store_row_.get_store_row(); ASSERT_NE(nullptr, shadow);
  EXPECT_EQ(reinterpret_cast<const char *>(&high), shadow->stored_cells()[0].ptr_);
  EXPECT_EQ(second.data(), shadow->stored_cells()[1].ptr_);
  EXPECT_EQ(sizeof(Row) + 3 * sizeof(StoredDatumCell) + sizeof(high) + second.size(), shadow->row_size_);
  first.assign(first.size(), 'x'); second.assign(second.size(), 'x'); low = high = 0;
  context.last_store_row_.reuse(); piece.reset();
  ObChunkDatumStore::Iterator iterator; ASSERT_EQ(OB_SUCCESS, store.begin(iterator));
  const Row *row = nullptr;
  ASSERT_EQ(OB_SUCCESS, iterator.get_next_row(row)); ASSERT_EQ(3, row->cnt_);
  EXPECT_EQ(INT64_MIN, StoredDatumCodec::decode_legacy_view(row->stored_cells()[0]).get_int());
  EXPECT_EQ(std::string("a\0b", 3), bytes(StoredDatumCodec::decode_legacy_view(row->stored_cells()[1])));
  EXPECT_EQ(ObDatumDesc::HAS_LOB_HEADER, row->stored_cells()[1].flag_); EXPECT_TRUE(row->stored_cells()[2].is_null());
  ASSERT_EQ(OB_SUCCESS, iterator.get_next_row(row));
  EXPECT_EQ(INT64_MAX, StoredDatumCodec::decode_legacy_view(row->stored_cells()[0]).get_int());
  EXPECT_EQ("z", bytes(StoredDatumCodec::decode_legacy_view(row->stored_cells()[1])));
  EXPECT_EQ(ObDatumDesc::NONE, row->stored_cells()[1].flag_);
  EXPECT_EQ(OB_ITER_END, iterator.get_next_row(row));
}

TEST_F(FixedFilterSample, sample_append_failure_does_not_publish_to_uninitialized_store)
{
  ObPxCoordSpec spec(arena_, PHY_PX_FIFO_COORD);
  SampleCoordinator coordinator(exec_, spec, nullptr);
  ObDynamicSamplePieceMsgCtx::SortDef sort;
  ObDynamicSamplePieceMsgCtx context(1, 1, INT64_MAX, exec_, coordinator, sort);
  SampleShadowCleanup cleanup(context);
  ObChunkDatumStore store("FixedSmplErr");
  ObDynamicSamplePieceMsg piece; ASSERT_EQ(OB_SUCCESS, piece.tablet_ids_.push_back(17));
  int64_t value = 7; add_range(piece, 17, {{integer(value)}});
  EXPECT_EQ(OB_NOT_INIT, (context.*member(SampleAppend{}))(piece, 0, &store));
  EXPECT_EQ(0, store.get_row_cnt());
  EXPECT_EQ(OB_ERR_UNEXPECTED, (context.*member(SampleAppend{}))(piece, 1, &store));
  EXPECT_EQ(0, store.get_row_cnt());
}
// These tests use public CG and the real JoinFilter producer. The RF physical
// metadata is never used to synthesize a semantic SqlType.
class NativeRuntimeFilter : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ObExprOperatorFactory::register_expr_operators();
  }
};

template<class Message> struct RuntimeMessage : public Message
{
  ~RuntimeMessage() { (void)this->destroy(); }
};
struct RuntimeMessageCopy
{
  ~RuntimeMessageCopy() { if (value != nullptr) { value->destroy(); ob_free(value); } }
  ObP2PDatahubMsgBase *value = nullptr;
};

class RuntimeInputs
{
public:
  static constexpr int64_t BATCH = 4;
  RuntimeInputs() : owner_("NativeRFTest"), exec_(owner_), eval_(exec_), info_(owner_),
      cg_(owner_, nullptr, nullptr, 0, 0), factory_(owner_), spec_(owner_, PHY_JOIN_FILTER),
      raw_(nullptr), probe_(nullptr), columns_(), coefficients_{}, skip_word_(0) {}

  void prepare(std::initializer_list<SqlValueType> kinds, RuntimeFilterType kind = RuntimeFilterType::IN)
  {
    ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_OP_RUNTIME_FILTER, raw_));
    ASSERT_EQ(OB_SUCCESS, raw_->init_param_exprs(kinds.size()));
    raw_->set_runtime_filter_type(kind);
    ObRawExprResType boolean;
    boolean.set_int32(); boolean.set_accuracy(ObAccuracy(1, 0));
    boolean.set_collation_type(CS_TYPE_BINARY); boolean.set_collation_level(CS_LEVEL_NUMERIC);
    raw_->set_result_type(boolean);
    ASSERT_EQ(OB_SUCCESS, raw_->add_flag(CNT_COLUMN));
    for (SqlValueType value_kind : kinds) {
      ObColumnRefRawExpr *column = nullptr;
      ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_REF_COLUMN, column));
      ObRawExprResType type;
      type.set_type(value_kind);
      type.set_collation_type(value_kind == ObVarcharType ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
      type.set_collation_level(value_kind == ObVarcharType ? CS_LEVEL_IMPLICIT : CS_LEVEL_NUMERIC);
      if (value_kind == ObVarcharType) { type.set_length(64); }
      else { type.set_accuracy(ObAccuracy(value_kind == ObDecimalIntType ? 30 : 20,
                                        value_kind == ObDecimalIntType ? 5 : 0)); }
      column->set_result_type(type);
      ASSERT_EQ(OB_SUCCESS, column->add_flag(IS_COLUMN));
      ASSERT_EQ(OB_SUCCESS, column->add_flag(CNT_COLUMN));
      ASSERT_EQ(OB_SUCCESS, raw_->add_param_expr(column));
      columns_.push_back(column);
    }
    ObRawExprUniqueSet roots(false);
    ASSERT_EQ(OB_SUCCESS, roots.append(raw_));
    cg_.set_batch_size(BATCH);
    ASSERT_EQ(OB_SUCCESS, cg_.generate(roots, info_));
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    ASSERT_EQ(OB_SUCCESS, info_.pre_alloc_exec_memory(exec_));
    eval_.frames_ = exec_.get_frames(); eval_.max_batch_size_ = BATCH;
    probe_ = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*raw_));
    ASSERT_NE(nullptr, probe_);
    ASSERT_EQ(kind == RuntimeFilterType::IN ? &ObExprJoinFilter::eval_in_filter
        : &ObExprJoinFilter::eval_range_filter, probe_->eval_func_);
    ASSERT_EQ(OB_SUCCESS, spec_.join_keys_.init(kinds.size()));
    ASSERT_EQ(OB_SUCCESS, spec_.cmp_funcs_.init(kinds.size()));
    ASSERT_EQ(OB_SUCCESS, spec_.hash_funcs_.init(kinds.size()));
    ASSERT_EQ(OB_SUCCESS, spec_.need_null_cmp_flags_.init(kinds.size()));
    for (ObColumnRefRawExpr *column : columns_) {
      ObExpr *expr = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*column));
      ASSERT_NE(nullptr, expr); ASSERT_NE(nullptr, expr->basic_funcs_);
      ASSERT_EQ(OB_SUCCESS, spec_.join_keys_.push_back(expr));
      ObCmpFunc cmp; cmp.cmp_func_ = expr->basic_funcs_->null_first_cmp_.legacy_function();
      ASSERT_EQ(OB_SUCCESS, spec_.cmp_funcs_.push_back(cmp));
      ObHashFunc hash;
      hash.hash_func_ = expr->basic_funcs_->legacy_functions()->murmur_hash_v2_;
      hash.batch_hash_func_ = expr->basic_funcs_->legacy_functions()->murmur_hash_v2_batch_;
      ASSERT_EQ(OB_SUCCESS, spec_.hash_funcs_.push_back(hash));
      ASSERT_EQ(OB_SUCCESS, spec_.need_null_cmp_flags_.push_back(true));
    }
    config_.runtime_filter_max_in_num_ = 16;
    context_.hash_funcs_.set_allocator(&exec_.get_allocator());
    context_.cmp_funcs_.set_allocator(&exec_.get_allocator());
    ASSERT_EQ(OB_SUCCESS, context_.hash_funcs_.init(spec_.hash_funcs_.count()));
    ASSERT_EQ(OB_SUCCESS, context_.cmp_funcs_.init(spec_.cmp_funcs_.count()));
    ASSERT_EQ(OB_SUCCESS, context_.cmp_funcs_.assign(spec_.cmp_funcs_));
    ASSERT_EQ(OB_SUCCESS, context_.hash_funcs_.assign(spec_.hash_funcs_));
    ASSERT_EQ(OB_SUCCESS, context_.cur_row_.prepare_allocate(kinds.size()));
    ASSERT_EQ(OB_SUCCESS, context_.native_hash_values_.prepare_allocate(BATCH));
    const ObDatumAccessContext *access = nullptr;
    ASSERT_EQ(OB_SUCCESS, eval_.get_datum_access_ctx(access));
  }

  Datum &cell(int64_t column, int64_t row = 0)
  { return spec_.join_keys_.at(column)->locate_batch_datums(eval_)[row]; }
  void ready()
  {
    for (int64_t i = 0; i < spec_.join_keys_.count(); ++i) {
      ObExpr *expr = spec_.join_keys_.at(i);
      expr->get_eval_info(eval_).evaluated_ = true;
      expr->get_evaluated_flags(eval_).set_all(BATCH);
    }
  }
  void decimal(int64_t column, int64_t value, int32_t width, int64_t row = 0)
  {
    coefficients_[row][0] = value; coefficients_[row][1] = value < 0 ? -1 : 0;
    ObDatum source;
    source.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(coefficients_[row]), width);
    ASSERT_EQ(OB_SUCCESS, spec_.join_keys_.at(column)->get_datum_copy_plan().import_legacy(source, cell(column, row)));
  }
  template<class Message> void init(Message &message, ObP2PDatahubMsgBase::ObP2PDatahubMsgType kind)
  {
    message.set_msg_type(kind);
    ASSERT_EQ(OB_SUCCESS, message.init(77, 88, 99, INT64_MAX));
    ASSERT_EQ(OB_SUCCESS, ObJoinFilterOpInput::construct_msg_details(spec_, config_, message, 16));
  }
  int insert(ObP2PDatahubMsgBase &message, int64_t row = 0)
  {
    ready();
    ObEvalCtx::BatchInfoScopeGuard scope(eval_);
    scope.set_batch_size(BATCH); scope.set_batch_idx(row);
    return message.insert_by_row(spec_.join_keys_, spec_.hash_funcs_, nullptr, eval_);
  }
  int probe(ObP2PDatahubMsgBase &message, Datum &result, int64_t row = 0)
  {
    ready();
    ObEvalCtx::BatchInfoScopeGuard scope(eval_);
    scope.set_batch_size(BATCH); scope.set_batch_idx(row);
    return message.might_contain(*probe_, eval_, context_, result);
  }
  int batch_probe(ObP2PDatahubMsgBase &message)
  { ready(); return message.might_contain_batch(*probe_, eval_, skip(), BATCH, context_); }
  ObBitVector &skip() { return *to_bit_vector(&skip_word_); }
  ObArenaAllocator owner_;
  ObExecContext exec_;
  ObEvalCtx eval_;
  ObExprFrameInfo info_;
  ObStaticEngineExprCG cg_;
  ObRawExprFactory factory_;
  ObJoinFilterSpec spec_;
  ObJoinFilterRuntimeConfig config_;
  ObOpRawExpr *raw_;
  ObExpr *probe_;
  std::vector<ObColumnRefRawExpr *> columns_;
  alignas(8) int64_t coefficients_[BATCH][2];
  uint64_t skip_word_;
  ObExprJoinFilter::ObExprJoinFilterContext context_;
};

template<class Message> std::vector<char> runtime_wire(const Message &message)
{
  std::vector<char> result(message.get_serialize_size(), 0);
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, message.serialize(result.data(), result.size(), pos));
  EXPECT_EQ(static_cast<int64_t>(result.size()), pos);
  return result;
}

std::string native_text(const Datum &value)
{ return value.length() == 0 ? std::string() : std::string(static_cast<const char *>(value.get_reference()), value.length()); }

class RuntimeFailAllocator : public ObIAllocator
{
public:
  void *alloc(int64_t size) override { ++calls_; return fail_ ? nullptr : arena_.alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *value) override { arena_.free(value); }
  ObArenaAllocator arena_;
  bool fail_ = false;
  int64_t calls_ = 0;
};

TEST_F(NativeRuntimeFilter, actual_scalar_range_is_inline_and_batch_probe_preserves_skip)
{
  RuntimeInputs input; input.prepare({ObIntType}, RuntimeFilterType::RANGE);
  RuntimeMessage<ObRFRangeFilterMsg> message;
  input.init(message, ObP2PDatahubMsgBase::RANGE_FILTER_MSG);
  auto &owner = static_cast<ObArenaAllocator &>(message.get_allocator());
  const int64_t before = owner.used();
  input.cell(0).set_int(-7); ASSERT_EQ(OB_SUCCESS, input.insert(message));
  input.cell(0).set_int(19); ASSERT_EQ(OB_SUCCESS, input.insert(message));
  EXPECT_EQ(before, owner.used()); // Only the insertion phase: arrays already exist.
  EXPECT_EQ(-7, message.lower_bounds_.at(0).get_int());
  EXPECT_EQ(19, message.upper_bounds_.at(0).get_int());
  EXPECT_EQ(0U, message.lower_bounds_.at(0).length());
  EXPECT_EQ(0, message.cells_size_.at(0).min_datum_buf_size_);
  EXPECT_EQ(0, message.cells_size_.at(0).max_datum_buf_size_);
  input.cell(0, 0).set_int(-8); input.cell(0, 1).set_int(-7);
  input.cell(0, 2).set_int(123); input.cell(0, 3).set_int(19);
  input.skip().reset(RuntimeInputs::BATCH); input.skip().set(2);
  Datum *result = input.probe_->locate_batch_datums(input.eval_);
  for (int i = 0; i < RuntimeInputs::BATCH; ++i) { result[i].set_int(555); }
  ASSERT_EQ(OB_SUCCESS, input.batch_probe(message));
  EXPECT_EQ(0, result[0].get_int()); EXPECT_EQ(1, result[1].get_int());
  EXPECT_EQ(555, result[2].get_int()); EXPECT_EQ(1, result[3].get_int());
  EXPECT_EQ(3, input.context_.check_count_); EXPECT_EQ(1, input.context_.filter_count_);
  for (int i = 0; i < RuntimeInputs::BATCH; ++i) {
    EXPECT_TRUE(input.probe_->get_evaluated_flags(input.eval_).at(i));
  }
  input.cell(0).set_null(); ASSERT_EQ(OB_SUCCESS, input.insert(message));
  EXPECT_TRUE(message.lower_bounds_.at(0).is_null());
  EXPECT_EQ(0U, message.lower_bounds_.at(0).length());
  EXPECT_EQ(before, owner.used());
  input.cell(0).set_int(INT64_MIN);
  Datum scalar; ASSERT_EQ(OB_SUCCESS, input.probe(message, scalar)); EXPECT_EQ(1, scalar.get_int());
}

TEST_F(NativeRuntimeFilter, actual_in_rows_own_references_and_preserve_selected_hash_seed)
{
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType, ObDecimalIntType});
  RuntimeMessage<ObRFInFilterMsg> message;
  input.init(message, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  std::string source("a\0b", 3);
  input.cell(0).set_int(INT64_MIN); input.cell(1).set_string(source.data(), source.size());
  input.decimal(2, -7, 4);
  ASSERT_EQ(OB_SUCCESS, input.insert(message));
  ASSERT_EQ(OB_SUCCESS, input.insert(message)); // Actual hash duplicate path.
  ASSERT_EQ(1, message.serial_rows_.count()); EXPECT_EQ(1, message.rows_set_.size());
  const auto &row = *message.serial_rows_.at(0);
  EXPECT_EQ(INT64_MIN, row.at(0).get_int()); EXPECT_EQ(0U, row.at(0).length());
  EXPECT_NE(source.data(), row.at(1).get_reference());
  EXPECT_EQ(16U, row.at(2).length()); // Actual precision-30 callback boundary.
  const int64_t expected_coefficient[2] = {-7, -1};
  EXPECT_EQ(0, MEMCMP(row.at(2).get_reference(), expected_coefficient, 16));
  uint64_t expected_hash = 4294967279ULL;
  const int64_t expected_integer = INT64_MIN;
  const char expected_text[] = {'a', '\0', 'b'};
  ObDatum oracle[3];
  oracle[0] = integer(expected_integer);
  oracle[1].set_string(expected_text, sizeof(expected_text));
  oracle[2].set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(expected_coefficient), 16);
  for (int i = 0; i < 3; ++i) {
    ASSERT_EQ(OB_SUCCESS, input.spec_.hash_funcs_.at(i).hash_func_(oracle[i], expected_hash, expected_hash, nullptr));
  }
  ObRFInFilterMsg::ObRFInFilterNode node(&message.cmp_funcs_, &message.hash_funcs_for_insert_,
      message.serial_rows_.at(0), &message.build_obj_metas_, nullptr);
  uint64_t actual_hash = 0; ASSERT_EQ(OB_SUCCESS, node.hash(actual_hash)); EXPECT_EQ(expected_hash, actual_hash);
  source.assign(3, 'x'); input.coefficients_[0][0] = 999;
  EXPECT_EQ(std::string(expected_text, sizeof(expected_text)), native_text(row.at(1)));
  EXPECT_EQ(0, MEMCMP(row.at(2).get_reference(), expected_coefficient, 16));
  input.cell(1).set_string(expected_text, sizeof(expected_text)); input.decimal(2, -7, 8);
  Datum result; ASSERT_EQ(OB_SUCCESS, input.probe(message, result)); EXPECT_EQ(1, result.get_int());
  input.cell(0).set_int(23); ASSERT_EQ(OB_SUCCESS, input.probe(message, result)); EXPECT_EQ(0, result.get_int());
  input.cell(0).set_null(); input.cell(1).set_string("", 0); input.cell(2).set_null();
  ASSERT_EQ(OB_SUCCESS, input.insert(message)); ASSERT_EQ(2, message.serial_rows_.count());
  ASSERT_EQ(OB_SUCCESS, input.probe(message, result)); EXPECT_EQ(1, result.get_int());
  EXPECT_TRUE(message.serial_rows_.at(1)->at(0).is_null());
  EXPECT_TRUE(message.serial_rows_.at(1)->at(2).is_null());
  EXPECT_EQ(0U, message.serial_rows_.at(1)->at(2).length());
  message.need_null_cmp_flags_.at(0) = false;
  input.cell(1).set_string("ignored", 7);
  ASSERT_EQ(OB_SUCCESS, input.insert(message)); EXPECT_EQ(2, message.serial_rows_.count());
}

TEST_F(NativeRuntimeFilter, actual_batch_in_probe_and_cross_type_scalar_probe_use_operand_types)
{
  RuntimeInputs input; input.prepare({ObIntType});
  RuntimeMessage<ObRFInFilterMsg> message; input.init(message, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  input.cell(0, 0).set_int(7); input.cell(0, 1).set_int(8);
  input.cell(0, 2).set_int(9); input.cell(0, 3).set_int(INT64_MAX);
  input.ready(); input.skip().reset(RuntimeInputs::BATCH); input.skip().set(1); input.skip().set(2);
  ObBatchRows batch(input.skip(), RuntimeInputs::BATCH, false);
  uint64_t batch_hashes[RuntimeInputs::BATCH] = {};
  ASSERT_EQ(OB_SUCCESS, message.insert_by_row_batch(&batch, input.spec_.join_keys_,
      input.spec_.hash_funcs_, nullptr, input.eval_, batch_hashes));
  ASSERT_EQ(2, message.serial_rows_.count());
  const int64_t values[] = {7, 8, -1, INT64_MAX};
  for (int i = 0; i < RuntimeInputs::BATCH; ++i) { input.cell(0, i).set_int(values[i]); }
  input.skip().reset(RuntimeInputs::BATCH); input.skip().set(2);
  Datum *results = input.probe_->locate_batch_datums(input.eval_);
  for (int i = 0; i < RuntimeInputs::BATCH; ++i) { results[i].set_int(123); }
  ASSERT_EQ(OB_SUCCESS, input.batch_probe(message));
  EXPECT_EQ(1, results[0].get_int()); EXPECT_EQ(0, results[1].get_int());
  EXPECT_EQ(123, results[2].get_int()); EXPECT_EQ(1, results[3].get_int());
  RuntimeInputs probe; probe.prepare({ObUInt64Type});
  probe.context_.cmp_funcs_.at(0).cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(
      ObUInt64Type, ObIntType, NULL_FIRST, CS_TYPE_BINARY, SCALE_UNKNOWN_YET, false);
  ASSERT_NE(nullptr, probe.context_.cmp_funcs_.at(0).cmp_func_);
  Datum result;
  probe.cell(0).set_uint(INT64_MAX); ASSERT_EQ(OB_SUCCESS, probe.probe(message, result)); EXPECT_EQ(1, result.get_int());
  probe.cell(0).set_uint(UINT64_MAX); ASSERT_EQ(OB_SUCCESS, probe.probe(message, result)); EXPECT_EQ(0, result.get_int());
  message.max_in_num_ = 1;
  input.cell(0).set_int(9); ASSERT_EQ(OB_SUCCESS, input.insert(message));
  EXPECT_FALSE(message.is_active()); // Existing strict count > maximum check.
  ASSERT_EQ(OB_SUCCESS, probe.probe(message, result)); EXPECT_EQ(1, result.get_int());
}

TEST_F(NativeRuntimeFilter, merge_deep_copy_and_reuse_keep_distinct_reference_owners)
{
  RuntimeInputs input; input.prepare({ObVarcharType});
  RuntimeMessage<ObRFInFilterMsg> left, right;
  RuntimeMessage<ObRFRangeFilterMsg> low, high;
  input.init(left, ObP2PDatahubMsgBase::IN_FILTER_MSG); input.init(right, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  input.init(low, ObP2PDatahubMsgBase::RANGE_FILTER_MSG); input.init(high, ObP2PDatahubMsgBase::RANGE_FILTER_MSG);
  std::string source = "alpha";
  input.cell(0).set_string(source.data(), source.size());
  ASSERT_EQ(OB_SUCCESS, input.insert(left)); ASSERT_EQ(OB_SUCCESS, input.insert(low));
  source = "zeta"; input.cell(0).set_string(source.data(), source.size());
  ASSERT_EQ(OB_SUCCESS, input.insert(right)); ASSERT_EQ(OB_SUCCESS, input.insert(high));
  ASSERT_EQ(OB_SUCCESS, left.merge(right)); ASSERT_EQ(OB_SUCCESS, low.merge(high));
  RuntimeMessageCopy in_copy, range_copy;
  ASSERT_EQ(OB_SUCCESS, left.deep_copy_msg(in_copy.value));
  ASSERT_EQ(OB_SUCCESS, low.deep_copy_msg(range_copy.value));
  auto &copied_in = static_cast<ObRFInFilterMsg &>(*in_copy.value);
  auto &copied_range = static_cast<ObRFRangeFilterMsg &>(*range_copy.value);
  EXPECT_NE(left.serial_rows_.at(0)->at(0).get_reference(), copied_in.serial_rows_.at(0)->at(0).get_reference());
  EXPECT_NE(low.lower_bounds_.at(0).get_reference(), copied_range.lower_bounds_.at(0).get_reference());
  source.assign(source.size(), 'x');
  ASSERT_EQ(OB_SUCCESS, left.destroy()); ASSERT_EQ(OB_SUCCESS, right.destroy());
  ASSERT_EQ(OB_SUCCESS, low.destroy()); ASSERT_EQ(OB_SUCCESS, high.destroy());
  EXPECT_EQ("alpha", native_text(copied_in.serial_rows_.at(0)->at(0)));
  EXPECT_EQ("zeta", native_text(copied_in.serial_rows_.at(1)->at(0)));
  EXPECT_EQ("alpha", native_text(copied_range.lower_bounds_.at(0)));
  EXPECT_EQ("zeta", native_text(copied_range.upper_bounds_.at(0)));
  ASSERT_EQ(OB_SUCCESS, copied_in.reuse()); EXPECT_TRUE(copied_in.is_empty()); EXPECT_EQ(0, copied_in.rows_set_.size());
  input.cell(0).set_string("next", 4); ASSERT_EQ(OB_SUCCESS, input.insert(copied_in));
  EXPECT_EQ("next", native_text(copied_in.serial_rows_.at(0)->at(0)));
  ASSERT_EQ(OB_SUCCESS, copied_range.reuse()); EXPECT_TRUE(copied_range.is_empty());
  ASSERT_EQ(OB_SUCCESS, input.insert(copied_range)); EXPECT_EQ("next", native_text(copied_range.lower_bounds_.at(0)));
}

TEST_F(NativeRuntimeFilter, reference_allocation_failure_keeps_existing_bound_and_hash_row)
{
  RuntimeInputs input; input.prepare({ObVarcharType});
  RuntimeFailAllocator range_owner, in_owner;
  ModulePageAllocator range_pages(range_owner), in_pages(in_owner);
  RuntimeMessage<ObRFRangeFilterMsg> range;
  RuntimeMessage<ObRFInFilterMsg> in;
  ASSERT_EQ(OB_SUCCESS, static_cast<ObArenaAllocator &>(range.get_allocator()).get_arena().init(8192, range_pages));
  ASSERT_EQ(OB_SUCCESS, static_cast<ObArenaAllocator &>(in.get_allocator()).get_arena().init(8192, in_pages));
  input.init(range, ObP2PDatahubMsgBase::RANGE_FILTER_MSG); input.init(in, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  input.cell(0).set_string("middle", 6);
  ASSERT_EQ(OB_SUCCESS, input.insert(range)); ASSERT_EQ(OB_SUCCESS, input.insert(in));
  const void *lower = range.lower_bounds_.at(0).get_reference();
  const void *upper = range.upper_bounds_.at(0).get_reference();
  const void *stored = in.serial_rows_.at(0)->at(0).get_reference();
  const int64_t range_calls = range_owner.calls_, in_calls = in_owner.calls_;
  range_owner.fail_ = true; in_owner.fail_ = true;
  std::string large(1024 * 1024, 'a'); input.cell(0).set_string(large.data(), large.size());
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, input.insert(range));
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, input.insert(in));
  EXPECT_GT(range_owner.calls_, range_calls); EXPECT_GT(in_owner.calls_, in_calls);
  EXPECT_EQ(lower, range.lower_bounds_.at(0).get_reference()); EXPECT_EQ(upper, range.upper_bounds_.at(0).get_reference());
  EXPECT_EQ("middle", native_text(range.lower_bounds_.at(0))); EXPECT_EQ("middle", native_text(range.upper_bounds_.at(0)));
  ASSERT_EQ(1, in.serial_rows_.count()); EXPECT_EQ(1, in.rows_set_.size());
  EXPECT_EQ(stored, in.serial_rows_.at(0)->at(0).get_reference());
  range_owner.fail_ = false; in_owner.fail_ = false;
  ASSERT_EQ(OB_SUCCESS, input.insert(range)); ASSERT_EQ(OB_SUCCESS, input.insert(in));
  large.assign(large.size(), 'x');
  EXPECT_EQ('a', native_text(range.lower_bounds_.at(0)).front());
  EXPECT_EQ('a', native_text(in.serial_rows_.at(1)->at(0)).front());
}

TEST_F(NativeRuntimeFilter, query_range_projection_and_storage_export_have_real_destination_owners)
{
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType});
  RuntimeMessage<ObRFRangeFilterMsg> range;
  RuntimeMessage<ObRFInFilterMsg> in;
  input.init(range, ObP2PDatahubMsgBase::RANGE_FILTER_MSG); input.init(in, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  input.cell(0).set_int(-7); input.cell(1).set_string("alpha", 5);
  ASSERT_EQ(OB_SUCCESS, input.insert(range)); ASSERT_EQ(OB_SUCCESS, input.insert(in));
  input.cell(0).set_int(19); input.cell(1).set_string("zeta", 4);
  ASSERT_EQ(OB_SUCCESS, input.insert(range)); ASSERT_EQ(OB_SUCCESS, input.insert(in));
  ObSEArray<int64_t, 2> prefix; ObSEArray<SqlTypeMeta, 2> meta;
  ASSERT_EQ(OB_SUCCESS, prefix.push_back(1)); ASSERT_EQ(OB_SUCCESS, prefix.push_back(0));
  ASSERT_EQ(OB_SUCCESS, meta.push_back(input.spec_.join_keys_.at(1)->obj_meta_));
  ASSERT_EQ(OB_SUCCESS, meta.push_back(input.spec_.join_keys_.at(0)->obj_meta_));
  ObPxQueryRangeInfo query_info(input.owner_);
  ASSERT_EQ(OB_SUCCESS, query_info.init(991, 3, prefix, meta));
  ASSERT_EQ(OB_SUCCESS, range.init_query_range_info(query_info)); ASSERT_EQ(OB_SUCCESS, in.init_query_range_info(query_info));
  range.after_process(); in.after_process();
  ObArenaAllocator destination;
  ObSEArray<ObNewRange, 2> range_result, in_result;
  bool extracted = false;
  ASSERT_EQ(OB_SUCCESS, range.try_extract_query_range(extracted, range_result, true, &destination)); ASSERT_TRUE(extracted);
  ASSERT_EQ(OB_SUCCESS, in.try_extract_query_range(extracted, in_result, true, &destination)); ASSERT_TRUE(extracted);
  ASSERT_EQ(1, range_result.count()); ASSERT_EQ(2, in_result.count());
  EXPECT_EQ(ObString::make_string("alpha"), range_result.at(0).start_key_.get_cell(0).get_varchar());
  EXPECT_EQ(ObString::make_string("zeta"), range_result.at(0).end_key_.get_cell(0).get_varchar());
  EXPECT_TRUE(range_result.at(0).start_key_.get_cell(1).is_min_value());
  EXPECT_TRUE(range_result.at(0).end_key_.get_cell(2).is_max_value());
  EXPECT_EQ(-7, in_result.at(0).start_key_.get_cell(1).get_int());
  EXPECT_EQ(19, in_result.at(1).start_key_.get_cell(1).get_int());
  ObPushdownExprSpec push_spec(destination); ObPushdownOperator push_op(input.eval_, push_spec);
  ObPushdownDynamicFilterNode node(destination); node.set_col_idx(0);
  ObDynamicFilterExecutor executor(destination, node, push_op);
  ObRuntimeFilterParams params; bool prepared = false;
  ASSERT_EQ(OB_SUCCESS, range.prepare_storage_white_filter_data(executor, input.eval_, params, prepared));
  ASSERT_TRUE(prepared); ASSERT_EQ(2, params.count());
  EXPECT_EQ(8U, params.at(0).len_); // Deliberate existing storage ABI, not an RF scalar owner.
  ASSERT_EQ(OB_SUCCESS, range.destroy()); ASSERT_EQ(OB_SUCCESS, in.destroy());
  EXPECT_EQ(-7, params.at(0).get_int()); EXPECT_EQ(19, params.at(1).get_int());
  EXPECT_EQ(ObString::make_string("alpha"), range_result.at(0).start_key_.get_cell(0).get_varchar());
  EXPECT_EQ(-7, in_result.at(0).start_key_.get_cell(1).get_int());
}

TEST_F(NativeRuntimeFilter, v2_roundtrip_is_owned_and_malformed_metadata_never_becomes_pass_all)
{
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType, ObDecimalIntType});
  RuntimeMessage<ObRFRangeFilterMsg> range, range_copy;
  RuntimeMessage<ObRFInFilterMsg> in, in_copy;
  input.init(range, ObP2PDatahubMsgBase::RANGE_FILTER_MSG); input.init(in, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  input.cell(0).set_int(-7); input.cell(1).set_string("alpha", 5); input.decimal(2, -7, 4);
  ASSERT_EQ(OB_SUCCESS, input.insert(range)); ASSERT_EQ(OB_SUCCESS, input.insert(in));
  input.cell(0).set_null(); input.cell(1).set_string("", 0); input.cell(2).set_null();
  ASSERT_EQ(OB_SUCCESS, input.insert(range)); ASSERT_EQ(OB_SUCCESS, input.insert(in));
  std::vector<char> range_wire = runtime_wire(range), in_wire = runtime_wire(in);
  ASSERT_EQ(2, range_wire[0]); ASSERT_EQ(2, in_wire[0]);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, range_copy.deserialize(range_wire.data(), range_wire.size(), pos)); EXPECT_EQ(range_wire.size(), pos);
  pos = 0; ASSERT_EQ(OB_SUCCESS, in_copy.deserialize(in_wire.data(), in_wire.size(), pos)); EXPECT_EQ(in_wire.size(), pos);
  EXPECT_FALSE(range_copy.check_ready()); EXPECT_FALSE(in_copy.check_ready());
  // Receiver reference capacity is its actual allocated span, not the sender's
  // spare growth capacity. The value/metadata protocol is otherwise unchanged.
  EXPECT_EQ(2, runtime_wire(range_copy)[0]); EXPECT_EQ(in_wire, runtime_wire(in_copy));
  ObSEArray<int64_t, 1> prefix; ObSEArray<SqlTypeMeta, 1> prefix_meta;
  ASSERT_EQ(OB_SUCCESS, prefix.push_back(0));
  ASSERT_EQ(OB_SUCCESS, prefix_meta.push_back(input.spec_.join_keys_.at(0)->obj_meta_));
  ObPxQueryRangeInfo query(input.owner_); ASSERT_EQ(OB_SUCCESS, query.init(991, 1, prefix, prefix_meta));
  ASSERT_EQ(OB_SUCCESS, range_copy.init_query_range_info(query)); range_copy.after_process();
  ASSERT_TRUE(range_copy.is_query_range_ready_);
  pos = 0; ASSERT_EQ(OB_SUCCESS, range_copy.deserialize(range_wire.data(), range_wire.size(), pos));
  EXPECT_FALSE(range_copy.is_query_range_ready_); // Old projection owner was retired.
  ASSERT_EQ(OB_SUCCESS, range.destroy()); ASSERT_EQ(OB_SUCCESS, in.destroy());
  MEMSET(range_wire.data(), 0xa5, range_wire.size()); MEMSET(in_wire.data(), 0xa5, in_wire.size());
  EXPECT_TRUE(range_copy.lower_bounds_.at(0).is_null()); EXPECT_EQ(0U, range_copy.lower_bounds_.at(0).length());
  EXPECT_TRUE(range_copy.lower_bounds_.at(2).is_null()); EXPECT_EQ(0U, range_copy.lower_bounds_.at(2).length());
  EXPECT_EQ("alpha", native_text(in_copy.serial_rows_.at(0)->at(1)));
  EXPECT_EQ(16U, in_copy.serial_rows_.at(0)->at(2).length());
  const auto valid = runtime_wire(in_copy);
  for (int64_t length : {int64_t(0), int64_t(valid.size() / 2), int64_t(valid.size() - 1)}) {
    RuntimeMessage<ObRFInFilterMsg> rejected; int64_t rejected_pos = 0;
    EXPECT_NE(OB_SUCCESS, rejected.deserialize(valid.data(), length, rejected_pos));
    EXPECT_EQ(0, rejected_pos); EXPECT_FALSE(rejected.check_ready()); EXPECT_EQ(0, rejected.serial_rows_.count());
  }
  auto corrupt = valid;
  const int64_t meta_start = corrupt.size() - in_copy.build_obj_metas_.get_serialize_size();
  ASSERT_EQ(3, corrupt[meta_start]); corrupt[meta_start + 1] = static_cast<char>(0xff);
  const int64_t old_count = in_copy.serial_rows_.count(); pos = 0;
  EXPECT_EQ(OB_DESERIALIZE_ERROR, in_copy.deserialize(corrupt.data(), corrupt.size(), pos));
  EXPECT_EQ(0, pos); EXPECT_EQ(old_count, in_copy.serial_rows_.count()); EXPECT_TRUE(in_copy.is_active());
  // An incomplete *present* tail is malformed even when the outer length is
  // internally consistent; it is not the permitted complete v1 omission.
  corrupt = valid; corrupt.pop_back();
  int64_t header_pos = 0, version = 0, body_size = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(corrupt.data(), corrupt.size(), header_pos, &version));
  const int64_t length_pos = header_pos;
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(corrupt.data(), corrupt.size(), header_pos, &body_size));
  int64_t written = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(corrupt.data() + length_pos,
      header_pos - length_pos, written, corrupt.size() - header_pos));
  pos = 0; EXPECT_EQ(OB_DESERIALIZE_ERROR, in_copy.deserialize(corrupt.data(), corrupt.size(), pos));
  EXPECT_EQ(0, pos); EXPECT_EQ(old_count, in_copy.serial_rows_.count());
  in_copy.set_is_ready(true); pos = 0;
  EXPECT_EQ(OB_STATE_NOT_MATCH, in_copy.deserialize(valid.data(), valid.size(), pos));
  EXPECT_EQ(0, pos); EXPECT_TRUE(in_copy.check_ready());

  // Current constructors publish canonical NULL endpoints even before the
  // first row. Both active-empty and inactive v2 states retain real metadata.
  for (bool active : {true, false}) {
    RuntimeMessage<ObRFRangeFilterMsg> empty, received_empty;
    input.init(empty, ObP2PDatahubMsgBase::RANGE_FILTER_MSG);
    ASSERT_EQ(OB_SUCCESS, empty.init_query_range_info(query));
    empty.set_is_active(active);
    ASSERT_TRUE(empty.is_empty());
    auto empty_wire = runtime_wire(empty);
    ASSERT_EQ(2, empty_wire[0]);
    pos = 0;
    ASSERT_EQ(OB_SUCCESS, received_empty.deserialize(empty_wire.data(), empty_wire.size(), pos));
    EXPECT_EQ(static_cast<int64_t>(empty_wire.size()), pos);
    EXPECT_EQ(active, received_empty.is_active()); EXPECT_TRUE(received_empty.is_empty());
    ASSERT_EQ(3, received_empty.lower_bounds_.count()); ASSERT_EQ(3, received_empty.upper_bounds_.count());
    ASSERT_EQ(3, received_empty.build_obj_metas_.count());
    for (int64_t column = 0; column < 3; ++column) {
      EXPECT_TRUE(received_empty.lower_bounds_.at(column).is_null());
      EXPECT_TRUE(received_empty.upper_bounds_.at(column).is_null());
      EXPECT_EQ(0U, received_empty.lower_bounds_.at(column).length());
      EXPECT_EQ(0U, received_empty.upper_bounds_.at(column).length());
      EXPECT_EQ(input.spec_.join_keys_.at(column)->obj_meta_.get_type(),
                received_empty.build_obj_metas_.at(column).get_type());
    }
    EXPECT_EQ(5, received_empty.build_obj_metas_.at(2).get_scale());
    EXPECT_EQ(empty_wire, runtime_wire(received_empty));
    MEMSET(empty_wire.data(), 0xa5, empty_wire.size());
    Datum result; ASSERT_EQ(OB_SUCCESS, input.probe(received_empty, result));
    EXPECT_EQ(active ? 0 : 1, result.get_int());
    received_empty.after_process();
    EXPECT_EQ(active, received_empty.is_query_range_ready_);
  }
}

// Immutable root captures from the unchanged 5bc library's real v1 serializers.
// Captured function-address bytes remain in these literals. Only the known
// selected-function arrays are explicitly relocated to this test process below.
// The two optional-tail records are labelled derivatives, not old-binary captures.
struct RuntimeV1Capture { const char *name; const char *hex; bool range; bool omission; };
static const RuntimeV1Capture RUNTIME_V1_CAPTURES[] = {
  {"in-empty-v1.bin",
    "01af81808000019480808000018480808000000000004d5863959aef3a0301010003018780808000e0d6a3bde9a405018780"
    "808000d0fdd3bde9a405018780808000c0f2c0bde9a40503018e8080800090e3a7bde9a405d0e5a7bde9a405018e80808000"
    "f09ad4bde9a405c09dd4bde9a405018e80808000d0d6c6bde9a40590d9c6bde9a405031003010101019680808000ffffffff"
    "ffffffffff01ffffffffffffffffff0100000305053f0016022eff27053f05"
    , false, false},
  {"in-inactive-v1.bin",
    "01af81808000019480808000018480808000000000004d5863959aef3a0300000003018780808000e0d6a3bde9a405018780"
    "808000d0fdd3bde9a405018780808000c0f2c0bde9a40503018e8080800090e3a7bde9a405d0e5a7bde9a405018e80808000"
    "f09ad4bde9a405c09dd4bde9a405018e80808000d0d6c6bde9a40590d9c6bde9a405031003010101019680808000ffffffff"
    "ffffffffff01ffffffffffffffffff0100000305053f0016022eff27053f05"
    , false, false},
  {"in-null-v1.bin",
    "01b282808000019480808000018480808000000000004d5863959aef3a0301000303018780808000e0d6a3bde9a405018780"
    "808000d0fdd3bde9a405018780808000c0f2c0bde9a40503018e8080800090e3a7bde9a405d0e5a7bde9a405018e80808000"
    "f09ad4bde9a405c09dd4bde9a405018e80808000d0d6c6bde9a40590d9c6bde9a40503100301010103018980808000080000"
    "00000000008001868080800005616c70686101918080800010f9ffffffffffffffffffffffffffffff0301898080800008ff"
    "ffffffffffff7f018580808000047a6574610191808080001013000000000000000000000000000000030185808080008080"
    "808008018180808000000185808080008080808008019680808000ffffffffffffffffff01ffffffffffffffffff01000003"
    "05053f0016022eff27053f05"
    , false, false},
  {"in-values-v1-optional-meta-omission.bin",
    "018782808000019480808000018480808000000000004d5863959aef3a0301000203018780808000e0d6a3bde9a405018780"
    "808000d0fdd3bde9a405018780808000c0f2c0bde9a40503018e8080800090e3a7bde9a405d0e5a7bde9a405018e80808000"
    "f09ad4bde9a405c09dd4bde9a405018e80808000d0d6c6bde9a40590d9c6bde9a40503100301010103018980808000080000"
    "00000000008001868080800005616c70686101918080800010f9ffffffffffffffffffffffffffffff0301898080800008ff"
    "ffffffffffff7f018580808000047a6574610191808080001013000000000000000000000000000000019680808000ffffff"
    "ffffffffffff01ffffffffffffffffff010000"
    , false, true},
  {"in-values-v1.bin",
    "019482808000019480808000018480808000000000004d5863959aef3a0301000203018780808000e0d6a3bde9a405018780"
    "808000d0fdd3bde9a405018780808000c0f2c0bde9a40503018e8080800090e3a7bde9a405d0e5a7bde9a405018e80808000"
    "f09ad4bde9a405c09dd4bde9a405018e80808000d0d6c6bde9a40590d9c6bde9a40503100301010103018980808000080000"
    "00000000008001868080800005616c70686101918080800010f9ffffffffffffffffffffffffffffff0301898080800008ff"
    "ffffffffffff7f018580808000047a6574610191808080001013000000000000000000000000000000019680808000ffffff"
    "ffffffffffff01ffffffffffffffffff0100000305053f0016022eff27053f05"
    , false, false},
  {"range-empty-v1.bin",
    "01b481808000019480808000018480808000000000004d5863959aef3a020101030181808080000001818080800000018180"
    "8080000003018180808000000181808080000001818080800000030101010301828080800000000182808080000000018280"
    "808000000003018780808000e0d6a3bde9a405018780808000d0fdd3bde9a405018780808000c0f2c0bde9a4050196808080"
    "00ffffffffffffffffff01ffffffffffffffffff0100000305053f0016022eff27053f05"
    , true, false},
  {"range-stale-null-v1.bin",
    "01f081808000019480808000018480808000000000004d5863959aef3a02010003018d808080008880808008000000000000"
    "0080018180808000000195808080009080808008f9ffffffffffffffffffffffffffffff0301898080800008ffffffffffff"
    "ff7f018580808000047a65746101918080800010130000000000000000000000000000000301010103018280808000101001"
    "82808080000a0a018280808000202003018780808000e0d6a3bde9a405018780808000d0fdd3bde9a405018780808000c0f2"
    "c0bde9a405019680808000ffffffffffffffffff01ffffffffffffffffff0100000305053f0016022eff27053f05"
    , true, false},
  {"range-values-v1-optional-meta-omission.bin",
    "01e081808000019480808000018480808000000000004d5863959aef3a020100030189808080000800000000000000800186"
    "8080800005616c70686101918080800010f9ffffffffffffffffffffffffffffff0301898080800008ffffffffffffff7f01"
    "8580808000047a65746101918080800010130000000000000000000000000000000301010103018280808000101001828080"
    "80000a0a018280808000202003018780808000e0d6a3bde9a405018780808000d0fdd3bde9a405018780808000c0f2c0bde9"
    "a405019680808000ffffffffffffffffff01ffffffffffffffffff010000"
    , true, true},
  {"range-values-v1.bin",
    "01ed81808000019480808000018480808000000000004d5863959aef3a020100030189808080000800000000000000800186"
    "8080800005616c70686101918080800010f9ffffffffffffffffffffffffffffff0301898080800008ffffffffffffff7f01"
    "8580808000047a65746101918080800010130000000000000000000000000000000301010103018280808000101001828080"
    "80000a0a018280808000202003018780808000e0d6a3bde9a405018780808000d0fdd3bde9a405018780808000c0f2c0bde9"
    "a405019680808000ffffffffffffffffff01ffffffffffffffffff0100000305053f0016022eff27053f05"
    , true, false},
};

std::vector<char> captured_runtime_bytes(const char *hex)
{
  std::vector<char> bytes;
  auto digit = [](char c) { return c <= '9' ? c - '0' : c - 'a' + 10; };
  for (size_t i = 0; hex[i] != '\0'; i += 2) {
    bytes.push_back(static_cast<char>((digit(hex[i]) << 4) | digit(hex[i + 1])));
  }
  return bytes;
}

// This only locates fields in complete, independently captured records. It is
// not another RF codec or the malformed-input validator under test.
int64_t captured_integer(const std::vector<char> &bytes, int64_t &pos)
{
  int64_t value = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::decode_vi64(bytes.data(), bytes.size(), pos, &value));
  return value;
}
void skip_captured_record(const std::vector<char> &bytes, int64_t &pos)
{
  EXPECT_EQ(1, captured_integer(bytes, pos));
  const int64_t length = captured_integer(bytes, pos);
  EXPECT_GE(length, 0); EXPECT_LE(pos + length, static_cast<int64_t>(bytes.size()));
  pos += length;
}
void skip_captured_records(const std::vector<char> &bytes, int64_t &pos)
{
  const int64_t count = captured_integer(bytes, pos);
  for (int64_t i = 0; i < count; ++i) { skip_captured_record(bytes, pos); }
}

void relocate_runtime_v1_functions(std::vector<char> &bytes, bool range, const ObJoinFilterSpec &spec)
{
  int64_t pos = 0;
  ASSERT_EQ(1, captured_integer(bytes, pos));
  const int64_t length_pos = pos;
  const int64_t old_body_size = captured_integer(bytes, pos);
  const int64_t body_pos = pos;
  ASSERT_EQ(static_cast<int64_t>(bytes.size()), body_pos + old_body_size);
  skip_captured_record(bytes, pos); // Actual nested P2P base.
  if (range) {
    skip_captured_records(bytes, pos); // lower
    skip_captured_records(bytes, pos); // upper
    const int64_t flags = captured_integer(bytes, pos); pos += flags;
    skip_captured_records(bytes, pos); // capacities
  } else {
    (void)captured_integer(bytes, pos); // row count
  }
  const int64_t function_begin = pos;
  skip_captured_records(bytes, pos); // selected comparison wrappers
  if (!range) { skip_captured_records(bytes, pos); }
  const int64_t function_end = pos;
  std::vector<char> functions(spec.cmp_funcs_.get_serialize_size()
      + (range ? 0 : spec.hash_funcs_.get_serialize_size()));
  int64_t written = 0;
  ASSERT_EQ(OB_SUCCESS, spec.cmp_funcs_.serialize(functions.data(), functions.size(), written));
  if (!range) {
    ASSERT_EQ(OB_SUCCESS, spec.hash_funcs_.serialize(functions.data(), functions.size(), written));
  }
  ASSERT_EQ(static_cast<int64_t>(functions.size()), written);
  std::vector<char> relocated(bytes.begin(), bytes.begin() + function_begin);
  relocated.insert(relocated.end(), functions.begin(), functions.end());
  relocated.insert(relocated.end(), bytes.begin() + function_end, bytes.end());
  written = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(relocated.data() + length_pos,
      body_pos - length_pos, written, relocated.size() - body_pos));
  bytes.swap(relocated);
}

TEST_F(NativeRuntimeFilter, captured_v1_values_and_optional_tail_have_explicit_compatibility_boundaries)
{
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType, ObDecimalIntType});
  const int64_t low[2] = {-7, -1}, high[2] = {19, 0};
  for (const auto &capture : RUNTIME_V1_CAPTURES) {
    SCOPED_TRACE(capture.name);
    auto wire = captured_runtime_bytes(capture.hex);
    ASSERT_EQ(1, wire[0]);
    relocate_runtime_v1_functions(wire, capture.range, input.spec_);
    const bool empty = std::string(capture.name).find("empty") != std::string::npos;
    const bool inactive = std::string(capture.name).find("inactive") != std::string::npos;
    const bool nulls = std::string(capture.name).find("null") != std::string::npos;
    int64_t pos = 0;
    if (capture.range) {
      RuntimeMessage<ObRFRangeFilterMsg> received;
      ASSERT_EQ(OB_SUCCESS, received.deserialize(wire.data(), wire.size(), pos));
      EXPECT_EQ(static_cast<int64_t>(wire.size()), pos); EXPECT_FALSE(received.check_ready());
      if (capture.omission) {
        EXPECT_FALSE(received.is_active()); EXPECT_EQ(0, received.lower_bounds_.count());
        Datum result; ASSERT_EQ(OB_SUCCESS, input.probe(received, result)); EXPECT_EQ(1, result.get_int());
        received.after_process(); EXPECT_FALSE(received.is_query_range_ready_);
        RuntimeMessage<ObRFRangeFilterMsg> merged;
        input.init(merged, ObP2PDatahubMsgBase::RANGE_FILTER_MSG);
        ASSERT_EQ(OB_SUCCESS, merged.merge(received)); EXPECT_FALSE(merged.is_active());
        ObArenaAllocator destination;
        ObPushdownExprSpec push_spec(destination); ObPushdownOperator push_op(input.eval_, push_spec);
        ObPushdownDynamicFilterNode node(destination); node.set_col_idx(0);
        ObDynamicFilterExecutor executor(destination, node, push_op);
        ObRuntimeFilterParams params; bool prepared = false;
        ASSERT_EQ(OB_SUCCESS, received.prepare_storage_white_filter_data(executor, input.eval_, params, prepared));
        EXPECT_TRUE(prepared); EXPECT_TRUE(executor.is_pass_all_data()); EXPECT_EQ(0, params.count());
        wire[0] = 2; RuntimeMessage<ObRFRangeFilterMsg> rejected; pos = 0;
        EXPECT_EQ(OB_DESERIALIZE_ERROR, rejected.deserialize(wire.data(), wire.size(), pos)); EXPECT_EQ(0, pos);
      } else {
        EXPECT_TRUE(received.is_active()); EXPECT_EQ(empty, received.is_empty());
        ASSERT_EQ(3, received.build_obj_metas_.count());
        EXPECT_EQ(ObIntType, received.build_obj_metas_.at(0).get_type());
        EXPECT_EQ(ObDecimalIntType, received.build_obj_metas_.at(2).get_type());
        EXPECT_EQ(5, received.build_obj_metas_.at(2).get_scale());
        if (empty) {
          ASSERT_EQ(3, received.lower_bounds_.count()); ASSERT_EQ(3, received.upper_bounds_.count());
          for (int64_t column = 0; column < 3; ++column) {
            EXPECT_TRUE(received.lower_bounds_.at(column).is_null());
            EXPECT_TRUE(received.upper_bounds_.at(column).is_null());
            EXPECT_EQ(0U, received.lower_bounds_.at(column).length());
            EXPECT_EQ(0U, received.upper_bounds_.at(column).length());
          }
          // Unused legacy placeholders are accepted only at the old port.
          // Relabeling those exact records v2 must still reject their invalid
          // present scalar spans rather than weakening native validation.
          auto not_legacy = wire; not_legacy[0] = 2;
          RuntimeMessage<ObRFRangeFilterMsg> rejected; int64_t rejected_pos = 0;
          EXPECT_EQ(OB_INVALID_ARGUMENT, rejected.deserialize(not_legacy.data(), not_legacy.size(), rejected_pos));
          EXPECT_EQ(0, rejected_pos); EXPECT_EQ(0, rejected.lower_bounds_.count());
          EXPECT_FALSE(rejected.check_ready());
          const auto canonical = runtime_wire(received);
          ASSERT_EQ(2, canonical[0]);
          RuntimeMessage<ObRFRangeFilterMsg> roundtrip; int64_t canonical_pos = 0;
          ASSERT_EQ(OB_SUCCESS, roundtrip.deserialize(canonical.data(), canonical.size(), canonical_pos));
          EXPECT_EQ(static_cast<int64_t>(canonical.size()), canonical_pos);
          EXPECT_TRUE(roundtrip.is_active()); EXPECT_TRUE(roundtrip.is_empty());
          EXPECT_EQ(3, roundtrip.build_obj_metas_.count());
          EXPECT_EQ(canonical, runtime_wire(roundtrip));
        }
        MEMSET(wire.data(), 0xa5, wire.size());
        if (!empty) {
          if (nulls) {
            // Old retained 8/16-byte stale spans are consumed, then discarded.
            EXPECT_TRUE(received.lower_bounds_.at(0).is_null()); EXPECT_EQ(0U, received.lower_bounds_.at(0).length());
            EXPECT_TRUE(received.lower_bounds_.at(2).is_null()); EXPECT_EQ(0U, received.lower_bounds_.at(2).length());
            EXPECT_EQ("", native_text(received.lower_bounds_.at(1)));
          } else {
            EXPECT_EQ(INT64_MIN, received.lower_bounds_.at(0).get_int());
            EXPECT_EQ("alpha", native_text(received.lower_bounds_.at(1)));
            EXPECT_EQ(16U, received.lower_bounds_.at(2).length());
            EXPECT_EQ(0, MEMCMP(received.lower_bounds_.at(2).get_reference(), low, 16));
          }
          EXPECT_EQ(INT64_MAX, received.upper_bounds_.at(0).get_int());
          EXPECT_EQ("zeta", native_text(received.upper_bounds_.at(1)));
          EXPECT_EQ(0, MEMCMP(received.upper_bounds_.at(2).get_reference(), high, 16));
        }
        EXPECT_EQ(2, runtime_wire(received)[0]);
      }
    } else {
      RuntimeMessage<ObRFInFilterMsg> received;
      ASSERT_EQ(OB_SUCCESS, received.deserialize(wire.data(), wire.size(), pos));
      EXPECT_EQ(static_cast<int64_t>(wire.size()), pos); EXPECT_FALSE(received.check_ready());
      if (capture.omission) {
        EXPECT_FALSE(received.is_active()); EXPECT_EQ(0, received.serial_rows_.count());
        Datum result; ASSERT_EQ(OB_SUCCESS, input.probe(received, result)); EXPECT_EQ(1, result.get_int());
        received.after_process(); EXPECT_FALSE(received.is_query_range_ready_);
        RuntimeMessage<ObRFInFilterMsg> merged;
        input.init(merged, ObP2PDatahubMsgBase::IN_FILTER_MSG);
        ASSERT_EQ(OB_SUCCESS, merged.merge(received)); EXPECT_FALSE(merged.is_active());
        wire[0] = 2; RuntimeMessage<ObRFInFilterMsg> rejected; pos = 0;
        EXPECT_EQ(OB_DESERIALIZE_ERROR, rejected.deserialize(wire.data(), wire.size(), pos)); EXPECT_EQ(0, pos);
      } else {
        EXPECT_EQ(!inactive, received.is_active()); EXPECT_EQ(empty, received.is_empty());
        ASSERT_EQ(3, received.build_obj_metas_.count());
        EXPECT_EQ(ObVarcharType, received.build_obj_metas_.at(1).get_type());
        EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, received.build_obj_metas_.at(1).get_cs_type());
        EXPECT_EQ(empty || inactive ? 0 : nulls ? 3 : 2, received.serial_rows_.count());
        MEMSET(wire.data(), 0xa5, wire.size());
        if (!empty && !inactive) {
          EXPECT_EQ(INT64_MIN, received.serial_rows_.at(0)->at(0).get_int());
          EXPECT_EQ("alpha", native_text(received.serial_rows_.at(0)->at(1)));
          EXPECT_EQ(0, MEMCMP(received.serial_rows_.at(0)->at(2).get_reference(), low, 16));
          EXPECT_EQ(INT64_MAX, received.serial_rows_.at(1)->at(0).get_int());
          EXPECT_EQ("zeta", native_text(received.serial_rows_.at(1)->at(1)));
          EXPECT_EQ(0, MEMCMP(received.serial_rows_.at(1)->at(2).get_reference(), high, 16));
          if (nulls) { EXPECT_TRUE(received.serial_rows_.at(2)->at(2).is_null()); }
          input.cell(0).set_int(INT64_MIN); input.cell(1).set_string("alpha", 5); input.decimal(2, -7, 4);
          Datum result; ASSERT_EQ(OB_SUCCESS, input.probe(received, result)); EXPECT_EQ(1, result.get_int());
        }
        EXPECT_EQ(2, runtime_wire(received)[0]);
      }
    }
  }
}

// Replace one known nested query-info record in an independently captured old
// message. Its cells/metadata/functions remain the real v1 serializer's bytes.
void replace_captured_runtime_query_info(std::vector<char> &bytes, bool range,
                                        const ObPxQueryRangeInfo &query)
{
  int64_t pos = 0;
  ASSERT_EQ(1, captured_integer(bytes, pos));
  const int64_t length_pos = pos;
  const int64_t old_body_size = captured_integer(bytes, pos);
  const int64_t body_pos = pos;
  ASSERT_EQ(static_cast<int64_t>(bytes.size()), body_pos + old_body_size);
  skip_captured_record(bytes, pos); // P2P base.
  if (range) {
    skip_captured_records(bytes, pos); // lower
    skip_captured_records(bytes, pos); // upper
    const int64_t flags = captured_integer(bytes, pos); pos += flags;
    skip_captured_records(bytes, pos); // capacities
    skip_captured_records(bytes, pos); // comparisons
  } else {
    const int64_t rows = captured_integer(bytes, pos);
    skip_captured_records(bytes, pos); // comparisons
    skip_captured_records(bytes, pos); // hashes
    (void)captured_integer(bytes, pos); // columns
    (void)captured_integer(bytes, pos); // maximum rows
    const int64_t flags = captured_integer(bytes, pos); pos += flags;
    for (int64_t i = 0; i < rows; ++i) { skip_captured_records(bytes, pos); }
  }
  const int64_t query_begin = pos;
  skip_captured_record(bytes, pos);
  const int64_t query_end = pos;
  std::vector<char> encoded(query.get_serialize_size());
  int64_t written = 0;
  ASSERT_EQ(OB_SUCCESS, query.serialize(encoded.data(), encoded.size(), written));
  ASSERT_EQ(static_cast<int64_t>(encoded.size()), written);
  std::vector<char> result(bytes.begin(), bytes.begin() + query_begin);
  result.insert(result.end(), encoded.begin(), encoded.end());
  result.insert(result.end(), bytes.begin() + query_end, bytes.end());
  written = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(result.data() + length_pos,
      body_pos - length_pos, written, result.size() - body_pos));
  bytes.swap(result);
}

TEST_F(NativeRuntimeFilter, captured_v1_query_range_cardinality_is_bounded_before_publication)
{
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType, ObDecimalIntType});
  ObSEArray<int64_t, 1> prefix;
  ObSEArray<SqlTypeMeta, 1> metas;
  ASSERT_EQ(OB_SUCCESS, prefix.push_back(0));
  ASSERT_EQ(OB_SUCCESS, metas.push_back(input.spec_.join_keys_.at(0)->obj_meta_));
  const int64_t overflow_columns = (INT64_C(1) << 61) + 1;
  const int64_t counts[] = {-2, 0, OB_MAX_ROWKEY_COLUMN_NUMBER,
                            OB_MAX_ROWKEY_COLUMN_NUMBER + 1, overflow_columns};
  for (const auto &capture : RUNTIME_V1_CAPTURES) {
    if (std::string(capture.name) != "range-values-v1.bin"
        && std::string(capture.name) != "in-values-v1.bin") { continue; }
    for (int64_t count : counts) {
      SCOPED_TRACE(capture.name);
      SCOPED_TRACE(count);
      const bool valid = count == OB_MAX_ROWKEY_COLUMN_NUMBER;
      ObPxQueryRangeInfo query(input.owner_);
      ASSERT_EQ(OB_SUCCESS, query.init(991, count, prefix, metas));
      auto wire = captured_runtime_bytes(capture.hex);
      relocate_runtime_v1_functions(wire, capture.range, input.spec_);
      replace_captured_runtime_query_info(wire, capture.range, query);
      ASSERT_EQ(1, wire[0]);
      int64_t pos = 0;
      if (capture.range) {
        RuntimeMessage<ObRFRangeFilterMsg> message;
        EXPECT_EQ(valid ? OB_SUCCESS : OB_DESERIALIZE_ERROR,
            message.deserialize(wire.data(), wire.size(), pos));
        EXPECT_FALSE(message.check_ready());
        if (valid) {
          EXPECT_EQ(static_cast<int64_t>(wire.size()), pos);
          EXPECT_EQ(count, message.query_range_info_.range_column_cnt_);
          ASSERT_EQ(3, message.lower_bounds_.count());
          EXPECT_EQ(INT64_MIN, message.lower_bounds_.at(0).get_int());
        } else {
          EXPECT_EQ(0, pos); EXPECT_EQ(0, message.lower_bounds_.count());
        }
      } else {
        RuntimeMessage<ObRFInFilterMsg> message;
        EXPECT_EQ(valid ? OB_SUCCESS : OB_DESERIALIZE_ERROR,
            message.deserialize(wire.data(), wire.size(), pos));
        EXPECT_FALSE(message.check_ready());
        if (valid) {
          EXPECT_EQ(static_cast<int64_t>(wire.size()), pos);
          EXPECT_EQ(count, message.query_range_info_.range_column_cnt_);
          ASSERT_EQ(2, message.serial_rows_.count());
          EXPECT_EQ(INT64_MIN, message.serial_rows_.at(0)->at(0).get_int());
        } else {
          EXPECT_EQ(0, pos); EXPECT_EQ(0, message.serial_rows_.count());
        }
      }
      // The public local port has the same count domain and does not replace
      // the default -1 query-info value when input validation fails.
      RuntimeMessage<ObRFRangeFilterMsg> local_range;
      RuntimeMessage<ObRFInFilterMsg> local_in;
      EXPECT_EQ(valid ? OB_SUCCESS : OB_INVALID_ARGUMENT, local_range.init_query_range_info(query));
      EXPECT_EQ(valid ? OB_SUCCESS : OB_INVALID_ARGUMENT, local_in.init_query_range_info(query));
      EXPECT_EQ(valid ? count : -1, local_range.query_range_info_.range_column_cnt_);
      EXPECT_EQ(valid ? count : -1, local_in.query_range_info_.range_column_cnt_);
    }
  }
  // The legitimate non-extracting default must remain usable by both senders.
  ObPxQueryRangeInfo empty(input.owner_);
  RuntimeMessage<ObRFRangeFilterMsg> range;
  RuntimeMessage<ObRFInFilterMsg> in;
  input.init(range, ObP2PDatahubMsgBase::RANGE_FILTER_MSG);
  input.init(in, ObP2PDatahubMsgBase::IN_FILTER_MSG);
  ASSERT_EQ(OB_SUCCESS, range.init_query_range_info(empty));
  ASSERT_EQ(OB_SUCCESS, in.init_query_range_info(empty));
  EXPECT_EQ(2, runtime_wire(range)[0]); EXPECT_EQ(2, runtime_wire(in)[0]);
  // Even callers assigning the old public query-info field directly cannot
  // serialize an invalid width. No invalid after_process is invoked here.
  ObPxQueryRangeInfo invalid(input.owner_);
  ASSERT_EQ(OB_SUCCESS, invalid.init(991, overflow_columns, prefix, metas));
  ASSERT_EQ(OB_SUCCESS, range.query_range_info_.assign(invalid));
  ASSERT_EQ(OB_SUCCESS, in.query_range_info_.assign(invalid));
  std::vector<char> range_bytes(range.get_serialize_size(), 0), in_bytes(in.get_serialize_size(), 0);
  int64_t range_pos = 0, in_pos = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT, range.serialize(range_bytes.data(), range_bytes.size(), range_pos));
  EXPECT_EQ(OB_INVALID_ARGUMENT, in.serialize(in_bytes.data(), in_bytes.size(), in_pos));
}

// These cells are produced from real prepared Exprs, not guessed from span
// length. Decoder/storage-only cells below use the separate opaque port.
TEST_F(NativeRuntimeFilter, px_range_expr_capture_is_inline_and_reference_clone_is_owned)
{
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType, ObDecimalIntType});
  input.cell(0).set_int(INT64_MIN);
  std::string text_source("a\0b", 3);
  ASSERT_EQ(OB_SUCCESS, input.cell(1).set_string(ObString(text_source.size(), text_source.data())));
  input.decimal(2, 12345, 16);
  input.ready();
  RuntimeFailAllocator owner;
  ObPxRangeCell integer_cell, text_cell, decimal_cell;
  Datum *evaluated = nullptr;
  ASSERT_EQ(OB_SUCCESS, input.spec_.join_keys_.at(0)->eval(input.eval_, evaluated));
  ASSERT_EQ(OB_SUCCESS, integer_cell.assign_expr_owned(*evaluated,
      input.spec_.join_keys_.at(0)->get_datum_copy_plan(), owner));
  EXPECT_FALSE(integer_cell.is_opaque()); EXPECT_EQ(INT64_MIN, integer_cell.get_datum().get_int());
  EXPECT_EQ(0U, integer_cell.get_datum().length()); EXPECT_EQ(0, owner.calls_);
  ObPxRangeCell null_cell; Datum null_input;
  ASSERT_EQ(OB_SUCCESS, null_cell.assign_expr_owned(null_input,
      input.spec_.join_keys_.at(0)->get_datum_copy_plan(), owner));
  EXPECT_TRUE(null_cell.is_null()); EXPECT_FALSE(null_cell.is_opaque()); EXPECT_EQ(0, owner.calls_);
  ASSERT_EQ(OB_SUCCESS, input.spec_.join_keys_.at(1)->eval(input.eval_, evaluated));
  ASSERT_EQ(OB_SUCCESS, text_cell.assign_expr_owned(*evaluated,
      input.spec_.join_keys_.at(1)->get_datum_copy_plan(), owner));
  EXPECT_NE(text_source.data(), text_cell.get_datum().get_reference());
  ASSERT_EQ(OB_SUCCESS, input.spec_.join_keys_.at(2)->eval(input.eval_, evaluated));
  ASSERT_EQ(OB_SUCCESS, decimal_cell.assign_expr_owned(*evaluated,
      input.spec_.join_keys_.at(2)->get_datum_copy_plan(), owner));
  EXPECT_EQ(16U, decimal_cell.get_datum().length());
  text_source.assign(text_source.size(), 'x'); input.cell(0).set_int(0);
  MEMSET(input.coefficients_, 0xA5, sizeof(input.coefficients_));
  EXPECT_EQ(std::string("a\0b", 3), native_text(text_cell.get_datum()));
  EXPECT_EQ(INT64_MIN, integer_cell.get_datum().get_int());
  alignas(8) char scratch[64]; ObDatum view;
  ASSERT_EQ(OB_SUCCESS, decimal_cell.export_stored_key(view, scratch, sizeof(scratch)));
  int64_t coefficient = 0; MEMCPY(&coefficient, view.ptr_, sizeof(coefficient));
  EXPECT_EQ(12345, coefficient); EXPECT_EQ(16U, view.len_);
  const void *old_pointer = text_cell.get_datum().get_reference();
  std::string replacement("new");
  ASSERT_EQ(OB_SUCCESS, input.cell(1).set_string(ObString(replacement.size(), replacement.data())));
  owner.fail_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, text_cell.assign_expr_owned(input.cell(1),
      input.spec_.join_keys_.at(1)->get_datum_copy_plan(), owner));
  EXPECT_EQ(old_pointer, text_cell.get_datum().get_reference());
  EXPECT_EQ(std::string("a\0b", 3), native_text(text_cell.get_datum()));
  ObDatum malformed(nullptr, 7, false);
  EXPECT_EQ(OB_INVALID_ARGUMENT, text_cell.assign_stored_key_borrowed(malformed));
  EXPECT_EQ(old_pointer, text_cell.get_datum().get_reference());
}

TEST_F(NativeRuntimeFilter, px_range_old_cell_wire_and_opaque_clone_keep_state_and_bytes)
{
  ObArenaAllocator owner, clone_owner;
  int64_t signed_value = INT64_MIN;
  std::string source("x\0z", 3), empty;
  blocksstable::ObStorageDatum minimum, maximum, nop;
  minimum.set_min(); maximum.set_max(); nop.set_nop();
  ObDatum with_header = text(source); with_header.set_has_lob_header();
  const ObDatum samples[] = {integer(signed_value), with_header, text(empty), null_value(), minimum, maximum, nop};
  for (const ObDatum &old : samples) {
    ObPxRangeCell cell;
    ASSERT_EQ(OB_SUCCESS, cell.assign_stored_key_owned(old, owner));
    EXPECT_TRUE(cell.is_opaque()); EXPECT_EQ(old.is_null(), cell.is_null());
    std::vector<char> original(old.get_serialize_size()); int64_t old_pos = 0;
    ASSERT_EQ(OB_SUCCESS, old.serialize(original.data(), original.size(), old_pos));
    EXPECT_EQ(original, runtime_wire(cell));
    ObPxRangeCell decoded, owned;
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, decoded.deserialize(original.data(), original.size(), pos));
    ASSERT_EQ(static_cast<int64_t>(original.size()), pos);
    ASSERT_EQ(OB_SUCCESS, owned.deep_copy(decoded, clone_owner));
    MEMSET(original.data(), 0xA5, original.size());
    EXPECT_EQ(runtime_wire(cell), runtime_wire(owned));
    alignas(8) char scratch[64]; ObDatum view;
    ASSERT_EQ(OB_SUCCESS, owned.export_stored_key(view, scratch, sizeof(scratch)));
    EXPECT_EQ(old.get_wire_pack(), view.pack_);
    if (old.is_ext()) {
      EXPECT_EQ(old.get_ext(), view.get_ext());
      EXPECT_EQ(old.get_extend_type(), view.get_extend_type());
      EXPECT_EQ(old.get_extend_size(), view.get_extend_size());
      EXPECT_EQ(old.get_extend_collation_level(), view.get_extend_collation_level());
      EXPECT_EQ(old.get_extend_collation(), view.get_extend_collation());
    } else if (!old.is_null()) {
      EXPECT_EQ(std::string(old.ptr_, old.len_), std::string(view.ptr_, view.len_));
    }
  }
  ObPxTabletRange range; range.tablet_id_ = 17;
  ObPxTabletRange::DatumKey key;
  ObPxRangeCell raw;
  ASSERT_EQ(OB_SUCCESS, raw.assign_stored_key_borrowed(integer(signed_value)));
  ASSERT_EQ(OB_SUCCESS, key.push_back(raw)); ASSERT_EQ(OB_SUCCESS, range.range_cut_.push_back(key));
  ObPxTabletRange cloned;
  int64_t clone_pos = 0;
  ASSERT_EQ(OB_SUCCESS, cloned.deep_copy_from<true>(range, clone_owner, nullptr, 0, clone_pos));
  signed_value = 41;
  alignas(8) char scratch[64]; ObDatum view;
  ASSERT_EQ(OB_SUCCESS, cloned.range_cut_.at(0).at(0).export_stored_key(view, scratch, sizeof(scratch)));
  EXPECT_EQ(INT64_MIN, view.get_int());
  RuntimeInputs declared; declared.prepare({ObIntType, ObUInt64Type});
  for (const ObDatum *marker : {static_cast<const ObDatum *>(&minimum),
                               static_cast<const ObDatum *>(&maximum),
                               static_cast<const ObDatum *>(&nop)}) {
    ObPxRangeCell cell;
    ASSERT_EQ(OB_SUCCESS, cell.assign_stored_key_borrowed(*marker));
    ASSERT_EQ(OB_SUCCESS, cell.bind_for_execution(
        declared.spec_.join_keys_.at(0)->get_datum_copy_plan(), clone_owner));
    EXPECT_FALSE(cell.is_opaque());
    EXPECT_EQ(marker->is_min(), cell.get_datum().is_min());
    EXPECT_EQ(marker->is_max(), cell.get_datum().is_max());
    EXPECT_EQ(marker->is_nop(), cell.get_datum().is_nop());
  }
  declared.cell(1).set_uint(UINT64_MAX); declared.ready();
  ObPxRangeCell unsigned_cell;
  ASSERT_EQ(OB_SUCCESS, unsigned_cell.assign_expr_owned(declared.cell(1),
      declared.spec_.join_keys_.at(1)->get_datum_copy_plan(), clone_owner));
  EXPECT_EQ(UINT64_MAX, unsigned_cell.get_datum().get_uint());
  EXPECT_EQ(0U, unsigned_cell.get_datum().length());
  const auto wire = runtime_wire(cloned);
  ObPxTabletRange decoded; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), wire.size(), pos));
  EXPECT_EQ(wire, runtime_wire(decoded));
}

TEST_F(NativeRuntimeFilter, px_range_actual_slice_scalar_batch_binds_old_keys_once)
{
  RuntimeInputs input; input.prepare({ObIntType});
  const ObDatumAccessContext *access = nullptr;
  ASSERT_EQ(OB_SUCCESS, input.eval_.get_datum_access_ctx(access));
  ASSERT_EQ(nullptr, access); // valid in-row execution has no LOB service
  ObSortFuncs comparisons(input.owner_); ObSortCollations collations(input.owner_);
  ASSERT_EQ(OB_SUCCESS, comparisons.init(1)); ASSERT_EQ(OB_SUCCESS, collations.init(1));
  ObSortCmpFunc compare;
  compare.cmp_func_ = input.spec_.join_keys_.at(0)->basic_funcs_->null_first_cmp_.legacy_function();
  ASSERT_EQ(OB_SUCCESS, comparisons.push_back(compare));
  ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(0, CS_TYPE_BINARY, true, NULL_FIRST)));
  int64_t lower = 10, upper = 20;
  ObPxTabletRange range; range.tablet_id_ = 17;
  for (const int64_t *value : {&lower, &upper}) {
    ObPxRangeCell cell;
    ASSERT_EQ(OB_SUCCESS, cell.assign_stored_key_borrowed(integer(*value)));
    ObPxTabletRange::DatumKey key; ASSERT_EQ(OB_SUCCESS, key.push_back(cell));
    ASSERT_EQ(OB_SUCCESS, range.range_cut_.push_back(key));
  }
  ObRangeSliceIdCalc calc(input.owner_, 3, &range, &input.spec_.join_keys_, comparisons,
                         collations, nullptr);
  ObSliceIdxCalc::SliceIdxArray result;
  input.cell(0).set_int(10); input.ready();
  ASSERT_EQ(OB_SUCCESS, calc.get_slice_indexes_inner(input.spec_.join_keys_, input.eval_, result));
  ASSERT_EQ(1, result.count()); EXPECT_EQ(0, result.at(0));
  ASSERT_EQ(2, calc.bound_ranges_.count());
  EXPECT_FALSE(calc.bound_ranges_.at(0).at(0).is_opaque());
  EXPECT_EQ(0U, calc.bound_ranges_.at(0).at(0).get_datum().length());
  EXPECT_EQ(10, calc.bound_ranges_.at(0).at(0).get_datum().get_int());
  lower = -100; upper = -50; range.range_cut_.reset();
  const int64_t values[] = {9, 10, 11, 21};
  for (int64_t i = 0; i < RuntimeInputs::BATCH; ++i) { input.cell(0, i).set_int(values[i]); }
  input.ready();
  int64_t *indexes = nullptr;
  ASSERT_EQ(OB_SUCCESS, calc.get_slice_idx_batch_inner(input.spec_.join_keys_, input.eval_, input.skip(),
      RuntimeInputs::BATCH, indexes));
  ASSERT_NE(nullptr, indexes);
  EXPECT_EQ(0, indexes[0]); EXPECT_EQ(0, indexes[1]); EXPECT_EQ(1, indexes[2]); EXPECT_EQ(2, indexes[3]);
  input.cell(0).set_null(); input.ready();
  ASSERT_EQ(OB_SUCCESS, calc.get_slice_indexes_inner(input.spec_.join_keys_, input.eval_, result));
  EXPECT_EQ(0, result.at(0));
}

TEST_F(NativeRuntimeFilter, px_range_checked_binding_failure_retries_and_keeps_reference_owner)
{
  RuntimeInputs input; input.prepare({ObVarcharType});
  const ObDatumAccessContext *access = nullptr;
  ASSERT_EQ(OB_SUCCESS, input.eval_.get_datum_access_ctx(access));
  ASSERT_EQ(nullptr, access); // valid in-row execution has no LOB service
  ObSortFuncs comparisons(input.owner_); ObSortCollations collations(input.owner_);
  ASSERT_EQ(OB_SUCCESS, comparisons.init(1)); ASSERT_EQ(OB_SUCCESS, collations.init(1));
  ObSortCmpFunc compare;
  compare.cmp_func_ = input.spec_.join_keys_.at(0)->basic_funcs_->null_first_cmp_.legacy_function();
  ASSERT_EQ(OB_SUCCESS, comparisons.push_back(compare));
  ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(0, CS_TYPE_UTF8MB4_BIN, false, NULL_FIRST)));
  std::string high("z"), low("m"), probe("n");
  ObPxTabletRange range; range.tablet_id_ = 17;
  for (const std::string *value : {&high, &low}) {
    ObPxRangeCell cell; ASSERT_EQ(OB_SUCCESS, cell.assign_stored_key_borrowed(text(*value)));
    ObPxTabletRange::DatumKey key; ASSERT_EQ(OB_SUCCESS, key.push_back(cell));
    ASSERT_EQ(OB_SUCCESS, range.range_cut_.push_back(key));
  }
  RuntimeFailAllocator owner; owner.fail_ = true;
  ObRangeSliceIdCalc calc(owner, 3, &range, &input.spec_.join_keys_, comparisons, collations, nullptr);
  ASSERT_EQ(OB_SUCCESS, input.cell(0).set_string(ObString(probe.size(), probe.data()))); input.ready();
  ObSliceIdxCalc::SliceIdxArray result;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      calc.get_slice_indexes_inner(input.spec_.join_keys_, input.eval_, result));
  EXPECT_FALSE(calc.ranges_bound_); EXPECT_GE(owner.calls_, 1);
  owner.fail_ = false;
  ASSERT_EQ(OB_SUCCESS, calc.get_slice_indexes_inner(input.spec_.join_keys_, input.eval_, result));
  EXPECT_TRUE(calc.ranges_bound_); EXPECT_EQ(1, result.at(0));
  const void *owned = calc.bound_ranges_.at(0).at(0).get_datum().get_reference();
  EXPECT_NE(high.data(), owned);
  high.assign(high.size(), 'a'); low.assign(low.size(), 'a'); range.range_cut_.reset();
  ASSERT_EQ(OB_SUCCESS, calc.get_slice_indexes_inner(input.spec_.join_keys_, input.eval_, result));
  EXPECT_EQ(1, result.at(0)); EXPECT_EQ(owned, calc.bound_ranges_.at(0).at(0).get_datum().get_reference());

  RuntimeInputs integer_input; integer_input.prepare({ObIntType});
  char short_value[7] = {};
  ObDatum bad(short_value, sizeof(short_value), false);
  ObPxRangeCell bad_cell; ASSERT_EQ(OB_SUCCESS, bad_cell.assign_stored_key_borrowed(bad));
  EXPECT_EQ(OB_INVALID_ARGUMENT, bad_cell.bind_for_execution(
      integer_input.spec_.join_keys_.at(0)->get_datum_copy_plan(), integer_input.owner_));
  EXPECT_TRUE(bad_cell.is_opaque());
  alignas(8) char scratch[64]; ObDatum unchanged;
  ASSERT_EQ(OB_SUCCESS, bad_cell.export_stored_key(unchanged, scratch, sizeof(scratch)));
  EXPECT_EQ(short_value, unchanged.ptr_); EXPECT_EQ(7U, unchanged.len_);
}

TEST_F(NativeRuntimeFilter, px_range_actual_dynamic_sample_producer_captures_native_cut_values)
{
  ObSQLSessionInfo session; ASSERT_EQ(OB_SUCCESS, session.test_init(1, 1, nullptr));
  RuntimeInputs input; input.prepare({ObIntType, ObVarcharType});
  input.exec_.set_my_session(&session);
  input.exec_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
  ObSortFuncs comparisons(input.owner_); ObSortCollations collations(input.owner_);
  ASSERT_EQ(OB_SUCCESS, comparisons.init(2)); ASSERT_EQ(OB_SUCCESS, collations.init(2));
  for (int64_t col = 0; col < 2; ++col) {
    ObSortCmpFunc compare;
    compare.cmp_func_ = input.spec_.join_keys_.at(col)->basic_funcs_->null_first_cmp_.legacy_function();
    ASSERT_EQ(OB_SUCCESS, comparisons.push_back(compare));
    ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(col,
        col == 0 ? CS_TYPE_BINARY : CS_TYPE_UTF8MB4_BIN, true, NULL_FIRST)));
  }
  ObPxCoordSpec spec(input.owner_, PHY_PX_FIFO_COORD);
  SampleCoordinator coordinator(input.exec_, spec, nullptr);
  coordinator.get_eval_ctx().frames_ = input.eval_.frames_;
  coordinator.get_eval_ctx().max_batch_size_ = input.eval_.max_batch_size_;
  ASSERT_EQ(input.eval_.frames_, coordinator.get_eval_ctx().frames_);

  ObDynamicSamplePieceMsgCtx::SortDef sort;
  sort.exprs_ = &input.spec_.join_keys_; sort.collations_ = &collations; sort.cmp_funs_ = &comparisons;
  ObDynamicSamplePieceMsgCtx context(1, 1, INT64_MAX, input.exec_, coordinator, sort);
  struct Cleanup {
    explicit Cleanup(ObDynamicSamplePieceMsgCtx &ctx) : ctx_(ctx) {}
    ~Cleanup() { ctx_.last_store_row_.reset(); ctx_.destroy(); }
    ObDynamicSamplePieceMsgCtx &ctx_;
  } cleanup(context);
  ObSEArray<uint64_t, 1> tablets; ASSERT_EQ(OB_SUCCESS, tablets.push_back(17));
  ASSERT_EQ(OB_SUCCESS, context.init(tablets));
  ASSERT_TRUE(context.is_inited_);
  ASSERT_TRUE(context.sort_impl_.is_inited());
  ObSqlMemMgrProcessor &sort_memory = context.sort_impl_.*member(SampleSortMemory{});
  ASSERT_EQ(0, sort_memory.get_dir_id());
  // before_add_row still runs real work-area initialization. With no file
  // directory selected, only its unused alloc_dir bootstrap is bypassed.
  sort_memory.*member(SampleSortDirectory{}) = -1;
  ASSERT_EQ(-1, sort_memory.get_dir_id());

  ObChunkDatumStore store("NativePXSample"); store.set_allocator(input.owner_);
  ASSERT_EQ(OB_SUCCESS, store.init(INT64_MAX, ObCtxIds::DEFAULT_CTX_ID, "NativePXSample", false));
  for (int64_t row = 5; row > 0; --row) {
    input.cell(0).set_int(row);
    std::string text_value(1, 'a' + row - 1);
    ASSERT_EQ(OB_SUCCESS, input.cell(1).set_string(ObString(text_value.size(), text_value.data())));
    input.ready();
    ASSERT_EQ(OB_SUCCESS, store.add_row(input.spec_.join_keys_, &input.eval_));
  }
  ObPxTabletRange::RangeCut cuts;
  ASSERT_EQ(OB_SUCCESS, context.split_range(&store, 3, cuts));
  const ObChunkDatumStore &sort_store = context.sort_impl_.*member(SampleSortStore{});
  EXPECT_FALSE(sort_store.is_file_open());
  EXPECT_EQ(0, sort_store.get_file_size());
  EXPECT_EQ(0, sort_memory.get_dumped_size());

  ASSERT_EQ(2, cuts.count());
  for (int64_t row = 0; row < cuts.count(); ++row) {
    ASSERT_EQ(2, cuts.at(row).count());
    EXPECT_FALSE(cuts.at(row).at(0).is_opaque());
    EXPECT_EQ(0U, cuts.at(row).at(0).get_datum().length());
    EXPECT_EQ(row + 1, cuts.at(row).at(0).get_datum().get_int());
    EXPECT_EQ(std::string(1, 'a' + row), native_text(cuts.at(row).at(1).get_datum()));
  }
  ObPxTabletRange range; range.tablet_id_ = 17; ASSERT_EQ(OB_SUCCESS, range.range_cut_.assign(cuts));
  ObArenaAllocator retained;
  ObPxTabletRange owned; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, owned.deep_copy_from<true>(range, retained, nullptr, 0, pos));
  const auto expected = runtime_wire(owned);
  for (int64_t row = 0; row < cuts.count(); ++row) {
    const Datum &source = cuts.at(row).at(1).get_datum();
    MEMSET(const_cast<void *>(source.get_reference()), 0xA5, source.length());
  }
  context.destroy(); store.reset();
  EXPECT_EQ(expected, runtime_wire(owned));
}

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
