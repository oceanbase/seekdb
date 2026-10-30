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

#define USING_LOG_PREFIX SHARE

#include "datum_core.h"
#include "common/datum/datum_value.h"
#include "common/wide_integer/ob_wide_integer_helper.h"
#include "common/timezone/ob_time_convert.h"

namespace oceanbase
{
namespace common
{

int DatumValue::build_not_strict_default_value(int16_t precision,
    ObCollationType string_collation, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  DatumValue result = *this;
  const SqlValueType type = get_type();
  switch (type) {
    case ObTinyIntType:
      ret = result.assign_scalar(static_cast<int64_t>(0), &SqlTypeMeta::set_tinyint, owner);
      break;
    case ObSmallIntType:
      ret = result.assign_scalar(static_cast<int64_t>(0), &SqlTypeMeta::set_smallint, owner);
      break;
    case ObMediumIntType:
      ret = result.assign_scalar(static_cast<int64_t>(0), &SqlTypeMeta::set_mediumint, owner);
      break;
    case ObInt32Type:
      ret = result.assign_scalar(static_cast<int64_t>(0), &SqlTypeMeta::set_int32, owner);
      break;
    case ObIntType:
      ret = result.assign_scalar(static_cast<int64_t>(0), &SqlTypeMeta::set_int, owner);
      break;
    case ObUTinyIntType:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_utinyint, owner);
      break;
    case ObUSmallIntType:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_usmallint, owner);
      break;
    case ObUMediumIntType:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_umediumint, owner);
      break;
    case ObUInt32Type:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_uint32, owner);
      break;
    case ObUInt64Type:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_uint64, owner);
      break;
    case ObFloatType:
      ret = result.assign_scalar(static_cast<float>(0), &SqlTypeMeta::set_float, owner);
      break;
    case ObUFloatType:
      ret = result.assign_scalar(static_cast<float>(0), &SqlTypeMeta::set_ufloat, owner);
      break;
    case ObDoubleType:
      ret = result.assign_scalar(static_cast<double>(0), &SqlTypeMeta::set_double, owner);
      break;
    case ObUDoubleType:
      ret = result.assign_scalar(static_cast<double>(0), &SqlTypeMeta::set_udouble, owner);
      break;
    case ObDateTimeType:
      ret = result.assign_scalar(static_cast<int64_t>(ObTimeConverter::ZERO_DATETIME), &SqlTypeMeta::set_datetime, owner);
      break;
    case ObTimestampType:
      ret = result.assign_scalar(static_cast<int64_t>(ObTimeConverter::ZERO_DATETIME), &SqlTypeMeta::set_timestamp, owner);
      break;
    case ObDateType:
      ret = result.assign_scalar(static_cast<int32_t>(ObTimeConverter::ZERO_DATE), &SqlTypeMeta::set_date, owner);
      break;
    case ObTimeType:
      ret = result.assign_scalar(static_cast<int64_t>(0), &SqlTypeMeta::set_time, owner);
      break;
    case ObYearType:
      ret = result.assign_scalar(static_cast<uint8_t>(0), &SqlTypeMeta::set_year, owner);
      break;
    case ObMySQLDateType:
      ret = result.assign_scalar(static_cast<ObMySQLDate>(ObTimeConverter::MYSQL_ZERO_DATE), &SqlTypeMeta::set_mysql_date, owner);
      break;
    case ObMySQLDateTimeType:
      ret = result.assign_scalar(static_cast<ObMySQLDateTime>(ObTimeConverter::MYSQL_ZERO_DATETIME), &SqlTypeMeta::set_mysql_datetime, owner);
      break;
    case ObBitType:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_bit, owner);
      break;
    case ObEnumType:
      ret = result.assign_scalar(static_cast<uint64_t>(1), &SqlTypeMeta::set_enum, owner);
      break;
    case ObSetType:
      ret = result.assign_scalar(static_cast<uint64_t>(0), &SqlTypeMeta::set_set, owner);
      break;
    case ObNumberType:
    case ObUNumberType: {
      number::ObNumber zero;
      zero.set_zero();
      ret = result.set_number(type, zero, owner);
      if (ret == OB_SUCCESS) {
        if (type == ObNumberType) { result.meta_.set_number(); }
        else { result.meta_.set_unumber(); }
      }
      break;
    }
    case ObVarcharType:
    case ObCharType:
      ret = result.set_string(type, ObString(), owner);
      break;
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType:
    case ObGeometryType:
    case ObCollectionSQLType:
      ret = result.set_string(type, ObString(), owner);
      if (ret == OB_SUCCESS) { result.meta_.set_inrow(); }
      break;
    case ObJsonType: {
      static const char json_null[2] = {0, 0};
      ret = result.set_string(type, ObString(sizeof(json_null), json_null), owner);
      if (ret == OB_SUCCESS) {
        result.meta_.set_inrow();
        result.meta_.set_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
      break;
    }
    case ObDecimalIntType: {
      const ObDecimalInt *zero = nullptr;
      int32_t bytes = 0;
      if (OB_FAIL(wide::ObDecimalIntConstValue::get_zero_value_byte_precision(precision, zero, bytes))) {
      } else {
        ret = result.set_decimal_int(bytes, 0, zero, owner);
      }
      break;
    }
    default:
      ret = OB_INVALID_ARGUMENT;
  }
  if (ret == OB_SUCCESS) {
    if (result.is_string_type()) {
      result.set_collation_level(CS_LEVEL_IMPLICIT);
      result.set_collation_type(string_collation);
    }
    *this = result;
  }
  return ret;
}

