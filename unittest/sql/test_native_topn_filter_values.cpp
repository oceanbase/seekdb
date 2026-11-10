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
#include "sql/engine/sort/ob_sort_basic_info.h"
#include "sql/engine/expr/ob_expr_topn_filter.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/basic/ob_pushdown_filter.h"
#include "common/datum/datum_parameter_codec.h"
#include "share/datum/ob_datum_funcs.h"
#include "lib/charset/ob_charset.h"
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
template<class Tag, typename Tag::Type Member> struct TopNPrivateMember
{ friend typename Tag::Type topn_member(Tag) { return Member; } };
#define TOPN_MEMBER(Name, TypeName, Field) \
  struct Name { using Type = TypeName ObPushDownTopNFilterMsg::*; friend Type topn_member(Name); }; \
  template struct TopNPrivateMember<Name, &ObPushDownTopNFilterMsg::Field>
using TopNCells = ObFixedArray<Datum, ObIAllocator>;
using TopNCapacities = ObFixedArray<int64_t, ObIAllocator>;
TOPN_MEMBER(TopNCellsTag, TopNCells, heap_top_datums_);
TOPN_MEMBER(TopNCapacitiesTag, TopNCapacities, cells_size_);
TOPN_MEMBER(TopNComparesTag, ObTopNFilterCompares, compares_);
#undef TOPN_MEMBER

const TopNCells &cells(const ObPushDownTopNFilterMsg &msg) { return msg.*topn_member(TopNCellsTag{}); }
const TopNCapacities &capacities(const ObPushDownTopNFilterMsg &msg) { return msg.*topn_member(TopNCapacitiesTag{}); }
const ObTopNFilterCompares &compares(const ObPushDownTopNFilterMsg &msg) { return msg.*topn_member(TopNComparesTag{}); }
std::string bytes(const Datum &datum) { return std::string(datum.get_string().ptr(), datum.get_string().length()); }
ObDatum signed_value(const int64_t &value) { return ObDatum(reinterpret_cast<const char *>(&value), 8, false); }
ObDatum unsigned_value(const uint64_t &value) { return ObDatum(reinterpret_cast<const char *>(&value), 8, false); }
ObDatum string_value(const std::string &value) { return ObDatum(value.data(), value.size(), false); }
ObDatum null_value() { ObDatum datum; datum.set_null(); return datum; }

struct TopNStoredRow
{
  explicit TopNStoredRow(const std::vector<ObDatum> &values)
  {
    using Row = ObChunkDatumStore::StoredRow;
    const uint32_t extra = (8 - (sizeof(Row) + values.size() * sizeof(StoredDatumCell)) % 8) % 8;
    const int64_t size = ObChunkDatumStore::Block::row_store_size(values.data(), values.size(), extra);
    memory_.resize(size);
    row_ = new (memory_.data()) Row;
    EXPECT_EQ(OB_SUCCESS, row_->copy_shadow_datums(values.data(), values.size(), row_->payload_,
        size - sizeof(Row), size, extra));
  }
  void poison() { MEMSET(memory_.data(), 0xA5, memory_.size()); }
  std::vector<char> memory_;
  ObChunkDatumStore::StoredRow *row_ = nullptr;
};

struct TopNMessageCopy
{
  ~TopNMessageCopy() { if (value_ != nullptr) { value_->destroy(); ob_free(value_); } }
  ObP2PDatahubMsgBase *value_ = nullptr;
};

class TopNPageOwner final : public ObIAllocator
{
public:
  void *alloc(const int64_t size) override { ++calls_; return fail_ ? nullptr : arena_.alloc(size); }
  void *alloc(const int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *value) override { arena_.free(value); }
  ObArenaAllocator arena_;
  int64_t calls_ = 0;
  bool fail_ = false;
};

