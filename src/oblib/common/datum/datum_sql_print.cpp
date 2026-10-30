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

#include "common/datum/datum_sql_print.h"
#include "common/timezone/ob_time_convert.h"
#include "common/json_type/ob_json_base.h"
#include "common/json_type/ob_json_parse.h"
#include "lib/charset/ob_dtoa.h"
#include "lib/encode/ob_base64_encode.h"
#include "lib/string/ob_sql_string.h"
#include "lib/utility/ob_print_utils.h"

#ifdef OCEANBASE_COMMON_OB_OBJECT_H_
#error "Native SQL printing must not include the legacy value definition"
#endif

namespace oceanbase { namespace common {
DEF_TO_STRING(ObHexEscapeSqlStr)
{
  int64_t buf_pos = 0;
  if (buf != NULL && buf_len > 0 && !str_.empty()) {
    const char *end = str_.ptr() + str_.length();
    if (do_single_quote_escape_) {
      for (const char *cur = str_.ptr(); cur < end && buf_pos < buf_len; ++cur) {
        if ('\'' == *cur) {
          // Only handle single quote escape in this escape mode.
          buf[buf_pos++] = '\'';
          if (buf_pos < buf_len) {
            buf[buf_pos++] = *cur;
          }
        } else {
          buf[buf_pos++] = *cur;
        }
      }
    } else if (skip_escape_) {
      // do not escape_ while in NO_BACKSLASH_ESCAPES mode
      for (const char *cur = str_.ptr(); cur < end && buf_pos < buf_len; ++cur) {
        buf[buf_pos++] = *cur;
      }
    } else {
      for (const char *cur = str_.ptr(); cur < end && buf_pos < buf_len; ++cur) {
        switch (*cur) {
          case '\\': {
            buf[buf_pos++] = '\\';
            if (buf_pos < buf_len) {
              buf[buf_pos++] = '\\';
            }
            break;
          }
          case '\0': {
            buf[buf_pos++] = '\\';
            if (buf_pos < buf_len) {
              buf[buf_pos++] = '0';
            }
            break;
          }
          case '\'':
          case '\"': {
            // A ' or " appeared in the string, it needs to be escaped
            buf[buf_pos++] = '\\';
            if (buf_pos < buf_len) {
              buf[buf_pos++] = *cur;
            }
            break;
          }
          case '\n': {
            buf[buf_pos++] = '\\';
            if (buf_pos < buf_len) {
              buf[buf_pos++] = 'n';
            }
            break;
          }
          case '\r': {
            buf[buf_pos++] = '\\';
            if (buf_pos < buf_len) {
              buf[buf_pos++] = 'r';
            }
            break;
          }
          case '\t': {
            buf[buf_pos++] = '\\';
            if (buf_pos < buf_len) {
              buf[buf_pos++] = 't';
            }
            break;
          }
          default: {
            buf[buf_pos++] = *cur;
            break;
          }
        }
      }
    }
  }
  return buf_pos;
}

int64_t ObHexEscapeSqlStr::get_extra_length() const
{
  int64_t ret_length = 0;
  if (!str_.empty()) {
    const char *end = str_.ptr() + str_.length();
    if (do_single_quote_escape_) {
      for (const char *cur = str_.ptr(); cur < end; ++cur) {
        if ('\'' == *cur) {
          ++ret_length;
        }
      }
    } else {
      for (const char *cur = str_.ptr(); cur < end; ++cur) {
        switch (*cur) {
          case '\\':
          case '\0':
          case '\'':
          case '\"':
          case '\n':
          case '\r':
          case '\t': {
            ++ret_length;
            break;
          }
          default: {
            //do nothing
          }
        }
      }
    }
  }
  return ret_length;
}


namespace {
int print_null(char *buffer, int64_t length, int64_t &pos, const ObValuePrintParams &params)
{
  int ret = OB_SUCCESS;
  const char *NULL_VALUE_STR = "NULL";
  if (params.print_null_string_value_) {
    switch (params.sql_value_type_) {
      case ObNullType:
        break;
      case ObTinyIntType:
      case ObSmallIntType:
      case ObMediumIntType:
      case ObInt32Type:
      case ObIntType:
        NULL_VALUE_STR = "CAST(NULL AS SIGNED)";
        break;
      case ObUTinyIntType:
      case ObUSmallIntType:
      case ObUMediumIntType:
      case ObUInt32Type:
      case ObUInt64Type:
        NULL_VALUE_STR = "CAST(NULL AS SIGNED)";
        break;
      case ObFloatType:
        NULL_VALUE_STR = "CAST(NULL AS FLOAT)";
        break;
      case ObDoubleType:
        NULL_VALUE_STR = "TO_BINARY_DOUBLE(NULL)";
        break;
      case ObUFloatType:
      case ObUDoubleType:
        break;
      case ObNumberType:
        NULL_VALUE_STR = "CAST(NULL AS DECIMAL)";
        break;
      case ObUNumberType:
        break;
      case ObDateTimeType:
      case ObMySQLDateTimeType:
        NULL_VALUE_STR = "STR_TO_DATE(NULL, '%Y-%m-%d %H:%i:%s')";
        break;
      case ObTimestampType:
        NULL_VALUE_STR = "FROM_UNIXTIME(UNIX_TIMESTAMP(NULL), '%Y-%m-%d %H:%i:%s')";//only need map to mysql datatype
        break;
      case ObDateType:
      case ObMySQLDateType:
        NULL_VALUE_STR = "STR_TO_DATE(NULL, '%Y-%m-%d')";//only need map to mysql datatype
        break;
      case ObTimeType:
        NULL_VALUE_STR = "CAST(NULL AS TIME)";//only need map to mysql datatype
        break;
      case ObYearType:
        NULL_VALUE_STR = "CAST(NULL AS YEAR)";//only need map to mysql datatype
        break;
      case ObVarcharType:
      case ObCharType:
        break;
      case ObHexStringType:
      case ObExtendType:
      case ObUnknownType:
      case ObTinyTextType:
      case ObTextType:
      case ObMediumTextType:
      case ObLongTextType:
      case ObBitType:
      case ObEnumType:
      case ObSetType:
      case ObEnumInnerType:
      case ObSetInnerType:
        break;
      case ObJsonType:
      case ObGeometryType:
      case ObUserDefinedSQLType:
      case ObCollectionSQLType:
      default:
        break;
    }
  }
  ret = databuff_printf(buffer, length, pos, "%s", NULL_VALUE_STR);
  return ret;
}
int print_hex(const ObString &str, char *buffer, int64_t length, int64_t &pos)
{
  int ret = databuff_printf(buffer, length, pos, "X'");
  if (OB_SUCC(ret)) { ret = hex_print(str.ptr(), str.length(), buffer, length, pos); }
  if (OB_SUCC(ret)) { ret = databuff_printf(buffer, length, pos, "'"); }
  return ret;
}
int print_safe_character(const DatumValue &value, char *buffer, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  const ObCollationType coll = value.get_collation_type();
  const ObCharsetType charset = ObCharset::charset_type_by_coll(coll);
  const char *name = ObCharset::charset_name(charset);
  const char *collation = ObCharset::collation_name(coll);
  if (!ObCharset::is_valid_collation(charset, coll) || name == nullptr || collation == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(databuff_printf(buffer, length, pos, "CONVERT(x'"))) {
  } else if (OB_FAIL(hex_print(value.datum_.ptr_, value.datum_.len_, buffer, length, pos))) {
  } else { ret = databuff_printf(buffer, length, pos, "' USING %s) COLLATE %s", name, collation); }
  return ret;
}
int print_character(const DatumValue &value, char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params)
{
  int ret = OB_SUCCESS;
  const ObString str = value.get_string();
  const ObCharsetType source = ObCharset::charset_type_by_coll(value.get_collation_type());
  const ObCharsetType target = ObCharset::charset_type_by_coll(params.cs_type_);
  if (source == CHARSET_BINARY) { ret = print_hex(str, buffer, length, pos); }
  else if (params.character_hex_safe_represent_ && ob_is_character_type(value.get_type(), value.get_collation_type())) {
    ret = print_safe_character(value, buffer, length, pos);
  } else if (OB_FAIL(databuff_printf(buffer, length, pos, "'"))) {
  } else if (source == target || source == CHARSET_INVALID) {
    ObHexEscapeSqlStr escaped(str, params.skip_escape_);
    pos += escaped.to_string(buffer + pos, length - pos);
    ret = databuff_printf(buffer, length, pos, "'");
  } else {
    uint32_t converted = 0;
    if (OB_FAIL(ObCharset::charset_convert(value.get_collation_type(), str.ptr(), str.length(),
        params.cs_type_, buffer + pos, length - pos, converted))) {
    } else {
      ObHexEscapeSqlStr escaped(ObString(converted, buffer + pos), params.skip_escape_);
      const int64_t temporary = pos + converted;
      const int64_t bytes = escaped.to_string(buffer + temporary, length - temporary);
      if (temporary + bytes >= length) { ret = OB_SIZE_OVERFLOW; }
      else {
        MEMMOVE(buffer + pos, buffer + temporary, bytes); pos += bytes;
        ret = databuff_printf(buffer, length, pos, "'");
      }
    }
  }
  return ret;
}
int print_text(const DatumValue &value, char *buffer, int64_t length, int64_t &pos)
{
  ObString data;
  ObLobLocatorV2 locator(value.get_string(), value.has_lob_header());
  int ret = locator.has_inrow_data() ? locator.get_inrow_data(data) : OB_NOT_SUPPORTED;
  if (OB_SUCC(ret)) {
    data.assign_ptr(data.ptr(), MIN(data.length(), length - pos));
    if (value.get_collation_type() == CS_TYPE_BINARY) { ret = print_hex(data, buffer, length, pos); }
    else if (OB_FAIL(databuff_printf(buffer, length, pos, "'"))) {
    } else {
      ObHexEscapeSqlStr escaped(data);
      pos += escaped.to_string(buffer + pos, length - pos);
      ret = databuff_printf(buffer, length, pos, "'");
    }
  }
  return ret;
}
int print_json(const DatumValue &value, char *buffer, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;
  ObString str = value.get_string();
  if (value.has_lob_header() || (str.length() >= sizeof(ObLobCommon)
      && reinterpret_cast<const ObLobCommon *>(str.ptr())->is_valid())) {
    ObLobLocatorV2 locator(str, true);
    if (!locator.is_valid(false)) {
      // Match the existing diagnostic printer: an invalid locator is parsed
      // as ordinary JSON below, which reports the actual JSON parse error.
    } else if (!locator.has_inrow_data()) {
      ret = databuff_printf(buffer, length, pos, "'outrow json'");
      if (OB_SUCC(ret)) { ret = OB_INVALID_ARGUMENT; }
    } else { ret = locator.get_inrow_data(str); }
  }
  ObArenaAllocator owner;
  ObJsonBuffer json(&owner);
  ObIJsonBase *base = nullptr;
  if (OB_FAIL(ret) || str.empty()) {
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&owner, str,
      ObJsonInType::JSON_BIN, ObJsonInType::JSON_BIN, base, 0))) {
  } else if (OB_FAIL(base->print(json, false, str.length()))) {
  } else if (OB_FAIL(databuff_printf(buffer, length, pos, "'"))) {
  } else if (OB_FAIL(databuff_printf(buffer, length, pos, "%.*s",
      static_cast<int>(MIN(json.length(), length - pos)), json.ptr()))) {
  } else { ret = databuff_printf(buffer, length, pos, "'"); }
  return ret;
}
int print_datetime(const DatumValue &value, char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params)
{
  const bool datetime = ob_is_datetime_or_mysql_datetime(value.get_type());
  int ret = OB_SUCCESS;
  if (params.print_const_expr_type_ && datetime) { ret = databuff_printf(buffer, length, pos, "CAST('"); }
  else if (params.print_const_expr_type_ && value.get_type() == ObTimestampType) {
    ret = databuff_printf(buffer, length, pos, "TIMESTAMP %s", params.beginning_space_ ? "' " : "'");
  } else { ret = databuff_printf(buffer, length, pos, "%s", params.beginning_space_ ? "' " : "'"); }
  if (OB_SUCC(ret)) {
    const ObTimeZoneInfo *zone = value.get_type() == ObTimestampType ? params.tz_info_ : nullptr;
    if (value.get_type() == ObMySQLDateTimeType) {
      ret = ObTimeConverter::mdatetime_to_str(value.datum_.get_mysql_datetime(), zone,
          value.get_scale(), buffer, length, pos);
    } else {
      ret = ObTimeConverter::datetime_to_str(value.datum_.get_datetime(), zone,
          value.get_scale(), buffer, length, pos);
    }
  }
  if (OB_SUCC(ret)) { ret = databuff_printf(buffer, length, pos,
      params.print_const_expr_type_ && datetime ? "' AS DATETIME)" : "'"); }
  return ret;
}
} // namespace

