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
#include "sql/engine/expr/native_frame_value_codec.h"
#include "sql/engine/expr/ob_expr_frame_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/px/ob_px_util.h"
#include "sql/engine/px/ob_dfo.h"
#include "sql/engine/px/exchange/ob_px_transmit_op.h"
#include "sql/engine/ob_physical_plan.h"
#include "query/engine/ob_operator.h"
#include <initializer_list>
#include "sql/ob_sql_context.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>


namespace {
oceanbase::common::Datum read_parameter_value(
    const oceanbase::sql::ObPhysicalPlanCtx &plan, int64_t index)
{
  oceanbase::common::Datum value;
  EXPECT_EQ(oceanbase::common::OB_SUCCESS, plan.read_native_parameter(index, value));
  return value;
}

// Only tests of a retaining generic Expr ABI explicitly request a projection.
const oceanbase::common::Datum *materialized_parameter(
    oceanbase::sql::ObPhysicalPlanCtx &plan, int64_t index)
{
  if (!plan.has_native_parameter(index)) { return nullptr; }
  oceanbase::common::Datum *value = nullptr;
  EXPECT_EQ(oceanbase::common::OB_SUCCESS, plan.materialize_parameter_for_expr(index, value));
  return value;
}
} // namespace

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase { namespace sql {
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, Datum &);
} }

namespace
{
template <typename Tag, typename Tag::Type Member>
struct PxPrivateMember { friend typename Tag::Type px_member(Tag) { return Member; } };
struct PxParts2Tag {
  using Type = int (ObPxInitSqcArgs::*)(char *, int64_t, int64_t &) const;
  friend Type px_member(PxParts2Tag);
};
template struct PxPrivateMember<PxParts2Tag, &ObPxInitSqcArgs::serialize_common_parts_2>;

void scalar_type(SqlValueType kind, SqlType &type, ObAccuracy accuracy = ObAccuracy())
{
  SqlTypeMeta meta;
  meta.set_type(kind);
  if (kind == ObDecimalIntType) { meta.set_scale(accuracy.get_scale()); }
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, type));
}

void encode_value(const SqlType &type, const Datum &value, std::vector<char> &wire)
{
  int64_t bytes = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::size(type, value, bytes));
  ASSERT_GT(bytes, 0);
  wire.resize(bytes);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::encode(type, value, wire.data(), wire.size(), pos));
  ASSERT_EQ(bytes, pos);
}

void decode_value(const SqlType &expected, const std::vector<char> &wire,
                  ObIAllocator &owner, Datum &value)
{
  const SqlType *type = nullptr;
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::decode(wire.data(), wire.size(), pos, owner, type, value));
  ASSERT_EQ(static_cast<int64_t>(wire.size()), pos);
  ASSERT_NE(nullptr, type);
  EXPECT_TRUE(expected.equals(*type));
  EXPECT_NE(&expected, type);
}

void expect_address_absent(const std::vector<char> &wire, const void *address)
{
  const auto bits = reinterpret_cast<uintptr_t>(address);
  const char *first = reinterpret_cast<const char *>(&bits);
  EXPECT_EQ(wire.end(), std::search(wire.begin(), wire.end(), first, first + sizeof(bits)));
}

void setup_context(ObExecContext &exec, ObSqlCtx &sql_context)
{
  sql_context.exec_type_ = MpQuery;
  sql_context.is_prepare_protocol_ = true;
  sql_context.is_dynamic_sql_ = true;
  sql_context.stmt_type_ = stmt::T_SELECT;
  exec.set_sql_ctx(&sql_context);
  ASSERT_EQ(OB_SUCCESS, exec.create_physical_plan_ctx());
}

void load_parameters(ObPhysicalPlanCtx &plan, ObIAllocator &owner, int64_t count,
                     int64_t original, bool source)
{
  plan.set_original_param_cnt(original);
  for (int64_t i = 0; i < count; ++i) {
    DatumParameter parameter;
    ASSERT_EQ(OB_SUCCESS, parameter.set_int(source ? (i == 0 ? INT64_MIN : i + 71) : -99, owner));
    parameter.set_accuracy(ObAccuracy(20, 0));
    if (source && i == 1) { parameter.datum_.set_null(); }
    ASSERT_EQ(OB_SUCCESS, plan.append_parameter_input(parameter));
  }
  ASSERT_EQ(OB_SUCCESS, plan.init_datum_param_store());
}

// Two logical page ranges, but only the final parameter has a runtime Expr.
// The first page's PX metadata is intentionally shrunk to one slot. All the
// remaining parameters must be recovered from PlanCtx, not inferred from Expr.
void partial_parameter_info(ObExprFrameInfo &info, ObPhysicalPlanCtx &plan, ObIAllocator &owner)
{
  const int64_t count = plan.get_parameter_count();
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.prepare_allocate(1));
  ObExpr &expr = info.rt_exprs_.at(0);
  expr.type_ = T_QUESTIONMARK;
  expr.native_parameter_alias_ = true;
  expr.datum_meta_.type_ = ObIntType;
  expr.datum_meta_.precision_ = 20;
  expr.datum_meta_.scale_ = 0;
  expr.obj_meta_.set_int();
  expr.extra_ = count - 1;
  int64_t page = 0, slot = 0;
  ScalarFrameLayout::locate_parameter(plan.get_original_param_cnt(), count - 1, page, slot);
  ASSERT_EQ(1, page);
  expr.frame_idx_ = page;
  expr.datum_off_ = ScalarFrameLayout::datum_offset(slot);
  expr.eval_info_off_ = ScalarFrameLayout::eval_info_offset(slot);
  ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(*plan.get_native_parameter_type(count - 1), owner));
  ASSERT_EQ(OB_SUCCESS, info.param_frame_.reserve(2));
  ASSERT_EQ(OB_SUCCESS, info.param_frame_.push_back(ObFrameInfo(1, 0, 24, 0, 24)));
  ASSERT_EQ(OB_SUCCESS, info.param_frame_.push_back(ObFrameInfo(slot + 1, 1, (slot + 1) * 24, 0, 0)));
}

void make_batch_info(ObExprFrameInfo &info, ObIAllocator &owner)
{
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.prepare_allocate(2));
  SqlType type;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, type));
  for (int64_t i = 0; i < 2; ++i) {
    ObExpr &expr = info.rt_exprs_.at(i);
    expr.type_ = T_REF_COLUMN;
    expr.datum_meta_.type_ = ObIntType;
    expr.frame_idx_ = 0;
    expr.batch_result_ = i == 0;
    expr.datum_off_ = i == 0 ? 16 : 80;
    expr.pvt_skip_off_ = i == 0 ? 64 : 96;
    expr.eval_info_off_ = 104 + i * 8;
    expr.eval_flags_off_ = i == 0 ? 120 : 72;
    ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(type, owner));
  }
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.push_back(ObFrameInfo(2, 0, 128, 0, 128)));
}
} // namespace

TEST(NativeFrameValueWire, signed_unsigned_float_bits_and_special_states)
{
  ObArenaAllocator owner("FrameValue");
  SqlType integer, unsigned_type, floating;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, integer));
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObUInt64Type, unsigned_type));
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObDoubleType, floating));
  Datum source, copy;
  std::vector<char> wire;
  source.set_int(INT64_MIN);
  ASSERT_NO_FATAL_FAILURE(encode_value(integer, source, wire));
  ASSERT_NO_FATAL_FAILURE(decode_value(integer, wire, owner, copy));
  EXPECT_EQ(INT64_MIN, copy.get_int()); EXPECT_EQ(0U, copy.length());
  source.set_uint(UINT64_MAX);
  ASSERT_NO_FATAL_FAILURE(encode_value(unsigned_type, source, wire));
  ASSERT_NO_FATAL_FAILURE(decode_value(unsigned_type, wire, owner, copy));
  EXPECT_EQ(UINT64_MAX, copy.get_uint());
  const uint64_t patterns[] = {UINT64_C(0x8000000000000000), UINT64_C(0x7ff800000001cafe)};
  for (const uint64_t pattern : patterns) {
    double number = 0;
    MEMCPY(&number, &pattern, sizeof(number)); source.set_double(number);
    ASSERT_NO_FATAL_FAILURE(encode_value(floating, source, wire));
    ASSERT_NO_FATAL_FAILURE(decode_value(floating, wire, owner, copy));
    const double decoded = copy.get_double();
    uint64_t bits = 0; MEMCPY(&bits, &decoded, sizeof(bits)); EXPECT_EQ(pattern, bits);
  }
  for (int i = 0; i < 4; ++i) {
    if (i == 0) { source.set_null(); } else if (i == 1) { source.set_nop(); }
    else if (i == 2) { source.set_min(); } else { source.set_max(); }
    ASSERT_NO_FATAL_FAILURE(encode_value(integer, source, wire));
    ASSERT_NO_FATAL_FAILURE(decode_value(integer, wire, owner, copy));
    EXPECT_EQ(source.state(), copy.state()); EXPECT_EQ(0U, copy.length());
  }
}

