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

#include "query/protocol/ob_mysql_protocol_util.h"

#include <algorithm>
#include <cstring>

#include "common/ob_field.h"
#include "common/json_type/ob_json_bin.h"
#include "common/timezone/ob_time_convert.h"
#include "lib/allocator/ob_allocator.h"
#include "lib/allocator/page_arena.h"
#include "lib/geometry/ob_geo_wkb_define.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;
using namespace oceanbase::share::schema;

struct ObMySQLTypeMap
{
  /* oceanbase::common::ObObjType ob_type; */
  EMySQLFieldType mysql_type;
  uint16_t flags;         /* flags if Field */
  uint64_t length;        /* other than varchar type */
};

// @todo
// reference: https://dev.mysql.com/doc/refman/5.6/en/c-api-data-structures.html
// reference: http://dev.mysql.com/doc/internals/en/client-server-protocol.html
static const ObMySQLTypeMap type_maps_[ObMaxType] =
{
  /* ObMinType */
  {EMySQLFieldType::MYSQL_TYPE_NULL,      BINARY_FLAG, 0},                        /* ObNullType */
  {EMySQLFieldType::MYSQL_TYPE_TINY,      0, 0},                                  /* ObTinyIntType */
  {EMySQLFieldType::MYSQL_TYPE_SHORT,     0, 0},                                  /* ObSmallIntType */
  {EMySQLFieldType::MYSQL_TYPE_INT24,     0, 0},                                  /* ObMediumIntType */
  {EMySQLFieldType::MYSQL_TYPE_LONG,      0, 0},                                  /* ObInt32Type */
  {EMySQLFieldType::MYSQL_TYPE_LONGLONG,  0, 0},                                  /* ObIntType */
  {EMySQLFieldType::MYSQL_TYPE_TINY,      UNSIGNED_FLAG, 0},                      /* ObUTinyIntType */
  {EMySQLFieldType::MYSQL_TYPE_SHORT,     UNSIGNED_FLAG, 0},                      /* ObUSmallIntType */
  {EMySQLFieldType::MYSQL_TYPE_INT24,     UNSIGNED_FLAG, 0},                      /* ObUMediumIntType */
  {EMySQLFieldType::MYSQL_TYPE_LONG,      UNSIGNED_FLAG, 0},                      /* ObUInt32Type */
  {EMySQLFieldType::MYSQL_TYPE_LONGLONG,  UNSIGNED_FLAG, 0},                      /* ObUInt64Type */
  {EMySQLFieldType::MYSQL_TYPE_FLOAT,     0, 0},                                  /* ObFloatType */
  {EMySQLFieldType::MYSQL_TYPE_DOUBLE,    0, 0},                                  /* ObDoubleType */
  {EMySQLFieldType::MYSQL_TYPE_FLOAT,     UNSIGNED_FLAG, 0},                      /* ObUFloatType */
  {EMySQLFieldType::MYSQL_TYPE_DOUBLE,    UNSIGNED_FLAG, 0},                      /* ObUDoubleType */
  {EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,0, 0},                                  /* ObNumberType */
  {EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,UNSIGNED_FLAG, 0},                      /* ObUNumberType */
  {EMySQLFieldType::MYSQL_TYPE_DATETIME,  BINARY_FLAG, 0},                        /* ObDateTimeType */
  {EMySQLFieldType::MYSQL_TYPE_TIMESTAMP, BINARY_FLAG | TIMESTAMP_FLAG, 0},       /* ObTimestampType */
  {EMySQLFieldType::MYSQL_TYPE_DATE,      BINARY_FLAG, 0},                        /* ObDateType */
  {EMySQLFieldType::MYSQL_TYPE_TIME,      BINARY_FLAG, 0},                        /* ObTimeType */
  {EMySQLFieldType::MYSQL_TYPE_YEAR,      UNSIGNED_FLAG | ZEROFILL_FLAG, 0},      /* ObYearType */
  {EMySQLFieldType::MYSQL_TYPE_VAR_STRING,   0, 0},                               /* ObVarcharType */
  {EMySQLFieldType::MYSQL_TYPE_STRING,       0, 0},                               /* ObCharType */
  {EMySQLFieldType::MYSQL_TYPE_VAR_STRING,   BINARY_FLAG, 0},                     /* ObHexStringType */
  {EMySQLFieldType::MYSQL_TYPE_COMPLEX,      0, 0},                               /* ObExtendType */
  {EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,  0, 0},                               /* ObUnknownType */
  {EMySQLFieldType::MYSQL_TYPE_TINY_BLOB,    BLOB_FLAG, 0},                       /* ObTinyTextType */
  {EMySQLFieldType::MYSQL_TYPE_BLOB,         BLOB_FLAG, 0},                       /* ObTextType */
  {EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB,  BLOB_FLAG, 0},                       /* ObMediumTextType */
  {EMySQLFieldType::MYSQL_TYPE_LONG_BLOB,    BLOB_FLAG, 0},                       /* ObLongTextType */
  {EMySQLFieldType::MYSQL_TYPE_BIT,          UNSIGNED_FLAG, 0},                   /* ObBitType */
  {EMySQLFieldType::MYSQL_TYPE_STRING,       ENUM_FLAG, 0},                       /* ObEnumType */
  {EMySQLFieldType::MYSQL_TYPE_STRING,       SET_FLAG, 0},                        /* ObSetType */
  {EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,  0, 0},                               /* ObEnumInnerType */
  {EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,  0, 0},                               /* ObSetInnerType */
  {EMySQLFieldType::MYSQL_TYPE_JSON,       BLOB_FLAG | BINARY_FLAG, 0}, /* ObJsonType */
  {EMySQLFieldType::MYSQL_TYPE_GEOMETRY,   BLOB_FLAG | BINARY_FLAG, 0}, /* ObGeometryType */
  {EMySQLFieldType::MYSQL_TYPE_COMPLEX,   0, 0}, /* ObUserDefinedSQLType */
  {EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL, 0, 0},                           /* ObDecimalIntType */
  {EMySQLFieldType::MYSQL_TYPE_STRING,     0, 0},   /* ObCollectionSQLType, will cast to string */
  {EMySQLFieldType::MYSQL_TYPE_DATE,      BINARY_FLAG, 0}, /* ObMySQLDateType */
  {EMySQLFieldType::MYSQL_TYPE_DATETIME,  BINARY_FLAG, 0}, /* ObMySQLDateTimeType */
  /* ObMaxType */
};

