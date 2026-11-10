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

#pragma once

#include "common/object/ob_obj_type.h"

namespace oceanbase { namespace common {

// Datum-native cast-support metadata. Keep this table independent of any
// value conversion function pointers.
inline bool datum_cast_supported(SqlTypeClass input_tc, SqlTypeClass output_tc)
{
  static_assert(ObMaxTC < 32, "cast support mask must fit in uint32_t");
  static constexpr uint32_t supported_output_masks[ObMaxTC] = {
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
  const int input = static_cast<int>(input_tc);
  const int output = static_cast<int>(output_tc);
  return input >= ObNullTC && input < ObMaxTC
      && output >= ObNullTC && output < ObMaxTC
      && 0 != (supported_output_masks[input] & (uint32_t(1) << output));
}

} } // namespace oceanbase::common
