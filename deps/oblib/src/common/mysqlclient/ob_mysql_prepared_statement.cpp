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

#define USING_LOG_PREFIX LIB_MYSQLC
#include "common/mysqlclient/ob_isql_connection_pool.h"
#include "lib/ob_check_macros.h"  // OZ/OV/CK(previously provided transitively by share)
#include "common/mysqlclient/ob_mysql_prepared_statement.h"

namespace oceanbase
{
namespace common
{
namespace sqlclient
{

static const int64_t IN_VALUE_ISNULL = 1;
static const int64_t OUT_VALUE_ISNULL = 2;

static const obmysql::EMySQLFieldType ob_type_to_mysql_type[ObMaxType] =
{
  /* ObMinType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NULL,          /* ObNullType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TINY,          /* ObTinyIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_SHORT,         /* ObSmallIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_INT24,         /* ObMediumIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONG,          /* ObInt32Type */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONGLONG,      /* ObIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TINY,          /* ObUTinyIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_SHORT,         /* ObUSmallIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_INT24,         /* ObUMediumIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONG,          /* ObUInt32Type */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONGLONG,      /* ObUInt64Type */
  obmysql::EMySQLFieldType::MYSQL_TYPE_FLOAT,         /* ObFloatType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DOUBLE,        /* ObDoubleType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_FLOAT,         /* ObUFloatType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DOUBLE,        /* ObUDoubleType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,    /* ObNumberType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,    /* ObUNumberType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DATETIME,      /* ObDateTimeType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TIMESTAMP,     /* ObTimestampType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_DATE,          /* ObDateType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TIME,          /* ObTimeType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_YEAR,          /* ObYearType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_VAR_STRING,    /* ObVarcharType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_STRING,        /* ObCharType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_VARCHAR,       /* ObHexStringType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,       /* ObExtendType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,   /* ObUnknownType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_TINY_BLOB,     /* ObTinyTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_BLOB,          /* ObTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB,   /* ObMediumTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_LONG_BLOB,     /* ObLongTextType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_BIT,           /* ObBitType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_STRING,        /* ObEnumType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_STRING,        /* ObSetType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,   /* ObEnumInnerType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,   /* ObSetInnerType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_JSON,                              /* ObJsonType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_GEOMETRY,                          /* ObGeometryType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,                           /* ObUserDefinedSQLType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,                        /* ObDecimalIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,                           /* ObCollectionSQLType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_BLOB,                              /* ObRoaringBitmapType */
  /* ObMaxType */
};

int ObBindParamEncode::encode_null(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  bind_param.is_null_ = 1;
  return ret;
}

int ObBindParamEncode::encode_int(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  bind_param.buffer_ = &(param.v_.int64_);
  bind_param.buffer_len_ = sizeof(param.v_.int64_);
  return ret;
}

int ObBindParamEncode::encode_uint(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  if (OB_FAIL(encode_int(col_idx, is_output_param, tz_info, param, bind_param, allocator, buffer_type))) {
    LOG_WARN("fail to encode", K(ret));
  } else {
    bind_param.is_unsigned_ = 1;
  }
  return ret;
}

int ObBindParamEncode::encode_float(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  bind_param.col_idx_ = col_idx;
  float *buf = NULL;
  OV (OB_NOT_NULL(buf = reinterpret_cast<float *>(allocator.alloc(sizeof(float)))), OB_ALLOCATE_MEMORY_FAILED);
  if (OB_SUCC(ret)) {
    *buf = param.v_.float_;
    bind_param.buffer_type_ = buffer_type;
    bind_param.buffer_ = buf;
    bind_param.buffer_len_ = sizeof(float);
  }
  return ret;
}

int ObBindParamEncode::encode_ufloat(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  if (OB_FAIL(encode_float(col_idx, is_output_param, tz_info, param, bind_param, allocator, buffer_type))) {
    LOG_WARN("fail to encode", K(ret));
  } else {
    bind_param.is_unsigned_ = 1;
  }
  return ret;
}

int ObBindParamEncode::encode_double(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  bind_param.col_idx_ = col_idx;
  double *buf = NULL;
  OV (OB_NOT_NULL(buf = reinterpret_cast<double *>(allocator.alloc(sizeof(double)))), OB_ALLOCATE_MEMORY_FAILED);
  if (OB_SUCC(ret)) {
    *buf = param.v_.double_;
    bind_param.buffer_type_ = buffer_type;
    bind_param.buffer_ = buf;
    bind_param.buffer_len_ = sizeof(double);
  }
  return ret;
}

int ObBindParamEncode::encode_udouble(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info, allocator);
  int ret = OB_SUCCESS;
  if (OB_FAIL(encode_double(col_idx, is_output_param, tz_info, param, bind_param, allocator, buffer_type))) {
    LOG_WARN("fail to encode", K(ret));
  } else {
    bind_param.is_unsigned_ = 1;
  }
  return ret;
}

int ObBindParamEncode::encode_number(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info);
  int ret = OB_SUCCESS;
  int64_t pos = 0;
  char *buf = nullptr;
  number::ObNumber num;
  const int64_t buf_len = OB_CAST_TO_VARCHAR_MAX_LENGTH;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  if (OB_ISNULL(buf = reinterpret_cast<char *>(allocator.alloc(buf_len)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret), K(buf_len));
  } else if (OB_FAIL(param.get_number(num))) {
    LOG_WARN("fail to get number", K(ret), K(param));
  } else if (OB_FAIL(num.format(buf, buf_len, pos, param.get_scale()))) {
    LOG_WARN("fail to convert number to string", K(ret));
  } else {
    bind_param.buffer_ = buf;
    bind_param.buffer_len_ = buf_len;
    bind_param.length_ = pos;
  }
  return ret;
}

int ObBindParamEncode::encode_unumber(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param, tz_info);
  int ret = OB_SUCCESS;
  if (OB_FAIL(encode_number(col_idx, is_output_param, tz_info, param, bind_param, allocator, buffer_type))) {
    LOG_WARN("fail to encode", K(ret));
  } else {
    bind_param.is_unsigned_ = 1;
  }
  return ret;
}