// Actual public Raw->CG expressions supply both the producer metadata and the
// semantic probe CopyPlans. No FullType is inferred from the retained 4B meta.
class TopNInputs
{
public:
  static constexpr int64_t BATCH = 4;
  TopNInputs() : owner_("NativeTopNTest"), exec_(owner_), eval_(exec_), info_(owner_),
      cg_(owner_, nullptr, nullptr, 0, 0), factory_(owner_), raw_(nullptr), probe_(nullptr) {}
  void prepare(std::initializer_list<SqlValueType> kinds)
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    exec_.set_my_session(&session_);
    ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_OP_PUSHDOWN_TOPN_FILTER, raw_));
    ASSERT_EQ(OB_SUCCESS, raw_->init_param_exprs(kinds.size()));
    ObRawExprResType boolean; boolean.set_int32(); boolean.set_accuracy(ObAccuracy(1, 0));
    boolean.set_collation_type(CS_TYPE_BINARY); boolean.set_collation_level(CS_LEVEL_NUMERIC);
    raw_->set_result_type(boolean);
    ASSERT_EQ(OB_SUCCESS, raw_->add_flag(CNT_COLUMN));
    for (SqlValueType kind : kinds) {
      ObColumnRefRawExpr *column = nullptr;
      ASSERT_EQ(OB_SUCCESS, factory_.create_raw_expr(T_REF_COLUMN, column));
      ObRawExprResType type; type.set_type(kind);
      type.set_collation_type(kind == ObVarcharType ? CS_TYPE_UTF8MB4_BIN : CS_TYPE_BINARY);
      type.set_collation_level(kind == ObVarcharType ? CS_LEVEL_IMPLICIT : CS_LEVEL_NUMERIC);
      if (kind == ObVarcharType) { type.set_length(64); }
      else { type.set_accuracy(ObAccuracy(kind == ObDecimalIntType ? 30 : 20,
                                         kind == ObDecimalIntType ? 5 : 0)); }
      column->set_result_type(type);
      ASSERT_EQ(OB_SUCCESS, column->add_flag(IS_COLUMN));
      ASSERT_EQ(OB_SUCCESS, column->add_flag(CNT_COLUMN));
      ASSERT_EQ(OB_SUCCESS, raw_->add_param_expr(column));
      columns_.push_back(column);
    }
    ObRawExprUniqueSet roots(false); ASSERT_EQ(OB_SUCCESS, roots.append(raw_));
    cg_.set_batch_size(BATCH);
    ASSERT_EQ(OB_SUCCESS, cg_.generate(roots, info_));
    ASSERT_EQ(OB_SUCCESS, exec_.create_physical_plan_ctx());
    exec_.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
    ASSERT_EQ(OB_SUCCESS, info_.pre_alloc_exec_memory(exec_));
    eval_.frames_ = exec_.get_frames(); eval_.max_batch_size_ = BATCH;
    probe_ = static_cast<ObExpr *>(cg_.get_left_value_rt_expr(*raw_));
    ASSERT_NE(nullptr, probe_);
    ASSERT_EQ(&ObExprTopNFilter::eval_topn_filter, probe_->eval_func_);
  }
  void init(ObPushDownTopNFilterMsg &msg, bool ascending = true, bool ties = false)
  {
    ObPushDownTopNFilterInfo details(owner_);
    ObSEArray<ObTopNFilterCmpMeta, 4> metas;
    ObSEArray<ObSortFieldCollation, 4> collations;
    for (int64_t i = 0; i < probe_->arg_cnt_; ++i) {
      const ObExpr *expr = probe_->args_[i];
      ObTopNFilterCmpMeta meta;
      meta.obj_meta_ = expr->obj_meta_;
      meta.cmp_func_ = ObDatumFuncs::get_nullsafe_cmp_func(expr->datum_meta_.type_, expr->datum_meta_.type_,
          NULL_LAST, expr->datum_meta_.cs_type_, expr->datum_meta_.scale_, expr->obj_meta_.has_lob_header(),
          expr->datum_meta_.precision_, expr->datum_meta_.precision_);
      ASSERT_NE(nullptr, meta.cmp_func_);
      ASSERT_EQ(OB_SUCCESS, metas.push_back(meta));
      ASSERT_EQ(OB_SUCCESS, collations.push_back(ObSortFieldCollation(i, expr->obj_meta_.get_collation_type(), ascending, NULL_LAST)));
    }
    ASSERT_EQ(OB_SUCCESS, details.init(77, probe_->arg_cnt_, probe_->arg_cnt_, metas,
        ObP2PDatahubMsgBase::PD_TOPN_FILTER_MSG, 0, false, false, BATCH, 0.5));
    msg.set_msg_type(ObP2PDatahubMsgBase::PD_TOPN_FILTER_MSG);
    ASSERT_EQ(OB_SUCCESS, msg.init(&details, &collations, &exec_, 1, ties));
  }
  Datum &cell(int64_t column, int64_t row = 0) { return probe_->args_[column]->locate_batch_datums(eval_)[row]; }
  void ready()
  {
    for (uint32_t i = 0; i < probe_->arg_cnt_; ++i) {
      probe_->args_[i]->get_eval_info(eval_).evaluated_ = true;
      probe_->args_[i]->get_evaluated_flags(eval_).set_all(BATCH);
    }
  }
  int probe(ObPushDownTopNFilterMsg &msg, Datum &result)
  {
    ready(); ObEvalCtx::BatchInfoScopeGuard scope(eval_); scope.set_batch_size(BATCH); scope.set_batch_idx(0);
    return msg.filter_out_data(*probe_, eval_, context_, result);
  }
  int batch_probe(ObPushDownTopNFilterMsg &msg, const ObBitVector &skip)
  { ready(); return msg.filter_out_data_batch(*probe_, eval_, skip, BATCH, context_); }
  ObArenaAllocator owner_;
  ObExecContext exec_;
  ObSQLSessionInfo session_;
  ObEvalCtx eval_;
  ObExprFrameInfo info_;
  ObStaticEngineExprCG cg_;
  ObRawExprFactory factory_;
  ObOpRawExpr *raw_;
  ObExpr *probe_;
  std::vector<ObColumnRefRawExpr *> columns_;
  ObExprTopNFilterContext context_;
};

