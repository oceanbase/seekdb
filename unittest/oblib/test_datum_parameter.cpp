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

#include <cstring>
#include <string>
#include <vector>
#include <gtest/gtest.h>
#include "common/datum/datum_parameter.h"
#include "lib/allocator/ob_allocator.h"

using namespace oceanbase::common;

namespace {
class RejectAllocator : public ObIAllocator
{
public:
  void *alloc(int64_t) override { ++allocations_; return nullptr; }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *) override {}
  int64_t allocations_ = 0;
};

class CountingAllocator : public ObIAllocator
{
public:
  void *alloc(int64_t size) override { ++allocations_; return arena_.alloc(size); }
  void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
  void free(void *pointer) override { arena_.free(pointer); }
  int64_t allocations_ = 0;
private:
  ObArenaAllocator arena_;
};
}

TEST(DatumParameter, keeps_null_type_accuracy_flags_and_source_position_independent)
{
  DatumParameter parameter;
  EXPECT_TRUE(parameter.is_null());
  SqlTypeMeta actual, declared;
  actual.set_null(); declared.set_varchar();
  declared.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  declared.set_collation_level(CS_LEVEL_COERCIBLE);
  parameter.set_type_meta(actual); parameter.set_param_meta(declared);
  parameter.datum_.set_null();
  ObAccuracy accuracy; accuracy.set_accuracy(0x123456789abcdef0ULL);
  parameter.set_accuracy(accuracy);
  ParamFlag flags; flags.flag_ = 0xa5; flags.extend_flag_ = 0x5a;
  parameter.set_param_flag(flags); parameter.set_result_flags(0xfedcba98);
  parameter.set_raw_text_info(31, 97);
  DatumParameter copy(parameter);
  EXPECT_TRUE(copy.is_null()); EXPECT_EQ(ObNullType, copy.get_type());
  EXPECT_EQ(declared, copy.get_param_meta());
  EXPECT_EQ(accuracy, copy.get_accuracy());
  EXPECT_EQ(0xa5, copy.get_param_flag().flag_);
  EXPECT_EQ(0x5a, copy.get_param_flag().extend_flag_);
  EXPECT_EQ(0xfedcba98U, copy.get_result_flag());
  EXPECT_EQ(31, copy.get_raw_text_pos()); EXPECT_EQ(97, copy.get_raw_text_len());
  copy.reset(); EXPECT_EQ(-1, copy.get_raw_text_pos());
  EXPECT_EQ(0U, copy.get_result_flag()); EXPECT_TRUE(copy.get_param_meta().is_null());
}

TEST(DatumParameter, complete_lob_metadata_survives_copy)
{
  DatumParameter parameter;
  SqlTypeMeta meta; meta.set_blob(); meta.set_has_lob_header();
  parameter.set_type_meta(meta);
  DatumParameter copy(parameter);
  EXPECT_EQ(meta, copy.get_type_meta());
  EXPECT_TRUE(copy.has_lob_header());
  EXPECT_EQ(CS_TYPE_BINARY, copy.get_collation_type());
  EXPECT_EQ(CS_LEVEL_IMPLICIT, copy.get_collation_level());
}

TEST(DatumParameter, deep_copy_outlives_payload_arena)
{
  DatumParameter destination;
  std::string expected("\0unicode\xe4\xb8\xad\xe6\x96\x87\xff", 16);
  ObArenaAllocator destination_arena;
  {
    ObArenaAllocator source_arena;
    DatumParameter source;
    SqlTypeMeta meta; meta.set_varbinary(); source.set_type_meta(meta);
    char *payload = static_cast<char *>(source_arena.alloc(expected.size()));
    ASSERT_NE(nullptr, payload); memcpy(payload, expected.data(), expected.size());
    source.datum_.set_string(payload, expected.size());
    source.set_raw_text_info(3, 16);
    ASSERT_EQ(OB_SUCCESS, destination.deep_copy(source, destination_arena));
    EXPECT_NE(source.datum_.get_reference(), destination.datum_.get_reference());
    memset(payload, 0x5a, expected.size());
  }
  EXPECT_EQ(expected.size(), destination.datum_.length());
  EXPECT_EQ(0, memcmp(expected.data(), destination.datum_.get_reference(), expected.size()));
  EXPECT_EQ(3, destination.get_raw_text_pos());
}

