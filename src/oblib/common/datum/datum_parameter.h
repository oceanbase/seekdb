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
#include "common/datum/datum_copy_plan.h"
#include "common/datum/datum_parameter_metadata.h"
#include "common/datum/datum_value.h"
#include "common/datum/datum_sql_print.h"
#include "common/datum/parameter_flags.h"

namespace oceanbase
{
namespace common
{

// A parameter owns its inline value and carries independent metadata. Shallow
// copies borrow Reference spans; deep_copy() copies that span into the explicit
// destination owner without retaining an extended resource pointee.
class DatumParameter
{
public:
  DatumParameter()
    : datum_(), metadata_()
  { datum_.set_null(); }

  DatumValue value_view() const { return DatumValue(datum_, metadata_.type_meta_); }
  int print_sql_literal(char *buffer, int64_t length, int64_t &pos,
      const ObValuePrintParams &params = ObValuePrintParams()) const
  { return print_datum_sql_literal(value_view(), buffer, length, pos, params); }

  void reset() { *this = DatumParameter(); }
  // Metadata-only assignment does not change the payload or its owner.
  const DatumParameterMetadata &get_metadata() const { return metadata_; }
  void set_metadata(const DatumParameterMetadata &metadata) { metadata_ = metadata; }
  // Trusted physical producer only. Old-storage input uses checked import.
  void set_datum(const Datum &datum) { datum_ = datum; }
  int assign_native_borrowed(const Datum &, const SqlTypeMeta &);
  int assign_native_owned(const Datum &, const SqlTypeMeta &, ObIAllocator &);
  int assign_legacy_borrowed(const ObDatum &, const SqlTypeMeta &);
  int assign_legacy_owned(const ObDatum &, const SqlTypeMeta &, ObIAllocator &);
  int export_legacy(ObDatum &, void *scratch, int64_t capacity) const;
  int copy_legacy(ObDatum &, ObIAllocator &) const;
  const SqlTypeMeta &get_type_meta() const { return metadata_.type_meta_; }
  // Metadata-only writes require a matching physical representation; casts
  // and Raw/Expr-to-generic crossings use explicit checked imports.
  void set_type_meta(const SqlTypeMeta &meta) { metadata_.type_meta_ = meta; }
  SqlValueType get_type() const { return metadata_.type_meta_.get_type(); }
  ObCollationType get_collation_type() const { return metadata_.type_meta_.get_collation_type(); }
  ObCollationLevel get_collation_level() const { return metadata_.type_meta_.get_collation_level(); }
  bool is_ext_sql_array() const { return metadata_.type_meta_.is_ext_sql_array(); }
  bool is_lob() const { return metadata_.type_meta_.is_lob(); }
  bool is_string_type() const { return metadata_.type_meta_.is_string_type(); }
  bool is_null() const { return datum_.is_null(); }
  // Preserve declared metadata, accuracy and flags, just as a runtime NULL
  // assignment does. The raw value type/collation become NULL.
  void set_null() { datum_.set_null(); metadata_.type_meta_.set_null(); }
  bool has_lob_header() const { return metadata_.type_meta_.has_lob_header(); }

  void set_accuracy(const ObAccuracy &accuracy) { metadata_.accuracy_ = accuracy; }
  const ObAccuracy &get_accuracy() const { return metadata_.accuracy_; }
  void set_length(ObLength length) { metadata_.accuracy_.set_length(length); }
  void set_precision(ObPrecision precision) { metadata_.accuracy_.set_precision(precision); }
  void set_length_semantics(ObLengthSemantics semantics)
  { metadata_.accuracy_.set_length_semantics(semantics); }
  void set_scale(ObScale scale) { metadata_.accuracy_.set_scale(scale); metadata_.type_meta_.set_scale(scale); }
  void set_udt_id(uint64_t id) { metadata_.accuracy_.set_accuracy(id); }
  ObLength get_length() const { return metadata_.accuracy_.get_length(); }
  ObPrecision get_precision() const { return metadata_.accuracy_.get_precision(); }
  ObScale get_scale() const { return metadata_.accuracy_.get_scale(); }
  uint64_t get_udt_id() const
  { return metadata_.type_meta_.is_ext() ? metadata_.accuracy_.get_accuracy() : OB_INVALID_INDEX; }