static_assert(sizeof(type_maps_) / sizeof(ObMySQLTypeMap) == ObMaxType, "Not enough initializer");

namespace {

int set_borrowed_lenenc_bytes(const ObString &value, ObMySQLCellValue &out) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(value.length() < 0 ||
                  (value.length() > 0 && NULL == value.ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    OB_LOG(WARN, "invalid borrowed cell bytes", K(ret), K(value.length()),
           KP(value.ptr()));
  } else {
    out.set_borrowed_bytes(ObMySQLCellValueKind::LENENC_BYTES, value.ptr(),
                           value.length());
  }
  return ret;
}

int set_copied_bytes(ObIAllocator &allocator, const ObMySQLCellValueKind kind,
                     const char *data, const int64_t len,
                     ObMySQLCellValue &out) {
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(len < 0 || (len > 0 && NULL == data))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (len <= ObMySQLCellValue::LOCAL_BUFFER_SIZE) {
    if (len > 0) {
      MEMCPY(out.get_local_buffer(), data, len);
    }
    ret = out.set_local_bytes(kind, static_cast<int32_t>(len));
  } else {
    char *buf = static_cast<char *>(allocator.alloc(len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "allocate mysql cell scratch failed", K(ret), K(len));
    } else {
      MEMCPY(buf, data, len);
      out.set_borrowed_bytes(kind, buf, len);
    }
  }
  return ret;
}

int set_zerofill_bytes(ObIAllocator &allocator, const char *data,
                       const int64_t data_len, const bool zerofill,
                       const int32_t zflength, ObMySQLCellValue &out) {
  int ret = OB_SUCCESS;
  const int64_t zero_count =
      zerofill && zflength > data_len ? zflength - data_len : 0;
  const int64_t total_len = data_len + zero_count;
  if (OB_UNLIKELY(data_len < 0 || (data_len > 0 && NULL == data))) {
    ret = OB_INVALID_ARGUMENT;
  } else if (total_len <= ObMySQLCellValue::LOCAL_BUFFER_SIZE) {
    char *buf = out.get_local_buffer();
    if (data == buf && zero_count > 0) {
      memmove(buf + zero_count, buf, data_len);
    } else if (data_len > 0 && data != buf) {
      MEMCPY(buf + zero_count, data, data_len);
    }
    if (zero_count > 0) {
      MEMSET(buf, '0', zero_count);
    }
    ret = out.set_local_bytes(ObMySQLCellValueKind::LENENC_BYTES,
                              static_cast<int32_t>(total_len));
  } else {
    char *buf = static_cast<char *>(allocator.alloc(total_len));
    if (OB_ISNULL(buf)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      OB_LOG(WARN, "allocate zerofill cell scratch failed", K(ret),
             K(total_len));
    } else {
      if (zero_count > 0) {
        MEMSET(buf, '0', zero_count);
      }
      if (data_len > 0) {
        MEMCPY(buf + zero_count, data, data_len);
      }
      out.set_borrowed_bytes(ObMySQLCellValueKind::LENENC_BYTES, buf,
                             total_len);
    }
  }
  return ret;
}

void set_date_time_parts(const ObTime &ob_time, const ObMySQLCellValueKind kind,
                         ObMySQLCellValue &out) {
  out.kind_ = kind;
  out.year_ = static_cast<uint16_t>(ob_time.parts_[DT_YEAR]);
  out.month_ = static_cast<uint8_t>(ob_time.parts_[DT_MON]);
  out.day_ = static_cast<uint8_t>(ob_time.parts_[DT_MDAY]);
  out.hour_ = static_cast<uint8_t>(ob_time.parts_[DT_HOUR]);
  out.minute_ = static_cast<uint8_t>(ob_time.parts_[DT_MIN]);
  out.second_ = static_cast<uint8_t>(ob_time.parts_[DT_SEC]);
  out.microseconds_ = static_cast<uint32_t>(ob_time.parts_[DT_USEC]);
}

int finish_local_text(const int ret, const int64_t pos, ObMySQLCellValue &out) {
  return OB_SUCCESS == ret
             ? out.set_local_bytes(ObMySQLCellValueKind::LENENC_BYTES,
                                   static_cast<int32_t>(pos))
             : ret;
}

} // namespace