TEST(DatumParameter, container_reallocation_never_points_into_moved_parameter)
{
  ObArenaAllocator arena;
  std::vector<DatumParameter> parameters;
  for (int64_t i = 0; i < 513; ++i) {
    DatumParameter parameter; SqlTypeMeta meta; meta.set_int();
    parameter.set_type_meta(meta);
    ASSERT_EQ(OB_SUCCESS, parameter.set_int(i, arena)); parameters.push_back(parameter);
  }
  for (int64_t i = 0; i < 513; ++i) {
    EXPECT_EQ(i, parameters[i].datum_.get_int());
    EXPECT_EQ(ObIntType, parameters[i].get_type());
  }
}

TEST(DatumParameter, failed_clone_is_atomic_and_self_clone_owns_new_bytes)
{
  ObArenaAllocator arena;
  RejectAllocator reject;
  DatumParameter source, destination;
  ASSERT_EQ(OB_SUCCESS, source.set_varchar(ObString::make_string("123"), arena));
  ASSERT_EQ(OB_SUCCESS, destination.set_varchar(ObString::make_string("456"), arena));
  const void *old = destination.datum_.get_reference();
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, destination.deep_copy(source, reject));
  EXPECT_EQ(1, reject.allocations_);
  EXPECT_EQ(old, destination.datum_.get_reference()); EXPECT_EQ(ObString("456"), destination.get_string());
  old = source.datum_.get_reference();
  ASSERT_EQ(OB_SUCCESS, source.deep_copy(source, arena));
  EXPECT_NE(old, source.datum_.get_reference()); EXPECT_EQ(ObString("123"), source.get_string());
  // The former integer OOM seam disappeared: an actual inline scalar clone
  // succeeds without calling even an allocator that rejects every request.
  DatumParameter integer, integer_copy;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(123, reject));
  ASSERT_EQ(OB_SUCCESS, integer_copy.set_int(456, reject));
  ASSERT_EQ(OB_SUCCESS, integer_copy.deep_copy(integer, reject));
  EXPECT_EQ(123, integer_copy.get_int()); EXPECT_EQ(1, reject.allocations_);
}

TEST(DatumParameter, array_payload_copy_keeps_pointee_ownership_explicit)
{
  struct Array { int count; } array{7};
  DatumParameter source, copy;
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  source.set_type_meta(meta);
  ObArenaAllocator arena;
  ASSERT_EQ(OB_SUCCESS, source.set_array_address(&array, arena));
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(source, arena));
  EXPECT_EQ(&array, copy.get_array<Array>());
  EXPECT_NE(source.datum_.get_reference(), copy.datum_.get_reference());
  array.count = 11; EXPECT_EQ(11, copy.get_array<Array>()->count);
}