SqlValueDatumMapType ObDatum::get_sql_value_datum_map_type(const SqlValueType type)
{

  const static SqlValueDatumMapType maps[] = {
    OBJ_DATUM_NULL,           // ObNullType
    OBJ_DATUM_8BYTE_DATA,     // ObTinyIntType
    OBJ_DATUM_8BYTE_DATA,     // ObSmallIntType
    OBJ_DATUM_8BYTE_DATA,     // ObMediumIntType
    OBJ_DATUM_8BYTE_DATA,     // ObInt32Type
    OBJ_DATUM_8BYTE_DATA,     // ObIntType
    OBJ_DATUM_8BYTE_DATA,     // ObUTinyIntType
    OBJ_DATUM_8BYTE_DATA,     // ObUSmallIntType
    OBJ_DATUM_8BYTE_DATA,     // ObUMediumIntType
    OBJ_DATUM_8BYTE_DATA,     // ObUInt32Type
    OBJ_DATUM_8BYTE_DATA,     // ObUInt64Type
    OBJ_DATUM_4BYTE_DATA,     // ObFloatType
    OBJ_DATUM_8BYTE_DATA,     // ObDoubleType
    OBJ_DATUM_4BYTE_DATA,     // ObUFloatType
    OBJ_DATUM_8BYTE_DATA,     // ObUDoubleType
    OBJ_DATUM_NUMBER,         // ObNumberType
    OBJ_DATUM_NUMBER,         // ObUNumberType
    OBJ_DATUM_8BYTE_DATA,     // ObDateTimeType
    OBJ_DATUM_8BYTE_DATA,     // ObTimestampType
    OBJ_DATUM_4BYTE_DATA,     // ObDateType
    OBJ_DATUM_8BYTE_DATA,     // ObTimeType
    OBJ_DATUM_1BYTE_DATA,     // ObYearType
    OBJ_DATUM_STRING,         // ObVarcharType
    OBJ_DATUM_STRING,         // ObCharType
    OBJ_DATUM_STRING,         // ObHexStringType
    OBJ_DATUM_EXTEND,           // ObExtendType
    OBJ_DATUM_8BYTE_DATA,     // ObUnknownType
    OBJ_DATUM_STRING,         // ObTinyTextType
    OBJ_DATUM_STRING,         // ObTextType
    OBJ_DATUM_STRING,         // ObMediumTextType
    OBJ_DATUM_STRING,         // ObLongTextType
    OBJ_DATUM_8BYTE_DATA,     // ObBitType
    OBJ_DATUM_8BYTE_DATA,     // ObEnumType
    OBJ_DATUM_8BYTE_DATA,     // ObSetType
    OBJ_DATUM_STRING,         // ObEnumInnerType
    OBJ_DATUM_STRING,         // ObSetInnerType
    OBJ_DATUM_STRING,         // ObJsonType
    OBJ_DATUM_STRING,         // ObGeometryType
    OBJ_DATUM_STRING,         // ObUserDefinedSQLType
    OBJ_DATUM_DECIMALINT,     // ObDecimalIntType
    OBJ_DATUM_STRING,         // ObCollectionSQLType
    OBJ_DATUM_4BYTE_DATA,     // ObMySQLDateType
    OBJ_DATUM_8BYTE_DATA,     // ObMySQLDateTimeType
  };
  static_assert(sizeof(maps) / sizeof(maps[0]) == ObMaxType,
      "new added type should extend this map");
  SqlValueDatumMapType t = OBJ_DATUM_MAPPING_MAX;
  if (type < 0 || type >= ObMaxType) {
    LOG_ERROR_RET(common::OB_INVALID_ARGUMENT, "invalid obj type", K(type));
  } else {
    t = maps[type];
  }

  return t;
}

