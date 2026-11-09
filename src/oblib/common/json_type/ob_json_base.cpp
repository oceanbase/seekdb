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

#define USING_LOG_PREFIX SQL

#include "ob_json_base.h"
#include "lib/utility/ob_fast_convert.h"
#include "ob_json_bin.h"
#include "ob_json_parse.h"
#include "rust/json.h"
#include "lib/hash_func/murmur_hash.h"
#include "lib/encode/ob_base64_encode.h" // for ObBase64Encoder
#include "lib/charset/ob_dtoa.h" // ob_gcvt_opt
#include "common/mysqlclient/ob_mysql_global.h" // DOUBLE_TO_STRING_CONVERSION_BUFFER_SIZE
#include "lib/utility/ob_smart_var.h" // for SMART_VAR
#include "lib/utility/ob_smart_call.h"

namespace oceanbase {
namespace common {

bool ObIJsonBase::is_json_number(ObJsonNodeType json_type) const 
{
  bool ret_bool = false;
  switch (json_type) {
    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_DOUBLE:
    case ObJsonNodeType::J_OFLOAT:  
    case ObJsonNodeType::J_ODOUBLE:  
    case ObJsonNodeType::J_ODECIMAL: 
    case ObJsonNodeType::J_OINT: 
    case ObJsonNodeType::J_OLONG: {
      ret_bool = true;
      break;
    }
    default:{
      ret_bool = false;
    }
  }
  return ret_bool;
}

bool ObIJsonBase::is_json_scalar(ObJsonNodeType json_type) const 
{
  bool ret_bool = false;
  if (json_type < ObJsonNodeType::J_MAX_TYPE 
      && (json_type != ObJsonNodeType::J_OBJECT && json_type != ObJsonNodeType::J_ARRAY)) {
    ret_bool = true;
  }
  return ret_bool;
}

bool ObIJsonBase::is_json_string(ObJsonNodeType json_type) const 
{
  bool ret_bool = false;
  switch (json_type) {
    case ObJsonNodeType::J_STRING:
    case ObJsonNodeType::J_OBINARY:
    case ObJsonNodeType::J_OOID:
    case ObJsonNodeType::J_ORAWHEX:
    case ObJsonNodeType::J_ORAWID:
    case ObJsonNodeType::J_ODAYSECOND:
    case ObJsonNodeType::J_OYEARMONTH: {
      ret_bool = true;
      break;
    }
    default: {
      ret_bool = false;
    }
  }
  return ret_bool;
}

bool ObIJsonBase::is_json_date(ObJsonNodeType json_type) const 
{
  bool ret_bool = false;
  switch (json_type) {
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_MYSQL_DATE:
    case ObJsonNodeType::J_MYSQL_DATETIME:
    case ObJsonNodeType::J_TIMESTAMP:
    case ObJsonNodeType::J_JSON_DATE_EXT:
    case ObJsonNodeType::J_OTIMESTAMP:
    case ObJsonNodeType::J_ODATE: {
      ret_bool = true;
      break;
    }
    default: {
      ret_bool = false;
    }
  }
  return ret_bool;
}

bool ObIJsonBase::is_real_json_null(const ObIJsonBase *value) const
{
  return value->json_type() == ObJsonNodeType::J_NULL;
}

int ObIJsonBase::print_jtime(ObJsonBuffer &j_buf, bool is_quoted) const
{
  INIT_SUCC(ret);

  if (is_quoted) {
    if (OB_FAIL(j_buf.append("\""))) {
    }
  }

  // ObTimeConverter::ob_time_to_str will lose precision.
  if (OB_SUCC(ret)) {
    ObDTMode tmode;
    ObJsonNodeType j_type = json_type();
    if (OB_FAIL(ObJsonBaseUtil::get_dt_mode_by_json_type(j_type, tmode))) {
    } else {
      ObTime t(tmode);
      const int64_t tmp_buf_len = DATETIME_MAX_LENGTH + 1;
      char tmp_buf[tmp_buf_len] = {0};
      int64_t pos = 0;
      const int16_t print_scale = 6;
      if (OB_FAIL(get_obtime(t))) {
      } else if (OB_FAIL(ObTimeConverter::ob_time_to_str(t, tmode, print_scale,
                                                         tmp_buf, tmp_buf_len, pos, true))) {
      } else if (OB_FAIL(j_buf.append(tmp_buf))) {
      } else if (is_quoted) {
        if (OB_FAIL(j_buf.append("\""))) {
        }
      }
    }
  }

  return ret;
}

int ObIJsonBase::print_decimal(ObJsonBuffer &j_buf) const
{
  INIT_SUCC(ret);
  int64_t pos = j_buf.length();

  if (OB_FAIL(j_buf.reserve(number::ObNumber::MAX_PRINTABLE_SIZE))) {
  } else {
    ObScale scale = get_decimal_scale();
    number::ObNumber val = get_decimal_data();
    if (OB_FAIL(val.format_v2(j_buf.ptr(), j_buf.capacity(), pos, scale))) {
    } else if (OB_FAIL(j_buf.set_length(pos))) {
    }
  }

  return ret;
}

struct ObFindDoubleEscapeFunc {
  ObFindDoubleEscapeFunc() {}

  bool operator()(const char c)
  {
    return c == '.' || c == 'e';
  }
};

int ObIJsonBase::print_float(ObJsonBuffer &j_buf) const
{
  INIT_SUCC(ret);

  if (OB_FAIL(j_buf.reserve(FLOAT_TO_STRING_CONVERSION_BUFFER_SIZE + 1))) {
  } else {
    double val = get_float();
    char *start = j_buf.ptr() + j_buf.length();
    uint64_t len = ob_gcvt(val, ob_gcvt_arg_type::OB_GCVT_ARG_FLOAT,
        FLOAT_TO_STRING_CONVERSION_BUFFER_SIZE, start, NULL);
    if (OB_FAIL(j_buf.set_length(j_buf.length() + len))) {
    } else {
      // add ".0" in the end.
      ObFindDoubleEscapeFunc func;
      if (std::none_of(start, start + len, func)) {
        if (OB_FAIL(j_buf.append("."))) {
        } else if (OB_FAIL(j_buf.append("0"))) {
        }
      }
    }
  }

  return ret;
}

// only used in json opaque type.
// Notice: varchar has different mapping in mysql 5.7 (var_string)and 8.0(varchar)
static const obmysql::EMySQLFieldType opaque_ob_type_to_mysql_type[ObMaxType] =
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
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,                           /* ObUserDefinedSQLType, buf for xml we use long_blob type currently? */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NEWDECIMAL,                        /* ObDecimalIntType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_COMPLEX,                           /* ObCollectionSQLType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,                       /* reserved for ObMySQLDateType */
  obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED,                       /* reserved for ObMySQLDateTimeType */
  /* ObMaxType */
};

int ObIJsonBase::print_opaque(ObJsonBuffer &j_buf, uint64_t depth, bool is_quoted) const
{
  INIT_SUCC(ret);
  const char *data = get_data();
  uint64_t data_length = get_data_length();
  ObObjType f_type = field_type();

  if (f_type >= ObMaxType) {
    ret = OB_ERR_MAX_VALUE;
  } else {
    const uint64_t need_len = ObBase64Encoder::needed_encoded_length(data_length);
    obmysql::EMySQLFieldType mysql_type = obmysql::EMySQLFieldType::MYSQL_TYPE_NOT_DEFINED;
    mysql_type = opaque_ob_type_to_mysql_type[f_type];
    char field_buf[ObFastFormatInt::MAX_DIGITS10_STR_SIZE] = {0};
    int64_t field_len = ObFastFormatInt::format_unsigned(mysql_type, field_buf);
    ObArenaAllocator tmp_allocator;
    ObJsonBuffer base64_buf(&tmp_allocator);

    // base64::typeXX:<binary data>
    if (OB_FAIL(base64_buf.append("base64:type"))) {
    } else if (OB_FAIL(base64_buf.append(field_buf, field_len, 0))) {
    } else if (OB_FAIL(base64_buf.append(":"))) {
    } else if (OB_FAIL(base64_buf.reserve(need_len))) {
    } else {
      int64_t pos = 0;
      const uint8_t *input = reinterpret_cast<const uint8_t *>(data);
      if (OB_ISNULL(input)) {
        ret = OB_ERR_NULL_VALUE;
      } else if (OB_FAIL(ObBase64Encoder::encode(input, data_length,
                                                 base64_buf.ptr() + base64_buf.length(),
                                                 need_len, pos))) {
      } else if (OB_FAIL(base64_buf.set_length(base64_buf.length() + need_len))) {
      } else {
        if (is_quoted) {
          if (OB_FAIL(ObJsonBaseUtil::add_double_quote(j_buf, base64_buf.ptr(),
                                                       base64_buf.length()))) {
          }
        } else if (OB_FAIL(j_buf.append(base64_buf.ptr(), base64_buf.length(), 0))) {
        }
      }
    }
    base64_buf.reset(); // free memory
  }

  return ret;
}

int ObIJsonBase::print(ObJsonBuffer &j_buf, bool is_quoted, uint64_t reserve_len, bool is_pretty, uint64_t depth) const
{
  int ret = OB_SUCCESS;
  if (json_type() == ObJsonNodeType::J_NULL && !is_real_json_null(this)) {
  } else if (reserve_len > 0 && OB_FAIL(j_buf.reserve(reserve_len * 1.2))) {
  } else {
    ObArenaAllocator allocator;
    ObString binary;
    if (OB_FAIL(const_cast<ObIJsonBase *>(this)->get_raw_binary(binary, &allocator))) {
    } else {
      const json::FormatResult result = json::format_binary(
          rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(binary.ptr()), binary.length()),
          is_quoted, is_pretty, depth);
      if (result.platform_error != OB_SUCCESS) {
        ret = result.platform_error;
      } else if (result.status != json::Status::Ok) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ret = j_buf.append(reinterpret_cast<const char *>(result.text.data()), result.text.size(), 0);
      }
    }
  }
  return ret;
}