int ObBindParamEncode::encode_datetime(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param);
  int ret = OB_SUCCESS;
  ObTime ob_time;
  MYSQL_TIME *tm = nullptr;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  const ObTimeZoneInfo *tmp_tz = &tz_info;
  if (obj_type == ObObjType::ObDateTimeType) {
    tmp_tz = NULL;
  }
  if (OB_ISNULL(tm = reinterpret_cast<MYSQL_TIME *>(allocator.alloc(sizeof(MYSQL_TIME))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(ObTimeConverter::datetime_to_ob_time(param.get_datetime(), tmp_tz, ob_time))) {
    LOG_WARN("convert usec ", K(ret));
  } else {
    MEMSET(tm, 0, sizeof(MYSQL_TIME));
    tm->year = ob_time.parts_[DT_YEAR];
    tm->month = ob_time.parts_[DT_MON];
    tm->day = ob_time.parts_[DT_MDAY];
    tm->hour = ob_time.parts_[DT_HOUR];
    tm->minute = ob_time.parts_[DT_MIN];
    tm->second = ob_time.parts_[DT_SEC];
    tm->second_part = ob_time.parts_[DT_USEC];
    tm->neg = DT_MODE_NEG & ob_time.mode_;
    bind_param.buffer_ = tm;
    bind_param.buffer_len_ = sizeof(MYSQL_TIME);
  }
  return ret;
}


int ObBindParamEncode::encode_date(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param);
  int ret = OB_SUCCESS;
  ObTime ob_time;
  MYSQL_TIME *tm = nullptr;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  if (OB_ISNULL(tm = reinterpret_cast<MYSQL_TIME *>(allocator.alloc(sizeof(MYSQL_TIME))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(ObTimeConverter::date_to_ob_time(param.get_date(), ob_time))) {
    LOG_WARN("convert usec ", K(ret));
  } else {
    MEMSET(tm, 0, sizeof(MYSQL_TIME));
    tm->year = ob_time.parts_[DT_YEAR];
    tm->month = ob_time.parts_[DT_MON];
    tm->day = ob_time.parts_[DT_MDAY];
    tm->neg = DT_MODE_NEG & ob_time.mode_;
    bind_param.buffer_ = tm;
    bind_param.buffer_len_ = sizeof(MYSQL_TIME);
  }
  return ret;
}

int ObBindParamEncode::encode_time(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param);
  int ret = OB_SUCCESS;
  ObTime ob_time;
  MYSQL_TIME *tm = nullptr;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  if (OB_ISNULL(tm = reinterpret_cast<MYSQL_TIME *>(allocator.alloc(sizeof(MYSQL_TIME))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(ObTimeConverter::time_to_ob_time(param.get_time(), ob_time))) {
    LOG_WARN("convert usec ", K(ret));
  } else {
    MEMSET(tm, 0, sizeof(MYSQL_TIME));
    tm->day = ob_time.parts_[DT_DATE];
    tm->hour= ob_time.parts_[DT_HOUR];
    tm->minute= ob_time.parts_[DT_MIN];
    tm->second= ob_time.parts_[DT_SEC];
    tm->second_part= ob_time.parts_[DT_USEC];
    tm->neg = DT_MODE_NEG & ob_time.mode_;
    bind_param.buffer_ = tm;
    bind_param.buffer_len_ = sizeof(MYSQL_TIME);
  }
  return ret;
}

int ObBindParamEncode::encode_year(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(is_output_param);
  int ret = OB_SUCCESS;
  int64_t *year = nullptr;
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  if (OB_ISNULL(year = reinterpret_cast<int64_t *>(allocator.alloc(sizeof(int64_t))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc memory", K(ret));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(param.get_year(), *year))) {
    LOG_WARN("convert usec ", K(ret));
  } else {
    bind_param.col_idx_ = col_idx;
    bind_param.buffer_type_ = buffer_type;
    bind_param.buffer_ = year;
    bind_param.buffer_len_ = sizeof(int64_t);
  }
  return ret;
}

int ObBindParamEncode::encode_string(ENCODE_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  ObString val = param.get_string();
  const ObObjType obj_type = param.get_type();
  bind_param.col_idx_ = col_idx;
  bind_param.buffer_type_ = buffer_type;
  bind_param.buffer_ = val.ptr();
  bind_param.buffer_len_ = val.length();
  bind_param.length_ = val.length();
  return ret;
}


int ObBindParamEncode::encode_not_supported(ENCODE_FUNC_ARG_DECL)
{
  UNUSEDx(col_idx, is_output_param, tz_info, bind_param, allocator);
  const ObObjType obj_type = param.get_type();
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not supported type", K(ret), K(obj_type));
  return ret;
}

const ObBindParamEncode::EncodeFunc ObBindParamEncode::encode_map_[ObMaxType + 1] =
{
  ObBindParamEncode::encode_null,                    // ObNullType
  ObBindParamEncode::encode_int,                     // ObTinyIntType
  ObBindParamEncode::encode_int,                     // ObSmallIntType
  ObBindParamEncode::encode_int,                     // ObMediumIntType
  ObBindParamEncode::encode_int,                     // ObInt32Type
  ObBindParamEncode::encode_int,                     // ObIntType
  ObBindParamEncode::encode_uint,                    // ObUTinyIntType
  ObBindParamEncode::encode_uint,                    // ObUSmallIntType
  ObBindParamEncode::encode_uint,                    // ObUMediumIntType
  ObBindParamEncode::encode_uint,                    // ObUInt32Type
  ObBindParamEncode::encode_uint,                    // ObUInt64Type
  ObBindParamEncode::encode_float,                   // ObFloatType
  ObBindParamEncode::encode_double,                  // ObDoubleType
  ObBindParamEncode::encode_ufloat,                  // ObUFloatType
  ObBindParamEncode::encode_udouble,                 // ObUDoubleType
  ObBindParamEncode::encode_number,                  // ObNumberType
  ObBindParamEncode::encode_unumber,                 // ObUNumberType
  ObBindParamEncode::encode_datetime,                // ObDateTimeType
  ObBindParamEncode::encode_datetime,                // ObTimestampType
  ObBindParamEncode::encode_date,                    // ObDateType
  ObBindParamEncode::encode_time,                    // ObTimeType
  ObBindParamEncode::encode_year,                    // ObYearType
  ObBindParamEncode::encode_string,                  // ObVarcharType
  ObBindParamEncode::encode_string,                  // ObCharType
  ObBindParamEncode::encode_not_supported,           // ObHexStringType
  ObBindParamEncode::encode_not_supported,           // ObExtendType
  ObBindParamEncode::encode_not_supported,           // ObUnknownType
  ObBindParamEncode::encode_string,                  // ObTinyTextType
  ObBindParamEncode::encode_string,                  // ObTextType
  ObBindParamEncode::encode_string,                  // ObMediumTextType
  ObBindParamEncode::encode_string,                  // ObLongTextType
  ObBindParamEncode::encode_not_supported,           // ObBitType
  ObBindParamEncode::encode_not_supported,           // ObEnumType
  ObBindParamEncode::encode_not_supported,           // ObSetType
  ObBindParamEncode::encode_not_supported,           // ObEnumInnerType
  ObBindParamEncode::encode_not_supported,           // ObSetInnerType
  ObBindParamEncode::encode_not_supported,           // ObJsonType
  ObBindParamEncode::encode_not_supported,           // ObGeometryType
  ObBindParamEncode::encode_not_supported            // ObMaxType
};

int ObBindParamDecode::decode_null(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info, bind_param, allocator);
  int ret = OB_SUCCESS;
  param.set_null();
  return ret;
}

int ObBindParamDecode::decode_int(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(tz_info, allocator);
  int ret = OB_SUCCESS;
  switch (field_type) {
    case MYSQL_TYPE_TINY:
      param.set_tinyint(*(reinterpret_cast<int8_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_SHORT:
      param.set_smallint(*(reinterpret_cast<int16_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_INT24:
      param.set_mediumint(*(reinterpret_cast<int32_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_LONG:
      param.set_int32(*(reinterpret_cast<int32_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_LONGLONG:
      param.set_int(*(reinterpret_cast<int64_t*>(bind_param.buffer_)));
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown type", K(field_type));
      break;
  };
  return ret;
}

int ObBindParamDecode::decode_uint(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(tz_info, allocator);
  int ret = OB_SUCCESS;
  switch (field_type) {
    case MYSQL_TYPE_TINY:
      param.set_utinyint(*(reinterpret_cast<uint8_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_SHORT:
      param.set_usmallint(*(reinterpret_cast<uint16_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_INT24:
      param.set_umediumint(*(reinterpret_cast<uint32_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_LONG:
      param.set_uint32(*(reinterpret_cast<uint32_t*>(bind_param.buffer_)));
      break;
    case MYSQL_TYPE_LONGLONG:
      param.set_uint64(*(reinterpret_cast<uint64_t*>(bind_param.buffer_)));
      break;
    default:
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unknown type", K(field_type));
      break;
  };
  return ret;
}

int ObBindParamDecode::decode_float(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info, allocator);
  int ret = OB_SUCCESS;
  param.set_float(*(reinterpret_cast<float*>(bind_param.buffer_)));
  return ret;
}

int ObBindParamDecode::decode_ufloat(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info, allocator);
  int ret = OB_SUCCESS;
  param.set_ufloat(*(reinterpret_cast<float*>(bind_param.buffer_)));
  return ret;
}

int ObBindParamDecode::decode_double(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info, allocator);
  int ret = OB_SUCCESS;
  param.set_double(*(reinterpret_cast<double*>(bind_param.buffer_)));
  return ret;
}

int ObBindParamDecode::decode_udouble(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info, allocator);
  int ret = OB_SUCCESS;
  param.set_udouble(*(reinterpret_cast<double*>(bind_param.buffer_)));
  return ret;
}

int ObBindParamDecode::decode_number(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info);
  int ret = OB_SUCCESS;
  number::ObNumber nb;
  if (OB_FAIL(nb.from(reinterpret_cast<char *>(bind_param.buffer_), bind_param.length_, allocator))) {
    LOG_WARN("decode param to number failed", K(ret), K(bind_param));
  } else {
    param.set_number(nb);
  }
  return ret;
}

int ObBindParamDecode::decode_unumber(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info);
  int ret = OB_SUCCESS;
  number::ObNumber nb;
  if (OB_FAIL(nb.from(reinterpret_cast<char *>(bind_param.buffer_), bind_param.length_, allocator))) {
    LOG_WARN("decode param to number failed", K(ret), K(bind_param));
  } else {
    param.set_unumber(nb);
  }
  return ret;
}

int ObBindParamDecode::decode_datetime(DECODE_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  ObTime ob_time;
  ObPreciseDateTime value;
  MYSQL_TIME *tm = reinterpret_cast<MYSQL_TIME *>(bind_param.buffer_);
  if (0 == bind_param.length_) {
    value = 0;
  } else {
    ob_time.parts_[DT_YEAR] = tm->year;
    ob_time.parts_[DT_MON] = tm->month;
    ob_time.parts_[DT_MDAY] = tm->day;
    ob_time.parts_[DT_HOUR] = tm->hour;
    ob_time.parts_[DT_MIN] = tm->minute;
    ob_time.parts_[DT_SEC] = tm->second;
    ob_time.parts_[DT_USEC] = tm->second_part;
    ObTimeConvertCtx cvrt_ctx(NULL, false);
    ob_time.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(ob_time);
    if (MYSQL_TYPE_DATE == field_type) {
      value = ob_time.parts_[DT_DATE];
    } else if (OB_FAIL(ObTimeConverter::ob_time_to_datetime(ob_time, cvrt_ctx, value))){
      LOG_WARN("convert obtime to datetime failed", K(ret), K(value), K(tm->year), K(tm->month),
                K(tm->day), K(tm->hour), K(tm->minute), K(tm->second));
    }
  }
  if (OB_SUCC(ret)) {
    if (MYSQL_TYPE_TIMESTAMP == field_type) {
      int64_t ts_value = 0;
      if (OB_FAIL(ObTimeConverter::datetime_to_timestamp(value, &tz_info, ts_value))) {
        LOG_WARN("datetime to timestamp failed", K(ret));
      } else {
        param.set_timestamp(ts_value);
      }
    } else if (MYSQL_TYPE_DATETIME == field_type) {
      param.set_datetime(value);
    } else if (MYSQL_TYPE_DATE == field_type) {
      param.set_date(static_cast<int32_t>(value));
    }
  }
  LOG_TRACE("get datetime", K(tm->year), K(tm->month), K(tm->day), K(tm->hour), K(tm->minute),
  K(tm->second), K(tm->second_part), K(value));
  return ret;
}

int ObBindParamDecode::decode_time(DECODE_FUNC_ARG_DECL)
{
  int ret = OB_SUCCESS;
  ObTime ob_time;
  ObPreciseDateTime value;
  MYSQL_TIME *tm = reinterpret_cast<MYSQL_TIME *>(bind_param.buffer_);
  if (0 == bind_param.length_) {
    value = 0;
  } else {
    ob_time.parts_[DT_YEAR] = tm->year;
    ob_time.parts_[DT_MON] = tm->month;
    ob_time.parts_[DT_MDAY] = tm->day;
    ob_time.parts_[DT_HOUR] = tm->hour;
    ob_time.parts_[DT_MIN] = tm->minute;
    ob_time.parts_[DT_SEC] = tm->second;
    ob_time.parts_[DT_USEC] = tm->second_part;
    ob_time.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(ob_time);
    value = ObTimeConverter::ob_time_to_time(ob_time);
  }
  if (OB_SUCC(ret)) {
    param.set_time(value);
  }
  LOG_TRACE("get time", K(tm->year), K(tm->month), K(tm->day), K(tm->hour), K(tm->minute),
  K(tm->second), K(tm->second_part), K(value));
  return ret;
}


int ObBindParamDecode::decode_year(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(field_type, tz_info, allocator);
  int ret = OB_SUCCESS;
  param.set_year(*reinterpret_cast<uint8_t*>(bind_param.buffer_));
  return ret;
}

int ObBindParamDecode::decode_string(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(tz_info);
  int ret = OB_SUCCESS;
  ObObjType obj_type = ObNullType;
  OZ (ObMySQLPreparedStatement::get_ob_type(obj_type, (obmysql::EMySQLFieldType)field_type));
  OZ (ObMySQLProcStatement::store_string_obj(param, obj_type, allocator,
                                             (int64_t)bind_param.length_,
                                             (char *)bind_param.buffer_));
  return ret;
}

int ObBindParamDecode::decode_not_supported(DECODE_FUNC_ARG_DECL)
{
  UNUSEDx(tz_info, bind_param, param, allocator);
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_WARN("not supported type", K(ret), K(field_type));
  return ret;
}

const ObBindParamDecode::DecodeFunc ObBindParamDecode::decode_map_[ObMaxType + 1] =
{
  ObBindParamDecode::decode_null,                    // ObNullType
  ObBindParamDecode::decode_int,                     // ObTinyIntType
  ObBindParamDecode::decode_int,                     // ObSmallIntType
  ObBindParamDecode::decode_int,                     // ObMediumIntType
  ObBindParamDecode::decode_int,                     // ObInt32Type
  ObBindParamDecode::decode_int,                     // ObIntType
  ObBindParamDecode::decode_uint,                    // ObUTinyIntType
  ObBindParamDecode::decode_uint,                    // ObUSmallIntType
  ObBindParamDecode::decode_uint,                    // ObUMediumIntType
  ObBindParamDecode::decode_uint,                    // ObUInt32Type
  ObBindParamDecode::decode_uint,                    // ObUInt64Type
  ObBindParamDecode::decode_float,                   // ObFloatType
  ObBindParamDecode::decode_double,                  // ObDoubleType
  ObBindParamDecode::decode_ufloat,                  // ObUFloatType
  ObBindParamDecode::decode_udouble,                 // ObUDoubleType
  ObBindParamDecode::decode_number,                  // ObNumberType
  ObBindParamDecode::decode_unumber,                 // ObUNumberType
  ObBindParamDecode::decode_datetime,                // ObDateTimeType
  ObBindParamDecode::decode_datetime,                // ObTimestampType
  ObBindParamDecode::decode_datetime,                // ObDateType
  ObBindParamDecode::decode_time,                    // ObTimeType
  ObBindParamDecode::decode_year,                    // ObYearType
  ObBindParamDecode::decode_string,                  // ObVarcharType
  ObBindParamDecode::decode_string,                  // ObCharType
  ObBindParamDecode::decode_not_supported,           // ObHexStringType
  ObBindParamDecode::decode_not_supported,           // ObExtendType
  ObBindParamDecode::decode_not_supported,           // ObUnknownType
  ObBindParamDecode::decode_string,                  // ObTinyTextType
  ObBindParamDecode::decode_string,                  // ObTextType
  ObBindParamDecode::decode_string,                  // ObMediumTextType
  ObBindParamDecode::decode_string,                  // ObLongTextType
  ObBindParamDecode::decode_not_supported,           // ObBitType
  ObBindParamDecode::decode_not_supported,           // ObEnumType
  ObBindParamDecode::decode_not_supported,           // ObSetType
  ObBindParamDecode::decode_not_supported,           // ObEnumInnerType
  ObBindParamDecode::decode_not_supported,           // ObSetInnerType
  ObBindParamDecode::decode_not_supported,           // ObJsonType
  ObBindParamDecode::decode_not_supported,           // ObGeometryType
  ObBindParamDecode::decode_not_supported            // ObMaxType
};

void ObBindParam::assign(const ObBindParam &other)
{
  col_idx_ = other.col_idx_;
  buffer_type_ = other.buffer_type_;
  buffer_ = other.buffer_;
  buffer_len_ = other.buffer_len_;
  length_ = other.length_;
  is_unsigned_ = other.is_unsigned_;
  is_null_ = other.is_null_;
  array_buffer_ = other.array_buffer_;
  ele_size_ = other.ele_size_;
  max_array_size_ = other.max_array_size_;
  out_valid_array_size_ = other.out_valid_array_size_;
  array_is_null_ = other.array_is_null_;
}

ObMySQLPreparedStatement::ObMySQLPreparedStatement() :
    conn_(NULL),
    arena_allocator_(ObModIds::MYSQL_CLIENT_CACHE),
    alloc_(&arena_allocator_),
    param_(*this),
    result_(*this),
    stmt_param_count_(0),
    result_column_count_(0),
    stmt_(NULL),
    bind_params_(NULL),
    result_params_(NULL)
{
}

ObMySQLPreparedStatement::~ObMySQLPreparedStatement()
{
}

ObIAllocator *ObMySQLPreparedStatement::get_allocator()
{
  return alloc_;
}

void ObMySQLPreparedStatement::set_allocator(ObIAllocator *alloc)
{
  alloc_ = alloc;
  result_.alloc_ = alloc;
  param_.alloc_ = alloc;
}

MYSQL_STMT *ObMySQLPreparedStatement::get_stmt_handler()
{
  return stmt_;
}



int ObMySQLPreparedStatement::alloc_bind_params(const int64_t size, ObBindParam *&bind_params)
{
  int ret = OB_SUCCESS;
  if (size <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(size));
  } else if (OB_ISNULL(bind_params = reinterpret_cast<ObBindParam *>(alloc_->alloc(sizeof(ObBindParam) * size)))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("out of memory, alloc mem for mysql_bind error", K(ret));
  } else {
    MEMSET(bind_params, 0, sizeof(ObBindParam) * size);
  }
  return ret;
}

int ObMySQLPreparedStatement::get_bind_param_by_idx(const int64_t idx,
                                                    ObBindParam *&param)
{
  int ret = OB_SUCCESS;
  param = nullptr;
  if (idx >= stmt_param_count_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index", K(ret), K(idx), K(stmt_param_count_));
  } else {
    param = &(bind_params_[idx]);
  }
  return ret;
}

int ObMySQLPreparedStatement::get_bind_result_param_by_idx(const int64_t idx,
                                                           ObBindParam *&param)
{
  int ret = OB_SUCCESS;
  param = nullptr;
  if (idx >= result_column_count_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid index", K(ret), K(idx), K(result_column_count_));
  } else {
    param = &(result_params_[idx]);
  }
  return ret;
}


int ObMySQLPreparedStatement::get_ob_type(ObObjType &ob_type, obmysql::EMySQLFieldType mysql_type)
{
  int ret = OB_SUCCESS;
  switch (mysql_type) {
    case obmysql::EMySQLFieldType::MYSQL_TYPE_NULL:
      ob_type = ObNullType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_TINY:
      ob_type = ObTinyIntType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_SHORT:
      ob_type = ObSmallIntType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_LONG:
      ob_type = ObInt32Type;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_LONGLONG:
      ob_type = ObIntType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_FLOAT:
      ob_type = ObFloatType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_DOUBLE:
      ob_type = ObDoubleType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_TIMESTAMP:
      ob_type = ObTimestampType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_DATETIME:
      ob_type = ObDateTimeType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_TIME:
      ob_type = ObTimeType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_DATE:
      ob_type = ObDateType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_YEAR:
      ob_type = ObYearType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_VARCHAR:
    case obmysql::EMySQLFieldType::MYSQL_TYPE_VAR_STRING:
      ob_type = ObVarcharType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_STRING:
      ob_type = ObCharType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_TINY_BLOB:
      ob_type = ObTinyTextType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_BLOB:
      ob_type = ObTextType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_MEDIUM_BLOB:
      ob_type = ObMediumTextType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_LONG_BLOB:
      ob_type = ObLongTextType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL:
      ob_type = ObNumberType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_JSON:
      ob_type = ObJsonType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_GEOMETRY:
      ob_type = ObGeometryType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_BIT:
      ob_type = ObBitType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_ENUM:
      ob_type = ObEnumType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_SET:
      ob_type = ObSetType;
      break;
    case obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX:
      ob_type = ObExtendType;
      break;
    default:
      LOG_WARN("unsupport MySQL type", K(ret), K(mysql_type));
      ret = OB_OBJ_TYPE_ERROR;
  }
  return ret;
}

int ObMySQLPreparedStatement::init(ObMySQLConnection &conn, const ObString &sql, int64_t param_count)
{
  int ret = OB_SUCCESS;
  conn_ = &conn;
  if (sql.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sql", K(sql), K(ret));
  } else if (OB_ISNULL(stmt_ = mysql_stmt_init(conn_->get_handler()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_ERROR("fail to init stmt", K(ret));
  } else if (0 != mysql_stmt_prepare(stmt_, sql.ptr(), sql.length())) {
    ret = -mysql_errno(conn_->get_handler());
    LOG_WARN("fail to prepare stmt", "info", mysql_error(conn_->get_handler()), K(ret));
  } else if (OB_FAIL(param_.init())) {
    LOG_WARN("fail to init prepared result", K(ret));
  } else if (OB_FAIL(result_.init())) {
    LOG_WARN("fail to init prepared result", K(ret));
  } else if (FALSE_IT(stmt_param_count_ = param_.get_stmt_param_count())) {
  } else if (FALSE_IT(result_column_count_ = result_.get_result_column_count())) {
  } else if (stmt_param_count_ > 0 && OB_FAIL(alloc_bind_params(stmt_param_count_, bind_params_))) {
    LOG_WARN("fail to alloc stmt bind params", K(ret));
  } else if (result_column_count_ > 0 && OB_FAIL(alloc_bind_params(result_column_count_, result_params_))) {
    LOG_WARN("fail to alloc result bind params", K(ret));
  } else {
    LOG_INFO("conn_handler", "handler", conn_->get_handler(), K_(stmt), K_(stmt_param_count), K_(result_column_count));
  }
  return ret;
}

int ObMySQLPreparedStatement::close()
{
  int ret = OB_SUCCESS;
  if (nullptr != stmt_) {
    if (0 != mysql_stmt_close(stmt_)) {
      ret = -mysql_errno(conn_->get_handler());
      LOG_WARN("fail to close stmt", "info", mysql_error(conn_->get_handler()), K(ret));
    }
  }
  stmt_param_count_ = 0;
  result_column_count_ = 0;
  bind_params_ = NULL;
  result_params_ = NULL;
  param_.close();
  result_.close();
  return ret;
}









} // end namespace sqlcient
} // end namespace common
} // end namespace oceanbase
