/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#include "obsm_utils.h"

#include "pl/ob_pl_stmt.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;
using namespace oceanbase::share::schema;
using namespace oceanbase::pl;

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
  {EMySQLFieldType::MYSQL_TYPE_BLOB,       BLOB_FLAG, 0},                         /* ObRoaringBitmapType */
  /* ObMaxType */
};

static_assert(sizeof(type_maps_) / sizeof(ObMySQLTypeMap) == ObMaxType, "Not enough initializer");

//called by handle COM_STMT_EXECUTE offset is 0
bool ObSMUtils::update_from_bitmap(ObObj &param, const char *bitmap, int64_t field_index)
{
  bool ret = false;
  if (update_from_bitmap(bitmap, field_index)) {
    param.set_null();
    ret = true;
  }
  return ret;
}

bool ObSMUtils::update_from_bitmap(const char *bitmap, int64_t field_index)
{
  bool ret = false;
  int byte_pos = static_cast<int>(field_index / 8);
  int bit_pos  = static_cast<int>(field_index % 8);
  if (NULL != bitmap) {
    char value = bitmap[byte_pos];
    if (value & (1 << bit_pos)) {
      ret = true;
    }
  }
  return ret;
}

int ObSMUtils::cell_str(
    char *buf, const int64_t len,
    const ObObj &obj,
    MYSQL_PROTOCOL_TYPE type, int64_t &pos,
    int64_t cell_idx, char *bitmap,
    const ObDataTypeCastParams &dtc_params,
    const ObField *field,
    const sql::ObSQLSessionInfo &session,
    ObSchemaGetterGuard *schema_guard,
    uint64_t tenant_id)
{
  int ret = OB_SUCCESS;

  ObScale scale = 0;
  ObPrecision precision = 0;
  bool zerofill = false;
  int32_t zflength = 0;
  bool is_oracle_raw = false;
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
    OB_LOG(DEBUG, "get field accuracy_", K(field->accuracy_), K(zerofill), K(obj));
  }
  if (OB_SUCC(ret)) {
    switch (obj.get_type_class()) {
      case ObNullTC:
        ret = ObMySQLUtil::null_cell_str(buf, len, type, pos, cell_idx, bitmap);
        break;
      case ObIntTC:
        ret = ObMySQLUtil::int_cell_str(buf, len, obj.get_int(), obj.get_type(), false, type, pos, zerofill, zflength);
        break;
      case ObUIntTC:
        ret = ObMySQLUtil::int_cell_str(buf, len, obj.get_int(), obj.get_type(), true, type, pos, zerofill, zflength);
        break;
      case ObFloatTC:
        ret = ObMySQLUtil::float_cell_str(buf, len, obj.get_float(), type, pos, scale, zerofill, zflength);
        break;
      case ObDoubleTC:
        ret = ObMySQLUtil::double_cell_str(buf, len, obj.get_double(), type, pos, scale, zerofill, zflength);
        break;
      case ObNumberTC:
        ret = ObMySQLUtil::number_cell_str(buf, len, obj.get_number(), pos, scale, zerofill, zflength);
        break;
      case ObDateTimeTC:
        ret = ObMySQLUtil::datetime_cell_str(buf, len, obj.get_datetime(), type, pos, (obj.is_timestamp() ? dtc_params.tz_info_ : NULL), scale);
        break;
      case ObDateTC:
        ret = ObMySQLUtil::date_cell_str(buf, len, obj.get_date(), type, pos);
        break;
      case ObTimeTC:
        ret = ObMySQLUtil::time_cell_str(buf, len, obj.get_time(), type, pos, scale);
        break;
      case ObYearTC:
        ret = ObMySQLUtil::year_cell_str(buf, len, obj.get_year(), type, pos);
        break;
      case ObOTimestampTC:
        ret = ObMySQLUtil::otimestamp_cell_str(buf, len, obj.get_otimestamp_value(), type, pos, dtc_params, scale, obj.get_type());
        break;
      case ObRawTC:
      case ObTextTC: // TODO@hanhui texttc share the stringtc temporarily
      case ObStringTC:
      // lob locator也会按varchar方式进行encode, 客户端往server端传输数据时,
      // 也是将lob locator按varchar传输, 先编码LobLocator length, 然后再编码整个lob Locator
      case ObLobTC:
      case ObRoaringBitmapTC: {
        ret = ObMySQLUtil::varchar_cell_str(buf, len, obj.get_string(), is_oracle_raw, pos);
        break;
      }
      case ObJsonTC:{
        ret = ObMySQLUtil::json_cell_str(MTL_ID(), buf, len, obj.get_string(), pos);
        break;
      }
      case ObGeometryTC: {
        if (lib::is_oracle_mode() && type == MYSQL_PROTOCOL_TYPE::TEXT) {
          ret = OB_NOT_SUPPORTED;
        } else {
          ret = ObMySQLUtil::geometry_cell_str(buf, len, obj.get_string(), pos);
        }
        break;
      }
      case ObBitTC: {
        int32_t bit_len = 0;
        if (OB_LIKELY(precision > 0)) {
          bit_len = precision;
        } else {
          bit_len = ObAccuracy::MAX_ACCURACY[obj.get_type()].precision_;
          _OB_LOG(WARN, "max precision is used. origin precision is %d", precision);
        }
        ret = ObMySQLUtil::bit_cell_str(buf, len, obj.get_bit(), bit_len, type, pos);
        break;
      }
      case ObUserDefinedSQLTC: {
        if (obj.get_udt_subschema_id() == 0) { // xml
          ret = ObMySQLUtil::sql_utd_cell_str(MTL_ID(), buf, len, obj.get_string(), pos);
        } else if (type == MYSQL_PROTOCOL_TYPE::TEXT) { // common sql udt text protocal
          ret = ObMySQLUtil::varchar_cell_str(buf, len, obj.get_string(), is_oracle_raw, pos);
        } else {
          // ToDo: sql udt binary protocal (result should be the same as extend type)
          ret = OB_NOT_IMPLEMENT;
          OB_LOG(WARN, "UDTSQLType binary protocal not implemented", K(ret));
        }
        break;
      }
      case ObCollectionSQLTC: {
        ret = ObMySQLUtil::varchar_cell_str(buf, len, obj.get_string(), is_oracle_raw, pos);
        break;
      }
      case ObDecimalIntTC: {
        ret = ObMySQLUtil::decimalint_cell_str(buf, len, obj.get_decimal_int(), obj.get_int_bytes(),
                                               obj.get_scale(), pos, zerofill, zflength);
        break;
      }
      case ObMySQLDateTC:
        ret = ObMySQLUtil::mdate_cell_str(buf, len, obj.get_mysql_date(), type, pos);
        break;
      case ObMySQLDateTimeTC:
        ret = ObMySQLUtil::mdatetime_cell_str(buf, len, obj.get_mysql_datetime(), type, pos, NULL, scale);
        break;
      default:
        _OB_LOG(ERROR, "invalid ob type=%d", obj.get_type());
        ret = OB_ERROR;
        break;
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
        num_decimals = static_cast<ObScale>(lib::is_oracle_mode()
        ? ORACLE_NOT_FIXED_DEC
        : 0);
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
            ? (lib::is_oracle_mode() ? ORACLE_NOT_FIXED_DEC : NOT_FIXED_DEC)
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

const char* ObSMUtils::get_extend_type_name(int type)
{
	switch(type){
    case PL_INVALID_TYPE: return "INVALID";
    case PL_OBJ_TYPE: return "OBJECT";
    case PL_RECORD_TYPE: return "RECORD";
    case PL_NESTED_TABLE_TYPE: return "NESTED TABLE";
    case PL_ASSOCIATIVE_ARRAY_TYPE: return "ASSOCIATIVE ARRAY";
    case PL_VARRAY_TYPE: return "VARRAY";
    case PL_CURSOR_TYPE: return "CURSOR";
    case PL_SUBTYPE: return "SUBTYPE";
    case PL_INTEGER_TYPE: return "PL INTEGER";
    case PL_REF_CURSOR_TYPE: return "REF CURSOR";
    case PL_OPAQUE_TYPE: return "OPAQUE";
    default: return "UNKNOW";
  }
}