int ObIJsonBase::print_database_scalar(ObJsonBuffer &j_buf, bool is_quoted) const
{
  switch (json_type()) {
    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL:
      return print_decimal(j_buf);
    case ObJsonNodeType::J_OFLOAT:
      return print_float(j_buf);
    case ObJsonNodeType::J_OPAQUE:
      return print_opaque(j_buf, 0, is_quoted);
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_TIME:
    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_TIMESTAMP:
    case ObJsonNodeType::J_MYSQL_DATETIME:
    case ObJsonNodeType::J_MYSQL_DATE:
    case ObJsonNodeType::J_JSON_DATE_EXT:
    case ObJsonNodeType::J_ODATE:
    case ObJsonNodeType::J_OTIMESTAMP:
    case ObJsonNodeType::J_OTIMESTAMPTZ:
      return print_jtime(j_buf, is_quoted);
    default:
      return OB_ERR_UNEXPECTED;
  }
}

namespace json {
FormatResult format_database_scalar(rust::Slice<const uint8_t> input, bool quoted)
{
  ObArenaAllocator allocator;
  ObJsonBin value(reinterpret_cast<const char *>(input.data()), input.size(), &allocator);
  ObJsonBuffer buffer(&allocator);
  FormatResult result;
  result.status = Status::Ok;
  result.platform_error = value.reset_iter();
  if (result.platform_error == OB_SUCCESS) {
    result.platform_error = value.print_database_scalar(buffer, quoted);
  }
  if (result.platform_error == OB_SUCCESS) {
    result.text.reserve(buffer.length());
    for (int64_t i = 0; i < buffer.length(); ++i) { result.text.push_back(buffer.ptr()[i]); }
  }
  return result;
}

OrderResult compare_temporal(rust::Slice<const uint8_t> left, rust::Slice<const uint8_t> right)
{
  ObArenaAllocator allocator;
  ObJsonBin a(reinterpret_cast<const char *>(left.data()), left.size(), &allocator);
  ObJsonBin b(reinterpret_cast<const char *>(right.data()), right.size(), &allocator);
  int ret = OB_SUCCESS;
  OrderResult result;
  result.status = Status::Ok;
  result.order = 0;
  ObTime time_a;
  ObTime time_b;
  ObDTMode mode_b;
  if (OB_FAIL(a.reset_iter())) {
  } else if (OB_FAIL(b.reset_iter())) {
  } else if (OB_FAIL(ObJsonBaseUtil::get_dt_mode_by_json_type(b.json_type(), mode_b))) {
  } else if (OB_FAIL(a.get_obtime(time_a))) {
  } else if (OB_FAIL(b.get_obtime(time_b))) {
  } else {
    const ObJsonNodeType kind = a.json_type();
    const ObDTMode mode_a = kind == ObJsonNodeType::J_TIME ? DT_TYPE_TIME
        : (kind == ObJsonNodeType::J_DATE || kind == ObJsonNodeType::J_MYSQL_DATE
           || kind == ObJsonNodeType::J_JSON_DATE_EXT) ? DT_TYPE_DATE : DT_TYPE_DATETIME;
    const int64_t value_a = ObTimeConverter::ob_time_to_int(time_a, mode_a);
    const int64_t value_b = ObTimeConverter::ob_time_to_int(time_b, mode_b);
    result.order = (value_a > value_b) - (value_a < value_b);
  }
  result.platform_error = ret;
  return result;
}
}


uint64_t json::HashCapability::append_bytes(uint64_t seed, rust::Slice<const uint8_t> input) const noexcept
{
  return algorithm_ == nullptr ? murmurhash64A(input.data(), static_cast<int32_t>(input.size()), seed)
                               : algorithm_(input.data(), input.size(), seed);
}

json::HashResult json::HashCapability::hash_database_scalar(uint64_t seed, uint8_t kind,
    int64_t integer, uint64_t unsigned_value, double real, rust::Slice<const uint8_t> payload) const
{
  char storage[256];
  ObDataBuffer number_allocator(storage, sizeof(storage));
  number::ObNumber number;
  HashResult result;
  result.status = Status::Ok;
  result.value = seed;
  int ret = OB_SUCCESS;
  bool numeric = true;
  const auto type = static_cast<ObJsonNodeType>(kind);
  switch (type) {
    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT:
      ret = number.from(integer, number_allocator);
      break;
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG:
      ret = number.from(unsigned_value, number_allocator);
      break;
    case ObJsonNodeType::J_OFLOAT:
    case ObJsonNodeType::J_DOUBLE:
    case ObJsonNodeType::J_ODOUBLE:
      ret = ObJsonBaseUtil::double_to_number(real, number_allocator, number);
      break;
    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      int64_t position = 4;
      ret = number.deserialize(reinterpret_cast<const char *>(payload.data()), payload.size(), position);
      break;
    }
    default:
      numeric = false;
      break;
  }
  if (OB_SUCC(ret) && numeric) {
    const uint64_t length = number.get_length() < 0 ? 0 : number.get_length();
    result.value = append_bytes(seed, {reinterpret_cast<const uint8_t *>(number.get_digits()), length});
  } else if (OB_SUCC(ret) && type == ObJsonNodeType::J_OPAQUE) {
    result.value = append_bytes(seed, payload);
  } else if (OB_SUCC(ret)) {
    ObTime time;
    ObDTMode mode = DT_TYPE_DATE;
    switch (type) {
      case ObJsonNodeType::J_JSON_DATE_EXT:
        mode = DT_TYPE_DATETIME;
        [[fallthrough]];
      case ObJsonNodeType::J_DATE:
        ret = ObTimeConverter::date_to_ob_time(integer, time);
        break;
      case ObJsonNodeType::J_MYSQL_DATE:
        time.mode_ |= DT_TYPE_MYSQL_DATE;
        ret = ObTimeConverter::mdate_to_ob_time(integer, time);
        break;
      case ObJsonNodeType::J_TIME:
        mode = DT_TYPE_TIME;
        ret = ObTimeConverter::time_to_ob_time(integer, time);
        break;
      case ObJsonNodeType::J_DATETIME:
      case ObJsonNodeType::J_TIMESTAMP:
      case ObJsonNodeType::J_ODATE:
        mode = DT_TYPE_DATETIME;
        [[fallthrough]];
      case ObJsonNodeType::J_OTIMESTAMP:
      case ObJsonNodeType::J_OTIMESTAMPTZ:
        ret = ObTimeConverter::datetime_to_ob_time(integer, nullptr, time);
        break;
      case ObJsonNodeType::J_MYSQL_DATETIME:
        mode = DT_TYPE_DATETIME;
        time.mode_ |= DT_TYPE_MYSQL_DATETIME;
        ret = ObTimeConverter::mdatetime_to_ob_time(integer, time);
        break;
      default:
        ret = OB_ERR_UNEXPECTED;
        break;
    }
    if (OB_SUCC(ret)) {
      const int64_t canonical = ObTimeConverter::ob_time_to_int(time, mode);
      result.value = append_bytes(seed, {reinterpret_cast<const uint8_t *>(&canonical), sizeof(canonical)});
    }
  }
  result.platform_error = ret;
  return result;
}

