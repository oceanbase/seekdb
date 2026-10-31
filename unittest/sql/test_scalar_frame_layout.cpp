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
#include "sql/engine/px/ob_px_util.h"
#include "sql/ob_sql_context.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include <gtest/gtest.h>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace oceanbase
{
namespace sql
{
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, ObDatum &);

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
// Deliberately keep the old ABI literal in the oracle, independently of the
// production helper. These tests must fail if the prework changes its bytes.
constexpr int64_t OLD_SLOT_BYTES = 16;
constexpr int64_t OLD_EVAL_OFFSET = 12;
const int64_t OLD_SLOTS_PER_FRAME = MAX_FRAME_SIZE / OLD_SLOT_BYTES;

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
    parameter.datum_.ptr_ = reinterpret_cast<const char *>(&input[i]);
    parameter.datum_.pack_ = 8;
    if (i == null_index) { parameter.datum_.set_null(); }
    ASSERT_EQ(OB_SUCCESS, plan.get_param_store_for_update().push_back(parameter));
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
  EXPECT_EQ(null_index < 0, plan.get_native_bigint_parameter(0) != nullptr);
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
    raw[n].set_value(index);
    raw[n].set_expr_type(T_QUESTIONMARK);
    raw[n].set_rt_expr(&expr[n]);
    expr[n].type_ = T_QUESTIONMARK;
    expr[n].datum_meta_.type_ = ObIntType;
    ASSERT_EQ(OB_SUCCESS, selected.push_back(&raw[n++]));
  }
  // A nonzero constant-frame prefix checks the CG-relative/runtime binding.
  alignas(8) char prefix[2][OLD_SLOT_BYTES] = {};
  ASSERT_EQ(OB_SUCCESS, info.const_frame_.reserve(2));
  ASSERT_EQ(OB_SUCCESS, info.const_frame_ptrs_.reserve(2));
  for (int64_t i = 0; i < 2; ++i) {
    ASSERT_EQ(OB_SUCCESS, info.const_frame_.push_back(ObFrameInfo(1, i, OLD_SLOT_BYTES, 0, 0)));
    ASSERT_EQ(OB_SUCCESS, info.const_frame_ptrs_.push_back(prefix[i]));
  }
  int64_t next_frame = 2;
  ASSERT_EQ(OB_SUCCESS, ScalarFrameLayoutTestAccess::parameters(cg, selected, next_frame, info));
  ASSERT_EQ(plan.get_param_frame_ptrs().count(), info.param_frame_.count());
  ASSERT_EQ(OB_SUCCESS, info.pre_alloc_exec_memory(exec));
  EXPECT_EQ(static_cast<uint64_t>(next_frame), exec.get_frame_cnt());
  ObEvalCtx eval(exec);
  n = 0;
  for (const ExpectedSlot &slot : expected) {
    SCOPED_TRACE(slot.parameter_);
    ObExpr &e = expr[n++];
    ASSERT_EQ(2 + slot.page_, e.frame_idx_);
    ASSERT_EQ(slot.slot_ * OLD_SLOT_BYTES, e.datum_off_);
    ASSERT_EQ(slot.slot_ * OLD_SLOT_BYTES + OLD_EVAL_OFFSET, e.eval_info_off_);
    EXPECT_EQ(0U, e.res_buf_len_);
    const char *frame = plan.get_param_frame_ptrs().at(slot.page_);
    ObDatum &datum = e.locate_expr_datum(eval);
    EXPECT_EQ(frame + slot.slot_ * OLD_SLOT_BYTES, reinterpret_cast<const char *>(&datum));
    EXPECT_EQ(frame + slot.slot_ * OLD_SLOT_BYTES + OLD_EVAL_OFFSET,
              reinterpret_cast<const char *>(&e.get_eval_info(eval)));
    EXPECT_FALSE(e.get_eval_info(eval).evaluated_);
    ObDatum questionmark;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(e, eval, questionmark));
    EXPECT_EQ(slot.parameter_ == null_index, datum.is_null());
    EXPECT_EQ(datum.is_null(), questionmark.is_null());
    if (!datum.is_null()) {
      EXPECT_EQ(input_value(slot.parameter_), datum.get_int());
      EXPECT_EQ(datum.get_int(), questionmark.get_int());
      EXPECT_EQ(datum.ptr_, questionmark.ptr_);
      // Writes to EvalInfo must not overwrite this or an adjacent 12B datum.
      ObEvalInfo &state = e.get_eval_info(eval);
      state.cnt_ = 57; state.notnull_ = true;
      EXPECT_EQ(input_value(slot.parameter_), datum.get_int());
      EXPECT_EQ(8U, datum.len_);
    }
  }
  for (int64_t i = 0; i < plan.get_param_frame_ptrs().count(); ++i) {
    owner.expect_frame_canary(plan.get_param_frame_ptrs().at(i));
    EXPECT_EQ(2 + i, info.param_frame_.at(i).frame_idx_);
    EXPECT_EQ(info.param_frame_.at(i).expr_cnt_ * OLD_SLOT_BYTES,
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
      raw_[i].set_value(value[i]); raw_[i].set_expr_type(kinds[i]);
      raw_[i].set_rt_expr(&info_.rt_exprs_.at(i));
      ASSERT_EQ(OB_SUCCESS, selected.push_back(&raw_[i]));
    }
    ObStaticEngineExprCG cg(plan_, nullptr, nullptr, 0, 0);
    ASSERT_EQ(OB_SUCCESS, ScalarFrameLayoutTestAccess::constants(cg, selected, info_));
    ASSERT_EQ(1, info_.const_frame_.count());
    ASSERT_EQ(1, info_.const_frame_ptrs_.count());
    for (int64_t i = 0; i < 3; ++i) {
      ASSERT_EQ(i * OLD_SLOT_BYTES, info_.rt_exprs_.at(i).datum_off_);
      ASSERT_EQ(i * OLD_SLOT_BYTES + OLD_EVAL_OFFSET, info_.rt_exprs_.at(i).eval_info_off_);
      ObEvalInfo *state = reinterpret_cast<ObEvalInfo *>(source_frame() + i * OLD_SLOT_BYTES + OLD_EVAL_OFFSET);
      state->flag_ = 0x0f; state->cnt_ = 71 + i;
    }
  }
  char *source_frame() { return info_.const_frame_ptrs_.at(0); }
  void poison_source()
  {
    ObDatum *external = reinterpret_cast<ObDatum *>(source_frame() + OLD_SLOT_BYTES);
    MEMSET(const_cast<char *>(external->ptr_), 0xd5, external->len_);
    MEMSET(source_frame(), 0xe6, info_.const_frame_.at(0).frame_size_);
    request_.reuse();
  }
  void expect_values(char *frame)
  {
    const ObDatum &integer = *reinterpret_cast<ObDatum *>(frame);
    const ObDatum &string = *reinterpret_cast<ObDatum *>(frame + OLD_SLOT_BYTES);
    const ObDatum &null = *reinterpret_cast<ObDatum *>(frame + 2 * OLD_SLOT_BYTES);
    ASSERT_FALSE(integer.is_null()); EXPECT_EQ(INT64_MIN, integer.get_int());
    ASSERT_FALSE(string.is_null()); EXPECT_EQ(text_, std::string(string.ptr_, string.len_));
    EXPECT_TRUE(null.is_null()); EXPECT_EQ(nullptr, null.ptr_);
    for (int64_t i = 0; i < 3; ++i) {
      const ObEvalInfo &state = *reinterpret_cast<ObEvalInfo *>(frame + i * OLD_SLOT_BYTES + OLD_EVAL_OFFSET);
      EXPECT_EQ(0x0f, state.flag_); EXPECT_EQ(71 + i, state.cnt_);
    }
  }
  ObArenaAllocator request_;
  ObArenaAllocator plan_;
  ObExprFrameInfo info_;
  ObConstRawExpr raw_[3];
  std::string text_;
};
} // namespace