int ObSMUtils::build_cell_value(const ObObj &obj, MYSQL_PROTOCOL_TYPE type,
                                ObIAllocator &scratch_allocator,
                                ObMySQLCellValue &out,
                                const ObDataTypeCastParams &dtc_params,
                                const ObField *field,
                                const sql::ObSQLSessionInfo &session,
                                ObSchemaGetterGuard *schema_guard) {
  int ret = OB_SUCCESS;
  ObScale scale = 0;
  ObPrecision precision = 0;
  bool zerofill = false;
  int32_t zflength = 0;
  UNUSED(session);
  UNUSED(schema_guard);
  out.reset();

  if (NULL == field) {
    if (OB_UNLIKELY(obj.is_invalid_type())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      scale = ObAccuracy::DML_DEFAULT_ACCURACY[obj.get_type()].get_scale();
    }
  } else {
    scale = field->accuracy_.get_scale();
    precision = field->accuracy_.get_precision();
    zerofill = field->flags_ & ZEROFILL_FLAG;
    zflength = field->length_;
  }

  if (OB_SUCC(ret)) {
    switch (obj.get_type_class()) {
    case ObNullTC: {
      out.kind_ = ObMySQLCellValueKind::NULL_VALUE;
      break;
    }
    case ObIntTC:
    case ObUIntTC: {
      const bool is_unsigned = ObUIntTC == obj.get_type_class();
      const int64_t value = obj.get_int();
      if (TEXT == type) {
        ObFastFormatInt ffi(value, is_unsigned);
        ret = set_zerofill_bytes(scratch_allocator, ffi.ptr(), ffi.length(),
                                 zerofill, zflength, out);
      } else {
        out.value_ = static_cast<uint64_t>(value);
        switch (obj.get_type()) {
        case ObTinyIntType:
        case ObUTinyIntType:
          out.kind_ = ObMySQLCellValueKind::I8;
          break;
        case ObSmallIntType:
        case ObUSmallIntType:
          out.kind_ = ObMySQLCellValueKind::I16;
          break;
        case ObMediumIntType:
        case ObUMediumIntType:
        case ObInt32Type:
        case ObUInt32Type:
          out.kind_ = ObMySQLCellValueKind::I32;
          break;
        case ObIntType:
        case ObUInt64Type:
          out.kind_ = ObMySQLCellValueKind::I64;
          break;
        default:
          ret = OB_INVALID_ARGUMENT;
          OB_LOG(WARN, "invalid integer object type", K(ret),
                 K(obj.get_type()));
          break;
        }
      }
      break;
    }
    case ObFloatTC: {
      const float value = obj.get_float();
      if (BINARY == type) {
        uint32_t bits = 0;
        MEMCPY(&bits, &value, sizeof(bits));
        out.kind_ = ObMySQLCellValueKind::F32;
        out.value_ = bits;
      } else {
        char tmp[FLOAT_TO_STRING_CONVERSION_BUFFER_SIZE];
        int64_t length = 0;
        if (scale >= 0) {
          length = ob_fcvt(value, scale, sizeof(tmp) - 1, tmp, NULL);
        } else {
          length = ob_gcvt_opt(value, OB_GCVT_ARG_FLOAT, sizeof(tmp) - 1, tmp,
                               NULL, TRUE);
        }
        if (OB_UNLIKELY(length >= 251)) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          ret = set_zerofill_bytes(scratch_allocator, tmp, length, zerofill,
                                   zflength, out);
        }
      }
      break;
    }
    case ObDoubleTC: {
      const double value = obj.get_double();
      if (BINARY == type) {
        uint64_t bits = 0;
        MEMCPY(&bits, &value, sizeof(bits));
        out.kind_ = ObMySQLCellValueKind::F64;
        out.value_ = bits;
      } else {
        char tmp[DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE];
        int64_t length = 0;
        if (scale >= 0) {
          length = ob_fcvt(value, scale, sizeof(tmp) - 1, tmp, NULL);
        } else {
          length = ob_gcvt_opt(value, OB_GCVT_ARG_DOUBLE, sizeof(tmp) - 1, tmp,
                               NULL, TRUE);
        }
        if (OB_UNLIKELY(length > static_cast<int64_t>(sizeof(tmp)))) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          ret = set_zerofill_bytes(scratch_allocator, tmp, length, zerofill,
                                   zflength, out);
        }
      }
      break;
    }
    case ObNumberTC: {
      char *buf = out.get_local_buffer();
      int64_t capacity = ObMySQLCellValue::LOCAL_BUFFER_SIZE;
      int64_t length = 0;
      ret = obj.get_number().format(buf, capacity, length, scale);
      if (OB_SIZE_OVERFLOW == ret) {
        const int64_t scale_width =
            scale >= 0 ? scale : -static_cast<int64_t>(scale);
        capacity = std::max<int64_t>(obj.get_number().get_max_format_length() +
                                         scale_width + 4,
                                     zerofill && zflength > 0 ? zflength : 0);
        capacity = std::max<int64_t>(capacity, 128);
        if (OB_ISNULL(
                buf = static_cast<char *>(scratch_allocator.alloc(capacity)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          length = 0;
          ret = obj.get_number().format(buf, capacity, length, scale);
        }
      }
      if (OB_SUCC(ret)) {
        const int64_t total_len =
            zerofill && zflength > length ? zflength : length;
        if (buf == out.get_local_buffer() || total_len > capacity) {
          ret = set_zerofill_bytes(scratch_allocator, buf, length, zerofill,
                                   zflength, out);
        } else {
          const int64_t zero_count = total_len - length;
          if (zero_count > 0) {
            memmove(buf + zero_count, buf, length);
            MEMSET(buf, '0', zero_count);
          }
          out.set_borrowed_bytes(ObMySQLCellValueKind::LENENC_BYTES, buf,
                                 total_len);
        }
      }
      break;
    }
    case ObDateTimeTC: {
      if (BINARY == type) {
        ObTime ob_time(DT_TYPE_DATETIME);
        if (OB_FAIL(ObTimeConverter::datetime_to_ob_time(
                obj.get_datetime(),
                obj.is_timestamp() ? dtc_params.tz_info_ : NULL, ob_time))) {
        } else if (OB_UNLIKELY(!HAS_TYPE_DATE(ob_time.mode_))) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          set_date_time_parts(ob_time, ObMySQLCellValueKind::DATETIME, out);
        }
      } else {
        int64_t pos = 0;
        ret = ObTimeConverter::datetime_to_str(
            obj.get_datetime(), obj.is_timestamp() ? dtc_params.tz_info_ : NULL,
            scale, out.get_local_buffer(),
            ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    case ObDateTC: {
      if (BINARY == type) {
        ObTime ob_time(DT_TYPE_DATE);
        if (OB_FAIL(
                ObTimeConverter::date_to_ob_time(obj.get_date(), ob_time))) {
        } else if (OB_UNLIKELY(!HAS_TYPE_DATE(ob_time.mode_))) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          set_date_time_parts(ob_time, ObMySQLCellValueKind::DATE, out);
        }
      } else {
        int64_t pos = 0;
        ret = ObTimeConverter::date_to_str(
            obj.get_date(), out.get_local_buffer(),
            ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    case ObTimeTC: {
      if (BINARY == type) {
        ObTime ob_time(DT_TYPE_TIME);
        if (OB_FAIL(
                ObTimeConverter::time_to_ob_time(obj.get_time(), ob_time))) {
        } else {
          out.kind_ = ObMySQLCellValueKind::TIME;
          out.days_ = static_cast<uint32_t>(ob_time.parts_[DT_DATE] +
                                            ob_time.parts_[DT_HOUR] / 24);
          out.hour_ = static_cast<uint8_t>(ob_time.parts_[DT_HOUR] % 24);
          out.minute_ = static_cast<uint8_t>(ob_time.parts_[DT_MIN]);
          out.second_ = static_cast<uint8_t>(ob_time.parts_[DT_SEC]);
          out.microseconds_ = static_cast<uint32_t>(ob_time.parts_[DT_USEC]);
          out.is_negative_ = (DT_MODE_NEG & ob_time.mode_) ? 1 : 0;
        }
      } else {
        int64_t pos = 0;
        ret = ObTimeConverter::time_to_str(
            obj.get_time(), scale, out.get_local_buffer(),
            ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    case ObYearTC: {
      if (BINARY == type) {
        int64_t year = 0;
        if (OB_FAIL(ObTimeConverter::year_to_int(obj.get_year(), year))) {
        } else {
          out.kind_ = ObMySQLCellValueKind::YEAR;
          out.year_ = static_cast<uint16_t>(year);
        }
      } else {
        int64_t pos = 0;
        ret = ObTimeConverter::year_to_str(
            obj.get_year(), out.get_local_buffer(),
            ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    case ObOTimestampTC: {
      const ObOTimestampData &value = obj.get_otimestamp_value();
      int64_t pos = 0;
      if (!value.is_null_value()) {
        ret = ObTimeConverter::encode_otimestamp(
            obj.get_type(), out.get_local_buffer(),
            ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos, dtc_params.tz_info_,
            value, static_cast<int8_t>(scale));
      }
      ret = finish_local_text(ret, pos, out);
      break;
    }
    case ObRawTC:
    case ObTextTC:
    case ObStringTC:
    case ObLobTC: {
      ret = set_borrowed_lenenc_bytes(obj.get_string(), out);
      break;
    }
    case ObJsonTC: {
      const ObString &value = obj.get_string();
      if (0 == value.length()) {
        out.kind_ = ObMySQLCellValueKind::LEGACY_LENENC_NULL;
      } else if (OB_UNLIKELY(value.length() < 0 || NULL == value.ptr())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        lib::ObMemAttr mem_attr("JsonAlloc");
        ObArenaAllocator allocator(mem_attr);
        ObJsonBin json_bin(value.ptr(), value.length(), &allocator);
        ObIJsonBase *json_base = &json_bin;
        ObJsonBuffer json_buf(&allocator);
        json_bin.set_seek_flag(true);
        if (OB_FAIL(json_bin.reset_iter())) {
        } else if (OB_FAIL(json_base->print(json_buf, true, value.length()))) {
        } else {
          ret = set_copied_bytes(scratch_allocator,
                                 ObMySQLCellValueKind::LENENC_BYTES,
                                 json_buf.ptr(), json_buf.length(), out);
        }
      }
      break;
    }
    case ObGeometryTC: {
      const ObString &value = obj.get_string();
      const int64_t length = value.length();
      if (OB_UNLIKELY(length < 0 || (length > 0 && NULL == value.ptr()))) {
        ret = OB_ERR_UNEXPECTED;
      } else if (length < WKB_DATA_OFFSET + WKB_GEO_TYPE_SIZE) {
        ret = set_borrowed_lenenc_bytes(value, out);
      } else {
        const uint8_t version = *(
            reinterpret_cast<const uint8_t *>(value.ptr() + WKB_GEO_SRID_SIZE));
        if (!IS_GEO_VERSION(version)) {
          ret = set_borrowed_lenenc_bytes(value, out);
        } else {
          const int64_t new_length = length - WKB_VERSION_SIZE;
          char *buf = NULL;
          if (new_length <= ObMySQLCellValue::LOCAL_BUFFER_SIZE) {
            buf = out.get_local_buffer();
          } else if (OB_ISNULL(buf = static_cast<char *>(
                                   scratch_allocator.alloc(new_length)))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
          }
          if (OB_SUCC(ret)) {
            MEMCPY(buf, value.ptr(), WKB_GEO_SRID_SIZE);
            MEMCPY(buf + WKB_GEO_SRID_SIZE,
                   value.ptr() + WKB_GEO_SRID_SIZE + WKB_VERSION_SIZE,
                   new_length - WKB_GEO_SRID_SIZE);
            if (buf == out.get_local_buffer()) {
              ret = out.set_local_bytes(ObMySQLCellValueKind::LENENC_BYTES,
                                        static_cast<int32_t>(new_length));
            } else {
              out.set_borrowed_bytes(ObMySQLCellValueKind::LENENC_BYTES, buf,
                                     new_length);
            }
          }
        }
      }
      break;
    }
    case ObBitTC: {
      int32_t bit_len = 0;
      if (OB_LIKELY(precision > 0)) {
        bit_len = precision;
      } else {
        bit_len = ObAccuracy::MAX_ACCURACY[obj.get_type()].precision_;
        _OB_LOG(WARN, "max precision is used. origin precision is %d",
                precision);
      }
      if (OB_UNLIKELY(bit_len <= 0 || bit_len > OB_MAX_BIT_LENGTH)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (BINARY == type) {
        out.kind_ = ObMySQLCellValueKind::BIT;
        out.value_ = obj.get_bit();
        out.bit_len_ = bit_len;
      } else {
        int64_t pos = 0;
        ret = bit_to_char_array(obj.get_bit(), bit_len, out.get_local_buffer(),
                                ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    case ObUserDefinedSQLTC: {
      const ObString &value = obj.get_string();
      if (0 == obj.get_udt_subschema_id()) {
        if (0 == value.length()) {
          out.kind_ = ObMySQLCellValueKind::LEGACY_LENENC_NULL;
        } else {
          ret = set_borrowed_lenenc_bytes(value, out);
        }
      } else if (TEXT == type) {
        ret = set_borrowed_lenenc_bytes(value, out);
      } else {
        ret = OB_NOT_IMPLEMENT;
        OB_LOG(WARN, "UDTSQLType binary protocol not implemented", K(ret));
      }
      break;
    }
    case ObCollectionSQLTC: {
      ret = set_borrowed_lenenc_bytes(obj.get_string(), out);
      break;
    }
    case ObDecimalIntTC: {
      char *buf = out.get_local_buffer();
      int64_t capacity = ObMySQLCellValue::LOCAL_BUFFER_SIZE;
      int64_t length = 0;
      ret = wide::to_string(obj.get_decimal_int(), obj.get_int_bytes(),
                            obj.get_scale(), buf, capacity, length);
      if (OB_SIZE_OVERFLOW == ret) {
        const int64_t scale_width =
            obj.get_scale() >= 0 ? obj.get_scale()
                                 : -static_cast<int64_t>(obj.get_scale());
        capacity =
            static_cast<int64_t>(obj.get_int_bytes()) * 3 + scale_width + 4;
        capacity = std::max<int64_t>(capacity,
                                     zerofill && zflength > 0 ? zflength : 0);
        capacity = std::max<int64_t>(capacity, 128);
        if (OB_ISNULL(
                buf = static_cast<char *>(scratch_allocator.alloc(capacity)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          length = 0;
          ret = wide::to_string(obj.get_decimal_int(), obj.get_int_bytes(),
                                obj.get_scale(), buf, capacity, length);
        }
      }
      if (OB_SUCC(ret)) {
        const int64_t total_len =
            zerofill && zflength > length ? zflength : length;
        if (buf == out.get_local_buffer() || total_len > capacity) {
          ret = set_zerofill_bytes(scratch_allocator, buf, length, zerofill,
                                   zflength, out);
        } else {
          const int64_t zero_count = total_len - length;
          if (zero_count > 0) {
            memmove(buf + zero_count, buf, length);
            MEMSET(buf, '0', zero_count);
          }
          out.set_borrowed_bytes(ObMySQLCellValueKind::LENENC_BYTES, buf,
                                 total_len);
        }
      }
      break;
    }
    case ObMySQLDateTC: {
      if (BINARY == type) {
        ObTime ob_time(DT_TYPE_MYSQL_DATE);
        if (OB_FAIL(ObTimeConverter::mdate_to_ob_time(obj.get_mysql_date(),
                                                      ob_time))) {
        } else if (OB_UNLIKELY(!HAS_TYPE_DATE(ob_time.mode_))) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          set_date_time_parts(ob_time, ObMySQLCellValueKind::DATE, out);
        }
      } else {
        int64_t pos = 0;
        ret = ObTimeConverter::mdate_to_str(
            obj.get_mysql_date(), out.get_local_buffer(),
            ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    case ObMySQLDateTimeTC: {
      if (BINARY == type) {
        ObTime ob_time(DT_TYPE_MYSQL_DATETIME);
        if (OB_FAIL(ObTimeConverter::mdatetime_to_ob_time(
                obj.get_mysql_datetime(), ob_time))) {
        } else if (OB_UNLIKELY(!HAS_TYPE_DATE(ob_time.mode_))) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          set_date_time_parts(ob_time, ObMySQLCellValueKind::DATETIME, out);
        }
      } else {
        int64_t pos = 0;
        ret = ObTimeConverter::mdatetime_to_str(
            obj.get_mysql_datetime(), NULL, scale,
            out.get_local_buffer(), ObMySQLCellValue::LOCAL_BUFFER_SIZE, pos);
        ret = finish_local_text(ret, pos, out);
      }
      break;
    }
    default: {
      _OB_LOG(ERROR, "invalid ob type=%d", obj.get_type());
      ret = OB_ERROR;
      break;
    }
    }
  }
  return ret;
}

// called by handle COM_STMT_EXECUTE offset is 0
bool ObSMUtils::update_from_bitmap(ObObj &param, const char *bitmap,
                                   int64_t field_index) {
  bool ret = false;
  if (update_from_bitmap(bitmap, field_index)) {
    param.set_null();
    ret = true;
  }
  return ret;
}

bool ObSMUtils::update_from_bitmap(const char *bitmap, int64_t field_index) {
  bool ret = false;
  int byte_pos = static_cast<int>(field_index / 8);
  int bit_pos = static_cast<int>(field_index % 8);
  if (NULL != bitmap) {
    char value = bitmap[byte_pos];
    if (value & (1 << bit_pos)) {
      ret = true;
    }
  }
  return ret;
}

int get_map(ObObjType ob_type, const ObMySQLTypeMap *&map)
{
  int ret = OB_SUCCESS;
  if (ob_type >= ObMaxType) {
    ret = OB_OBJ_TYPE_ERROR;
  }

  if (OB_SUCC(ret)) {
    map = type_maps_ + ob_type;
  }

  return ret;
}

int ObSMUtils::get_type_length(ObObjType ob_type, int64_t &length)
{
  const ObMySQLTypeMap *map = NULL;
  int ret = OB_SUCCESS;

  if ((ret = get_map(ob_type, map)) == OB_SUCCESS) {
    length = map->length;
  }
  return ret;
}

int ObSMUtils::get_mysql_type(ObObjType ob_type, EMySQLFieldType &mysql_type,
                              uint16_t &flags, ObScale &num_decimals)
{
  const ObMySQLTypeMap *map = NULL;
  int ret = OB_SUCCESS;

  if ((ret = get_map(ob_type, map)) == OB_SUCCESS) {
    mysql_type = map->mysql_type;
    flags |= map->flags;
    // batch fixup num_decimal values
    // so as to be compatible with mysql metainfo
    switch (mysql_type) {
      case EMySQLFieldType::MYSQL_TYPE_LONGLONG:
      case EMySQLFieldType::MYSQL_TYPE_LONG:
      case EMySQLFieldType::MYSQL_TYPE_INT24:
      case EMySQLFieldType::MYSQL_TYPE_SHORT:
      case EMySQLFieldType::MYSQL_TYPE_TINY:
      case EMySQLFieldType::MYSQL_TYPE_NULL:
      case EMySQLFieldType::MYSQL_TYPE_DATE:
      case EMySQLFieldType::MYSQL_TYPE_YEAR:
      case EMySQLFieldType::MYSQL_TYPE_BIT:
      case EMySQLFieldType::MYSQL_TYPE_JSON: // mysql json and long text decimals are 0, we do not need it?
      case EMySQLFieldType::MYSQL_TYPE_GEOMETRY:
      case EMySQLFieldType::MYSQL_TYPE_ORA_XML:
        num_decimals = 0;
        break;

      case EMySQLFieldType::MYSQL_TYPE_TINY_BLOB:
      case EMySQLFieldType::MYSQL_TYPE_BLOB:
      case EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB:
      case EMySQLFieldType::MYSQL_TYPE_LONG_BLOB:
      case EMySQLFieldType::MYSQL_TYPE_VAR_STRING:
      case EMySQLFieldType::MYSQL_TYPE_STRING:
      case EMySQLFieldType::MYSQL_TYPE_OB_RAW:
      case EMySQLFieldType::MYSQL_TYPE_COMPLEX:
      case EMySQLFieldType::MYSQL_TYPE_ORA_BLOB:
      case EMySQLFieldType::MYSQL_TYPE_ORA_CLOB:
        // for compatible with MySQL, ugly convention.
        num_decimals = static_cast<ObScale>(0);
        break;
      case EMySQLFieldType::MYSQL_TYPE_OB_TIMESTAMP_WITH_TIME_ZONE:
      case EMySQLFieldType::MYSQL_TYPE_OB_TIMESTAMP_WITH_LOCAL_TIME_ZONE:
      case EMySQLFieldType::MYSQL_TYPE_OB_TIMESTAMP_NANO:
      case EMySQLFieldType::MYSQL_TYPE_TIMESTAMP:
      case EMySQLFieldType::MYSQL_TYPE_DATETIME:
      case EMySQLFieldType::MYSQL_TYPE_TIME:
      case EMySQLFieldType::MYSQL_TYPE_FLOAT:
      case EMySQLFieldType::MYSQL_TYPE_DOUBLE:
      case EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL:
        num_decimals = static_cast<ObScale>((num_decimals == -1)
            ? NOT_FIXED_DEC
            : num_decimals);
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        _OB_LOG(WARN, "unexpected mysql_type=%d", mysql_type);
        break;
    } // end switch
  }
  return ret;
}

int ObSMUtils::get_ob_type(ObObjType &ob_type, EMySQLFieldType mysql_type, const bool is_unsigned)
{
  int ret = OB_SUCCESS;
  switch (mysql_type) {
    case EMySQLFieldType::MYSQL_TYPE_NULL:
      ob_type = ObNullType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_TINY:
      ob_type = is_unsigned ? ObUTinyIntType : ObTinyIntType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_SHORT:
      ob_type = is_unsigned ? ObUSmallIntType : ObSmallIntType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_INT24:
      ob_type = is_unsigned ? ObUMediumIntType : ObMediumIntType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_LONG:
      ob_type = is_unsigned ? ObUInt32Type : ObInt32Type;
      break;
    case EMySQLFieldType::MYSQL_TYPE_LONGLONG:
      ob_type = is_unsigned ? ObUInt64Type : ObIntType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_FLOAT:
      ob_type = ObFloatType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_DOUBLE:
      ob_type = ObDoubleType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_TIMESTAMP:
      ob_type = ObTimestampType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_DATETIME:
      ob_type = ObDateTimeType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_TIME:
      ob_type = ObTimeType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_DATE:
      ob_type = ObDateType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_YEAR:
      ob_type = ObYearType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_VARCHAR:
    case EMySQLFieldType::MYSQL_TYPE_STRING:
    case EMySQLFieldType::MYSQL_TYPE_VAR_STRING:
      ob_type = ObVarcharType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_TINY_BLOB:
      ob_type = ObTinyTextType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_BLOB:
      ob_type = ObTextType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB:
      ob_type = ObMediumTextType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_LONG_BLOB:
      ob_type = ObLongTextType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL:
      ob_type = ObNumberType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_BIT:
      ob_type = ObBitType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_ENUM:
      ob_type = ObEnumType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_SET:
      ob_type = ObSetType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_COMPLEX:
      ob_type = ObExtendType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_JSON:
      ob_type = ObJsonType;
      break;
    case EMySQLFieldType::MYSQL_TYPE_GEOMETRY:
      ob_type = ObGeometryType;
      break;
    default:
      _OB_LOG(WARN, "unsupport MySQL type %d", mysql_type);
      ret = OB_OBJ_TYPE_ERROR;
  }
  return ret;
}