int ObIJsonBase::calc_json_hash_value(uint64_t seed, hash_algo algorithm, uint64_t &result) const
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator;
  ObString binary;
  if (OB_FAIL(const_cast<ObIJsonBase *>(this)->get_raw_binary(binary, &allocator))) {
  } else if (OB_FAIL(hash_json_binary(binary, seed, algorithm, result))) {
  }
  return ret;
}

int hash_json_binary(const ObString &input, uint64_t seed, hash_algo algorithm, uint64_t &result)
{
  const json::HashCapability capability(algorithm);
  const json::HashResult hashed = json::hash_binary(
      {reinterpret_cast<const uint8_t *>(input.ptr()), static_cast<size_t>(input.length())}, seed, capability);
  const int ret = hashed.platform_error != OB_SUCCESS ? hashed.platform_error
      : hashed.status != json::Status::Ok ? OB_ERR_UNEXPECTED : OB_SUCCESS;
  if (ret == OB_SUCCESS) { result = hashed.value; }
  return ret;
}

int compare_json_binary(const ObString &left, const ObString &right, int &result)
{
  const json::OrderResult ordered = json::order_binary(
      {reinterpret_cast<const uint8_t *>(left.ptr()), static_cast<size_t>(left.length())},
      {reinterpret_cast<const uint8_t *>(right.ptr()), static_cast<size_t>(right.length())});
  result = ordered.order;
  return ordered.platform_error != OB_SUCCESS ? ordered.platform_error
      : ordered.status == json::Status::Ok ? OB_SUCCESS
      : ordered.status == json::Status::UnsupportedComparison ? OB_OP_NOT_ALLOW
      : ordered.status == json::Status::NumericOverflow ? OB_NUMERIC_OVERFLOW
      : OB_ERR_UNEXPECTED;
}
int ObIJsonBase::compare(const ObIJsonBase &other, int &res, bool is_path) const
{
  int ret = OB_SUCCESS;
  res = 0;
  const ObJsonNodeType a = json_type();
  const ObJsonNodeType b = other.json_type();
  if (a == ObJsonNodeType::J_ERROR || b == ObJsonNodeType::J_ERROR) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_path && (a == ObJsonNodeType::J_OBJECT || b == ObJsonNodeType::J_OBJECT
                        || (a == ObJsonNodeType::J_NULL && a != b))) {
    res = -3;
  } else if (a == ObJsonNodeType::J_NULL && b == ObJsonNodeType::J_NULL
             && is_real_json_null(this) != is_real_json_null(&other)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObArenaAllocator allocator;
    ObString left;
    ObString right;
    if (OB_FAIL(const_cast<ObIJsonBase *>(this)->get_raw_binary(left, &allocator))) {
    } else if (OB_FAIL(const_cast<ObIJsonBase &>(other).get_raw_binary(right, &allocator))) {
    } else {
      ret = compare_json_binary(left, right, res);
    }
  }
  return ret;
}




int ObIJsonBase::get_used_size(uint64_t &size)
{
  return static_cast<const ObJsonBin *>(this)->get_used_bytes(size);
}

int ObIJsonBase::get_free_space(uint64_t &size)
{
  size_t space = 0;
  const int ret = static_cast<const ObJsonBin *>(this)->get_free_space(space);
  if (ret == OB_SUCCESS) { size = space; }
  return ret;
}

int ObIJsonBase::get_raw_binary(common::ObString &out, ObIAllocator *allocator)
{
  return static_cast<const ObJsonBin *>(this)->get_raw_binary(out, allocator);
}


JsonObjectIterator ObIJsonBase::object_iterator() const
{
  return JsonObjectIterator(this);
}

int ObIJsonBase::to_int(int64_t &value, bool check_range, bool force_convert) const
{
  INIT_SUCC(ret);
  int64_t val = 0;

  switch (json_type()) {
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG: {
      uint64_t ui = get_uint();
      if (ui > INT64_MAX && check_range) {
        ret = OB_OPERATE_OVERFLOW;
      } else {
        val = static_cast<int64_t>(ui);
      }
      break;
    }

    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT: {
      val = get_int();
      break;
    }

    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *start = get_data();
      if (OB_ISNULL(start)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        const char *end = start + length;
        bool valid = false;
        if (check_range && OB_UNLIKELY(NULL != memchr(start, '.', length))) {
          ret = OB_OPERATE_OVERFLOW;
        } else {
          val = ObFastAtoi<int64_t>::atoi(start, end, valid);
          if (valid == false) {
            ret = OB_ERR_DATA_TRUNCATED;
          }
          value = val;
        }
      }
      break;
    }

    case ObJsonNodeType::J_BOOLEAN: {
      val = get_boolean();
      break;
    }

    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      number::ObNumber nmb = get_decimal_data();
      if (OB_FAIL(nmb.extract_valid_int64_with_round(val))) {
      }
      break;
    }

    case ObJsonNodeType::J_DOUBLE:
    case ObJsonNodeType::J_ODOUBLE: {
      double d = get_double();
      if (d <= static_cast<double>(LLONG_MIN)) {
        if (check_range) {
          ret = OB_OPERATE_OVERFLOW;
        } else {
          val = LLONG_MIN;
        }
      } else if (d >= static_cast<double>(LLONG_MAX)) {
        if (check_range) {
          ret = OB_OPERATE_OVERFLOW;
        } else {
          val = LLONG_MAX;
        }
      } else {
        val = static_cast<int64_t>(rint(d));
      }
      break;
    }  
    
    case ObJsonNodeType::J_OFLOAT: {
      double d = get_float();
      if (d <= static_cast<float>(LLONG_MIN)) {
        if (check_range) {
          ret = OB_OPERATE_OVERFLOW;
        } else {
          val = LLONG_MIN;
        }
      } else if (d >= static_cast<float>(LLONG_MAX)) {
        if (check_range) {
          ret = OB_OPERATE_OVERFLOW;
        } else {
          val = LLONG_MAX;
        }
      } else {
        val = static_cast<int64_t>(rint(d));
      }
      break;
    }  
    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret) || force_convert) {
    value = val;
  }

  return ret;
}

