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

#include "common/datum/datum_parameter.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/charset/ob_charset.h"
#include "sql/code_generator/ob_static_engine_expr_cg.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/engine/table/ob_table_scan_op.h"
#include "sql/engine/basic/ob_batch_result_holder.h"
#include "sql/engine/expr/ob_expr_operator_factory.h"
#include "sql/engine/px/ob_px_util.h"
#include "sql/ob_sql_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/resolver/expr/ob_raw_expr_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "share/schema/ob_table_schema.h"
#include "share/system_variable/ob_system_variable_init.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, Datum &);

// Exercise CG's production layout/allocation entry points without exposing
// them as public APIs or requiring an optimizer/session fixture.
struct ScalarFrameLayoutTestAccess
{
  static int parameters(ObStaticEngineExprCG &cg, const common::ObIArray<ObRawExpr *> &raw,
                        int64_t &next_frame, ObExprFrameInfo &info)
  {
    return cg.cg_param_frame_layout(raw, next_frame, info.param_frame_);
  }

  static int constants(ObStaticEngineExprCG &cg, const common::ObIArray<ObRawExpr *> &raw,
                       ObExprFrameInfo &info)
  {
    int ret = cg.cg_expr_basic(raw);
    int64_t next_frame = 0;
    if (ret == common::OB_SUCCESS) {
      ret = cg.cg_const_frame_layout(raw, next_frame, info.const_frame_);
    }
    if (ret == common::OB_SUCCESS) {
      ret = cg.alloc_const_frame(raw, info.const_frame_, info.const_frame_ptrs_);
    }
    return ret;
  }
};
} // namespace sql
} // namespace oceanbase

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
// Pin the new ABI independently of the production layout helper; stored cells
// and common::ObDatum keep their separate legacy descriptor ABI.
constexpr int64_t NATIVE_SLOT_BYTES = 24;
constexpr int64_t NATIVE_EVAL_OFFSET = 16;
const int64_t NATIVE_SLOTS_PER_FRAME = MAX_FRAME_SIZE / NATIVE_SLOT_BYTES;

class FrameAllocator final : public ObIAllocator
{
public:
  FrameAllocator() : fail_size_(0), arena_("ScalarFrame"), blocks_() {}
  void *alloc(int64_t size) override
  {
    if (size == fail_size_) { return nullptr; }
    char *bytes = static_cast<char *>(arena_.alloc(size + 8));
    if (bytes != nullptr) {
      MEMSET(bytes + size, 0xa7, 8);
      blocks_.push_back({bytes, size});
    }
    return bytes;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *bytes) override { arena_.free(bytes); }
  void expect_frame_canary(const char *frame) const
  {
    bool found = false;
    for (const Block &block : blocks_) {
      if (block.bytes_ == frame) {
        found = true;
        for (int64_t i = 0; i < 8; ++i) {
          EXPECT_EQ(0xa7, static_cast<unsigned char>(frame[block.size_ + i]));
        }
        break;
      }
    }
    EXPECT_TRUE(found);
  }
  int64_t allocation_size(const char *frame) const
  {
    for (const Block &block : blocks_) { if (block.bytes_ == frame) { return block.size_; } }
    return -1;
  }
  int64_t fail_size_;
private:
  struct Block { char *bytes_; int64_t size_; };
  ObArenaAllocator arena_;
  std::vector<Block> blocks_;
};

void setup_context(ObExecContext &exec, ObSqlCtx &sql_ctx)
{
  sql_ctx.exec_type_ = MpQuery;
  sql_ctx.is_prepare_protocol_ = true;
  sql_ctx.is_dynamic_sql_ = true;
  sql_ctx.stmt_type_ = stmt::T_SELECT;
  exec.set_sql_ctx(&sql_ctx);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
}

int64_t input_value(int64_t index)
{
  return index == 0 ? INT64_MIN : (index % 3 == 0 ? -index : index + 41);
}

void load_parameters(ObPhysicalPlanCtx &plan, int64_t original, std::vector<int64_t> &input,
                     int64_t null_index = -1)
{
  SqlTypeMeta meta;
  meta.set_int(); meta.set_scale(0); meta.set_collation_level(CS_LEVEL_COERCIBLE);
  plan.set_original_param_cnt(original);
  for (int64_t i = 0; i < static_cast<int64_t>(input.size()); ++i) {
    input[i] = input_value(i);
    DatumParameter parameter;
    parameter.set_type_meta(meta);
    parameter.set_accuracy(ObAccuracy(20, 0));
    parameter.datum_.set_int(input[i]);
    if (i == null_index) { parameter.datum_.set_null(); }
    ASSERT_EQ(OB_SUCCESS, plan.append_parameter_input(parameter));
  }
}

struct ExpectedSlot
{
  int64_t parameter_;
  int64_t page_;
  int64_t slot_;
};

void verify_parameter_layout(int64_t original, int64_t count,
                             std::initializer_list<ExpectedSlot> expected,
                             int64_t null_index = -1)
{
  FrameAllocator owner;
  ObSqlCtx sql_ctx;
  ObExecContext exec(owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(exec, sql_ctx));
  ObPhysicalPlanCtx &plan = *exec.get_physical_plan_ctx();
  std::vector<int64_t> input(count);
  ASSERT_NO_FATAL_FAILURE(load_parameters(plan, original, input, null_index));
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  int64_t extracted = 101;
  EXPECT_TRUE(plan.read_native_bigint_parameter(0, extracted));
  EXPECT_EQ(INT64_MIN, extracted);
  // Request payload must no longer supply either the native-owner or clone path.
  MEMSET(input.data(), 0x5c, input.size() * sizeof(input[0]));

  ObExprFrameInfo info(owner);
  ObStaticEngineExprCG cg(owner, nullptr, nullptr, original, count);
  ObConstRawExpr raw[8];
  ObExpr expr[8];
  ObSEArray<ObRawExpr *, 8> selected;
  ASSERT_LE(expected.size(), 8U);
  int64_t n = 0;
  for (const ExpectedSlot &slot : expected) {
    DatumValue index;
    ASSERT_EQ(OB_SUCCESS, index.set_unknown(slot.parameter_, owner));
    ASSERT_EQ(OB_SUCCESS, raw[n].set_value_borrowed(index));
    raw[n].set_expr_type(T_QUESTIONMARK);
    raw[n].set_rt_expr(&expr[n]);
    expr[n].type_ = T_QUESTIONMARK;
    expr[n].datum_meta_.type_ = ObIntType;
    ASSERT_EQ(OB_SUCCESS, selected.push_back(&raw[n++]));
  }
  // A nonzero constant-frame prefix checks the CG-relative/runtime binding.
  alignas(8) char prefix[2][NATIVE_SLOT_BYTES] = {};
  ASSERT_EQ(OB_SUCCESS, info.const_frame_.reserve(2));
  ASSERT_EQ(OB_SUCCESS, info.const_frame_ptrs_.reserve(2));
  for (int64_t i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, info.const_frame_.push_back(ObFrameInfo(1, i, NATIVE_SLOT_BYTES, 0, 0)));
    ASSERT_EQ(OB_SUCCESS, info.const_frame_ptrs_.push_back(prefix[i]));
  }
  int64_t next_frame = 2;
  ASSERT_EQ(OB_SUCCESS, ScalarFrameLayoutTestAccess::parameters(cg, selected, next_frame, info));
  ASSERT_EQ(plan.get_param_frame_ptrs().count(), info.param_frame_.count());
  ASSERT_EQ(OB_SUCCESS, info.pre_alloc_exec_memory(exec));
  EXPECT_EQ(static_cast<uint64_t>(next_frame), exec.get_frame_cnt());
  ObEvalCtx eval(exec);
  std::vector<Datum *> projections(count, nullptr);
  n = 0;
  for (const ExpectedSlot &slot : expected) {
    SCOPED_TRACE(slot.parameter_);
    ObExpr &e = expr[n++];
    ASSERT_EQ(2 + slot.page_, e.frame_idx_);
    ASSERT_EQ(slot.slot_ * NATIVE_SLOT_BYTES, e.datum_off_);
    ASSERT_EQ(slot.slot_ * NATIVE_SLOT_BYTES + NATIVE_EVAL_OFFSET, e.eval_info_off_);
    EXPECT_EQ(0U, e.res_buf_len_);
    const auto &page = ParameterValuePage::from_handle(plan.get_param_frame_ptrs().at(slot.page_));
    EXPECT_TRUE(e.native_parameter_alias_);
    EXPECT_EQ(projections[slot.parameter_], plan.locate_materialized_parameter(slot.parameter_));
    EXPECT_EQ(plan.get_parameter_eval_info(slot.parameter_), &e.get_eval_info(eval));
    Datum *evaluated = nullptr;
    ASSERT_EQ(OB_SUCCESS, e.eval(eval, evaluated));
    if (projections[slot.parameter_] == nullptr) { projections[slot.parameter_] = evaluated; }
    EXPECT_EQ(projections[slot.parameter_], evaluated);
    Datum &datum = e.locate_expr_datum(eval);
    EXPECT_EQ(evaluated, &datum);
    EXPECT_EQ(&page.eval_info(slot.slot_), &e.get_eval_info(eval));
    Datum owner_value;
    ASSERT_EQ(OB_SUCCESS, page.read(slot.slot_, owner_value));
    EXPECT_EQ(owner_value.state(), datum.state());
    EXPECT_FALSE(e.get_eval_info(eval).evaluated_);
    Datum questionmark;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(e, eval, questionmark));
    EXPECT_EQ(slot.parameter_ == null_index, datum.is_null());
    EXPECT_EQ(datum.is_null(), questionmark.is_null());
    if (!datum.is_null()) {
      EXPECT_EQ(input_value(slot.parameter_), datum.get_int());
      EXPECT_EQ(datum.get_int(), questionmark.get_int());
      EXPECT_EQ(0U, questionmark.length());
      // State writes must not overwrite the compact word or the ABI projection.
      ObEvalInfo &state = e.get_eval_info(eval);
      state.cnt_ = 57; state.notnull_ = true;
      EXPECT_EQ(input_value(slot.parameter_), datum.get_int());
      EXPECT_EQ(0U, datum.length());
    }
  }
  for (int64_t i = 0; i < plan.get_param_frame_ptrs().count(); ++i) {
    owner.expect_frame_canary(plan.get_param_frame_ptrs().at(i));
    EXPECT_LE(owner.allocation_size(plan.get_param_frame_ptrs().at(i)), MAX_FRAME_SIZE);
    EXPECT_EQ(2 + i, info.param_frame_.at(i).frame_idx_);
    EXPECT_EQ(info.param_frame_.at(i).expr_cnt_ * NATIVE_SLOT_BYTES,
              info.param_frame_.at(i).frame_size_);
  }
}

