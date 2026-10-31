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

#include "sql/engine/table/native_point_get_input.h"
#include "lib/charset/ob_charset.h"
#include <cstring>
#include <type_traits>
#include <gtest/gtest.h>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace
{
static_assert(!std::is_copy_constructible<NativePointGetInput>::value, "A published owner cannot move");
static_assert(!std::is_copy_assignable<NativePointGetInput>::value, "A published owner cannot copy");
static_assert(!std::is_move_constructible<NativePointGetInput>::value, "Views require a stable address");
static_assert(!std::is_move_assignable<NativePointGetInput>::value, "Views require a stable address");

void prepare_scalar_type(SqlType &type, SqlValueType kind,
                         ObPrecision precision = PRECISION_UNKNOWN_YET, ObScale scale = -1)
{
  SqlTypeMeta meta;
  meta.set_type(kind);
  if (kind == ObDecimalIntType) { meta.set_scale(scale); }
  ObAccuracy accuracy;
  accuracy.set_precision(precision); accuracy.set_scale(scale);
  ASSERT_EQ(OB_SUCCESS, SqlType::from_legacy(meta, accuracy, type));
}

void assert_same_key(const NativePointGetInput &owner, const int64_t (&expected)[3])
{
  ASSERT_EQ(1, owner.lookup_keys().count());
  const auto &key = owner.lookup_keys().at(0);
  ASSERT_TRUE(key.is_valid());
  ASSERT_EQ(3, key.count_);
  ASSERT_NE(nullptr, key.cached_rowkey_);
  ASSERT_TRUE(key.cached_rowkey_->is_valid());
  ASSERT_EQ(3, key.cached_rowkey_->get_value_count());
  SqlTypeMeta meta; meta.set_int();
  DatumValue baseline[3];
  for (int i = 0; i < 3; ++i) {
    const uintptr_t begin = reinterpret_cast<uintptr_t>(owner.values() + i);
    const uintptr_t address = reinterpret_cast<uintptr_t>(key.datums_[i].ptr_);
    EXPECT_GE(address, begin); EXPECT_LE(address + sizeof(int64_t), begin + sizeof(Datum));
    EXPECT_EQ(0U, address % alignof(int64_t));
    EXPECT_FALSE(key.datums_[i].is_local_buf());
    EXPECT_EQ(expected[i], owner.values()[i].get_int());
    EXPECT_EQ(expected[i], key.datums_[i].get_int());
    EXPECT_EQ(expected[i], key.cached_rowkey_->get_cell(i).get_int());
    EXPECT_EQ(key.datums_[i].ptr_, key.cached_rowkey_->get_cell(i).datum_.ptr_);
    EXPECT_EQ(ObIntType, key.cached_rowkey_->get_cell(i).get_type());
    ObDatum legacy;
    legacy.ptr_ = reinterpret_cast<const char *>(&expected[i]);
    legacy.pack_ = sizeof(int64_t);
    baseline[i] = DatumValue(legacy, meta);
  }
  const ObRowkey reference(baseline, 3);
  EXPECT_EQ(0, key.cached_rowkey_->get_rowkey().compare(reference));
  EXPECT_EQ(reference.murmurhash(0), key.cached_rowkey_->murmurhash(0));
}
}

TEST(NativePointGetInput, source_overwrite_cannot_change_owned_inline_keys_or_their_cached_views)
{
  const int64_t expected[] = {INT64_MIN, 0, INT64_MAX};
  Datum source[3];
  for (int i = 0; i < 3; ++i) { source[i].set_int(expected[i]); }
  NativePointGetInput owner;
  EXPECT_EQ(0, owner.lookup_keys().count());
  const int64_t group_id = int64_t(7) << 32;
  ASSERT_EQ(OB_SUCCESS, owner.init(source, group_id));
  for (Datum &value : source) { value.set_null(); }
  assert_same_key(owner, expected);
  const auto &key = owner.lookup_keys().at(0);
  EXPECT_EQ(group_id, key.group_id_);
  EXPECT_EQ(group_id, key.cached_rowkey_->get_group_idx());
}