int ObIJsonBase::to_uint(uint64_t &value, bool fail_on_negative, bool check_range) const
{
  INIT_SUCC(ret);
  uint64_t val = 0;

  switch (json_type()) {
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG: {
      val = get_uint();
      break;
    }

    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT: {
      int64_t signed_val = get_int();
      if (signed_val < 0 && fail_on_negative) {
        ret = OB_OPERATE_OVERFLOW;
      } else {
        val = static_cast<uint64_t>(signed_val);
      }
      break;
    }

    case ObJsonNodeType::J_STRING: {
      const char *data = get_data();
      uint64_t length = get_data_length();
      int err = 0;
      char *endptr = NULL;
      bool is_unsigned = true;
      if (OB_ISNULL(data) || length == 0) {
        ret = OB_ERR_NULL_VALUE;
      } else if (data[0] == '-') {
        is_unsigned = false;
        if (fail_on_negative) {
          ret = OB_OPERATE_OVERFLOW;
        }
      } else if (check_range && OB_UNLIKELY(NULL != memchr(data, '.', length))) {
        ret = OB_OPERATE_OVERFLOW;
      }

      if (OB_SUCC(ret)) {
        val = ObCharset::strntoullrnd(data, length, is_unsigned, &endptr, &err);
        if (ERANGE == err && (UINT64_MAX == val || 0 == val)) {
          ret = OB_DATA_OUT_OF_RANGE;
        } else {
          // check_convert_str_err
          // 1. only one of data and endptr is null, it is invalid input.
          if ((OB_ISNULL(data) || OB_ISNULL(endptr)) && data != endptr) {
            ret = OB_ERR_UNEXPECTED;
          } else
          // 2. data == endptr include NULL == NULL.
          if (OB_UNLIKELY(data == endptr) || OB_UNLIKELY(EDOM == err)) {
            ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD; //1366
          } else {
            // 3. so here we are sure that both data and endptr are not NULL.
            endptr += ObCharset::scan_str(endptr, data + length, OB_SEQ_SPACES);
            if (endptr < data + length) {
              ret = OB_ERR_DATA_TRUNCATED; //1265
            }
          }
        }
      }
      value = val;
      break;
    }

    case ObJsonNodeType::J_BOOLEAN: {
      val = get_boolean();
      break;
    }

    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      number::ObNumber num_val = get_decimal_data();
      int64_t i = 0;
      uint64_t ui = 0;
      number::ObNumber nmb;
      ObArenaAllocator allocator;
      if (num_val.is_negative() && fail_on_negative) {
        ret = OB_OPERATE_OVERFLOW;
      } else if (OB_FAIL(nmb.from(num_val, allocator))) {
      } else if (OB_FAIL(ObJsonBaseUtil::number_to_uint(nmb, val))) {
      }
      break;
    }

    case ObJsonNodeType::J_DOUBLE:
    case ObJsonNodeType::J_ODOUBLE: {
      double d = get_double();
      if (d < 0 && fail_on_negative) {
        ret = OB_OPERATE_OVERFLOW;
      } else {
        ret = ObJsonBaseUtil::double_to_uint(d, val, check_range);
      }
      break;
    }

    case ObJsonNodeType::J_OFLOAT: {
      double d = get_float();
      if (d < 0 && fail_on_negative) {
        ret = OB_OPERATE_OVERFLOW;
      } else {
        ret = ObJsonBaseUtil::double_to_uint(d, val, check_range);
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = val;
  }

  return ret;
}

int ObIJsonBase::to_double(double &value) const
{
  INIT_SUCC(ret);
  double val = 0.0;

  switch (json_type()) {
    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      number::ObNumber num_val = get_decimal_data();
      char buf[MAX_DOUBLE_PRINT_SIZE] = {0};
      int64_t pos = 0;
      if (OB_FAIL(num_val.format(buf, sizeof(buf), pos, -1))) {
      } else {
        char *endptr = NULL;
        int err = 0;
        val = ObCharset::strntod(buf, pos, &endptr, &err);
        if (EOVERFLOW == err && (-DBL_MAX == value || DBL_MAX == value)) {
          ret = OB_DATA_OUT_OF_RANGE;
        }
      }
      break;
    }

    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        char *endptr = NULL;
        int err = 0;
        val = ObCharset::strntodv2(data, length, &endptr, &err);
        if (EOVERFLOW == err && (-DBL_MAX == value || DBL_MAX == value)) {
          ret = OB_DATA_OUT_OF_RANGE;
        } else {
          ObString tmp_str(length, data);
          ObString trimed_str = tmp_str.trim();
          if (0 == trimed_str.length()) {
            ret = OB_ERR_DOUBLE_TRUNCATED;
          } else {
            // 1. only one of data and endptr is null, it is invalid input.
            if ((OB_ISNULL(data) || OB_ISNULL(endptr)) && data != endptr) {
              ret = OB_ERR_UNEXPECTED;
            } else if (OB_UNLIKELY(data == endptr) || OB_UNLIKELY(EDOM == err)) { // 2. data == endptr include NULL == NULL.
              ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD; //1366
            } else { // 3. so here we are sure that both data and endptr are not NULL.
              endptr += ObCharset::scan_str(endptr, data + length, OB_SEQ_SPACES);
              if (endptr < data + length) {
                ret = OB_ERR_DATA_TRUNCATED; //1265
              }
            }
            value = val;
          }
        }
      }
      break;
    }

    case ObJsonNodeType::J_DOUBLE:
    case ObJsonNodeType::J_ODOUBLE: {
      val = get_double();
      break;
    }

    case ObJsonNodeType::J_OFLOAT: {
      val = get_float();
      break;
    }

    case ObJsonNodeType::J_INT: 
    case ObJsonNodeType::J_OINT: {
      val = static_cast<double>(get_int());
      break;
    }

    case ObJsonNodeType::J_UINT: 
    case ObJsonNodeType::J_OLONG: {
      val = static_cast<double>(get_uint());
      break;
    }
    
    case ObJsonNodeType::J_BOOLEAN: {
      val = get_boolean();
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }

    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = val;
  }

  return ret;
}

