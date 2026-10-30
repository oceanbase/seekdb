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
// cloned DatumValue can escape the conversion. Scalars use native widths;
// NUMBER stores its descriptor followed by digits, exactly as ObDatum does.
class DatumCastFrame : public DatumValue
{
public:
  DatumCastFrame() = default;
  explicit DatumCastFrame(const DatumValue &view) : DatumValue(view) {}
  DatumCastFrame(const DatumCastFrame &other) { *this = other; }
  DatumCastFrame &operator=(const DatumCastFrame &other) {
    if (this != &other) {
      static_cast<DatumValue &>(*this) = other;
      const uintptr_t pointer = reinterpret_cast<uintptr_t>(other.datum_.ptr_);
      const uintptr_t begin = reinterpret_cast<uintptr_t>(other.storage_);
      if (!other.is_null() && pointer >= begin && pointer < begin + sizeof(storage_)) {
        const uint64_t offset = pointer - begin;
        OB_ASSERT(other.datum_.len_ <= sizeof(storage_) - offset);
        MEMCPY(storage_ + offset, other.datum_.ptr_, other.datum_.len_);
        datum_.ptr_ = storage_ + offset;
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
    datum_.ptr_ = storage_; datum_.set_number(value);
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
    datum_.set_string(pointer, length);
  }
  void set_string(SqlValueType type, const ObString &value) {
    set_string(type, value.ptr(), value.length());
  }
  void set_varchar(const ObString &value) {
    meta_.set_varchar(); meta_.set_collation_level(CS_LEVEL_IMPLICIT); datum_.set_string(value);
  }
  void set_varchar(const char *pointer, int32_t length) {
    set_varchar(ObString(length, pointer));
  }
  void set_lob_value(SqlValueType type, const char *pointer, int32_t length) {
    meta_.set_type(type); meta_.set_inrow();
    if (type != ObCollectionSQLType) { meta_.set_collation_level(CS_LEVEL_IMPLICIT); }
    datum_.set_string(pointer, length);
  }
  void set_decimal_int(int32_t bytes, ObScale scale, ObDecimalInt *coefficient) {
    meta_.set_decimal_int(scale); datum_.set_decimal_int_shallow(coefficient, bytes);
  }
  // Decimal conversion scratch copies only coefficient bytes. This is a
  // native payload view owned by the cast arena until the public final clone.
  int64_t get_deep_copy_size() const { OB_ASSERT(is_decimal_int()); return datum_.len_; }
  int deep_copy(const DatumCastFrame &source, char *buffer, int64_t length, int64_t &position) {
    if (!source.is_decimal_int() || position < 0 || length < position
        || source.datum_.len_ > length - position
        || (source.datum_.len_ != 0 && buffer == nullptr)) { return OB_INVALID_ARGUMENT; }
    if (source.datum_.len_ != 0) { MEMMOVE(buffer + position, source.datum_.ptr_, source.datum_.len_); }
    meta_ = source.meta_;
    datum_.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(
        buffer == nullptr ? nullptr : buffer + position), source.datum_.len_);
    position += source.datum_.len_;
    return OB_SUCCESS;
  }
private:
  template <typename T> void numeric(SqlValueType type, T value) {
    MEMCPY(storage_, &value, sizeof(value)); datum_.set_string(storage_, sizeof(value));
    meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_NUMERIC);
  }
  template <typename T> void scalar(T value, void (SqlTypeMeta::*setter)()) {
    MEMCPY(storage_, &value, sizeof(value)); datum_.set_string(storage_, sizeof(value));
    (meta_.*setter)();
  }
  alignas(8) char storage_[64];
};
} } }
