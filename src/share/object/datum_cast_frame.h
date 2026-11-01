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
#include "common/datum/datum_value.h"
namespace oceanbase { namespace common { namespace detail {
// A temporary native result slot, like an expression's Datum plus result
// buffer. There is no legacy value union or layout compatibility. Only a
// cloned DatumValue can escape the conversion. Scalars are inline; NUMBER
// owns its compact descriptor and digits in the bounded local result buffer.
class DatumCastFrame : public DatumValue
{
public:
  DatumCastFrame() = default;
  explicit DatumCastFrame(const DatumValue &view) : DatumValue(view) {}
  DatumCastFrame(const DatumCastFrame &other) { *this = other; }
  DatumCastFrame &operator=(const DatumCastFrame &other) {
    if (this != &other) {
      static_cast<DatumValue &>(*this) = other;
      // Only NUMBER writes this local buffer. Metadata selects Reference;
      // inline numeric payload bits must never be inspected as an address.
      if (!other.is_null() && other.get_type_class() == ObNumberTC) {
        const uintptr_t pointer = reinterpret_cast<uintptr_t>(other.datum_.get_reference());
        const uintptr_t begin = reinterpret_cast<uintptr_t>(other.storage_);
        if (pointer >= begin && pointer < begin + sizeof(storage_)) {
          const uint64_t offset = pointer - begin;
          OB_ASSERT(other.datum_.length() <= sizeof(storage_) - offset);
          MEMCPY(storage_ + offset, other.datum_.get_reference(), other.datum_.length());
          const int ret = datum_.set_reference(storage_ + offset, other.datum_.length(),
                                                other.datum_.reference_flags());
          OB_ASSERT(ret == OB_SUCCESS);
        }
      }
    }
    return *this;
  }
  bool is_json_outrow() const { return meta_.is_json_outrow(); }
  void set_json_value(SqlValueType type, const char *pointer, int32_t length) {
    set_lob_value(type, pointer, length); meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
  }
  bool is_outrow() const { return meta_.is_outrow(); }
  bool is_string_or_lob_locator_type() const { return meta_.is_string_or_lob_locator_type(); }
  void set_inrow() { meta_.set_inrow(); }
  void set_has_lob_header() { meta_.set_has_lob_header(); }
  void set_int(SqlValueType type, int64_t value) { numeric(type, value); }
  void set_int(int64_t value) { numeric(ObIntType, value); }
  void set_uint(SqlValueType type, uint64_t value) { numeric(type, value); }
  void set_uint64(uint64_t value) { numeric(ObUInt64Type, value); }
  void set_float(SqlValueType type, float value) { numeric(type, value); }
  void set_double(SqlValueType type, double value) { numeric(type, value); }
  void set_number(const number::ObNumber &value) { set_number(ObNumberType, value); }
  void set_number(SqlValueType type, const number::ObNumber &value) {
    static_assert(number::ObNumber::MAX_BYTE_LEN <= sizeof(storage_));
    const int ret = datum_.set_number(value, storage_, sizeof(storage_));
    OB_ASSERT(ret == OB_SUCCESS); // MAX_BYTE_LEN is bounded by storage_ above.
    meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_NUMERIC);
  }
  void set_datetime(SqlValueType type, int64_t value) { numeric(type, value); }
  void set_date(int32_t value) { scalar(value, &SqlTypeMeta::set_date); }
  void set_time(int64_t value) { scalar(value, &SqlTypeMeta::set_time); }
  void set_year(uint8_t value) { scalar(value, &SqlTypeMeta::set_year); }
  void set_mysql_date(ObMySQLDate value) { scalar(value, &SqlTypeMeta::set_mysql_date); }
  void set_mysql_date(int32_t value) { scalar(value, &SqlTypeMeta::set_mysql_date); }
  void set_mysql_datetime(ObMySQLDateTime value) { scalar(value, &SqlTypeMeta::set_mysql_datetime); }
  void set_mysql_datetime(int64_t value) { scalar(value, &SqlTypeMeta::set_mysql_datetime); }
  void set_bit(uint64_t value) { scalar(value, &SqlTypeMeta::set_bit); }
  void set_string(SqlValueType type, const char *pointer, int32_t length) {
    meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_IMPLICIT);
    const int ret = datum_.set_string(pointer, length);
    OB_ASSERT(ret == OB_SUCCESS);
  }
  void set_string(SqlValueType type, const ObString &value) {
    set_string(type, value.ptr(), value.length());
  }
  void set_varchar(const ObString &value) {
    const int ret = datum_.set_string(value);
    OB_ASSERT(ret == OB_SUCCESS);
    meta_.set_varchar(); meta_.set_collation_level(CS_LEVEL_IMPLICIT);
  }
  void set_varchar(const char *pointer, int32_t length) {
    set_varchar(ObString(length, pointer));
  }
  void set_lob_value(SqlValueType type, const char *pointer, int32_t length) {
    meta_.set_type(type); meta_.set_inrow();
    if (type != ObCollectionSQLType) { meta_.set_collation_level(CS_LEVEL_IMPLICIT); }
    const int ret = datum_.set_string(pointer, length);
    OB_ASSERT(ret == OB_SUCCESS);
  }
  void set_decimal_int(int32_t bytes, ObScale scale, ObDecimalInt *coefficient) {
    const int ret = datum_.set_reference(coefficient, bytes);
    OB_ASSERT(ret == OB_SUCCESS);
    meta_.set_decimal_int(scale);
  }
  // Decimal conversion scratch copies only coefficient bytes. This is a
  // native payload view owned by the cast arena until the public final clone.
  int64_t get_deep_copy_size() const { OB_ASSERT(is_decimal_int()); return datum_.length(); }
  int deep_copy(const DatumCastFrame &source, char *buffer, int64_t length, int64_t &position) {
    if (!source.is_decimal_int() || position < 0 || length < position
        || source.datum_.length() > length - position
        || (source.datum_.length() != 0 && buffer == nullptr)) { return OB_INVALID_ARGUMENT; }
    if (source.datum_.length() != 0) { MEMMOVE(buffer + position, source.datum_.get_reference(), source.datum_.length()); }
    meta_ = source.meta_;
    const int ret = datum_.set_reference(buffer == nullptr ? nullptr : buffer + position,
                                         source.datum_.length());
    OB_ASSERT(ret == OB_SUCCESS);
    position += source.datum_.length();
    return OB_SUCCESS;
  }
private:
  template <typename T> void numeric(SqlValueType type, T value) {
    if constexpr (std::is_same_v<T, float>) { datum_.set_float(value); }
    else if constexpr (std::is_same_v<T, double>) { datum_.set_double(value); }
    else if constexpr (std::is_unsigned_v<T>) { datum_.set_uint(value); }
    else { datum_.set_int(value); }
    meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_NUMERIC);
  }
  template <typename T> void scalar(T value, void (SqlTypeMeta::*setter)()) {
    if constexpr (std::is_same_v<T, ObMySQLDate>) { datum_.set_mysql_date(value); }
    else if constexpr (std::is_same_v<T, ObMySQLDateTime>) { datum_.set_mysql_datetime(value); }
    else if constexpr (std::is_unsigned_v<T>) { datum_.set_uint(value); }
    else { datum_.set_int(value); }
    (meta_.*setter)();
  }
  alignas(8) char storage_[64];
};
} } }