int ObIJsonBase::to_number(ObIAllocator *allocator, number::ObNumber &number) const
{
  INIT_SUCC(ret);
  number::ObNumber num;

  if (OB_ISNULL(allocator)) {
    ret = OB_ERR_NULL_VALUE;
  } else {
    switch (json_type()) {
      case ObJsonNodeType::J_DECIMAL:
      case ObJsonNodeType::J_ODECIMAL: {
        num = get_decimal_data();
        break;
      }

      case ObJsonNodeType::J_STRING: {
        uint64_t length = get_data_length();
        const char *data = get_data();
        if (OB_ISNULL(data)) {
          ret = OB_ERR_NULL_VALUE;
        } else if (OB_FAIL(num.from(data, length, *allocator))) {
        }
        number = num;
        break;
      }

      case ObJsonNodeType::J_DOUBLE:
      case ObJsonNodeType::J_ODOUBLE: {
        double d = get_double();
        if (OB_FAIL(ObJsonBaseUtil::double_to_number(d, *allocator, num))) {
        }
        break;
      }

      case ObJsonNodeType::J_OFLOAT: {
        double d = get_float();
        if (OB_FAIL(ObJsonBaseUtil::double_to_number(d, *allocator, num))) {
        }
        break;
      }

      case ObJsonNodeType::J_INT:
      case ObJsonNodeType::J_OINT: {
        int64_t i = get_int();
        if (OB_FAIL(num.from(i, *allocator))) {
        }
        break;
      }

      case ObJsonNodeType::J_UINT:
      case ObJsonNodeType::J_OLONG: {
        uint64_t ui = get_uint();
        if (OB_FAIL(num.from(ui, *allocator))) {
        }
        break;
      }

      case ObJsonNodeType::J_BOOLEAN: {
        {
          bool b = get_boolean();
          if (OB_FAIL(num.from(static_cast<int64_t>(b), *allocator))) {
          }
        }
        break;
      }
      case ObJsonNodeType::J_ARRAY:
      case ObJsonNodeType::J_OBJECT: {
        ret = OB_OPERATE_OVERFLOW;
        break;
      }

      case ObJsonNodeType::J_DATE:
      case ObJsonNodeType::J_TIME:
      case ObJsonNodeType::J_DATETIME:
      case ObJsonNodeType::J_TIMESTAMP:
      case ObJsonNodeType::J_JSON_DATE_EXT:
      case ObJsonNodeType::J_ODATE:
      case ObJsonNodeType::J_OTIMESTAMP:
      case ObJsonNodeType::J_OTIMESTAMPTZ: {
        // json_element_t::to_Number use
        ObTime val ;
        ObDTMode dt_mode = DT_TYPE_DATE;
        if (OB_FAIL(get_obtime(val))) {
        } else {
          int64_t ival = 0;
          ObTimeConvertCtx cvrt_ctx(nullptr, false);
          if (OB_FAIL(ObTimeConverter::ob_time_to_datetime(val, cvrt_ctx, ival))) {
          } else {
            ival /= (1000 * 1000);
            if (OB_FAIL(num.from(static_cast<int64_t>(ival), *allocator))) {
            }
          }
        }
        break;
      }
      case ObJsonNodeType::J_MYSQL_DATETIME:
      case ObJsonNodeType::J_MYSQL_DATE: {
        // json_element_t::to_Number use
        ObTime val ;
        ObDTMode dt_mode = DT_TYPE_DATE;
        ObDateSqlMode date_sql_mode;
        date_sql_mode.allow_invalid_dates_ = false;
        date_sql_mode.no_zero_date_ = false;
        int64_t ival = 0;
        if (OB_FAIL(get_obtime(val))) {
        } else if (OB_FAIL(ObTimeConverter::validate_datetime(val, date_sql_mode))) {
          ret = OB_SUCCESS;
          ival = ObTimeConverter::ZERO_DATETIME;
        }
        if (OB_SUCC(ret)) {
          ObTimeConvertCtx cvrt_ctx(nullptr, false);
          if (OB_FAIL(ObTimeConverter::ob_time_to_datetime(val, cvrt_ctx, ival))) {
          } else {
            ival /= (1000 * 1000);
            if (OB_FAIL(num.from(static_cast<int64_t>(ival), *allocator))) {
            }
          }
        }
        break;
      }
      
      default: {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
  }

  if (OB_SUCC(ret)) {
    number = num;
  }

  return ret;
}

int ObIJsonBase::to_datetime(int64_t &value, ObTimeConvertCtx *cvrt_ctx_t) const
{
  INIT_SUCC(ret);
  int64_t datetime;
  ObTimeConvertCtx cvrt_ctx(NULL, false);
  if (OB_NOT_NULL(cvrt_ctx_t) && cvrt_ctx_t->is_timestamp_) {
    cvrt_ctx.tz_info_ = cvrt_ctx_t->tz_info_;
    cvrt_ctx.is_timestamp_ = cvrt_ctx_t->is_timestamp_;
  }
  switch (json_type()) {
    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT: {
      if (get_int() < 0) {
        ret = OB_INVALID_DATE_FORMAT;
      } else {
        ObDateSqlMode date_sql_mode;
        date_sql_mode.allow_invalid_dates_ = false;
        date_sql_mode.no_zero_date_ = false;
        if (OB_FAIL(ObTimeConverter::int_to_datetime(get_int(), 0, cvrt_ctx, datetime, date_sql_mode))) {
        }
      }
      break;
    }

    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      number::ObNumber num_val = get_decimal_data();
      uint64_t val = 0;
      if (OB_FAIL(ObJsonBaseUtil::number_to_uint(num_val, val))) {
      } else {
        datetime = (1000 * 1000) *  val; 
      }
      break;
    }

    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_TIMESTAMP:
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_JSON_DATE_EXT:
    case ObJsonNodeType::J_ODATE:
    case ObJsonNodeType::J_OTIMESTAMP:
    case ObJsonNodeType::J_OTIMESTAMPTZ: {
      ObTime t;
      if (OB_FAIL(get_obtime(t))) {
      } else if (OB_FAIL(ObTimeConverter::ob_time_to_datetime(t, cvrt_ctx, datetime))) {
      }
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATE:
    case ObJsonNodeType::J_MYSQL_DATETIME: {
      ObTime t;
      ObDateSqlMode date_sql_mode;
      date_sql_mode.allow_invalid_dates_ = false;
      date_sql_mode.no_zero_date_ = false;
      if (OB_FAIL(get_obtime(t))) {
      } else if (OB_FAIL(ObTimeConverter::validate_datetime(t, date_sql_mode))) {
        ret = OB_SUCCESS;
        datetime = ObTimeConverter::ZERO_DATETIME;
      } else {
        t.parts_[DT_DATE] = ObTimeConverter::ob_time_to_date(t);
        if(OB_FAIL(ObTimeConverter::ob_time_to_datetime(t, cvrt_ctx, datetime))) {
        }
      }
      break;
    }
    
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG: {
      ObArenaAllocator tmp_allocator;
      ObJsonBuffer str_data(&tmp_allocator);
      if (OB_FAIL(print(str_data, false))) {
      } else if (OB_ISNULL(str_data.ptr())) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str = str_data.string();
        if (OB_FAIL(ObTimeConverter::str_to_datetime(str, cvrt_ctx, datetime))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str(static_cast<int32_t>(length), static_cast<int32_t>(length), data);
        if (OB_FAIL(ObTimeConverter::str_to_datetime(str, cvrt_ctx, datetime))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = datetime;
  }

  return ret;
}

int ObIJsonBase::to_mdatetime(ObMySQLDateTime &value, ObTimeConvertCtx *cvrt_ctx_t) const
{
  INIT_SUCC(ret);
  int64_t datetime = 0;
  ObMySQLDateTime mdatetime = 0;
  ObTimeConvertCtx cvrt_ctx(NULL, false);
  switch (json_type()) {
    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT: {
      // for JSON date conversion from integer input
      int64_t datetime = (1000 * 1000) *  get_int();
      if (OB_FAIL(ObTimeConverter::datetime_to_mdatetime(datetime, mdatetime))) {
      }
      break;
    }

    case ObJsonNodeType::J_DECIMAL:
    case ObJsonNodeType::J_ODECIMAL: {
      number::ObNumber num_val = get_decimal_data();
      uint64_t val = 0;
      if (OB_FAIL(ObJsonBaseUtil::number_to_uint(num_val, val))) {
      } else {
        datetime = (1000 * 1000) *  val; 
        if (OB_FAIL(ObTimeConverter::datetime_to_mdatetime(datetime, mdatetime))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_MYSQL_DATETIME:
    case ObJsonNodeType::J_MYSQL_DATE: {
      ObTime t;
      if (OB_FAIL(get_obtime(t))) {
      } else if (OB_FAIL(ObTimeConverter::validate_datetime(t, cvrt_ctx_t->date_sql_mode_))) {
        mdatetime = ObTimeConverter::MYSQL_ZERO_DATETIME;
        ret = OB_SUCCESS;
      } else if (OB_FAIL(ObTimeConverter::ob_time_to_mdatetime(t, mdatetime))) {
      }
      break;
    }
    
    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG: {
      ObArenaAllocator tmp_allocator;
      ObJsonBuffer str_data(&tmp_allocator);
      if (OB_FAIL(print(str_data, false))) {
      } else if (OB_ISNULL(str_data.ptr())) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str = str_data.string();
        if (OB_FAIL(ObTimeConverter::str_to_mdatetime(str, cvrt_ctx, mdatetime, NULL,
                                                      cvrt_ctx_t->date_sql_mode_))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str(static_cast<int32_t>(length), static_cast<int32_t>(length), data);
        if (OB_FAIL(ObTimeConverter::str_to_mdatetime(str, cvrt_ctx, mdatetime, NULL,
                                                      cvrt_ctx_t->date_sql_mode_))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = mdatetime;
  }

  return ret;
}


int ObIJsonBase::to_date(int32_t &value) const
{
  INIT_SUCC(ret);
  int32_t date;

  switch (json_type()) {
    case ObJsonNodeType::J_DATETIME:
    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_TIMESTAMP: {
      ObTime t;
      if (OB_FAIL(get_obtime(t))) {
      } else {
        date = ObTimeConverter::ob_time_to_date(t);
      }
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATETIME:
    case ObJsonNodeType::J_MYSQL_DATE: {
      ObTime t;
      ObDateSqlMode date_sql_mode;
      date_sql_mode.allow_invalid_dates_ = false;
      date_sql_mode.no_zero_date_ = false;
      if (OB_FAIL(get_obtime(t))) {
      } else if (OB_FAIL(ObTimeConverter::validate_datetime(t, date_sql_mode))) {
        ret = OB_SUCCESS;
        date = ObTimeConverter::ZERO_DATE;
      } else {
        date = ObTimeConverter::ob_time_to_date(t);
      }
      break;
    }
    
    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str(static_cast<int32_t>(length), static_cast<int32_t>(length), data);
        if (OB_FAIL(ObTimeConverter::str_to_date(str, date))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }
    case ObJsonNodeType::J_INT: {
      int64_t in_val = get_int();
      ObDateSqlMode date_sql_mode;
      date_sql_mode.allow_invalid_dates_ = false;
      date_sql_mode.no_zero_date_ = false;
      if (OB_FAIL(ObTimeConverter::int_to_date(in_val, date, date_sql_mode))) {
      }
      break;
    }

    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = date;
  }

  return ret;
}

int ObIJsonBase::to_mdate(ObMySQLDate &value, ObDateSqlMode date_sql_mode) const
{
  INIT_SUCC(ret);
  ObMySQLDate date;
  switch (json_type()) {
    case ObJsonNodeType::J_MYSQL_DATETIME:
    case ObJsonNodeType::J_MYSQL_DATE: {
      ObTime t;
      if (OB_FAIL(get_obtime(t))) {
      } else if (OB_FAIL(ObTimeConverter::validate_datetime(t, date_sql_mode))) {
        date = ObTimeConverter::MYSQL_ZERO_DATE;
        ret = OB_SUCCESS;
      } else {
        date = ObTimeConverter::ob_time_to_mdate(t);
      }
      break;
    }
    
    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str(static_cast<int32_t>(length), static_cast<int32_t>(length), data);
        if (OB_FAIL(ObTimeConverter::str_to_mdate(str, date, date_sql_mode))) {
        }
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }
    case ObJsonNodeType::J_INT: {
      int64_t in_val = get_int();
      ObTime ob_time;
      if (in_val == 0) {
        value = ObTimeConverter::MYSQL_ZERO_DATE;
      } else if (OB_FAIL(ObTimeConverter::int_to_ob_time_with_date(in_val, ob_time, date_sql_mode))) {
      } else {
        value = ObTimeConverter::ob_time_to_mdate(ob_time);
      }
      break;
    }

    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = date;
  }

  return ret;
}

int ObIJsonBase::to_time(int64_t &value) const
{
  INIT_SUCC(ret);
  int64_t time;

  switch (json_type()) {
    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_UINT: {
      int64_t in_val = get_int();
      if (OB_FAIL(ObTimeConverter::int_to_time(in_val, value))) {
      }
      break;
    }
    case ObJsonNodeType::J_TIME: {
      ObTime t;
      if (OB_FAIL(get_obtime(t))) {
      } else {
        value = ObTimeConverter::ob_time_to_time(t);
      }
      break;
    }

    case ObJsonNodeType::J_STRING: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str(static_cast<int32_t>(length), static_cast<int32_t>(length), data);
        if (OB_FAIL(ObTimeConverter::str_to_time(str, time))) {
        } else {
          value = time;
        }
      }
      break;
    }
    case ObJsonNodeType::J_ARRAY:
    case ObJsonNodeType::J_OBJECT: {
      ret = OB_OPERATE_OVERFLOW;
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  return ret;
}

int ObIJsonBase::to_bit(uint64_t &value) const
{
  INIT_SUCC(ret);
  uint64_t bit;
  const ObJsonNodeType j_type = json_type();
  ObDTMode dt_mode = 0;

  switch (j_type) {
    case ObJsonNodeType::J_DATETIME: {
      dt_mode = DT_TYPE_DATETIME; // set dt_node and pass through
    }
    case ObJsonNodeType::J_MYSQL_DATETIME: {
      if (dt_mode == 0) {
        dt_mode = DT_TYPE_MYSQL_DATETIME; // set dt_node and pass through
      }
    }

    case ObJsonNodeType::J_DATE:
    case ObJsonNodeType::J_JSON_DATE_EXT: {
      if (dt_mode == 0) {
        dt_mode = DT_TYPE_DATE; // set dt_node and pass through
      }
    }
    case ObJsonNodeType::J_MYSQL_DATE: {
      if (dt_mode == 0) {
        dt_mode = DT_TYPE_MYSQL_DATE; // set dt_node and pass through
      }
    }

    case ObJsonNodeType::J_TIME: {
      if (dt_mode == 0) {
        dt_mode = DT_TYPE_TIME; // set dt_node and pass through
      }
    }

    case ObJsonNodeType::J_TIMESTAMP:
    case ObJsonNodeType::J_ODATE:
    case ObJsonNodeType::J_OTIMESTAMP:
    case ObJsonNodeType::J_OTIMESTAMPTZ: {
      if (dt_mode == 0) {
        dt_mode = DT_TYPE_DATETIME; // set dt_node and pass through
      }
  
      ObTime t;
      if (OB_FAIL(get_obtime(t))) {
      } else {
        const int64_t length = OB_CAST_TO_VARCHAR_MAX_LENGTH + 1;
        char buf[length] = {0};
        int64_t pos = 0;
        dt_mode = (t.mode_ == 0 ? dt_mode : t.mode_);
        if (OB_FAIL(ObTimeConverter::ob_time_to_str(t, dt_mode, 0, buf, length, pos, true))) {
        } else {
          ObString str(sizeof(buf), strlen(buf), buf);
          if (OB_FAIL(ObJsonBaseUtil::string_to_bit(str, bit))) {
          }
        }
      }
      break;
    }
    
    case ObJsonNodeType::J_OBJECT:
    case ObJsonNodeType::J_ARRAY: {
      ObArenaAllocator allocator;
      ObStringBuffer buffer(&allocator);
      if (OB_FAIL(print(buffer, false))) {
      } else if (buffer.length() > sizeof(value)) {
        ret = OB_ERR_DATA_TOO_LONG;
        LOG_WARN("bit len too long", K(buffer.length()));
      } else {
        ObString str = buffer.string();
        if (OB_FAIL(ObJsonBaseUtil::string_to_bit(str, bit))) {
        }
      }
      break;
    }

    case ObJsonNodeType::J_BOOLEAN: {
      bit = get_boolean();
      break;
    }

    case ObJsonNodeType::J_DECIMAL: 
    case ObJsonNodeType::J_ODECIMAL: {
      int64_t i = 0;
      uint64_t ui = 0;
      number::ObNumber nmb;
      number::ObNumber num_val = get_decimal_data();
      ObArenaAllocator allocator;
      if (OB_FAIL(nmb.from(num_val, allocator))) {
      } else if (OB_FAIL(ObJsonBaseUtil::number_to_uint(nmb, bit))) {
      }
      break;
    }

    case ObJsonNodeType::J_DOUBLE: 
    case ObJsonNodeType::J_ODOUBLE: {
      double d = get_double();
      if (OB_FAIL(ObJsonBaseUtil::double_to_uint(d, bit))) {
      }
      break;
    }

    case ObJsonNodeType::J_OFLOAT: {
      double d = get_float();
      if (OB_FAIL(ObJsonBaseUtil::double_to_uint(d, bit))) {
      }
      break;
    }

    case ObJsonNodeType::J_INT:
    case ObJsonNodeType::J_OINT: {
      bit = static_cast<uint64_t>(get_int());
      break;
    }

    case ObJsonNodeType::J_UINT:
    case ObJsonNodeType::J_OLONG: {
      bit = get_uint();
      break;
    }

    case ObJsonNodeType::J_STRING:
    case ObJsonNodeType::J_OPAQUE: {
      uint64_t length = get_data_length();
      const char *data = get_data();
      if (OB_ISNULL(data)) {
        ret = OB_ERR_NULL_VALUE;
      } else {
        ObString str(static_cast<int32_t>(length), static_cast<int32_t>(length), data);
        if (OB_FAIL(ObJsonBaseUtil::string_to_bit(str, bit))) {
        }
      }
      break;
    }

    case ObJsonNodeType::J_NULL: {
      bit = 0;
      break;
    }

    default: {
      ret = OB_ERR_UNEXPECTED;
      break;
    }
  }

  if (OB_SUCC(ret)) {
    value = bit;
  }

  return ret;
}


int ObJsonBaseFactory::get_json_base(ObIAllocator *allocator, const ObString &buf,
                                     ObJsonInType in_type,
                                     ObIJsonBase *&out, uint32_t parse_flag,
                                     uint32_t max_depth_config)
{
  return get_json_base(allocator, buf.ptr(), buf.length(), in_type, out, parse_flag, max_depth_config);
}

int ObJsonBaseFactory::get_json_base(ObIAllocator *allocator, const char *ptr, uint64_t length,
    ObJsonInType in_type, ObIJsonBase *&out,
    uint32_t parse_flag, uint32_t max_depth_config)
{
  int ret = OB_SUCCESS;
  ObJsonBin *binary = out == nullptr ? nullptr : static_cast<ObJsonBin *>(out);
  if (binary != nullptr) { allocator = binary->get_allocator(); }
  ObString data(length, ptr);
  if (allocator == nullptr) {
    ret = OB_ERR_NULL_VALUE;
  } else if (ptr == nullptr || length == 0) {
    ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
  } else if (in_type == ObJsonInType::JSON_TEXT
      && OB_FAIL(ObJsonParser::get_binary(allocator, ptr, length, data, parse_flag, max_depth_config))) {
  } else if (in_type != ObJsonInType::JSON_TEXT && in_type != ObJsonInType::JSON_BIN) {
    ret = OB_ERR_UNEXPECTED;
  } else if (binary != nullptr && binary->get_update_ctx() != nullptr) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    if (binary == nullptr) {
      void *buffer = allocator->alloc(sizeof(ObJsonBin));
      if (buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
      else { binary = new (buffer) ObJsonBin(allocator); }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(binary->reset(data, 0, nullptr))) {
    } else if (in_type == ObJsonInType::JSON_BIN && HAS_FLAG(parse_flag, ObJsonParser::JSN_SCHEMA_FLAG)
        && OB_FAIL(ObJsonBaseUtil::check_json_schema_ref_def(*allocator, binary))) {
    } else {
      out = binary;
    }
  }
  return ret;
}





bool ObJsonBaseUtil::is_time_type(ObJsonNodeType j_type)
{
  bool bool_ret = (j_type == ObJsonNodeType::J_DATETIME
                  || j_type == ObJsonNodeType::J_DATE
                  || j_type == ObJsonNodeType::J_MYSQL_DATE
                  || j_type == ObJsonNodeType::J_MYSQL_DATETIME
                  || j_type == ObJsonNodeType::J_TIME 
                  || j_type == ObJsonNodeType::J_TIMESTAMP
                  || j_type == ObJsonNodeType::J_JSON_DATE_EXT
                  || j_type == ObJsonNodeType::J_ODATE
                  || j_type == ObJsonNodeType::J_OTIMESTAMP
                  || j_type == ObJsonNodeType::J_OTIMESTAMPTZ);
  return bool_ret;
}

ObObjType ObJsonBaseUtil::get_time_type(ObJsonNodeType json_type)
{
  ObObjType type = ObMaxType;
  switch(json_type) {
    case  ObJsonNodeType::J_DATE: {
      type = ObDateType;
      break;
    }
    case  ObJsonNodeType::J_MYSQL_DATE: {
      type = ObMySQLDateType;
      break;
    }
    case ObJsonNodeType::J_TIME: {
      type = ObTimeType;
      break;
    }
    case ObJsonNodeType::J_DATETIME: {
      type = ObDateTimeType;
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATETIME: {
      type = ObMySQLDateTimeType;
      break;
    }
    case ObJsonNodeType::J_TIMESTAMP: {
      type = ObTimestampType;
      break;
    }
    case ObJsonNodeType::J_JSON_DATE_EXT: {
      type = ObDateType;
      break;
    }
    case ObJsonNodeType::J_ODATE: {
      type = ObTimestampType;
      break;
    }
    case ObJsonNodeType::J_OTIMESTAMP: {
      type = ObTimestampType;
      break;
    }
    default:
      LOG_WARN_RET(OB_INVALID_ARGUMENT, "undefined datetime json type", K(json_type));
      break;
  }
  return type;
}

int ObJsonBaseUtil::get_dt_mode_by_json_type(ObJsonNodeType j_type, ObDTMode &dt_mode)
{
  INIT_SUCC(ret);

  switch (j_type) {
    case ObJsonNodeType::J_DATETIME: {
      dt_mode = DT_TYPE_DATETIME;
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATETIME: {
      dt_mode = DT_TYPE_MYSQL_DATETIME;
      break;
    }

    case ObJsonNodeType::J_DATE: {
      dt_mode = DT_TYPE_DATE;
      break;
    }
    case ObJsonNodeType::J_MYSQL_DATE: {
      dt_mode = DT_TYPE_MYSQL_DATE;
      break;
    }

    case ObJsonNodeType::J_TIMESTAMP: {
      dt_mode = DT_TYPE_DATETIME;
      break;
    }

    case ObJsonNodeType::J_TIME: {
      dt_mode = DT_TYPE_TIME;
      break;
    }
    
    case ObJsonNodeType::J_JSON_DATE_EXT: {
      dt_mode = DT_TYPE_DATE;
      break;
    }
    
    case ObJsonNodeType::J_ODATE: {
      dt_mode = DT_TYPE_DATETIME;
      break;
    }
    case ObJsonNodeType::J_OTIMESTAMP: {
      dt_mode = (DT_TYPE_DATETIME | DT_TYPE_NANOSECOND);
      break;
    }
    case ObJsonNodeType::J_OTIMESTAMPTZ: {
      dt_mode = (DT_TYPE_DATETIME | DT_TYPE_NANOSECOND | DT_TYPE_TIMEZONE);
      break;
    }
    default: {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected json type", K(j_type));
      break;
    }
  }

  return ret;
}

int ObJsonBaseUtil::add_double_quote(ObJsonBuffer &j_buf, const char *cptr, uint64_t length)
{
  if (OB_ISNULL(cptr)) { return OB_ERR_NULL_VALUE; }
  const rust::Vec<uint8_t> text = json::quote_string(
      rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(cptr), length), true);
  return j_buf.append(reinterpret_cast<const char *>(text.data()), text.size(), 0);
}

int ObJsonBaseUtil::append_string(ObJsonBuffer &j_buf, bool is_quoted,
                                  const char *data, uint64_t length)
{
  INIT_SUCC(ret);

  if (is_quoted) {
    if (OB_FAIL(ObJsonBaseUtil::add_double_quote(j_buf, data, length))) {
    }
  } else {
    if (OB_FAIL(j_buf.append(data, length, 0))) {
    }
  }

  return ret;
}

int ObJsonBaseUtil::compare_int_json(int a, ObIJsonBase* other, int& result)
{
  INIT_SUCC(ret);
  if (other->json_type() != ObJsonNodeType::J_INT) {
    result = 1;
  } else {
    int64_t value = other->get_int();
    result = (a == value) ? 0 : (a > value ? 1 : -1);
  }
  return ret;
}

int ObJsonBaseUtil::number_to_uint(number::ObNumber &nmb, uint64_t &value)
{
  INIT_SUCC(ret);

  int64_t i = 0;
  uint64_t ui = 0;
  uint64_t val = 0;
  
  if (OB_UNLIKELY(!nmb.is_integer() && OB_FAIL(nmb.round(0)))) {
  } else if (nmb.is_valid_int64(i)) {
    val = static_cast<uint64_t>(i);
  } else if (nmb.is_valid_uint64(ui)) {
    val = ui;
  } else {
    val = UINT64_MAX;
  }

  if (OB_SUCC(ret)) {
    value = val;
  }

  return ret;
}

int ObJsonBaseUtil::double_to_uint(double d, uint64_t &value, bool check_range)
{
  INIT_SUCC(ret);

  if (d <= LLONG_MIN) {
    if (check_range) {
      ret = OB_OPERATE_OVERFLOW;
    } else {
      value = LLONG_MIN;
    }
  } else if (d >= static_cast<double>(ULLONG_MAX)) {
    if (check_range) {
      ret = OB_OPERATE_OVERFLOW;
    } else {
      value = ULLONG_MAX;
    }
  } else {
    value = static_cast<uint64_t>(rint(d));
  }

  return ret;
}

int ObJsonBaseUtil::get_bit_len(const ObString &str, int32_t &bit_len)
{
  INIT_SUCC(ret);

  if (str.empty()) {
    bit_len = 1;
  } else {
    const char *ptr = str.ptr();
    uint32_t uneven_value = reinterpret_cast<const unsigned char&>(ptr[0]);
    if (0 == uneven_value) {
      bit_len = 1;
    } else {
      // Built-in Function: int __builtin_clz (unsigned int x).
      // Returns the number of leading 0-bits in x, starting at the most significant bit position.
      // If x is 0, the result is undefined.
      int32_t len = str.length();
      int32_t uneven_len = static_cast<int32_t>(
          sizeof(unsigned int) * 8 - __builtin_clz(uneven_value));
      bit_len = uneven_len + 8 * (len - 1);
    }
  }

  return ret;
}

int32_t ObJsonBaseUtil::get_bit_len(uint64_t value)
{
  int32_t bit_len = 0;
  if (0 == value) {
    bit_len = 1;
  } else {
    bit_len = static_cast<int32_t>(sizeof(unsigned long long) * 8 - __builtin_clzll(value));
  }
  return bit_len;
}


uint64_t ObJsonBaseUtil::hex_to_uint64(const ObString &str)
{
  int32_t N = str.length();
  const uint8_t *p = reinterpret_cast<const uint8_t*>(str.ptr());
  uint64_t value = 0;

  if (OB_LIKELY(NULL != p)) {
    for (int32_t i = 0; i < N; ++i, ++p) {
      value = value * (UINT8_MAX + 1) + *p;
    }
  }

  return value;
}

int ObJsonBaseUtil::string_to_bit(ObString &str, uint64_t &value)
{
  INIT_SUCC(ret);

  uint64_t bit;
  int32_t bit_len = 0;

  if (OB_FAIL(ObJsonBaseUtil::get_bit_len(str, bit_len))) {
  } else if (bit_len > OB_MAX_BIT_LENGTH) {
    bit = UINT64_MAX;
    ret = OB_ERR_DATA_TOO_LONG;
    LOG_WARN("bit len too long", K(bit_len));
  } else {
    bit = ObJsonBaseUtil::hex_to_uint64(str);
  }

  if (OB_SUCC(ret)) {
    value = bit;
  }

  return ret;
}

int ObJsonBaseUtil::str_low_to_up(ObIAllocator* allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  const ObString::obstr_size_t src_len = src.length();
  const char *src_ptr = src.ptr();
  char *dst_ptr = NULL;
  void *ptr = NULL;
  char letter='\0';
  if (OB_ISNULL(src_ptr) || OB_UNLIKELY(0 >= src_len)) {
    dst.assign(NULL, 0);
  } else if (NULL == (ptr = allocator->alloc(src_len))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LIB_LOG(ERROR, "allocate memory failed", K(ret), "size", src_len);
  } else {
    dst_ptr = static_cast<char *>(ptr);
    for(ObString::obstr_size_t i = 0; i < src_len; ++i) {
      letter = src_ptr[i];
      if (letter >= 'a' && letter <= 'z') {
        dst_ptr[i] = static_cast<char>(letter - 32);
      } else{
        dst_ptr[i] = letter;
      }
    }
    dst.assign_ptr(dst_ptr, src_len);
  }
  return ret;
}

int ObJsonBaseUtil::str_up_to_low(ObIAllocator* allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  const ObString::obstr_size_t src_len = src.length();
  const char *src_ptr = src.ptr();
  char *dst_ptr = NULL;
  void *ptr = NULL;
  char letter='\0';
  if (OB_ISNULL(src_ptr) || OB_UNLIKELY(0 >= src_len)) {
    dst.assign(NULL, 0);
  } else if (NULL == (ptr = allocator->alloc(src_len))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LIB_LOG(ERROR, "allocate memory failed", K(ret), "size", src_len);
  } else {
    dst_ptr = static_cast<char *>(ptr);
    for(ObString::obstr_size_t i = 0; i < src_len; ++i) {
      letter = src_ptr[i];
      if (letter >= 'A' && letter <= 'Z') {
        dst_ptr[i] = static_cast<char>(letter + 32);
      } else{
        dst_ptr[i] = letter;
      }
    }
    dst.assign_ptr(dst_ptr, src_len);
  }
  return ret;
}

int ObJsonBaseUtil::check_json_schema_ref_def(ObIAllocator& allocator, ObIJsonBase* json_doc)
{
  int ret = OB_SUCCESS;
  ObString binary;
  if (OB_ISNULL(json_doc)) {
    ret = OB_ERR_NULL_VALUE;
  } else if (OB_FAIL(json_doc->get_raw_binary(binary, &allocator))) {
  } else {
    const json::BoolResult result = json::check_schema_references(rust::Slice<const uint8_t>(
        reinterpret_cast<const uint8_t *>(binary.ptr()), binary.length()));
    if (result.status != json::Status::Ok) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!result.value) {
      ret = OB_ERR_UNSUPPROTED_REF_IN_JSON_SCHEMA;
    }
  }
  return ret;
}

JsonObjectIterator::JsonObjectIterator(const ObIJsonBase *json_doc)
    : curr_element_(0),
      json_object_(json_doc)
{
  // only object json_docs can construct JsonObjectIterator
  element_count_ = json_object_->element_count();
}


bool JsonObjectIterator::end() const
{
  return curr_element_ >= element_count_;
}

int JsonObjectIterator::get_elem(ObJsonObjPair &elem)
{
  INIT_SUCC(ret);
  if (OB_FAIL(json_object_->get_key(curr_element_, elem.first))) {
  } else if (OB_FAIL(json_object_->get_object_value(curr_element_, elem.second))) {
  }
  return ret;
}

int JsonObjectIterator::get_key(ObString &key)
{
  INIT_SUCC(ret);
  if (OB_FAIL(json_object_->get_key(curr_element_, key))) {
  }
  return ret;
}

int JsonObjectIterator::get_value(ObIJsonBase *&value)
{
  INIT_SUCC(ret);
  if (OB_FAIL(json_object_->get_object_value(curr_element_, value))) {
  }
  return ret;
}

int JsonObjectIterator::get_value(ObString &key, ObIJsonBase *&value)
{
  INIT_SUCC(ret);
  if (OB_FAIL(json_object_->get_object_value(key, value))) {
  }
  return ret;
}

void JsonObjectIterator::next()
{
  curr_element_++;
}


} // namespace common
} // namespace oceanbase