TEST(ScalarFrameLayout, keeps_old_scalar_abi_and_parameter_page_boundaries)
{
  EXPECT_EQ(12U, sizeof(ObDatum)); EXPECT_EQ(4U, sizeof(ObEvalInfo));
  EXPECT_EQ(OLD_SLOT_BYTES, ScalarFrameLayout::slot_size());
  EXPECT_EQ(OLD_SLOTS_PER_FRAME, static_cast<int64_t>(ObExprFrameInfo::EXPR_CNT_PER_FRAME));
  const int64_t c = OLD_SLOTS_PER_FRAME;
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(0, c + 2, {{0, 0, 0}, {c - 1, 0, c - 1}, {c, 1, 0}, {c + 1, 1, 1}}));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(1, c + 2, {{0, 0, 0}, {1, 1, 0}, {c, 1, c - 1}, {c + 1, 2, 0}}));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c - 1, c + 2, {{c - 2, 0, c - 2}, {c - 1, 1, 0}, {c, 1, 1}, {c + 1, 1, 2}}));
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c, c + 2, {{c - 1, 0, c - 1}, {c, 1, 0}, {c + 1, 1, 1}}));
}

TEST(ScalarFrameLayout, reordered_and_repeated_parameters_cross_both_page_ranges)
{
  const int64_t c = OLD_SLOTS_PER_FRAME;
  ASSERT_NO_FATAL_FAILURE(verify_parameter_layout(c + 1, c + 3,
      {{c + 2, 2, 1}, {c, 1, 0}, {0, 0, 0}, {c + 1, 2, 0}, {c + 2, 2, 1}, {c - 1, 0, c - 1}}));
}