TEST(NativeFrameValueWire, actual_short_decimal_span_and_reference_flags_survive)
{
  ObArenaAllocator owner("FrameDecimal");
  SqlType decimal, json;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObDecimalIntType, decimal, ObAccuracy(30, -2)));
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObJsonType, json));
  const int64_t minus = -17;
  const int32_t short_minus = -17;
  for (uint32_t width : {0U, 4U, 8U}) {
    Datum source, copy;
    const void *coefficient = width == 0 ? nullptr
        : width == 4 ? static_cast<const void *>(&short_minus) : static_cast<const void *>(&minus);
    ASSERT_EQ(OB_SUCCESS, source.set_reference(coefficient, width));
    std::vector<char> wire;
    ASSERT_NO_FATAL_FAILURE(encode_value(decimal, source, wire));
    ASSERT_NO_FATAL_FAILURE(decode_value(decimal, wire, owner, copy));
    EXPECT_EQ(width, copy.length());
    int128_t number = 123;
    ASSERT_EQ(OB_SUCCESS, copy.get_decimal_int128(number));
    EXPECT_TRUE(number == (width == 0 ? int128_t(0) : int128_t(-17)));
  }
  for (uint16_t flags : {uint16_t(Datum::OutRow), uint16_t(Datum::HasLobHeader)}) {
    char payload[] = "owned reference";
    Datum source, copy;
    ASSERT_EQ(OB_SUCCESS, source.set_reference(payload, sizeof(payload), flags));
    std::vector<char> wire;
    ASSERT_NO_FATAL_FAILURE(encode_value(json, source, wire));
    ASSERT_NO_FATAL_FAILURE(decode_value(json, wire, owner, copy));
    expect_address_absent(wire, payload);
    MEMSET(payload, 0x6b, sizeof(payload)); MEMSET(wire.data(), 0x7c, wire.size());
    EXPECT_EQ(flags, copy.reference_flags());
    EXPECT_EQ(std::string("owned reference"), std::string(static_cast<const char *>(copy.get_reference())));
  }
}

TEST(NativeFrameValueWire, sql_array_domain_codec_owns_nested_values_and_rebuilds_wrapper)
{
  ObArenaAllocator source_owner("ArraySource"), destination_owner("ArrayWire");
  SqlType integer;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, integer));
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  spec.form = SqlTypeForm::Array; spec.element = &integer;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, source_owner, type));
  auto *array = DatumParameterArray::alloc(source_owner, 2);
  ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(INT64_MIN, source_owner));
  ASSERT_EQ(OB_SUCCESS, array->data_[1].set_int(91, source_owner));
  for (int64_t i = 0; i < array->count_; ++i) {
    ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(i, integer, source_owner));
  }
  alignas(8) char wrapper[15];
  Datum source, copy;
  ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY,
      123, wrapper, sizeof(wrapper), CS_LEVEL_COERCIBLE, CS_TYPE_BINARY));
  std::vector<char> wire;
  ASSERT_NO_FATAL_FAILURE(encode_value(*type, source, wire));
  expect_address_absent(wire, array); expect_address_absent(wire, wrapper); expect_address_absent(wire, type);
  ASSERT_NO_FATAL_FAILURE(decode_value(*type, wire, destination_owner, copy));
  ASSERT_TRUE(copy.has_ext_value());
  const auto *decoded = reinterpret_cast<const DatumParameterArray *>(copy.get_ext());
  ASSERT_NE(nullptr, decoded); EXPECT_NE(array, decoded); ASSERT_EQ(2, decoded->count_);
  EXPECT_EQ(123, copy.get_extend_size()); EXPECT_EQ(CS_LEVEL_COERCIBLE, copy.get_extend_collation_level());
  source_owner.reuse(); MEMSET(wrapper, 0x8d, sizeof(wrapper)); MEMSET(wire.data(), 0x9e, wire.size());
  EXPECT_EQ(INT64_MIN, decoded->data_[0].get_int()); EXPECT_EQ(91, decoded->data_[1].get_int());
  for (int64_t i = 0; i < decoded->count_; ++i) {
    ASSERT_NE(nullptr, decoded->get_element_type(i));
    EXPECT_TRUE(integer.equals(*decoded->get_element_type(i)));
    EXPECT_NE(&integer, decoded->get_element_type(i));
  }
}

TEST(NativeFrameValueWire, every_truncated_value_preserves_output_type_and_position)
{
  ObArenaAllocator owner("ValueTruncate");
  SqlType type;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObVarcharType, type));
  Datum source; ASSERT_EQ(OB_SUCCESS, source.set_string("payload", 7));
  std::vector<char> wire;
  ASSERT_NO_FATAL_FAILURE(encode_value(type, source, wire));
  for (int64_t length = 0; length < static_cast<int64_t>(wire.size()); ++length) {
    const SqlType *decoded_type = &type;
    Datum destination; destination.set_int(81);
    int64_t pos = 0;
    EXPECT_NE(OB_SUCCESS, NativeFrameValueCodec::decode(wire.data(), length, pos, owner, decoded_type, destination));
    EXPECT_EQ(0, pos); EXPECT_EQ(&type, decoded_type); EXPECT_EQ(81, destination.get_int());
  }
}

TEST(NativeFrameValueWire, pl_addresses_are_rejected_while_special_states_remain_serializable)
{
  ObArenaAllocator owner("PlWireReject");
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(1);
  spec.form = SqlTypeForm::Record;
  // This wire fixture carries a complete local declaration identity; it does
  // not exercise the parser/builder's compilation-scope producer.
  constexpr uint64_t declaration_id = UINT64_C(0x1234567890abcdef);
  spec.attributes.set_udt_id(declaration_id);
  spec.pl_declaration.source = SqlPLDeclarationSource::Local;
  spec.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  spec.pl_declaration.declaration_id = declaration_id;
  spec.pl_declaration.compilation_scope.high = UINT64_C(0x1020304050607080);
  spec.pl_declaration.compilation_scope.low = UINT64_C(0x8877665544332211);
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  alignas(8) char wrapper[15];
  int64_t pointed = 19;
  Datum source;
  ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(&pointed), 1, 8, wrapper, sizeof(wrapper)));
  int64_t size = 91;
  EXPECT_EQ(OB_NOT_SUPPORTED, NativeFrameValueCodec::size(*type, source, size));
  EXPECT_EQ(91, size);
  char buffer[256]; MEMSET(buffer, 0x7d, sizeof(buffer));
  int64_t pos = 3;
  EXPECT_EQ(OB_NOT_SUPPORTED, NativeFrameValueCodec::encode(*type, source, buffer, sizeof(buffer), pos));
  EXPECT_EQ(3, pos);
  for (char byte : buffer) { EXPECT_EQ(0x7d, static_cast<uint8_t>(byte)); }
  for (bool legacy16 : {false, true}) {
    for (int64_t address : {int64_t(0), reinterpret_cast<int64_t>(&pointed)}) {
      Datum inline_value; inline_value.set_inline_extended(address, 1, 8,
          CS_LEVEL_INVALID, CS_TYPE_INVALID, legacy16);
      size = 91; pos = 3;
      EXPECT_EQ(OB_NOT_SUPPORTED, NativeFrameValueCodec::size(*type, inline_value, size));
      EXPECT_EQ(91, size);
      EXPECT_EQ(OB_NOT_SUPPORTED, NativeFrameValueCodec::encode(*type, inline_value, buffer, sizeof(buffer), pos));
      EXPECT_EQ(3, pos);
      for (char byte : buffer) { EXPECT_EQ(0x7d, static_cast<uint8_t>(byte)); }
    }
  }
  source.set_null();
  std::vector<char> wire;
  ASSERT_NO_FATAL_FAILURE(encode_value(*type, source, wire));
  Datum copy;
  ASSERT_NO_FATAL_FAILURE(decode_value(*type, wire, owner, copy));
  EXPECT_TRUE(copy.is_null());
}

TEST(NativeFrameWire, unselected_placeholder_uses_native_expression_envelope_and_decodes_empty)
{
  ObArenaAllocator owner("EmptyExprWire");
  SqlType integer;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, integer));
  const auto &placeholder = ObEmptyExpr::instance();
  std::vector<char> wire(placeholder.get_serialize_size());
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, placeholder.serialize(wire.data(), wire.size(), pos));
  ASSERT_EQ(static_cast<int64_t>(wire.size()), pos);

  int64_t version = 0, body_length = -1;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(wire.data(), wire.size(), pos, &version));
  ASSERT_EQ(OB_SUCCESS, serialization::decode_vi64(wire.data(), wire.size(), pos, &body_length));
  EXPECT_EQ(2, version);
  EXPECT_EQ(0, body_length);
  EXPECT_EQ(static_cast<int64_t>(wire.size()), pos);

  ObExpr restored;
  restored.type_ = T_QUESTIONMARK;
  ASSERT_EQ(OB_SUCCESS, restored.bind_sql_type(integer, owner));
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(wire.data(), wire.size(), pos));
  EXPECT_EQ(static_cast<int64_t>(wire.size()), pos);
  EXPECT_EQ(T_INVALID, restored.type_);
  EXPECT_EQ(nullptr, restored.sql_type_);
  EXPECT_EQ(nullptr, restored.basic_funcs_);
  EXPECT_EQ(0U, restored.arg_cnt_);
}

