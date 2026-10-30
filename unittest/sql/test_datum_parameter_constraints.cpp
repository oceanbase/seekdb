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

#include <memory>
#include <vector>
#include <gtest/gtest.h>
#include "sql/ob_sql_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/rewrite/ob_query_range_define.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace {
int match(DatumParameter &value, PreCalcExprExpectResult expected, bool &matches)
{
  ObArenaAllocator allocator;
  std::unique_ptr<ObExecContext> context(new ObExecContext(allocator));
  ObPreCalcExprConstraint constraint(allocator); constraint.expect_result_ = expected;
  return constraint.check_is_match(value, *context, matches);
}
void expect_match(DatumParameter &value, PreCalcExprExpectResult expected, bool matches)
{
  bool actual = !matches;
  ASSERT_EQ(OB_SUCCESS, match(value, expected, actual));
  EXPECT_EQ(matches, actual) << "constraint=" << expected;
}
}

TEST(DatumParameterConstraints, null_boolean_never_reads_a_payload)
{
  DatumParameter value; SqlTypeMeta meta; meta.set_int(); value.set_type_meta(meta);
  value.datum_.ptr_ = nullptr; value.datum_.set_null();
  expect_match(value, PRE_CALC_RESULT_NULL, true);
  expect_match(value, PRE_CALC_RESULT_NOT_NULL, false);
  expect_match(value, PRE_CALC_RESULT_TRUE, false);
  expect_match(value, PRE_CALC_RESULT_FALSE, false);
  expect_match(value, PRE_CALC_PRECISE, false);
  expect_match(value, PRE_CALC_NOT_PRECISE, true);
  expect_match(value, PRE_CALC_RESULT_NO_WILDCARD, true);
}

TEST(DatumParameterConstraints, signed_boolean_zero_and_nonzero)
{
  char bytes[8]; DatumParameter value; SqlTypeMeta meta; meta.set_int();
  value.set_type_meta(meta); value.datum_.ptr_ = bytes;
  for (int64_t n : {-17LL, 0LL, 1LL, 9223372036854775807LL}) {
    value.datum_.set_int(n);
    expect_match(value, PRE_CALC_RESULT_NULL, false);
    expect_match(value, PRE_CALC_RESULT_NOT_NULL, true);
    expect_match(value, PRE_CALC_RESULT_TRUE, n != 0);
    expect_match(value, PRE_CALC_RESULT_FALSE, n == 0);
  }
}

TEST(DatumParameterConstraints, empty_null_and_nonempty_string_are_distinct)
{
  DatumParameter value; SqlTypeMeta meta; meta.set_varchar();
  meta.set_collation_type(CS_TYPE_UTF8MB4_BIN); value.set_type_meta(meta);
  value.datum_.set_string(nullptr, 0);
  expect_match(value, PRE_CALC_RESULT_NULL, false);
  expect_match(value, PRE_CALC_PRECISE, true);
  expect_match(value, PRE_CALC_RESULT_NO_WILDCARD, true);
  value.datum_.set_null();
  expect_match(value, PRE_CALC_RESULT_NULL, true);
  expect_match(value, PRE_CALC_PRECISE, false);
  expect_match(value, PRE_CALC_NOT_PRECISE, true);
}

TEST(DatumParameterConstraints, wildcard_check_handles_binary_nul_and_unicode)
{
  DatumParameter value; SqlTypeMeta meta; meta.set_varbinary(); value.set_type_meta(meta);
  struct Case { const char *bytes; uint32_t length; bool no_wildcard; };
  const Case cases[] = {{"plain", 5, true}, {"a%b", 3, false}, {"a_b", 3, false},
                        {"a\\b", 3, false}, {"\0%", 2, false}, {"\0x", 2, true},
                        {"中文", 6, true}};
  for (const auto &c : cases) {
    value.datum_.set_string(c.bytes, c.length);
    expect_match(value, PRE_CALC_RESULT_NO_WILDCARD, c.no_wildcard);
  }
}

TEST(DatumParameterConstraints, lob_null_and_value_preserve_old_constraint_results)
{
  DatumParameter value; SqlTypeMeta meta; meta.set_blob(); meta.set_has_lob_header();
  value.set_type_meta(meta);
  value.datum_.set_null();
  expect_match(value, PRE_CALC_RESULT_NO_WILDCARD, true);
  value.datum_.set_string("", 0);
  expect_match(value, PRE_CALC_RESULT_NO_WILDCARD, false);
}

