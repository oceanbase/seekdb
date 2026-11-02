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

#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter_array.h"
#include "common/datum/native_typed_datum.h"
#include <gtest/gtest.h>
#include <cstring>

using namespace oceanbase::common;

namespace {
class CopyPlanAllocator final : public ObIAllocator
{
public:
  CopyPlanAllocator() : calls(0), reject(false), arena() {}
  void *alloc(int64_t bytes) override
  { ++calls; if (bytes == 15 || bytes == 16) { ++carrier_calls; } return reject ? nullptr : arena.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *bytes) override { arena.free(bytes); }
  int64_t calls;
  int64_t carrier_calls = 0;
  bool reject;
  ObArenaAllocator arena;
};

ObDatum legacy(const void *bytes, int64_t length)
{
  ObDatum value;
  value.ptr_ = static_cast<const char *>(bytes); value.pack_ = length;
  return value;
}

SqlTypeMeta metadata(SqlValueType kind)
{
  SqlTypeMeta meta; meta.set_type(kind); return meta;
}

DatumCopyPlan decimal_plan(ObPrecision precision, ObScale scale)
{
  SqlTypeMeta meta; meta.set_decimal_int(scale);
  ObAccuracy accuracy; accuracy.set_precision(precision); accuracy.set_scale(scale);
  return DatumCopyPlan(meta, accuracy);
}

void write_coefficient(char *bytes, int64_t width, int64_t value)
{
  MEMSET(bytes, value < 0 ? 0xff : 0, width);
  if (width == 4) {
    const int32_t coefficient = static_cast<int32_t>(value);
    MEMCPY(bytes, &coefficient, sizeof(coefficient));
  } else if (width > 0) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    MEMCPY(bytes + width - sizeof(value), &value, sizeof(value));
#else
    MEMCPY(bytes, &value, sizeof(value));
#endif
  }
}

void check_integer(SqlValueType kind, int64_t input)
{
  char unaligned[9]; MEMCPY(unaligned + 1, &input, sizeof(input));
  const DatumCopyPlan plan(metadata(kind), ObAccuracy());
  Datum value; value.set_int(99);
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(unaligned + 1, sizeof(input)), value));
  EXPECT_EQ(input, value.get_int());
  MEMSET(unaligned, 0, sizeof(unaligned));
  EXPECT_EQ(input, value.get_int());
  CopyPlanAllocator allocator; allocator.reject = true;
  Datum clone;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(value, clone, allocator));
  EXPECT_EQ(0, allocator.calls);
  EXPECT_EQ(input, clone.get_int());
  int64_t native_pos = 0;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(value, clone, nullptr, 0, native_pos));
  EXPECT_EQ(0, native_pos);
  char output[10]; MEMSET(output, 0x5a, sizeof(output));
  int64_t position = 1, size = -1;
  ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(value, size)); EXPECT_EQ(8, size);
  ObDatum stored;
  ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, stored, output, sizeof(output), position));
  EXPECT_EQ(9, position); EXPECT_EQ(output + 1, stored.ptr_);
  int64_t restored = 0; MEMCPY(&restored, stored.ptr_, sizeof(restored));
  EXPECT_EQ(input, restored); EXPECT_EQ(char(0x5a), output[0]); EXPECT_EQ(char(0x5a), output[9]);
}
}

TEST(DatumCopyPlan, signed_integer_extremes_round_trip_unaligned_without_scalar_allocation)
{
  const SqlValueType kinds[] = {ObTinyIntType, ObSmallIntType, ObMediumIntType, ObInt32Type, ObIntType};
  const int64_t values[] = {INT64_MIN, INT32_MIN, -1, 0, 1, INT32_MAX, INT64_MAX};
  for (const SqlValueType kind : kinds) {
    for (const int64_t value : values) { check_integer(kind, value); }
  }
}

TEST(DatumCopyPlan, unsigned_integer_extremes_keep_all_bits)
{
  const SqlValueType kinds[] = {ObUTinyIntType, ObUSmallIntType, ObUMediumIntType, ObUInt32Type, ObUInt64Type, ObBitType};
  const uint64_t values[] = {0, uint64_t(INT64_MAX), uint64_t(INT64_MAX) + 1, UINT64_MAX};
  for (const SqlValueType kind : kinds) {
    const DatumCopyPlan plan(metadata(kind), ObAccuracy());
    for (const uint64_t input : values) {
      char source[9]; MEMCPY(source + 1, &input, sizeof(input));
      Datum value;
      ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(source + 1, 8), value));
      EXPECT_EQ(input, value.get_uint());
      char output[9]; int64_t position = 1; ObDatum stored;
      ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, stored, output, sizeof(output), position));
      EXPECT_EQ(0, MEMCMP(stored.ptr_, source + 1, 8));
    }
  }
}

TEST(DatumCopyPlan, native_lengths_follow_float_date_year_and_timestamp_legacy_mapping)
{
  const SqlValueType kinds[] = {ObFloatType, ObUFloatType, ObDoubleType, ObUDoubleType,
      ObDateType, ObMySQLDateType, ObYearType, ObTimestampType, ObMySQLDateTimeType};
  for (const SqlValueType kind : kinds) {
    const DatumCopyPlan plan(metadata(kind), ObAccuracy());
    const int64_t width = kind == ObYearType ? 1 : kind == ObFloatType || kind == ObUFloatType
        || kind == ObDateType || kind == ObMySQLDateType ? 4 : 8;
    char source[9]; MEMSET(source, 0, sizeof(source));
    if (kind == ObFloatType || kind == ObUFloatType) {
      const float input = -123.5f; MEMCPY(source + 1, &input, sizeof(input));
    } else if (kind == ObDoubleType || kind == ObUDoubleType) {
      const double input = -123.5; MEMCPY(source + 1, &input, sizeof(input));
    } else if (width == 4) {
      const int32_t input = INT32_MIN; MEMCPY(source + 1, &input, sizeof(input));
    } else if (width == 1) {
      source[1] = char(UINT8_MAX);
    } else {
      const int64_t input = INT64_MIN; MEMCPY(source + 1, &input, sizeof(input));
    }
    Datum value;
    ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(source + 1, width), value));
    char output[9]; ObDatum stored; int64_t position = 1, size = 0;
    ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(value, size)); EXPECT_EQ(width, size);
    ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, stored, output, sizeof(output), position));
    EXPECT_EQ(width, stored.len_); EXPECT_EQ(0, MEMCMP(source + 1, stored.ptr_, width));
  }
}