class ScalarConstants : public ::testing::Test
{
protected:
  ScalarConstants() : request_("ScalarRequest"), plan_("ScalarPlan"), info_(plan_), raw_(), text_("frame payload") {}
  void SetUp() override
  {
    ASSERT_EQ(OB_SUCCESS, info_.rt_exprs_.prepare_allocate(3));
    DatumValue value[3];
    ASSERT_EQ(OB_SUCCESS, value[0].set_int(INT64_MIN, request_));
    ASSERT_EQ(OB_SUCCESS, value[1].set_varchar(ObString::make_string(text_.c_str()), request_));
    value[1].set_collation_type(CS_TYPE_UTF8MB4_BIN);
    value[2].set_null();
    const ObItemType kinds[] = {T_INT, T_VARCHAR, T_NULL};
    ObSEArray<ObRawExpr *, 3> selected;
    for (int64_t i = 0; i < 3; ++i) {
      ASSERT_EQ(OB_SUCCESS, raw_[i].set_value_borrowed(value[i]));
      raw_[i].set_expr_type(kinds[i]);
      raw_[i].set_rt_expr(&info_.rt_exprs_.at(i));
      ASSERT_EQ(OB_SUCCESS, selected.push_back(&raw_[i]));
    }
    ObStaticEngineExprCG cg(plan_, nullptr, nullptr, 0, 0);
    ASSERT_EQ(OB_SUCCESS, ScalarFrameLayoutTestAccess::constants(cg, selected, info_));
    ASSERT_EQ(1, info_.const_frame_.count());
    ASSERT_EQ(1, info_.const_frame_ptrs_.count());
    for (int64_t i = 0; i < 3; ++i) {
      ASSERT_EQ(i * NATIVE_SLOT_BYTES, info_.rt_exprs_.at(i).datum_off_);
      ASSERT_EQ(i * NATIVE_SLOT_BYTES + NATIVE_EVAL_OFFSET, info_.rt_exprs_.at(i).eval_info_off_);
      ObEvalInfo *state = reinterpret_cast<ObEvalInfo *>(source_frame() + i * NATIVE_SLOT_BYTES + NATIVE_EVAL_OFFSET);
      state->flag_ = 0x0f; state->cnt_ = 71 + i;
    }
  }
  char *source_frame() { return info_.const_frame_ptrs_.at(0); }
  void poison_source()
  {
    Datum *external = reinterpret_cast<Datum *>(source_frame() + NATIVE_SLOT_BYTES);
    MEMSET(const_cast<void *>(external->get_reference()), 0xd5, external->length());
    MEMSET(source_frame(), 0xe6, info_.const_frame_.at(0).frame_size_);
    request_.reuse();
  }
  void expect_values(char *frame, bool copied = true)
  {
    const Datum &integer = *reinterpret_cast<Datum *>(frame);
    const Datum &string = *reinterpret_cast<Datum *>(frame + NATIVE_SLOT_BYTES);
    const Datum &null = *reinterpret_cast<Datum *>(frame + 2 * NATIVE_SLOT_BYTES);
    ASSERT_FALSE(integer.is_null()); EXPECT_EQ(INT64_MIN, integer.get_int());
    ASSERT_FALSE(string.is_null()); EXPECT_EQ(text_, std::string(static_cast<const char *>(string.get_reference()), string.length()));
    EXPECT_TRUE(null.is_null()); EXPECT_EQ(0U, null.length());
    for (int64_t i = 0; i < 3; ++i) {
      const ObEvalInfo &state = *reinterpret_cast<ObEvalInfo *>(frame + i * NATIVE_SLOT_BYTES + NATIVE_EVAL_OFFSET);
      EXPECT_EQ(copied ? 0x07 : 0x0f, state.flag_); EXPECT_EQ(71 + i, state.cnt_);
    }
  }
  ObArenaAllocator request_;
  ObArenaAllocator plan_;
  ObExprFrameInfo info_;
  ObConstRawExpr raw_[3];
  std::string text_;
};
} // namespace

TEST(ScalarFrameLayout, native_scalar_abi_and_parameter_page_boundaries)
{
  EXPECT_EQ(12U, sizeof(ObDatum)); EXPECT_EQ(16U, sizeof(Datum));
  EXPECT_EQ(4U, sizeof(ObEvalInfo));
  EXPECT_EQ(NATIVE_SLOT_BYTES, ScalarFrameLayout::slot_size());
  EXPECT_EQ(NATIVE_SLOTS_PER_FRAME, static_cast<int64_t>(ObExprFrameInfo::EXPR_CNT_PER_FRAME));
  const int64_t c = NATIVE_SLOTS_PER_FRAME;
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(0, c + 2, {{0, 0, 0}, {c - 1, 0, c - 1}, {c, 1, 0}, {c + 1, 1, 1}}));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(1, c + 2, {{0, 0, 0}, {1, 1, 0}, {c, 1, c - 1}, {c + 1, 2, 0}}));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c - 1, c + 2, {{c - 2, 0, c - 2}, {c - 1, 1, 0}, {c, 1, 1}, {c + 1, 1, 2}}));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c, c + 2, {{c - 1, 0, c - 1}, {c, 1, 0}, {c + 1, 1, 1}}));
}

TEST(ScalarFrameLayout, reordered_and_repeated_parameters_cross_both_page_ranges)
{
  const int64_t c = NATIVE_SLOTS_PER_FRAME;
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c + 1, c + 3,
      {{c + 2, 2, 1}, {c, 1, 0}, {0, 0, 0}, {c + 1, 2, 0}, {c + 2, 2, 1}, {c - 1, 0, c - 1}}));
}

