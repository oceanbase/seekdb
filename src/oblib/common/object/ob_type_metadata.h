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

#include "lib/ob_define.h"
#include "common/object/ob_obj_type.h"
#include "common/ob_accuracy.h"
#include "lib/charset/ob_charset.h"

namespace oceanbase
{
namespace common
{

enum ObCmpNullPos
{
  NULL_LAST = 1,
  NULL_FIRST,
  MAX_NULL_POS
};

inline ObCmpNullPos default_null_pos()
{
  return NULL_FIRST;
}

struct ObLobScale
{
  static const uint8_t LOB_SCALE_MASK = 0xF;
  enum StorageType
  {
    STORE_IN_ROW = 0,
    STORE_OUT_ROW,
    STORE_HAS_LOB_HEADER,
  };
  union
  {
    int8_t scale_;
    struct
    {
      uint8_t reserve_: 4;
      uint8_t type_: 4;
    };
  };
  ObLobScale() : scale_(0) {}
  ObLobScale(const ObScale scale) : scale_(static_cast<const int8_t>(scale)) { reserve_ = 0; }
  OB_INLINE void reset() { scale_ = -1; }
  OB_INLINE bool is_valid() { return STORE_IN_ROW == type_ || STORE_OUT_ROW == type_ || STORE_HAS_LOB_HEADER == type_; }
  OB_INLINE void set_in_row() { reserve_ = 0; type_ = STORE_IN_ROW; }
  OB_INLINE void set_out_row() { reserve_ = 0; type_ = STORE_OUT_ROW; }
  OB_INLINE void set_has_lob_header() { reserve_ = 0; type_ = STORE_HAS_LOB_HEADER; }
  OB_INLINE bool is_in_row() const { return type_ == STORE_IN_ROW; }
  OB_INLINE bool is_out_row() const { return type_ == STORE_OUT_ROW; }
  OB_INLINE bool has_lob_header() const { return type_ == STORE_HAS_LOB_HEADER; }
  OB_INLINE ObScale get_scale() const { return static_cast<ObScale>(scale_); }
  TO_STRING_KV(K_(scale));
};

class SqlTypeMeta
{
public:
  SqlTypeMeta()
      :type_(ObNullType),
       cs_level_(CS_LEVEL_INVALID),
       cs_type_(CS_TYPE_INVALID),
       scale_(-1)
  {}