// A literal without wildcards still needs the LIKE filter for trailing-space
// semantics; an empty string is the existing special precise case.
TEST(DatumParameterConstraints, like_precision_keeps_prefix_and_gap_rules)
{
  DatumParameter value; SqlTypeMeta meta; meta.set_varchar();
  meta.set_collation_type(CS_TYPE_UTF8MB4_BIN); value.set_type_meta(meta);
  struct Case { const char *pattern; bool precise; };
  for (const Case &c : {Case{"literal", false}, Case{"prefix%", true},
                         Case{"gap%tail", false}, Case{"a_b", false}}) {
    value.datum_.set_string(c.pattern, strlen(c.pattern));
    expect_match(value, PRE_CALC_PRECISE, c.precise);
    expect_match(value, PRE_CALC_NOT_PRECISE, !c.precise);
  }
}

TEST(DatumParameterConstraints, legacy_input_keeps_full_metadata_without_reverse_object)
{
  ObArenaAllocator allocator;
  DatumParameter source; source.set_null();
  SqlTypeMeta declared; declared.set_varbinary();
  declared.set_collation_level(CS_LEVEL_COERCIBLE); source.set_param_meta(declared);
  ObAccuracy accuracy; accuracy.set_accuracy(0x123456789abcdef0ULL);
  source.set_accuracy(accuracy); source.set_raw_text_info(35, 71);
  ParamFlag flags; flags.flag_ = 0xa5; flags.extend_flag_ = 0x5a;
  source.set_param_flag(flags); source.set_result_flag(0xfedcba98U);
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, value.alloc_datum_reserved_buff(source.get_meta(), source.get_precision(), allocator));
  ASSERT_EQ(OB_SUCCESS, bind_datum_parameter(source, value, &allocator));
  EXPECT_TRUE(value.is_null());
  EXPECT_EQ(source.get_meta(), value.get_type_meta());
  EXPECT_EQ(source.get_param_meta(), value.get_param_meta());
  EXPECT_EQ(source.get_accuracy(), value.get_accuracy());
  EXPECT_EQ(source.get_result_flag(), value.get_result_flag());
  EXPECT_EQ(flags.flag_, value.get_param_flag().flag_);
  EXPECT_EQ(flags.extend_flag_, value.get_param_flag().extend_flag_);
  EXPECT_EQ(35, value.get_raw_text_pos()); EXPECT_EQ(71, value.get_raw_text_len());
}

TEST(DatumParameterConstraints, numeric_payload_is_contiguous_and_not_a_legacy_object)
{
  ObArenaAllocator allocator; number::ObNumber number;
  ASSERT_EQ(OB_SUCCESS, number.from("123456789012345678901234567890.123456789", allocator));
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, source.set_number(number, allocator)); source.set_precision(65); source.set_scale(30);
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, value.alloc_datum_reserved_buff(source.get_meta(), source.get_precision(), allocator));
  ASSERT_EQ(OB_SUCCESS, bind_datum_parameter(source, value, &allocator));
  EXPECT_EQ(ObNumberType, value.get_type()); EXPECT_EQ(65, value.get_precision());
  EXPECT_EQ(30, value.get_scale());
  EXPECT_EQ(sizeof(number.get_desc()) + number.get_length() * sizeof(uint32_t), value.datum_.len_);
  EXPECT_EQ(0, memcmp(value.datum_.get_number_digits(), number.get_digits(), number.get_length() * sizeof(uint32_t)));
}