TEST(DatumParameter, typed_null_copy_preserves_type_accuracy_and_needs_no_payload)
{
  DatumParameter source, destination;
  ObArenaAllocator previous_owner;
  ASSERT_EQ(OB_SUCCESS, destination.set_int(17, previous_owner));
  SqlTypeMeta meta; meta.set_decimal_int(5);
  source.set_type_meta(meta);
  source.set_precision(20); source.set_scale(5);
  source.set_result_flags(0x1234); source.set_raw_text_info(7, 19);
  source.datum_.set_null();
  CountingAllocator owner;
  ASSERT_EQ(OB_SUCCESS, destination.deep_copy(source, owner));
  EXPECT_TRUE(destination.is_null());
  EXPECT_EQ(ObDecimalIntType, destination.get_type());
  EXPECT_EQ(20, destination.get_precision()); EXPECT_EQ(5, destination.get_scale());
  EXPECT_EQ(0x1234U, destination.get_result_flag());
  EXPECT_EQ(7, destination.get_raw_text_pos()); EXPECT_EQ(19, destination.get_raw_text_len());
  EXPECT_EQ(0, owner.allocations_);

  DatumCopyPlan plan(meta, source.get_accuracy());
  int64_t size = -1;
  ASSERT_EQ(OB_SUCCESS, plan.payload_copy_size(source.datum_, size));
  EXPECT_EQ(0, size);
  RejectAllocator reject;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(source.datum_, destination.datum_, reject));
  ASSERT_EQ(OB_SUCCESS, source.deep_copy(source, reject));
  EXPECT_TRUE(source.is_null()); EXPECT_EQ(ObDecimalIntType, source.get_type());
  EXPECT_EQ(20, source.get_precision()); EXPECT_EQ(5, source.get_scale());
}

TEST(DatumParameter, prepared_payload_copy_owns_scalar_and_borrowed_string_after_source_reset)
{
  ObArenaAllocator input, output;
  DatumParameter integer, text;
  ASSERT_EQ(OB_SUCCESS, integer.set_int(INT64_MIN, input));
  char *borrowed = static_cast<char *>(input.alloc(9));
  ASSERT_NE(nullptr, borrowed);
  const char expected[] = {'a', '\0', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
  memcpy(borrowed, expected, sizeof(expected));
  SqlTypeMeta text_meta; text_meta.set_varchar();
  text.set_type_meta(text_meta); text.set_length(9);
  text.datum_.set_string(borrowed, sizeof(expected));

  DatumCopyPlan integer_plan(integer.get_type_meta(), integer.get_accuracy());
  DatumCopyPlan text_plan(text.get_type_meta(), text.get_accuracy());
  int64_t integer_size = -1, text_size = -1;
  ASSERT_EQ(OB_SUCCESS, integer_plan.payload_copy_size(integer.datum_, integer_size));
  ASSERT_EQ(OB_SUCCESS, text_plan.payload_copy_size(text.datum_, text_size));
  EXPECT_EQ(0, integer_size); EXPECT_EQ(sizeof(expected), text_size);
  Datum owned_integer, owned_text;
  CountingAllocator inline_owner;
  ASSERT_EQ(OB_SUCCESS, integer_plan.deep_copy(integer.datum_, owned_integer, inline_owner));
  EXPECT_EQ(0, inline_owner.allocations_);
  ASSERT_EQ(OB_SUCCESS, text_plan.deep_copy(text.datum_, owned_text, output));
  integer.datum_.set_int(INT64_MAX);
  memset(borrowed, 'x', sizeof(expected));
  input.reset();
  EXPECT_EQ(INT64_MIN, owned_integer.get_int());
  EXPECT_EQ(sizeof(expected), owned_text.length());
  EXPECT_EQ(0, memcmp(expected, owned_text.get_reference(), sizeof(expected)));
}

TEST(DatumParameter, prepared_buffer_copy_and_size_fail_without_publishing_destination)
{
  ObAccuracy accuracy;
  SqlTypeMeta meta; meta.set_int();
  DatumCopyPlan plan(meta, accuracy);
  int64_t payload = INT64_MIN, previous = 17;
  ObDatum source, destination;
  source.ptr_ = reinterpret_cast<char *>(&payload); source.set_int(payload);
  destination.ptr_ = reinterpret_cast<char *>(&previous); destination.set_int(previous);
  const char *old_pointer = destination.ptr_;
  char buffer[16]; memset(buffer, 0x5a, sizeof(buffer));
  int64_t position = 9;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH,
      plan.deep_copy(source, destination, buffer, sizeof(buffer), position));
  EXPECT_EQ(9, position); EXPECT_EQ(old_pointer, destination.ptr_);
  EXPECT_EQ(17, destination.get_int());
  for (char byte : buffer) { EXPECT_EQ(char(0x5a), byte); }

  ObDatum malformed = source; malformed.ptr_ = nullptr;
  int64_t size = 31;
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan.payload_copy_size(malformed, size));
  EXPECT_EQ(31, size);
  RejectAllocator reject;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.deep_copy(source, destination, reject));
  EXPECT_EQ(old_pointer, destination.ptr_); EXPECT_EQ(17, destination.get_int());

  memcpy(buffer + 4, &payload, sizeof(payload));
  source.ptr_ = buffer + 4;
  position = 0;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(source, source, buffer, sizeof(buffer), position));
  EXPECT_EQ(8, position); EXPECT_EQ(buffer, source.ptr_);
  int64_t copied = 0; memcpy(&copied, source.ptr_, sizeof(copied));
  EXPECT_EQ(INT64_MIN, copied);
}