TEST(DatumCopyPlan, typed_null_and_range_markers_have_explicit_states)
{
  const DatumCopyPlan plan(metadata(ObIntType), ObAccuracy());
  ObDatum null; null.set_null();
  Datum value; value.set_int(42);
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(null, value)); EXPECT_TRUE(value.is_null());
  EXPECT_EQ(ObIntType, plan.get_type_meta().get_type());
  int64_t size = -1, position = 0; ObDatum restored;
  ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(value, size)); EXPECT_EQ(0, size);
  ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, restored, nullptr, 0, position));
  EXPECT_TRUE(restored.is_null()); EXPECT_EQ(0, position);
  const int64_t markers[] = {DATUM_MIN_EXT_VALUE, DATUM_MAX_EXT_VALUE, ObActionFlag::OP_NOP};
  for (const int64_t marker : markers) {
    char source[DATUM_EXTEND_PAYLOAD_SIZE + 1]; ObDatum extended;
    extended.ptr_ = source + 1; extended.set_extend(marker, 0);
    ASSERT_EQ(OB_SUCCESS, plan.import_legacy(extended, value));
    EXPECT_EQ(marker == DATUM_MIN_EXT_VALUE, value.is_min());
    EXPECT_EQ(marker == DATUM_MAX_EXT_VALUE, value.is_max());
    EXPECT_EQ(marker == ObActionFlag::OP_NOP, value.is_nop());
    char output[DATUM_EXTEND_PAYLOAD_SIZE + 1]; position = 1;
    ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, restored, output, sizeof(output), position));
    EXPECT_TRUE(restored.is_ext()); EXPECT_EQ(marker, restored.get_ext());
  }
}

TEST(DatumCopyPlan, narrow_decimal_accepts_zero_and_verified_wide_sign_extension)
{
  const DatumCopyPlan small = decimal_plan(9, 2), wide_word = decimal_plan(18, 5);
  Datum value;
  ASSERT_EQ(OB_SUCCESS, small.import_legacy(legacy(nullptr, 0), value));
  EXPECT_EQ(0, value.get_int());
  int64_t size = -1;
  ASSERT_EQ(OB_SUCCESS, small.legacy_payload_size(value, size)); EXPECT_EQ(4, size);
  for (const int64_t input : {int64_t(-1), int64_t(0), INT64_MIN, INT64_MAX}) {
    char coefficient[33]; MEMSET(coefficient, input < 0 ? 0xff : 0, sizeof(coefficient));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    MEMCPY(coefficient + 1 + 32 - sizeof(input), &input, sizeof(input));
#else
    MEMCPY(coefficient + 1, &input, sizeof(input));
#endif
    ASSERT_EQ(OB_SUCCESS, wide_word.import_legacy(legacy(coefficient + 1, 32), value));
    EXPECT_EQ(input, value.get_int());
    MEMSET(coefficient, 0, sizeof(coefficient)); EXPECT_EQ(input, value.get_int());
  }
  char malformed[16]; MEMSET(malformed, 0xff, sizeof(malformed));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  malformed[0] = 0;
#else
  malformed[15] = 0;
#endif
  value.set_int(99);
  EXPECT_EQ(OB_INVALID_ARGUMENT, wide_word.import_legacy(legacy(malformed, 16), value));
  EXPECT_EQ(99, value.get_int());
  const int64_t too_large = int64_t(INT32_MAX) + 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, small.import_legacy(legacy(&too_large, 8), value));
  EXPECT_EQ(99, value.get_int());
}

TEST(DatumCopyPlan, unknown_precision_and_wide_decimals_remain_references)
{
  const DatumCopyPlan unknown = decimal_plan(-1, 5), wide = decimal_plan(20, 2);
  char coefficient[16]; MEMSET(coefficient, 0x23, sizeof(coefficient));
  Datum view, copy;
  ASSERT_EQ(OB_SUCCESS, unknown.import_legacy(legacy(coefficient, 16), view));
  EXPECT_EQ(coefficient, view.get_reference()); EXPECT_EQ(16, view.length());
  EXPECT_EQ(5, unknown.get_accuracy().get_scale());
  CopyPlanAllocator allocator;
  ASSERT_EQ(OB_SUCCESS, unknown.deep_copy(view, copy, allocator)); EXPECT_EQ(1, allocator.calls);
  EXPECT_NE(view.get_reference(), copy.get_reference());
  MEMSET(coefficient, 0x44, sizeof(coefficient));
  EXPECT_EQ(char(0x23), static_cast<const char *>(copy.get_reference())[0]);
  ASSERT_EQ(OB_SUCCESS, wide.import_legacy(legacy(coefficient, 16), view));
  EXPECT_EQ(coefficient, view.get_reference());
  const int64_t short_value = 1;
  ASSERT_EQ(OB_SUCCESS, wide.import_legacy(legacy(&short_value, 8), view));
  EXPECT_EQ(&short_value, view.get_reference()); EXPECT_EQ(8, view.length());
}

TEST(DatumCopyPlan, wide_decimal_zero_materializes_the_declared_legacy_coefficient_span)
{
  const ObPrecision precisions[] = {20, 40, 80};
  const int64_t widths[] = {16, 32, 64};
  for (int i = 0; i < 3; ++i) {
    const DatumCopyPlan plan = decimal_plan(precisions[i], 2);
    Datum value;
    ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(nullptr, 0), value));
    EXPECT_TRUE(value.is_present()); EXPECT_EQ(0, value.length());
    int64_t size = -1;
    ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(value, size)); EXPECT_EQ(widths[i], size);
    char output[66]; MEMSET(output, 0x5a, sizeof(output));
    ObDatum stored; int64_t position = 1;
    ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, stored, output, sizeof(output), position));
    EXPECT_EQ(widths[i], stored.len_); EXPECT_EQ(widths[i] + 1, position);
    char read[64]; MEMCPY(read, stored.ptr_, widths[i]);
    for (int64_t j = 0; j < widths[i]; ++j) { EXPECT_EQ(0, read[j]); }
    EXPECT_EQ(char(0x5a), output[0]); EXPECT_EQ(char(0x5a), output[position]);
    CopyPlanAllocator owner; Datum clone;
    ASSERT_EQ(OB_SUCCESS, plan.deep_copy(value, clone, owner));
    EXPECT_EQ(0, owner.calls); EXPECT_EQ(0, clone.length());
  }
}

