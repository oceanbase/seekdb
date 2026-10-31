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
  void *alloc(int64_t) override { return nullptr; }
  void *alloc(int64_t, const ObMemAttr &) override { return nullptr; }
  void free(void *) override {}
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
    EXPECT_NE(source.datum_.ptr_, destination.datum_.ptr_);
    memset(payload, 0x5a, expected.size());
  }
  EXPECT_EQ(expected.size(), destination.datum_.len_);
  EXPECT_EQ(0, memcmp(expected.data(), destination.datum_.ptr_, expected.size()));
  EXPECT_EQ(3, destination.get_raw_text_pos());
}

TEST(DatumParameter, container_reallocation_never_points_into_moved_parameter)
{
  ObArenaAllocator arena;
  std::vector<DatumParameter> parameters;
  for (int64_t i = 0; i < 513; ++i) {
    DatumParameter parameter; SqlTypeMeta meta; meta.set_int();
    parameter.set_type_meta(meta);
    ASSERT_EQ(OB_SUCCESS, parameter.alloc_datum_reserved_buff(meta, -1, arena));
    parameter.datum_.set_int(i); parameters.push_back(parameter);
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
  SqlTypeMeta meta; meta.set_int();
  source.set_type_meta(meta); destination.set_type_meta(meta);
  ASSERT_EQ(OB_SUCCESS, source.alloc_datum_reserved_buff(meta, -1, arena));
  ASSERT_EQ(OB_SUCCESS, destination.alloc_datum_reserved_buff(meta, -1, arena));
  source.datum_.set_int(123); destination.datum_.set_int(456);
  const char *old = destination.datum_.ptr_;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, destination.deep_copy(source, reject));
  EXPECT_EQ(old, destination.datum_.ptr_); EXPECT_EQ(456, destination.datum_.get_int());
  old = source.datum_.ptr_;
  ASSERT_EQ(OB_SUCCESS, source.deep_copy(source, arena));
  EXPECT_NE(old, source.datum_.ptr_); EXPECT_EQ(123, source.datum_.get_int());
}

TEST(DatumParameter, array_payload_copy_keeps_pointee_ownership_explicit)
{
  struct Array { int count; } array{7};
  char address[8]; DatumParameter source, copy;
  SqlTypeMeta meta; meta.set_ext(); meta.set_extend_type(T_EXT_SQL_ARRAY);
  source.set_type_meta(meta); source.datum_.ptr_ = address;
  source.set_array_address(&array);
  ObArenaAllocator arena;
  ASSERT_EQ(OB_SUCCESS, copy.deep_copy(source, arena));
  EXPECT_EQ(&array, copy.get_array<Array>());
  EXPECT_NE(source.datum_.ptr_, copy.datum_.ptr_);
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
  EXPECT_EQ(8, integer_size); EXPECT_EQ(sizeof(expected), text_size);
  ObDatum owned_integer, owned_text;
  ASSERT_EQ(OB_SUCCESS, integer_plan.deep_copy(integer.datum_, owned_integer, output));
  ASSERT_EQ(OB_SUCCESS, text_plan.deep_copy(text.datum_, owned_text, output));
  integer.datum_.set_int(INT64_MAX);
  memset(borrowed, 'x', sizeof(expected));
  input.reset();
  EXPECT_EQ(INT64_MIN, owned_integer.get_int());
  EXPECT_EQ(sizeof(expected), owned_text.len_);
  EXPECT_EQ(0, memcmp(expected, owned_text.ptr_, sizeof(expected)));
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
    source.datum_.set_decimal_int_shallow(reinterpret_cast<ObDecimalInt *>(coefficient), width);
    DatumCopyPlan plan(meta, source.get_accuracy());
    EXPECT_EQ(PRECISION_UNKNOWN_YET, plan.get_accuracy().get_precision());
    int64_t bytes = -1;
    ASSERT_EQ(OB_SUCCESS, plan.payload_copy_size(source.datum_, bytes));
    EXPECT_EQ(width, bytes);
    ASSERT_EQ(OB_SUCCESS, destination.deep_copy(source, output));
    memset(coefficient, 0, width); input.reset();
    EXPECT_EQ(PRECISION_UNKNOWN_YET, destination.get_precision());
    EXPECT_EQ(7, destination.get_scale()); EXPECT_EQ(width, destination.datum_.len_);
    EXPECT_EQ(0, memcmp(expected.data(), destination.datum_.ptr_, width));
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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
