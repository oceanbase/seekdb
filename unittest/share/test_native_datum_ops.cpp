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

#include "share/datum/ob_datum_funcs.h"
#include "common/datum/datum_copy_plan.h"
#include <gtest/gtest.h>
#include <cmath>
#include <cstring>
#include <limits>

using namespace oceanbase::common;
using oceanbase::sql::ObBitVector;

namespace {
ObDatum legacy(const void *bytes, uint32_t size)
{
  ObDatum result; result.ptr_ = static_cast<const char *>(bytes); result.pack_ = size; return result;
}

void check_hashes(const SqlType &type, const Datum &value, const ObDatum &old)
{
  DatumBasicFuncs native;
  ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(type, native));
  const auto *previous = ObDatumFuncs::get_basic_func(type.kind(),
      type.collation() == CS_TYPE_INVALID ? CS_TYPE_BINARY : type.collation(),
      type.has_scale() ? type.scale() : SCALE_UNKNOWN_YET, type.legacy_meta().has_lob_header(),
      type.has_precision() ? type.precision() : PRECISION_UNKNOWN_YET);
  ASSERT_NE(nullptr, previous);
  const DatumHashOp functions[] = {native.default_hash_, native.murmur_hash_, native.xx_hash_,
                                   native.wy_hash_, native.murmur_hash_v2_};
  const ObDatumHashFuncType old_functions[] = {previous->default_hash_, previous->murmur_hash_,
      previous->xx_hash_, previous->wy_hash_, previous->murmur_hash_v2_};
  for (int64_t i = 0; i < 5; ++i) {
    ASSERT_NE(nullptr, old_functions[i]);
    for (uint64_t seed : {uint64_t(0), uint64_t(42), UINT64_MAX}) {
      uint64_t actual = 17, expected = 19;
      const int old_ret = old_functions[i](old, seed, expected, nullptr);
      ASSERT_EQ(old_ret, functions[i](value, seed, actual, nullptr));
      if (old_ret == OB_SUCCESS) { EXPECT_EQ(expected, actual); }
    }
  }
}

SqlTypeSpec decimal(ObPrecision precision, ObScale scale)
{
  SqlTypeSpec spec; spec.attributes.meta_.set_decimal_int(scale);
  spec.attributes.set_precision(precision); spec.attributes.set_scale(scale);
  spec.specified = SqlTypeSpec::Precision | SqlTypeSpec::Scale;
  return spec;
}

void coefficient(char *buffer, int bytes, int64_t value)
{
  MEMSET(buffer, value < 0 ? 0xff : 0, bytes);
  const int retained = bytes < int(sizeof(value)) ? bytes : sizeof(value);
  if (retained > 0) {
#if __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    MEMCPY(buffer + bytes - retained, reinterpret_cast<const char *>(&value) + sizeof(value) - retained, retained);
#else
    MEMCPY(buffer, &value, retained);
#endif
  }
}
}

TEST(NativeDatumOps, integer_hash_and_bound_compare_match_existing_semantics)
{
  SqlType type, unsigned_type;
  SqlTypeMeta signed_meta, unsigned_meta; signed_meta.set_int(); unsigned_meta.set_uint64();
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(signed_meta, ObAccuracy(), type));
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(unsigned_meta, ObAccuracy(), unsigned_type));
  const int64_t signed_values[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
  const uint64_t unsigned_values[] = {0, 1, uint64_t(INT64_MAX), UINT64_MAX};
  DatumCmpOp compare;
  ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_cmp(type, unsigned_type, NULL_FIRST, CS_TYPE_BINARY, compare));
  for (int64_t left : signed_values) {
    Datum l; l.set_int(left); check_hashes(type, l, legacy(&left, sizeof(left)));
    for (uint64_t right : unsigned_values) {
      Datum r; r.set_uint(right); check_hashes(unsigned_type, r, legacy(&right, sizeof(right)));
      int result = 99;
      ASSERT_EQ(OB_SUCCESS, compare(l, r, result));
      const int expected = left < 0 ? -1 : uint64_t(left) < right ? -1 : uint64_t(left) > right ? 1 : 0;
      EXPECT_EQ(expected, result);
    }
  }
}