TEST(DatumCopyPlan, wide_decimal_native_clone_preserves_actual_span_before_legacy_materialization)
{
  const DatumCopyPlan plan = decimal_plan(20, 2);
  const int64_t input = -7;
  Datum view, owned, buffered;
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(&input, sizeof(input)), view));
  EXPECT_EQ(sizeof(input), view.length());
  CopyPlanAllocator owner;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(view, owned, owner));
  EXPECT_EQ(1, owner.calls); EXPECT_EQ(sizeof(input), owned.length());
  EXPECT_EQ(0, MEMCMP(&input, owned.get_reference(), sizeof(input)));
  char native_buffer[11]; int64_t native_position = 3;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(view, buffered, native_buffer, sizeof(native_buffer), native_position));
  EXPECT_EQ(11, native_position); EXPECT_EQ(8, buffered.length());
  EXPECT_EQ(0, MEMCMP(&input, buffered.get_reference(), sizeof(input)));
  char output[17]; ObDatum stored; int64_t position = 1, size = -1;
  ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(owned, size)); EXPECT_EQ(16, size);
  ASSERT_EQ(OB_SUCCESS, plan.export_legacy(owned, stored, output, sizeof(output), position));
  EXPECT_EQ(16, stored.len_);
  char read[16], expected[16]; MEMCPY(read, stored.ptr_, sizeof(read));
  write_coefficient(expected, sizeof(expected), input);
  EXPECT_EQ(0, MEMCMP(expected, read, sizeof(read)));
}

TEST(DatumCopyPlan, wide_decimal_sign_narrowing_is_verified_before_any_legacy_write)
{
  const DatumCopyPlan plan = decimal_plan(20, 2);
  for (const int64_t width : {int64_t(32), int64_t(64)}) {
    for (const int64_t input : {int64_t(-7), int64_t(17)}) {
      char coefficient[64]; write_coefficient(coefficient, width, input);
      Datum value;
      ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(coefficient, width), value));
      EXPECT_EQ(width, value.length());
      int64_t size = -1;
      ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(value, size)); EXPECT_EQ(16, size);
      char output[17]; int64_t position = 1; ObDatum stored;
      ASSERT_EQ(OB_SUCCESS, plan.export_legacy(value, stored, output, sizeof(output), position));
      char read[16], expected[16]; MEMCPY(read, stored.ptr_, sizeof(read));
      write_coefficient(expected, sizeof(expected), input);
      EXPECT_EQ(0, MEMCMP(expected, read, sizeof(read)));

#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
      coefficient[0] = char(0x7e);
#else
      coefficient[width - 1] = char(0x7e);
#endif
      size = 123; position = 1;
      MEMSET(output, 0x55, sizeof(output));
      const int64_t original = 99; stored = legacy(&original, sizeof(original));
      EXPECT_EQ(OB_INVALID_ARGUMENT, plan.legacy_payload_size(value, size)); EXPECT_EQ(123, size);
      EXPECT_EQ(OB_INVALID_ARGUMENT, plan.export_legacy(value, stored, output, sizeof(output), position));
      EXPECT_EQ(1, position); EXPECT_EQ(reinterpret_cast<const char *>(&original), stored.ptr_);
      for (const char byte : output) { EXPECT_EQ(char(0x55), byte); }
      // A native reference owns its actual coefficient even if it cannot yet
      // be narrowed for this legacy expression slot.
      CopyPlanAllocator owner; Datum clone;
      ASSERT_EQ(OB_SUCCESS, plan.deep_copy(value, clone, owner)); EXPECT_EQ(width, clone.length());
    }
  }
  char coefficient[32]; MEMSET(coefficient, 0, sizeof(coefficient));
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  coefficient[16] = char(0x80);
#else
  coefficient[15] = char(0x80);
#endif
  Datum sign_mismatch;
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(coefficient, sizeof(coefficient)), sign_mismatch));
  int64_t size = 123;
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan.legacy_payload_size(sign_mismatch, size)); EXPECT_EQ(123, size);
}

TEST(DatumCopyPlan, unknown_decimal_precision_preserves_every_actual_span_without_inference)
{
  const DatumCopyPlan plan = decimal_plan(-1, 5);
  const int64_t widths[] = {0, 4, 8, 16, 32, 64};
  for (const int64_t width : widths) {
    char coefficient[64]; MEMSET(coefficient, 0x23, sizeof(coefficient));
    Datum value, clone;
    ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(width == 0 ? nullptr : coefficient, width), value));
    EXPECT_EQ(width, value.length());
    int64_t size = -1;
    ASSERT_EQ(OB_SUCCESS, plan.legacy_payload_size(value, size)); EXPECT_EQ(width, size);
    CopyPlanAllocator owner;
    ASSERT_EQ(OB_SUCCESS, plan.deep_copy(value, clone, owner)); EXPECT_EQ(width, clone.length());
    EXPECT_EQ(width == 0 ? 0 : 1, owner.calls);
    char output[66]; MEMSET(output, 0x5a, sizeof(output));
    ObDatum stored; int64_t position = 1;
    ASSERT_EQ(OB_SUCCESS, plan.export_legacy(clone, stored, output, sizeof(output), position));
    EXPECT_EQ(width, stored.len_); EXPECT_EQ(width + 1, position);
    if (width > 0) { EXPECT_EQ(0, MEMCMP(coefficient, stored.ptr_, width)); }
    EXPECT_EQ(char(0x5a), output[0]); EXPECT_EQ(char(0x5a), output[position]);
  }
}

