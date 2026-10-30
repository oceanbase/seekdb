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

#include "common/datum/datum_core.h"
#include "lib/container/ob_iarray.h"

namespace oceanbase
{
namespace common
{

// A compact typed Datum view for constants and PL members. Metadata is adjacent to the native value;
// payload ownership belongs to the containing plan/statement arena. Parameter
// accuracy, result flags and wire state belong in DatumParameter, not here.
struct ObValuePrintParams;
// Defined where DatumParameterArray is complete. Native value views avoid an
// include cycle while retaining the decoded array value's old extent size.
int32_t datum_array_source_value_size(const ObDatum &address);

struct DatumValue
{
  DatumValue() : datum_(), meta_() { datum_.set_null(); }
  DatumValue(const ObDatum &datum, const SqlTypeMeta &meta) : datum_(datum), meta_(meta) {}
  const SqlTypeMeta &get_meta() const { return meta_; }
  SqlTypeMeta &get_meta() { return meta_; }
  SqlValueType get_type() const { return meta_.get_type(); }
  void set_type(SqlValueType type) { if (type == ObNullType) { set_null(); } else { meta_.set_type(type); } }
  bool is_null() const { return datum_.is_null(); }
  void set_null() { datum_.set_null(); meta_.set_null(); }
  bool is_ext() const { return meta_.is_ext(); }
  bool is_invalid_type() const { return !meta_.is_valid(); }
  bool is_decimal_int() const { return ob_is_decimal_int_tc(get_type()); }
  bool is_varchar_or_char() const { return meta_.is_varchar_or_char(); }
  bool is_lob_storage() const { return meta_.is_lob_storage(); }
  bool has_lob_header() const { return meta_.has_lob_header(); }
  uint8_t get_extend_type() const { return meta_.get_extend_type(); }
  ObCollationType get_collation_type() const { return meta_.get_collation_type(); }
  ObScale get_scale() const { return meta_.get_scale(); }
  int64_t get_int() const { return datum_.get_int(); }
  int64_t get_ext() const
  { return is_null() ? 0 : datum_.has_ext_value() ? datum_.get_ext() : datum_.get_int(); }
  int32_t get_int_bytes() const { return datum_.get_int_bytes(); }
  const ObDecimalInt *get_decimal_int() const { return datum_.get_decimal_int(); }
  ObString get_string() const { return datum_.get_string(); }
  int64_t get_data_length() const { return datum_.len_; }
  int32_t get_val_len() const {
    if (is_null()) { return 0; }
    if (is_ext_sql_array()) { return datum_array_source_value_size(datum_); }
    if (is_ext()) { return datum_.has_ext_value() ? datum_.get_extend_size() : 0; }
    if (ob_is_number_tc(get_type())) { return static_cast<int32_t>(datum_.get_number_desc().desc_); }
    return is_string_type() || is_json() || is_geometry() || is_decimal_int()
        || is_collection_sql_type() ? datum_.len_ : 0;
  }
  // Normalize this typed value to the common row Datum representation used
  // by persisted default checksums. Non-extended payloads borrow this value;
  // Ext payloads borrow the caller's stack storage. Current metadata, rather
  // than an earlier Ext payload snapshot, defines subtype/collation bytes.
  // This is a native conversion: no value object or heap allocation is used.
  int to_row_datum(ObDatum &output,
                   char (&extended_payload)[DATUM_EXTEND_PAYLOAD_SIZE]) const
  {
    int ret = OB_SUCCESS;
    ObDatum candidate;
    if (!meta_.is_valid()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (is_null() || meta_.is_null()) {
      candidate.set_null();
    } else if (is_ext()) {
      if ((is_ext_sql_array() && (datum_.ptr_ == nullptr || datum_.len_ != sizeof(int64_t)))
          || (!is_ext_sql_array() && (datum_.ptr_ == nullptr || !datum_.has_ext_value()))) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        candidate.ptr_ = extended_payload;
        candidate.set_extend(get_ext(), meta_.get_extend_type(), get_val_len(),
                             meta_.get_cs_level(), meta_.get_cs_type());
      }
    } else {
      const SqlValueDatumMapType mapping = ObDatum::get_sql_value_datum_map_type(get_type());
      const bool fixed = mapping == OBJ_DATUM_8BYTE_DATA
          || mapping == OBJ_DATUM_4BYTE_DATA || mapping == OBJ_DATUM_1BYTE_DATA;
      if ((datum_.len_ > 0 && datum_.ptr_ == nullptr)
          || (fixed && datum_.len_ != ObDatum::get_reserved_size(mapping))
          || (mapping == OBJ_DATUM_NUMBER && (datum_.ptr_ == nullptr
              || datum_.len_ < sizeof(number::ObNumber::Desc)
              || datum_.len_ != sizeof(number::ObNumber::Desc)
                  + sizeof(uint32_t) * datum_.get_number_desc().len_))) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        candidate = datum_;
        candidate.pack_ = datum_.len_;
        if (mapping == OBJ_DATUM_STRING && meta_.is_outrow()) { candidate.set_outrow(); }
      }
    }
    if (ret == OB_SUCCESS) { output = candidate; }
    return ret;
  }
  bool is_pl_extend() const {
    if (is_null() || !is_ext()) { return false; }
    const int64_t pointer = get_ext();
    return !(ObActionFlag::OP_MIN_OBJ <= pointer && pointer <= ObActionFlag::OP_LOCK_ROW)
        && pointer != DATUM_MIN_EXT_VALUE && pointer != DATUM_MAX_EXT_VALUE
        && get_extend_type() > 0 && get_extend_type() < T_EXT_SQL_ARRAY;
  }
  void reset() { *this = DatumValue(); }
  SqlTypeClass get_type_class() const { return sql_type_class(get_type()); }
  ObCollationLevel get_collation_level() const { return meta_.get_collation_level(); }
  void set_collation_type(ObCollationType cs) { meta_.set_collation_type(cs); }
  void set_collation_level(ObCollationLevel level) { meta_.set_collation_level(level); }
  void set_collation(const SqlTypeMeta &meta) { meta_.set_collation(meta); }
  void set_meta_type(const SqlTypeMeta &meta) { meta_ = meta; }
  void set_scale(ObScale scale) { meta_.set_scale(scale); }
  void set_subschema_id(uint16_t id) { meta_.set_subschema_id(id); }
  uint16_t get_subschema_id() const { return meta_.get_subschema_id(); }
  bool is_unknown() const { return get_type() == ObUnknownType; }
  bool is_min_value() const { return datum_.is_min(); }
  bool is_max_value() const { return datum_.is_max(); }
  // Bound markers borrow immutable native payload bytes with static lifetime.
  // Every data setter still publishes its own allocator-owned payload.
  void set_min_value() { set_bound_marker(DATUM_MIN_EXT_VALUE); }
  void set_max_value() { set_bound_marker(DATUM_MAX_EXT_VALUE); }
  bool is_nop_value() const { return datum_.is_nop(); }
  bool is_integer_type() const { return ob_is_integer_type(get_type()); }
  bool is_numeric_type() const { return ob_is_numeric_type(get_type()); }
  bool is_string_type() const { return meta_.is_string_type(); }
  bool is_character_type() const { return meta_.is_character_type(); }
  bool is_ext_sql_array() const { return meta_.is_ext_sql_array(); }
  bool is_collection_sql_type() const { return meta_.is_collection_sql_type(); }
  bool is_true() const { return !is_null() && ob_is_int_tc(get_type()) && get_int() != 0; }
  bool is_false() const { return !is_null() && ob_is_int_tc(get_type()) && get_int() == 0; }
#define DATUM_VALUE_META_PREDICATE(name) bool name() const { return meta_.name(); }
  DATUM_VALUE_META_PREDICATE(is_int)
  DATUM_VALUE_META_PREDICATE(is_tinyint)
  DATUM_VALUE_META_PREDICATE(is_uint64)
  DATUM_VALUE_META_PREDICATE(is_float)
  DATUM_VALUE_META_PREDICATE(is_double)
  DATUM_VALUE_META_PREDICATE(is_number)
  DATUM_VALUE_META_PREDICATE(is_unumber)
  DATUM_VALUE_META_PREDICATE(is_varchar)
  DATUM_VALUE_META_PREDICATE(is_char)
  DATUM_VALUE_META_PREDICATE(is_varbinary)
  DATUM_VALUE_META_PREDICATE(is_binary)
  DATUM_VALUE_META_PREDICATE(is_temporal_type)
  DATUM_VALUE_META_PREDICATE(is_datetime)
  DATUM_VALUE_META_PREDICATE(is_mysql_datetime)
  DATUM_VALUE_META_PREDICATE(is_mysql_date)
  DATUM_VALUE_META_PREDICATE(is_date)
  DATUM_VALUE_META_PREDICATE(is_timestamp)
  DATUM_VALUE_META_PREDICATE(is_time)
  DATUM_VALUE_META_PREDICATE(is_year)
  DATUM_VALUE_META_PREDICATE(is_json)
  DATUM_VALUE_META_PREDICATE(is_geometry)
  DATUM_VALUE_META_PREDICATE(is_enum)
  DATUM_VALUE_META_PREDICATE(is_set)
#undef DATUM_VALUE_META_PREDICATE
  int8_t get_tinyint() const { return static_cast<int8_t>(get_int()); }
  bool is_zero_number() const { return get_number().is_zero(); }
  bool is_negative_number() const { return get_number().is_negative(); }
  int get_tinyint(int8_t &out) const {
    if (is_null() || !meta_.is_tinyint()) { return OB_OBJ_TYPE_ERROR; }
    out = static_cast<int8_t>(get_int()); return OB_SUCCESS;
  }
  int get_int32(int32_t &out) const {
    if (is_null() || !(meta_.is_int32())) { return OB_OBJ_TYPE_ERROR; }
    out = get_int32(); return OB_SUCCESS;
  }
  int get_uint64(uint64_t &out) const {
    if (is_null() || !(meta_.is_uint64())) { return OB_OBJ_TYPE_ERROR; }
    out = get_uint64(); return OB_SUCCESS;
  }
  int get_float(float &out) const {
    if (is_null() || !(meta_.is_float())) { return OB_OBJ_TYPE_ERROR; }
    out = get_float(); return OB_SUCCESS;
  }
  int get_double(double &out) const {
    if (is_null() || !(meta_.is_double())) { return OB_OBJ_TYPE_ERROR; }
    out = get_double(); return OB_SUCCESS;
  }
  int get_bool(bool &out) const {
    if (is_null() || !(meta_.is_tinyint())) { return OB_OBJ_TYPE_ERROR; }
    out = get_bool(); return OB_SUCCESS;
  }
  int get_string(ObString &out) const {
    if (is_null()) { out.assign_ptr(nullptr, 0); return OB_SUCCESS; }
    if (!meta_.is_string_or_lob_locator_type() && !is_lob_storage()) { return OB_OBJ_TYPE_ERROR; }
    if (is_lob_storage() && datum_.len_ != 0) {
      ObLobLocatorV2 locator(get_string(), has_lob_header());
      // Retain the existing checked getter's success contract. A request
      // reader, not this constant view, handles external LOB materialization.
      if (locator.is_valid()) { locator.get_inrow_data(out); }
    } else { out = get_string(); }
    return OB_SUCCESS;
  }
  int get_varchar(ObString &out) const { return get_string(out); }
  int apply(const DatumValue &mutation, ObIAllocator &owner) {
    const SqlValueType original = is_null() ? ObNullType : get_type();
    const SqlValueType incoming = mutation.is_null() ? ObNullType : mutation.get_type();
    if (incoming >= ObMaxType || (original != ObExtendType && original != ObNullType
        && incoming != ObExtendType && incoming != ObNullType && original != incoming)) {
      return OB_INVALID_ARGUMENT;
    }
    if (incoming == ObNullType) { set_null(); return OB_SUCCESS; }
    if (incoming == ObExtendType) {
      switch (mutation.get_ext()) {
        case ObActionFlag::OP_DEL_ROW: case ObActionFlag::OP_DEL_TABLE: set_null(); break;
        case ObActionFlag::OP_ROW_DOES_NOT_EXIST: break;
        case ObActionFlag::OP_NOP:
          if (is_ext() && (get_ext() == ObActionFlag::OP_ROW_DOES_NOT_EXIST || get_ext() == ObActionFlag::OP_DEL_ROW)) {
            set_null();
          }
          break;
        default: return OB_INVALID_ARGUMENT;
      }
      return OB_SUCCESS;
    }
    ObDatum owned;
    const int ret = owned.deep_copy(mutation.datum_, owner);
    if (OB_SUCCESS == ret) { datum_ = owned; meta_ = mutation.meta_; }
    return ret;
  }
  int get_int(int64_t &out) const {
    if (is_null() || !is_int()) { return OB_OBJ_TYPE_ERROR; }
    out = get_int(); return OB_SUCCESS;
  }
  int64_t get_int64() const { return datum_.get_int(); }
  int32_t get_int32() const { return static_cast<int32_t>(datum_.get_int()); }
  uint64_t get_uint64() const { return datum_.get_uint(); }
  float get_float() const { return datum_.get_float(); }
  double get_double() const { return datum_.get_double(); }
  int64_t get_unknown() const { return datum_.get_int(); }
  int get_unknown(int64_t &out) const {
    if (is_null() || !is_unknown()) { return OB_OBJ_TYPE_ERROR; }
    out = get_unknown(); return OB_SUCCESS;
  }
  bool get_bool() const { return datum_.get_int() != 0; }
  int64_t get_datetime() const { return datum_.get_datetime(); }
  int64_t get_timestamp() const { return datum_.get_timestamp(); }
  ObOTimestampData get_otimestamp_value() const {
    return datum_.get_otimestamp_tiny();
  }
  int64_t get_time() const { return datum_.get_time(); }
  int32_t get_date() const { return datum_.get_date(); }
  uint8_t get_year() const { return datum_.get_year(); }
  ObMySQLDate get_mysql_date() const { return datum_.get_mysql_date(); }
  ObMySQLDateTime get_mysql_datetime() const { return datum_.get_mysql_datetime(); }
  number::ObNumber get_number() const { return datum_.get_number(); }
  int get_number(number::ObNumber &out) const {
    if (is_null() || !is_number()) { return OB_OBJ_TYPE_ERROR; }
    out = get_number(); return OB_SUCCESS;
  }
  uint64_t get_bit() const { return datum_.get_uint(); }
  uint64_t get_enum() const { return datum_.get_uint(); }
  uint64_t get_set() const { return datum_.get_uint(); }
  const char *get_string_ptr() const { return datum_.ptr_; }
  int32_t get_string_len() const { return datum_.len_; }
  ObString get_varchar() const { return get_string(); }
  int print_sql_literal(char *buffer, int64_t length, int64_t &pos) const;
  int print_sql_literal(char *buffer, int64_t length, int64_t &pos, const ObValuePrintParams &params) const;
  int print_plain_str_literal(char *buffer, int64_t length, int64_t &pos) const;
  int print_plain_str_literal(char *buffer, int64_t length, int64_t &pos, const ObValuePrintParams &params) const;
  int print_varchar_literal(char *buffer, int64_t length, int64_t &pos) const;
  int print_varchar_literal(char *buffer, int64_t length, int64_t &pos, const ObValuePrintParams &params) const;
  int print_plain_str_literal(const ObIArray<ObString> &type_infos, char *buffer, int64_t length, int64_t &pos) const;
  int print_varchar_literal(const ObIArray<ObString> &type_infos, char *buffer, int64_t length, int64_t &pos) const;
  int set_unknown(int64_t value, ObIAllocator &owner) {
    char *bytes = static_cast<char *>(owner.alloc(sizeof(value)));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    ObDatum data; data.ptr_ = bytes; data.set_int(value);
    datum_ = data; meta_.set_unknown(); return OB_SUCCESS;
  }
  int set_uint64(uint64_t value, ObIAllocator &owner) {
    char *bytes = static_cast<char *>(owner.alloc(sizeof(value)));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    ObDatum data; data.ptr_ = bytes; data.set_uint(value);
    datum_ = data; meta_.set_uint64(); return OB_SUCCESS;
  }
  int set_string(SqlValueType type, const ObString &value, ObIAllocator &owner) {
    ObDatum source, owned; source.set_string(value);
    const int ret = owned.deep_copy(source, owner);
    if (OB_SUCCESS == ret) { datum_ = owned; meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_IMPLICIT); }
    return ret;
  }
  int set_varchar(const ObString &value, ObIAllocator &owner) {
    return set_string(ObVarcharType, value, owner);
  }
  // Generate implicit MySQL defaults in the caller's arena. A failure leaves
  // both the current payload and its metadata unchanged.
  int build_not_strict_default_value(int16_t precision,
      ObCollationType string_collation, ObIAllocator &owner);
  int set_enum(uint64_t value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_enum, owner);
  }
  // Setters own bytes in an explicit allocator; copying this value is a view.
  int set_int(int64_t value, ObIAllocator &owner) {
    char *bytes = static_cast<char *>(owner.alloc(sizeof(value)));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    ObDatum data; data.ptr_ = bytes; data.set_int(value);
    datum_ = data; meta_.set_int(); return OB_SUCCESS;
  }
  int set_extend(int64_t value, uint8_t subtype, ObIAllocator &owner, int32_t size = 0) {
    SqlTypeMeta meta = meta_; meta.set_ext(); meta.set_extend_type(subtype);
    char *bytes = static_cast<char *>(owner.alloc(meta.is_ext_sql_array() ? sizeof(int64_t) : DATUM_EXTEND_PAYLOAD_SIZE));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    ObDatum data; data.ptr_ = bytes;
    if (meta.is_ext_sql_array()) { data.set_int(value); }
    else { data.set_extend(value, subtype, size); }
    datum_ = data; meta_ = meta; return OB_SUCCESS;
  }
  bool is_decimal_or_float() const { return meta_.is_decimal_or_float(); }
  int set_decimal_int(int32_t bytes, ObScale scale, const ObDecimalInt *value, ObIAllocator &owner) {
    if (bytes != 0 && bytes != 4 && bytes != 8 && bytes != 16 && bytes != 32 && bytes != 64) { return OB_INVALID_ARGUMENT; }
    if (bytes > 0 && value == nullptr) { return OB_INVALID_ARGUMENT; }
    char *buffer = static_cast<char *>(owner.alloc(bytes > 0 ? bytes : 1));
    if (buffer == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    if (bytes > 0) { MEMCPY(buffer, value, bytes); }
    ObDatum data; data.set_decimal_int_shallow(reinterpret_cast<const ObDecimalInt *>(buffer), bytes);
    datum_ = data; meta_.set_decimal_int(scale); return OB_SUCCESS;
  }
  int set_int(SqlValueType type, int64_t value, ObIAllocator &owner) {
    return assign_numeric(value, type, owner);
  }
  int set_uint(SqlValueType type, uint64_t value, ObIAllocator &owner) {
    return assign_numeric(value, type, owner);
  }
  int set_double(SqlValueType type, double value, ObIAllocator &owner) {
    return assign_numeric(value, type, owner);
  }
  int set_number(SqlValueType type, const number::ObNumber &value, ObIAllocator &owner) {
    const int64_t size = sizeof(number::ObCompactNumber) + value.get_length() * sizeof(uint32_t);
    char *bytes = static_cast<char *>(owner.alloc(size));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    ObDatum data; data.ptr_ = bytes; data.set_number(value);
    datum_ = data; meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_NUMERIC);
    return OB_SUCCESS;
  }
  int set_number(const number::ObNumber &value, ObIAllocator &owner) {
    return set_number(ObNumberType, value, owner);
  }
  int set_bool(bool value, ObIAllocator &owner) {
    return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_tinyint, owner);
  }
  int set_tinyint(int8_t value, ObIAllocator &owner) {
    return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_tinyint, owner);
  }
  int set_double(double value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_double, owner);
  }
  int set_float(float value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_float, owner);
  }
  int set_datetime(int64_t value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_datetime, owner);
  }
  int set_timestamp(int64_t value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_timestamp, owner);
  }
  int set_date(int32_t value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_date, owner);
  }
  int set_time(int64_t value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_time, owner);
  }
  int set_year(uint8_t value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_year, owner);
  }
  int set_mysql_datetime(ObMySQLDateTime value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_mysql_datetime, owner);
  }
  int set_mysql_date(ObMySQLDate value, ObIAllocator &owner) {
    return assign_scalar(value, &SqlTypeMeta::set_mysql_date, owner);
  }