TEST(ScalarFrameLayout, non_power_of_two_page_capacity_clamps_final_page_roundup)
{
  const int64_t c = NATIVE_SLOTS_PER_FRAME;
  ASSERT_NE(0, c & (c - 1));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(0, c - 1,
      {{0, 0, 0}, {c - 2, 0, c - 2}}));
}

TEST(ScalarFrameLayout, nullable_native_parameters_use_cg_offsets_after_request_poison)
{
  const int64_t c = NATIVE_SLOTS_PER_FRAME;
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c + 1, c + 3,
      {{c + 1, 2, 0}, {c, 1, 0}, {0, 0, 0}, {c - 1, 0, c - 1}, {c + 2, 2, 1}}, c));
}

TEST(ScalarFrameLayout, failed_second_original_frame_keeps_request_descriptor_unpublished)
{
  FrameAllocator owner;
  ObSqlCtx sql_ctx;
  ObExecContext exec(owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(exec, sql_ctx));
  ObPhysicalPlanCtx &plan = *exec.get_physical_plan_ctx();
  std::vector<int64_t> input(NATIVE_SLOTS_PER_FRAME + 1);
  ASSERT_NO_FATAL_FAILURE(load_parameters(plan, input.size(), input));
  const int64_t before = plan.get_parameter_inputs().at(0).get_int();
  owner.fail_size_ = ParameterValuePage::allocation_bytes(1);
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.init_datum_param_store());
  EXPECT_EQ(0, plan.get_param_frame_ptrs().count());
  int64_t extracted = 101;
  EXPECT_FALSE(plan.read_native_bigint_parameter(0, extracted));
  EXPECT_EQ(101, extracted);
  EXPECT_FALSE(plan.native_parameters_ready());
  EXPECT_EQ(before, plan.get_parameter_inputs().at(0).get_int());
  EXPECT_EQ(INT64_MIN, plan.get_parameter_inputs().at(0).get_int());
  owner.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  ASSERT_EQ(2, plan.get_param_frame_ptrs().count());
  EXPECT_TRUE(plan.read_native_bigint_parameter(0, extracted));
  EXPECT_EQ(INT64_MIN, extracted);
  for (int64_t i = 0; i < plan.get_param_frame_ptrs().count(); ++i) {
    owner.expect_frame_canary(plan.get_param_frame_ptrs().at(i));
  }
}

TEST_F(ScalarConstants, clone_keeps_integer_inline_and_owns_reference_payload)
{
  ObArenaAllocator clone_owner("ScalarClone");
  ObExprFrameInfo clone(clone_owner);
  ASSERT_EQ(OB_SUCCESS, clone.assign(info_, clone_owner));
  ASSERT_EQ(1, clone.const_frame_ptrs_.count());
  char *destination = clone.const_frame_ptrs_.at(0);
  const Datum &integer = *reinterpret_cast<Datum *>(destination);
  EXPECT_EQ(0U, integer.length()); EXPECT_EQ(0U, info_.rt_exprs_.at(0).res_buf_len_);
  EXPECT_NE(reinterpret_cast<Datum *>(source_frame() + NATIVE_SLOT_BYTES)->get_reference(),
            reinterpret_cast<Datum *>(destination + NATIVE_SLOT_BYTES)->get_reference());
  poison_source();
  ASSERT_NO_FATAL_FAILURE(expect_values(destination));
}

TEST_F(ScalarConstants, px_typed_scalar_wire_owns_decoded_payload)
{
  char *source[] = {source_frame()};
  const int64_t size = ObPxTreeSerializer::get_serialize_frame_info_size<false>(info_.const_frame_, info_, source, 1);
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_frame_info<false>(wire.data(), size, pos, info_.const_frame_, info_, source, 1));
  ASSERT_EQ(size, pos);
  // Explicit protocol fields replace the old raw frame-header memcpy.
  int64_t header_pos = 0, records = 0, first_expr = -1;
  int32_t version = 0;
  int8_t no_data = -1;
  ASSERT_EQ(OB_SUCCESS, serialization::decode_i32(wire.data(), size, header_pos, &version));
  ASSERT_EQ(2, version); // The explicit frame protocol now carries typed array/metadata v2.
  ASSERT_EQ(OB_SUCCESS, serialization::decode_i8(wire.data(), size, header_pos, &no_data));
  ASSERT_EQ(0, no_data);
  ASSERT_EQ(OB_SUCCESS, serialization::decode_i64(wire.data(), size, header_pos, &records));
  ASSERT_EQ(3, records);
  ASSERT_EQ(OB_SUCCESS, serialization::decode_i64(wire.data(), size, header_pos, &first_expr));
  ASSERT_EQ(0, first_expr);
  ObArenaAllocator decoded_owner("ScalarWire");
  char *decoded_array[] = {nullptr};
  char **decoded = decoded_array;
  int64_t frame_count = 1;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_frame_info<false>(wire.data(), size, pos, decoded_owner,
      info_.const_frame_, info_, nullptr, decoded, frame_count));
  ASSERT_EQ(size, pos); ASSERT_NE(nullptr, decoded[0]);
  poison_source(); MEMSET(wire.data(), 0x8a, wire.size());
  ASSERT_NO_FATAL_FAILURE(expect_values(decoded[0]));
}

TEST_F(ScalarConstants, complete_frame_info_roundtrip_owns_types_and_values)
{
  const int64_t size = info_.get_serialize_size();
  ASSERT_GT(size, 0);
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, info_.serialize(wire.data(), size, pos));
  ASSERT_EQ(size, pos);
  ObArenaAllocator owner("FrameInfoWire");
  ObExprFrameInfo decoded(owner);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, decoded.deserialize(wire.data(), size, pos));
  ASSERT_EQ(size, pos); ASSERT_EQ(3, decoded.rt_exprs_.count());
  ASSERT_EQ(1, decoded.const_frame_ptrs_.count());
  for (int64_t i = 0; i < 3; ++i) {
    EXPECT_TRUE(decoded.rt_exprs_.at(i).get_sql_type().equals(info_.rt_exprs_.at(i).get_sql_type()));
    EXPECT_NE(decoded.rt_exprs_.at(i).sql_type_, info_.rt_exprs_.at(i).sql_type_);
  }
  poison_source(); MEMSET(wire.data(), 0xe3, wire.size());
  ASSERT_NO_FATAL_FAILURE(expect_values(decoded.const_frame_ptrs_.at(0)));
}

TEST_F(ScalarConstants, px_scalar_serialization_reports_short_payload_buffer)
{
  char *source[] = {source_frame()};
  const int64_t size = ObPxTreeSerializer::get_serialize_frame_info_size<false>(info_.const_frame_, info_, source, 1);
  std::vector<char> wire(size);
  int64_t pos = 0;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, ObPxTreeSerializer::serialize_frame_info<false>(wire.data(), size - 2, pos,
      info_.const_frame_, info_, source, 1));
  EXPECT_EQ(0, pos);
  ASSERT_NO_FATAL_FAILURE(expect_values(source_frame(), false));
}

TEST_F(ScalarConstants, truncated_typed_group_never_publishes_a_partial_frame)
{
  char *source[] = {source_frame()};
  const int64_t size = ObPxTreeSerializer::get_serialize_frame_info_size<false>(info_.const_frame_, info_, source, 1);
  ASSERT_GT(size, 0);
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_frame_info<false>(wire.data(), size, pos,
      info_.const_frame_, info_, source, 1));
  ObArenaAllocator owner("FrameTruncate");
  for (int64_t length = 0; length < size; ++length) {
    char *frames[] = {nullptr};
    ObSEArray<char *, 1> pointers;
    pos = 0;
    EXPECT_NE(OB_SUCCESS, ObPxTreeSerializer::deserialize_frame_info<false>(wire.data(), length, pos,
        owner, info_.const_frame_, info_, &pointers, frames, 1));
    EXPECT_EQ(0, pos); EXPECT_EQ(nullptr, frames[0]); EXPECT_EQ(0, pointers.count());
    owner.reuse();
  }
  ASSERT_NO_FATAL_FAILURE(expect_values(source_frame(), false));
}