uint32_t ObDatum::get_reserved_size(const SqlValueDatumMapType type,
                                    const int16_t prec /*PRECISION_UNKNOWN_YET*/)
{
  static const uint32_t OBOBJ_DATUM_MAP_TYPE_TO_RES_SIZE_MAP[] =
  {
    OBJ_DATUM_NULL_RES_SIZE,           // OBJ_DATUM_NULL
    OBJ_DATUM_STRING_RES_SIZE,         // OBJ_DATUM_STRING
    OBJ_DATUM_NUMBER_RES_SIZE,         // OBJ_DATUM_NUMBER
    OBJ_DATUM_8BYTE_DATA_RES_SIZE,     // OBJ_DATUM_8BYTE_DATA
    OBJ_DATUM_4BYTE_DATA_RES_SIZE,     // OBJ_DATUM_4BYTE_DATA
    OBJ_DATUM_1BYTE_DATA_RES_SIZE,     // OBJ_DATUM_1BYTE_DATA
    OBJ_DATUM_4BYTE_LEN_DATA_RES_SIZE, // OBJ_DATUM_4BYTE_LEN_DATA
    OBJ_DATUM_2BYTE_LEN_DATA_RES_SIZE, // OBJ_DATUM_2BYTE_LEN_DATA
    OBJ_DATUM_EXTEND_RES_SIZE,      // OBJ_DATUM_EXTEND
    OBJ_DATUM_DECIMALINT_MAX_RES_SIZE, // OBJ_DATUM_DECIMALINT
  };
  static_assert(sizeof(OBOBJ_DATUM_MAP_TYPE_TO_RES_SIZE_MAP)
                / sizeof(OBOBJ_DATUM_MAP_TYPE_TO_RES_SIZE_MAP[0]) == OBJ_DATUM_MAPPING_MAX,
      "new added SqlValueDatumMapType should extend this map");
  static const uint32_t OBOBJ_DECIMAL_INT_TO_RES_SIZE_MAP[] =
  {
    sizeof(int32_t),   // DECIMAL_INT_32
    sizeof(int64_t),   // DECIMAL_INT_64
    sizeof(int128_t),  // DECIMAL_INT_128
    sizeof(int256_t),  // DECIMAL_INT_256
    sizeof(int512_t),  // DECIMAL_INT_512
  };
  uint32_t res_size = 0;
  if (type >= OBJ_DATUM_MAPPING_MAX) {
    LOG_ERROR_RET(common::OB_INVALID_ARGUMENT, "invalid obj type", K(type));
  } else if (OBJ_DATUM_DECIMALINT == type && prec > 0) {
    const ObDecimalIntWideType decimalint_type = get_decimalint_type(prec);
    res_size = OBOBJ_DECIMAL_INT_TO_RES_SIZE_MAP[decimalint_type];
  } else {
    res_size = OBOBJ_DATUM_MAP_TYPE_TO_RES_SIZE_MAP[type];
  }

  return res_size;
}