TEST(NativeDatumOps, decimal_declared_domain_preserves_all_legacy_hash_algorithms)
{
  ObArenaAllocator owner;
  const ObPrecision precisions[] = {9, 18, 20, 40, 80};
  const int widths[] = {4, 8, 16, 32, 64};
  for (int p = 0; p < 5; ++p) {
    for (ObScale scale : {ObScale(-1), ObScale(0), ObScale(2)}) {
      const SqlType *type = nullptr;
      ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal(precisions[p], scale), owner, type));
      const DatumCopyPlan plan(*type);
      DatumCmpOp compare;
      ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_cmp(*type, *type, NULL_FIRST, CS_TYPE_BINARY, compare));
      for (int64_t value : {int64_t(-123456), int64_t(0), int64_t(123456)}) {
        alignas(8) char original[64]; coefficient(original, widths[p], value);
        alignas(8) char short_coefficient[8]; coefficient(short_coefficient, 8, value);
        const int bytes = value == 0 ? 0 : 8;
        Datum native;
        ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(short_coefficient, bytes), native));
        check_hashes(*type, native, legacy(original, widths[p]));
        Datum second;
        ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy(original, widths[p]), second));
        int cmp = 31;
        ASSERT_EQ(OB_SUCCESS, compare(native, second, cmp)); EXPECT_EQ(0, cmp);
      }
    }
  }
}

TEST(NativeDatumOps, decimal_preparation_does_not_silently_compare_different_scales)
{
  ObArenaAllocator owner;
  const SqlType *left = nullptr, *right = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal(20, 2), owner, left));
  ASSERT_EQ(OB_SUCCESS, SqlType::create(decimal(20, 3), owner, right));
  DatumCmpOp compare;
  ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_cmp(*left, *left, NULL_FIRST, CS_TYPE_BINARY, compare));
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDatumFuncs::prepare_cmp(*left, *right, NULL_FIRST, CS_TYPE_BINARY, compare));
  EXPECT_EQ(left, compare.left_type()); EXPECT_EQ(left, compare.right_type());
}

TEST(NativeDatumOps, float_bits_and_fixed_double_domain_keep_old_hashes)
{
  SqlTypeMeta meta; meta.set_double();
  SqlType type, fixed;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(20, 2), fixed));
  const double values[] = {-0.0, 0.0, -1.125, 1.125,
      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity()};
  for (double value : values) {
    Datum native; native.set_double(value);
    check_hashes(type, native, legacy(&value, sizeof(value)));
    if (std::isfinite(value)) { check_hashes(fixed, native, legacy(&value, sizeof(value))); }
  }
  Datum left, right; left.set_double(-0.0); right.set_double(0.0);
  bool equal = true;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(type).binary_equal(left, right, equal)); EXPECT_FALSE(equal);
  DatumBasicFuncs funcs; ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(type, funcs));
  int cmp = 7; ASSERT_EQ(OB_SUCCESS, funcs.null_first_cmp_(left, right, cmp)); EXPECT_EQ(0, cmp);
}

TEST(NativeDatumOps, collation_and_null_order_are_bound_and_reference_bytes_are_borrowed)
{
  SqlTypeMeta meta; meta.set_varchar(); meta.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
  SqlType type; ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
  DatumBasicFuncs funcs; ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(type, funcs));
  char a[] = "AbC  ", b[] = "abc";
  Datum left, right; ASSERT_EQ(OB_SUCCESS, left.set_string(a, 5)); ASSERT_EQ(OB_SUCCESS, right.set_string(b, 3));
  int cmp = 99;
  ASSERT_EQ(OB_SUCCESS, funcs.null_first_cmp_(left, right, cmp)); EXPECT_EQ(0, cmp);
  check_hashes(type, left, legacy(a, 5)); check_hashes(type, right, legacy(b, 3));
  right.set_null();
  ASSERT_EQ(OB_SUCCESS, funcs.null_first_cmp_(left, right, cmp)); EXPECT_GT(cmp, 0);
  ASSERT_EQ(OB_SUCCESS, funcs.null_last_cmp_(left, right, cmp)); EXPECT_LT(cmp, 0);
  ObDatum old_null; old_null.set_null(); check_hashes(type, right, old_null);
}