TEST(DatumParameterConstraints, scalar_input_envelope_shares_canonical_native_array)
{
  ObArenaAllocator allocator;
  DatumParameterArray *array = DatumParameterArray::alloc(allocator, 2);
  ASSERT_NE(nullptr, array);
  array->element_.set_obj_type(ObIntType);
  DatumParameter integer, text;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(123, allocator)); integer.set_precision(19); integer.set_result_flag(ZEROFILL_FLAG); integer.set_raw_text_info(17, 3);
  text.set_varchar_view(ObString("bytes")); text.set_length(37); text.set_collation_type(CS_TYPE_UTF8MB4_BIN); text.set_param_meta(text.get_meta());
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(integer, array->data_[0], allocator));
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(text, array->data_[1], allocator));
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, allocator));
  DatumParameter value;
  ASSERT_EQ(OB_SUCCESS, value.alloc_datum_reserved_buff(source.get_meta(), source.get_precision(), allocator));
  ASSERT_EQ(OB_SUCCESS, bind_datum_parameter(source, value, &allocator));
  EXPECT_EQ(array, value.get_array<DatumParameterArray>());
  EXPECT_EQ(123, array->data_[0].datum_.get_int()); EXPECT_EQ(ObIntType, array->data_[0].get_type());
  EXPECT_EQ(19, array->data_[0].get_precision()); EXPECT_TRUE(array->data_[0].has_result_flag(ZEROFILL_FLAG));
  EXPECT_EQ(17, array->data_[0].get_raw_text_pos()); EXPECT_EQ(ObVarcharType, array->data_[1].get_type());
  EXPECT_EQ(37, array->data_[1].get_length()); EXPECT_EQ(text.get_meta(), array->data_[1].get_param_meta());
  EXPECT_EQ(ObString("bytes"), array->data_[1].datum_.get_string());
}

TEST(DatumParameterConstraints, batch_frames_select_full_native_array_values)
{
  ObArenaAllocator allocator;
  DatumParameterArray *array = DatumParameterArray::alloc(allocator, 3);
  ASSERT_NE(nullptr, array);
  DatumParameter integer, text; ASSERT_EQ(OB_SUCCESS, integer.set_int(123, allocator)); text.set_varchar_view(ObString("bytes"));
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(integer, array->data_[0], allocator));
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(text, array->data_[1], allocator));
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, allocator));
  ObPhysicalPlanCtx context(allocator);
  ASSERT_EQ(OB_SUCCESS, context.get_param_store_for_update().push_back(source));
  ASSERT_EQ(OB_SUCCESS, context.init_datum_param_store());
  EXPECT_NE(array, context.get_datum_param_store().at(0).get_array<DatumParameterArray>());
  array->data_[0].datum_.set_int(999);
  array->data_[1].set_null();
  ASSERT_EQ(1, context.get_param_frame_ptrs().count());
  ObDatum *frame = reinterpret_cast<ObDatum *>(context.get_param_frame_ptrs().at(0));
  ASSERT_EQ(OB_SUCCESS, context.replace_batch_param_datum(0, 0, 1)); EXPECT_EQ(123, frame->get_int());
  ASSERT_EQ(OB_SUCCESS, context.replace_batch_param_datum(1, 0, 1)); EXPECT_EQ(ObString("bytes"), frame->get_string());
  ASSERT_EQ(OB_SUCCESS, context.replace_batch_param_datum(2, 0, 1)); EXPECT_TRUE(frame->is_null());
  EXPECT_EQ(OB_ERR_UNEXPECTED, context.replace_batch_param_datum(-1, 0, 1));
  EXPECT_EQ(OB_ERR_UNEXPECTED, context.replace_batch_param_datum(3, 0, 1));
}

TEST(DatumParameterConstraints, nested_array_envelope_preserves_native_descriptors)
{
  ObArenaAllocator allocator;
  DatumParameterArray *inner = DatumParameterArray::alloc(allocator, 1);
  DatumParameterArray *outer = DatumParameterArray::alloc(allocator, 1);
  ASSERT_NE(nullptr, inner); ASSERT_NE(nullptr, outer);
  DatumParameter integer; ASSERT_EQ(OB_SUCCESS, integer.set_uint64(UINT64_MAX, allocator)); integer.set_precision(20);
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(integer, inner->data_[0], allocator));
  DatumParameter child; ASSERT_EQ(OB_SUCCESS, child.set_extend(reinterpret_cast<int64_t>(inner), T_EXT_SQL_ARRAY, allocator));
  ASSERT_EQ(OB_SUCCESS, outer->data_[0].alloc_datum_reserved_buff(child.get_meta(), -1, allocator));
  ASSERT_EQ(OB_SUCCESS, bind_datum_parameter(child, outer->data_[0], &allocator));
  DatumParameter source; ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(outer), T_EXT_SQL_ARRAY, allocator));
  DatumParameter destination;
  ASSERT_EQ(OB_SUCCESS, destination.alloc_datum_reserved_buff(source.get_meta(), -1, allocator));
  ASSERT_EQ(OB_SUCCESS, bind_datum_parameter(source, destination, &allocator));
  const DatumParameterArray *native_outer = destination.get_array<DatumParameterArray>();
  ASSERT_EQ(outer, native_outer);
  const DatumParameterArray *native_inner = native_outer->data_[0].get_array<DatumParameterArray>();
  ASSERT_EQ(inner, native_inner); ASSERT_EQ(1, native_inner->count_);
  EXPECT_EQ(UINT64_MAX, native_inner->data_[0].datum_.get_uint());
  EXPECT_EQ(20, native_inner->data_[0].get_precision()); EXPECT_EQ(ObUInt64Type, native_inner->data_[0].get_type());
}