  OB_INLINE bool operator ==(const SqlTypeMeta &other) const
  {
    return (type_ == other.type_ && cs_level_ == other.cs_level_ && cs_type_ == other.cs_type_);
  }
  OB_INLINE bool operator !=(const SqlTypeMeta &other) const { return !this->operator ==(other); }
  // this method is inefficient, you'd better use set_tinyint() etc. instead
  OB_INLINE void set_type(const SqlValueType &type)
  {
    type_ = static_cast<uint8_t>(type);
    if (ObNullType == type_) {
      set_collation_level(CS_LEVEL_IGNORABLE);
      set_collation_type(CS_TYPE_BINARY);
    } else if (ObUnknownType == type_
               || ObExtendType == type_) {
      set_collation_level(CS_LEVEL_INVALID);
      set_collation_type(CS_TYPE_INVALID);
    } else if (ObHexStringType == type_) {
      set_collation_type(CS_TYPE_BINARY);
    } else if (ObJsonType == type_) {
      set_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (ObUserDefinedSQLType == type_ ||
               ObCollectionSQLType == type_) {
      set_subschema_id(UINT_MAX16);
    } else if (!ob_is_string_type(static_cast<SqlValueType>(type_))
               && !ob_is_enum_or_set_type(static_cast<SqlValueType>(type_))
               && !ob_is_geometry(static_cast<SqlValueType>(type_))) {
      set_collation_level(CS_LEVEL_NUMERIC);
      set_collation_type(CS_TYPE_BINARY);
    }
  }
  OB_INLINE void set_type_simple(const SqlValueType &type)
  {
    type_ = static_cast<uint8_t>(type);
  }
  // in greatest case need manually set numeric collation,
  // e.g greatest(2, 'x') => type=varchar,collation=binary,cmp_collation=utf8
  OB_INLINE void set_numeric_collation() {
    set_collation_level(CS_LEVEL_NUMERIC);
    set_collation_type(CS_TYPE_BINARY);
  }
  OB_INLINE void set_meta(const SqlTypeMeta &meta)
  {
    type_ = meta.type_;
    cs_level_ = meta.cs_level_;
    cs_type_ = meta.cs_type_;
    scale_ = meta.scale_;
  }
  OB_INLINE void reset()
  {
    type_ = ObNullType;
    cs_level_ = CS_LEVEL_INVALID;
    cs_type_ = CS_TYPE_INVALID;
    scale_ = -1;
  }

  OB_INLINE SqlValueType get_type() const { return static_cast<SqlValueType>(type_); }
  OB_INLINE ExtendedSqlValueType get_extended_type() const { return sql_value_type_to_extended_type(static_cast<SqlValueType>(type_)); }
  OB_INLINE const SqlTypeMeta &get_sql_type_meta() const { return *this; }
  OB_INLINE SqlTypeClass get_type_class() const { return sql_type_class(get_type()); }
  OB_INLINE SqlTypeClass get_type_class_for_extended() const { return extended_sql_type_class(get_type()); }

  OB_INLINE void set_null() { type_ = static_cast<uint8_t>(ObNullType); set_collation_level(CS_LEVEL_IGNORABLE); set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_tinyint() { type_ = static_cast<uint8_t>(ObTinyIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_smallint() { type_ = static_cast<uint8_t>(ObSmallIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_mediumint() { type_ = static_cast<uint8_t>(ObMediumIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_int32() { type_ = static_cast<uint8_t>(ObInt32Type); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_int() { type_ = static_cast<uint8_t>(ObIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_utinyint() { type_ = static_cast<uint8_t>(ObUTinyIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_usmallint() { type_ = static_cast<uint8_t>(ObUSmallIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_umediumint() { type_ = static_cast<uint8_t>(ObUMediumIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_uint32() { type_ = static_cast<uint8_t>(ObUInt32Type); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_uint64() { type_ = static_cast<uint8_t>(ObUInt64Type); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_float() { type_ = static_cast<uint8_t>(ObFloatType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_double() { type_ = static_cast<uint8_t>(ObDoubleType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_ufloat() { type_ = static_cast<uint8_t>(ObUFloatType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_udouble() { type_ = static_cast<uint8_t>(ObUDoubleType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_number() { type_ = static_cast<uint8_t>(ObNumberType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_unumber() { type_ = static_cast<uint8_t>(ObUNumberType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_datetime() { type_ = static_cast<uint8_t>(ObDateTimeType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_mysql_datetime() { type_ = static_cast<uint8_t>(ObMySQLDateTimeType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_timestamp() { type_ = static_cast<uint8_t>(ObTimestampType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_year() { type_ = static_cast<uint8_t>(ObYearType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_date() { type_ = static_cast<uint8_t>(ObDateType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_mysql_date() { type_ = static_cast<uint8_t>(ObMySQLDateType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_time() { type_ = static_cast<uint8_t>(ObTimeType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_varchar() { type_ = static_cast<uint8_t>(ObVarcharType); }
  OB_INLINE void set_char() { type_ = static_cast<uint8_t>(ObCharType); }
  OB_INLINE void set_varbinary() { type_ = static_cast<uint8_t>(ObVarcharType); set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_binary() { type_ = static_cast<uint8_t>(ObCharType); set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_hex_string() { type_ = static_cast<uint8_t>(ObHexStringType); set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_ext() { type_ = static_cast<uint8_t>(ObExtendType); set_collation_level(CS_LEVEL_INVALID); set_collation_type(CS_TYPE_INVALID); set_extend_type(0); }
  OB_INLINE void set_unknown() { type_ = static_cast<uint8_t>(ObUnknownType); set_collation_level(CS_LEVEL_INVALID); set_collation_type(CS_TYPE_INVALID); }
  OB_INLINE void set_bit() { type_ = static_cast<uint8_t>(ObBitType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  //TODO():collation_level, collation_type
  OB_INLINE void set_enum() { type_ = static_cast<uint8_t>(ObEnumType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_set() { type_ = static_cast<uint8_t>(ObSetType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_enum_inner() { type_ = static_cast<uint8_t>(ObEnumInnerType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_set_inner() { type_ = static_cast<uint8_t>(ObSetInnerType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }

  OB_INLINE void set_clob() { type_ = static_cast<uint8_t>(ObLongTextType); lob_scale_.set_in_row(); set_collation_level(CS_LEVEL_IMPLICIT); set_default_collation_type(); }
  OB_INLINE void set_blob() { type_ = static_cast<uint8_t>(ObLongTextType); lob_scale_.set_in_row(); set_collation_level(CS_LEVEL_IMPLICIT); set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_inrow() { lob_scale_.set_in_row(); }
  OB_INLINE void set_outrow() { lob_scale_.set_out_row(); }
  OB_INLINE void set_has_lob_header() { lob_scale_.set_has_lob_header(); }
  OB_INLINE void set_json()
  {
    type_ = static_cast<uint8_t>(ObJsonType);
    lob_scale_.set_in_row();
    set_collation_level(CS_LEVEL_IMPLICIT);
    set_collation_type(CS_TYPE_UTF8MB4_BIN);
  }
  OB_INLINE void set_geometry()
  {
    type_ = static_cast<uint8_t>(ObGeometryType);
    lob_scale_.set_in_row();
    set_collation_level(CS_LEVEL_IMPLICIT);
    set_collation_type(CS_TYPE_BINARY);
  }

  OB_INLINE void set_otimestamp_type(const SqlValueType type) { type_ = static_cast<uint8_t>(type); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_decimal_int() { type_ = static_cast<uint8_t>(ObDecimalIntType); set_collation_level(CS_LEVEL_NUMERIC);set_collation_type(CS_TYPE_BINARY); }
  OB_INLINE void set_decimal_int(ObScale scale)
  {
    set_decimal_int();
    scale_ = scale;
  }

  OB_INLINE bool is_valid() const { return is_valid_sql_value_type(static_cast<SqlValueType>(type_)); }
  OB_INLINE bool is_invalid() const { return !is_valid_sql_value_type(static_cast<SqlValueType>(type_)); }

  OB_INLINE bool is_null() const { return type_ == static_cast<uint8_t>(ObNullType); }
  OB_INLINE bool is_tinyint() const { return type_ == static_cast<uint8_t>(ObTinyIntType); }
  OB_INLINE bool is_smallint() const { return type_ == static_cast<uint8_t>(ObSmallIntType); }
  OB_INLINE bool is_mediumint() const { return type_ == static_cast<uint8_t>(ObMediumIntType); }
  OB_INLINE bool is_int32() const { return type_ == static_cast<uint8_t>(ObInt32Type); }
  OB_INLINE bool is_int() const { return type_ == static_cast<uint8_t>(ObIntType); }
  OB_INLINE bool is_utinyint() const { return type_ == static_cast<uint8_t>(ObUTinyIntType); }
  OB_INLINE bool is_usmallint() const { return type_ == static_cast<uint8_t>(ObUSmallIntType); }
  OB_INLINE bool is_umediumint() const { return type_ == static_cast<uint8_t>(ObUMediumIntType); }
  OB_INLINE bool is_uint32() const { return type_ == static_cast<uint8_t>(ObUInt32Type); }
  OB_INLINE bool is_uint64() const { return type_ == static_cast<uint8_t>(ObUInt64Type); }
  OB_INLINE bool is_float() const { return type_ == static_cast<uint8_t>(ObFloatType); }
  OB_INLINE bool is_double() const { return type_ == static_cast<uint8_t>(ObDoubleType); }
  OB_INLINE bool is_ufloat() const { return type_ == static_cast<uint8_t>(ObUFloatType); }
  OB_INLINE bool is_udouble() const { return type_ == static_cast<uint8_t>(ObUDoubleType); }
  OB_INLINE bool is_number() const { return type_ == static_cast<uint8_t>(ObNumberType); }
  OB_INLINE bool is_unumber() const { return type_ == static_cast<uint8_t>(ObUNumberType); }
  OB_INLINE bool is_datetime() const { return type_ == static_cast<uint8_t>(ObDateTimeType); }
  OB_INLINE bool is_mysql_datetime() const { return type_ == static_cast<uint8_t>(ObMySQLDateTimeType); }
  OB_INLINE bool is_timestamp() const { return type_ == static_cast<uint8_t>(ObTimestampType); }
  OB_INLINE bool is_year() const { return type_ == static_cast<uint8_t>(ObYearType); }
  OB_INLINE bool is_date() const { return type_ == static_cast<uint8_t>(ObDateType); }
  OB_INLINE bool is_mysql_date() const { return type_ == static_cast<uint8_t>(ObMySQLDateType); }
  OB_INLINE bool is_time() const { return type_ == static_cast<uint8_t>(ObTimeType); }
  OB_INLINE bool is_decimal_int() const { return type_ == static_cast<uint8_t>(ObDecimalIntType); }
  OB_INLINE bool is_varchar() const
  {
    return ((type_ == static_cast<uint8_t>(ObVarcharType)) && (CS_TYPE_BINARY != get_collation_type()));
  }
  OB_INLINE bool is_char() const
  {
    return ((type_ == static_cast<uint8_t>(ObCharType)) && (CS_TYPE_BINARY != get_collation_type()));
  }
  OB_INLINE bool is_varbinary() const
  {
    return (type_ == static_cast<uint8_t>(ObVarcharType) && CS_TYPE_BINARY == get_collation_type());
  }
  static bool is_binary(const SqlValueType type, const ObCollationType cs_type)
  {
    return (ObCharType == type && CS_TYPE_BINARY == cs_type);
  }
  OB_INLINE bool is_binary() const
  {
    return is_binary(static_cast<SqlValueType>(type_), get_collation_type());
  }
  OB_INLINE bool is_cs_collation_free() const
  {
    return get_collation_type() == CS_TYPE_UTF8MB4_GENERAL_CI || get_collation_type() == CS_TYPE_UTF8MB4_BIN;
  }
  OB_INLINE bool is_hex_string() const { return type_ == static_cast<uint8_t>(ObHexStringType); }
  OB_INLINE bool is_ext() const { return type_ == static_cast<uint8_t>(ObExtendType); }
  OB_INLINE bool is_ext_sql_array() const { return is_ext() && get_extend_type() == T_EXT_SQL_ARRAY; }
  OB_INLINE bool is_unknown() const { return type_ == static_cast<uint8_t>(ObUnknownType); }
  OB_INLINE bool is_bit() const { return type_ == static_cast<uint8_t>(ObBitType); }
  OB_INLINE bool is_enum() const { return type_ == static_cast<uint8_t>(ObEnumType); }
  OB_INLINE bool is_set() const { return type_ == static_cast<uint8_t>(ObSetType); }
  OB_INLINE bool is_enum_or_set() const { return type_ == static_cast<uint8_t>(ObEnumType)
    || type_ == static_cast<uint8_t>(ObSetType); }
  OB_INLINE bool is_text() const
  {
    return (ob_is_text_tc(get_type()) && CS_TYPE_BINARY != get_collation_type());
  }
  OB_INLINE bool is_clob() const
  {
    return false;
  }
  OB_INLINE bool is_blob() const
  {
    return (ob_is_text_tc(get_type()) && CS_TYPE_BINARY == get_collation_type());
  }
  OB_INLINE bool is_lob_storage() const
  { return ob_is_large_text(get_type())
           || ob_is_json_tc(get_type())
           || ob_is_geometry_tc(get_type())
           || ob_is_collection_sql_type(get_type()); }
  OB_INLINE bool is_lob() const { return ob_is_text_tc(get_type()); }
  OB_INLINE bool is_inrow() const { return is_lob() && lob_scale_.is_in_row(); }
  OB_INLINE bool is_outrow() const { return is_lob() && lob_scale_.is_out_row(); }
  OB_INLINE bool has_lob_header() const { return is_lob_storage() && lob_scale_.has_lob_header(); }

  OB_INLINE bool is_json() const { return type_ == static_cast<uint8_t>(ObJsonType); }
  OB_INLINE bool is_json_inrow() const { return is_json() && lob_scale_.is_in_row(); }
  OB_INLINE bool is_json_outrow() const { return is_json() && lob_scale_.is_out_row(); }

  OB_INLINE bool is_geometry() const { return type_ == static_cast<uint8_t>(ObGeometryType); }
  OB_INLINE bool is_geometry_inrow() const { return is_geometry() && lob_scale_.is_in_row(); }
  OB_INLINE bool is_geometry_outrow() const { return is_geometry() && lob_scale_.is_out_row(); }

  // combination of above functions.
  OB_INLINE bool is_varbinary_or_binary() const { return is_varbinary() || is_binary(); }
  OB_INLINE bool is_varchar_or_char() const { return is_varchar() || is_char(); }
  OB_INLINE bool is_fixed_len_char_type() const { return is_char(); }
  OB_INLINE bool is_varying_len_char_type() const { return is_varchar(); }
  OB_INLINE bool is_character_type() const { return is_varchar_or_char(); }
  OB_INLINE bool is_collation_free_compatible() const { return is_character_type() && is_cs_collation_free(); }
  OB_INLINE bool is_numeric_type() const { return ob_is_numeric_type(get_type()); }
  OB_INLINE bool is_integer_type() const { return ob_is_integer_type(get_type()); }
  OB_INLINE bool is_string_type() const { return ob_is_string_tc(get_type()) || ob_is_text_tc(get_type()); }
  OB_INLINE bool is_string_or_lob_locator_type() const {
    return ob_is_string_tc(get_type()) || ob_is_text_tc(get_type());
  }
  OB_INLINE bool is_temporal_type() const { return ob_is_temporal_type(get_type()); }
  OB_INLINE bool is_unsigned_integer() const
  {
    return (static_cast<uint8_t>(ObUTinyIntType) <= type_
         && static_cast<uint8_t>(ObUInt64Type)   >= type_);
  }
  OB_INLINE bool is_signed_integer() const
  {
    return (static_cast<uint8_t>(ObTinyIntType) <= type_
        && static_cast<uint8_t>(ObIntType)   >= type_);
  }
  OB_INLINE bool is_unsigned() const {
    return (static_cast<uint8_t>(ObUTinyIntType) <= type_
         && static_cast<uint8_t>(ObUInt64Type)   >= type_)
        || static_cast<uint8_t>(ObUNumberType) == type_
        || static_cast<uint8_t>(ObUDoubleType) == type_
        || static_cast<uint8_t>(ObUFloatType) == type_;
  }
  OB_INLINE bool is_enumset_inner_type() const { return static_cast<uint8_t>(ObEnumInnerType) == type_
    || static_cast<uint8_t>(ObSetInnerType) == type_;}
  OB_INLINE bool is_decimal_or_float() const { return ObNumberType == type_ || ObFloatType == type_ || ObDoubleType == type_ || ObDecimalIntType == type_; }
  OB_INLINE bool is_extended_temporal_type() const { return is_datetime(); }

  OB_INLINE void set_cs_level(uint8_t cs_level) {
    cs_level_ = cs_level;
  }
  OB_INLINE uint8_t get_cs_level() const {
    return cs_level_;
  }
  OB_INLINE void set_cs_type(uint8_t cs_type) {
    cs_type_ = cs_type;
  }
  OB_INLINE uint8_t get_cs_type() const {
    return cs_type_;
  }

  OB_INLINE void set_collation_level(ObCollationLevel cs_level) {
    cs_level_ = (cs_level_ & 0xF0) | (cs_level & 0xF);
  }
  OB_INLINE void set_collation_type(ObCollationType cs_type) {
    cs_type_ = (cs_type & 0xFF);
    cs_level_ = (cs_level_ & 0xF) | ((cs_type & 0xF00) >> 4);
  }
  OB_INLINE ObCollationType get_collation_type() {
    return (is_collection_sql_type() || is_decimal_int()) ? CS_TYPE_BINARY:
              static_cast<ObCollationType>((uint16_t)cs_type_ | (((uint16_t)cs_level_ & 0xF0) << 4));
  }

  OB_INLINE void set_default_collation_type() { set_collation_type(ObCharset::get_default_collation(ObCharset::get_default_charset())); }
  OB_INLINE ObCollationLevel get_collation_level() const {
    // ObUserDefinedSQLType and ObCollectionSQLType reused cs_level as part of sub schema id,
    // therefore always return CS_LEVEL_EXPLICIT.
    return (is_collection_sql_type()) ? CS_LEVEL_EXPLICIT:
              static_cast<ObCollationLevel>(cs_level_ & 0x0F);
  }
  OB_INLINE ObCollationType get_collation_type() const {
    // ObUserDefinedSQLType reused cs_type as part of sub schema id,
    // ObDecimalIntType reuse cs_type as precision, therefore always return CS_TYPE_BINARY.
    return (is_collection_sql_type() || is_decimal_int()) ? CS_TYPE_BINARY :
                static_cast<ObCollationType>((uint16_t)cs_type_ | (((uint16_t)cs_level_ & 0xF0) << 4) );
  }
  OB_INLINE ObCharsetType get_charset_type() const {
    return ObCharset::charset_type_by_coll(get_collation_type());
  }
  OB_INLINE bool is_collation_invalid() const
  {
    return CS_LEVEL_INVALID == cs_level_ && CS_TYPE_INVALID == cs_type_;
  }
  OB_INLINE void set_collation(const SqlTypeMeta &other)
  {
    cs_level_ = other.cs_level_;
    cs_type_ = other.cs_type_;
  }
  OB_INLINE void set_scale(const ObScale scale) { scale_ = static_cast<int8_t>(scale); }
  OB_INLINE ObScale get_scale() const { return static_cast<ObScale>(scale_); }
  OB_INLINE void set_extend_type(uint8_t type) { extend_type_ = type; }
  OB_INLINE uint8_t get_extend_type() const { return is_ext() ? extend_type_ : -1; }
  OB_INLINE bool is_pl_extend_type() const
  {
    return is_ext() && extend_type_ > 0
                    && extend_type_ < T_EXT_SQL_ARRAY;
  }

// to_string function is used to calc the result of ObExprCmpMeta, can not be modified
#ifdef NDEBUG
  TO_STRING_KV(N_TYPE, inner_sql_value_type_to_string(static_cast<SqlValueType>(type_)),
               N_COLLATION, ObCharset::collation_name(get_collation_type()),
               N_COERCIBILITY, ObCharset::collation_level(get_collation_level()));
#else
  DECLARE_TO_STRING
  {
    int64_t pos = 0;
    J_OBJ_START();
    J_KV(N_TYPE, inner_sql_value_type_to_string(static_cast<SqlValueType>(type_)),
         N_COLLATION, ObCharset::collation_name(get_collation_type()),
         N_COERCIBILITY, ObCharset::collation_level(get_collation_level()));
    if (is_enum_or_set() || is_collection_sql_type()) {
      J_COMMA();
      J_KV("subschema_id", get_subschema_id());
    }
    J_OBJ_END();
    return pos;
  }
#endif
  NEED_SERIALIZE_AND_DESERIALIZE;

  static uint32_t type_offset_bits() { return offsetof(SqlTypeMeta, type_) * 8; }
  static uint32_t cs_level_offset_bits() { return offsetof(SqlTypeMeta, cs_level_) * 8; }
  static uint32_t cs_type_offset_bits() { return offsetof(SqlTypeMeta, cs_type_) * 8; }
  static uint32_t scale_offset_bits() { return offsetof(SqlTypeMeta, scale_) * 8; }

  OB_INLINE uint16_t get_subschema_id() const { return ((static_cast<uint16_t>(cs_type_)) << 8) | cs_level_; }
  OB_INLINE void set_subschema_id(const uint16_t subschema_id) {
    cs_type_ = (subschema_id >> 8) & UINT_MAX8;
    cs_level_ = subschema_id & UINT_MAX8;
  }
  OB_INLINE uint8_t get_udt_flags() const { return scale_; }
  OB_INLINE void set_udt_flags(uint8_t udt_flag) { scale_  = udt_flag; }
  // UDTs are not lobs, but they may contain lob types
  OB_INLINE void set_sql_udt(const uint16_t subschema_id) {
    type_ = static_cast<uint8_t>(ObUserDefinedSQLType);
    set_subschema_id(subschema_id);
  }
  OB_INLINE bool is_calc_end_space() const {
    return false;
  }

  void set_stored_precision(int16_t precision)
  {
    OB_ASSERT(precision <= OB_MAX_DECIMAL_PRECISION);
    // if stored_numeric_precision < CS_LEVELINVALID, is's cs_level
    // otherwise it's precision + cs_level_invalid
    stored_numeric_precision_ = static_cast<uint8_t>(precision) + CS_LEVEL_INVALID;
  }

  int16_t get_stored_precision() const
  {
    OB_ASSERT(stored_numeric_precision_ >= CS_LEVEL_INVALID);
    if (stored_numeric_precision_ <= CS_LEVEL_INVALID
        || stored_numeric_precision_ > CS_LEVEL_INVALID + OB_MAX_DECIMAL_PRECISION) {
      return PRECISION_UNKNOWN_YET;
    } else {
      return stored_numeric_precision_ - CS_LEVEL_INVALID;
    }
  }

  OB_INLINE bool is_expectd_udt_type(const uint16_t subschema_id) const { return (ObUserDefinedSQLType == type_ && get_subschema_id() == subschema_id); }
  OB_INLINE bool is_collection_sql_type() const { return ObCollectionSQLType == type_; }
  OB_INLINE void set_collection(const uint16_t subschema_id)
  {
    type_ = static_cast<uint8_t>(ObCollectionSQLType);
    lob_scale_.set_in_row();
    set_subschema_id(subschema_id);
  }
protected:
  uint8_t type_;
  union
  {
    uint8_t cs_level_; // collation level, low bits of sub schema id if type_ is UDT
    // only used in storage layer!!!
    // numeric type's precision is stored here
    uint8_t stored_numeric_precision_;
  };
  uint8_t cs_type_; // collation type, high bits of sub schema id if type_ is UDT
  union {
    int8_t scale_;  // scale, length of bit if type_ is ObBitType; reserved flags if type_ is UDT
    ObLobScale lob_scale_;
    uint8_t extend_type_; //detailed extend value(enum ObPLType), if type_ is ObExtendType
  };
};

struct ObDataType
{
  OB_UNIS_VERSION(1);
public:
  ObDataType()
    : meta_(),
      accuracy_(),
      charset_(CHARSET_UTF8MB4),
      is_binary_collation_(false),
      is_zero_fill_(false)
  {}
  TO_STRING_KV(K_(meta),
               K_(accuracy),
               K_(charset),
               K_(is_binary_collation),
               K_(is_zero_fill));
  inline void reset()
  {
    meta_.reset();
    accuracy_.reset();
    charset_ = CHARSET_UTF8MB4;
    is_binary_collation_ = false;
    is_zero_fill_ = false;
  }
  inline bool operator==(const ObDataType &other) const
  {
    return meta_ == other.meta_
        && accuracy_ == other.accuracy_
        && charset_ == other.charset_
        && is_binary_collation_ == other.is_binary_collation_
        && is_zero_fill_ == other.is_zero_fill_;
  }
  inline SqlValueType get_obj_type() const { return meta_.get_type(); }
  inline SqlTypeClass get_type_class() const { return meta_.get_type_class(); }
  inline ObLength get_length() const { return accuracy_.get_length(); }
  inline ObPrecision get_precision() const { return accuracy_.get_precision(); }
  inline ObLengthSemantics get_length_semantics() const { return accuracy_.get_length_semantics(); }
  inline ObScale get_scale() const { return accuracy_.get_scale(); }
  inline ObCharsetType get_charset_type() const { return charset_; }
  inline ObCollationType get_collation_type() const { return meta_.get_collation_type(); }
  inline ObCollationLevel get_collation_level() const { return meta_.get_collation_level(); }
  inline uint16_t get_subschema_id() { return meta_.get_subschema_id(); }
  inline bool is_binary_collation() const { return is_binary_collation_; }
  inline bool is_zero_fill() const { return is_zero_fill_; }
  inline void set_obj_type(const SqlValueType &type) { return meta_.set_type(type); }
  inline void set_length(const ObLength length) { accuracy_.set_length(length); }
  inline void set_precision(const ObPrecision precision) { accuracy_.set_precision(precision); }
  inline void set_length_semantics(const ObLengthSemantics length_semantics) { accuracy_.set_length_semantics(length_semantics); }
  inline void set_scale(const ObScale scale) { accuracy_.set_scale(scale); }
  inline void set_charset_type(const ObCharsetType charset_type) { charset_ = charset_type; }
  inline void set_collation_type(const ObCollationType coll_type) { meta_.set_collation_type(coll_type); }
  inline void set_collation_level(const ObCollationLevel coll_level) { meta_.set_collation_level(coll_level); }
  inline void set_binary_collation(const bool is_binary_collation) { is_binary_collation_ = is_binary_collation; }
  inline void set_zero_fill(const bool is_zero_fill) { is_zero_fill_ = is_zero_fill; }
  inline const SqlTypeMeta &get_meta_type() const { return meta_; }
  inline void set_meta_type(const SqlTypeMeta &meta_type) { meta_ = meta_type; }
  inline const ObAccuracy &get_accuracy() const { return accuracy_; }
  inline void set_accuracy(const ObAccuracy &accuracy) { accuracy_ = accuracy; }
  inline int64_t get_accuracy_value() const { return accuracy_.accuracy_; }
  inline void set_int() { meta_.set_int(); }
  inline void set_uint64() { meta_.set_uint64(); }
  inline uint64_t get_udt_id() const { return accuracy_.get_accuracy(); }
  inline void set_udt_id(uint64_t udt_id) { accuracy_.set_accuracy(udt_id); }
  inline void set_subschema_id(const uint16_t subschema_id) { meta_.set_subschema_id(subschema_id); }
  SqlTypeMeta meta_;
  ObAccuracy accuracy_;
  ObCharsetType charset_;
  bool is_binary_collation_;
  bool is_zero_fill_;
};

} // namespace common
} // namespace oceanbase