TEST(NativeDatumOps, explicitly_selected_null_order_and_error_results_are_not_replaced)
{
  SqlTypeMeta meta; meta.set_int();
  SqlType type; ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
  const auto original = ObDatumFuncs::get_nullsafe_cmp_func(ObIntType, ObIntType,
      NULL_LAST, CS_TYPE_BINARY, SCALE_UNKNOWN_YET, false);
  DatumCmpOp cmp;
  ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::bind_legacy_cmp(type, type, original, cmp));
  Datum value, null; value.set_int(7);
  int result = 123;
  ASSERT_EQ(OB_SUCCESS, cmp(value, null, result)); EXPECT_LT(result, 0);
  EXPECT_EQ(OB_INVALID_ARGUMENT, ObDatumFuncs::bind_legacy_cmp(type, type, nullptr, cmp));
  ASSERT_TRUE(cmp.is_valid());
  Datum malformed; char bytes[8] = {}; ASSERT_EQ(OB_SUCCESS, malformed.set_reference(bytes, 8));
  result = 123;
  EXPECT_EQ(OB_INVALID_ARGUMENT, cmp(value, malformed, result)); EXPECT_EQ(123, result);
  DatumBasicFuncs funcs; ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(type, funcs));
  uint64_t hash = 456;
  EXPECT_EQ(OB_INVALID_ARGUMENT, funcs.murmur_hash_v2_(malformed, 42, hash)); EXPECT_EQ(456, hash);
  malformed.set_nop();
  EXPECT_EQ(OB_INVALID_ARGUMENT, cmp(value, malformed, result)); EXPECT_EQ(123, result);
}

TEST(NativeDatumOps, batch_broadcast_skip_null_and_completed_prefix_follow_bound_domain)
{
  SqlTypeMeta meta; meta.set_int();
  SqlType type; ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
  DatumBasicFuncs funcs; ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(type, funcs));
  const auto *previous = ObDatumFuncs::get_basic_func(ObIntType, CS_TYPE_BINARY);
  alignas(8) char skip_memory[8]; auto *skip = new (skip_memory) ObBitVector();
  skip->init(5); skip->set(2);
  int64_t values[] = {INT64_MIN, 0, 17, 77, INT64_MAX};
  Datum native[5]; ObDatum old[5];
  for (int i = 0; i < 5; ++i) { native[i].set_int(values[i]); old[i] = legacy(&values[i], 8); }
  native[1].set_null(); old[1].set_null();
  uint64_t seeds[] = {1, 2, 3, 4, 5};
  for (bool batch_value : {false, true}) {
    for (bool batch_seed : {false, true}) {
      uint64_t actual[] = {91, 92, 93, 94, 95}, expected[] = {91, 92, 93, 94, 95};
      previous->murmur_hash_v2_batch_(expected, old, batch_value, *skip, 5, seeds, batch_seed, nullptr);
      ASSERT_EQ(OB_SUCCESS, funcs.murmur_hash_v2_batch_(actual, native, batch_value, *skip, 5, seeds, batch_seed));
      for (int i = 0; i < 5; ++i) { EXPECT_EQ(expected[i], actual[i]); }
    }
  }
  native[3].set_nop();
  uint64_t output[] = {91, 92, 93, 94, 95};
  EXPECT_EQ(OB_INVALID_ARGUMENT, funcs.murmur_hash_v2_batch_(output, native, true, *skip, 5, seeds, true));
  uint64_t first = 0, second = 0;
  ASSERT_EQ(OB_SUCCESS, previous->murmur_hash_v2_(old[0], seeds[0], first, nullptr));
  ASSERT_EQ(OB_SUCCESS, previous->murmur_hash_v2_(old[1], seeds[1], second, nullptr));
  EXPECT_EQ(first, output[0]); EXPECT_EQ(second, output[1]);
  EXPECT_EQ(93, output[2]); EXPECT_EQ(94, output[3]); EXPECT_EQ(95, output[4]);
}

