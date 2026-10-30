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

#include "share/datum/datum_cast_support.h"
#include <gtest/gtest.h>

namespace oceanbase { namespace common {

TEST(DatumCastSupport, matches_golden_type_class_matrix)
{
  // Golden support bits captured from the cast support contract. Bit N means
  // that a cast from this input class to class N is supported.
  static constexpr uint32_t expected_output_masks[ObMaxTC] = {
    0x1ffbffffu, // ObNullTC
    0x1bf9e7feu, // ObIntTC
    0x1bf9e7feu, // ObUIntTC
    0x1bf9e5feu, // ObFloatTC
    0x1bf9e7feu, // ObDoubleTC
    0x1bf9e7feu, // ObNumberTC
    0x1bf9e7feu, // ObDateTimeTC
    0x1bf9e7feu, // ObDateTC
    0x1bf9e7feu, // ObTimeTC
    0x1bf9e7feu, // ObYearTC
    0x1ff9e7feu, // ObStringTC
    0x01180800u, // ObExtendTC
    0x1b1bdfffu, // ObUnknownTC
    0x1ff9e7feu, // ObTextTC
    0x1bf9e7feu, // ObBitTC
    0x1b79e7feu, // ObEnumSetTC
    0x1b1bc7feu, // ObEnumSetInnerTC
    0x1ffbffffu, // ObOTimestampTC
    0x1fffffffu, // ObRawTC
    0x1fffffffu, // ObIntervalTC
    0x1fffffffu, // ObRowIDTC
    0x017b8000u, // ObLobTC
    0x1bfbe7feu, // ObJsonTC
    0x1bf9e7feu, // ObGeometryTC
    0x1fffffffu, // ObUserDefinedSQLTC
    0x1bf9e7feu, // ObDecimalIntTC
    0x05398000u, // ObCollectionSQLTC
    0x1ffbffffu, // ObMySQLDateTC
    0x1ffbffffu, // ObMySQLDateTimeTC
  };
  static_assert(sizeof(expected_output_masks) / sizeof(expected_output_masks[0]) == ObMaxTC,
                "golden matrix must cover every type class");
  for (int input_tc = ObNullTC; input_tc < ObMaxTC; ++input_tc) {
    for (int output_tc = ObNullTC; output_tc < ObMaxTC; ++output_tc) {
      const bool expected = 0 != (expected_output_masks[input_tc] & (uint32_t(1) << output_tc));
      EXPECT_EQ(expected,
                datum_cast_supported(static_cast<SqlTypeClass>(input_tc),
                                     static_cast<SqlTypeClass>(output_tc)))
          << "input tc=" << input_tc << " output tc=" << output_tc;
    }
  }
  EXPECT_FALSE(datum_cast_supported(ObMaxTC, ObNullTC));
  EXPECT_FALSE(datum_cast_supported(ObNullTC, ObActualMaxTC));
}

} } // namespace oceanbase::common