TEST(NativeFrameWire, px_empty_parent_cannot_clear_constant_payload_during_dynamic_update)
{
  ObArenaAllocator source_owner("PxParentSrc"), destination_owner("PxParentDst");
  ObSqlCtx source_sql, destination_sql;
  ObExecContext source(source_owner), destination(destination_owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(source, source_sql));
  ASSERT_NO_FATAL_FAILURE(setup_context(destination, destination_sql));
  auto &source_plan = *source.get_physical_plan_ctx();
  ASSERT_NO_FATAL_FAILURE(load_parameters(source_plan, source_owner, 1, 1, true));
  ObArray<ObExpr> global_expressions;
  ObExprFrameInfo source_info(source_owner, global_expressions), destination_info(destination_owner);
  ASSERT_EQ(OB_SUCCESS, global_expressions.prepare_allocate(5));
  for (int64_t i = 0; i < global_expressions.count(); ++i) {
    ObExpr &expr = global_expressions.at(i);
    expr.type_ = i == 0 ? T_INT : (i == 1 ? T_QUESTIONMARK : T_OP_ADD);
    expr.native_parameter_alias_ = i == 1;
    expr.obj_meta_.set_int(); expr.datum_meta_.type_ = ObIntType;
    expr.datum_meta_.precision_ = 20; expr.datum_meta_.scale_ = 0;
    expr.frame_idx_ = i < 2 ? i : 2;
    const int64_t slot = i < 2 ? 0 : i - 2;
    expr.datum_off_ = ScalarFrameLayout::datum_offset(slot);
    expr.eval_info_off_ = ScalarFrameLayout::eval_info_offset(slot);
    ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(*source_plan.get_native_parameter_type(0), source_owner));
  }
  global_expressions.at(1).extra_ = 0;
  ObExpr *parent_args[] = {&global_expressions.at(1), &global_expressions.at(0)};
  ObExpr *ancestor_args[] = {&global_expressions.at(2), &global_expressions.at(0)};
  ObExpr *unselected_args[] = {&global_expressions.at(1), &global_expressions.at(3)};
  global_expressions.at(2).args_ = parent_args; global_expressions.at(2).arg_cnt_ = 2;
  global_expressions.at(3).args_ = ancestor_args; global_expressions.at(3).arg_cnt_ = 2;
  global_expressions.at(4).args_ = unselected_args; global_expressions.at(4).arg_cnt_ = 2;
  ObExpr *parameter_parents[] = {&global_expressions.at(2), &global_expressions.at(4)};
  ObExpr *parent_parents[] = {&global_expressions.at(3)};
  ObExpr *ancestor_parents[] = {&global_expressions.at(4)};
  global_expressions.at(1).parents_ = parameter_parents;
  global_expressions.at(1).parent_cnt_ = 2;
  global_expressions.at(2).parents_ = parent_parents;
  global_expressions.at(2).parent_cnt_ = 1;
  global_expressions.at(3).parents_ = ancestor_parents;
  global_expressions.at(3).parent_cnt_ = 1;
  ASSERT_EQ(OB_SUCCESS, source_info.ser_expr_marks_.prepare_allocate(5));
  for (int64_t i = 0; i < 5; ++i) { source_info.ser_expr_marks_.at(i) = i != 4; }
  const int64_t slot_size = ScalarFrameLayout::slot_size();
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_.push_back(ObFrameInfo(1, 0, slot_size, 0, slot_size)));
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_ptrs_.reserve(1));
  char *constant_frame = static_cast<char *>(source_owner.alloc(slot_size));
  ASSERT_NE(nullptr, constant_frame);
  MEMSET(constant_frame, 0, slot_size);
  new (constant_frame) Datum();
  reinterpret_cast<Datum *>(constant_frame)->set_int(1); // TEXT mode must remain one.
  new (constant_frame + ScalarFrameLayout::eval_info_offset(0)) ObEvalInfo{};
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_ptrs_.push_back(constant_frame));
  ASSERT_EQ(OB_SUCCESS, source_info.param_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.param_frame_.push_back(ObFrameInfo(1, 1, slot_size, 0, slot_size)));
  ASSERT_EQ(OB_SUCCESS, source_info.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.datum_frame_.push_back(ObFrameInfo(3, 2, 3 * slot_size, 0, 3 * slot_size)));
  ASSERT_EQ(OB_SUCCESS, source_info.pre_alloc_exec_memory(source));
  const int64_t bytes = ObPxTreeSerializer::get_serialize_expr_frame_info_size<true>(source, source_info);
  ASSERT_GT(bytes, 0);
  std::vector<char> wire(bytes);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_expr_frame_info<true>(
      wire.data(), bytes, pos, source, source_info));
  ASSERT_EQ(bytes, pos);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<true>(
      wire.data(), bytes, pos, destination, destination_info));
  ASSERT_EQ(bytes, pos);
  ASSERT_EQ(5, destination_info.rt_exprs_.count());
  ASSERT_TRUE(destination.get_physical_plan_ctx()->native_parameters_ready());
  ObExpr &mode = destination_info.rt_exprs_.at(0);
  ObExpr &parameter = destination_info.rt_exprs_.at(1);
  ObExpr &parent = destination_info.rt_exprs_.at(2);
  ObExpr &ancestor = destination_info.rt_exprs_.at(3);
  ObExpr &empty = destination_info.rt_exprs_.at(4);
  ASSERT_EQ(T_INVALID, empty.type_); ASSERT_EQ(nullptr, empty.sql_type_);
  ASSERT_EQ(0U, empty.frame_idx_); ASSERT_EQ(0U, empty.eval_info_off_);
  ASSERT_EQ(0U, empty.parent_cnt_);
  ASSERT_EQ(&parent, parameter.parents_[0]); ASSERT_EQ(&empty, parameter.parents_[1]);
  ASSERT_EQ(&ancestor, parent.parents_[0]); ASSERT_EQ(&empty, ancestor.parents_[0]);
  ObEvalCtx eval(destination);
  ASSERT_EQ(1, mode.locate_expr_datum(eval).get_int());
  for (ObExpr *expr : {&mode, &parameter, &parent, &ancestor}) {
    expr->get_eval_info(eval).evaluated_ = true;
    expr->get_eval_info(eval).projected_ = true;
    expr->get_eval_info(eval).notnull_ = true;
    expr->get_eval_info(eval).cnt_ = 7;
  }
  const std::string constant_before(destination.get_frames()[0], slot_size);
  Datum replacement; replacement.set_int(901);
  ObDynamicParamSetter setter(0, nullptr, &parameter);
  ASSERT_EQ(OB_SUCCESS, setter.update_dynamic_param(eval, replacement));
  EXPECT_EQ(901, read_parameter_value(*destination.get_physical_plan_ctx(), 0).get_int());
  EXPECT_EQ(901, parameter.locate_expr_datum(eval).get_int());
  EXPECT_TRUE(parameter.get_eval_info(eval).evaluated_);
  EXPECT_FALSE(parameter.get_eval_info(eval).projected_);
  for (ObExpr *expr : {&parent, &ancestor}) {
    EXPECT_FALSE(expr->get_eval_info(eval).evaluated_);
    EXPECT_FALSE(expr->get_eval_info(eval).projected_);
    EXPECT_TRUE(expr->get_eval_info(eval).notnull_);
    EXPECT_EQ(7, expr->get_eval_info(eval).cnt_);
  }
  EXPECT_EQ(0, MEMCMP(constant_before.data(), destination.get_frames()[0], slot_size));
  EXPECT_EQ(1, mode.locate_expr_datum(eval).get_int());
  ObDynamicParamSetter::clear_parent_evaluated_flag(eval, empty);
  EXPECT_EQ(0, MEMCMP(constant_before.data(), destination.get_frames()[0], slot_size));

  // A selected constant still has a real EvalInfo. It is not a placeholder.
  ObDynamicParamSetter::clear_parent_evaluated_flag(eval, mode);
  EXPECT_FALSE(mode.get_eval_info(eval).evaluated_);
  EXPECT_FALSE(mode.get_eval_info(eval).projected_);
  EXPECT_TRUE(mode.get_eval_info(eval).notnull_);
  EXPECT_EQ(7, mode.get_eval_info(eval).cnt_);
  EXPECT_EQ(1, mode.locate_expr_datum(eval).get_int());

  // Either a real expression kind or a prepared Type prevents empty-slot
  // classification; retain the existing non-placeholder traversal behavior.
  ObExpr typed_parent;
  ASSERT_EQ(OB_SUCCESS, typed_parent.bind_sql_type(ancestor.get_sql_type(), destination_owner));
  typed_parent.frame_idx_ = ancestor.frame_idx_; typed_parent.eval_info_off_ = ancestor.eval_info_off_;
  ancestor.get_eval_info(eval).evaluated_ = true;
  ObDynamicParamSetter::clear_parent_evaluated_flag(eval, typed_parent);
  EXPECT_FALSE(ancestor.get_eval_info(eval).evaluated_);
  ObExpr kind_parent;
  kind_parent.type_ = T_OP_ADD;
  kind_parent.frame_idx_ = ancestor.frame_idx_; kind_parent.eval_info_off_ = ancestor.eval_info_off_;
  ancestor.get_eval_info(eval).evaluated_ = true;
  ObDynamicParamSetter::clear_parent_evaluated_flag(eval, kind_parent);
  EXPECT_FALSE(ancestor.get_eval_info(eval).evaluated_);
}