TEST(DatumParameter, unknown_decimal_precision_keeps_legacy_coefficient_width_and_bytes)
{
  ObArenaAllocator output;
  for (uint32_t width : {0U, 4U, 8U, 16U, 32U, 64U}) {
    SCOPED_TRACE(width);
    ObArenaAllocator input;
    char *coefficient = static_cast<char *>(input.alloc(width > 0 ? width : 1));
    ASSERT_NE(nullptr, coefficient);
    for (uint32_t i = 0; i < width; ++i) { coefficient[i] = char(0x80 + i); }
    const std::string expected(coefficient, width);
    DatumParameter source, destination;
    SqlTypeMeta meta; meta.set_decimal_int(7);
    source.set_type_meta(meta); source.set_precision(PRECISION_UNKNOWN_YET); source.set_scale(7);
    ASSERT_EQ(OB_SUCCESS, source.datum_.set_reference(coefficient, width));
    DatumCopyPlan plan(meta, source.get_accuracy());
    EXPECT_EQ(PRECISION_UNKNOWN_YET, plan.get_accuracy().get_precision());
    int64_t bytes = -1;
    ASSERT_EQ(OB_SUCCESS, plan.payload_copy_size(source.datum_, bytes));
    EXPECT_EQ(width, bytes);
    ASSERT_EQ(OB_SUCCESS, destination.deep_copy(source, output));
    memset(coefficient, 0, width); input.reset();
    EXPECT_EQ(PRECISION_UNKNOWN_YET, destination.get_precision());
    EXPECT_EQ(7, destination.get_scale()); EXPECT_EQ(width, destination.datum_.length());
    EXPECT_EQ(0, memcmp(expected.data(), destination.datum_.get_reference(), width));
  }
}

TEST(DatumParameter, prepared_payload_copy_preserves_empty_string_and_range_sentinel)
{
  ObArenaAllocator output;
  SqlTypeMeta meta; meta.set_varchar();
  DatumCopyPlan plan(meta, ObAccuracy());
  ObDatum empty, copy;
  empty.set_string(nullptr, 0);
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(empty, copy, output));
  EXPECT_FALSE(copy.is_null()); EXPECT_EQ(0U, copy.len_);
  EXPECT_NE(nullptr, copy.ptr_);

  char marker_bytes[DATUM_LEGACY_EXTEND_SIZE];
  ObDatum marker; marker.ptr_ = marker_bytes; marker.set_ext_value(DATUM_MIN_EXT_VALUE);
  meta.set_int();
  DatumCopyPlan integer_plan(meta, ObAccuracy());
  ASSERT_EQ(OB_SUCCESS, integer_plan.deep_copy(marker, copy, output));
  EXPECT_TRUE(copy.is_min()); EXPECT_NE(marker.ptr_, copy.ptr_);
}