TEST(DatumCopyPlan, reference_flags_and_owned_payload_survive_source_overwrite)
{
  SqlTypeMeta meta; meta.set_json();
  const DatumCopyPlan plan(meta, ObAccuracy());
  const ObDatumDesc::FlagType flags[] = {ObDatumDesc::NONE, ObDatumDesc::OUTROW, ObDatumDesc::HAS_LOB_HEADER};
  for (const auto flag : flags) {
    char source[] = "referenced bytes";
    ObDatum old = legacy(source, sizeof(source)); old.set_flag(flag);
    Datum view, clone;
    ASSERT_EQ(OB_SUCCESS, plan.import_legacy(old, view));
    EXPECT_EQ(source, view.get_reference());
    CopyPlanAllocator owner;
    ASSERT_EQ(OB_SUCCESS, plan.deep_copy(view, clone, owner));
    MEMSET(source, 0x66, sizeof(source));
    EXPECT_EQ(char('r'), static_cast<const char *>(clone.get_reference())[0]);
    char output[64]; int64_t position = 3; ObDatum encoded;
    ASSERT_EQ(OB_SUCCESS, plan.export_legacy(clone, encoded, output, sizeof(output), position));
    EXPECT_EQ(flag, encoded.flag_); EXPECT_EQ(sizeof(source), encoded.len_);
    EXPECT_EQ(char('r'), encoded.ptr_[0]);
  }
}

TEST(DatumCopyPlan, complete_enum_type_uses_inline_ordinal_but_requires_labels)
{
  ObArenaAllocator owner;
  ObString labels[] = {ObString("first"), ObString("second")};
  SqlTypeSpec spec; spec.attributes.meta_.set_enum(); spec.form = SqlTypeForm::Enum;
  spec.labels = labels; spec.label_count = 2;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  const DatumCopyPlan plan(*type);
  const uint64_t input = 2; Datum value;
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(&input, sizeof(input)), value));
  EXPECT_EQ(input, value.get_uint());
  CopyPlanAllocator reject; reject.reject = true; Datum clone;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(value, clone, reject)); EXPECT_EQ(0, reject.calls);
  const DatumCopyPlan incomplete(spec.attributes.meta_, ObAccuracy());
  EXPECT_EQ(OB_NOT_SUPPORTED, incomplete.import_legacy(legacy(&input, sizeof(input)), clone));
  EXPECT_EQ(input, clone.get_uint());
}

TEST(DatumCopyPlan, export_and_size_share_shape_checks_and_fail_without_any_publication)
{
  const DatumCopyPlan year(metadata(ObYearType), ObAccuracy());
  Datum invalid; invalid.set_uint(256);
  int64_t size = 123;
  EXPECT_EQ(OB_INVALID_ARGUMENT, year.legacy_payload_size(invalid, size)); EXPECT_EQ(123, size);
  char output[32]; MEMSET(output, 0x55, sizeof(output));
  char snapshot[sizeof(output)]; MEMCPY(snapshot, output, sizeof(output));
  const int64_t old = 19; ObDatum destination = legacy(&old, 8);
  int64_t position = 1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, year.export_legacy(invalid, destination, output, sizeof(output), position));
  EXPECT_EQ(1, position); EXPECT_EQ(reinterpret_cast<const char *>(&old), destination.ptr_);
  EXPECT_EQ(0, MEMCMP(snapshot, output, sizeof(output)));
  const DatumCopyPlan integer(metadata(ObIntType), ObAccuracy());
  Datum value; value.set_int(42);
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, integer.export_legacy(value, destination, output, 8, position));
  EXPECT_EQ(1, position); EXPECT_EQ(reinterpret_cast<const char *>(&old), destination.ptr_);
  EXPECT_EQ(0, MEMCMP(snapshot, output, sizeof(output)));
  position = -1;
  EXPECT_EQ(OB_INVALID_ARGUMENT, integer.export_legacy(value, destination, output, sizeof(output), position));
  EXPECT_EQ(-1, position); EXPECT_EQ(0, MEMCMP(snapshot, output, sizeof(output)));
  const DatumCopyPlan reference(metadata(ObVarcharType), ObAccuracy());
  ASSERT_EQ(OB_SUCCESS, invalid.set_reference("x", 1, Datum::OutRow | Datum::HasLobHeader));
  EXPECT_EQ(OB_NOT_SUPPORTED, reference.legacy_payload_size(invalid, size)); EXPECT_EQ(123, size);
}

TEST(DatumCopyPlan, failed_native_reference_clone_keeps_destination_cursor_and_buffer)
{
  const DatumCopyPlan plan(metadata(ObVarcharType), ObAccuracy());
  Datum source, output; source.set_reference("source", 6); output.set_int(99);
  CopyPlanAllocator reject; reject.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.deep_copy(source, output, reject)); EXPECT_EQ(99, output.get_int());
  char bytes[8]; MEMSET(bytes, 0x77, sizeof(bytes)); int64_t position = 3;
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, plan.deep_copy(source, output, bytes, sizeof(bytes), position));
  EXPECT_EQ(3, position); EXPECT_EQ(99, output.get_int());
  for (const char byte : bytes) { EXPECT_EQ(char(0x77), byte); }
  position = 1;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(source, output, bytes, sizeof(bytes), position));
  EXPECT_EQ(7, position); EXPECT_EQ(bytes + 1, output.get_reference());
  EXPECT_EQ(0, MEMCMP("source", output.get_reference(), 6));
}

TEST(DatumCopyPlan, resources_require_dedicated_cloners_and_nondefault_time_requires_codec)
{
  SqlTypeMeta array_meta; array_meta.set_ext(); array_meta.set_extend_type(T_EXT_SQL_ARRAY);
  const DatumCopyPlan array(array_meta, ObAccuracy());
  const int64_t address = 123; Datum result; result.set_int(99);
  EXPECT_EQ(OB_NOT_SUPPORTED, array.import_legacy(legacy(&address, 8), result)); EXPECT_EQ(99, result.get_int());
  SqlTypeMeta ext_meta; ext_meta.set_ext();
  const DatumCopyPlan generic(ext_meta, ObAccuracy());
  char wrapper[DATUM_EXTEND_PAYLOAD_SIZE]; ObDatum extended; extended.ptr_ = wrapper;
  extended.set_extend(123, 1);
  EXPECT_EQ(OB_NOT_SUPPORTED, generic.import_legacy(extended, result)); EXPECT_EQ(99, result.get_int());
  extended.set_extend(ObActionFlag::OP_DEL_ROW, 0);
  ASSERT_EQ(OB_SUCCESS, generic.import_legacy(extended, result));
  EXPECT_EQ(Datum::LegacyExtended, result.reference_flags());
  char output[32]; int64_t position = 0; ObDatum restored;
  ASSERT_EQ(OB_SUCCESS, generic.export_legacy(result, restored, output, sizeof(output), position));
  EXPECT_TRUE(restored.is_ext()); EXPECT_EQ(int64_t(ObActionFlag::OP_DEL_ROW), restored.get_ext());
  ObArenaAllocator owner;
  SqlTypeSpec spec; spec.attributes.meta_.set_timestamp(); spec.time = SqlTimeSemantics::WithTimeZone;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  const DatumCopyPlan zone(*type);
  EXPECT_EQ(OB_NOT_SUPPORTED, zone.import_legacy(legacy(&address, 8), result));
}