TEST(NativeFrameWire, no_data_batch_constructs_actual_offsets_without_copying_source_values)
{
  ObArenaAllocator owner("BatchWire");
  ObExprFrameInfo info(owner);
  ASSERT_NO_FATAL_FAILURE(make_batch_info(info, owner));
  ObSEArray<char *, 1> no_parameters;
  char **source = nullptr; uint64_t frame_count = 0;
  ASSERT_EQ(OB_SUCCESS, info.alloc_frame(owner, no_parameters, frame_count, source));
  for (int64_t i = 0; i < 3; ++i) { reinterpret_cast<Datum *>(source[0] + 16)[i].set_int(300 + i); }
  reinterpret_cast<Datum *>(source[0] + 80)->set_int(711);
  const int64_t size = ObPxTreeSerializer::get_serialize_frame_info_size<false>(info.datum_frame_, info, source, 1, true);
  ASSERT_EQ(13, size); // version + no-data byte + zero record count
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_frame_info<false>(wire.data(), size, pos,
      info.datum_frame_, info, source, 1, true));
  char *destination[] = {nullptr}; pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_frame_info<false>(wire.data(), size, pos, owner,
      info.datum_frame_, info, nullptr, destination, 1, true));
  ASSERT_EQ(size, pos);
  for (int64_t i = 0; i < 3; ++i) { EXPECT_TRUE(reinterpret_cast<Datum *>(destination[0] + 16)[i].is_null()); }
  EXPECT_TRUE(reinterpret_cast<Datum *>(destination[0] + 80)->is_null());
  EXPECT_EQ(0, reinterpret_cast<ObEvalInfo *>(destination[0] + 104)->flag_);
  EXPECT_EQ(0, reinterpret_cast<ObEvalInfo *>(destination[0] + 112)->cnt_);
}

TEST(NativeFrameWire, invalid_batch_extent_rejects_before_publishing_frame)
{
  ObArenaAllocator owner("BadFrame");
  ObExprFrameInfo info(owner);
  ASSERT_NO_FATAL_FAILURE(make_batch_info(info, owner));
  char wire[13]; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_i32(wire, sizeof(wire), pos, 1));
  ASSERT_EQ(OB_SUCCESS, serialization::encode_i8(wire, sizeof(wire), pos, 1));
  ASSERT_EQ(OB_SUCCESS, serialization::encode_i64(wire, sizeof(wire), pos, 0));
  info.rt_exprs_.at(0).pvt_skip_off_ = UINT32_MAX;
  char *frames[] = {nullptr}; pos = 0;
  EXPECT_NE(OB_SUCCESS, ObPxTreeSerializer::deserialize_frame_info<false>(wire, sizeof(wire), pos, owner,
      info.datum_frame_, info, nullptr, frames, 1, true));
  EXPECT_EQ(0, pos); EXPECT_EQ(nullptr, frames[0]);
}

TEST(NativeFrameWire, px_restores_all_parameters_directly_into_unique_owner_despite_partial_metadata)
{
  ObArenaAllocator source_owner("PxSource"), destination_owner("PxDestination");
  ObSqlCtx source_sql, destination_sql;
  ObExecContext source(source_owner), destination(destination_owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(source, source_sql));
  ASSERT_NO_FATAL_FAILURE(setup_context(destination, destination_sql));
  auto &source_plan = *source.get_physical_plan_ctx();
  auto &destination_plan = *destination.get_physical_plan_ctx();
  const int64_t original = 2;
  const int64_t count = original + 4;
  ASSERT_NO_FATAL_FAILURE(load_parameters(source_plan, source_owner, count, original, true));
  ASSERT_NO_FATAL_FAILURE(load_parameters(destination_plan, destination_owner, count, original, false));
  ObArray<ObExpr> global_expressions;
  ObExprFrameInfo source_info(source_owner, global_expressions), destination_info(destination_owner);
  ASSERT_NO_FATAL_FAILURE(partial_parameter_info(source_info, source_plan, source_owner));
  const ObExpr selected = global_expressions.at(0);
  ASSERT_EQ(OB_SUCCESS, global_expressions.push_back(selected));
  global_expressions.at(0) = ObExpr();
  ASSERT_EQ(OB_SUCCESS, source_info.ser_expr_marks_.prepare_allocate(2));
  source_info.ser_expr_marks_.at(0) = false;
  source_info.ser_expr_marks_.at(1) = true;
  ASSERT_EQ(OB_SUCCESS, source_info.pre_alloc_exec_memory(source));
  const int64_t bytes = ObPxTreeSerializer::get_serialize_expr_frame_info_size<true>(source, source_info);
  ASSERT_GT(bytes, 0);
  std::vector<char> wire(bytes);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_expr_frame_info<true>(wire.data(), bytes, pos, source, source_info));
  ASSERT_EQ(bytes, pos);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<true>(wire.data(), bytes, pos, destination, destination_info));
  ASSERT_EQ(bytes, pos); ASSERT_EQ(2, destination_plan.get_param_frame_ptrs().count());
  ASSERT_EQ(2, destination_info.rt_exprs_.count());
  EXPECT_EQ(T_INVALID, destination_info.rt_exprs_.at(0).type_);
  EXPECT_EQ(nullptr, destination_info.rt_exprs_.at(0).sql_type_);
  for (int64_t i = 0; i < 2; ++i) { EXPECT_EQ(destination.get_frames()[i], destination_plan.get_param_frame_ptrs().at(i)); }
  for (int64_t i = 0; i < count; ++i) {
    const Datum snapshot = read_parameter_value(destination_plan, i);
    const Datum *value = &snapshot;
    ASSERT_NE(nullptr, destination_plan.get_native_parameter_type(i));
    EXPECT_EQ(nullptr, destination_plan.locate_materialized_parameter(i));
    if (i == 1) { EXPECT_TRUE(value->is_null()); }
    else {
      EXPECT_EQ(i == 0 ? INT64_MIN : i + 71, value->get_int());
      EXPECT_EQ(0, value->length());
      ASSERT_NE(nullptr, destination_plan.get_parameter_metadata(i));
      EXPECT_EQ(ObIntType, destination_plan.get_parameter_metadata(i)->type_meta_.get_type());
    }
  }
  ObEvalCtx eval(destination);
  Datum questionmark;
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(destination_info.rt_exprs_.at(1), eval, questionmark));
  EXPECT_EQ(count - 1 + 71, questionmark.get_int());
  for (int64_t page = 0; page < source_plan.get_param_frame_ptrs().count(); ++page) {
    char *handle = source_plan.get_param_frame_ptrs().at(page);
    const int64_t bytes = ParameterValuePage::allocation_bytes(ParameterValuePage::from_handle(handle).capacity());
    MEMSET(handle, 0x6c, bytes);
  }
  source_plan.reset_datum_param_store();
  MEMSET(wire.data(), 0xb7, wire.size());
  EXPECT_EQ(INT64_MIN, read_parameter_value(destination_plan, 0).get_int());
  EXPECT_EQ(count - 1 + 71, read_parameter_value(destination_plan, count - 1).get_int());
}

TEST(NativeFrameWire, trailing_frame_truncation_keeps_existing_plan_owner_and_context_frames)
{
  ObArenaAllocator source_owner("PxTruncSource"), destination_owner("PxTruncDest");
  ObSqlCtx source_sql, destination_sql;
  ObExecContext source(source_owner), destination(destination_owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(source, source_sql));
  ASSERT_NO_FATAL_FAILURE(setup_context(destination, destination_sql));
  const int64_t original = 2;
  const int64_t count = original + 4;
  ASSERT_NO_FATAL_FAILURE(load_parameters(*source.get_physical_plan_ctx(), source_owner, count, original, true));
  ASSERT_NO_FATAL_FAILURE(load_parameters(*destination.get_physical_plan_ctx(), destination_owner, count, original, false));
  ObExprFrameInfo info(source_owner);
  ASSERT_NO_FATAL_FAILURE(partial_parameter_info(info, *source.get_physical_plan_ctx(), source_owner));
  ASSERT_EQ(OB_SUCCESS, info.pre_alloc_exec_memory(source));
  ASSERT_EQ(OB_SUCCESS, info.pre_alloc_exec_memory(destination));
  const Datum *before = materialized_parameter(*destination.get_physical_plan_ctx(), 0);
  char **old_frames = destination.get_frames();
  const int64_t size = ObPxTreeSerializer::get_serialize_expr_frame_info_size<false>(source, info);
  ASSERT_GT(size, 0);
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_expr_frame_info<false>(wire.data(), size, pos, source, info));
  pos = 0;
  EXPECT_NE(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<false>(wire.data(), size - 1, pos, destination, info));
  EXPECT_EQ(0, pos); EXPECT_EQ(old_frames, destination.get_frames());
  EXPECT_EQ(before, materialized_parameter(*destination.get_physical_plan_ctx(), 0));
  EXPECT_EQ(-99, before->get_int());
}