TEST(ScalarFrameLayout, scalar_dynamic_frame_initialization_preserves_eval_info_and_frame_tail)
{
  FrameAllocator owner;
  ObExprFrameInfo info(owner);
  ASSERT_EQ(OB_SUCCESS, info.dynamic_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, info.dynamic_frame_.push_back(ObFrameInfo(3, 0, 72, 0, 72)));
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.push_back(ObFrameInfo(3, 1, 72, 0, 72)));
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.prepare_allocate(6));
  SqlTypeMeta meta; meta.set_int();
  SqlType type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 0), type));
  for (int64_t i = 0; i < 6; ++i) {
    ObExpr &expr = info.rt_exprs_.at(i);
    expr.type_ = T_INT; expr.frame_idx_ = i / 3;
    expr.datum_off_ = (i % 3) * NATIVE_SLOT_BYTES;
    expr.eval_info_off_ = expr.datum_off_ + NATIVE_EVAL_OFFSET;
    ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(type, owner));
  }
  ObSEArray<char *, 1> no_params;
  uint64_t count = 0;
  char **frames = nullptr;
  ASSERT_EQ(OB_SUCCESS, info.alloc_frame(owner, no_params, count, frames));
  ASSERT_EQ(2U, count);
  for (int64_t i = 0; i < 3; ++i) {
    const Datum &datum = *reinterpret_cast<Datum *>(frames[0] + NATIVE_SLOT_BYTES * i);
    const ObEvalInfo &state = *reinterpret_cast<ObEvalInfo *>(frames[0] + NATIVE_SLOT_BYTES * i + NATIVE_EVAL_OFFSET);
    EXPECT_TRUE(datum.is_null()); EXPECT_EQ(0U, datum.length());
    EXPECT_EQ(0, state.flag_); EXPECT_EQ(0, state.cnt_);
  }
  owner.expect_frame_canary(frames[0]); owner.expect_frame_canary(frames[1]);
}

namespace
{
int build_native_point_test_graph(ObPhysicalPlan &physical, ObExecContext &exec,
    ObSQLSessionInfo &session, ObRawExprFactory &factory, ObTableScanSpec &scan,
    ObColumnRefRawExpr *(&columns)[3], ObOpRawExpr *(&equals)[3],
    ObRangeNode &range_node, int64_t (&bounds)[3], int64_t batch_size,
    ObRawExpr *output_pack = nullptr)
{
  int ret = OB_SUCCESS;
  ObRawExprResType bigint;
  bigint.set_int();
  bigint.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType]);
  bigint.set_collation_type(CS_TYPE_BINARY);
  bigint.set_collation_level(CS_LEVEL_NUMERIC);
  ObRawExprUniqueSet roots(false); // public generate owns flatten/IS_MARKED.
  ObSEArray<ColumnItem, 3> range_columns;
  for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
    ObConstRawExpr *parameter = nullptr;
    OZ(factory.create_raw_expr(T_REF_COLUMN, columns[i]));
    OZ(factory.create_raw_expr(T_QUESTIONMARK, parameter));
    if (OB_SUCC(ret)) {
      columns[i]->set_ref_id(1001, 16 + i);
      columns[i]->set_result_type(bigint);
      OZ(columns[i]->add_flag(IS_COLUMN));
      OZ(columns[i]->add_flag(CNT_COLUMN));
      OZ(parameter->set_questionmark_index(i));
      parameter->set_result_type(bigint);
      OZ(parameter->add_flag(IS_CONST));
      OZ(parameter->add_flag(CNT_CONST));
      OZ(factory.create_raw_expr(T_OP_EQ, equals[i]));
      OZ(equals[i]->set_param_exprs(columns[i], parameter));
      OZ(equals[i]->formalize(&session));
      OZ(roots.append(equals[i]));
      ColumnItem item;
      item.expr_ = columns[i];
      item.set_ref_id(1001, 16 + i);
      OZ(range_columns.push_back(item));
    }
  }
  // If needed, construct the actual output pack here with build_pack_expr:
  // format arg0 = T_INT(0 binary / 1 text), data args1..3 = columns.
  // Its ObField array and raw object must be owned by the fixture.
  if (OB_SUCC(ret) && output_pack != nullptr) { OZ(roots.append(output_pack)); }
  physical.set_plan_type(OB_PHY_PLAN_LOCAL);
  physical.set_stmt_type(stmt::T_SELECT);
  physical.set_px_dop(1);
  physical.set_das_dop(1);
  physical.set_batch_size(batch_size);
  ObStaticEngineExprCG cg(physical.get_allocator(), &session,
      exec.get_sql_ctx()->schema_guard_, 3, 3);
  cg.set_batch_size(batch_size);
  OZ(cg.generate(roots, physical.get_expr_frame_info()));
  auto &das = scan.tsc_ctdef_.scan_ctdef_;
  auto &pd = das.pd_expr_spec_;
  OZ(pd.access_exprs_.init(3));
  OZ(pd.calc_exprs_.init(6));
  OZ(das.result_output_.init(3));
  OZ(scan.output_.init(output_pack == nullptr ? 3 : 1));
  OZ(scan.filters_.init(3));
  for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
    auto *column = static_cast<ObExpr *>(cg.get_left_value_rt_expr(*columns[i]));
    auto *equal = static_cast<ObExpr *>(cg.get_left_value_rt_expr(*equals[i]));
    OZ(pd.access_exprs_.push_back(column));
    OZ(pd.calc_exprs_.push_back(column));
    OZ(pd.calc_exprs_.push_back(equal));
    OZ(das.result_output_.push_back(column));
    if (output_pack == nullptr) { OZ(scan.output_.push_back(column)); }
    OZ(scan.filters_.push_back(equal));
  }
  if (OB_SUCC(ret) && output_pack != nullptr) {
    OZ(scan.output_.push_back(static_cast<ObExpr *>(cg.get_left_value_rt_expr(*output_pack))));
  }
  pd.max_batch_size_ = batch_size;
  scan.ref_table_id_ = das.ref_table_id_ = 1001;
  scan.part_level_ = oceanbase::share::schema::PARTITION_LEVEL_ZERO;
  das.is_get_ = true;
  auto &range = scan.tsc_ctdef_.pre_range_graph_;
  OZ(range.fill_column_metas(range_columns));
  OZ(range.get_range_map().expr_final_infos_.prepare_allocate(3));
  for (int64_t i = 0; OB_SUCC(ret) && i < 3; ++i) {
    auto &value = range.get_range_map().expr_final_infos_.at(i);
    value.is_param_ = true;
    value.param_idx_ = i;
    bounds[i] = i;
  }
  range_node.column_cnt_ = 3;
  range_node.start_keys_ = range_node.end_keys_ = bounds;
  range_node.min_offset_ = 0; range_node.max_offset_ = 2;
  range_node.include_start_ = range_node.include_end_ = true;
  range.set_range_head(&range_node); range.set_node_count(1);
  range.set_is_precise_get(true); range.set_is_standard_range(true);
  range.set_is_equal_range(true); range.set_is_get(true);
  physical.set_root_op_spec(&scan);

  // Check native_i64_point_layout_ != nullptr; then allocate through:
  // physical.get_expr_frame_info().pre_alloc_exec_memory(exec).
  // Rebind different actual full Type before another pre_alloc call to test
  // all-generic fallback. Keep the original plan offsets immutable throughout.
  return ret;
}

class NativePointCoreFixture
{
public:
  NativePointCoreFixture() : owner_(), session_(), sql_(), exec_(owner_), physical_(),
      scan_(physical_.get_allocator(), PHY_TABLE_SCAN), factory_(physical_.get_allocator()),
      range_node_(physical_.get_allocator()), bounds_{}, columns_{}, equals_{} {}
  ~NativePointCoreFixture()
  { ObSQLSessionInfo::ExecCtxSessionRegister unregister(session_, nullptr); }

  void prepare(int64_t batch = 4, bool finalize = true)
  {
    ASSERT_EQ(OB_SUCCESS, session_.test_init(1, 1, nullptr));
    ASSERT_EQ(OB_SUCCESS, session_.load_default_sys_variable(false, false));
    ASSERT_NO_FATAL_FAILURE(setup_context(exec_, sql_));
    exec_.set_my_session(&session_);
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session_, &exec_);
    std::vector<int64_t> input(3);
    ASSERT_NO_FATAL_FAILURE(load_parameters(*exec_.get_physical_plan_ctx(), 3, input));
    ASSERT_EQ(OB_SUCCESS, exec_.get_physical_plan_ctx()->init_datum_param_store());