TEST(DatumCopyPlan, legacy_byte_clone_keeps_old_untyped_cost_and_does_not_build_complete_types)
{
  SqlTypeMeta meta; meta.set_enum(); // Missing enum labels prevent a native transition.
  const DatumCopyPlan plan(meta, ObAccuracy());
  const uint64_t ordinal = 2; const ObDatum source = legacy(&ordinal, 8);
  CopyPlanAllocator owner; ObDatum output;
  ASSERT_EQ(OB_SUCCESS, plan.deep_copy(source, output, owner));
  EXPECT_EQ(1, owner.calls); EXPECT_NE(source.ptr_, output.ptr_);
  EXPECT_EQ(0, MEMCMP(source.ptr_, output.ptr_, 8));
}

TEST(DatumCopyPlan, explicit_mutable_signed_int_view_updates_the_same_active_i64)
{
  SqlType type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(metadata(ObIntType), ObAccuracy(20, 0), type));
  const DatumCopyPlan plan(type);
  Datum value; value.set_int(INT64_MIN);
  ObDatum view;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_mutable_signed_int(value, view));
  EXPECT_EQ(8, view.len_); EXPECT_EQ(ObDatumDesc::NONE, view.flag_);
  EXPECT_EQ(0, reinterpret_cast<uintptr_t>(view.ptr_) % alignof(int64_t));
  EXPECT_EQ(INT64_MIN, view.get_int());
  view.set_int(INT64_MAX);
  EXPECT_EQ(INT64_MAX, value.get_int());
  ObDatum readonly;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_legacy(value, readonly));
  EXPECT_EQ(view.ptr_, readonly.ptr_);
  EXPECT_EQ(INT64_MAX, readonly.get_int()); // Do not write through borrow_legacy.
}

TEST(DatumCopyPlan, mutable_signed_int_rejects_other_types_and_invalid_shapes_without_publishing)
{
  int64_t sentinel = 91;
  const ObDatum saved = legacy(&sentinel, sizeof(sentinel));
  ObDatum output = saved;
  Datum value; value.set_int(7);
  const DatumCopyPlan incomplete(metadata(ObIntType), ObAccuracy(20, 0));
  EXPECT_EQ(OB_NOT_SUPPORTED, incomplete.borrow_mutable_signed_int(value, output));
  const SqlValueType unsupported[] = {ObInt32Type, ObUInt64Type, ObDoubleType, ObVarcharType};
  for (SqlValueType kind : unsupported) {
    SqlType type;
    SqlTypeMeta meta = metadata(kind);
    if (kind == ObVarcharType) {
      meta.set_collation_type(CS_TYPE_UTF8MB4_BIN); meta.set_collation_level(CS_LEVEL_IMPLICIT);
    }
    ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
    EXPECT_EQ(OB_NOT_SUPPORTED, DatumCopyPlan(type).borrow_mutable_signed_int(value, output));
    EXPECT_EQ(saved.ptr_, output.ptr_); EXPECT_EQ(saved.pack_, output.pack_);
  }
  SqlType type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(metadata(ObIntType), ObAccuracy(20, 0), type));
  const DatumCopyPlan plan(type);
  for (int state = 0; state < 6; ++state) {
    if (state == 0) { value.set_null(); }
    else if (state == 1) { value.set_nop(); }
    else if (state == 2) { value.set_min(); }
    else if (state == 3) { value.set_max(); }
    else if (state == 4) { ASSERT_EQ(OB_SUCCESS, value.set_reference(&sentinel, 8)); }
    else { ASSERT_EQ(OB_SUCCESS, value.set_reference(nullptr, 0, Datum::OutRow)); }
    EXPECT_EQ(OB_INVALID_ARGUMENT, plan.borrow_mutable_signed_int(value, output));
    EXPECT_EQ(saved.ptr_, output.ptr_); EXPECT_EQ(saved.pack_, output.pack_);
  }
  // An empty reference with no flags has the same shape as inline data. Calling
  // the mutable bridge for it would violate the producer/type contract, so this
  // test does not pretend that shape validation can identify the active member.
  EXPECT_EQ(91, sentinel);
}

TEST(DatumCopyPlan, native_size_and_explicit_decimal_writer_keep_actual_span)
{
  const auto small = decimal_plan(9, 2);
  const auto wide = decimal_plan(40, 2);
  const int64_t coefficient = -1234;
  Datum value; value.set_int(71);
  ASSERT_EQ(OB_SUCCESS, small.set_decimal(reinterpret_cast<const ObDecimalInt *>(&coefficient), 8,
                                         value, nullptr, 0));
  EXPECT_EQ(coefficient, value.get_int());
  int64_t size = -1;
  ASSERT_EQ(OB_SUCCESS, small.payload_copy_size(value, size)); EXPECT_EQ(0, size);
  char owned[9]; MEMSET(owned, 0x5a, sizeof(owned));
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, wide.set_decimal(reinterpret_cast<const ObDecimalInt *>(&coefficient), 8,
                                               value, owned, 7));
  EXPECT_EQ(coefficient, value.get_int()); EXPECT_EQ(char(0x5a), owned[0]);
  ASSERT_EQ(OB_SUCCESS, wide.set_decimal(reinterpret_cast<const ObDecimalInt *>(&coefficient), 8,
                                         value, owned, sizeof(owned)));
  ASSERT_EQ(OB_SUCCESS, wide.payload_copy_size(value, size)); EXPECT_EQ(8, size);
  ASSERT_EQ(OB_SUCCESS, wide.legacy_payload_size(value, size)); EXPECT_EQ(32, size);
  alignas(8) char scratch[64]; ObDatum view;
  ASSERT_EQ(OB_SUCCESS, wide.get_decimal(value, view, scratch, sizeof(scratch)));
  EXPECT_EQ(32, view.len_); EXPECT_EQ(scratch, view.ptr_);
  EXPECT_EQ(int256_t(coefficient), view.get_decimal_int256());
  EXPECT_EQ(char(0x5a), owned[8]);
}

