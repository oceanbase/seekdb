/*
 * Copyright (c) 2026 OceanBase.
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

#include <stdint.h>
#include "common/object/ob_obj_type.h"
#include "lib/charset/ob_charset.h"

namespace oceanbase::sql {

// CAST's integer argument is part of compiled expression serialization. String
// length shares the high word with numeric precision and scale.
class CastTypeDescriptor
{
public:
  explicit CastTypeDescriptor(int64_t value = 0) : bits_(static_cast<uint64_t>(value)) {}
  int64_t value() const { return static_cast<int64_t>(bits_); }
  common::ObObjType type() const
  { return static_cast<common::ObObjType>(static_cast<int16_t>(bits_ & UINT16_MAX)); }
  common::ObCollationType collation() const
  { return static_cast<common::ObCollationType>(static_cast<int16_t>((bits_ >> 16) & UINT16_MAX)); }
  int32_t length() const { return static_cast<int32_t>(bits_ >> 32); }
  int16_t precision() const { return static_cast<int16_t>((bits_ >> 32) & UINT16_MAX); }
  int16_t scale() const { return static_cast<int16_t>(bits_ >> 48); }
  common::ObCollationLevel collation_level() const
  { return static_cast<common::ObCollationLevel>(static_cast<int16_t>((bits_ >> 32) & UINT16_MAX)); }
  common::ObGeoType geometry_type() const
  { return static_cast<common::ObGeoType>(static_cast<int16_t>((bits_ >> 16) & UINT16_MAX)); }

  void set_type(common::ObObjType type)
  { bits_ = (bits_ & ~uint64_t(UINT16_MAX)) | static_cast<uint16_t>(type); }
  void set_collation(common::ObCollationType collation)
  { bits_ = (bits_ & ~(uint64_t(UINT16_MAX) << 16)) | (uint64_t(static_cast<uint16_t>(collation)) << 16); }
  void set_length(int32_t length)
  { bits_ = (bits_ & UINT32_MAX) | (uint64_t(static_cast<uint32_t>(length)) << 32); }
  void set_precision(int16_t precision)
  { bits_ = (bits_ & ~(uint64_t(UINT16_MAX) << 32)) | (uint64_t(static_cast<uint16_t>(precision)) << 32); }
  void set_scale(int16_t scale)
  { bits_ = (bits_ & ~(uint64_t(UINT16_MAX) << 48)) | (uint64_t(static_cast<uint16_t>(scale)) << 48); }
  void set_geometry_type(common::ObGeoType type)
  { bits_ = (bits_ & ~(uint64_t(UINT16_MAX) << 16)) | (uint64_t(static_cast<uint16_t>(type)) << 16); }
  void set_subschema_id(uint16_t id)
  {
    bits_ &= ~((uint64_t(UINT16_MAX) << 16) | (uint64_t(UINT16_MAX) << 32));
    bits_ |= (uint64_t(id >> 8) << 16) | (uint64_t(id & UINT8_MAX) << 32);
  }

private:
  uint64_t bits_;
};

} // namespace oceanbase::sql
