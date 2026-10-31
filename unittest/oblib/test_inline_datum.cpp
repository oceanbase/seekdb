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

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