TEST(DatumParameterConstraints, invalid_constraint_reports_an_error)
{
  DatumParameter value; value.datum_.set_null(); bool matches = false;
  EXPECT_EQ(OB_ERR_UNEXPECTED, match(value, PRE_CALC_RESULT_NONE, matches));
}

TEST(DatumParameterConstraints, context_serialization_decodes_canonical_arrays_in_its_own_arena)
{
  ObArenaAllocator source_arena, output_arena;
  DatumParameterArray *array = DatumParameterArray::alloc(source_arena, 2); ASSERT_NE(nullptr, array);
  char bytes[] = "persisted array";
  DatumParameter text, integer; text.set_varchar_view(ObString(sizeof(bytes)-1, bytes)); text.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  ASSERT_EQ(OB_SUCCESS, integer.set_uint64(UINT64_MAX, source_arena)); integer.set_precision(20);
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(text, array->data_[0], source_arena));
  ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(integer, array->data_[1], source_arena));
  DatumParameter envelope; ASSERT_EQ(OB_SUCCESS, envelope.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_arena));
  ObPhysicalPlanCtx source(source_arena), output(output_arena);
  ASSERT_EQ(OB_SUCCESS, source.get_param_store_for_update().push_back(envelope));
  ASSERT_EQ(OB_SUCCESS, source.get_array_param_groups().push_back(ObArrayParamGroup(2, 1, 0)));
  std::vector<char> wire(source.get_serialize_size()); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(wire.data(), wire.size(), pos)); EXPECT_EQ(wire.size(), pos);
  pos = 0; ASSERT_EQ(OB_SUCCESS, output.deserialize(wire.data(), wire.size(), pos)); EXPECT_EQ(wire.size(), pos);
  std::vector<char> repeated(output.get_serialize_size()); pos = 0;
  ASSERT_EQ(OB_SUCCESS, output.serialize(repeated.data(), repeated.size(), pos)); EXPECT_EQ(wire, repeated);
  memset(wire.data(), 0xa5, wire.size()); memset(bytes, 'x', sizeof(bytes));
  ASSERT_EQ(1, output.get_datum_param_store().count());
  auto *owned = output.get_datum_param_store().at(0).get_array<DatumParameterArray>();
  ASSERT_NE(nullptr, owned); EXPECT_NE(array, owned); ASSERT_EQ(2, owned->count_);
  EXPECT_EQ(ObString("persisted array"), owned->data_[0].datum_.get_string());
  EXPECT_EQ(UINT64_MAX, owned->data_[1].datum_.get_uint()); EXPECT_EQ(20, owned->data_[1].get_precision());
  EXPECT_EQ(reinterpret_cast<int64_t>(owned), output.get_param_store().at(0).get_ext());
}