TEST(ScalarFrameLayout, nullable_legacy_clone_uses_same_cg_offsets_after_request_poison)
{
  const int64_t c = OLD_SLOTS_PER_FRAME;
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
  std::vector<int64_t> input(OLD_SLOTS_PER_FRAME + 1);
  ASSERT_NO_FATAL_FAILURE(load_parameters(plan, input.size(), input));
  const char *before = plan.get_param_store().at(0).datum_.ptr_;
  owner.fail_size_ = OLD_SLOT_BYTES;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.init_datum_param_store());
  EXPECT_EQ(0, plan.get_param_frame_ptrs().count());
  EXPECT_EQ(nullptr, plan.get_native_bigint_parameter(0));
  EXPECT_EQ(before, plan.get_param_store().at(0).datum_.ptr_);
  EXPECT_EQ(INT64_MIN, plan.get_param_store().at(0).get_int());
  owner.fail_size_ = 0;
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
  ASSERT_EQ(2, plan.get_param_frame_ptrs().count());
  EXPECT_NE(nullptr, plan.get_native_bigint_parameter(0));
  for (int64_t i = 0; i < plan.get_param_frame_ptrs().count(); ++i) {
    owner.expect_frame_canary(plan.get_param_frame_ptrs().at(i));
  }
}

TEST_F(ScalarConstants, clone_relocates_in_frame_payload_and_copies_external_payload)
{
  ObArenaAllocator clone_owner("ScalarClone");
  ObExprFrameInfo clone(clone_owner);
  ASSERT_EQ(OB_SUCCESS, clone.assign(info_, clone_owner));
  ASSERT_EQ(1, clone.const_frame_ptrs_.count());
  char *destination = clone.const_frame_ptrs_.at(0);
  const ObDatum &integer = *reinterpret_cast<ObDatum *>(destination);
  EXPECT_EQ(destination + info_.rt_exprs_.at(0).res_buf_off_, integer.ptr_);
  EXPECT_NE(reinterpret_cast<ObDatum *>(source_frame() + OLD_SLOT_BYTES)->ptr_,
            reinterpret_cast<ObDatum *>(destination + OLD_SLOT_BYTES)->ptr_);
  poison_source();
  ASSERT_NO_FATAL_FAILURE(expect_values(destination));
}

TEST_F(ScalarConstants, px_scalar_wire_retains_16_byte_headers_and_owns_decoded_payload)
{
  char *source[] = {source_frame()};
  const int64_t size = ObPxTreeSerializer::get_serialize_frame_info_size<false>(info_.const_frame_, source, 1);
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_frame_info<false>(wire.data(), size, pos, info_.const_frame_, source, 1));
  ASSERT_EQ(size, pos);
  // Pin the historical scalar header shape independently of deserialization.
  int64_t header_pos = 0;
  int64_t header_bytes = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), size, header_pos, header_bytes));
  ASSERT_EQ(3 * OLD_SLOT_BYTES, header_bytes);
  EXPECT_EQ(0, MEMCMP(source_frame(), wire.data() + header_pos, header_bytes));
  ObArenaAllocator decoded_owner("ScalarWire");
  char *decoded_array[] = {nullptr};
  char **decoded = decoded_array;
  int64_t frame_count = 1;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_frame_info<false>(wire.data(), size, pos, decoded_owner,
      info_.const_frame_, nullptr, decoded, frame_count));
  ASSERT_EQ(size, pos); ASSERT_NE(nullptr, decoded[0]);
  poison_source(); MEMSET(wire.data(), 0x8a, wire.size());
  ASSERT_NO_FATAL_FAILURE(expect_values(decoded[0]));
}

TEST_F(ScalarConstants, px_scalar_serialization_reports_short_payload_buffer)
{
  char *source[] = {source_frame()};
  const int64_t size = ObPxTreeSerializer::get_serialize_frame_info_size<false>(info_.const_frame_, source, 1);
  std::vector<char> wire(size);
  int64_t pos = 0;
  EXPECT_EQ(OB_SIZE_OVERFLOW, ObPxTreeSerializer::serialize_frame_info<false>(wire.data(), size - 2, pos,
      info_.const_frame_, source, 1));
  // Cursor rollback is not this existing API's contract; source values survive.
  ASSERT_NO_FATAL_FAILURE(expect_values(source_frame()));
}

TEST(ScalarFrameLayout, scalar_dynamic_frame_initialization_preserves_eval_info_and_frame_tail)
{
  FrameAllocator owner;
  ObExprFrameInfo info(owner);
  ASSERT_EQ(OB_SUCCESS, info.dynamic_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, info.dynamic_frame_.push_back(ObFrameInfo(3, 0, 48, 0, 48)));
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.push_back(ObFrameInfo(3, 1, 48, 0, 48)));
  ObSEArray<char *, 1> no_params;
  uint64_t count = 0;
  char **frames = nullptr;
  ASSERT_EQ(OB_SUCCESS, info.alloc_frame(owner, no_params, count, frames));
  ASSERT_EQ(2U, count);
  for (int64_t i = 0; i < 3; ++i) {
    const ObDatum &datum = *reinterpret_cast<ObDatum *>(frames[0] + OLD_SLOT_BYTES * i);
    const ObEvalInfo &state = *reinterpret_cast<ObEvalInfo *>(frames[0] + OLD_SLOT_BYTES * i + OLD_EVAL_OFFSET);
    EXPECT_TRUE(datum.is_null()); EXPECT_EQ(nullptr, datum.ptr_);
    EXPECT_EQ(0, state.flag_); EXPECT_EQ(0, state.cnt_);
  }
  owner.expect_frame_canary(frames[0]); owner.expect_frame_canary(frames[1]);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