static_assert(sizeof(DatumParameterMetadata) == 32, "P0 metadata must occupy 32 bytes");
static_assert(alignof(DatumParameterMetadata) == 8, "P0 metadata must be eight-byte aligned");

namespace {
void expect_parameter_metadata_equal(const DatumParameterMetadata &expected,
                                     const DatumParameterMetadata &actual)
{
  EXPECT_EQ(expected.accuracy_.get_accuracy(), actual.accuracy_.get_accuracy());
  EXPECT_EQ(expected.type_meta_, actual.type_meta_);
  EXPECT_EQ(expected.type_meta_.get_scale(), actual.type_meta_.get_scale());
  EXPECT_EQ(expected.res_flags_, actual.res_flags_);
  EXPECT_EQ(expected.raw_text_pos_, actual.raw_text_pos_);
  EXPECT_EQ(expected.raw_text_len_, actual.raw_text_len_);
  EXPECT_EQ(expected.param_meta_, actual.param_meta_);
  EXPECT_EQ(expected.param_meta_.get_scale(), actual.param_meta_.get_scale());
  EXPECT_EQ(expected.flag_.flag_, actual.flag_.flag_);
  EXPECT_EQ(expected.flag_.extend_flag_, actual.flag_.extend_flag_);
}

void expect_default_parameter_metadata(const DatumParameterMetadata &metadata)
{
  EXPECT_EQ(-1, metadata.accuracy_.get_accuracy());
  EXPECT_EQ(ObNullType, metadata.type_meta_.get_type());
  EXPECT_EQ(CS_LEVEL_INVALID, metadata.type_meta_.get_collation_level());
  EXPECT_EQ(CS_TYPE_INVALID, metadata.type_meta_.get_collation_type());
  EXPECT_EQ(-1, metadata.type_meta_.get_scale());
  EXPECT_EQ(0U, metadata.res_flags_);
  EXPECT_EQ(-1, metadata.raw_text_pos_);
  EXPECT_EQ(-1, metadata.raw_text_len_);
  EXPECT_EQ(ObNullType, metadata.param_meta_.get_type());
  EXPECT_EQ(CS_LEVEL_INVALID, metadata.param_meta_.get_collation_level());
  EXPECT_EQ(CS_TYPE_INVALID, metadata.param_meta_.get_collation_type());
  EXPECT_EQ(-1, metadata.param_meta_.get_scale());
  EXPECT_EQ(0x09, metadata.flag_.flag_);
  EXPECT_EQ(0x00, metadata.flag_.extend_flag_);
}
}