TEST(NativeFrameWire, every_truncated_parameter_segment_leaves_page_and_type_outputs_empty)
{
  ObArenaAllocator source_owner("ParamSource"), destination_owner("ParamTruncate");
  ObSqlCtx sql_context;
  ObExecContext exec(source_owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(exec, sql_context));
  ASSERT_NO_FATAL_FAILURE(load_parameters(*exec.get_physical_plan_ctx(), source_owner, 3, 1, true));
  int64_t size = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::parameter_size(*exec.get_physical_plan_ctx(), size));
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::serialize_parameters(*exec.get_physical_plan_ctx(), wire.data(), size, pos));
  for (int64_t length = 0; length < size; ++length) {
    ObSEArray<char *, 4> pages;
    ObSEArray<const SqlType *, 16> types;
    int64_t original = 81; pos = 0;
    EXPECT_NE(OB_SUCCESS, NativeFrameCodec::deserialize_parameters(wire.data(), length, pos, destination_owner,
        pages, types, original));
    EXPECT_EQ(0, pos); EXPECT_EQ(81, original); EXPECT_EQ(0, pages.count()); EXPECT_EQ(0, types.count());
    destination_owner.reuse();
  }
}

TEST(NativeFrameWire, malformed_parameter_count_is_rejected_before_any_page_publication)
{
  char wire[20]; int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_i32(wire, sizeof(wire), pos, 1));
  ASSERT_EQ(OB_SUCCESS, serialization::encode_i64(wire, sizeof(wire), pos, INT64_MAX));
  ASSERT_EQ(OB_SUCCESS, serialization::encode_i64(wire, sizeof(wire), pos, 0));
  ObArenaAllocator owner("ParamBadCount");
  ObSEArray<char *, 1> pages;
  ObSEArray<const SqlType *, 1> types;
  int64_t original = 17; pos = 0;
  EXPECT_EQ(OB_DESERIALIZE_ERROR, NativeFrameCodec::deserialize_parameters(wire, sizeof(wire), pos,
      owner, pages, types, original));
  EXPECT_EQ(0, pos); EXPECT_EQ(17, original); EXPECT_EQ(0, pages.count()); EXPECT_EQ(0, types.count());
}


TEST(NativeFrameWire, sqc_parts2_keeps_selected_expression_directory_through_tree_and_restores_caller)
{
  // Preserve the test runner's TLS directory even if a fatal assertion returns.
  ObPxTreeSerializer::ExprArrayScope restore_runner_directory;
  ObArenaAllocator source_owner("PxCompoundSrc");
  ObSqlCtx source_sql;
  ObExecContext source(source_owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(source, source_sql));
  auto &parameters = *source.get_physical_plan_ctx();
  ASSERT_NO_FATAL_FAILURE(load_parameters(parameters, source_owner, 1, 1, true));
  ObExprFrameInfo source_info(source_owner);
  ASSERT_EQ(OB_SUCCESS, source_info.rt_exprs_.prepare_allocate(1));
  ObExpr &parameter = source_info.rt_exprs_.at(0);
  parameter.type_ = T_QUESTIONMARK;
  parameter.native_parameter_alias_ = true;
  parameter.obj_meta_.set_int();
  parameter.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 20);
  parameter.extra_ = 0;
  parameter.frame_idx_ = 0;
  parameter.datum_off_ = ScalarFrameLayout::datum_offset(0);
  parameter.eval_info_off_ = ScalarFrameLayout::eval_info_offset(0);
  ASSERT_EQ(OB_SUCCESS, parameter.bind_sql_type(*parameters.get_native_parameter_type(0), source_owner));
  ASSERT_EQ(OB_SUCCESS, source_info.param_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.param_frame_.push_back(ObFrameInfo(1, 0, 24, 0, 24)));
  ASSERT_EQ(OB_SUCCESS, source_info.pre_alloc_exec_memory(source));

  ObPhysicalPlan physical;
  ObOpSpec *transmit = nullptr, *values = nullptr;
  ASSERT_EQ(OB_SUCCESS, physical.alloc_op_spec(PHY_PX_REDUCE_TRANSMIT, 1, transmit, 0));
  ASSERT_EQ(OB_SUCCESS, physical.alloc_op_spec(PHY_VALUES, 0, values, 1));
  ASSERT_NE(nullptr, transmit); ASSERT_NE(nullptr, values);
  ASSERT_EQ(OB_SUCCESS, transmit->set_child(0, values));
  physical.set_root_op_spec(transmit);
  auto &px_transmit = static_cast<ObPxTransmitSpec &>(*transmit);
  px_transmit.dfo_expr_frame_info_ = &source_info;
  px_transmit.tablet_id_expr_ = &parameter;
  for (ObOpSpec *op : {transmit, values}) {
    ASSERT_EQ(OB_SUCCESS, op->output_.reserve(1));
    ASSERT_EQ(OB_SUCCESS, op->output_.push_back(&parameter));
  }
  ASSERT_EQ(0, physical.get_expr_frame_info().rt_exprs_.count());
  ASSERT_EQ(OB_SUCCESS, source.get_kit_store().init(source_owner, 2));
  ObPxInitSqcArgs arguments;
  arguments.set_serialize_param(source, *transmit, physical);
  ObPhyOpSeriCtx serialization_context;
  serialization_context.exec_ctx_ = &source;
  ObArray<ObExpr> sentinel; // An empty prior directory is legal and unrelated.

  for (int prior = 0; prior < 2; ++prior) {
    ObExpr::ObExprIArray *previous = prior == 0 ? nullptr : &sentinel;
    ObExpr::get_serialize_array() = previous;
    const int64_t frame_bytes = ObPxTreeSerializer::get_serialize_expr_frame_info_size<true>(source, source_info);
    ASSERT_GT(frame_bytes, 0);
    const int64_t size = frame_bytes
        + ObPxTreeSerializer::get_tree_serialize_size(*transmit, false, &serialization_context)
        + ObPxTreeSerializer::get_serialize_op_input_size(*transmit, source.get_kit_store());
    EXPECT_EQ(previous, ObExpr::get_serialize_array());
    // The real SQC size caller also selects this DFO directory, then restores TLS.
    EXPECT_GT(arguments.get_serialize_size(), 0);
    EXPECT_EQ(previous, ObExpr::get_serialize_array());
    std::vector<char> wire(size);
    int64_t pos = 0;
    ASSERT_EQ(OB_SUCCESS, (arguments.*px_member(PxParts2Tag{}))(wire.data(), size, pos));
    ASSERT_EQ(size, pos);
    EXPECT_EQ(previous, ObExpr::get_serialize_array());

    ObArenaAllocator destination_owner("PxCompoundDst");
    ObSqlCtx destination_sql;
    ObExecContext destination(destination_owner);
    ASSERT_NO_FATAL_FAILURE(setup_context(destination, destination_sql));
    ObExprFrameInfo destination_info(destination_owner);
    ObPhysicalPlan destination_physical;
    ObOpSpec *decoded = nullptr;
    ObSEArray<const ObTableScanSpec *, 1> scans;
    pos = 0;
    {
      // Exercise the corresponding public frame/tree/input decoders under the
      // same compound directory contract, without a complete RPC/session clone.
      ObPxTreeSerializer::ExprArrayScope restore_previous;
      ObExpr::get_serialize_array() = &destination_info.rt_exprs_;
      ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<true>(
          wire.data(), size, pos, destination, destination_info));
      EXPECT_EQ(&destination_info.rt_exprs_, ObExpr::get_serialize_array());
      ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_tree(
          wire.data(), size, pos, destination_physical, decoded, scans));
      ASSERT_NE(nullptr, decoded); ASSERT_EQ(1, decoded->output_.count());
      ASSERT_EQ(1, decoded->get_child_cnt()); ASSERT_NE(nullptr, decoded->get_child(0));
      ASSERT_EQ(1, decoded->get_child(0)->output_.count());
      EXPECT_EQ(&destination_info.rt_exprs_.at(0), decoded->output_.at(0));
      EXPECT_EQ(decoded->output_.at(0), decoded->get_child(0)->output_.at(0));
      EXPECT_EQ(decoded->output_.at(0), static_cast<ObPxTransmitSpec *>(decoded)->tablet_id_expr_);
      ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_op_input(
          wire.data(), size, pos, destination.get_kit_store()));
      ASSERT_EQ(size, pos);
    }
    EXPECT_EQ(previous, ObExpr::get_serialize_array());
    ASSERT_NE(nullptr, materialized_parameter(*destination.get_physical_plan_ctx(), 0));
    EXPECT_EQ(INT64_MIN, read_parameter_value(*destination.get_physical_plan_ctx(), 0).get_int());
    ObEvalCtx evaluation(destination);
    Datum result;
    ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(destination_info.rt_exprs_.at(0), evaluation, result));
    EXPECT_EQ(INT64_MIN, result.get_int());

    // Fail after the complete frame segment, while the tree is being written.
    // Its error must restore the original directory too, without weakening it.
    pos = 0;
    EXPECT_NE(OB_SUCCESS, (arguments.*px_member(PxParts2Tag{}))(wire.data(), frame_bytes, pos));
    EXPECT_EQ(previous, ObExpr::get_serialize_array());
    MEMSET(wire.data(), 0xA5, wire.size());
    EXPECT_EQ(INT64_MIN, read_parameter_value(*destination.get_physical_plan_ctx(), 0).get_int());
  }
}