class NativeTopNFilter : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, ObCharset::init_charset());
    ObExprOperatorFactory::register_expr_operators();
  }
};

// This is an independent v1 record assembled from the unchanged old member
// serializers, not a claim of a captured binary from a historical executable.
std::vector<char> legacy_wire(const ObPushDownTopNFilterMsg &source,
                             const std::vector<ObDatum> &values, int64_t declared_count = -1)
{
  std::vector<char> wire(8192);
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, int64_t(1)));
  const int64_t length_pos = pos;
  pos += serialization::OB_SERIALIZE_SIZE_NEED_BYTES;
  const int64_t begin = pos;
  EXPECT_EQ(OB_SUCCESS, source.ObP2PDatahubMsgBase::serialize(wire.data(), wire.size(), pos));
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, static_cast<int64_t>(values.size())));
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, compares(source)));
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos,
      declared_count < 0 ? static_cast<int64_t>(values.size()) : declared_count));
  for (const ObDatum &value : values) { EXPECT_EQ(OB_SUCCESS, value.serialize(wire.data(), wire.size(), pos)); }
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, static_cast<int64_t>(values.size())));
  for (const ObDatum &value : values) {
    EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, int64_t(value.len_) * 2));
  }
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, int64_t(7)));
  EXPECT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), pos, false));
  int64_t patch_pos = 0;
  EXPECT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(wire.data() + length_pos,
      serialization::OB_SERIALIZE_SIZE_NEED_BYTES, patch_pos, pos - begin));
  wire.resize(pos);
  return wire;
}

std::vector<char> serialize_topn(const ObPushDownTopNFilterMsg &msg)
{
  std::vector<char> result(msg.get_serialize_size()); int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, msg.serialize(result.data(), result.size(), pos));
  EXPECT_EQ(static_cast<int64_t>(result.size()), pos);
  return result;
}

TEST_F(NativeTopNFilter, stored_row_integers_are_inline_without_payload_allocations)
{
  TopNInputs input; input.prepare({ObIntType, ObUInt64Type});
  TopNPageOwner owner; ModulePageAllocator pages(owner);
  ObPushDownTopNFilterMsg msg;
  ASSERT_EQ(OB_SUCCESS, static_cast<ObArenaAllocator &>(msg.get_allocator()).get_arena().init(8192, pages));
  input.init(msg);
  auto &arena = static_cast<ObArenaAllocator &>(msg.get_allocator());
  const int64_t used = arena.used(), allocations = owner.calls_;
  owner.fail_ = true;
  int64_t signed_key = INT64_MIN; uint64_t unsigned_key = UINT64_MAX;
  TopNStoredRow first({signed_value(signed_key), unsigned_value(unsigned_key)});
  bool updated = false;
  ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(first.row_, updated)); ASSERT_TRUE(updated);
  first.poison(); signed_key = 0; unsigned_key = 0;
  ASSERT_EQ(2, cells(msg).count());
  EXPECT_EQ(INT64_MIN, cells(msg).at(0).get_int()); EXPECT_EQ(UINT64_MAX, cells(msg).at(1).get_uint());
  EXPECT_EQ(0U, cells(msg).at(0).length()); EXPECT_EQ(0U, cells(msg).at(1).length());
  EXPECT_EQ(0, capacities(msg).at(0)); EXPECT_EQ(0, capacities(msg).at(1));
  signed_key = INT64_MAX; unsigned_key = 0;
  TopNStoredRow second({signed_value(signed_key), unsigned_value(unsigned_key)});
  ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(second.row_, updated)); ASSERT_TRUE(updated); second.poison();
  EXPECT_EQ(INT64_MAX, cells(msg).at(0).get_int()); EXPECT_EQ(0U, cells(msg).at(1).get_uint());
  EXPECT_EQ(used, arena.used()); EXPECT_EQ(allocations, owner.calls_); EXPECT_EQ(2, msg.get_current_data_version());
}