TEST(DatumParameter, metadata_assignment_preserves_destination_descriptor_and_payload)
{
  ObArenaAllocator owner;
  DatumParameter source, destination;
  ASSERT_EQ(OB_SUCCESS, source.set_int(INT64_MIN, owner));
  ASSERT_EQ(OB_SUCCESS, destination.set_int(INT64_MAX, owner));
  source.set_accuracy(ObAccuracy(20, 3));
  source.set_scale(3);
  SqlTypeMeta declared; declared.set_int(); declared.set_scale(7);
  source.set_param_meta(declared);
  source.set_result_flags(0xfedcba98U);
  ParamFlag flags; flags.flag_ = 0xa5; flags.extend_flag_ = 0x5a;
  source.set_param_flag(flags);
  source.set_raw_text_info(31, 97);
  const DatumParameterMetadata captured = source.get_metadata();
  const Datum before = destination.datum_;
  ASSERT_NE(source.get_int(), before.get_int());

  destination.set_metadata(captured);
  EXPECT_EQ(before.state(), destination.datum_.state());
  EXPECT_EQ(before.length(), destination.datum_.length());
  EXPECT_EQ(before.reference_flags(), destination.datum_.reference_flags());
  EXPECT_EQ(INT64_MAX, destination.get_int());
  EXPECT_EQ(INT64_MIN, source.get_int());
  expect_parameter_metadata_equal(captured, destination.get_metadata());
  EXPECT_EQ(ObIntType, destination.get_type());
  EXPECT_EQ(20, destination.get_precision());
  EXPECT_EQ(3, destination.get_scale());
  EXPECT_EQ(3, destination.get_type_meta().get_scale());
  EXPECT_EQ(7, destination.get_param_meta().get_scale());
  EXPECT_EQ(0xfedcba98U, destination.get_result_flag());
  EXPECT_EQ(0xa5, destination.get_param_flag().flag_);
  EXPECT_EQ(0x5a, destination.get_param_flag().extend_flag_);
  EXPECT_EQ(31, destination.get_raw_text_pos());
  EXPECT_EQ(97, destination.get_raw_text_len());

  // The metadata bundle is copied; it is not an alias of source metadata.
  source.set_raw_text_info(1, 2); source.set_scale(0);
  source.get_param_flag().extend_flag_ = 0;
  source.datum_.set_int(17);
  expect_parameter_metadata_equal(captured, destination.get_metadata());
  EXPECT_EQ(INT64_MAX, destination.get_int());
  destination.set_metadata(destination.get_metadata());
  EXPECT_EQ(before.state(), destination.datum_.state());
  EXPECT_EQ(before.length(), destination.datum_.length());
  EXPECT_EQ(before.reference_flags(), destination.datum_.reference_flags());
  EXPECT_EQ(INT64_MAX, destination.get_int());
  expect_parameter_metadata_equal(captured, destination.get_metadata());
}

TEST(DatumParameter, metadata_copy_preserves_full_udt_accuracy_and_typed_null_declaration)
{
  const uint64_t declaration_id = 0x123456789abcde01ULL;
  DatumParameter source, destination;
  SqlTypeMeta declared; declared.set_ext();
  source.set_type_meta(declared);
  source.set_param_meta(declared);
  source.set_udt_id(declaration_id);
  source.datum_.set_null();
  source.set_result_flags(0x80000001U);
  source.set_raw_text_info(0x12345678, 0x23456789);
  // Both values are NULL; copying their declaration does not create PL data.
  const Datum before = destination.datum_;
  destination.set_metadata(source.get_metadata());
  EXPECT_TRUE(destination.is_null());
  EXPECT_EQ(before.state(), destination.datum_.state());
  EXPECT_EQ(before.length(), destination.datum_.length());
  EXPECT_EQ(before.reference_flags(), destination.datum_.reference_flags());
  EXPECT_EQ(declaration_id, destination.get_udt_id());
  EXPECT_EQ(declaration_id, static_cast<uint64_t>(destination.get_accuracy().get_accuracy()));
  EXPECT_EQ(ObExtendType, destination.get_type());
  EXPECT_EQ(declared, destination.get_param_meta());
  EXPECT_EQ(declared.get_scale(), destination.get_param_meta().get_scale());
  expect_parameter_metadata_equal(source.get_metadata(), destination.get_metadata());

  // A runtime NULL changes raw type only; the complete declared ID stays intact.
  source.set_null();
  source.set_param_meta();
  destination.set_metadata(source.get_metadata());
  EXPECT_TRUE(destination.is_null());
  EXPECT_EQ(ObNullType, destination.get_type());
  EXPECT_EQ(ObExtendType, destination.get_param_meta().get_type());
  EXPECT_EQ(declared.get_scale(), destination.get_param_meta().get_scale());
  EXPECT_EQ(declaration_id, static_cast<uint64_t>(destination.get_accuracy().get_accuracy()));
  EXPECT_EQ(0x80000001U, destination.get_result_flag());
  EXPECT_EQ(0x12345678, destination.get_raw_text_pos());
  EXPECT_EQ(0x23456789, destination.get_raw_text_len());
}