TEST(NativeDatumOps, number_and_enum_use_the_existing_semantic_tables)
{
  ObArenaAllocator owner;
  SqlTypeMeta number_meta; number_meta.set_number();
  SqlType number_type;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(number_meta, ObAccuracy(), number_type));
  for (int64_t integer : {INT64_MIN, int64_t(0), INT64_MAX}) {
    number::ObNumber number;
    ASSERT_EQ(OB_SUCCESS, number.from(integer, owner));
    alignas(8) char old_bytes[64], native_bytes[64];
    ObDatum old = legacy(old_bytes, sizeof(old_bytes)); old.set_number(number);
    Datum native;
    ASSERT_EQ(OB_SUCCESS, native.set_number(number, native_bytes, sizeof(native_bytes)));
    check_hashes(number_type, native, old);
  }
  ObString labels[] = {ObString("first"), ObString("second")};
  SqlTypeSpec spec;
  spec.attributes.meta_.set_enum(); spec.form = SqlTypeForm::Enum;
  spec.labels = labels; spec.label_count = 2;
  const SqlType *enumeration = nullptr;
  ASSERT_EQ(OB_SUCCESS, SqlType::create(spec, owner, enumeration));
  const uint64_t ordinal = 2;
  Datum native; native.set_enum(ordinal);
  check_hashes(*enumeration, native, legacy(&ordinal, sizeof(ordinal)));
}

TEST(NativeDatumOps, explicit_bounds_and_empty_references_do_not_read_missing_payload)
{
  SqlTypeMeta meta; meta.set_int();
  SqlType type; ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, ObAccuracy(), type));
  DatumBasicFuncs funcs; ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::prepare_basic(type, funcs));
  Datum min, max, ordinary, null;
  min.set_min(); max.set_max(); ordinary.set_int(7);
  int cmp = 9;
  for (const Datum *value : {&ordinary, &max}) {
    ASSERT_EQ(OB_SUCCESS, funcs.null_first_cmp_(min, *value, cmp)); EXPECT_LT(cmp, 0);
    ASSERT_EQ(OB_SUCCESS, funcs.null_last_cmp_(min, *value, cmp)); EXPECT_LT(cmp, 0);
  }
  ASSERT_EQ(OB_SUCCESS, funcs.null_first_cmp_(min, null, cmp)); EXPECT_GT(cmp, 0);
  ASSERT_EQ(OB_SUCCESS, funcs.null_last_cmp_(min, null, cmp)); EXPECT_LT(cmp, 0);
  ASSERT_EQ(OB_SUCCESS, funcs.null_last_cmp_(max, null, cmp)); EXPECT_LT(cmp, 0);
  ASSERT_EQ(OB_SUCCESS, funcs.null_first_cmp_(max, max, cmp)); EXPECT_EQ(0, cmp);
  const auto *extended = ObDatumFuncs::get_basic_func(ObExtendType, CS_TYPE_BINARY);
  ASSERT_NE(nullptr, extended);
  alignas(8) char bytes[DATUM_LEGACY_EXTEND_SIZE];
  ObDatum old = legacy(bytes, sizeof(bytes));
  old.set_extend(DATUM_MIN_EXT_VALUE, 0, 0, meta.get_cs_level(), meta.get_cs_type());
  uint64_t actual = 0, expected = 0;
  ASSERT_EQ(OB_SUCCESS, extended->murmur_hash_v2_(old, 42, expected, nullptr));
  ASSERT_EQ(OB_SUCCESS, funcs.murmur_hash_v2_(min, 42, actual)); EXPECT_EQ(expected, actual);
  DatumHashOp unbound_state;
  ASSERT_EQ(OB_SUCCESS, ObDatumFuncs::bind_legacy_hash(type, extended->murmur_hash_v2_, unbound_state));
  actual = 91;
  EXPECT_EQ(OB_NOT_SUPPORTED, unbound_state(min, 42, actual)); EXPECT_EQ(91, actual);

  SqlTypeMeta text_meta; text_meta.set_varchar();
  text_meta.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  SqlType text;
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(text_meta, ObAccuracy(), text));
  Datum a, b; ASSERT_EQ(OB_SUCCESS, a.set_reference(nullptr, 0));
  ASSERT_EQ(OB_SUCCESS, b.set_reference(nullptr, 0));
  bool equal = false;
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(text).binary_equal(a, b, equal)); EXPECT_TRUE(equal);
  b.set_null();
  ASSERT_EQ(OB_SUCCESS, DatumCopyPlan(text).binary_equal(a, b, equal)); EXPECT_FALSE(equal);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