TEST_F(NativeTopNFilter, reference_growth_failure_keeps_all_columns_and_can_retry)
{
  TopNInputs input; input.prepare({ObIntType, ObVarcharType});
  TopNPageOwner owner; ModulePageAllocator pages(owner); ObPushDownTopNFilterMsg msg;
  ASSERT_EQ(OB_SUCCESS, static_cast<ObArenaAllocator &>(msg.get_allocator()).get_arena().init(8192, pages)); input.init(msg);
  int64_t key = 9; std::string text("m\0d", 3);
  ObDatum flagged = string_value(text); flagged.set_has_lob_header();
  TopNStoredRow first({signed_value(key), flagged}); bool updated = false;
  ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(first.row_, updated)); ASSERT_TRUE(updated); first.poison();
  const void *pointer = cells(msg).at(1).get_reference(); const int64_t capacity = capacities(msg).at(1);
  const int64_t calls = owner.calls_;
  key = -5; text.assign(1024 * 1024, 'a'); TopNStoredRow large({signed_value(key), string_value(text)});
  owner.fail_ = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, msg.update_filter_data(large.row_, updated)); EXPECT_FALSE(updated);
  EXPECT_GT(owner.calls_, calls); EXPECT_EQ(1, msg.get_current_data_version());
  EXPECT_EQ(9, cells(msg).at(0).get_int()); EXPECT_EQ(pointer, cells(msg).at(1).get_reference());
  EXPECT_EQ(std::string("m\0d", 3), bytes(cells(msg).at(1))); EXPECT_TRUE(cells(msg).at(1).has_lob_header());
  EXPECT_EQ(capacity, capacities(msg).at(1));
  owner.fail_ = false; ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(large.row_, updated)); ASSERT_TRUE(updated);
  large.poison(); text.assign(text.size(), 'x'); EXPECT_EQ(-5, cells(msg).at(0).get_int());
  EXPECT_EQ('a', bytes(cells(msg).at(1)).front()); EXPECT_FALSE(cells(msg).at(1).has_lob_header());
  const void *large_pointer = cells(msg).at(1).get_reference(); const int64_t grown = capacities(msg).at(1);
  key = 11; text = "z"; TopNStoredRow shorter({signed_value(key), string_value(text)});
  ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(shorter.row_, updated)); shorter.poison();
  EXPECT_EQ(large_pointer, cells(msg).at(1).get_reference()); EXPECT_EQ(grown, capacities(msg).at(1));
  EXPECT_EQ("z", bytes(cells(msg).at(1))); EXPECT_EQ(3, msg.get_current_data_version());
  TopNStoredRow with_null({signed_value(key), null_value()});
  ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(with_null.row_, updated)); EXPECT_FALSE(updated);
  EXPECT_EQ(3, msg.get_current_data_version()); EXPECT_EQ("z", bytes(cells(msg).at(1)));
}

