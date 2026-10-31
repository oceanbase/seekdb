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
#include "sql/ob_sql_context.h"
#include <gtest/gtest.h>
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase { namespace sql {
int eval_question_mark_func(const ObExpr &, ObEvalCtx &, Datum &);
} }

namespace
{
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
  const int64_t count = plan.get_param_store().count();
  ASSERT_EQ(OB_SUCCESS, info.rt_exprs_.prepare_allocate(1));
  ObExpr &expr = info.rt_exprs_.at(0);
  expr.type_ = T_QUESTIONMARK;
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
    const Datum *value = destination_plan.get_native_parameter(i);
    ASSERT_NE(nullptr, value); ASSERT_NE(nullptr, destination_plan.get_native_parameter_type(i));
    if (i == 1) { EXPECT_TRUE(value->is_null()); }
    else {
      EXPECT_EQ(i == 0 ? INT64_MIN : i + 71, value->get_int());
      ObDatum borrowed;
      ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(*destination_plan.get_native_parameter_type(i)).borrow_legacy(*value, borrowed));
      EXPECT_EQ(borrowed.ptr_, destination_plan.get_param_store().at(i).datum_.ptr_);
    }
  }
  ObEvalCtx eval(destination);
  Datum questionmark;
  ASSERT_EQ(OB_SUCCESS, eval_question_mark_func(destination_info.rt_exprs_.at(1), eval, questionmark));
  EXPECT_EQ(count - 1 + 71, questionmark.get_int());
  for (int64_t page = 0; page < source_plan.get_param_frame_ptrs().count(); ++page) {
    const int64_t slots = page == 0 ? original : count - original;
    MEMSET(source_plan.get_param_frame_ptrs().at(page), 0x6c, slots * 24);
  }
  source_plan.reset_datum_param_store();
  MEMSET(wire.data(), 0xb7, wire.size());
  EXPECT_EQ(INT64_MIN, destination_plan.get_native_parameter(0)->get_int());
  EXPECT_EQ(count - 1 + 71, destination_plan.get_native_parameter(count - 1)->get_int());
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
  const Datum *before = destination.get_physical_plan_ctx()->get_native_parameter(0);
  char **old_frames = destination.get_frames();
  const int64_t size = ObPxTreeSerializer::get_serialize_expr_frame_info_size<false>(source, info);
  ASSERT_GT(size, 0);
  std::vector<char> wire(size);
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, ObPxTreeSerializer::serialize_expr_frame_info<false>(wire.data(), size, pos, source, info));
  pos = 0;
  EXPECT_NE(OB_SUCCESS, ObPxTreeSerializer::deserialize_expr_frame_info<false>(wire.data(), size - 1, pos, destination, info));
  EXPECT_EQ(0, pos); EXPECT_EQ(old_frames, destination.get_frames());
  EXPECT_EQ(before, destination.get_physical_plan_ctx()->get_native_parameter(0));
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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
