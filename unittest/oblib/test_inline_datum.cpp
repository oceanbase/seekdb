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

#include "common/datum/datum.h"
#include "lib/allocator/ob_allocator.h"
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>
#include <gtest/gtest.h>

using namespace oceanbase::common;

TEST(InlineDatum, integers_survive_source_overwrite_copy_and_container_relocation)
{
  const int64_t values[] = {INT64_MIN, -1, 0, 1, INT64_MAX};
  std::vector<Datum> cells;
  for (int64_t value : values) {
    Datum source;
    source.set_int(value);
    cells.push_back(source);
    source.set_null();
  }
  for (int i = 0; i < 128; ++i) {
    Datum cell;
    cell.set_int(i);
    cells.push_back(cell);
  }
  for (size_t i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
    EXPECT_EQ(values[i], cells[i].get_int());
    EXPECT_TRUE(cells[i].is_present());
    EXPECT_EQ(0U, cells[i].length());
    EXPECT_EQ(0U, reinterpret_cast<uintptr_t>(&cells[i]) % alignof(int64_t));
  }
  Datum relocated;
  std::memcpy(&relocated, &cells[0], sizeof(relocated));
  cells[0].set_int(17);
  EXPECT_EQ(INT64_MIN, relocated.get_int());
}

TEST(InlineDatum, unsigned_and_floating_scalars_preserve_their_value_bits)
{
  Datum value;
  value.set_uint(UINT64_MAX);
  EXPECT_EQ(UINT64_MAX, value.get_uint());
  const auto copy = value;
  value.set_int(0);
  EXPECT_EQ(UINT64_MAX, copy.get_uint());
  value.set_float(-1.25F);
  EXPECT_EQ(-1.25F, value.get_float());
  value.set_double(-0.0);
  EXPECT_TRUE(std::signbit(value.get_double()));
  value.set_double(std::numeric_limits<double>::infinity());
  EXPECT_TRUE(std::isinf(value.get_double()));
  value.set_double(std::numeric_limits<double>::quiet_NaN());
  EXPECT_TRUE(std::isnan(value.get_double()));
}

TEST(InlineDatum, references_borrow_bytes_and_keep_empty_separate_from_null)
{
  const std::array<unsigned char, 3> text{{'a', 0, 'b'}};
  Datum value;
  ASSERT_EQ(OB_SUCCESS, value.set_reference(text.data(), text.size(), Datum::OutRow));
  const auto copy = value;
  value.set_null();
  EXPECT_EQ(text.data(), copy.get_reference());
  EXPECT_EQ(text.size(), copy.length());
  EXPECT_EQ(Datum::OutRow, copy.reference_flags());
  EXPECT_EQ(0, std::memcmp(copy.get_reference(), text.data(), text.size()));
  ASSERT_EQ(OB_SUCCESS, value.set_reference(nullptr, 0));
  EXPECT_TRUE(value.is_present());
  EXPECT_EQ(0U, value.length());
  EXPECT_EQ(nullptr, value.get_reference());
  value.set_int(42);
  EXPECT_EQ(OB_INVALID_ARGUMENT, value.set_reference(nullptr, 3));
  EXPECT_EQ(42, value.get_int());
}

TEST(InlineDatum, state_changes_do_not_need_a_type_or_reference_payload)
{
  Datum value;
  EXPECT_TRUE(value.is_null());
  value.set_nop();
  EXPECT_TRUE(value.is_nop());
  value.set_min();
  EXPECT_TRUE(value.is_min());
  value.set_max();
  EXPECT_TRUE(value.is_max());
  value.set_int(INT64_MAX);
  EXPECT_TRUE(value.is_present());
  EXPECT_EQ(INT64_MAX, value.get_int());
  EXPECT_EQ(0U, value.reference_flags());
  value.set_null();
  EXPECT_TRUE(value.is_null());
}