TEST(DatumParameter, public_metadata_setters_preserve_reserved_flag_bits_and_full_source_positions)
{
  ObArenaAllocator owner;
  DatumParameter parameter;
  ASSERT_EQ(OB_SUCCESS, parameter.set_varchar(ObString::make_string("abc"), owner));
  const Datum before = parameter.datum_;
  parameter.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  parameter.set_collation_level(CS_LEVEL_COERCIBLE);
  parameter.set_length(37); parameter.set_precision(20);
  EXPECT_EQ(20, parameter.get_metadata().accuracy_.get_precision());
  // Precision and length semantics share the original ObAccuracy union slot.
  parameter.set_length_semantics(LS_CHAR); parameter.set_scale(7);
  parameter.set_param_meta();
  parameter.set_result_flags(0x80000001U);
  parameter.set_result_flag(0x00000006U);
  parameter.unset_result_flag(0x00000002U);
  ParamFlag flags; flags.flag_ = 0xf8; flags.extend_flag_ = 0xfe;
  parameter.set_param_flag(flags);
  parameter.set_need_to_check_type(true);
  parameter.set_need_to_check_bool_value(true);
  parameter.set_expected_bool_value(true);
  parameter.get_param_flag().ignore_scale_check_ = true;
  parameter.set_raw_text_info(INT32_MAX - 4, INT32_MAX - 9);
  const DatumParameterMetadata &metadata = parameter.get_metadata();
  EXPECT_EQ(37, metadata.accuracy_.get_length());
  EXPECT_EQ(LS_CHAR, metadata.accuracy_.get_precision());
  EXPECT_EQ(LS_CHAR, metadata.accuracy_.get_length_semantics());
  EXPECT_EQ(7, metadata.accuracy_.get_scale());
  EXPECT_EQ(ObVarcharType, metadata.type_meta_.get_type());
  EXPECT_EQ(CS_TYPE_UTF8MB4_BIN, metadata.type_meta_.get_collation_type());
  EXPECT_EQ(CS_LEVEL_COERCIBLE, metadata.type_meta_.get_collation_level());
  EXPECT_EQ(7, metadata.type_meta_.get_scale());
  EXPECT_EQ(7, metadata.param_meta_.get_scale());
  EXPECT_EQ(0x80000005U, metadata.res_flags_);
  EXPECT_TRUE(parameter.has_result_flag(0x80000000U));
  EXPECT_FALSE(parameter.has_result_flag(0x00000002U));
  EXPECT_EQ(0xff, metadata.flag_.flag_);
  EXPECT_EQ(0xff, metadata.flag_.extend_flag_);
  EXPECT_TRUE(parameter.need_to_check_type());
  EXPECT_TRUE(parameter.need_to_check_bool_value());
  EXPECT_TRUE(parameter.expected_bool_value());
  EXPECT_EQ(INT32_MAX - 4, metadata.raw_text_pos_);
  EXPECT_EQ(INT32_MAX - 9, metadata.raw_text_len_);
  parameter.set_need_to_check_type(false);
  parameter.set_need_to_check_bool_value(false);
  parameter.set_expected_bool_value(false);
  EXPECT_EQ(0xf8, metadata.flag_.flag_);  // Includes the preserved legacy reserved bit.
  EXPECT_EQ(0xff, metadata.flag_.extend_flag_);  // All seven reserved extension bits survive.
  EXPECT_EQ(before.state(), parameter.datum_.state());
  EXPECT_EQ(before.length(), parameter.datum_.length());
  EXPECT_EQ(before.reference_flags(), parameter.datum_.reference_flags());
  EXPECT_EQ(before.get_reference(), parameter.datum_.get_reference());
  EXPECT_EQ(0, memcmp("abc", parameter.datum_.get_reference(), 3));
}