int DatumValue::print_sql_literal(char *buffer, int64_t length, int64_t &pos) const
{ return print_datum_sql_literal(*this, buffer, length, pos); }

int DatumValue::print_sql_literal(char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params) const
{ return print_datum_sql_literal(*this, buffer, length, pos, params); }

int print_datum_sql_literal(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params)
{
  if (buffer == nullptr || length < 0 || pos < 0 || pos > length || value.is_invalid_type()) {
    return OB_INVALID_ARGUMENT;
  }
  // A full output buffer is retryable; SET SQL grows its allocation only
  // for this error. Keep invalid arguments separate from buffer exhaustion.
  if (pos == length) { return OB_SIZE_OVERFLOW; }
  if (value.is_null()) { return print_null(buffer, length, pos, params); }
  const ObDatum &data = value.datum_;
  int ret = OB_SUCCESS;
  switch (value.get_type()) {
    case ObTinyIntType: return databuff_printf(buffer, length, pos, "%hhd", static_cast<int8_t>(data.get_int()));
    case ObSmallIntType: return databuff_printf(buffer, length, pos, "%hd", static_cast<int16_t>(data.get_int()));
    case ObMediumIntType: case ObInt32Type: return databuff_printf(buffer, length, pos, "%d", static_cast<int32_t>(data.get_int()));
    case ObIntType: return databuff_printf(buffer, length, pos, "%" PRId64, data.get_int());
    case ObUTinyIntType: return databuff_printf(buffer, length, pos, "%hhu", static_cast<uint8_t>(data.get_uint()));
    case ObUSmallIntType: return databuff_printf(buffer, length, pos, "%hu", static_cast<uint16_t>(data.get_uint()));
    case ObUMediumIntType: case ObUInt32Type: return databuff_printf(buffer, length, pos, "%u", static_cast<uint32_t>(data.get_uint()));
    case ObUInt64Type: case ObEnumType: case ObSetType: return databuff_printf(buffer, length, pos, "%" PRIu64, data.get_uint());
    case ObFloatType: case ObUFloatType: case ObDoubleType: case ObUDoubleType: {
      const bool dbl = value.get_type() == ObDoubleType || value.get_type() == ObUDoubleType;
      // gcvt can write a terminator even when its width is one (NaN/Inf).
      // Render into bounded scratch, then publish only when the output and
      // terminator fit. Never let its width contract write past the caller.
      char rendered[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE + 1];
      const int width = static_cast<int>(MIN(length - pos, sizeof(rendered) - 1));
      const int64_t bytes = ob_gcvt_opt(dbl ? data.get_double() : data.get_float(),
          dbl ? OB_GCVT_ARG_DOUBLE : OB_GCVT_ARG_FLOAT, width, rendered, nullptr, TRUE);
      if (bytes <= 0 || bytes >= length - pos) { ret = OB_SIZE_OVERFLOW; }
      else { MEMCPY(buffer + pos, rendered, bytes); pos += bytes; buffer[pos] = '\0'; }
      break;
    }
    case ObNumberType: case ObUNumberType: {
      number::ObNumber number = data.get_number();
      ret = number.format(buffer, length, pos, value.get_scale()); break;
    }
    case ObDateTimeType: case ObMySQLDateTimeType: case ObTimestampType:
      return print_datetime(value, buffer, length, pos, params);
    case ObDateType: case ObMySQLDateType: case ObTimeType: case ObYearType: {
      const bool date = value.get_type() == ObDateType || value.get_type() == ObMySQLDateType;
      ret = databuff_printf(buffer, length, pos, "%s", params.print_const_expr_type_
          ? date ? "DATE '" : value.get_type() == ObTimeType ? "TIME '" : "'" : "'");
      if (OB_SUCC(ret)) {
        if (value.get_type() == ObDateType) { ret = ObTimeConverter::date_to_str(data.get_date(), buffer, length, pos); }
        else if (value.get_type() == ObMySQLDateType) { ret = ObTimeConverter::mdate_to_str(data.get_mysql_date(), buffer, length, pos); }
        else if (value.get_type() == ObTimeType) { ret = ObTimeConverter::time_to_str(data.get_time(), value.get_scale(), buffer, length, pos); }
        else { ret = ObTimeConverter::year_to_str(data.get_year(), buffer, length, pos); }
      }
      if (OB_SUCC(ret)) { ret = databuff_printf(buffer, length, pos, "'"); }
      break;
    }
    case ObVarcharType: case ObCharType: return print_character(value, buffer, length, pos, params);
    case ObHexStringType: return print_hex(value.get_string(), buffer, length, pos);
    case ObTinyTextType: case ObTextType: case ObMediumTextType: case ObLongTextType: case ObGeometryType:
      return print_text(value, buffer, length, pos);
    case ObBitType:
      if (OB_FAIL(databuff_printf(buffer, length, pos, "b'"))) {
      } else if (OB_FAIL(bit_print(data.get_uint(), buffer, length, pos))) {
      } else { ret = databuff_printf(buffer, length, pos, "'"); } break;
    case ObDecimalIntType:
      if (OB_FAIL(wide::to_string(data.get_decimal_int(), data.get_int_bytes(), value.get_scale(), buffer, length, pos))) {
      } else if (pos >= length) { ret = OB_SIZE_OVERFLOW; }
      else { buffer[pos] = '\0'; } break;
    case ObUnknownType: return databuff_printf(buffer, length, pos, ":%" PRId64, data.get_int());
    case ObExtendType: {
      const int64_t marker = value.get_ext();
      const char *text = "Extend Obj";
      if (marker == ObActionFlag::OP_MIN_OBJ || marker == DATUM_MIN_EXT_VALUE) { text = "__OB__MIN__"; }
      else if (marker == ObActionFlag::OP_MAX_OBJ || marker == DATUM_MAX_EXT_VALUE) { text = "__OB__MAX__"; }
      else if (marker == ObActionFlag::OP_NOP) { text = "__OB__NOP__"; }
      else if (marker == ObActionFlag::OP_DEFAULT_NOW_FLAG) { text = N_UPPERCASE_CUR_TIMESTAMP; }
      return databuff_printf(buffer, length, pos, "%s", text);
    }
    case ObJsonType: return print_json(value, buffer, length, pos);
    case ObEnumInnerType: case ObSetInnerType: return OB_ERR_UNEXPECTED;
    case ObUserDefinedSQLType: return OB_NOT_SUPPORTED;
    case ObCollectionSQLType: {
      ObString str = value.get_string(); ObLobLocatorV2 locator(str, true);
      if (!locator.is_valid(false)) {
      } else if (!locator.is_inrow()) {
        ret = databuff_printf(buffer, length, pos, "'outrow udt'");
        return OB_SUCC(ret) ? OB_INVALID_ARGUMENT : ret;
      } else { ret = locator.get_inrow_data(str); }
      if (OB_SUCC(ret) && OB_SUCC(ret = databuff_printf(buffer, length, pos, "'"))) {
        ObHexEscapeSqlStr escaped(str); pos += escaped.to_string(buffer + pos, length - pos);
        ret = databuff_printf(buffer, length, pos, "'");
      }
      break;
    }
    default: return OB_INVALID_ARGUMENT;
  }
  return ret;
}
namespace {
int print_character_text(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params, bool quoted)
{
  int ret = OB_SUCCESS;
  const ObString str = value.get_string();
  const ObCharsetType source = ObCharset::charset_type_by_coll(value.get_collation_type());
  const ObCharsetType target = ObCharset::charset_type_by_coll(params.cs_type_);
  if (quoted && OB_FAIL(databuff_printf(buffer, length, pos, "'"))) {
  } else if (source == CHARSET_BINARY || source == target || source == CHARSET_INVALID) {
    if (!quoted && value.get_collation_type() == CS_TYPE_BINARY && params.binary_string_print_hex_) {
      ret = hex_print(str.ptr(), str.length(), buffer, length, pos);
    } else if (!quoted && value.get_collation_type() == CS_TYPE_BINARY && params.binary_string_print_base64_) {
      ret = ObBase64Encoder::encode(reinterpret_cast<const uint8_t *>(str.ptr()), str.length(), buffer, length, pos);
    } else if (!quoted && params.use_memcpy_) {
      ret = databuff_memcpy(buffer, length, pos, str.length(), str.ptr());
    } else {
      ret = databuff_printf(buffer, length, pos, "%.*s", str.length(), str.ptr());
    }
    if (OB_SUCC(ret) && !quoted && params.refine_range_max_value_) {
      int64_t tails = 0;
      while (pos > 0 && static_cast<char>(0xff) == buffer[pos - 1]) { --pos; ++tails; }
      if (tails > 0) { ret = databuff_printf(buffer, length, pos, "<FF><repeat %ld times>", tails); }
    }
  } else {
    uint32_t converted = 0;
    if (OB_FAIL(ObCharset::charset_convert(value.get_collation_type(), str.ptr(), str.length(),
        params.cs_type_, buffer + pos, length - pos, converted))) {
    } else { pos += converted; }
  }
  if (OB_SUCC(ret) && quoted) { ret = databuff_printf(buffer, length, pos, "'"); }
  return ret;
}

ObString text_print_data(const DatumValue &value, int64_t max_length)
{
  ObString str = value.get_string();
  if (!str.empty() && value.has_lob_header()) {
    ObLobLocatorV2 locator(str, true);
    if (!locator.is_valid(false)) {
      // The diagnostic/default printer retains invalid locator bytes verbatim.
    } else if (!locator.has_inrow_data()) {
      str.assign_ptr("outrow", 6);
    } else if (OB_SUCCESS == locator.get_inrow_data(str)) {
      str.assign_ptr(str.ptr(), MIN(str.length(), max_length));
    }
  }
  return str;
}

int print_temporal_text(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params, bool quoted)
{
  int ret = OB_SUCCESS;
  const ObDatum &data = value.datum_;
  const ObTimeZoneInfo *zone = value.get_type() == ObTimestampType ? params.tz_info_ : nullptr;
  const ObScale scale = quoted ? value.get_scale() : OB_MAX_DATETIME_PRECISION;
  if (quoted && OB_FAIL(databuff_printf(buffer, length, pos, "'"))) {
  } else {
    switch (value.get_type()) {
      case ObDateTimeType: case ObTimestampType:
        ret = ObTimeConverter::datetime_to_str(data.get_datetime(), zone, scale, buffer, length, pos); break;
      case ObMySQLDateTimeType:
        ret = ObTimeConverter::mdatetime_to_str(data.get_mysql_datetime(), zone, scale, buffer, length, pos); break;
      case ObDateType: ret = ObTimeConverter::date_to_str(data.get_date(), buffer, length, pos); break;
      case ObMySQLDateType: ret = ObTimeConverter::mdate_to_str(data.get_mysql_date(), buffer, length, pos); break;
      case ObTimeType:
        ret = ObTimeConverter::time_to_str(data.get_time(), value.get_scale(), buffer, length, pos); break;
      case ObYearType: ret = ObTimeConverter::year_to_str(data.get_year(), buffer, length, pos); break;
      default: ret = OB_ERR_UNEXPECTED; break;
    }
  }
  if (OB_SUCC(ret) && quoted) { ret = databuff_printf(buffer, length, pos, "'"); }
  return ret;
}

int print_json_text(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params)
{
  int ret = OB_SUCCESS;
  ObString str = value.get_string();
  if (value.has_lob_header() || (str.length() >= sizeof(ObLobCommon)
      && reinterpret_cast<const ObLobCommon *>(str.ptr())->is_valid())) {
    ObLobLocatorV2 locator(str, true);
    if (!locator.is_valid(false)) {
    } else if (!locator.has_inrow_data()) {
      databuff_printf(buffer, length, pos, "'outrow json'");
      ret = OB_INVALID_ARGUMENT;
    } else { ret = locator.get_inrow_data(str); }
  }
  ObArenaAllocator owner;
  ObJsonBuffer json(&owner);
  ObIJsonBase *base = nullptr;
  if (OB_FAIL(ret) || str.empty()) {
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(&owner, str,
      ObJsonInType::JSON_BIN, ObJsonInType::JSON_BIN, base, 0))) {
  } else if (OB_FAIL(base->print(json, false, str.length()))) {
  } else if (params.use_memcpy_) {
    ret = databuff_memcpy(buffer, length, pos, json.length(), json.ptr());
  } else {
    ret = databuff_printf(buffer, length, pos, "%.*s",
        static_cast<int>(MIN(json.length(), length - pos)), json.ptr());
  }
  return ret;
}