TEST(DatumParameterConstraints, owned_scalar_import_does_not_borrow_temporary_strings)
{
  ObArenaAllocator arena; char bytes[] = "utf8 bytes";
  DatumParameter source; source.set_varchar_view(ObString(sizeof(bytes)-1, bytes)); source.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  source.set_length(77); source.set_result_flag(ZEROFILL_FLAG); source.set_raw_text_info(3, 7); source.set_param_meta(source.get_meta());
  DatumParameter native; ASSERT_EQ(OB_SUCCESS, clone_datum_parameter(source, native, arena));
  memset(bytes, 'x', sizeof(bytes)); EXPECT_EQ(ObString("utf8 bytes"), native.datum_.get_string());
  DatumParameter result; ASSERT_EQ(OB_SUCCESS, assign_datum_parameter(native, result));
  EXPECT_EQ(native.datum_.get_string(), result.get_string()); EXPECT_EQ(source.get_accuracy(), result.get_accuracy());
  EXPECT_EQ(source.get_result_flag(), result.get_result_flag()); EXPECT_EQ(source.get_param_meta(), result.get_param_meta());
  EXPECT_EQ(3, result.get_raw_text_pos()); EXPECT_EQ(7, result.get_raw_text_len());
}

TEST(DatumParameterConstraints, context_rejects_trailing_bytes_in_deferred_native_array)
{
  ObArenaAllocator arena; auto *array = DatumParameterArray::alloc(arena, 0); ASSERT_NE(nullptr, array);
  DatumParameter envelope; ASSERT_EQ(OB_SUCCESS, envelope.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, arena));
  ObPhysicalPlanCtx source(arena), output(arena);
  ASSERT_EQ(OB_SUCCESS, source.get_param_store_for_update().push_back(envelope));
  ASSERT_EQ(OB_SUCCESS, source.get_array_param_groups().push_back(ObArrayParamGroup(0, 1, 0)));
  std::vector<char> wire(source.get_serialize_size()); int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, source.serialize(wire.data(), wire.size(), pos));
  int64_t cursor = 0, version = 0, payload_length = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, version));
  const int64_t outer_length_pos = cursor;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, payload_length));
  // The remaining legacy scalar envelope is metadata, zero marker, body size.
  const unsigned char marker[] = {25, 7, 0, 100, 0};
  int64_t body_length_pos = -1;
  for (int64_t i = cursor; i + sizeof(marker) < wire.size(); ++i) {
    if (memcmp(wire.data() + i, marker, sizeof(marker)) == 0) { ASSERT_EQ(-1, body_length_pos); body_length_pos = i + sizeof(marker); }
  }
  ASSERT_GE(body_length_pos, 0); cursor = body_length_pos; int64_t body_length = 0;
  ASSERT_EQ(OB_SUCCESS, serialization::decode(wire.data(), wire.size(), cursor, body_length));
  ASSERT_EQ(serialization::encoded_length(body_length), serialization::encoded_length(body_length + 1));
  wire.insert(wire.begin() + cursor + body_length, '\x55');
  cursor = body_length_pos;
  ASSERT_EQ(OB_SUCCESS, serialization::encode(wire.data(), wire.size(), cursor, body_length + 1));
  cursor = outer_length_pos;
  ASSERT_EQ(OB_SUCCESS, serialization::encode_fixed_bytes_i64(wire.data(), wire.size(), cursor, payload_length + 1));
  pos = 0; EXPECT_EQ(OB_DESERIALIZE_ERROR, output.deserialize(wire.data(), wire.size(), pos));
}

TEST(DatumParameterConstraints, scalar_array_boundary_preserves_full_metadata_and_runtime_flags)
{
  ObArenaAllocator arena; auto *array = DatumParameterArray::alloc(arena, 0); ASSERT_NE(nullptr, array);
  DatumParameter source; SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY); meta.set_cs_level(0xa7);
  source.set_type_meta(meta); source.set_param_meta(meta); source.set_raw_text_info(17, 19);
  source.set_result_flags(0x1234); source.get_param_flag().flag_ = 0xa5;
  ASSERT_EQ(OB_SUCCESS, source.alloc_datum_reserved_buff(meta, -1, arena)); source.set_array_address(array);
  DatumParameter output; ASSERT_EQ(OB_SUCCESS, assign_datum_parameter(source, output));
  EXPECT_EQ(0, memcmp(&meta, &output.get_meta(), sizeof(meta))); EXPECT_EQ(meta, output.get_param_meta());
  EXPECT_EQ(reinterpret_cast<int64_t>(array), output.get_ext()); EXPECT_EQ(0x1234, output.get_result_flag());
  EXPECT_EQ(0xa5, output.get_param_flag().flag_); EXPECT_EQ(17, output.get_raw_text_pos()); EXPECT_EQ(19, output.get_raw_text_len());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