TEST(DatumCopyPlan, legacy_view_borrows_reference_but_uses_caller_scratch_for_scalar_and_state)
{
  const DatumCopyPlan integer(metadata(ObIntType), ObAccuracy());
  Datum value; value.set_int(INT64_MIN);
  ObDatum view = legacy("sentinel", 8);
  const char *prior = view.ptr_;
  alignas(8) char scratch[64]; MEMSET(scratch, 0x21, sizeof(scratch));
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, integer.legacy_view(value, view, scratch, 63));
  EXPECT_EQ(prior, view.ptr_); EXPECT_EQ(char(0x21), scratch[0]);
  ASSERT_EQ(OB_SUCCESS, integer.legacy_view(value, view, scratch, sizeof(scratch)));
  EXPECT_EQ(scratch, view.ptr_); EXPECT_EQ(INT64_MIN, view.get_int());
  value.set_max();
  ASSERT_EQ(OB_SUCCESS, integer.legacy_view(value, view, scratch, sizeof(scratch)));
  EXPECT_TRUE(view.is_max());
  const DatumCopyPlan string(metadata(ObVarcharType), ObAccuracy());
  char text[] = "abc"; ASSERT_EQ(OB_SUCCESS, value.set_string(text, 3));
  ASSERT_EQ(OB_SUCCESS, string.legacy_view(value, view, scratch, sizeof(scratch)));
  EXPECT_EQ(text, view.ptr_); EXPECT_EQ(3, view.len_);
}

TEST(DatumCopyPlan, typed_wide_reads_extend_actual_zero_short_and_negative_coefficients)
{
  const DatumCopyPlan plan = decimal_plan(30, 2);
  const int64_t values[] = {0, 127, -128, INT32_MIN};
  for (int64_t value : values) {
    for (int width : {0, 4, 8}) {
      if (width == 0 && value != 0) { continue; }
      char source[8]; write_coefficient(source, width, value);
      char owned[8]; Datum datum;
      ASSERT_EQ(OB_SUCCESS, plan.set_decimal(reinterpret_cast<const ObDecimalInt *>(source), width,
                                             datum, owned, sizeof(owned)));
      MEMSET(source, 0x7f, sizeof(source));
      int128_t value128 = 71; int256_t value256 = 72; int512_t value512 = 73;
      ASSERT_EQ(OB_SUCCESS, datum.get_decimal_int128(value128)); EXPECT_EQ(int128_t(value), value128);
      ASSERT_EQ(OB_SUCCESS, datum.get_decimal_int256(value256)); EXPECT_EQ(int256_t(value), value256);
      ASSERT_EQ(OB_SUCCESS, datum.get_decimal_int512(value512)); EXPECT_EQ(int512_t(value), value512);
    }
  }
  char wider[32]; write_coefficient(wider, sizeof(wider), -7);
  Datum reference; ASSERT_EQ(OB_SUCCESS, reference.set_reference(wider, sizeof(wider)));
  int128_t result = 99;
  ASSERT_EQ(OB_SUCCESS, reference.get_decimal_int128(result)); EXPECT_EQ(int128_t(-7), result);
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
  wider[0] = 0;
#else
  wider[31] = 0;
#endif
  result = 99;
  EXPECT_EQ(OB_INVALID_ARGUMENT, reference.get_decimal_int128(result)); EXPECT_EQ(int128_t(99), result);
  ASSERT_EQ(OB_SUCCESS, reference.set_reference(wider, 7));
  EXPECT_EQ(OB_INVALID_ARGUMENT, reference.get_decimal_int128(result)); EXPECT_EQ(int128_t(99), result);
}

namespace {
int clone_native_sql_array(const SqlType &type, const Datum &input, ObIAllocator &owner,
                           Datum &output, void *)
{
  DatumParameter source, clone;
  source.set_type_meta(type.legacy_meta()); source.set_accuracy(type.accuracy());
  const DatumCopyPlan plan(type);
  ObDatum legacy; alignas(8) char scratch[64];
  int ret = plan.borrow_legacy(input, legacy);
  if (ret == OB_SUCCESS) { ret = source.assign_legacy_borrowed(legacy, type.legacy_meta()); }
  if (ret == OB_SUCCESS) { ret = DatumParameterArray::clone_parameter(source, clone, owner); }
  if (ret == OB_SUCCESS) { ret = clone.export_legacy(legacy, scratch, sizeof(scratch)); }
  if (ret == OB_SUCCESS) { ret = plan.import_legacy(legacy, output); }
  return ret;
}
}

