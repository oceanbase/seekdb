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
#include <gtest/gtest.h>
#include <cstring>

using namespace oceanbase::common;

namespace {
class CopyPlanAllocator final : public ObIAllocator
{
public:
  CopyPlanAllocator() : calls(0), reject(false), arena() {}
  void *alloc(int64_t bytes) override { ++calls; return reject ? nullptr : arena.alloc(bytes); }
  void *alloc(int64_t bytes, const ObMemAttr &) override { return alloc(bytes); }
  void free(void *bytes) override { arena.free(bytes); }
  int64_t calls;
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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