namespace
{
class FrameInitAllocator final : public ObIAllocator
{
public:
  explicit FrameInitAllocator(int64_t frame_bytes)
    : owner_("FrameInitTest"), frame_bytes_(frame_bytes), calls_(0), fail_call_(0),
      rejected_(0), last_frame_(nullptr) {}
  void *alloc(int64_t size) override
  {
    ++calls_;
    if (calls_ == fail_call_) { ++rejected_; return nullptr; }
    void *memory = owner_.alloc(size);
    if (memory != nullptr) {
      MEMSET(memory, 0xA5, size);
      if (size == frame_bytes_) { last_frame_ = static_cast<char *>(memory); }
    }
    return memory;
  }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *memory) override { owner_.free(memory); }
  void begin_attempt(int64_t fail_call = 0)
  { calls_ = 0; fail_call_ = fail_call; rejected_ = 0; last_frame_ = nullptr; }
  ObArenaAllocator owner_;
  int64_t frame_bytes_;
  int64_t calls_;
  int64_t fail_call_;
  int64_t rejected_;
  char *last_frame_;
};

void make_init_info(ObExprFrameInfo &info, ObIAllocator &owner,
                    const std::vector<int64_t> &slots)
{
  ASSERT_FALSE(slots.empty());
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.prepare_allocate(slots.size()));
  SqlType type;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, type, ObAccuracy(20, 0)));
  int64_t max_slot = 0;
  for (int64_t i = 0; i < static_cast<int64_t>(slots.size()); ++i) {
    max_slot = std::max(max_slot, slots[i]);
    ObExpr &expr = info.rt_exprs_.at(i);
    expr.type_ = T_REF_COLUMN;
    expr.datum_meta_.type_ = ObIntType;
    expr.datum_meta_.precision_ = 20;
    expr.datum_meta_.scale_ = 0;
    expr.obj_meta_.set_int();
    expr.frame_idx_ = 0;
    expr.datum_off_ = ScalarFrameLayout::datum_offset(slots[i]);
    expr.eval_info_off_ = ScalarFrameLayout::eval_info_offset(slots[i]);
    ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(type, owner));
  }
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, info.datum_frame_.push_back(ObFrameInfo(slots.size(), 0,
      (max_slot + 1) * ScalarFrameLayout::slot_size(), 0, 0)));
}

void expect_init_failure_without_writes(const ObExprFrameInfo &info, int expected)
{
  const int64_t bytes = info.datum_frame_.at(0).frame_size_;
  FrameInitAllocator exec_owner(bytes);
  ObSEArray<char *, 1> no_parameters;
  char previous_bytes[8]; char *previous = previous_bytes;
  char **frames = &previous; uint64_t count = 77;
  EXPECT_EQ(expected, info.alloc_frame(exec_owner, no_parameters, count, frames));
  EXPECT_EQ(&previous, frames); EXPECT_EQ(77, count); EXPECT_EQ(previous_bytes, previous);
  ASSERT_NE(nullptr, exec_owner.last_frame_);
  EXPECT_TRUE(std::all_of(exec_owner.last_frame_, exec_owner.last_frame_ + bytes,
      [](char byte) { return static_cast<unsigned char>(byte) == 0xA5; }));
}

void expect_valid_init_matches_wire_initializer(const ObExprFrameInfo &info)
{
  const ObFrameInfo &frame = info.datum_frame_.at(0);
  FrameInitAllocator exec_owner(frame.frame_size_);
  ObSEArray<char *, 1> no_parameters;
  char **frames = nullptr; uint64_t count = 0;
  ASSERT_EQ(OB_SUCCESS, info.alloc_frame(exec_owner, no_parameters, count, frames));
  ASSERT_EQ(1, count); ASSERT_NE(nullptr, frames); ASSERT_NE(nullptr, frames[0]);
  std::vector<char> reference(frame.frame_size_, static_cast<char>(0xA5));
  ASSERT_EQ(OB_SUCCESS, NativeFrameCodec::initialize_frame(frame, info, reference.data()));
  EXPECT_EQ(0, MEMCMP(reference.data(), frames[0], frame.frame_size_));
  for (int64_t i = 0; i < info.rt_exprs_.count(); ++i) {
    if (!info.is_mark_serialize() || (i < info.ser_expr_marks_.count() && info.ser_expr_marks_.at(i))) {
      const ObExpr &expr = info.rt_exprs_.at(i);
      EXPECT_TRUE(reinterpret_cast<const Datum *>(frames[0] + expr.datum_off_)->is_null());
      EXPECT_EQ(0, reinterpret_cast<const ObEvalInfo *>(frames[0] + expr.eval_info_off_)->flag_);
    }
  }
}
} // namespace

TEST(NativeFrameWire, execution_layout_accepts_shuffled_aliases_and_shared_result_regions)
{
  ObArenaAllocator owner("InitAliases");
  ObExprFrameInfo info(owner);
  ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, {2, 0, 2, 1, 0}));
  ASSERT_NO_FATAL_FAILURE(expect_valid_init_matches_wire_initializer(info));

  // Result buffers and dynamic headers may be shared. The existing overlap
  // policy concerns Datum/EvalInfo regions; this optimization must not widen it.
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_BINARY);
  SqlType text;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), text));
  ObExprFrameInfo shared(owner);
  ASSERT_NO_FATAL_FAILURE(make_init_info(shared, owner, {1, 0}));
  shared.datum_frame_.at(0).frame_size_ = 256;
  for (int64_t i = 0; i < shared.rt_exprs_.count(); ++i) {
    ObExpr &expr = shared.rt_exprs_.at(i);
    expr.datum_meta_.type_ = ObVarcharType;
    expr.datum_meta_.cs_type_ = CS_TYPE_BINARY;
    expr.obj_meta_ = meta;
    expr.res_buf_len_ = 16; expr.res_buf_off_ = 160; expr.dyn_buf_header_offset_ = 128;
    ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(text, owner));
  }
  ASSERT_NO_FATAL_FAILURE(expect_valid_init_matches_wire_initializer(shared));
}

TEST(NativeFrameWire, execution_layout_rejects_alias_type_eval_and_batch_count_mismatches)
{
  ObArenaAllocator owner("BadInitAlias");
  {
    ObExprFrameInfo info(owner);
    ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, {0, 0}));
    SqlType different;
    ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, different, ObAccuracy(19, 0)));
    info.rt_exprs_.at(1).datum_meta_.precision_ = 19;
    ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.at(1).bind_sql_type(different, owner));
    ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(info, OB_DESERIALIZE_ERROR));
  }
  {
    ObExprFrameInfo info(owner);
    ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, {0, 0}));
    info.datum_frame_.at(0).frame_size_ = 96;
    info.rt_exprs_.at(1).eval_info_off_ = 40;
    ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(info, OB_DESERIALIZE_ERROR));
  }
  {
    ObExprFrameInfo info(owner);
    ASSERT_NO_FATAL_FAILURE(make_batch_info(info, owner));
    ObExpr &alias = info.rt_exprs_.at(1);
    alias.batch_result_ = true; alias.datum_off_ = 16;
    alias.pvt_skip_off_ = 48; alias.eval_info_off_ = 104; alias.eval_flags_off_ = 120;
    ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(info, OB_DESERIALIZE_ERROR));
  }
}

TEST(NativeFrameWire, execution_layout_rejects_each_cross_slot_overlap_without_writes)
{
  ObArenaAllocator owner("InitOverlap");
  for (int kind = 0; kind < 3; ++kind) {
    ObExprFrameInfo info(owner);
    ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, {2, 0, 1}));
    if (kind == 0) { info.rt_exprs_.at(0).datum_off_ = 8; } // Datum/Datum
    else if (kind == 1) { info.rt_exprs_.at(0).eval_info_off_ = 0; } // EvalInfo/Datum
    else { info.rt_exprs_.at(0).eval_info_off_ = 40; } // EvalInfo/EvalInfo, nonadjacent Exprs
    ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(info, OB_DESERIALIZE_ERROR));
  }
}