TEST(DatumCopyPlan, complete_array_borrow_requires_the_existing_resource_cloner_to_own_leaves)
{
  ObArenaAllocator source_owner, type_owner;
  CopyPlanAllocator destination_owner;
  SqlType element;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(metadata(ObIntType), ObAccuracy(), element));
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  spec.form = SqlTypeForm::Array; spec.element = &element;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, type_owner, type));
  const DatumCopyPlan plan(*type);
  DatumParameterArray *array = DatumParameterArray::alloc(source_owner, 2);
  ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(INT64_MIN, source_owner));
  ASSERT_EQ(OB_SUCCESS, array->data_[1].set_int(77, source_owner));
  DatumParameter source;
  ASSERT_EQ(OB_SUCCESS, source.set_extend(reinterpret_cast<int64_t>(array), T_EXT_SQL_ARRAY, source_owner));
  Datum borrowed, copied; copied.set_int(91);
  alignas(8) char scratch[64]; ObDatum source_view;
  ASSERT_EQ(OB_SUCCESS, source.export_legacy(source_view, scratch, sizeof(scratch)));
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(source_view, borrowed));
  int64_t size = 123;
  EXPECT_EQ(OB_NOT_SUPPORTED, plan.payload_copy_size(borrowed, size)); EXPECT_EQ(123, size);
  EXPECT_EQ(OB_NOT_SUPPORTED, plan.deep_copy(borrowed, copied, destination_owner));
  EXPECT_EQ(91, copied.get_int()); EXPECT_EQ(0, destination_owner.calls);
  destination_owner.reject = true;
  EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED,
      plan.deep_copy_resource(borrowed, copied, destination_owner, clone_native_sql_array, nullptr));
  EXPECT_EQ(91, copied.get_int());
  destination_owner.reject = false;
  ASSERT_EQ(OB_SUCCESS,
      plan.deep_copy_resource(borrowed, copied, destination_owner, clone_native_sql_array, nullptr));
  ObDatum view;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_legacy(copied, view));
  DatumParameter owned; ASSERT_EQ(OB_SUCCESS, owned.assign_legacy_borrowed(view, type->legacy_meta()));
  const auto *copy = owned.get_array<DatumParameterArray>();
  ASSERT_NE(array, copy); ASSERT_EQ(2, copy->count_);
  array->data_[0].datum_.set_int(0); array->data_[1].datum_.set_int(0);
  EXPECT_EQ(INT64_MIN, copy->data_[0].get_int()); EXPECT_EQ(77, copy->data_[1].get_int());
}

TEST(DatumCopyPlan, same_scope_array_carrier_copy_borrows_pointee_and_preflights_all_writes)
{
  ObArenaAllocator owner;
  SqlType element;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(metadata(ObIntType), ObAccuracy(), element));
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(T_EXT_SQL_ARRAY);
  spec.form = SqlTypeForm::Array; spec.element = &element;
  const SqlType *type = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  const DatumCopyPlan plan(*type);
  auto *array = DatumParameterArray::alloc(owner, 1);
  ASSERT_NE(nullptr, array);
  ASSERT_EQ(OB_SUCCESS, array->data_[0].set_int(17, owner));
  alignas(8) char wrapper[16]; MEMSET(wrapper, 0x5a, sizeof(wrapper));
  const int64_t address = reinterpret_cast<int64_t>(array);
  MEMCPY(wrapper, &address, sizeof(address));
  Datum source, copied; copied.set_int(91);
  ASSERT_EQ(OB_SUCCESS, source.set_reference(wrapper, sizeof(address)));
  int64_t bytes = -1;
  ASSERT_EQ(OB_SUCCESS, plan.carrier_span_size(source, bytes)); EXPECT_EQ(8, bytes);
  EXPECT_EQ(OB_NOT_SUPPORTED, plan.payload_copy_size(source, bytes)); EXPECT_EQ(8, bytes);
  int64_t position = 4;
  char before[16]; MEMCPY(before, wrapper, sizeof(wrapper));
  EXPECT_EQ(OB_BUF_NOT_ENOUGH,
      plan.copy_carrier_span_borrowing_resources(source, copied, wrapper, 11, position));
  EXPECT_EQ(4, position); EXPECT_EQ(91, copied.get_int());
  EXPECT_EQ(0, MEMCMP(before, wrapper, sizeof(wrapper)));
  ASSERT_EQ(OB_SUCCESS,
      plan.copy_carrier_span_borrowing_resources(source, copied, wrapper, sizeof(wrapper), position));
  EXPECT_EQ(12, position); EXPECT_EQ(wrapper + 4, copied.get_reference());
  int64_t retained_address = 0; MEMCPY(&retained_address, copied.get_reference(), sizeof(retained_address));
  EXPECT_EQ(address, retained_address); EXPECT_EQ(char(0x5a), wrapper[12]);
  // The wrapper moved/overlapped; its pointee is still the original array.
  array->data_[0].datum_.set_int(33);
  EXPECT_EQ(33, reinterpret_cast<const DatumParameterArray *>(retained_address)->data_[0].get_int());
  position = 4;
  ASSERT_EQ(OB_SUCCESS,
      plan.copy_carrier_span_borrowing_resources(copied, copied, wrapper, sizeof(wrapper), position));
  EXPECT_EQ(12, position); EXPECT_EQ(wrapper + 4, copied.get_reference());
  ASSERT_EQ(OB_SUCCESS, source.set_reference(wrapper, 7));
  position = 0; bytes = 91;
  EXPECT_EQ(OB_INVALID_ARGUMENT, plan.carrier_span_size(source, bytes)); EXPECT_EQ(91, bytes);
  EXPECT_EQ(OB_INVALID_ARGUMENT,
      plan.copy_carrier_span_borrowing_resources(source, copied, wrapper, sizeof(wrapper), position));
  EXPECT_EQ(0, position); EXPECT_EQ(wrapper + 4, copied.get_reference());
}


namespace {
const SqlType *inline_record_type(ObIAllocator &owner)
{
  SqlTypeSpec spec;
  spec.attributes.meta_.set_ext(); spec.attributes.meta_.set_extend_type(1);
  spec.attributes.set_udt_id(907); spec.form = SqlTypeForm::Record;
  spec.pl_declaration.source = SqlPLDeclarationSource::Local;
  spec.pl_declaration.tenant_id = OB_SERVER_RUNTIME_ID;
  spec.pl_declaration.declaration_id = 907;
  spec.pl_declaration.compilation_scope.low = 0x97531;
  const SqlType *type = nullptr;
  EXPECT_EQ(OB_SUCCESS, SqlType::create(spec, owner, type));
  return type;
}
struct InlineCloneProbe { int calls = 0; int result = OB_SUCCESS; int64_t address = 0; };
int inline_clone_probe(const SqlType &, const Datum &source, ObIAllocator &, Datum &out, void *opaque)
{
  auto &probe = *static_cast<InlineCloneProbe *>(opaque);
  ++probe.calls;
  EXPECT_TRUE(source.is_inline_extended());
  if (probe.result == OB_SUCCESS) {
    out.set_inline_extended(probe.address, source.get_extend_type(), source.get_extend_size(),
        source.get_extend_collation_level(), source.get_extend_collation(),
        source.inline_extended_payload_size() == DATUM_LEGACY_EXTEND_SIZE);
  }
  return probe.result;
}
}