TEST(NativePointGetInput, rebind_after_consumers_finish_refreshes_views_and_cached_hash)
{
  Datum source[3];
  NativePointGetInput owner;
  const int64_t first[] = {1, 2, 3};
  for (int i = 0; i < 3; ++i) { source[i].set_int(first[i]); }
  ASSERT_EQ(OB_SUCCESS, owner.init(source, 0));
  assert_same_key(owner, first);
  const auto *storage = owner.lookup_keys().at(0).datums_;
  const auto *cached = owner.lookup_keys().at(0).cached_rowkey_;
  const int64_t second[] = {INT64_MAX, -19, INT64_MIN};
  for (int i = 0; i < 3; ++i) { source[i].set_int(second[i]); }
  ASSERT_EQ(OB_SUCCESS, owner.init(source, int64_t(11) << 32));
  EXPECT_EQ(storage, owner.lookup_keys().at(0).datums_);
  EXPECT_EQ(cached, owner.lookup_keys().at(0).cached_rowkey_);
  assert_same_key(owner, second);
}

TEST(NativePointGetInput, invalid_columns_do_not_publish_or_replace_an_existing_key)
{
  const int64_t expected[] = {17, -23, 31};
  for (int column = 0; column < 3; ++column) {
    for (int state = 0; state < 5; ++state) {
      Datum source[3];
      for (int i = 0; i < 3; ++i) { source[i].set_int(expected[i]); }
      NativePointGetInput owner, empty;
      ASSERT_EQ(OB_SUCCESS, owner.init(source, int64_t(5) << 32));
      char bytes[8] = {};
      switch (state) {
        case 0: source[column].set_null(); break;
        case 1: source[column].set_min(); break;
        case 2: source[column].set_nop(); break;
        case 3: ASSERT_EQ(OB_SUCCESS, source[column].set_reference(bytes, sizeof(bytes))); break;
        default: ASSERT_EQ(OB_SUCCESS, source[column].set_reference(nullptr, 0, Datum::OutRow)); break;
      }
      EXPECT_NE(OB_SUCCESS, empty.init(source, 0));
      EXPECT_EQ(0, empty.lookup_keys().count());
      EXPECT_NE(OB_SUCCESS, owner.init(source, 0));
      assert_same_key(owner, expected);
      EXPECT_EQ(int64_t(5) << 32, owner.lookup_keys().at(0).group_id_);
    }
  }
}

TEST(NativePointGetInput, legacy_borrow_uses_inline_scalar_bytes_after_unaligned_import)
{
  SqlType type;
  prepare_scalar_type(type, ObIntType, 20, 0);
  const DatumCopyPlan plan(type);
  alignas(8) char input[sizeof(int64_t) + 1];
  const int64_t expected = INT64_MIN;
  memcpy(input + 1, &expected, sizeof(expected));
  ObDatum legacy;
  legacy.ptr_ = input + 1; legacy.pack_ = sizeof(expected);
  Datum value;
  ASSERT_EQ(OB_SUCCESS, plan.import_legacy(legacy, value));
  memset(input, 0, sizeof(input));
  ObDatum borrowed;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_legacy(value, borrowed));
  const uintptr_t begin = reinterpret_cast<uintptr_t>(&value);
  const uintptr_t address = reinterpret_cast<uintptr_t>(borrowed.ptr_);
  EXPECT_GE(address, begin); EXPECT_LE(address + sizeof(expected), begin + sizeof(value));
  EXPECT_EQ(0U, address % alignof(int64_t));
  EXPECT_EQ(expected, borrowed.get_int());
  EXPECT_EQ(sizeof(expected), borrowed.len_);
}

TEST(NativePointGetInput, null_can_borrow_but_wrapped_markers_leave_the_output_unchanged)
{
  SqlType type;
  prepare_scalar_type(type, ObIntType, 20, 0);
  const DatumCopyPlan plan(type);
  Datum value;
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, plan.borrow_legacy(value, output));
  EXPECT_TRUE(output.is_null());
  int64_t previous = 71;
  output.ptr_ = reinterpret_cast<const char *>(&previous); output.pack_ = sizeof(previous);
  for (int marker = 0; marker < 3; ++marker) {
    if (marker == 0) { value.set_min(); }
    else if (marker == 1) { value.set_max(); }
    else { value.set_nop(); }
    EXPECT_EQ(OB_NOT_SUPPORTED, plan.borrow_legacy(value, output));
    EXPECT_EQ(reinterpret_cast<const char *>(&previous), output.ptr_);
    EXPECT_EQ(71, output.get_int()); EXPECT_EQ(sizeof(previous), output.len_);
  }
}