    oceanbase::share::schema::ObTableSchema schema;
    schema.set_database_id(1000); schema.set_table_id(1001);
    schema.set_table_type(oceanbase::share::schema::USER_TABLE);
    schema.set_schema_version(1);
    schema.set_index_type(oceanbase::share::schema::INDEX_TYPE_IS_NOT);
    schema.set_max_used_column_id(18);
    schema.set_rowkey_column_num(3);
    schema.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    ASSERT_EQ(OB_SUCCESS, schema.set_table_name("native_i64_core"));
    auto &das = scan_.tsc_ctdef_.scan_ctdef_;
    ASSERT_EQ(OB_SUCCESS, das.access_column_ids_.init(3));
    for (int64_t i = 0; i < 3; ++i) {
      oceanbase::share::schema::ObColumnSchemaV2 column;
      column.set_table_id(1001); column.set_column_id(16 + i);
      const char *name = i == 0 ? "k1" : i == 1 ? "k2" : "k3";
      ASSERT_EQ(OB_SUCCESS, column.set_column_name(name));
      column.set_data_type(ObIntType); column.set_data_length(8);
      column.set_accuracy(ObAccuracy(20, 0));
      column.set_rowkey_position(i + 1); column.set_nullable(false);
      column.set_collation_type(CS_TYPE_BINARY);
      ASSERT_EQ(OB_SUCCESS, schema.add_column(column));
      ASSERT_EQ(OB_SUCCESS, das.access_column_ids_.push_back(16 + i));
    }
    ASSERT_EQ(OB_SUCCESS, das.table_param_.convert(schema, das.access_column_ids_,
                                                  das.pd_expr_spec_.pd_storage_flag_));
    ASSERT_EQ(3, scan_.get_rowkey_cnt());
    ASSERT_EQ(OB_SUCCESS, build_native_point_test_graph(physical_, exec_, session_, factory_,
        scan_, columns_, equals_, range_node_, bounds_, batch));
    scan_.max_batch_size_ = batch;
    scan_.plan_ = &physical_;
    if (finalize) {
      ASSERT_EQ(OB_SUCCESS, ObStaticEngineExprCG::finalize_native_i64_point(physical_, exec_));
      ASSERT_NE(nullptr, info().native_i64_point_layout_);
    }
  }

  ObExprFrameInfo &info() { return physical_.get_expr_frame_info(); }
  ObExpr &lane(int64_t index)
  { return info().rt_exprs_.at(info().native_i64_point_layout_->expr_indexes[index]); }
  ObPhysicalPlanCtx &parameters() { return *exec_.get_physical_plan_ctx(); }
  void allocate(ObEvalCtx &eval)
  {
    ASSERT_EQ(OB_SUCCESS, info().pre_alloc_exec_memory(exec_));
    eval.frames_ = exec_.get_frames();
    eval.max_batch_size_ = physical_.get_batch_size();
    eval.reuse(physical_.get_batch_size() > 0 ? physical_.get_batch_size() : 1);
  }
  void seed(ObEvalCtx &eval, int64_t row, int64_t column, int64_t value, bool null = false)
  {
    NativeI64Lane target;
    ASSERT_EQ(OB_SUCCESS, lane(column).get_native_i64_lane(eval, target));
    ASSERT_LT(row, target.capacity);
    target.values[row] = value;
    target.states[row] = static_cast<uint8_t>(null ? Datum::State::Null : Datum::State::Present);
    lane(column).get_eval_info(eval).evaluated_ = true;
  }
  void bind(int64_t index, SqlValueType kind, int64_t value, bool null = false)
  {
    SqlTypeMeta meta; meta.set_type(kind); meta.set_collation_type(CS_TYPE_BINARY);
    SqlType type;
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy::DDL_DEFAULT_ACCURACY[kind], type));
    Datum datum;
    if (kind == ObDoubleType) { datum.set_double(static_cast<double>(value)); }
    else { datum.set_int(value); }
    if (null) { datum.set_null(); }
    DatumParameterMetadata attrs = *parameters().get_parameter_metadata(index);
    attrs.type_meta_ = meta; attrs.param_meta_ = meta;
    ASSERT_EQ(OB_SUCCESS, parameters().set_runtime_parameter(index, datum, type, attrs));
  }
  FrameAllocator owner_;
  ObSQLSessionInfo session_;
  ObSqlCtx sql_;
  ObExecContext exec_;
  ObPhysicalPlan physical_;
  ObTableScanSpec scan_;
  ObRawExprFactory factory_;
  ObRangeNode range_node_;
  int64_t bounds_[3];
  ObColumnRefRawExpr *columns_[3];
  ObOpRawExpr *equals_[3];
};

class NativeI64ExprFrames : public ::testing::Test
{
protected:
  static void SetUpTestCase()
  {
    ASSERT_EQ(OB_SUCCESS, oceanbase::share::ObSysVariables::init_default_values());
    ObExprOperatorFactory::register_expr_operators();
  }
};

class CoreBatchRows final : public ObOperator
{
public:
  CoreBatchRows(ObExecContext &exec, const ObOpSpec &spec)
      : ObOperator(exec, spec, nullptr), calls_(0), rows_(), skip_word_(0)
  {
    rows_.skip_ = reinterpret_cast<ObBitVector *>(&skip_word_);
    rows_.skip_->reset(4); rows_.skip_->set(1);
  }
  int inner_get_next_row() override { return OB_NOT_SUPPORTED; }
  int get_next_batch(int64_t, const ObBatchRows *&output) override
  {
    rows_.size_ = calls_++ == 0 ? 3 : 0;
    rows_.end_ = rows_.size_ == 0;
    output = &rows_;
    return OB_SUCCESS;
  }
  void destroy() override { ObOperator::destroy(); }
  int calls_;
  ObBatchRows rows_;
  uint64_t skip_word_;
};
} // namespace

TEST_F(NativeI64ExprFrames, public_cg_selects_one_native_page_and_evaluates_i64_with_separate_null_state)
{
  for (int64_t batch : {int64_t(0), int64_t(4)}) {
    NativePointCoreFixture fixture;
    ASSERT_NO_FATAL_FAILURE(fixture.prepare(batch));
    ObEvalCtx eval(fixture.exec_);
    ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
    const auto &layout = *fixture.info().native_i64_point_layout_;
    ASSERT_EQ(1U, layout.frame_count);
    const int64_t capacity = batch == 0 ? 1 : batch;
    const int64_t expected_bytes = batch == 0 ? 88 : 368;
    EXPECT_EQ(expected_bytes, layout.frames[0].compact_size);
    EXPECT_EQ(expected_bytes, fixture.owner_.allocation_size(eval.frames_[layout.frames[0].index]));
    for (int64_t i = 0; i < 6; ++i) {
      const ObExpr &expr = fixture.lane(i);
      NativeI64Lane lane;
      ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_lane(eval, lane));
      EXPECT_EQ(capacity, lane.capacity);
      EXPECT_EQ(0U, reinterpret_cast<uintptr_t>(lane.values) % alignof(int64_t));
      if (i < 3) { EXPECT_EQ(ObIntType, expr.get_sql_type().kind()); }
      else { EXPECT_EQ(ObInt32Type, expr.get_sql_type().kind()); }
      EXPECT_EQ(sizeof(int64_t), reinterpret_cast<char *>(lane.values + 1)
          - reinterpret_cast<char *>(lane.values));
      for (int64_t row = 0; row < capacity; ++row) {
        EXPECT_EQ(static_cast<uint8_t>(Datum::State::Null), lane.states[row]);
      }
    }
    for (int64_t c = 0; c < 3; ++c) {
      for (int64_t row = 0; row < capacity; ++row) {
        ASSERT_NO_FATAL_FAILURE(fixture.seed(eval, row, c, input_value(c),
                                             row == 2 && c == 0));
      }
    }
    uint64_t skip_word = 0;
    auto &skip = *reinterpret_cast<ObBitVector *>(&skip_word);
    skip.reset(capacity);
    if (batch > 0) { skip.set(1); }
    for (int64_t c = 0; c < 3; ++c) {
      ASSERT_EQ(OB_SUCCESS, fixture.lane(3 + c).eval_batch(eval, skip, capacity));
      Datum result;
      ASSERT_EQ(OB_SUCCESS, fixture.lane(3 + c).read_value(eval, 0, result));
      ASSERT_FALSE(result.is_null()); EXPECT_EQ(1, result.get_int());
      if (batch > 0) {
        ASSERT_EQ(OB_SUCCESS, fixture.lane(3 + c).read_value(eval, 1, result));
        EXPECT_TRUE(result.is_null()); // skipped lane was never published.
        ASSERT_EQ(OB_SUCCESS, fixture.lane(3 + c).read_value(eval, 2, result));
        if (c == 0) { EXPECT_TRUE(result.is_null()); }
        else { EXPECT_EQ(1, result.get_int()); }
      }
      EXPECT_EQ(nullptr, fixture.parameters().locate_materialized_parameter(c));
    }
    bool filtered = true;
    ObEvalCtx::BatchInfoScopeGuard row_scope(eval);
    row_scope.set_batch_idx(0);
    ASSERT_EQ(OB_SUCCESS, ObOperator::filter_row(eval, fixture.scan_.filters_, filtered));
    EXPECT_FALSE(filtered);
    char text[1024] = {};
    EXPECT_GT(ObToStringExpr(eval, fixture.lane(0)).to_string(text, sizeof(text)), 0);
    EXPECT_EQ(nullptr, fixture.parameters().locate_materialized_parameter(0));
    fixture.owner_.expect_frame_canary(eval.frames_[layout.frames[0].index]);
  }
}