private:
  void set_bound_marker(int64_t marker) {
    struct Payload {
      int64_t value;
      int32_t size;
      uint8_t subtype;
      uint8_t level;
      uint8_t collation;
    } __attribute__((packed));
    static_assert(sizeof(Payload) == DATUM_EXTEND_PAYLOAD_SIZE, "Native bound payload");
    static const Payload minimum{DATUM_MIN_EXT_VALUE, 0, 0, CS_LEVEL_INVALID, CS_TYPE_INVALID};
    static const Payload maximum{DATUM_MAX_EXT_VALUE, 0, 0, CS_LEVEL_INVALID, CS_TYPE_INVALID};
    datum_.ptr_ = reinterpret_cast<const char *>(marker == DATUM_MIN_EXT_VALUE ? &minimum : &maximum);
    datum_.set_ext();
    meta_.set_ext();
  }
  template <typename T>
  int assign_scalar(T value, void (SqlTypeMeta::*setter)(), ObIAllocator &owner) {
    char *bytes = static_cast<char *>(owner.alloc(sizeof(T)));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    MEMCPY(bytes, &value, sizeof(T));
    ObDatum data; data.set_string(bytes, sizeof(T));
    datum_ = data; (meta_.*setter)(); return OB_SUCCESS;
  }
  template <typename T>
  int assign_numeric(T value, SqlValueType type, ObIAllocator &owner) {
    char *bytes = static_cast<char *>(owner.alloc(sizeof(T)));
    if (bytes == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    MEMCPY(bytes, &value, sizeof(T));
    ObDatum data; data.set_string(bytes, sizeof(T));
    datum_ = data; meta_.set_type(type); meta_.set_collation_level(CS_LEVEL_NUMERIC);
    return OB_SUCCESS;
  }
public:
  TO_STRING_KV(K_(datum), K_(meta));
  ObDatum datum_;
  SqlTypeMeta meta_;
};
static_assert(sizeof(DatumValue) == 16, "Typed native constants stay compact");

} // namespace common
} // namespace oceanbase