int print_value_text(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params, bool quoted)
{
  if (buffer == nullptr || length < 0 || pos < 0 || pos > length || value.is_invalid_type()) {
    return OB_INVALID_ARGUMENT;
  }
  if (value.is_null() || value.get_type() == ObNullType) {
    return databuff_printf(buffer, length, pos, "NULL");
  }
  const ObDatum &data = value.datum_;
  int ret = OB_SUCCESS;
  switch (value.get_type()) {
    case ObVarcharType: case ObCharType:
      return print_character_text(value, buffer, length, pos, params, quoted);
    case ObTinyTextType: case ObTextType: case ObMediumTextType: case ObLongTextType: case ObGeometryType: {
      DatumValue raw = value;
      raw.datum_.set_string(text_print_data(value, length - pos));
      if (quoted) {
        return databuff_printf(buffer, length, pos, "'%.*s'", raw.get_string_len(), raw.get_string_ptr());
      }
      return print_character_text(raw, buffer, length, pos, params, false);
    }
    case ObDateTimeType: case ObMySQLDateTimeType: case ObTimestampType:
    case ObDateType: case ObMySQLDateType: case ObTimeType: case ObYearType:
      return print_temporal_text(value, buffer, length, pos, params, quoted);
    case ObHexStringType:
      if (quoted) { return print_hex(value.get_string(), buffer, length, pos); }
      if (OB_FAIL(databuff_printf(buffer, length, pos, "X"))) {
      } else { ret = hex_print(data.ptr_, data.len_, buffer, length, pos); }
      return ret;
    case ObBitType:
      if (quoted) { return print_datum_sql_literal(value, buffer, length, pos); }
      if (params.binary_string_print_hex_) { return databuff_printf(buffer, length, pos, "%" PRIX64, data.get_uint()); }
      if (params.binary_string_print_base64_) {
        const uint64_t bits = data.get_uint();
        const ObScale scale = value.get_scale() < 1 || value.get_scale() > 64 ? 64 : value.get_scale();
        return ObBase64Encoder::encode(reinterpret_cast<const uint8_t *>(&bits), (scale + 7) / 8, buffer, length, pos);
      }
      return databuff_printf(buffer, length, pos, "%" PRIu64, data.get_uint());
    case ObUnknownType: return databuff_printf(buffer, length, pos, quoted ? "'?'" : "?");
    case ObExtendType: {
      const int64_t marker = value.get_ext();
      const char *text = nullptr;
      if (marker == ObActionFlag::OP_MIN_OBJ) { text = "__OB__MIN__"; }
      else if (marker == ObActionFlag::OP_MAX_OBJ) { text = "__OB__MAX__"; }
      else if (marker == ObActionFlag::OP_NOP) { text = "__OB__NOP__"; }
      else if (marker == ObActionFlag::OP_DEFAULT_NOW_FLAG) { text = N_UPPERCASE_CUR_TIMESTAMP; }
      // Plain/varchar intentionally reject other Ext values, including the
      // DATUM_MIN/MAX sentinels accepted by the SQL literal printer.
      return text == nullptr ? OB_INVALID_ARGUMENT
          : databuff_printf(buffer, length, pos, quoted ? "'%s'" : "%s", text);
    }
    case ObJsonType:
      return quoted ? print_json(value, buffer, length, pos)
                    : print_json_text(value, buffer, length, pos, params);
    case ObEnumInnerType: case ObSetInnerType: {
      if (!quoted) { return OB_ERR_UNEXPECTED; }
      ObEnumSetInnerValue inner;
      int64_t cursor = 0;
      if (OB_FAIL(inner.deserialize(data.ptr_, data.len_, cursor))) {
      } else { ret = databuff_printf(buffer, length, pos, "%.*s; %ld",
          inner.string_value_.length(), inner.string_value_.ptr(), inner.numberic_value_); }
      return ret;
    }
    case ObUserDefinedSQLType: return OB_NOT_SUPPORTED;
    case ObCollectionSQLType: {
      ObString str = value.get_string();
      ObLobLocatorV2 locator(str, true);
      if (!locator.is_valid(false)) {
      } else if (!locator.is_inrow()) {
        databuff_printf(buffer, length, pos, "'outrow udt'");
        return OB_INVALID_ARGUMENT;
      } else if (OB_FAIL(locator.get_inrow_data(str))) { return ret; }
      if (quoted) { return databuff_printf(buffer, length, pos, "'%.*s'", str.length(), str.ptr()); }
      if (params.coll_meta_ != nullptr) {
        return print_collection_plain_payload(str, *params.coll_meta_,
            params.use_memcpy_, buffer, length, pos);
      }
      DatumValue raw = value;
      raw.datum_.set_string(str);
      raw.meta_.set_collation_type(CS_TYPE_BINARY);
      return print_character_text(raw, buffer, length, pos, params, false);
    }
    case ObNumberType: case ObUNumberType: {
      number::ObNumber number = data.get_number();
      if (quoted) {
        if (length - pos < number::ObNumber::MAX_APPEND_LEN) { return OB_SIZE_OVERFLOW; }
        buffer[pos++] = '\'';
        if (OB_FAIL(number.format(buffer, length - 3, pos, value.get_scale()))) {
        } else if (pos + 1 >= length) { ret = OB_SIZE_OVERFLOW; }
        else { buffer[pos++] = '\''; buffer[pos] = '\0'; }
      } else if (OB_FAIL(number.format(buffer, length - 1, pos, value.get_scale()))) {
      } else if (pos >= length) { ret = OB_SIZE_OVERFLOW; }
      else { buffer[pos] = '\0'; }
      return ret;
    }
    case ObDecimalIntType:
      if (quoted && length - pos < number::ObNumber::MAX_APPEND_LEN) { return OB_SIZE_OVERFLOW; }
      break;
    default: break;
  }
  // Remaining types are numeric. Rendering uses only raw value metadata and
  // intentionally ignores the caller's SQL CAST/converter/escape flags.
  if (quoted && OB_FAIL(databuff_printf(buffer, length, pos, "'"))) {
  } else if (OB_FAIL(print_datum_sql_literal(value, buffer, length, pos))) {
  } else if (quoted) { ret = databuff_printf(buffer, length, pos, "'"); }
  return ret;
}