  void set_result_flags(uint32_t flags) { metadata_.res_flags_ = flags; }
  void set_result_flag(uint32_t flags) { metadata_.res_flags_ |= flags; }
  void unset_result_flag(uint32_t flags) { metadata_.res_flags_ &= ~flags; }
  bool has_result_flag(uint32_t flags) const { return (metadata_.res_flags_ & flags) != 0; }
  uint32_t get_result_flag() const { return metadata_.res_flags_; }
  const ParamFlag &get_param_flag() const { return metadata_.flag_; }
  ParamFlag &get_param_flag() { return metadata_.flag_; }
  void set_param_flag(const ParamFlag &flag) { metadata_.flag_ = flag; }
  void set_need_to_check_type(bool value) { metadata_.flag_.need_to_check_type_ = value; }
  bool need_to_check_type() const { return metadata_.flag_.need_to_check_type_; }
  void set_need_to_check_bool_value(bool value) { metadata_.flag_.need_to_check_bool_value_ = value; }
  bool need_to_check_bool_value() const { return metadata_.flag_.need_to_check_bool_value_; }
  void set_expected_bool_value(bool value) { metadata_.flag_.expected_bool_value_ = value; }
  bool expected_bool_value() const { return metadata_.flag_.expected_bool_value_; }
  bool is_nop_value() const { return datum_.is_nop()
      || (datum_.has_ext_value() && datum_.get_ext() == ObActionFlag::OP_NOP); }

  void set_raw_text_info(int32_t pos, int32_t len)
  { metadata_.raw_text_pos_ = pos; metadata_.raw_text_len_ = len; }
  int32_t get_raw_text_pos() const { return metadata_.raw_text_pos_; }
  int32_t get_raw_text_len() const { return metadata_.raw_text_len_; }
  // Initialize declared type and numeric defaults after a parameter crosses a
  // wire/protocol boundary. A typed NULL keeps its existing declared metadata.
  void set_param_meta()
  {
    if (metadata_.param_meta_.is_null() || get_type() != ObNullType) { metadata_.param_meta_ = metadata_.type_meta_; }
    if (ob_is_numeric_type(get_type())) {
      const ObAccuracy &defaults = ObAccuracy::DDL_DEFAULT_ACCURACY[get_type()];
      if (get_scale() < 0) {
        if (metadata_.type_meta_.get_scale() >= 0) { set_scale(metadata_.type_meta_.get_scale()); }
        else if (ob_is_integer_type(get_type())) { set_scale(defaults.get_scale()); }
      }
      if (get_precision() == PRECISION_UNKNOWN_YET) { set_precision(defaults.get_precision()); }
    }
  }
  void set_param_meta(const SqlTypeMeta &meta)
  {
    if (metadata_.param_meta_.is_null() || !meta.is_null()) { metadata_.param_meta_ = meta; }
  }
  const SqlTypeMeta &get_param_meta() const { return metadata_.param_meta_; }

  // SQL arrays use an explicit eight-byte Reference address wrapper. The object
  // behind the address belongs to its producer; copying does not clone it.
  template <typename T> int set_array_address(const T *array, ObIAllocator &owner)
  {
    if (!is_ext_sql_array()) { return OB_INVALID_ARGUMENT; }
    const int64_t address = reinterpret_cast<int64_t>(array);
    ObDatum source; source.set_string(reinterpret_cast<const char *>(&address), sizeof(address));
    return assign_legacy_owned(source, metadata_.type_meta_, owner);
  }
  template <typename T> const T *get_array() const
  {
    return is_ext_sql_array() && !is_null()
        ? reinterpret_cast<const T *>(datum_.get_unknown()) : nullptr;
  }
  template <typename T> T *get_array()
  {
    return is_ext_sql_array() && !is_null()
        ? reinterpret_cast<T *>(datum_.get_unknown()) : nullptr;
  }


  // Clones payload bytes, not the pointee of an extended PL/SQL array address.
  // The destination is unchanged if allocation fails, including self-copy.
  int deep_copy(const DatumParameter &source, ObIAllocator &allocator);

  static uint32_t accuracy_offset_bits()
  { return (offsetof(DatumParameter, metadata_) + offsetof(DatumParameterMetadata, accuracy_)) * 8; }
  static uint32_t res_flags_offset_bits()
  { return (offsetof(DatumParameter, metadata_) + offsetof(DatumParameterMetadata, res_flags_)) * 8; }
  static uint32_t flag_offset_bits()
  { return (offsetof(DatumParameter, metadata_) + offsetof(DatumParameterMetadata, flag_)) * 8; }
  TO_STRING_KV(K_(datum), "type_meta", metadata_.type_meta_, "accuracy", metadata_.accuracy_,
                "res_flags", metadata_.res_flags_, "flag", metadata_.flag_,
                "raw_text_pos", metadata_.raw_text_pos_, "raw_text_len", metadata_.raw_text_len_,
                "param_meta", metadata_.param_meta_);