TEST(NativePointGetInput, referenced_decimal_borrows_only_without_coefficient_normalization)
{
  SqlType wide, unknown;
  prepare_scalar_type(wide, ObDecimalIntType, 20, 5);
  prepare_scalar_type(unknown, ObDecimalIntType, PRECISION_UNKNOWN_YET, 5);
  const DatumCopyPlan wide_plan(wide), unknown_plan(unknown);
  alignas(8) char bytes[65] = {};
  Datum value;
  ASSERT_EQ(OB_SUCCESS, value.set_reference(bytes + 1, 16));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, wide_plan.borrow_legacy(value, output));
  EXPECT_EQ(bytes + 1, output.ptr_); EXPECT_EQ(16U, output.len_);
  int64_t previous = 97;
  for (uint32_t width : {0U, 4U, 8U, 32U, 64U}) {
    ASSERT_EQ(OB_SUCCESS, value.set_reference(bytes + 1, width));
    output.ptr_ = reinterpret_cast<const char *>(&previous); output.pack_ = sizeof(previous);
    EXPECT_EQ(OB_NOT_SUPPORTED, wide_plan.borrow_legacy(value, output));
    EXPECT_EQ(reinterpret_cast<const char *>(&previous), output.ptr_);
    EXPECT_EQ(97, output.get_int());
    ASSERT_EQ(OB_SUCCESS, unknown_plan.borrow_legacy(value, output));
    EXPECT_EQ(bytes + 1, output.ptr_); EXPECT_EQ(width, output.len_);
  }
}

TEST(NativePointGetInput, narrow_scalar_borrow_uses_the_matching_member_bytes)
{
  SqlType decimal, floating;
  prepare_scalar_type(decimal, ObDecimalIntType, 9, 2);
  prepare_scalar_type(floating, ObFloatType);
  const DatumCopyPlan decimal_plan(decimal), float_plan(floating);
  Datum value;
  ObDatum output;
  value.set_int(INT32_MIN);
  ASSERT_EQ(OB_SUCCESS, decimal_plan.borrow_legacy(value, output));
  int32_t coefficient = 0; memcpy(&coefficient, output.ptr_, sizeof(coefficient));
  EXPECT_EQ(INT32_MIN, coefficient); EXPECT_EQ(sizeof(coefficient), output.len_);
  value.set_float(-1.25F);
  ASSERT_EQ(OB_SUCCESS, float_plan.borrow_legacy(value, output));
  float scalar = 0; memcpy(&scalar, output.ptr_, sizeof(scalar));
  EXPECT_EQ(-1.25F, scalar); EXPECT_EQ(sizeof(scalar), output.len_);
}

TEST(NativePointGetInput, borrowed_string_keeps_the_producers_bytes_and_scalar_shape_failures_are_atomic)
{
  SqlType text, integer;
  prepare_scalar_type(text, ObVarcharType);
  prepare_scalar_type(integer, ObIntType, 20, 0);
  const DatumCopyPlan text_plan(text), integer_plan(integer);
  const char bytes[] = {'a', '\0', 'b'};
  Datum value;
  ASSERT_EQ(OB_SUCCESS, value.set_reference(bytes, sizeof(bytes)));
  ObDatum output;
  ASSERT_EQ(OB_SUCCESS, text_plan.borrow_legacy(value, output));
  EXPECT_EQ(bytes, output.ptr_); EXPECT_EQ(sizeof(bytes), output.len_);
  EXPECT_EQ(0, memcmp(bytes, output.ptr_, sizeof(bytes)));
  const char *previous = output.ptr_;
  const uint32_t descriptor = output.pack_;
  EXPECT_NE(OB_SUCCESS, integer_plan.borrow_legacy(value, output));
  EXPECT_EQ(previous, output.ptr_); EXPECT_EQ(descriptor, output.pack_);
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  if (ObCharset::init_charset() != OB_SUCCESS) { return 1; }
  return RUN_ALL_TESTS();
}