TEST(DatumParameter, jit_offsets_address_metadata_members_and_flag_payload)
{
  DatumParameter parameter;
  SqlTypeMeta meta; meta.set_ext(); parameter.set_type_meta(meta);
  parameter.set_param_meta(meta);
  const DatumParameterMetadata &metadata = parameter.get_metadata();
  char *base = reinterpret_cast<char *>(&parameter);
  const char *constant_base = base;
  const uint32_t accuracy_bits = DatumParameter::accuracy_offset_bits();
  const uint32_t result_bits = DatumParameter::res_flags_offset_bits();
  const uint32_t flag_bits = DatumParameter::flag_offset_bits();
  ASSERT_EQ(0U, accuracy_bits % 8); ASSERT_EQ(0U, result_bits % 8);
  ASSERT_EQ(0U, flag_bits % 8);
  EXPECT_EQ(static_cast<uint32_t>((reinterpret_cast<const char *>(&metadata.accuracy_) - constant_base) * 8),
            accuracy_bits);
  EXPECT_EQ(static_cast<uint32_t>((reinterpret_cast<const char *>(&metadata.res_flags_) - constant_base) * 8),
            result_bits);
  EXPECT_EQ(static_cast<uint32_t>((reinterpret_cast<const char *>(&metadata.flag_) - constant_base) * 8),
            flag_bits);
  const uint32_t flag_payload_bits = flag_bits + ParamFlag::flag_offset_bits();
  ASSERT_EQ(0U, flag_payload_bits % 8);
  EXPECT_EQ(static_cast<uint32_t>((reinterpret_cast<const char *>(&metadata.flag_.flag_) - constant_base) * 8),
            flag_payload_bits);
  const Datum before = parameter.datum_;
  const int64_t accuracy = static_cast<int64_t>(0x123456789abcde01ULL);
  const uint32_t result_flags = 0x8000fedcU;
  const uint8_t flag_payload = 0xb7;
  // Write only actual field payloads, never a ParamFlag object/vptr or padding.
  memcpy(base + accuracy_bits / 8, &accuracy, sizeof(accuracy));
  memcpy(base + result_bits / 8, &result_flags, sizeof(result_flags));
  memcpy(base + flag_payload_bits / 8, &flag_payload, sizeof(flag_payload));
  EXPECT_EQ(accuracy, parameter.get_accuracy().get_accuracy());
  EXPECT_EQ(static_cast<uint64_t>(accuracy), parameter.get_udt_id());
  EXPECT_EQ(result_flags, parameter.get_result_flag());
  EXPECT_EQ(flag_payload, parameter.get_param_flag().flag_);
  EXPECT_EQ(0, parameter.get_param_flag().extend_flag_);
  EXPECT_EQ(before.state(), parameter.datum_.state());
  EXPECT_EQ(before.length(), parameter.datum_.length());
  EXPECT_EQ(before.reference_flags(), parameter.datum_.reference_flags());
}

TEST(DatumParameter, metadata_default_and_parameter_reset_restore_all_seven_groups)
{
  EXPECT_EQ(size_t(48), sizeof(DatumParameter));
  EXPECT_EQ(size_t(12), sizeof(ObDatum));
  DatumParameterMetadata metadata;
  expect_default_parameter_metadata(metadata);
  DatumParameter parameter;
  expect_default_parameter_metadata(parameter.get_metadata());
  ObArenaAllocator owner;
  ASSERT_EQ(OB_SUCCESS, parameter.set_int(17, owner));
  const Datum old_value = parameter.datum_;
  parameter.set_accuracy(ObAccuracy(20, 7)); parameter.set_scale(7);
  parameter.set_param_meta();
  parameter.set_result_flags(0xfedcba98U);
  ParamFlag flags; flags.flag_ = 0xff; flags.extend_flag_ = 0xff;
  parameter.set_param_flag(flags); parameter.set_raw_text_info(1234, 5678);
  parameter.reset();
  EXPECT_TRUE(parameter.is_null());
  expect_default_parameter_metadata(parameter.get_metadata());
  EXPECT_EQ(17, old_value.get_int()); // A copied inline value remains independent after reset.
  EXPECT_EQ(size_t(16), sizeof(Datum));
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