TEST(DatumCopyPlan, inline_pl_carrier_keeps_explicit_borrow_and_resource_clone_boundaries)
{
  ObArenaAllocator type_owner;
  const SqlType *type = inline_record_type(type_owner); ASSERT_NE(nullptr, type);
  const DatumCopyPlan plan(*type);
  CopyPlanAllocator reject; reject.reject = true;
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    Datum source; source.set_inline_extended(65536, 1, INT32_MIN, 0xab, 0xcd, legacy16);
    ObDatum view; char scratch[64];
    ASSERT_EQ(OB_SUCCESS, plan.legacy_view(source, view, scratch, sizeof(scratch)));
    EXPECT_EQ(scratch, view.ptr_); EXPECT_EQ(legacy16 ? 16 : 15, view.len_);
    Datum borrowed;
    ASSERT_EQ(OB_SUCCESS, plan.import_legacy(view, borrowed));
    EXPECT_FALSE(borrowed.is_inline_extended()); EXPECT_EQ(scratch, borrowed.get_reference());
    ObDatum sentinel; sentinel.set_null();
    EXPECT_EQ(OB_NOT_SUPPORTED, plan.borrow_legacy(source, sentinel)); EXPECT_TRUE(sentinel.is_null());
    Datum output; output.set_int(91);
    int64_t bytes = 99, position = 0;
    ASSERT_EQ(OB_SUCCESS, plan.carrier_span_size(source, bytes)); EXPECT_EQ(0, bytes);
    ASSERT_EQ(OB_SUCCESS, plan.copy_carrier_span_borrowing_resources(source, output, nullptr, 0, position));
    EXPECT_TRUE(output.is_inline_extended()); EXPECT_EQ(65536, output.get_ext()); EXPECT_EQ(0, position);
    output.set_int(91); bytes = 99;
    EXPECT_EQ(OB_NOT_SUPPORTED, plan.payload_copy_size(source, bytes)); EXPECT_EQ(99, bytes);
    EXPECT_EQ(OB_NOT_SUPPORTED, plan.deep_copy(source, output, reject)); EXPECT_EQ(91, output.get_int());
    InlineCloneProbe probe; probe.result = OB_ALLOCATE_MEMORY_FAILED;
    EXPECT_EQ(OB_ALLOCATE_MEMORY_FAILED, plan.deep_copy_resource(source, output, reject, inline_clone_probe, &probe));
    EXPECT_EQ(1, probe.calls); EXPECT_EQ(91, output.get_int());
    probe.result = OB_SUCCESS; probe.address = 131072;
    ASSERT_EQ(OB_SUCCESS, plan.deep_copy_resource(source, output, reject, inline_clone_probe, &probe));
    EXPECT_EQ(2, probe.calls); EXPECT_TRUE(output.is_inline_extended()); EXPECT_EQ(131072, output.get_ext());
    EXPECT_EQ(INT32_MIN, output.get_extend_size()); EXPECT_EQ(0xab, output.get_extend_collation_level());
    EXPECT_EQ(0xcd, output.get_extend_collation()); EXPECT_EQ(0, reject.calls);
  }
}

TEST(DatumCopyPlan, typed_retaining_and_array_leaf_preserve_inline_carrier_after_scratch_retirement)
{
  ObArenaAllocator source_owner;
  const SqlType *type = inline_record_type(source_owner); ASSERT_NE(nullptr, type);
  for (bool legacy16 : {false, true}) {
    SCOPED_TRACE(legacy16);
    Datum inline_value; inline_value.set_inline_extended(65536, 1, -73, 0xa7, 0xef, legacy16);
    char wrapper[16]; ASSERT_EQ(OB_SUCCESS, inline_value.write_inline_extended_payload(wrapper, sizeof(wrapper)));
    NativeTypedDatum input, retained;
    input.type_ = type;
    ASSERT_EQ(OB_SUCCESS, input.datum_.set_reference(wrapper, legacy16 ? 16 : 15, Datum::LegacyExtended));
    CopyPlanAllocator target;
    ASSERT_EQ(OB_SUCCESS, clone_native_parameter_value(input, target, retained));
    ASSERT_NE(nullptr, retained.type_); EXPECT_NE(type, retained.type_); EXPECT_TRUE(type->equals(*retained.type_));
    EXPECT_TRUE(retained.datum_.is_inline_extended()); EXPECT_EQ(0, target.carrier_calls);
    MEMSET(wrapper, 0x5a, sizeof(wrapper));
    EXPECT_EQ(65536, retained.datum_.get_ext()); EXPECT_EQ(-73, retained.datum_.get_extend_size());
    EXPECT_EQ(legacy16 ? 16U : 15U, retained.datum_.inline_extended_payload_size());
    auto *array = DatumParameterArray::alloc(target, 1); ASSERT_NE(nullptr, array);
    DatumParameterMetadata attributes; attributes.type_meta_ = type->legacy_meta();
    attributes.param_meta_ = type->legacy_meta(); attributes.raw_text_pos_ = 17;
    ASSERT_EQ(OB_SUCCESS, array->set_native_element_borrowed_type(0, retained.datum_, *retained.type_, attributes, target));
    Datum leaf; const SqlType *leaf_type = nullptr;
    ASSERT_EQ(OB_SUCCESS, array->get_native_element(0, leaf, leaf_type));
    ASSERT_TRUE(leaf.is_inline_extended()); EXPECT_EQ(retained.type_, leaf_type);
    EXPECT_EQ(65536, leaf.get_ext()); EXPECT_EQ(-73, leaf.get_extend_size());
    EXPECT_EQ(0xa7, leaf.get_extend_collation_level()); EXPECT_EQ(0xef, leaf.get_extend_collation());
    EXPECT_EQ(17, array->data_[0].get_raw_text_pos()); EXPECT_EQ(0, target.carrier_calls);
    // A failed binding must not publish a value or replace the caller's Type.
    const SqlType *sentinel_type = type;
    leaf.set_int(99); array->data_[0].get_meta().set_int();
    EXPECT_EQ(OB_INVALID_ARGUMENT, array->get_native_element(0, leaf, sentinel_type));
    EXPECT_EQ(99, leaf.get_int()); EXPECT_EQ(type, sentinel_type);
  }
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