TEST_F(NativeI64ExprFrames, rebind_pl_fallback_bad_header_and_failed_allocation_preserve_execution_boundary)
{
  NativePointCoreFixture fixture;
  ASSERT_NO_FATAL_FAILURE(fixture.prepare());
  ObEvalCtx eval(fixture.exec_);
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  const auto *descriptor = fixture.info().native_i64_point_layout_;
  char **previous = fixture.exec_.get_frames();
  const uint64_t previous_count = fixture.exec_.get_frame_cnt();
  fixture.owner_.fail_size_ = descriptor->frames[0].compact_size;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, fixture.info().pre_alloc_exec_memory(fixture.exec_));
  EXPECT_EQ(previous, fixture.exec_.get_frames());
  EXPECT_EQ(previous_count, fixture.exec_.get_frame_cnt());
  fixture.owner_.fail_size_ = 0;
  bool mode = false;
  ASSERT_EQ(OB_SUCCESS, fixture.lane(0).get_native_i64_mode(eval, mode));
  ASSERT_TRUE(mode);
  auto *header = reinterpret_cast<NativeI64FrameHeader *>(eval.frames_[fixture.lane(0).frame_idx_]);
  const uint32_t old_magic = header->magic;
  header->magic ^= 1;
  mode = true;
  EXPECT_EQ(OB_ERR_UNEXPECTED, fixture.lane(0).get_native_i64_mode(eval, mode));
  EXPECT_TRUE(mode);
  Datum sentinel; sentinel.set_int(991);
  EXPECT_EQ(OB_ERR_UNEXPECTED, fixture.lane(0).read_value(eval, 0, sentinel));
  EXPECT_EQ(991, sentinel.get_int());
  header->magic = old_magic;

  ASSERT_NO_FATAL_FAILURE(fixture.bind(0, ObIntType, 0, true));
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  ASSERT_EQ(OB_SUCCESS, fixture.lane(0).get_native_i64_mode(eval, mode));
  EXPECT_TRUE(mode); // typed NULL keeps the actual signed I64 layout.
  ASSERT_NO_FATAL_FAILURE(fixture.bind(0, ObDoubleType, 0, true));
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  for (int64_t i = 0; i < 6; ++i) {
    mode = true;
    ASSERT_EQ(OB_SUCCESS, fixture.lane(i).get_native_i64_mode(eval, mode));
    EXPECT_FALSE(mode); // one actual unsupported Type chooses the whole generic graph.
    fixture.lane(i).locate_expr_datum(eval).set_int(30 + i);
  }
  EXPECT_EQ(descriptor, fixture.info().native_i64_point_layout_);
  EXPECT_EQ(descriptor->frames[0].generic_size,
      fixture.owner_.allocation_size(eval.frames_[descriptor->frames[0].index]));
  ASSERT_NO_FATAL_FAILURE(fixture.bind(0, ObIntType, INT64_MIN));
  fixture.sql_.is_from_pl_ = true; // includes the real PS cursor entry.
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  ASSERT_EQ(OB_SUCCESS, fixture.lane(0).get_native_i64_mode(eval, mode));
  EXPECT_FALSE(mode);
  fixture.sql_.is_from_pl_ = false;
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  ASSERT_EQ(OB_SUCCESS, fixture.lane(0).get_native_i64_mode(eval, mode));
  EXPECT_TRUE(mode);
}

TEST_F(NativeI64ExprFrames, two_plan_clones_rebind_descriptors_and_generic_fallback_keeps_wire_abi)
{
  NativePointCoreFixture fixture;
  ASSERT_NO_FATAL_FAILURE(fixture.prepare(0));
  FrameAllocator first_owner, second_owner, receiver;
  ObExprFrameInfo first(first_owner), second(second_owner);
  ASSERT_EQ(OB_SUCCESS, first.assign(fixture.info(), first_owner));
  ASSERT_EQ(OB_SUCCESS, second.assign(first, second_owner));
  const auto *source = fixture.info().native_i64_point_layout_;
  const auto *middle = first.native_i64_point_layout_;
  const auto *last = second.native_i64_point_layout_;
  ASSERT_NE(nullptr, last);
  EXPECT_NE(source, middle); EXPECT_NE(middle, last);
  for (int64_t i = 0; i < 6; ++i) {
    const ObExpr &expr = second.rt_exprs_.at(last->expr_indexes[i]);
    EXPECT_EQ(&last->lanes[i], expr.native_i64_layout_);
    EXPECT_TRUE(expr.get_sql_type().equals(fixture.lane(i).get_sql_type()));
    EXPECT_NE(expr.sql_type_, fixture.lane(i).sql_type_);
    for (uint32_t a = 0; a < expr.arg_cnt_; ++a) {
      EXPECT_GE(expr.args_[a], &second.rt_exprs_.at(0));
      EXPECT_LT(expr.args_[a], &second.rt_exprs_.at(second.rt_exprs_.count() - 1) + 1);
    }
  }
  MEMSET(const_cast<NativeI64PointLayout *>(source), 0xa5, sizeof(*source));
  MEMSET(const_cast<NativeI64PointLayout *>(middle), 0xa6, sizeof(*middle));
  fixture.sql_.is_from_pl_ = true;
  ASSERT_EQ(OB_SUCCESS, second.pre_alloc_exec_memory(fixture.exec_));
  ObEvalCtx eval(fixture.exec_);
  for (int64_t i = 0; i < 6; ++i) {
    ObExpr &expr = second.rt_exprs_.at(last->expr_indexes[i]);
    bool native = true;
    ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_mode(eval, native)); EXPECT_FALSE(native);
    expr.locate_expr_datum(eval).set_int(101 + i);
  }
  int64_t size = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::group_size(false, second.datum_frame_, second,
      eval.frames_, fixture.exec_.get_frame_cnt(), false, size));
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::serialize_group(wire.data(), wire.size(), pos, false,
      second.datum_frame_, second, eval.frames_, fixture.exec_.get_frame_cnt(), false));
  EXPECT_EQ(size, pos);
  std::vector<char *> restored(fixture.exec_.get_frame_cnt(), nullptr);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::deserialize_group(wire.data(), wire.size(), pos,
      receiver, false, second.datum_frame_, second, nullptr, restored.data(), restored.size(), false));
  ObEvalCtx decoded(fixture.exec_); decoded.frames_ = restored.data();
  for (int64_t i = 0; i < 6; ++i) {
    ObExpr &expr = second.rt_exprs_.at(last->expr_indexes[i]);
    bool native = true; Datum value;
    ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_mode(decoded, native)); EXPECT_FALSE(native);
    ASSERT_EQ(OB_SUCCESS, expr.read_value(decoded, 0, value)); EXPECT_EQ(101 + i, value.get_int());
  }
}