  // Ordinary scalar writes are inline. Reference writes require an explicit
  // owner and never publish a setter stack address. Existing accuracy/flags
  // remain independent of value; allocation failure leaves the value unchanged.
  int set_int(int64_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_int, allocator); }
  int set_tinyint(int8_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_tinyint, allocator); }
  int set_smallint(int16_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_smallint, allocator); }
  int set_mediumint(int32_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_mediumint, allocator); }
  int set_int32(int32_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_int32, allocator); }
  int set_uint64(uint64_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<uint64_t>(value), &SqlTypeMeta::set_uint64, allocator); }
  int set_utinyint(uint8_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<uint64_t>(value), &SqlTypeMeta::set_utinyint, allocator); }
  int set_usmallint(uint16_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<uint64_t>(value), &SqlTypeMeta::set_usmallint, allocator); }
  int set_umediumint(uint32_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<uint64_t>(value), &SqlTypeMeta::set_umediumint, allocator); }
  int set_uint32(uint32_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<uint64_t>(value), &SqlTypeMeta::set_uint32, allocator); }
  int set_float(float value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<float>(value), &SqlTypeMeta::set_float, allocator); }
  int set_double(double value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<double>(value), &SqlTypeMeta::set_double, allocator); }
  int set_date(int32_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int32_t>(value), &SqlTypeMeta::set_date, allocator); }
  int set_datetime(int64_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_datetime, allocator); }
  int set_time(int64_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_time, allocator); }
  int set_timestamp(int64_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<int64_t>(value), &SqlTypeMeta::set_timestamp, allocator); }
  int set_year(uint8_t value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<uint8_t>(value), &SqlTypeMeta::set_year, allocator); }
  int set_mysql_date(ObMySQLDate value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<ObMySQLDate>(value), &SqlTypeMeta::set_mysql_date, allocator); }
  int set_mysql_datetime(ObMySQLDateTime value, ObIAllocator &allocator)
  { return assign_scalar(static_cast<ObMySQLDateTime>(value), &SqlTypeMeta::set_mysql_datetime, allocator); }
  int set_unknown(int64_t value, ObIAllocator &allocator)
  {
    SqlTypeMeta meta = metadata_.type_meta_; meta.set_unknown();
    ObDatum source; source.set_string(reinterpret_cast<const char *>(&value), sizeof(value));
    return assign_legacy_owned(source, meta, allocator);
  }
  void set_string_view(SqlValueType type, const ObString &value)
  {
    Datum candidate;
    const int ret = candidate.set_string(value);
    OB_ASSERT(ret == OB_SUCCESS);
    datum_ = candidate;
    metadata_.type_meta_.set_type(type);
    metadata_.type_meta_.set_collation_level(CS_LEVEL_IMPLICIT);
  }
  void set_varchar_view(const ObString &value) { set_string_view(ObVarcharType, value); }
  int set_string(SqlValueType type, const ObString &value, ObIAllocator &allocator)
  {
    DatumValue copy(datum_, metadata_.type_meta_);
    const int ret = copy.set_string(type, value, allocator);
    if (ret == OB_SUCCESS) { assign_value(copy); }
    return ret;
  }
  int set_varchar(const ObString &value, ObIAllocator &allocator)
  { return set_string(ObVarcharType, value, allocator); }
  int set_number(const number::ObNumber &value, ObIAllocator &allocator)
  {
    const int64_t bytes = sizeof(number::ObCompactNumber) + value.get_length() * sizeof(uint32_t);
    char *buffer = static_cast<char *>(allocator.alloc(bytes));
    if (buffer == nullptr) { return OB_ALLOCATE_MEMORY_FAILED; }
    Datum candidate;
    const int ret = candidate.set_number(value, buffer, bytes);
    if (ret != OB_SUCCESS) { allocator.free(buffer); }
    else { datum_ = candidate; metadata_.type_meta_.set_number(); }
    return ret;
  }
  bool is_lob_storage() const { return metadata_.type_meta_.is_lob_storage(); }
  void set_has_lob_header() { metadata_.type_meta_.set_has_lob_header(); }

  int set_extend(int64_t value, uint8_t subtype, ObIAllocator &allocator, int32_t size = 0)
  {
    DatumValue copy(datum_, metadata_.type_meta_);
    const int ret = copy.set_extend(value, subtype, allocator, size);
    if (ret == OB_SUCCESS) { assign_value(copy); }
    return ret;
  }
  int set_ext(int64_t value, ObIAllocator &allocator)
  { return set_extend(value, 0, allocator); }

  // Typed reads use the same canonical Datum payload; metadata stays separate.
  const SqlTypeMeta &get_meta() const { return metadata_.type_meta_; }
  SqlTypeMeta &get_meta() { return metadata_.type_meta_; }
  SqlTypeClass get_type_class() const { return sql_type_class(get_type()); }
  void set_collation_type(ObCollationType cs) { metadata_.type_meta_.set_collation_type(cs); }
  void set_collation_level(ObCollationLevel level) { metadata_.type_meta_.set_collation_level(level); }
  void set_subschema_id(uint16_t id) { metadata_.type_meta_.set_subschema_id(id); }
  uint16_t get_subschema_id() const { return metadata_.type_meta_.get_subschema_id(); }
  void set_type(SqlValueType type)
  {
    if (type == ObNullType) { set_null(); }
    else { metadata_.type_meta_.set_type(type); }
  }
  void set_collation(const SqlTypeMeta &meta) { metadata_.type_meta_.set_collation(meta); }
  bool is_null_or_empty_string() const
  { return is_null() || (metadata_.type_meta_.is_character_type() && datum_.length() == 0); }
  int get_char_length(const ObAccuracy &, int32_t &length) const
  {
    int ret = OB_SUCCESS;
    if (is_null() || !metadata_.type_meta_.is_fixed_len_char_type()) { ret = OB_ERR_UNEXPECTED; }
    else { length = ObCharset::strlen_char(get_collation_type(), static_cast<const char *>(datum_.get_reference()), datum_.length()); }
    return ret;
  }
  // Assign a value without overwriting the parameter's declaration/flags.
  // The producer continues to own the bytes until the receiver clones them.
  void assign_value(const DatumParameter &source)
  { datum_ = source.datum_; metadata_.type_meta_ = source.metadata_.type_meta_; }
  void assign_value(const DatumValue &source)
  { datum_ = source.datum_; metadata_.type_meta_ = source.meta_; }
  int apply(const DatumParameter &mutation)
  {
    int ret = OB_SUCCESS;
    const SqlValueType original_type = get_type(), mutation_type = mutation.get_type();
    if (mutation_type >= ObMaxType
        || (original_type != ObExtendType && original_type != ObNullType
            && mutation_type != ObExtendType && mutation_type != ObNullType
            && original_type != mutation_type)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (mutation_type == ObNullType) {
      set_null();
    } else if (mutation_type == ObExtendType) {
      switch (mutation.get_ext()) {
        case ObActionFlag::OP_DEL_ROW:
        case ObActionFlag::OP_DEL_TABLE: set_null(); break;
        case ObActionFlag::OP_ROW_DOES_NOT_EXIST: break;
        case ObActionFlag::OP_NOP:
          if (is_ext() && (get_ext() == ObActionFlag::OP_ROW_DOES_NOT_EXIST
                          || get_ext() == ObActionFlag::OP_DEL_ROW)) { set_null(); }
          break;
        default: ret = OB_INVALID_ARGUMENT; break;
      }
    } else {
      assign_value(mutation);
    }
    return ret;
  }
  bool is_character_type() const { return metadata_.type_meta_.is_character_type(); }
  bool is_collection_sql_type() const { return metadata_.type_meta_.is_collection_sql_type(); }
  bool is_invalid_type() const { return !metadata_.type_meta_.is_valid(); }
  bool is_pl_extend() const
  {
    if (is_null() || !is_ext()) { return false; }
    const int64_t value = get_ext();
    return !(ObActionFlag::OP_MIN_OBJ <= value && ObActionFlag::OP_LOCK_ROW >= value)
        && value != DATUM_MIN_EXT_VALUE && value != DATUM_MAX_EXT_VALUE
        && get_extend_type() > 0 && get_extend_type() < T_EXT_SQL_ARRAY;
  }
  bool is_true() const { return !is_null() && ob_is_int_tc(get_type()) && get_int() != 0; }
  bool is_false() const { return !is_null() && ob_is_int_tc(get_type()) && get_int() == 0; }
  bool get_bool() const { return datum_.get_int() != 0; }
  int get_bool(bool &out) const
  {
    if (is_null() || !metadata_.type_meta_.is_tinyint()) { return OB_OBJ_TYPE_ERROR; }
    out = get_bool(); return OB_SUCCESS;
  }
  bool is_negative_number() const
  { return number::ObNumber::is_negative_number(datum_.get_number_desc()); }
  int32_t get_val_len() const
  {
    if (is_null()) { return 0; }
    if (is_ext_sql_array()) { return datum_array_source_value_size(datum_); }
    if (is_ext()) { return datum_.has_ext_value() ? datum_.get_extend_size() : 0; }
    if (ob_is_number_tc(get_type())) { return static_cast<int32_t>(datum_.get_number_desc().desc_); }
    return is_string_type() || is_json() || is_geometry() || is_decimal_int()
        || is_collection_sql_type() ? datum_.length() : 0;
  }
  int get_real_param_count(int64_t &count) const;
  bool is_outrow_lob() const;

  void set_meta_type(const SqlTypeMeta &meta) { set_type_meta(meta); }
  bool is_boolean() const { return metadata_.flag_.is_boolean_; }
  void set_is_boolean(bool b) { metadata_.flag_.is_boolean_ = b; }
  bool is_batch_parameters() const { return metadata_.flag_.is_batch_parameter_; }
  void set_need_to_check_extend_type(bool b) { metadata_.flag_.need_to_check_extend_type_ = b; }
  bool need_to_check_extend_type() const { return metadata_.flag_.need_to_check_extend_type_; }
  void set_is_pl_mock_default_param(bool b) { metadata_.flag_.is_pl_mock_default_param_ = b; }
  bool is_pl_mock_default_param() const { return metadata_.flag_.is_pl_mock_default_param_; }
  void set_ignore_scale_check(bool b) { metadata_.flag_.ignore_scale_check_ = b; }
  bool ignore_scale_check() const { return metadata_.flag_.ignore_scale_check_; }
  bool is_unknown() const { return get_type() == ObUnknownType; }
  bool is_ext() const { return get_type() == ObExtendType; }
  bool is_integer_type() const { return ob_is_integer_type(get_type()); }
  bool is_numeric_type() const { return ob_is_numeric_type(get_type()); }
  bool is_min_value() const { return datum_.is_min()
      || (datum_.has_ext_value() && datum_.get_ext() == DATUM_MIN_EXT_VALUE); }
  bool is_max_value() const { return datum_.is_max()
      || (datum_.has_ext_value() && datum_.get_ext() == DATUM_MAX_EXT_VALUE); }
#define DATUM_PARAMETER_META_PREDICATE(name) bool name() const { return metadata_.type_meta_.name(); }
  DATUM_PARAMETER_META_PREDICATE(is_int)
  DATUM_PARAMETER_META_PREDICATE(is_uint64)
  DATUM_PARAMETER_META_PREDICATE(is_float)
  DATUM_PARAMETER_META_PREDICATE(is_double)
  DATUM_PARAMETER_META_PREDICATE(is_number)
  DATUM_PARAMETER_META_PREDICATE(is_unumber)
  DATUM_PARAMETER_META_PREDICATE(is_decimal_int)
  DATUM_PARAMETER_META_PREDICATE(is_varchar)
  DATUM_PARAMETER_META_PREDICATE(is_char)
  DATUM_PARAMETER_META_PREDICATE(is_varbinary)
  DATUM_PARAMETER_META_PREDICATE(is_binary)
  DATUM_PARAMETER_META_PREDICATE(is_varchar_or_char)
  DATUM_PARAMETER_META_PREDICATE(is_datetime)
  DATUM_PARAMETER_META_PREDICATE(is_mysql_datetime)
  DATUM_PARAMETER_META_PREDICATE(is_mysql_date)
  DATUM_PARAMETER_META_PREDICATE(is_date)
  DATUM_PARAMETER_META_PREDICATE(is_timestamp)
  DATUM_PARAMETER_META_PREDICATE(is_time)
  DATUM_PARAMETER_META_PREDICATE(is_year)
  DATUM_PARAMETER_META_PREDICATE(is_json)
  DATUM_PARAMETER_META_PREDICATE(is_geometry)
  DATUM_PARAMETER_META_PREDICATE(is_enum)
  DATUM_PARAMETER_META_PREDICATE(is_set)
#undef DATUM_PARAMETER_META_PREDICATE
  int64_t get_int() const { return datum_.get_int(); }
  int64_t get_int64() const { return datum_.get_int(); }
  int32_t get_int32() const { return static_cast<int32_t>(datum_.get_int()); }
  int get_int32(int32_t &out) const
  {
    if (is_null() || !metadata_.type_meta_.is_int32()) { return OB_OBJ_TYPE_ERROR; }
    out = get_int32(); return OB_SUCCESS;
  }
  int get_int(int64_t &out) const
  {
    if (is_null() || !is_int()) { return OB_OBJ_TYPE_ERROR; }
    out = get_int(); return OB_SUCCESS;
  }
  uint64_t get_uint64() const { return datum_.get_uint(); }
  float get_float() const { return datum_.get_float(); }
  double get_double() const { return datum_.get_double(); }
  int64_t get_unknown() const { return datum_.get_unknown(); }
  int64_t get_datetime() const { return datum_.get_datetime(); }
  ObMySQLDateTime get_mysql_datetime() const { return datum_.get_mysql_datetime(); }
  ObMySQLDate get_mysql_date() const { return datum_.get_mysql_date(); }
  int64_t get_timestamp() const { return datum_.get_timestamp(); }
  int32_t get_date() const { return datum_.get_date(); }
  int64_t get_time() const { return datum_.get_time(); }
  uint8_t get_year() const { return datum_.get_year(); }
  uint64_t get_bit() const { return datum_.get_uint(); }
  uint64_t get_enum() const { return datum_.get_uint(); }
  uint64_t get_set() const { return datum_.get_uint(); }
  int64_t get_ext() const
  {
    if (is_null()) { return 0; }
    if (!datum_.is_present() || datum_.has_ext_value()) { return datum_.get_ext(); }
    if (is_ext_sql_array() || is_unknown()) { return datum_.get_unknown(); }
    return datum_.get_int();
  }
  uint8_t get_extend_type() const { return metadata_.type_meta_.get_extend_type(); }
  const char *get_string_ptr() const { return static_cast<const char *>(datum_.get_reference()); }
  int32_t get_string_len() const { return datum_.length(); }
  ObString get_string() const { return datum_.get_string(); }
  ObString get_varchar() const { return get_string(); }
  int get_string(ObString &out) const;
  int get_varchar(ObString &out) const { return get_string(out); }
  number::ObNumber get_number() const { return datum_.get_number(); }
  int get_number(number::ObNumber &out) const
  {
    if (is_null() || !is_number()) { return OB_OBJ_TYPE_ERROR; }
    out = get_number(); return OB_SUCCESS;
  }
  int32_t get_int_bytes() const { return static_cast<int32_t>(value_view().get_data_length()); }
  const ObDecimalInt *get_decimal_int() const { return static_cast<const ObDecimalInt *>(datum_.get_reference()); }
  const Datum &get_datum() const { return datum_; }
  Datum &get_datum() { return datum_; }
  int serialize(char *buf, int64_t length, int64_t &pos) const;
  int deserialize(const char *buf, int64_t length, int64_t &pos, ObIAllocator &allocator);
  int64_t get_serialize_size() const;

  Datum datum_;
private:
  template <typename T>
  int assign_scalar(T value, void (SqlTypeMeta::*setter)(), ObIAllocator &)
  {
    Datum candidate;
    if constexpr (std::is_same_v<T, float>) { candidate.set_float(value); }
    else if constexpr (std::is_same_v<T, double>) { candidate.set_double(value); }
    else if constexpr (std::is_same_v<T, ObMySQLDate>) { candidate.set_mysql_date(value); }
    else if constexpr (std::is_same_v<T, ObMySQLDateTime>) { candidate.set_mysql_datetime(value); }
    else if constexpr (std::is_signed_v<T>) { candidate.set_int(value); }
    else { candidate.set_uint(value); }
    datum_ = candidate; (metadata_.type_meta_.*setter)(); return OB_SUCCESS;
  }
  DatumParameterMetadata metadata_;
};
static_assert(sizeof(DatumParameter) == 48 && alignof(DatumParameter) == 8,
              "Native parameter plus P0 metadata uses the aligned 48-byte ABI");

} // namespace common
} // namespace oceanbase