TEST_F(NativeTopNFilter, scalar_probe_batch_ties_and_merge_use_real_expression_types)
{
  TopNInputs input; input.prepare({ObIntType});
  ObPushDownTopNFilterMsg left, right, ties, descending;
  input.init(left); input.init(right); input.init(ties, true, true); input.init(descending, false);
  int64_t bound = 10, smaller = 5; TopNStoredRow row({signed_value(bound)}), other({signed_value(smaller)});
  bool updated = false;
  ASSERT_EQ(OB_SUCCESS, left.update_filter_data(row.row_, updated));
  ASSERT_EQ(OB_SUCCESS, right.update_filter_data(other.row_, updated));
  ASSERT_EQ(OB_SUCCESS, ties.update_filter_data(row.row_, updated));
  ASSERT_EQ(OB_SUCCESS, descending.update_filter_data(row.row_, updated));
  row.poison(); other.poison();
  Datum result;
  input.cell(0).set_int(9); ASSERT_EQ(OB_SUCCESS, input.probe(left, result)); EXPECT_EQ(1, result.get_int());
  input.cell(0).set_int(10); ASSERT_EQ(OB_SUCCESS, input.probe(left, result)); EXPECT_EQ(0, result.get_int());
  ASSERT_EQ(OB_SUCCESS, input.probe(ties, result)); EXPECT_EQ(1, result.get_int());
  input.cell(0).set_int(11); ASSERT_EQ(OB_SUCCESS, input.probe(descending, result)); EXPECT_EQ(1, result.get_int());
  input.cell(0).set_null(); ASSERT_EQ(OB_SUCCESS, input.probe(left, result)); EXPECT_EQ(1, result.get_int());
  ASSERT_EQ(OB_SUCCESS, left.merge(right)); ASSERT_EQ(OB_SUCCESS, right.destroy()); EXPECT_EQ(5, cells(left).at(0).get_int());
  uint64_t skip_word = 0; ObBitVector &skip = *to_bit_vector(&skip_word);
  input.cell(0, 0).set_int(4); input.cell(0, 1).set_int(5); input.cell(0, 2).set_int(6); input.cell(0, 3).set_null();
  ASSERT_EQ(OB_SUCCESS, input.batch_probe(left, skip));
  Datum *results = input.probe_->locate_batch_datums(input.eval_);
  EXPECT_EQ(1, results[0].get_int()); EXPECT_EQ(0, results[1].get_int());
  EXPECT_EQ(0, results[2].get_int()); EXPECT_EQ(1, results[3].get_int());
}

TEST_F(NativeTopNFilter, assign_and_storage_projection_have_distinct_real_owners)
{
  TopNInputs input; input.prepare({ObIntType, ObVarcharType});
  ObPushDownTopNFilterMsg source, copy; input.init(source);
  int64_t key = INT64_MIN; std::string text("a\0b", 3); TopNStoredRow row({signed_value(key), string_value(text)});
  bool updated = false; ASSERT_EQ(OB_SUCCESS, source.update_filter_data(row.row_, updated));
  ASSERT_EQ(OB_SUCCESS, copy.assign(source));
  TopNMessageCopy cloned;
  ASSERT_EQ(OB_SUCCESS, source.deep_copy_msg(cloned.value_)); ASSERT_NE(nullptr, cloned.value_);
  auto &deep = static_cast<ObPushDownTopNFilterMsg &>(*cloned.value_);
  EXPECT_NE(cells(source).at(1).get_reference(), cells(deep).at(1).get_reference());
  EXPECT_NE(cells(source).at(1).get_reference(), cells(copy).at(1).get_reference());
  ObArenaAllocator destination; ObPushdownExprSpec spec(destination); ObPushdownOperator op(input.eval_, spec);
  ObPushdownDynamicFilterNode node(destination); node.set_col_idx(0);
  ObDynamicFilterExecutor executor(destination, node, op); ObRuntimeFilterParams params;
  bool prepared = false;
  ASSERT_EQ(OB_SUCCESS, copy.prepare_storage_white_filter_data(executor, input.eval_, params, prepared));
  ASSERT_TRUE(prepared); ASSERT_EQ(1, params.count()); EXPECT_EQ(8U, params.at(0).len_);
  ASSERT_EQ(OB_SUCCESS, source.destroy()); row.poison(); text.assign(text.size(), 'x');
  EXPECT_EQ(INT64_MIN, cells(copy).at(0).get_int()); EXPECT_EQ(std::string("a\0b", 3), bytes(cells(copy).at(1)));
  ASSERT_EQ(OB_SUCCESS, copy.destroy()); EXPECT_EQ(INT64_MIN, params.at(0).get_int());
  EXPECT_EQ(INT64_MIN, cells(deep).at(0).get_int()); EXPECT_EQ(std::string("a\0b", 3), bytes(cells(deep).at(1)));
}