TEST(InlineDatum, number_write_owns_caller_buffer_and_failed_write_does_not_publish)
{
  ObArenaAllocator source_owner;
  number::ObNumber input_number;
  ASSERT_EQ(OB_SUCCESS, input_number.from(INT64_MIN, source_owner));
  alignas(8) char owned[64]; MEMSET(owned, 0x5a, sizeof(owned));
  Datum result; result.set_int(91);
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, result.set_number(input_number, owned, 1));
  EXPECT_EQ(91, result.get_int());
  for (char byte : owned) { EXPECT_EQ(char(0x5a), byte); }
  ASSERT_EQ(OB_SUCCESS, result.set_number(input_number, owned, sizeof(owned)));
  EXPECT_EQ(owned, result.get_reference());
  EXPECT_EQ(0, number::ObNumber(result.get_number()).compare(INT64_MIN));
  MEMSET(const_cast<uint32_t *>(input_number.get_digits()), 0, input_number.get_length() * sizeof(uint32_t));
  EXPECT_EQ(0, number::ObNumber(result.get_number()).compare(INT64_MIN));
  // The compact overload accepts exact source/destination overlap.
  ASSERT_EQ(OB_SUCCESS, result.set_number(result.get_number(), owned, sizeof(owned)));
  EXPECT_EQ(0, number::ObNumber(result.get_number()).compare(INT64_MIN));
  Datum borrowed;
  ASSERT_EQ(OB_SUCCESS, borrowed.set_number_shallow(result.get_number()));
  EXPECT_EQ(result.get_reference(), borrowed.get_reference());
  number::ObNumber zero;
  zero.set_zero();
  ASSERT_EQ(OB_SUCCESS, result.set_number(zero, owned, sizeof(owned)));
  EXPECT_TRUE(result.get_number().is_zero());
  EXPECT_EQ(sizeof(number::ObNumber::Desc), result.length());
}

TEST(InlineDatum, timestamp_bytes_are_copied_before_local_value_dies)
{
  ObOTimestampData source;
  source.time_us_ = -123456789; source.time_ctx_.time_desc_ = 999; source.time_ctx_.tz_desc_ = 0;
  char owned[32]; MEMSET(owned, 0x3c, sizeof(owned));
  Datum value; value.set_int(71);
  EXPECT_EQ(OB_BUF_NOT_ENOUGH, value.set_otimestamp_tiny(source, owned, 9));
  EXPECT_EQ(71, value.get_int()); EXPECT_EQ(char(0x3c), owned[0]);
  ASSERT_EQ(OB_SUCCESS, value.set_otimestamp_tiny(source, owned, sizeof(owned)));
  source.time_us_ = 0; source.time_ctx_.time_desc_ = 0;
  EXPECT_EQ(-123456789, value.get_otimestamp_tiny().time_us_);
  EXPECT_EQ(999, value.get_otimestamp_tiny().time_ctx_.time_desc_);
  EXPECT_EQ(10, value.length());
}

TEST(InlineDatum, scalar_aliases_keep_inline_members_and_reference_flags_are_explicit)
{
  Datum value;
  value.set_bool(true); EXPECT_EQ(1, value.get_int()); EXPECT_EQ(0, value.length());
  value.set_year(UINT8_MAX); EXPECT_EQ(UINT8_MAX, value.get_year());
  value.set_mysql_date(ObMySQLDate(INT32_MIN)); EXPECT_EQ(INT32_MIN, value.get_date());
  value.set_mysql_datetime(ObMySQLDateTime(INT64_MIN)); EXPECT_EQ(INT64_MIN, value.get_datetime());
  value.set_enum(UINT64_MAX); EXPECT_EQ(UINT64_MAX, value.get_enumset());
  char bytes[] = "abc";
  ASSERT_EQ(OB_SUCCESS, value.set_string(bytes, 3));
  EXPECT_EQ(bytes, value.get_string().ptr());
  value.set_has_lob_header(); EXPECT_TRUE(value.has_lob_header());
  value.clear_lob_header(); EXPECT_FALSE(value.has_lob_header());
  value.set_int(7); EXPECT_EQ(0, value.reference_flags());
}

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