TEST_F(NativeI64ExprFrames, batch_row_adapter_and_retaining_holder_use_checked_value_state_paths)
{
  NativePointCoreFixture fixture;
  ASSERT_NO_FATAL_FAILURE(fixture.prepare());
  ObEvalCtx eval(fixture.exec_);
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  for (int64_t column = 0; column < 3; ++column) {
    ASSERT_NO_FATAL_FAILURE(fixture.seed(eval, 0, column, input_value(column)));
    ASSERT_NO_FATAL_FAILURE(fixture.seed(eval, 1, column, 900 + column));
    ASSERT_NO_FATAL_FAILURE(fixture.seed(eval, 2, column, 800 + column, column == 1));
  }
  CoreBatchRows producer(fixture.exec_, fixture.scan_);
  // Enter the public operator method: it owns the batch adapter/Holder and
  // performs the final projected-output evaluation after selecting a row.
  ASSERT_EQ(OB_SUCCESS, producer.get_next_row());
  ASSERT_EQ(OB_SUCCESS, producer.get_next_row()); // row one is skipped.
  for (int64_t column = 0; column < 3; ++column) {
    Datum value;
    ASSERT_EQ(OB_SUCCESS, fixture.lane(column).read_value(eval, 0, value));
    if (column == 1) { EXPECT_TRUE(value.is_null()); }
    else { EXPECT_EQ(800 + column, value.get_int()); }
  }
  EXPECT_EQ(OB_ITER_END, producer.get_next_row());
  for (int64_t column = 0; column < 3; ++column) {
    Datum value;
    ASSERT_EQ(OB_SUCCESS, fixture.lane(column).read_value(eval, 0, value));
    ASSERT_FALSE(value.is_null()); EXPECT_EQ(input_value(column), value.get_int());
  }
  // A late corrupt candidate must be found before restoring an earlier
  // ordinary parameter or generic Expr in the retaining group.
  alignas(8) char ordinary_frame[24] = {};
  new (ordinary_frame) Datum();
  new (ordinary_frame + 16) ObEvalInfo{};
  ObExpr ordinary;
  ordinary.type_ = T_INT;
  ordinary.frame_idx_ = fixture.exec_.get_frame_cnt();
  ordinary.datum_off_ = 0; ordinary.eval_info_off_ = 16;
  ASSERT_EQ(OB_SUCCESS, ordinary.bind_sql_type(fixture.lane(0).get_sql_type(), fixture.owner_));
  std::vector<char *> directories(eval.frames_, eval.frames_ + fixture.exec_.get_frame_cnt());
  directories.push_back(ordinary_frame);
  eval.frames_ = directories.data();
  ordinary.locate_expr_datum(eval).set_int(71);
  ObSEArray<ObExpr *, 4> mixed;
  ObExpr *parameter = fixture.lane(3).args_[1];
  ASSERT_TRUE(parameter->native_parameter_alias_);
  ASSERT_EQ(OB_SUCCESS, mixed.push_back(&ordinary));
  ASSERT_EQ(OB_SUCCESS, mixed.push_back(parameter));
  ASSERT_EQ(OB_SUCCESS, mixed.push_back(&fixture.lane(0)));
  ObBatchResultHolder holder;
  ASSERT_EQ(OB_SUCCESS, holder.init(mixed, eval));
  ASSERT_EQ(OB_SUCCESS, holder.save(1));
  ordinary.locate_expr_datum(eval).set_int(72);
  ASSERT_NO_FATAL_FAILURE(fixture.bind(parameter->extra_, ObIntType, 555));
  NativeI64Lane target;
  ASSERT_EQ(OB_SUCCESS, fixture.lane(0).get_native_i64_lane(eval, target));
  target.values[0] = 777;
  auto *header = reinterpret_cast<NativeI64FrameHeader *>(eval.frames_[fixture.lane(0).frame_idx_]);
  const uint32_t magic = header->magic; header->magic = 0;
  EXPECT_EQ(OB_ERR_UNEXPECTED, holder.restore());
  int64_t current = 0;
  ASSERT_TRUE(fixture.parameters().read_native_bigint_parameter(parameter->extra_, current));
  EXPECT_EQ(555, current); EXPECT_EQ(777, target.values[0]);
  EXPECT_EQ(72, ordinary.locate_expr_datum(eval).get_int());
  header->magic = magic;
  ASSERT_EQ(OB_SUCCESS, holder.restore());
  EXPECT_EQ(71, ordinary.locate_expr_datum(eval).get_int());
  EXPECT_EQ(input_value(0), target.values[0]);
  EXPECT_EQ(nullptr, fixture.parameters().locate_materialized_parameter(parameter->extra_));
}

TEST_F(NativeI64ExprFrames, whole_consumer_graph_declines_before_offsets_or_frames_change)
{
  for (int64_t unsupported = 0; unsupported < 5; ++unsupported) {
    NativePointCoreFixture fixture;
    ASSERT_NO_FATAL_FAILURE(fixture.prepare(4, false));
    const int64_t old_frames = fixture.info().datum_frame_.count();
    std::vector<std::pair<uint32_t, uint32_t>> offsets;
    for (int64_t i = 0; i < fixture.info().rt_exprs_.count(); ++i) {
      const ObExpr &expr = fixture.info().rt_exprs_.at(i);
      offsets.emplace_back(expr.frame_idx_, expr.datum_off_);
    }
    if (unsupported == 0) { fixture.scan_.use_dist_das_ = true; }
    if (unsupported == 1) { fixture.physical_.set_px_dop(2); }
    if (unsupported == 2) { fixture.sql_.is_from_pl_ = true; }
    if (unsupported == 3) { fixture.scan_.need_check_output_datum_ = true; }
    if (unsupported == 4) { fixture.scan_.limit_ = fixture.scan_.output_.at(0); }
    ASSERT_EQ(OB_SUCCESS, ObStaticEngineExprCG::finalize_native_i64_point(
        fixture.physical_, fixture.exec_));
    EXPECT_EQ(nullptr, fixture.info().native_i64_point_layout_);
    EXPECT_EQ(old_frames, fixture.info().datum_frame_.count());
    for (int64_t i = 0; i < fixture.info().rt_exprs_.count(); ++i) {
      EXPECT_EQ(offsets[i].first, fixture.info().rt_exprs_.at(i).frame_idx_);
      EXPECT_EQ(offsets[i].second, fixture.info().rt_exprs_.at(i).datum_off_);
      EXPECT_EQ(nullptr, fixture.info().rt_exprs_.at(i).native_i64_layout_);
    }
  }
}