TEST(NativeFrameWire, execution_layout_preserves_pair_and_extent_error_precedence)
{
  ObArenaAllocator owner("InitErrorOrder");
  {
    ObExprFrameInfo info(owner);
    ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, {0, 1, 2}));
    info.rt_exprs_.at(1).eval_info_off_ = 0; // invalid earlier pair
    info.rt_exprs_.at(2).sql_type_ = nullptr; // later independent extent error
    ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(info, OB_DESERIALIZE_ERROR));
  }
  {
    ObExprFrameInfo info(owner);
    ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, {0, 1, 2}));
    info.rt_exprs_.at(0).sql_type_ = nullptr; // first failure must win
    info.rt_exprs_.at(2).eval_info_off_ = 0;
    ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(info, OB_ERR_UNEXPECTED));
  }
}

TEST(NativeFrameWire, execution_layout_honors_partial_selection_and_batch_offsets)
{
  ObArenaAllocator owner("InitPartial");
  ObExprFrameInfo global(owner);
  ASSERT_NO_FATAL_FAILURE(make_init_info(global, owner, {1, 100, 0}));
  ObExprFrameInfo partial(owner, global.rt_exprs_);
  ASSERT_EQ(OB_SUCCESS, partial.ser_expr_marks_.init(3));
  ASSERT_EQ(OB_SUCCESS, partial.ser_expr_marks_.prepare_allocate(3));
  partial.ser_expr_marks_.at(0) = true; partial.ser_expr_marks_.at(1) = false;
  partial.ser_expr_marks_.at(2) = true;
  ASSERT_EQ(OB_SUCCESS, partial.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, partial.datum_frame_.push_back(ObFrameInfo(3, 0, 48, 0, 2424)));
  // The unselected Expr lies outside the shrunk frame and is not a bound.
  ASSERT_NO_FATAL_FAILURE(expect_valid_init_matches_wire_initializer(partial));
  partial.ser_expr_marks_.at(1) = true;
  ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(partial, OB_DESERIALIZE_ERROR));

  ObExprFrameInfo batch(owner);
  ASSERT_NO_FATAL_FAILURE(make_batch_info(batch, owner));
  ASSERT_NO_FATAL_FAILURE(expect_valid_init_matches_wire_initializer(batch));
  batch.rt_exprs_.at(0).pvt_skip_off_ = UINT32_MAX;
  ASSERT_NO_FATAL_FAILURE(expect_init_failure_without_writes(batch, OB_DESERIALIZE_ERROR));
}

TEST(NativeFrameWire, execution_layout_large_shuffled_frame_initializes_every_native_slot)
{
  ObArenaAllocator owner("LargeInit");
  std::vector<int64_t> slots;
  for (int64_t i = 0; i < 257; ++i) { slots.push_back((i * 73) % 257); }
  slots.push_back(0); slots.push_back(128);
  ObExprFrameInfo info(owner);
  ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, slots));
  ASSERT_NO_FATAL_FAILURE(expect_valid_init_matches_wire_initializer(info));
}

TEST(NativeFrameWire, execution_layout_spill_oom_keeps_frame_and_published_outputs_unchanged)
{
  ObArenaAllocator owner("InitOomInfo");
  std::vector<int64_t> slots;
  for (int64_t i = 0; i < 65; ++i) { slots.push_back((i * 17) % 65); }
  ObExprFrameInfo info(owner);
  ASSERT_NO_FATAL_FAILURE(make_init_info(info, owner, slots));
  const int64_t bytes = info.datum_frame_.at(0).frame_size_;
  FrameInitAllocator exec_owner(bytes);
  ObSEArray<char *, 1> no_parameters;
  // Directory and frame are the first two real allocations. Fail each of the
  // cell/interval spills in turn, after the frame exists but before any write.
  for (int64_t fail_call : {int64_t(3), int64_t(4)}) {
    exec_owner.begin_attempt(fail_call);
    char previous_bytes[8]; char *previous = previous_bytes;
    char **frames = &previous; uint64_t count = 77;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, info.alloc_frame(exec_owner, no_parameters, count, frames));
    EXPECT_EQ(fail_call, exec_owner.calls_); EXPECT_EQ(1, exec_owner.rejected_);
    EXPECT_EQ(&previous, frames); EXPECT_EQ(77, count); EXPECT_EQ(previous_bytes, previous);
    ASSERT_NE(nullptr, exec_owner.last_frame_);
    EXPECT_TRUE(std::all_of(exec_owner.last_frame_, exec_owner.last_frame_ + bytes,
        [](char byte) { return static_cast<unsigned char>(byte) == 0xA5; }));
    exec_owner.begin_attempt();
    ASSERT_EQ(OB_SUCCESS, info.alloc_frame(exec_owner, no_parameters, count, frames));
    ASSERT_EQ(1, count); ASSERT_NE(nullptr, frames);
    for (int64_t slot = 0; slot < 65; ++slot) {
      EXPECT_TRUE(reinterpret_cast<const Datum *>(frames[0]
          + ScalarFrameLayout::datum_offset(slot))->is_null());
    }
  }
}