int print_enum_set_text(const DatumValue &value, const ObIArray<ObString> &type_infos,
    char *buffer, int64_t length, int64_t &pos, bool quoted)
{
  int ret = OB_SUCCESS;
  ObSqlString text;
  if (value.get_type() == ObEnumType) {
    const uint64_t ordinal = value.datum_.get_uint();
    if (ordinal > type_infos.count()) { ret = OB_ERR_UNEXPECTED; }
    else if (ordinal > 0) { ret = text.append(type_infos.at(ordinal - 1)); }
  } else if (value.get_type() == ObSetType) {
    const uint64_t bits = value.datum_.get_uint();
    const int64_t count = type_infos.count();
    if (count <= 0 || count > 64 || (count < 64 && bits > ((1ULL << count) - 1))) {
      ret = OB_ERR_UNEXPECTED;
    }
    bool first = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < count; ++i) {
      if (bits & (1ULL << i)) {
        if (!first && OB_FAIL(text.append(","))) {
        } else if (OB_FAIL(text.append(type_infos.at(i)))) {
        } else { first = false; }
      }
    }
  } else { ret = OB_ERR_UNEXPECTED; }
  if (OB_SUCC(ret)) {
    // Keep the existing enum/set-overload contract: formatting exhaustion is
    // reflected in cursor/output but does not replace the enum validation code.
    databuff_printf(buffer, length, pos, quoted ? "'%.*s'" : "%.*s",
        static_cast<int32_t>(text.length()), text.ptr());
  }
  return ret;
}
} // namespace