TEST_F(NativeI64ExprFrames, real_cg_frame_capacity_is_independent_of_consumer_batch_size)
{
  // The live system-query failure has a batch Expr with one physical slot,
  // consumed by a scalar parent whose EvalCtx batch size is zero.
  for (const int64_t capacity : {1, 4}) {
    NativePointCoreFixture fixture;
    ASSERT_NO_FATAL_FAILURE(fixture.prepare(capacity, false));
    fixture.sql_.is_cursor_ = true; // a real finalizer decline, not a cleared descriptor
    ASSERT_EQ(OB_SUCCESS, ObStaticEngineExprCG::finalize_native_i64_point(
        fixture.physical_, fixture.exec_));
    ASSERT_EQ(nullptr, fixture.info().native_i64_point_layout_);
    ObExpr &expr = *fixture.scan_.tsc_ctdef_.scan_ctdef_.pd_expr_spec_.access_exprs_.at(0);
    ASSERT_TRUE(expr.is_batch_result());
    ASSERT_EQ(nullptr, expr.native_i64_layout_);
    ASSERT_EQ(sizeof(Datum) * capacity, expr.pvt_skip_off_ - expr.datum_off_);
    ObEvalCtx eval(fixture.exec_);
    ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
    expr.locate_expr_datum(eval, 0).set_int(701);
    if (capacity > 1) { expr.locate_expr_datum(eval, capacity - 1).set_int(709); }
    for (const int64_t consumer_batch : {0, 1}) {
      eval.set_max_batch_size(consumer_batch);
      Datum out; out.set_int(991);
      ASSERT_EQ(OB_SUCCESS, expr.read_value(eval, 0, out)); EXPECT_EQ(701, out.get_int());
      ASSERT_EQ(OB_SUCCESS, expr.read_value(eval, capacity - 1, out));
      EXPECT_EQ(capacity == 1 ? 701 : 709, out.get_int());
      out.set_int(991);
      EXPECT_EQ(OB_INVALID_ARGUMENT, expr.read_value(eval, capacity, out));
      EXPECT_EQ(991, out.get_int());
      EXPECT_EQ(OB_INVALID_ARGUMENT, expr.read_value(eval, -1, out));
      EXPECT_EQ(991, out.get_int());
      ObExpr malformed = expr;
      for (const uint32_t extent : {uint32_t(0), uint32_t(1)}) {
        malformed.pvt_skip_off_ = malformed.datum_off_ + extent;
        EXPECT_EQ(OB_INVALID_ARGUMENT, malformed.read_value(eval, 0, out));
        EXPECT_EQ(991, out.get_int());
      }
    }
    expr.locate_expr_datum(eval, capacity - 1).set_null();
    Datum null_value; null_value.set_int(991);
    ASSERT_EQ(OB_SUCCESS, expr.read_value(eval, capacity - 1, null_value));
    EXPECT_TRUE(null_value.is_null());
  }

  NativePointCoreFixture fixture;
  ASSERT_NO_FATAL_FAILURE(fixture.prepare());
  ObEvalCtx eval(fixture.exec_);
  ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
  ObExpr &expr = fixture.lane(0);
  for (const bool fallback : {false, true}) {
    if (fallback) {
      ASSERT_NO_FATAL_FAILURE(fixture.bind(0, ObInt32Type, 0));
      ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
      expr.locate_expr_datum(eval, 0).set_int(811);
      expr.locate_expr_datum(eval, 3).set_int(819);
    } else {
      ASSERT_NO_FATAL_FAILURE(fixture.seed(eval, 0, 0, 811));
      ASSERT_NO_FATAL_FAILURE(fixture.seed(eval, 3, 0, 819));
    }
    ASSERT_NE(nullptr, expr.native_i64_layout_);
    bool native = fallback;
    ASSERT_EQ(OB_SUCCESS, expr.get_native_i64_mode(eval, native)); EXPECT_EQ(!fallback, native);
    for (const int64_t consumer_batch : {0, 1}) {
      eval.set_max_batch_size(consumer_batch);
      Datum out; out.set_int(991);
      ASSERT_EQ(OB_SUCCESS, expr.read_value(eval, 0, out)); EXPECT_EQ(811, out.get_int());
      ASSERT_EQ(OB_SUCCESS, expr.read_value(eval, 3, out)); EXPECT_EQ(819, out.get_int());
      out.set_int(991);
      EXPECT_EQ(OB_INVALID_ARGUMENT, expr.read_value(eval, 4, out)); EXPECT_EQ(991, out.get_int());
      EXPECT_EQ(OB_INVALID_ARGUMENT, expr.read_value(eval, -1, out)); EXPECT_EQ(991, out.get_int());
      auto *header = reinterpret_cast<NativeI64FrameHeader *>(eval.frames_[expr.frame_idx_]);
      const uint32_t magic = header->magic;
      header->magic ^= 1;
      EXPECT_EQ(OB_ERR_UNEXPECTED, expr.read_value(eval, 0, out)); EXPECT_EQ(991, out.get_int());
      header->magic = magic;
    }
  }
}

TEST_F(NativeI64ExprFrames, malformed_cg_lane_regions_reject_public_allocation_and_boundary_validation)
{
  class RejectingAllocator final : public ObIAllocator
  {
  public:
    RejectingAllocator() : calls_(0) {}
    void *alloc(int64_t) override { ++calls_; return nullptr; }
    void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
    void free(void *) override {}
    int64_t calls_;
  };

  for (const bool generic_overlap : {false, true}) {
    SCOPED_TRACE(generic_overlap ? "generic region overlap" : "compact region overlap");
    NativePointCoreFixture fixture;
    ASSERT_NO_FATAL_FAILURE(fixture.prepare(4));
    ObEvalCtx eval(fixture.exec_);
    ASSERT_NO_FATAL_FAILURE(fixture.allocate(eval));
    char **const previous = fixture.exec_.get_frames();
    const uint64_t previous_count = fixture.exec_.get_frame_cnt();
    auto &layout = *const_cast<NativeI64PointLayout *>(fixture.info().native_i64_point_layout_);
    ASSERT_EQ(1U, layout.frame_count);
    ASSERT_EQ(fixture.lane(0).frame_idx_, fixture.lane(1).frame_idx_);
    const uint32_t old_datum = fixture.lane(1).datum_off_;
    const uint32_t old_skip = fixture.lane(1).pvt_skip_off_;
    const uint32_t old_value = layout.lanes[1].value_off;
    const auto corrupt = [&]() {
      if (generic_overlap) {
        // Retain the valid Datum-array extent but overlap another live lane.
        fixture.lane(1).datum_off_ = fixture.lane(0).datum_off_;
        fixture.lane(1).pvt_skip_off_ = fixture.lane(0).pvt_skip_off_;
      } else {
        layout.lanes[1].value_off = layout.lanes[0].value_off;
      }
    };
    const auto restore = [&]() {
      fixture.lane(1).datum_off_ = old_datum;
      fixture.lane(1).pvt_skip_off_ = old_skip;
      layout.lanes[1].value_off = old_value;
    };
    RejectingAllocator blocked;

    corrupt();
    for (const bool compact : {false, true}) {
      char **frames = previous;
      uint64_t count = previous_count;
      EXPECT_EQ(OB_ERR_UNEXPECTED, fixture.info().alloc_frame(blocked,
          fixture.parameters().get_param_frame_ptrs(), count, frames, compact));
      EXPECT_EQ(0, blocked.calls_);
      EXPECT_EQ(previous, frames);
      EXPECT_EQ(previous_count, count);
      EXPECT_EQ(previous, fixture.exec_.get_frames());
      EXPECT_EQ(previous_count, fixture.exec_.get_frame_cnt());
    }
    restore();

    corrupt();
    EXPECT_EQ(OB_ERR_UNEXPECTED, fixture.info().validate_native_i64_layout());
    EXPECT_EQ(0, blocked.calls_);
    EXPECT_EQ(previous, fixture.exec_.get_frames());
    EXPECT_EQ(previous_count, fixture.exec_.get_frame_cnt());
    restore();
    EXPECT_EQ(OB_SUCCESS, fixture.info().validate_native_i64_layout());
    fixture.owner_.expect_frame_canary(previous[layout.frames[0].index]);
  }
}

TEST_F(NativeI64ExprFrames, publication_validates_static_frame_directory_and_parameter_aliases)
{
  NativePointCoreFixture fixture;
  ASSERT_NO_FATAL_FAILURE(fixture.prepare(4));
  ObExprFrameInfo &info = fixture.info();
  ASSERT_TRUE(info.native_i64_layout_validated_);
  ASSERT_GT(info.param_frame_.count(), 0);

  const uint32_t old_index = info.param_frame_.at(0).frame_idx_;
  info.param_frame_.at(0).frame_idx_ = info.native_i64_point_layout_->frames[0].index;
  EXPECT_EQ(OB_INVALID_ARGUMENT, info.validate_and_mark_native_i64_layout());
  EXPECT_FALSE(info.native_i64_layout_validated_);
  info.param_frame_.at(0).frame_idx_ = old_index;
  ASSERT_EQ(OB_SUCCESS, info.validate_and_mark_native_i64_layout());
  ASSERT_TRUE(info.native_i64_layout_validated_);

  ObExpr *parameter = fixture.lane(3).args_[1];
  ASSERT_NE(nullptr, parameter);
  ASSERT_TRUE(parameter->native_parameter_alias_);
  parameter->native_parameter_alias_ = false;
  EXPECT_EQ(OB_INVALID_ARGUMENT, info.validate_and_mark_native_i64_layout());
  EXPECT_FALSE(info.native_i64_layout_validated_);
  parameter->native_parameter_alias_ = true;
  ASSERT_EQ(OB_SUCCESS, info.validate_and_mark_native_i64_layout());
  EXPECT_TRUE(info.native_i64_layout_validated_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