TEST(NativeFrameWire, nested_px_partial_directory_reserializes_empty_slots_without_losing_indices)
{
  ObArenaAllocator source_owner("PxHopSrc"), middle_owner("PxHopMid"), final_owner("PxHopDst");
  ObSqlCtx source_sql, middle_sql, final_sql;
  ObExecContext source(source_owner), middle(middle_owner), final_context(final_owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(source, source_sql));
  ASSERT_NO_FATAL_FAILURE(setup_context(middle, middle_sql));
  ASSERT_NO_FATAL_FAILURE(setup_context(final_context, final_sql));
  auto &parameters = *source.get_physical_plan_ctx();
  ASSERT_NO_FATAL_FAILURE(load_parameters(parameters, source_owner, 1, 1, true));
  ObArray<ObExpr> global_expressions;
  ObExprFrameInfo source_info(source_owner, global_expressions);
  ObExprFrameInfo middle_info(middle_owner), final_info(final_owner);
  ASSERT_EQ(OB_SUCCESS, global_expressions.prepare_allocate(6));
  for (int64_t i = 0; i < global_expressions.count(); ++i) {
    ObExpr &expr = global_expressions.at(i);
    expr.type_ = i == 0 ? T_INT : (i == 2 ? T_QUESTIONMARK : (i == 5 ? T_NULL : T_OP_ADD));
    expr.native_parameter_alias_ = i == 2;
    expr.obj_meta_.set_int();
    expr.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 20);
    expr.frame_idx_ = (i == 0 || i == 5) ? 0 : (i == 2 ? 1 : 2);
    const int64_t slot = i == 5 ? 1 : (i == 1 ? 2 : (i >= 3 ? i - 3 : 0));
    expr.datum_off_ = ScalarFrameLayout::datum_offset(slot);
    expr.eval_info_off_ = ScalarFrameLayout::eval_info_offset(slot);
    ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(*parameters.get_native_parameter_type(0), source_owner));
  }
  ObExpr *sum_args[] = {&global_expressions.at(2), &global_expressions.at(0)};
  ObExpr *outer_args[] = {&global_expressions.at(3), &global_expressions.at(2)};
  ObExpr *parameter_parents[] = {&global_expressions.at(3), &global_expressions.at(1)};
  ObExpr *sum_parents[] = {&global_expressions.at(4)};
  global_expressions.at(3).args_ = sum_args; global_expressions.at(3).arg_cnt_ = 2;
  global_expressions.at(4).args_ = outer_args; global_expressions.at(4).arg_cnt_ = 2;
  global_expressions.at(2).parents_ = parameter_parents; global_expressions.at(2).parent_cnt_ = 2;
  global_expressions.at(3).parents_ = sum_parents; global_expressions.at(3).parent_cnt_ = 1;
  ASSERT_EQ(OB_SUCCESS, source_info.ser_expr_marks_.prepare_allocate(6));
  for (int64_t i = 0; i < 6; ++i) { source_info.ser_expr_marks_.at(i) = i != 1; }
  ASSERT_TRUE(source_info.is_mark_serialize());
  const int64_t slot_size = ScalarFrameLayout::slot_size();
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_.push_back(ObFrameInfo(2, 0, 2 * slot_size, 0, 2 * slot_size)));
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_ptrs_.reserve(1));
  char *constants = static_cast<char *>(source_owner.alloc(2 * slot_size));
  ASSERT_NE(nullptr, constants);
  MEMSET(constants, 0, 2 * slot_size);
  for (int64_t slot = 0; slot < 2; ++slot) {
    new (constants + ScalarFrameLayout::datum_offset(slot)) Datum();
    new (constants + ScalarFrameLayout::eval_info_offset(slot)) ObEvalInfo{};
  }
  reinterpret_cast<Datum *>(constants)->set_int(1);
  reinterpret_cast<Datum *>(constants + ScalarFrameLayout::datum_offset(1))->set_null();
  ASSERT_EQ(OB_SUCCESS, source_info.const_frame_ptrs_.push_back(constants));
  ASSERT_EQ(OB_SUCCESS, source_info.param_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.param_frame_.push_back(ObFrameInfo(1, 1, slot_size, 0, slot_size)));
  ASSERT_EQ(OB_SUCCESS, source_info.datum_frame_.reserve(1));
  ASSERT_EQ(OB_SUCCESS, source_info.datum_frame_.push_back(ObFrameInfo(3, 2, 3 * slot_size, 0, 3 * slot_size)));
  ASSERT_EQ(OB_SUCCESS, source_info.pre_alloc_exec_memory(source));

  const int64_t first_size = ObPxTreeSerializer::get_serialize_expr_frame_info_size<true>(source, source_info);
  ASSERT_GT(first_size, 0);
  std::vector<char> first_wire(first_size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_expr_frame_info<true>(
      first_wire.data(), first_wire.size(), pos, source, source_info));
  ASSERT_EQ(first_size, pos);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<true>(
      first_wire.data(), first_wire.size(), pos, middle, middle_info));
  ASSERT_EQ(first_size, pos);
  ASSERT_FALSE(middle_info.is_mark_serialize());
  ASSERT_EQ(0, middle_info.ser_expr_marks_.count());
  ASSERT_EQ(6, middle_info.rt_exprs_.count());
  ASSERT_EQ(T_INVALID, middle_info.rt_exprs_.at(1).type_);
  ASSERT_EQ(nullptr, middle_info.rt_exprs_.at(1).sql_type_);

  // Nested DFO dispatch sizes and writes the decoded, now-owning directory.
  // This public call must succeed, rather than serialize slot 1 as an ordinary
  // expression whose mandatory native metadata has no SQL Type.
  const int64_t second_size = ObPxTreeSerializer::get_serialize_expr_frame_info_size<true>(middle, middle_info);
  ASSERT_GT(second_size, 0);
  std::vector<char> second_wire(second_size);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_expr_frame_info<true>(
      second_wire.data(), second_wire.size(), pos, middle, middle_info));
  ASSERT_EQ(second_size, pos);
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<true>(
      second_wire.data(), second_wire.size(), pos, final_context, final_info));
  ASSERT_EQ(second_size, pos);

  for (int hop = 0; hop < 2; ++hop) {
    SCOPED_TRACE(hop);
    ObExprFrameInfo &info = hop == 0 ? middle_info : final_info;
    ObExecContext &context = hop == 0 ? middle : final_context;
    ASSERT_FALSE(info.is_mark_serialize());
    ASSERT_EQ(6, info.rt_exprs_.count());
    ObExpr &mode = info.rt_exprs_.at(0), &empty = info.rt_exprs_.at(1);
    ObExpr &parameter = info.rt_exprs_.at(2), &sum = info.rt_exprs_.at(3);
    ObExpr &outer = info.rt_exprs_.at(4), &typed_null = info.rt_exprs_.at(5);
    EXPECT_EQ(T_INVALID, empty.type_); EXPECT_EQ(nullptr, empty.sql_type_);
    EXPECT_EQ(0U, empty.arg_cnt_); EXPECT_EQ(0U, empty.parent_cnt_);
    ASSERT_EQ(2U, sum.arg_cnt_); ASSERT_EQ(2U, outer.arg_cnt_);
    ASSERT_EQ(2U, parameter.parent_cnt_); ASSERT_EQ(1U, sum.parent_cnt_);
    EXPECT_EQ(&parameter, sum.args_[0]); EXPECT_EQ(&mode, sum.args_[1]);
    EXPECT_EQ(&sum, outer.args_[0]); EXPECT_EQ(&parameter, outer.args_[1]);
    EXPECT_EQ(&sum, parameter.parents_[0]); EXPECT_EQ(&empty, parameter.parents_[1]);
    EXPECT_EQ(&outer, sum.parents_[0]);
    ASSERT_NE(nullptr, parameter.sql_type_);
    EXPECT_TRUE(parameters.get_native_parameter_type(0)->equals(parameter.get_sql_type()));
    ASSERT_TRUE(context.get_physical_plan_ctx()->native_parameters_ready());
    ASSERT_NE(nullptr, materialized_parameter(*context.get_physical_plan_ctx(), 0));
    EXPECT_EQ(INT64_MIN, read_parameter_value(*context.get_physical_plan_ctx(), 0).get_int());
    ObEvalCtx eval(context);
    EXPECT_EQ(1, mode.locate_expr_datum(eval).get_int());
    EXPECT_EQ(T_NULL, typed_null.type_);
    ASSERT_NE(nullptr, typed_null.sql_type_);
    EXPECT_TRUE(parameters.get_native_parameter_type(0)->equals(typed_null.get_sql_type()));
    EXPECT_TRUE(typed_null.locate_expr_datum(eval).is_null());
  }
}

TEST(NativeFrameWire, px_nonplaceholder_invalid_native_metadata_is_still_rejected)
{
  ObArenaAllocator owner("PxBadNative");
  ObSqlCtx sql_context;
  ObExecContext context(owner);
  ASSERT_NO_FATAL_FAILURE(setup_context(context, sql_context));
  ASSERT_EQ(OB_SUCCESS, context.get_physical_plan_ctx()->init_datum_param_store());
  ObExprFrameInfo info(owner);
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.prepare_allocate(1));
  ObExpr &expr = info.rt_exprs_.at(0);
  expr.type_ = T_INT;
  expr.obj_meta_.set_int();
  expr.datum_meta_ = ObDatumMeta(ObIntType, CS_TYPE_BINARY, 0, 20);
  std::vector<char> wire(4096);
  int64_t pos = 7;
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObPxTreeSerializer::serialize_expr_frame_info<true>(
      wire.data(), wire.size(), pos, context, info));
  EXPECT_EQ(7, pos);

  // A Type-bearing T_INVALID is not an empty PX slot. Deliberately corrupt
  // its comparison count to exercise the existing mandatory metadata guard.
  SqlType integer;
  ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, integer));
  ASSERT_EQ(OB_SUCCESS, expr.bind_sql_type(integer, owner));
  expr.type_ = T_INVALID;
  expr.native_cmp_op_count_ = -1;
  pos = 7;
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObPxTreeSerializer::serialize_expr_frame_info<true>(
      wire.data(), wire.size(), pos, context, info));
  EXPECT_EQ(7, pos);
}


TEST(NativeFrameValueWire, inline_extended_array_envelopes_match_both_existing_wire_versions)
{
  ObArenaAllocator owner;
  SqlType integer; ASSERT_NO_FATAL_FAILURE(scalar_type(ObIntType, integer));
  SqlTypeSpec spec; spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  spec.form = SqlTypeForm::Array; spec.element = &integer;
  const SqlType *type = nullptr; ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  auto *array = DatumParameterArray::alloc(owner, 1); ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(INT64_MIN, owner));
  ASSERT_EQ(OB_SUCCESS, array->set_element_type_borrowed(0, integer, owner));
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    Datum input; input.set_inline_extended(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY,
        123, CS_LEVEL_COERCIBLE, CS_TYPE_BINARY, legacy16);
    char wrapper[16]; ASSERT_EQ(OB_SUCCESS, input.write_inline_extended_payload(wrapper, sizeof(wrapper)));
    Datum old; ASSERT_EQ(OB_SUCCESS, old.set_reference(wrapper, legacy16 ? 16 : 15, Datum::LegacyExtended));
    for (int64_t version : {NativeFrameValueCodec::PHYSICAL_ARRAY_VERSION, NativeFrameValueCodec::TYPED_ARRAY_VERSION}) {
      SCOPED_TRACE(version);
      int64_t old_size = 0, size = 0;
      ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::size(*type, old, old_size, version));
      ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::size(*type, input, size, version)); EXPECT_EQ(old_size, size);
      std::vector<char> before(old_size), after(size); int64_t a = 0, b = 0;
      ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::encode(*type, old, before.data(), before.size(), a, version));
      ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::encode(*type, input, after.data(), after.size(), b, version));
      EXPECT_EQ(before, after); EXPECT_EQ(a, b); expect_address_absent(after, array);
      Datum decoded; const SqlType *decoded_type = nullptr; b = 0;
      ASSERT_EQ(OB_SUCCESS, NativeFrameValueCodec::decode(after.data(), after.size(), b, owner, decoded_type, decoded, version));
      EXPECT_TRUE(type->equals(*decoded_type)); EXPECT_EQ(size, b);
      const auto *decoded_array = reinterpret_cast<const DatumParameterArray *>(decoded.get_ext());
      ASSERT_NE(nullptr, decoded_array); EXPECT_NE(array, decoded_array); ASSERT_EQ(1, decoded_array->count_);
      EXPECT_EQ(INT64_MIN, decoded_array->data_[0].get_int()); EXPECT_EQ(123, decoded.get_extend_size());
      EXPECT_EQ(CS_LEVEL_COERCIBLE, decoded.get_extend_collation_level());
      for (int64_t length = 0; length < size; ++length) {
        Datum destination; destination.set_int(91); const SqlType *sentinel_type = type; int64_t pos = 0;
        EXPECT_NE(OB_SUCCESS, NativeFrameValueCodec::decode(after.data(), length, pos, owner, sentinel_type, destination, version));
        EXPECT_EQ(0, pos); EXPECT_EQ(type, sentinel_type); EXPECT_EQ(91, destination.get_int());
      }
    }
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