DEF_TO_STRING(ObDatum)
{
  int64_t pos = 0;
  int ret = OB_SUCCESS;
  const static int64_t STR_MAX_PRINT_LEN = 128L;
  if (null_) {
    J_NULL();
  } else {
    J_OBJ_START();
    BUF_PRINTF("len: %d, flag: %d, null: %d, ptr: %p", len_, flag_, null_, ptr_);
    if (len_ > 0) {
      OB_ASSERT(NULL != ptr_);
      const int64_t plen = std::min(static_cast<int64_t>(len_),
                                    static_cast<int64_t>(STR_MAX_PRINT_LEN));
      // print hex value
      BUF_PRINTF(", hex: ");
      if (OB_FAIL(hex_print(ptr_, plen, buf, buf_len, pos))) {
        // no logging in to_string function.
      } else {
        // maybe ObIntTC
        if (sizeof(int64_t) == len_) {
          BUF_PRINTF(", int: %ld", *int_);
          // maybe number with one digit
          if (1 == num_->desc_.len_) {
            BUF_PRINTF(", num_digit0: %u", num_->digits_[0]);
          }
        }
        // maybe printable C string
        int64_t idx = 0;
        while (idx < plen && isprint(ptr_[idx])) {
          idx++;
        }
        if (idx >= plen) {
          BUF_PRINTF(", cstr: %.*s", static_cast<int>(plen), ptr_);
        }
      }
    }
    J_OBJ_END();
  }
  return pos;
}

OB_DEF_SERIALIZE(ObDatum)
{
  int ret = OB_SUCCESS;
  const uint32_t wire_pack = get_wire_pack();
  const uint32_t wire_len = is_native_extend() ? DATUM_LEGACY_EXTEND_SIZE : len_;
  OB_UNIS_ENCODE(wire_pack);
  if (OB_SUCC(ret) && wire_len > 0) {
    if (OB_ISNULL(ptr_)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_UNLIKELY(pos > buf_len || wire_len > buf_len - pos)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      char scratch[DATUM_LEGACY_EXTEND_SIZE];
      const char *payload = is_native_extend() ? get_wire_ext_payload(scratch) : ptr_;
      MEMCPY(buf + pos, payload, wire_len);
      pos += wire_len;
    }
  }
  return ret;
}

OB_DEF_DESERIALIZE(ObDatum)
{
  int ret = OB_SUCCESS;
  uint32_t pack = 0;
  OB_UNIS_DECODE(pack);
  if (OB_SUCC(ret)) {
    ObDatumDesc value;
    value.pack_ = pack;
    if (OB_UNLIKELY(pos > data_len || value.len_ > data_len - pos
        || (value.is_ext() && value.len_ != DATUM_EXTEND_PAYLOAD_SIZE
                           && value.len_ != DATUM_LEGACY_EXTEND_SIZE))) {
      ret = OB_DESERIALIZE_ERROR;
    } else {
      pack_ = pack;
      ptr_ = buf + pos;
      pos += len_;
    }
  }
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObDatum)
{
  int64_t len = 0;
  const uint32_t wire_pack = get_wire_pack();
  OB_UNIS_ADD_LEN(wire_pack);
  len += is_native_extend() ? DATUM_LEGACY_EXTEND_SIZE : len_;
  return len;
}


} // end namespace common
} // end namespace oceanbase