int print_datum_plain_str_literal(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params)
{ return print_value_text(value, buffer, length, pos, params, false); }
int print_datum_varchar_literal(const DatumValue &value, char *buffer, int64_t length,
    int64_t &pos, const ObValuePrintParams &params)
{ return print_value_text(value, buffer, length, pos, params, true); }
namespace {
int print_debug_prefix(const DatumValue &value, char *buffer, int64_t length, int64_t &pos)
{
  return databuff_printf(buffer, length, pos, "{\"%s\":", sql_value_type_to_string(value.get_type()));
}
int print_debug_collation(const DatumValue &value, char *buffer, int64_t length, int64_t &pos)
{
  return databuff_printf(buffer, length, pos, ",\"collation\":\"%s\",\"coercibility\":\"%s\"",
      ObCharset::collation_name(value.get_collation_type()),
      ObCharset::collation_level(value.get_collation_level()));
}
int print_debug_payload(const DatumValue &value, char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params)
{
  int ret = OB_SUCCESS;
  const ObDatum &datum = value.datum_;
  const SqlValueType type = value.get_type();
  switch (type) {
    case ObNullType:
      ret = databuff_printf(buffer, length, pos, "\"NULL\""); break;
    case ObVarcharType: case ObCharType: {
      const ObCharsetType charset = ObCharset::charset_type_by_coll(value.get_collation_type());
      if (charset == CHARSET_BINARY || charset == CHARSET_UTF16 || charset == CHARSET_UTF16LE) {
        ret = hex_print(datum.ptr_, datum.len_, buffer, length, pos);
      } else {
        ret = databuff_print_obj(buffer, length, pos, value.get_string());
      }
      if (OB_SUCC(ret)) { ret = print_debug_collation(value, buffer, length, pos); }
      break;
    }
    case ObHexStringType:
      if (OB_FAIL(databuff_printf(buffer, length, pos, "\""))) {
      } else if (OB_FAIL(hex_print(datum.ptr_, datum.len_, buffer, length, pos))) {
      } else { ret = databuff_printf(buffer, length, pos, "\""); }
      break;
    case ObTinyTextType: case ObTextType: case ObMediumTextType: case ObLongTextType:
    case ObGeometryType: {
      const ObString text = text_print_data(value, length - pos);
      ret = databuff_print_obj(buffer, length, pos, text);
      if (OB_SUCC(ret)) { ret = print_debug_collation(value, buffer, length, pos); }
      break;
    }
    case ObJsonType:
      ret = print_json_text(value, buffer, length, pos, params); break;
    case ObExtendType: {
      const int64_t marker = value.get_ext();
      const char *text = nullptr;
      if (marker == ObActionFlag::OP_MIN_OBJ || marker == DATUM_MIN_EXT_VALUE) { text = "__OB__MIN__"; }
      else if (marker == ObActionFlag::OP_MAX_OBJ || marker == DATUM_MAX_EXT_VALUE) { text = "__OB__MAX__"; }
      else if (marker == ObActionFlag::OP_NOP) { text = "__OB__NOP__"; }
      else if (marker == ObActionFlag::OP_DEFAULT_NOW_FLAG) { text = N_UPPERCASE_CUR_TIMESTAMP; }
      if (OB_FAIL(databuff_printf(buffer, length, pos, "\""))) {
      } else if (text != nullptr) { ret = databuff_printf(buffer, length, pos, "%s", text); }
      else { ret = databuff_printf(buffer, length, pos, "%" PRId64, marker); }
      if (OB_SUCC(ret)) { ret = databuff_printf(buffer, length, pos, "\""); }
      break;
    }
    case ObDateTimeType: case ObMySQLDateTimeType: case ObTimestampType:
    case ObDateType: case ObMySQLDateType: case ObTimeType: case ObYearType: {
      ret = databuff_printf(buffer, length, pos, "\"");
      if (OB_SUCC(ret)) {
        const ObTimeZoneInfo *zone = type == ObTimestampType ? params.tz_info_ : nullptr;
        switch (type) {
          case ObDateTimeType: case ObTimestampType:
            ret = ObTimeConverter::datetime_to_str(datum.get_datetime(), zone,
                OB_MAX_DATETIME_PRECISION, buffer, length, pos); break;
          case ObMySQLDateTimeType:
            ret = ObTimeConverter::mdatetime_to_str(datum.get_mysql_datetime(), nullptr,
                OB_MAX_DATETIME_PRECISION, buffer, length, pos); break;
          case ObDateType: ret = ObTimeConverter::date_to_str(datum.get_date(), buffer, length, pos); break;
          case ObMySQLDateType: ret = ObTimeConverter::mdate_to_str(datum.get_mysql_date(), buffer, length, pos); break;
          case ObTimeType: ret = ObTimeConverter::time_to_str(datum.get_time(), value.get_scale(), buffer, length, pos); break;
          case ObYearType: ret = ObTimeConverter::year_to_str(datum.get_year(), buffer, length, pos); break;
          default: ret = OB_ERR_UNEXPECTED; break;
        }
      }
      if (OB_SUCC(ret)) { ret = databuff_printf(buffer, length, pos, "\""); }
      break;
    }
    case ObNumberType: case ObUNumberType: {
      const number::ObNumber number = datum.get_number();
      if (OB_FAIL(databuff_printf(buffer, length, pos, "\""))) {
      } else if (OB_FAIL(number.format(buffer, length, pos, value.get_scale()))) {
      } else { ret = databuff_printf(buffer, length, pos, "\""); }
      break;
    }
    case ObTinyIntType: case ObSmallIntType: case ObMediumIntType: case ObInt32Type: case ObIntType:
      ret = databuff_printf(buffer, length, pos, "%" PRId64, datum.get_int()); break;
    case ObUTinyIntType: case ObUSmallIntType: case ObUMediumIntType: case ObUInt32Type: case ObUInt64Type:
      ret = databuff_printf(buffer, length, pos, "%" PRIu64, datum.get_uint()); break;
    case ObDecimalIntType:
      ret = wide::to_string(datum.get_decimal_int(), datum.get_int_bytes(), value.get_scale(), buffer, length, pos); break;
    case ObUnknownType:
      ret = databuff_printf(buffer, length, pos, "%" PRId64, datum.get_int()); break;
    case ObFloatType: case ObUFloatType:
      ret = databuff_printf(buffer, length, pos, "%f", static_cast<double>(datum.get_float())); break;
    case ObDoubleType: case ObUDoubleType:
      ret = databuff_printf(buffer, length, pos, "%f", datum.get_double()); break;
    case ObBitType:
      ret = databuff_printf(buffer, length, pos, "%" PRIu64, datum.get_uint()); break;
    case ObEnumType: case ObSetType:
      ret = databuff_printf(buffer, length, pos, "%" PRIu64, datum.get_uint()); break;
    default:
      ret = print_datum_sql_literal(value, buffer, length, pos, params); break;
  }
  return ret;
}
} // namespace
int print_datum_debug_json(const DatumValue &value, char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params)
{
  int ret = OB_SUCCESS;
  if (buffer == nullptr || length < 0 || pos < 0 || pos > length || value.is_invalid_type()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(print_debug_prefix(value, buffer, length, pos))) {
  } else if (OB_FAIL(print_debug_payload(value, buffer, length, pos, params))) {
  } else if (OB_FAIL(databuff_printf(buffer, length, pos, "}"))) {
  }
  return ret;
}
int print_datum_debug_smart(const DatumValue &value, char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params)
{
  int ret = OB_SUCCESS;
  bool printable = true;
  const SqlValueType type = value.get_type();
  const bool string_or_lob = ob_is_string_type(type) && type != ObHexStringType
      && type != ObJsonType && type != ObGeometryType;
  if (string_or_lob && !value.is_null()) {
    const ObString str = value.get_string();
    ret = is_printable(str.ptr(), str.length(), printable);
  }
  if (OB_SUCC(ret) && string_or_lob && !printable) {
    ret = print_debug_prefix(value, buffer, length, pos);
    if (OB_SUCC(ret)) {
      ret = print_hex(value.get_string(), buffer, length, pos);
    }
    if (OB_SUCC(ret)) { ret = print_debug_collation(value, buffer, length, pos); }
    if (OB_SUCC(ret)) { ret = databuff_printf(buffer, length, pos, "}"); }
  } else if (OB_SUCC(ret)) {
    ret = print_datum_debug_json(value, buffer, length, pos, params);
  }
  return ret;
}
int DatumValue::print_plain_str_literal(char *buffer, int64_t length, int64_t &pos) const
{ return print_datum_plain_str_literal(*this, buffer, length, pos); }
int DatumValue::print_plain_str_literal(char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params) const
{ return print_datum_plain_str_literal(*this, buffer, length, pos, params); }
int DatumValue::print_varchar_literal(char *buffer, int64_t length, int64_t &pos) const
{ return print_datum_varchar_literal(*this, buffer, length, pos); }
int DatumValue::print_varchar_literal(char *buffer, int64_t length, int64_t &pos,
    const ObValuePrintParams &params) const
{ return print_datum_varchar_literal(*this, buffer, length, pos, params); }
int DatumValue::print_plain_str_literal(const ObIArray<ObString> &type_infos,
    char *buffer, int64_t length, int64_t &pos) const
{ return print_enum_set_text(*this, type_infos, buffer, length, pos, false); }
int DatumValue::print_varchar_literal(const ObIArray<ObString> &type_infos,
    char *buffer, int64_t length, int64_t &pos) const
{ return print_enum_set_text(*this, type_infos, buffer, length, pos, true); }
} }