TEST_F(NativeTopNFilter, actual_decimal_widths_and_typed_null_survive_v1_boundaries)
{
  TopNInputs input; input.prepare({ObDecimalIntType});
  for (int32_t width : {0, 4, 8, 16, 32, 64}) {
    SCOPED_TRACE(width);
    ObPushDownTopNFilterMsg source, receiver; input.init(source);
    alignas(8) unsigned char coefficient[64] = {};
    coefficient[0] = 7;
    ObDatum decimal; decimal.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(coefficient), width);
    TopNStoredRow row({decimal}); bool updated = false;
    ASSERT_EQ(OB_SUCCESS, source.update_filter_data(row.row_, updated)); ASSERT_TRUE(updated); row.poison();
    ASSERT_EQ(static_cast<uint32_t>(width), cells(source).at(0).length());
    if (width > 0) { EXPECT_NE(static_cast<const void *>(coefficient), cells(source).at(0).get_reference()); }
    auto old = legacy_wire(source, {decimal});
    int64_t pos = 0; ASSERT_EQ(OB_SUCCESS, receiver.deserialize(old.data(), old.size(), pos)); EXPECT_EQ(old.size(), pos);
    MEMSET(coefficient, 0xA5, sizeof(coefficient)); MEMSET(old.data(), 0xA5, old.size());
    EXPECT_EQ(static_cast<uint32_t>(width), cells(receiver).at(0).length());
    EXPECT_EQ(5, compares(receiver).at(0).build_meta_.obj_meta_.get_scale());
    if (width > 0) { EXPECT_EQ(7, *static_cast<const unsigned char *>(cells(receiver).at(0).get_reference())); }
    auto next = serialize_topn(receiver); ObPushDownTopNFilterMsg again;
    pos = 0; ASSERT_EQ(OB_SUCCESS, again.deserialize(next.data(), next.size(), pos));
    ASSERT_EQ(OB_SUCCESS, receiver.destroy()); MEMSET(next.data(), 0xA5, next.size());
    EXPECT_EQ(static_cast<uint32_t>(width), cells(again).at(0).length());
  }
  ObPushDownTopNFilterMsg empty, null_receiver; input.init(empty);
  auto null_wire = legacy_wire(empty, {null_value()}); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, null_receiver.deserialize(null_wire.data(), null_wire.size(), pos));
  ASSERT_EQ(1, cells(null_receiver).count()); EXPECT_TRUE(cells(null_receiver).at(0).is_null());
  EXPECT_EQ(ObDecimalIntType, compares(null_receiver).at(0).build_meta_.obj_meta_.get_type());
  EXPECT_EQ(5, compares(null_receiver).at(0).build_meta_.obj_meta_.get_scale()); EXPECT_TRUE(null_receiver.is_empty());
  // Construct a noncanonical old record explicitly: current set_null clears
  // length. A serialized NULL bit with stale payload is accepted semantically,
  // without retaining the obsolete payload solely for re-emission.
  int64_t stale_bits = 19;
  ObDatum stale_null = signed_value(stale_bits); stale_null.null_ = 1;
  ASSERT_EQ(8U, stale_null.len_);
  auto stale_wire = legacy_wire(empty, {stale_null}); pos = 0;
  ASSERT_EQ(OB_SUCCESS, null_receiver.deserialize(stale_wire.data(), stale_wire.size(), pos));
  EXPECT_TRUE(cells(null_receiver).at(0).is_null()); EXPECT_EQ(0U, cells(null_receiver).at(0).length());
}

TEST_F(NativeTopNFilter, new_sender_keeps_the_old_member_order_and_physical_cell_records)
{
  TopNInputs input; input.prepare({ObIntType, ObVarcharType});
  ObPushDownTopNFilterMsg msg; input.init(msg);
  int64_t key = INT64_MIN; std::string text("x\0y", 3);
  ObDatum flagged = string_value(text); flagged.set_has_lob_header();
  TopNStoredRow row({signed_value(key), flagged}); bool updated = false;
  ASSERT_EQ(OB_SUCCESS, msg.update_filter_data(row.row_, updated)); row.poison();
  auto wire = serialize_topn(msg); int64_t pos = 0, version = 0, body_size = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, version)); EXPECT_EQ(1, version);
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, body_size));
  EXPECT_EQ(static_cast<int64_t>(wire.size()) - pos, body_size);
  ObPushDownTopNFilterMsg old_base;
  ASSERT_EQ(OB_SUCCESS, old_base.ObP2PDatahubMsgBase::deserialize(wire.data(), wire.size(), pos));
  int64_t total = 0, data_version = 0; bool ties = true; ObArenaAllocator owner;
  ObFixedArray<ObTopNFilterCompare, ObIAllocator> old_compares(owner);
  ObFixedArray<ObDatum, ObIAllocator> old_values(owner);
  ObFixedArray<int64_t, ObIAllocator> old_capacities(owner);
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, total)); EXPECT_EQ(2, total);
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, old_compares));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, old_values));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, old_capacities));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, data_version));
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), pos, ties));
  ASSERT_EQ(2, old_values.count()); ASSERT_EQ(2, old_compares.count()); ASSERT_EQ(2, old_capacities.count());
  EXPECT_EQ(INT64_MIN, old_values.at(0).get_int()); EXPECT_EQ(8U, old_values.at(0).len_);
  EXPECT_EQ(std::string("x\0y", 3), std::string(old_values.at(1).ptr_, old_values.at(1).len_));
  EXPECT_TRUE(old_values.at(1).has_lob_header()); EXPECT_EQ(0, old_capacities.at(0));
  EXPECT_EQ(ObIntType, old_compares.at(0).build_meta_.obj_meta_.get_type());
  EXPECT_EQ(1, data_version); EXPECT_FALSE(ties); EXPECT_EQ(static_cast<int64_t>(wire.size()), pos);
}

