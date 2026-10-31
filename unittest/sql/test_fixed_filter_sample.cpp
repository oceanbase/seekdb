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

#include "sql/engine/px/p2p_datahub/ob_pushdown_topn_filter_msg.h"
#include "sql/engine/px/datahub/components/ob_dh_sample.h"
#include "sql/engine/px/ob_px_coord_op.h"
#include "sql/optimizer/stat/ob_hybrid_hist_estimator.h"
#include "sql/engine/basic/ob_material_op_impl.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/charset/ob_charset.h"
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
  using Type = ObFixedArray<ObDatum, ObIAllocator> ObPushDownTopNFilterMsg::*;
  friend Type member(TopNValues);
};
template struct PrivateMember<TopNValues, &ObPushDownTopNFilterMsg::heap_top_datums_>;
struct SampleAppend
{
  using Type = int (ObDynamicSamplePieceMsgCtx::*)(const ObDynamicSamplePieceMsg &, int64_t, ObChunkDatumStore *);
  friend Type member(SampleAppend);
};
template struct PrivateMember<SampleAppend, &ObDynamicSamplePieceMsgCtx::append_object_sample_data>;
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
      for (const auto &value : row) { ASSERT_EQ(OB_SUCCESS, key.push_back(value)); }
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
  ASSERT_EQ(2, values.count()); EXPECT_NE(source_pointer, values.at(0).ptr_);
  source.assign(source.size(), 'x'); low = 0; row.poison();
  EXPECT_EQ(std::string("a\0b", 3), bytes(values.at(0))); EXPECT_EQ(expected_pack, values.at(0).pack_);
  EXPECT_EQ(INT64_MIN, values.at(1).get_int());
  int64_t high = INT64_MAX; std::string shorter = "z";
  OwnedRow replacement({text(shorter), integer(high)});
  ASSERT_EQ(OB_SUCCESS, message.update_filter_data(replacement.row_, updated)); EXPECT_TRUE(updated);
  replacement.poison();
  EXPECT_EQ("z", bytes(values.at(0))); EXPECT_EQ(ObDatumDesc::NONE, values.at(0).flag_);
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
  EXPECT_EQ(std::string("a\0b", 3), std::string(buckets.at(0).endpoint_value_.get_string().ptr(), 3));
  EXPECT_EQ("beta", std::string(buckets.at(1).endpoint_value_.get_string().ptr(), 4));
  EXPECT_TRUE(buckets.at(2).endpoint_value_.is_null());
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
  EXPECT_EQ(source, std::string(histogram.get_buckets().at(0).endpoint_value_.get_string().ptr(), source.size()));
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
} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