TEST_F(NativeTopNFilter, v1_empty_and_malformed_records_do_not_publish_partial_state)
{
  TopNInputs input; input.prepare({ObIntType});
  ObPushDownTopNFilterMsg source, receiver; input.init(source);
  int64_t key = 17; TopNStoredRow row({signed_value(key)}); bool updated = false;
  ASSERT_EQ(OB_SUCCESS, source.update_filter_data(row.row_, updated));
  auto old = legacy_wire(source, {signed_value(key)}); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, receiver.deserialize(old.data(), old.size(), pos)); EXPECT_EQ(17, cells(receiver).at(0).get_int());
  auto bad_count = legacy_wire(source, {signed_value(key)}, 2);
  pos = 0; EXPECT_EQ(OB_DESERIALIZE_ERROR, receiver.deserialize(bad_count.data(), bad_count.size(), pos)); EXPECT_EQ(0, pos);
  EXPECT_EQ(17, cells(receiver).at(0).get_int()); EXPECT_EQ(7, receiver.get_current_data_version());
  pos = 0; EXPECT_EQ(OB_DESERIALIZE_ERROR, receiver.deserialize(old.data(), old.size() - 1, pos)); EXPECT_EQ(0, pos);
  EXPECT_EQ(17, cells(receiver).at(0).get_int());
  // Actual old init() allocated a default ObDatum per effective key: pack=0,
  // no payload, and is_empty=true. It is an unused slot, not an integer zero.
  ObPushDownTopNFilterMsg initialized_empty, initialized_copy; input.init(initialized_empty);
  auto initialized_wire = legacy_wire(initialized_empty, {ObDatum()}); pos = 0;
  ASSERT_EQ(OB_SUCCESS, initialized_copy.deserialize(initialized_wire.data(), initialized_wire.size(), pos));
  ASSERT_EQ(1, cells(initialized_copy).count()); EXPECT_TRUE(initialized_copy.is_empty());
  EXPECT_TRUE(cells(initialized_copy).at(0).is_null());
  ASSERT_EQ(OB_SUCCESS, initialized_copy.update_filter_data(row.row_, updated)); EXPECT_TRUE(updated);
  EXPECT_EQ(17, cells(initialized_copy).at(0).get_int()); EXPECT_FALSE(initialized_copy.is_empty());
  // The same missing scalar payload is invalid when the message has a value.
  ObPushDownTopNFilterMsg invalid_copy; auto missing_scalar = legacy_wire(source, {ObDatum()}); pos = 0;
  EXPECT_EQ(OB_INVALID_ARGUMENT, invalid_copy.deserialize(missing_scalar.data(), missing_scalar.size(), pos));
  EXPECT_EQ(0, pos); EXPECT_EQ(0, cells(invalid_copy).count()); EXPECT_TRUE(invalid_copy.is_empty());
  ObPushDownTopNFilterMsg empty, empty_copy;
  auto empty_wire = serialize_topn(empty); pos = 0;
  ASSERT_EQ(OB_SUCCESS, empty_copy.deserialize(empty_wire.data(), empty_wire.size(), pos));
  EXPECT_EQ(0, cells(empty_copy).count()); EXPECT_TRUE(empty_copy.is_empty());
}
} // namespace
