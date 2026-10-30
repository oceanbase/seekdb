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

#define USING_LOG_PREFIX COMMON

#ifdef _WIN32
#include "lib/utility/alloc_assist.h"
#define memmem ob_memmem
#endif
#include "ob_obj_cast.h"
#include <type_traits>
#include "common/object/ob_cmp_op.h" // ObCmpOp and CO_* operators are shared metadata.
#include "share/datum/datum_value_cast.h"
#include "share/datum/typed_datum_compare.h"
#include "share/object/datum_cast_frame.h"
#include "common/enumset/ob_enum_set_meta.h"
#include "common/json_type/ob_json_bin.h"
#include "common/json_type/ob_json_parse.h"  // ObJsonParser, previously hidden behind the exec_ctx include chain(free within lib)
#include "share/object/ob_obj_cast_util.h"
#include "share/object/ob_obj_cast_runtime.h"
#include "share/ob_lob_access_utils.h"
#include "share/ob_json_access_utils.h"
#include "share/geo/ob_srs_provider.h"
#include "share/object/ob_array_cast.h"
#include "share/object/ob_enumset_str_util.h"
#include "share/geo/ob_geometry_cast.h"
#include "share/object/ob_decint_scale_util.h"

// from sql_parser_base.h
#define DEFAULT_STR_LENGTH -1

#define NEED_SCALE_DECIMAL_INT(in_obj, in_scale, res_acc)                                          \
  common::decint_scale::need_scale(                                                                \
    in_scale, (in_obj).get_int_bytes(), (res_acc).get_scale(),                                     \
    wide::ObDecimalIntConstValue::get_int_bytes_by_precision((res_acc).get_precision()))

#define DO_SCALE_DECIMAL_INT(in_obj, in_scale, out_obj)                                            \
  do {                                                                                             \
    ObDecimalIntBuilder tmp_dec;                                                                   \
    std::remove_reference_t<decltype(out_obj)> tmp_obj;                                                                                 \
    ObAccuracy res_acc = *params.res_accuracy_;                                                    \
    char *buf = nullptr;                                                                           \
    int64_t pos = 0;                                                                               \
    if (OB_FAIL(common::decint_scale::scale_decimalint(                                            \
          (in_obj).get_decimal_int(), (in_obj).get_int_bytes(), in_scale,                          \
          res_acc.get_scale(), res_acc.get_precision(), params.cast_mode_, tmp_dec,                \
          params.runtime_))) {                                                                     \
      LOG_WARN("common scale decimal int failed", K(ret));                                         \
    } else if (OB_ISNULL(buf = (char *)params.alloc(tmp_dec.get_int_bytes()))) {                   \
      ret = OB_ALLOCATE_MEMORY_FAILED;                                                             \
      LOG_WARN("failed to allocate memory", K(ret));                                               \
    } else {                                                                                       \
      tmp_obj.set_decimal_int(tmp_dec.get_int_bytes(), params.res_accuracy_->get_scale(),          \
                              const_cast<ObDecimalInt *>(tmp_dec.get_decimal_int()));              \
      ret = (out_obj).deep_copy(tmp_obj, buf, tmp_obj.get_deep_copy_size(), pos);                  \
    }                                                                                              \
  } while (0)

namespace oceanbase
{
using namespace lib;
namespace common
{
using namespace number;

static int init_lob_text_iter(ObTextStringIter &iter,
                              DatumCastParams &params)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(params.lob_read_service_)) {
    ret = iter.init(0, nullptr, params.allocator_v2_);
  } else {
    const ObLobReadOptions options(*params.lob_read_service_);
    ret = iter.init(0, &options, params.allocator_v2_);
  }
  return ret;
}

template <typename Value>
static int read_real_lob_data(DatumCastParams &params, const Value &value, ObString &data)
{
  int ret = OB_SUCCESS;
  data = value.get_string();
  const ObLobCommon *lob = reinterpret_cast<const ObLobCommon *>(data.ptr());
  if (value.get_meta().is_null()) {
    data.reset();
  } else if (!value.is_lob_storage()) {
  } else if (value.has_lob_header() && data.length() >= sizeof(ObLobCommon)
             && !lob->is_mem_loc_ && lob->in_row_) {
    data.assign_ptr(lob->get_inrow_data_ptr(),
        static_cast<int32_t>(lob->get_byte_size(data.length())));
  } else if (params.lob_read_service_ == nullptr) {
    ret = lob_helper::read_real_string_data(params.allocator_v2_, value.get_type(),
        value.get_collation_type(), value.has_lob_header(), data, nullptr);
  } else {
    const ObLobReadOptions options(*params.lob_read_service_);
    ret = lob_helper::read_real_string_data(params.allocator_v2_, value.get_type(),
        value.get_collation_type(), value.has_lob_header(), data, &options);
  }
  return ret;
}

static const int64_t MAX_DOUBLE_STRICT_PRINT_SIZE = 512;

// SqlValueType values intentionally match the parser's literal type tags.  Keep
// the historical user-error text here without introducing a Share -> Query
// dependency on sql/parser/type_name.c.
static const char *legacy_cast_error_type_name(const SqlValueType type)
{
  static const char *TYPE_NAMES[] = {
    "T_NULL", "T_TINYINT", "T_SMALLINT", "T_MEDIUMINT", "T_INT32", "T_INT",
    "T_UTINYINT", "T_USMALLINT", "T_UMEDIUMINT", "T_UINT32", "T_UINT64",
    "T_FLOAT", "T_DOUBLE", "T_UFLOAT", "T_UDOUBLE", "T_NUMBER", "T_UNUMBER",
    "T_DATETIME", "T_TIMESTAMP", "T_DATE", "T_TIME", "T_YEAR", "T_VARCHAR",
    "T_CHAR", "T_HEX_STRING", "T_EXTEND", "T_QUESTIONMARK", "T_TINYTEXT",
    "T_TEXT", "T_MEDIUMTEXT", "T_LONGTEXT", "T_BIT", "T_ENUM", "T_SET"
  };
  const int64_t type_idx = static_cast<int64_t>(type);
  const char *type_name = "Unknown";
  if (type_idx >= 0 && type_idx < ARRAYSIZEOF(TYPE_NAMES)) {
    type_name = TYPE_NAMES[type_idx];
  } else if (ObJsonType == type) {
    type_name = "T_JSON";
  } else if (ObGeometryType == type) {
    type_name = "T_GEOMETRY";
  } else if (ObUserDefinedSQLType == type) {
    type_name = "T_RESERVED_TYPE_38";
  } else if (ObCollectionSQLType == type) {
    type_name = "T_COLLECTION";
  }
  return type_name;
}

static ObDateSqlMode get_date_sql_mode(const ObCastMode cast_mode) {
  ObDateSqlMode date_sql_mode;
  date_sql_mode.allow_invalid_dates_ =
        CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_ALLOW_INVALID_DATES(cast_mode);
  date_sql_mode.no_zero_date_ =
        CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_DATE(cast_mode);
  date_sql_mode.no_zero_in_date_ =
        CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_IN_DATE(cast_mode);
  return date_sql_mode;
}











////////////////////////////////////////////////////////////////
// Utility func

// %align_offset is used in mysql mode to align blob to other charset.
template <typename Value>
static int copy_string(const DatumCastParams &params,
                       const SqlValueType type,
                       const char *str,
                       int64_t len,
                       Value &obj,
                       int64_t align_offset = 0)
{
  int ret = OB_SUCCESS;
  char *buf = NULL;
  if (OB_LIKELY(len > 0 && NULL != str)) {
    if (OB_LIKELY(NULL != params.zf_info_) && params.zf_info_->need_zerofill_) {
      int64_t str_len = std::max(len, static_cast<int64_t>(params.zf_info_->max_length_));
      if (OB_UNLIKELY(NULL == (buf = static_cast<char*>(params.alloc(str_len))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        /* If zero fill is needed, add 0 here */
        int64_t zf = params.zf_info_->max_length_ - len;
        if (zf > 0) {
          MEMSET(buf, '0', zf);
          MEMCPY(buf + zf, str, len);
          len = str_len; // set string length
        } else {
          MEMCPY(buf, str, len);
        }
      }
    } else {
      len += align_offset;
      if (OB_UNLIKELY(NULL == (buf = static_cast<char*>(params.alloc(len))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMSET(buf, 0, align_offset);
        MEMCPY(buf + align_offset, str, len - align_offset);
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (ob_is_text_tc(type)) {
      obj.set_lob_value(type, buf, static_cast<int32_t>(len));
    } else {
      obj.set_string(type, buf, static_cast<int32_t>(len));
    }
  }
  return ret;
}

template <typename Value>
static int copy_string(const DatumCastParams &params,
                       const SqlValueType type,
                       const ObString &str,
                       Value &obj,
                       int64_t align_offset = 0)
{
  return copy_string(params, type, str.ptr(), str.length(), obj, align_offset);
}

/*
 * check err when a string cast to int/uint/double/float according to endptr
 *@str input string
 *@endptr result pointer to end of converted string
 *@len length of str
 *@err
 * incorrect -> "a232"->int or "a2323.23"->double
 * truncated -> "23as" -> int or "as2332.a"->double
 * @note
 *  This is called after one has called strntoull10rnd() or strntod function.
 */
int check_convert_str_err(const char *str,
                          const char *endptr,
                          const int32_t len,
                          const int err,
                          const ObCollationType &in_cs_type)
{
  int ret = OB_SUCCESS;
  // 1. only one of str and endptr is null, it is invalid input.
  if ((OB_ISNULL(str) || OB_ISNULL(endptr)) && str != endptr) {
    ret = OB_ERR_UNEXPECTED;
  } else
  // 2. str == endptr include NULL == NULL.
  if (OB_UNLIKELY(str == endptr) || OB_UNLIKELY(EDOM == err)) {
    ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD; //1366
  } else {
    if (CS_TYPE_BINARY == in_cs_type) {
    } else {
      endptr += ObCharset::scan_str(endptr, str + len, OB_SEQ_SPACES);
    }
    // 3. so here we are sure that both str and endptr are not NULL.
    if (endptr < str + len) {
      ret = OB_ERR_DATA_TRUNCATED; //1265
    }
  }
  return ret;
}

static int convert_string_collation(const ObString &in,
                                    const ObCollationType in_collation,
                                    ObString &out,
                                    const ObCollationType out_collation,
                                    DatumCastParams &params)
{
  int ret = OB_SUCCESS;
  if (!ObCharset::is_valid_collation(in_collation)
      || !ObCharset::is_valid_collation(out_collation)
      || ObCharset::charset_type_by_coll(in_collation) == CHARSET_BINARY
      || ObCharset::charset_type_by_coll(out_collation) == CHARSET_BINARY
      || (ObCharset::charset_type_by_coll(in_collation)
          == ObCharset::charset_type_by_coll(out_collation))) {
    out = in;
  } else if (in.empty()) {
    out.reset();
  } else {
    char *buf = NULL;
    int32_t buf_len = in.length() * ObCharset::CharConvertFactorNum;
    uint32_t result_len = 0;
    if (OB_ISNULL(buf = static_cast<char*>(params.alloc(buf_len)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("alloc memory failed", K(ret));
    } else if (OB_FAIL(ObCharset::charset_convert(in_collation,
                                                  in.ptr(),
                                                  in.length(),
                                                  out_collation,
                                                  buf,
                                                  buf_len,
                                                  result_len))) {
    } else {
      out.assign_ptr(buf, result_len);
    }
  }
  LOG_DEBUG("convert_string_collation",
            K(in.length()), K(in_collation), K(out.length()), K(out_collation));
  return ret;
}

////////////////////////////////////////////////////////////////

OB_INLINE int get_cast_ret(const ObCastMode cast_mode,
                           int ret,
                           int &warning)
{
  //compatibility for old ob
  if (OB_UNLIKELY(OB_ERR_UNEXPECTED_TZ_TRANSITION == ret)
      || OB_UNLIKELY(OB_ERR_UNKNOWN_TIME_ZONE == ret)) {
    ret = OB_INVALID_DATE_VALUE;
  } else if (OB_SUCCESS != ret &&
  //OB_ERR_UNEXPECTED != ret &&
      CM_IS_WARN_ON_FAIL(cast_mode)) {
    warning = ret;
    ret = OB_SUCCESS;
  }
  return ret;
}

#define CAST_FAIL(stmt) \
  (OB_UNLIKELY((OB_SUCCESS != (ret = get_cast_ret(cast_mode, (stmt), params.warning_)))))

#define CAST_RET(stmt) \
  (ret = get_cast_ret(cast_mode, (stmt), params.warning_))

#define SET_RES_OBJ(res, func_val, obj_type, comma, val, zero_val)        \
  do {                                                                    \
    if (OB_SUCC(ret)) {                                                   \
      if (OB_SUCCESS == params.warning_                                   \
          || OB_ERR_TRUNCATED_WRONG_VALUE == params.warning_              \
          || OB_DATA_OUT_OF_RANGE == params.warning_                      \
          || OB_ERR_DATA_TRUNCATED == params.warning_                     \
          || OB_ERR_DOUBLE_TRUNCATED == params.warning_                   \
          || OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD == params.warning_) { \
        res.set_##func_val(obj_type comma val);                           \
      } else if (CM_IS_ZERO_ON_WARN(cast_mode)) {                         \
        res.set_##func_val(obj_type comma zero_val);                      \
      } else {                                                            \
        res.set_null();                                                   \
      }                                                                   \
    } else {                                                              \
      res.set_##func_val(obj_type comma val);                             \
    }                                                                     \
  } while (0)

#define COMMA ,
#define SET_RES_INT(res)          SET_RES_OBJ(res, int, expect_type, COMMA, value, 0)
#define SET_RES_UINT(res)         SET_RES_OBJ(res, uint, expect_type, COMMA, value, 0)
#define SET_RES_FLOAT(res)        SET_RES_OBJ(res, float, expect_type, COMMA, value, 0.0)
#define SET_RES_DOUBLE(res)       SET_RES_OBJ(res, double, expect_type, COMMA, value, 0.0)
#define SET_RES_NUMBER(res)       SET_RES_OBJ(res, number, expect_type, COMMA, value, (value.set_zero(), value))
#define SET_RES_DATETIME(res)     SET_RES_OBJ(res, datetime, expect_type, COMMA, value, ObTimeConverter::ZERO_DATETIME)
#define SET_RES_MDATETIME(res)     SET_RES_OBJ(res, mysql_datetime, , , mdt_value, ObTimeConverter::MYSQL_ZERO_DATETIME)
#define SET_RES_DATE(res)         SET_RES_OBJ(res, date, , , value, ObTimeConverter::ZERO_DATE)
#define SET_RES_MDATE(res)         SET_RES_OBJ(res, mysql_date, , , md_value, ObTimeConverter::MYSQL_ZERO_DATE)
#define SET_RES_TIME(res)         SET_RES_OBJ(res, time, , , value, ObTimeConverter::ZERO_TIME)
#define SET_RES_YEAR(res)         SET_RES_OBJ(res, year, , , value, ObTimeConverter::ZERO_YEAR)
#define SET_RES_BIT(res)          SET_RES_OBJ(res, bit, , , value, 0)
#define SET_RES_ENUM(res)         SET_RES_OBJ(res, enum, , , value, 0)
#define SET_RES_SET(res)          SET_RES_OBJ(res, set, , , value, 0)
#define SET_RES_OTIMESTAMP(res)   SET_RES_OBJ(res, otimestamp_value, expect_type, COMMA, value, ObOTimestampData())
// Add matching SET_RES_XXXTYPE handling when introducing a new cast target.


#define SET_RES_ACCURACY(res_precision, res_scale, res_length) \
  if (params.res_accuracy_ != NULL && OB_SUCCESS == ret) {\
    params.res_accuracy_->set_scale(res_scale);\
    params.res_accuracy_->set_precision(res_precision);\
    params.res_accuracy_->set_length(static_cast<int32_t>(res_length));\
  }

#define SET_RES_ACCURACY_STRING(type, res_precision, res_length) \
  if (params.res_accuracy_ != NULL && OB_SUCCESS == ret) {\
    params.res_accuracy_->set_precision(res_precision);\
    params.res_accuracy_->set_length(res_length);\
    if (ob_is_text_tc(type)) { \
      params.res_accuracy_->set_scale(DEFAULT_SCALE_FOR_TEXT);\
    } else {\
      params.res_accuracy_->set_scale(DEFAULT_SCALE_FOR_STRING);\
    }\
  }
////////////////////////////////////////////////////////////////
// range check function templates.


////////////////////////////////////////////////////////////////////////////////////////////////////

ObNumber ObNumberConstValue::MYSQL_MIN[ObNumber::MAX_PRECISION + 1][ObNumber::MAX_SCALE + 1] = {};
ObNumber ObNumberConstValue::MYSQL_MAX[ObNumber::MAX_PRECISION + 1][ObNumber::MAX_SCALE + 1] = {};
ObNumber ObNumberConstValue::MYSQL_CHECK_MIN[ObNumber::MAX_PRECISION + 1][ObNumber::MAX_SCALE + 1] = {};
ObNumber ObNumberConstValue::MYSQL_CHECK_MAX[ObNumber::MAX_PRECISION + 1][ObNumber::MAX_SCALE + 1] = {};

int ObNumberConstValue::init(ObIAllocator &allocator)
{
  int ret = OB_SUCCESS;
  int64_t total_alloc_size = 0;
  const int64_t BUFFER_SIZE = 2 * (ObNumber::MAX_SCALE + ObNumber::MAX_PRECISION);
  // The character array is used for character storage, the setting of the null terminator '\0' needs to be done by the caller. The logic below has been reviewed and ensures the setting of '\0'.
  // For performance optimization, the {0} operation is no longer enforced
  char buf[BUFFER_SIZE];
  buf[BUFFER_SIZE-1] = '\0';
  // prepare string like "99.999".
  int pos = 0;
  buf[pos++] = '-';

  {
    for (int16_t precision = OB_MIN_DECIMAL_PRECISION; OB_SUCC(ret) && precision <= ObNumber::MAX_PRECISION; ++precision) {
      for (int16_t scale = 0; OB_SUCC(ret) && precision >= scale && scale <= ObNumber::MAX_SCALE; ++scale) {
        pos = 1;
        MEMSET(buf + pos, 0, BUFFER_SIZE - pos);
        if (precision == scale) {
          buf[pos++] = '0';
        }
        MEMSET(buf + pos, '9', precision + 1);
        buf[pos + precision + 1] = '5';
        buf[pos + precision - scale] = '.';
        // make min and max numbers.
        ObNumber &min_check_num = MYSQL_CHECK_MIN[precision][scale];
        ObNumber &max_check_num = MYSQL_CHECK_MAX[precision][scale];
        ObString tmp_string(pos + precision + 1 + 1, buf);
        if (OB_FAIL(min_check_num.from(tmp_string.ptr(), tmp_string.length(), allocator))) {
        } else if (OB_FAIL(max_check_num.from(tmp_string.ptr() + 1, tmp_string.length() - 1, allocator))) {
        } else {
          total_alloc_size += sizeof(uint32_t) * (min_check_num.get_length() + max_check_num.get_length());
        }

        if (OB_SUCC(ret)) {
          buf[pos + precision + 1] = '0';
          // make min and max numbers.
          ObNumber &min_num = MYSQL_MIN[precision][scale];
          ObNumber &max_num = MYSQL_MAX[precision][scale];
          if (OB_FAIL(min_num.from(tmp_string.ptr(), tmp_string.length(), allocator))) {
          } else if (OB_FAIL(max_num.from(tmp_string.ptr() + 1, tmp_string.length() - 1, allocator))) {
          } else {
            total_alloc_size += sizeof(uint32_t) * (min_num.get_length() + max_num.get_length());
          }
        }
      }
    }
  }

  return ret;
}

// Type is float/double
template <typename Type>
int real_range_check_only(const ObAccuracy &accuracy, Type value)
{
  int ret = OB_SUCCESS;
  const ObPrecision precision = accuracy.get_precision();
  const ObScale scale = accuracy.get_scale();
  if (OB_LIKELY(precision > 0) &&
      OB_LIKELY(scale >= 0) &&
      OB_LIKELY(precision >= scale)) {
    Type integer_part = static_cast<Type>(pow(10.0, static_cast<double>(precision - scale)));
    Type decimal_part = static_cast<Type>(pow(10.0, static_cast<double>(scale)));
    Type max_value = integer_part - 1 / decimal_part;
    Type min_value = -max_value;
    if (OB_FAIL(numeric_range_check(value, min_value, max_value, value))) {
    }
  }
  return ret;
}

#define BOUND_INFO_START_POS 18

template<typename T>
ObPrecision get_precision_for_integer(T value)
{
  static const uint64_t bound_info[] = {
      INT64_MAX + 1ULL,
      999999999999999999ULL,
      99999999999999999ULL,
      9999999999999999ULL,
      999999999999999ULL,
      99999999999999ULL,
      9999999999999ULL,
      999999999999ULL,
      99999999999ULL,
      9999999999ULL,
      999999999ULL,
      99999999ULL,
      9999999ULL,
      999999ULL,
      99999ULL,
      9999ULL,
      999ULL,
      99ULL,
      9ULL,
      99ULL,
      999ULL,
      9999ULL,
      99999ULL,
      999999ULL,
      9999999ULL,
      99999999ULL,
      999999999ULL,
      9999999999ULL,
      99999999999ULL,
      999999999999ULL,
      9999999999999ULL,
      99999999999999ULL,
      999999999999999ULL,
      9999999999999999ULL,
      99999999999999999ULL,
      999999999999999999ULL,
      9999999999999999999ULL,
      UINT64_MAX,
    };
  int64_t flag = std::less<T>()(value, 0) ? -1 : 1;
  uint64_t abs_value = value * flag;
  const uint64_t *iter = bound_info + BOUND_INFO_START_POS;
  while(abs_value > *iter) {
    iter += flag;
  }
  // *(iter - 1) < abs <= *iter
  return static_cast<ObPrecision>((iter - (bound_info + BOUND_INFO_START_POS)) * flag + 1);
}

template <typename Value>
static int check_convert_string(const SqlValueType expect_type, DatumCastParams &params,
    const Value &in, Value &out)
{
  int ret = OB_SUCCESS;
  out = in;
  return ret;
}

template <typename Value>
static int check_convert_string(const SqlValueType expect_type, DatumCastParams &params,
    const ObString &in_string, Value &out)
{
  Value tmp_obj;
  tmp_obj.set_varchar(in_string);
  return check_convert_string(expect_type, params, tmp_obj, out);
}

template <typename Value>
static int check_convert_string(const SqlValueType expect_type, DatumCastParams &params,
    const char *in_str, const int64_t len, Value &out)
{
  Value tmp_obj;
  tmp_obj.set_varchar(in_str, static_cast<ObString::obstr_size_t>(len));
  return check_convert_string(expect_type, params, tmp_obj, out);
}

template <typename Value>
class CastTextResult : public ObTextStringResult
{
public:
  CastTextResult(SqlValueType type, DatumCastParams *params, Value *result, bool header)
      : ObTextStringResult(type, header, nullptr), params_(params), result_(result) {}
  int init(int64_t length, ObIAllocator *allocator = nullptr) override {
    if (is_init_) { return OB_SUCCESS; }
    if (result_ == nullptr) { return OB_INVALID_ARGUMENT; }
    return init_cast_buffer(length, params_, allocator);
  }
  void set_result() {
    result_->set_lob_value(type_, buffer_, pos_);
    if (is_lob_storage(type_) && has_lob_header_) { result_->set_has_lob_header(); }
  }
private:
  DatumCastParams *params_;
  Value *result_;
};

template <typename Value>
static int set_json_bin_res(DatumCastParams *params, Value *obj, const ObString &raw_bin)
{
  int ret = OB_SUCCESS;
  bool has_lob_header = true;
  CastTextResult<Value> text_result(ObJsonType, params, obj, has_lob_header);
  if (OB_FAIL(text_result.init(raw_bin.length(), params->allocator_v2_))) {
  } else if (OB_FAIL(text_result.append(raw_bin.ptr(), raw_bin.length()))) {
  } else {
    text_result.set_result();
  }
  return ret;
}


////////////////////////////////////////////////////////////////
// Int -> XXX

template <typename Value>
static int int_int(const SqlValueType expect_type, DatumCastParams &params, const Value &in,
                   Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = in.get_int();
    if (in.get_type() > expect_type && CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      out.set_int(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int int_uint(const SqlValueType expect_type, DatumCastParams &params, const Value &in,
                    Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (CM_SKIP_CAST_INT_UINT(cast_mode)) {
    out = in;
    res_precision = get_precision_for_integer(out.get_uint64());
  } else {
    uint64_t value = static_cast<uint64_t>(in.get_int());
    if (CM_NEED_RANGE_CHECK(cast_mode)
        && CAST_FAIL(uint_range_check(expect_type, in.get_int(), value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(out.get_uint64());
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int int_float(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    double value = static_cast<double>(in.get_int());
    if (ObUFloatType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
    } else {
      out.set_float(expect_type, static_cast<float>(value));
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int int_double(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    double value = static_cast<double>(in.get_int());
    if (ObUDoubleType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
    } else {
      out.set_double(expect_type, value);
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int int_number(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = in.get_int();
    number::ObNumber nmb;
    if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
    } else if (OB_FAIL(nmb.from(value, params))) {
    } else {
      out.set_number(expect_type, nmb);
      res_precision = get_precision_for_integer(in.get_int());
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int int_datetime(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    int64_t value = 0;
    if (in.get_int() < 0) {
      ret = OB_INVALID_DATE_FORMAT;
    } else {
      ObDateSqlMode date_sql_mode;
      date_sql_mode.allow_invalid_dates_ = CM_IS_EXPLICIT_CAST(cast_mode)
                                           ? false : CM_IS_ALLOW_INVALID_DATES(cast_mode);
      date_sql_mode.no_zero_date_ = CM_IS_EXPLICIT_CAST(cast_mode)
                                    ? false : CM_IS_NO_ZERO_DATE(cast_mode);
      ret = ObTimeConverter::int_to_datetime(in.get_int(), 0, cvrt_ctx, value, date_sql_mode);
    }
    if (CAST_FAIL(ret)) {
    } else {
      SET_RES_DATETIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int int_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  ||  ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    ObMySQLDateTime mdt_value = 0;
    if (in.get_int() < 0) {
      ret = OB_INVALID_DATE_FORMAT;
    } else {
      ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
      ret = ObTimeConverter::int_to_mdatetime(in.get_int(), 0, cvrt_ctx, mdt_value, date_sql_mode);
    }
    if (CAST_FAIL(ret)) {
    } else {
      SET_RES_MDATETIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int int_date(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int32_t value = 0;
    ObDateSqlMode date_sql_mode;
    date_sql_mode.allow_invalid_dates_ = CM_IS_EXPLICIT_CAST(cast_mode)
                                          ? false : CM_IS_ALLOW_INVALID_DATES(cast_mode);
    date_sql_mode.no_zero_date_ = CM_IS_EXPLICIT_CAST(cast_mode)
                                  ? false : CM_IS_NO_ZERO_DATE(cast_mode);
    if (CAST_FAIL(ObTimeConverter::int_to_date(in.get_int(), value, date_sql_mode))) {
    } else {
      SET_RES_DATE(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int int_mdate(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDate md_value = 0;
    ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
    if (CAST_FAIL(ObTimeConverter::int_to_mdate(in.get_int(), md_value, date_sql_mode))) {
    } else {
      SET_RES_MDATE(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int int_time(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = 0;
    if (CAST_FAIL(ObTimeConverter::int_to_time(in.get_int(), value))) {
    } else {
      SET_RES_TIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int int_year(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    uint8_t value = 0;
    if (CAST_FAIL(ObTimeConverter::int_to_year(in.get_int(), value))) {
    } else {
      SET_RES_YEAR(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int int_string(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    Value tmp_out;
    ObFastFormatInt ffi(in.get_int());
    ObString tmp_str;
    // Perform character set conversion, use DatumCastParams.alloc to allocate space for the result.
    if (OB_FAIL(convert_string_collation(ObString(ffi.length(), ffi.ptr()),
                                         ObCharset::get_system_collation(),
                                         tmp_str,
                                         params.dest_collation_,
                                         params))) {
    } else if (OB_FAIL(check_convert_string(expect_type, params, tmp_str.ptr(),
                                            tmp_str.length(), tmp_out))) {
    } else if (OB_FAIL(copy_string(params, expect_type, tmp_out.get_string(), out))) {
    } else {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int int_bit(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  uint64_t value = static_cast<uint64_t>(in.get_int());
  int32_t bit_len = 0;
  if (OB_UNLIKELY(ObIntTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (CM_NEED_RANGE_CHECK(cast_mode)
             && CAST_FAIL(uint_range_check(ObUInt64Type, in.get_int(), value))) {
  } else if (OB_FAIL(get_bit_len(value, bit_len))) {
  } else {
    SET_RES_BIT(out);
    SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
  }
  return ret;
}




#define CAST_TO_TEXT_METHOD(TYPE, TYPE_CLASS)                                                 \
  template <typename Value>                                                               \
  static int TYPE##_text(const SqlValueType expect_type, DatumCastParams &params,                \
                         const Value &in, Value &out, const ObCastMode cast_mode)             \
  {                                                                                           \
    int ret = OB_SUCCESS;                                                                     \
    Value tmp_val;                                                                            \
    if (OB_UNLIKELY(TYPE_CLASS != in.get_type_class()                                         \
                    || ObTextTC != sql_type_class(expect_type))) {                         \
      ret = OB_ERR_UNEXPECTED;                                                                \
      LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));                    \
    } else if (OB_FAIL(TYPE##_string(ObVarcharType, params, in, tmp_val, cast_mode))) {       \
      LOG_WARN("fail to cast" #TYPE "to string", K(ret), K(expect_type), K(in), K(tmp_val));  \
    } else if (OB_FAIL(string_text(expect_type, params, tmp_val, out, cast_mode))) {               \
      LOG_WARN("fail to cast string to lob", K(ret), K(expect_type), K(in), K(tmp_val));      \
    }                                                                                         \
    return ret;                                                                               \
  }

template <typename Value>
static int string_text(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode,
                       const ObLobLocatorV2 *lob_locator);
template <typename Value>
static int string_text(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  return string_text(expect_type, params, in, out, cast_mode, NULL);
}

CAST_TO_TEXT_METHOD(int, ObIntTC);

template <typename Value>
static int int_json(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObIntTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    ObJsonInt j_int(in.get_int());
    ObIJsonBase *j_base = &j_int;
    ObString raw_bin;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
    } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
    } else {
      res_length = out.get_val_len();
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}

static int common_get_srs_item(DatumCastParams &params,
                               ObSrsCacheGuard &srs_guard,
                               const ObString &wkb,
                               const ObSrsItem *&srs,
                               const char *cast_name)
{
  int ret = OB_SUCCESS;
  uint32_t srid = UINT32_MAX;
  if (wkb.length() < WKB_GEO_SRID_SIZE) {
    ret = OB_ERR_GIS_INVALID_DATA;
    if (OB_NOT_NULL(cast_name)) {
      LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, cast_name);
    }
  } else if (FALSE_IT(srid = ObGeoWkbByteOrderUtil::read<uint32_t>((const char *)wkb.ptr(),
      ObGeoWkbByteOrder::LittleEndian))) { // adapt mysql, always use little-endian
    // do nothing
  } else if (0 == srid) {
    // do nothing
  } else if (OB_ISNULL(params.srs_provider_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(params.srs_provider_->get_tenant_srs_guard(srs_guard))) {
  } else if (OB_FAIL(srs_guard.get_srs_item(srid, srs))) {
  }
  return ret;
}

static int common_build_geometry(ObIAllocator &allocator,
                                 const ObString &wkb,
                                 ObGeometry *&geo,
                                 const ObSrsItem *srs,
                                 const char *cast_name)
{
  int ret = OB_SUCCESS;
  ObGeoErrLogInfo log_info;
  if (OB_FAIL(ObGeoTypeUtil::build_geometry(allocator, wkb, geo, srs, log_info, ObGeoBuildFlag::GEO_ALLOW_3D_DEFAULT))) {
    if (OB_ERR_GIS_INVALID_DATA == ret && OB_NOT_NULL(cast_name)) {
      LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, cast_name);
    } else if (OB_ERR_GEOMETRY_PARAM_LONGITUDE_OUT_OF_RANGE == ret && OB_NOT_NULL(cast_name)) {
      LOG_USER_ERROR(OB_ERR_GEOMETRY_PARAM_LONGITUDE_OUT_OF_RANGE,
                     cast_name,
                     log_info.value_out_of_range_,
                     log_info.min_long_val_,
                     log_info.max_long_val_);
    } else if (OB_ERR_GEOMETRY_PARAM_LATITUDE_OUT_OF_RANGE == ret && OB_NOT_NULL(cast_name)) {
      LOG_USER_ERROR(OB_ERR_GEOMETRY_PARAM_LATITUDE_OUT_OF_RANGE,
                     cast_name,
                     log_info.value_out_of_range_,
                     log_info.min_lat_val_,
                     log_info.max_lat_val_);
    }
  }
  return ret;
}


////////////////////////////////////////////////////////////////
// UInt -> XXX

template <typename Value>
static int uint_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (CM_IS_EXTERNAL_CALL(cast_mode) && CM_SKIP_CAST_INT_UINT(cast_mode)) {
    out = in;
    res_precision = get_precision_for_integer(out.get_int());
  } else {
    int64_t value = static_cast<int64_t>(in.get_uint64());
    if (CM_NEED_RANGE_CHECK(cast_mode)
        && CAST_FAIL(int_upper_check(expect_type, in.get_uint64(), value))) {
    } else {
      out.set_int(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int uint_uint(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    uint64_t value = in.get_uint64();
    if (in.get_type() > expect_type && CAST_FAIL(uint_upper_check(expect_type, value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int uint_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    // The result of directly converting uint to float is different from converting it to double first and then to float. Here, we maintain compatibility with MySQL.
    double value = static_cast<double>(in.get_uint64());
    out.set_float(expect_type, static_cast<float>(value));
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int uint_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    out.set_double(expect_type, static_cast<double>(in.get_uint64()));
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int uint_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  number::ObNumber nmb;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(nmb.from(in.get_uint64(), params))) {
  } else {
    out.set_number(expect_type, nmb);
    res_precision = get_precision_for_integer(in.get_uint64());
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int uint_datetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(uint_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
  } else if (OB_FAIL(int_datetime(expect_type, params, int64, out, cast_mode))) {
  }
  //has set the accuracy in prev int_datetime call
  return ret;
}

template <typename Value>
static int uint_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(uint_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
  } else if (OB_FAIL(int_mdatetime(expect_type, params, int64, out, cast_mode))) {
  }
  //has set the accuracy in prev int_datetime call
  return ret;
}

template <typename Value>
static int uint_date(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(uint_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
  } else if (OB_FAIL(int_date(expect_type, params, int64, out, cast_mode))) {
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int uint_mdate(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(uint_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
  } else if (OB_FAIL(int_mdate(expect_type, params, int64, out, cast_mode))) {
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int uint_time(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(uint_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
  } else if (OB_FAIL(int_time(expect_type, params, int64, out, cast_mode))) {
  }
  //accuracy has been set in prev int_time call
  return ret;
}

template <typename Value>
static int uint_year(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(uint_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
  } else if (OB_FAIL(int_year(expect_type, params, int64, out, cast_mode))) {
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int uint_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    Value tmp_out;
    ObFastFormatInt ffi(in.get_uint64());
    ObString tmp_str;
    if (OB_FAIL(convert_string_collation(ObString(ffi.length(), ffi.ptr()),
        ObCharset::get_system_collation(), tmp_str, params.dest_collation_, params))) {
    } else if (OB_FAIL(check_convert_string(expect_type, params,
                      tmp_str.ptr(), tmp_str.length(), tmp_out))) {
    } else if (OB_FAIL(copy_string(params, expect_type, tmp_out.get_string(), out))) {
    } else {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }

  UNUSED(cast_mode);
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  return ret;
}

template <typename Value>
static int uint_bit(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  uint64_t value = in.get_uint64();
  int32_t bit_len = 0;
  if (OB_UNLIKELY(ObUIntTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(get_bit_len(value, bit_len))) {
  } else {
    SET_RES_BIT(out);
    SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
  }
  return ret;
}



CAST_TO_TEXT_METHOD(uint, ObUIntTC);

template <typename Value>
static int uint_json(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObUIntTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    ObJsonUint j_uint(in.get_uint64());
    ObIJsonBase *j_base = &j_uint;
    ObString raw_bin;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
    } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
    } else {
      res_length = out.get_val_len();
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}


////////////////////////////////////////////////////////////////
// Float -> XXX

// directly stacit cast from float to int or uint:
// case 1: float = -99999896450971467776.000000, int64 = -9223372036854775808, uint64 = 9223372036854775808.
// case 2: float = 99999896450971467776.000000, int64 = -9223372036854775808, uint64 = 0.
// case 3: float = -99999904.000000, int64 = -99999904, uint64 = 18446744073609551712.
// case 4: float = 99999904.000000, int64 = 99999904, uint64 = 99999904.
// we can see that if float value is out of range of int or uint value, the casted int or uint
// value can't be used to compare with INT64_MAX and so on, see case 2.
// so we should use float value to determine weither it is in range of int or uint.
// trunc_min_value and trunc_max_value are the values that in should be truncated to when it is less than the minimum value and greater than the maximum value, respectively.
// float to int conversion, if exceeds LLONG_MAX should be truncated to LLONG_MIN, while double to int conversion exceeding LLONG_MAX should be truncated to LLONG_MAX
int common_double_int(const double in, int64_t &out,
                      const int64_t trunc_min_value,
                      const int64_t trunc_max_value)
{
  int ret = OB_SUCCESS;
  out = 0;
  if (in <= static_cast<double>(LLONG_MIN)) {
    out = trunc_min_value;
    if (in < static_cast<double>(LLONG_MIN)) {
      ret = OB_DATA_OUT_OF_RANGE;
    }
  } else if (in >= static_cast<double>(LLONG_MAX)) {
    // Include the equal case to handle situations where a floating-point number equal to LLONG_MAX, when converted to int, may result in LLONG_MIN or LLONG_MAX.
    // double to int, and float to int when used as insert value, the result is LLONG_MAX;
    // In other cases, the result of float to int conversion is LLONG_MIN. The behavior without error is also compatible with mysql
    out = trunc_max_value;
    if (in > static_cast<double>(LLONG_MAX)) {
      ret = OB_DATA_OUT_OF_RANGE;
    }
  } else {
    out = static_cast<int64_t>(rint(in));
  }
  return ret;
}

template <typename Value>
static int float_int(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    // The float value equal to LLONG_MAX cast to int should result in LLONG_MAX if it is an insert value, otherwise the cast result should be LLONG_MIN
    if (CAST_FAIL(common_double_int(in.get_float(), value, LLONG_MIN,
                  CM_IS_COLUMN_CONVERT(cast_mode) ? LLONG_MAX : LLONG_MIN))) {
    } else if (CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      out.set_int(expect_type, value);
    }
    if (OB_SUCC(ret)) {
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int float_uint(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t value = 0;
  ObPrecision res_precision = -1;
  double in_value = static_cast<double>(in.get_float());
  bool is_column_convert = CM_IS_COLUMN_CONVERT(cast_mode);
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ret = round_floating_to_uint64(in_value, true, is_column_convert, value);
    if (CAST_FAIL(ret)) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (CM_NEED_RANGE_CHECK(cast_mode) &&
    // Use value instead of in_value for range check, because using in_value would cause the check to always fail when the input is negative,
    // cast result is set to 0. mysql result is a negative float first converted to int, then to uint.
          CAST_FAIL(uint_range_check(expect_type, value, value))) {
  } else {
    out.set_uint(expect_type, value);
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(value);
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int float_float(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  float value = in.get_float();
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (ObUFloatType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    out.set_float(expect_type, value);
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int float_double(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  double value = static_cast<double>(in.get_float());
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (ObUDoubleType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    out.set_double(expect_type, value);
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int float_number(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  float value = in.get_float();
  ObPrecision res_precision = -1;
  ObScale res_scale = -1;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    char buf[MAX_DOUBLE_STRICT_PRINT_SIZE];
    MEMSET(buf, 0, MAX_DOUBLE_STRICT_PRINT_SIZE);
    int64_t length = 0;
    length = ob_gcvt(value, OB_GCVT_ARG_DOUBLE, sizeof(buf) - 1, buf, NULL);
    ObString str(sizeof(buf), static_cast<int32_t>(length), buf);
    number::ObNumber nmb;
    if (OB_FAIL(nmb.from_sci_opt(str.ptr(), str.length(), params, &res_precision, &res_scale))) {
    } else {
      out.set_number(expect_type, nmb);
    }
    LOG_DEBUG("finish float to number", K(ret), K(str), K(length), K(in), K(expect_type), K(nmb), K(out), K(lbt()));
  }
  SET_RES_ACCURACY(res_precision, res_scale, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int double_datetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int float_datetime(const SqlValueType expect_type, DatumCastParams &params,
                          const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(float_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_datetime(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_datetime call
  return ret;
}

template <typename Value>
static int double_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int float_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                          const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(float_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_mdatetime(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_datetime call
  return ret;
}


template <typename Value>
static int double_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int float_date(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(float_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_date(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_date call
  return ret;
}

template <typename Value>
static int double_mdate(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int float_mdate(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(float_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_mdate(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_date call
  return ret;
}

template <typename Value>
static int double_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int float_time(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(float_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_time(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_time call
  return ret;
}

template <typename Value>
static int float_string(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  ObScale scale = in.get_scale();
  int64_t length = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    if (in.get_float() == -INFINITY) {
      length = strlen("-Inf");
      strncpy(buf, "-Inf", length);
    } else if (in.get_float() == INFINITY) {
      length = strlen("Inf");
      strncpy(buf, "Inf", length);
    } else if (isnan(in.get_float())) {
      length = strlen("Nan");
      strncpy(buf, "Nan", length);
    } else {
      if (0 <= scale) {
        length = ob_fcvt(in.get_float(), scale, sizeof(buf) - 1, buf, NULL);
      } else {
        length = ob_gcvt_opt(in.get_float(), OB_GCVT_ARG_FLOAT, static_cast<int32_t>(sizeof(buf) - 1),
                             buf, NULL, TRUE);
      }
    }
    ObString str(sizeof(buf), static_cast<int32_t>(length), buf);
    Value tmp_out;
    ObString tmp_str;
    if (OB_FAIL(convert_string_collation(str, ObCharset::get_system_collation(),
                                         tmp_str, params.dest_collation_, params))) {
    } else if (OB_FAIL(check_convert_string(expect_type, params,
                    tmp_str.ptr(), tmp_str.length(), tmp_out))) {
    } else if (OB_FAIL(copy_string(params, expect_type, tmp_out.get_string(), out))) {
    } else {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  return ret;
}

template <typename Value>
static int float_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  uint64_t value = static_cast<uint64_t>(
      truncate_floating_to_int64_clamped(in.get_float()));
  int32_t bit_len = 0;
  if (OB_UNLIKELY(ObFloatTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(get_bit_len(value, bit_len))) {
  } else {
    SET_RES_BIT(out);
    SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
  }

  return ret;
}



CAST_TO_TEXT_METHOD(float, ObFloatTC);

template <typename Value>
static int float_json(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObFloatTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    ObJsonDouble j_double(in.get_float());
    ObIJsonBase *j_base = &j_double;
    ObString raw_bin;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
    } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
    } else {
      res_length = out.get_val_len();
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}


////////////////////////////////////////////////////////////////
// Double -> XXX

template <typename Value>
static int double_int(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    if (CAST_FAIL(common_double_int(in.get_double(), value, LLONG_MIN, LLONG_MAX))) {
    } else if (CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      out.set_int(expect_type, value);
    }
    if (OB_SUCC(ret)) {
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int double_uint(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t value = 0;
  ObPrecision res_precision = -1;
  double in_value = in.get_double();
  bool is_column_convert = CM_IS_COLUMN_CONVERT(cast_mode);
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ret = round_floating_to_uint64(in_value, false, is_column_convert, value);
    if (CAST_FAIL(ret)) {
    }
  }

  if (OB_FAIL(ret)) {
  } else if (CM_NEED_RANGE_CHECK(cast_mode)
      && CAST_FAIL(uint_range_check(expect_type, value, value))) {
  } else {
    out.set_uint(expect_type, value);
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(value);
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int double_float(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  float value = static_cast<float>(in.get_double());
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(real_range_check(expect_type, in.get_double(), value))) {
  } else {
    out.set_float(expect_type, value);
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int double_double(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  double value = in.get_double();
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (ObUDoubleType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    out.set_double(expect_type, value);
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int double_number(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  double value = in.get_double();
  ObPrecision res_precision = -1;
  ObScale res_scale = -1;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    char buf[MAX_DOUBLE_STRICT_PRINT_SIZE];
    MEMSET(buf, 0, MAX_DOUBLE_STRICT_PRINT_SIZE);
    int64_t length = ob_gcvt_opt(value, OB_GCVT_ARG_DOUBLE, static_cast<int32_t>(sizeof(buf) - 1),
                                 buf, NULL, TRUE);
    ObString str(sizeof(buf), static_cast<int32_t>(length), buf);
    number::ObNumber nmb;
    if (OB_FAIL(nmb.from_sci_opt(str.ptr(), str.length(), params, &res_precision, &res_scale))) {
    } else {
      out.set_number(expect_type, nmb);
    }
    LOG_DEBUG("finish double to number", K(ret), K(str), K(length), K(in), K(expect_type), K(nmb), K(out), K(lbt()));
  }
  SET_RES_ACCURACY(res_precision, res_scale, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}
template <typename Value>
static int number_datetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int double_datetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  // double to datetime must cast to number first, then cast string to datetime.
  // because double 20151016153421.8 may actually 20151016153421.801 in memory,
  // so we will get '2015-10-16 15:34:21.801' instead of '2015-10-16 15:34:21.8'.
  // this problem can be resolved by cast double to number.
  int ret = OB_SUCCESS;
  ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
  Value tmp_num_obj;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(double_number(ObNumberType, params, in, tmp_num_obj, cast_mode))) {
    if (CM_IS_WARN_ON_FAIL(cast_mode)){
      ret = OB_SUCCESS;
      if (CM_IS_ZERO_ON_WARN(cast_mode)) {
        out.set_datetime(expect_type, ObTimeConverter::ZERO_DATETIME);
      } else {
        out.set_null();
      }
    } else {
      ret = OB_INVALID_DATE_VALUE;
    }
  } else if (CAST_FAIL(number_datetime(expect_type, params, tmp_num_obj, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int number_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int double_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  // double to datetime must cast to number first, then cast string to datetime.
  // because double 20151016153421.8 may actually 20151016153421.801 in memory,
  // so we will get '2015-10-16 15:34:21.801' instead of '2015-10-16 15:34:21.8'.
  // this problem can be resolved by cast double to number.
  int ret = OB_SUCCESS;
  Value tmp_num_obj;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(double_number(ObNumberType, params, in, tmp_num_obj, cast_mode))) {
    if (CM_IS_WARN_ON_FAIL(cast_mode)){
      ret = OB_SUCCESS;
      if (CM_IS_ZERO_ON_WARN(cast_mode)) {
        out.set_mysql_datetime(ObTimeConverter::MYSQL_ZERO_DATETIME);
      } else {
        out.set_null();
      }
    } else {
      ret = OB_INVALID_DATE_VALUE;
    }
  } else if (CAST_FAIL(number_mdatetime(expect_type, params, tmp_num_obj, out, cast_mode))) {
  }
  return ret;
}


template <typename Value>
static int double_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  //TODO<xiaoni> In datediff,mysql does truncate NOT round
  //So,we have to revise double 2 int which act as round (right now) to truncation
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(double_int(ObIntType, params, in, int64, cast_mode))) {
  } else if (OB_FAIL(int_date(expect_type, params, int64, out, cast_mode))) {
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int double_mdate(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(double_int(ObIntType, params, in, int64, cast_mode))) {
  } else if (OB_FAIL(int_mdate(expect_type, params, int64, out, cast_mode))) {
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int double_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  // double to time must cast to string first, then cast string to time.
  // see comment in double_datetime.
  int ret = OB_SUCCESS;
  ObScale res_scale = -1;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = 0;
    char buf[MAX_DOUBLE_PRINT_SIZE];
    MEMSET(buf, 0, MAX_DOUBLE_PRINT_SIZE);
    int64_t length = ob_gcvt(in.get_double(), OB_GCVT_ARG_DOUBLE, sizeof(buf) - 1, buf, NULL);
    ObString str(sizeof(buf), static_cast<int32_t>(length), buf);
    if (CAST_FAIL(ObTimeConverter::str_to_time(str, value, &res_scale))) {
    } else {
      SET_RES_TIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int double_year(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    uint8_t value = 0;
    double in_val = in.get_double();
    in_val = in_val < 0 ? INT_MIN : in_val + 0.5;
    uint64_t intvalue = static_cast<uint64_t>(in_val);
    if (CAST_FAIL(ObTimeConverter::int_to_year(intvalue, value))) {
    } else {
      SET_RES_YEAR(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int double_string(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                      && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    ObScale scale = in.get_scale();
    int64_t length = 0;
    if (in.get_double() == -INFINITY) {
      length = strlen("-Inf");
      strncpy(buf, "-Inf", length);
    } else if (in.get_double() == INFINITY) {
      length = strlen("Inf");
      strncpy(buf, "Inf", length);
    } else if (isnan(in.get_double())) {
      length = strlen("Nan");
      strncpy(buf, "Nan", length);
    } else {
      if (0 <= scale) {
        length = ob_fcvt(in.get_double(), scale, sizeof(buf) - 1, buf, NULL);
      } else {
        const int32_t buf_length = static_cast<int32_t>(sizeof(buf) - 1);
        int32_t double_width = buf_length;
        if (CM_IS_COLUMN_CONVERT(cast_mode) && params.dest_max_length_ > 0) {
          double_width = min(double_width, params.dest_max_length_);
        }
        length = ob_gcvt_opt(in.get_double(), OB_GCVT_ARG_DOUBLE,
                             double_width, buf, NULL, TRUE);
        if (length == 0 && double_width < buf_length) {
          length = double_width;
          buf[length] = '\0';
        }
      }
    }
    ObString str(sizeof(buf), static_cast<int32_t>(length), buf);
    Value tmp_out;
    ObString tmp_str;
    if (OB_FAIL(convert_string_collation(str, ObCharset::get_system_collation(),
                                         tmp_str, params.dest_collation_, params))) {
    } else if (OB_FAIL(check_convert_string(expect_type, params, tmp_str, tmp_out))) {
    } else if (OB_FAIL(copy_string(params, expect_type, tmp_out.get_string(), out))) {
    } else {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int double_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  uint64_t value = static_cast<uint64_t>(
      truncate_floating_to_int64_clamped(in.get_double()));
  int32_t bit_len = 0;
  if (OB_UNLIKELY(ObDoubleTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(get_bit_len(value, bit_len))) {
  } else {
    SET_RES_BIT(out);
    SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
  }
  return ret;
}



CAST_TO_TEXT_METHOD(double, ObDoubleTC);

template <typename Value>
static int double_json(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObDoubleTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    ObJsonDouble j_double(in.get_double());
    ObIJsonBase *j_base = &j_double;
    ObString raw_bin;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
    } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
    } else {
      res_length = out.get_val_len();
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}


////////////////////////////////////////////////////////////////
// Number -> XXX

template <typename Value>
static int string_int(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_int(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const number::ObNumber &nmb = in.get_number();
    const char *value = nmb.format();
    if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      Value from;
      from.set_varchar(value, static_cast<ObString::obstr_size_t>(strlen(value)));
      ret = string_int(expect_type, params, from, out,
                       CM_UNSET_STRING_INTEGER_TRUNC(cast_mode));
    }
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(out.get_int());
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int string_uint(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_uint(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const number::ObNumber &nmb = in.get_number();
    const char *value = nmb.format();
    if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      Value from;
      from.set_varchar(value, static_cast<ObString::obstr_size_t>(strlen(value)));
      ret = string_uint(expect_type, params, from, out, CM_UNSET_STRING_INTEGER_TRUNC(cast_mode));
    }
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(out.get_uint64());
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int string_float(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_float(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const char *value = in.get_number().format();
    if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      Value from;
      from.set_varchar(value, static_cast<ObString::obstr_size_t>(strlen(value)));
      ret = string_float(expect_type, params, from, out, cast_mode);
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int string_double(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_double(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const char *value = in.get_number().format();
    if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      Value from;
      from.set_varchar(value, static_cast<ObString::obstr_size_t>(strlen(value)));
      ret = string_double(expect_type, params, from, out, cast_mode);
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int number_number(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const number::ObNumber &nmb = in.get_number();
    number::ObNumber value;
    if (OB_FAIL(value.from(nmb, params))) {
    } else if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
    } else {
      out.set_number(expect_type, value);
    }
  }
  //todo<xiaoni> maybe we can do some dirty work to get better result.
  //for now, just set it to unknown....
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, NUMBER_SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int number_datetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  const int64_t three_digit_min = 100;
  const int64_t eight_digit_max = 99999999;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || (ObDateTimeTC != sql_type_class(expect_type)
                      && ObMySQLDateTimeTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    int64_t value = 0;
    ObMySQLDateTime mdt_value = 0;
    int64_t int_part = 0;
    int64_t dec_part = 0;
    if (in.get_number().is_negative()) {
      ret = OB_INVALID_DATE_FORMAT;
    } else if (!in.get_number().is_int_parts_valid_int64(int_part,dec_part)) {
      ret = OB_INVALID_DATE_FORMAT;
    } else if (OB_UNLIKELY(dec_part != 0
              && ((0 == int_part && ObTimestampType == expect_type)
                  || (int_part >= three_digit_min && int_part <= eight_digit_max)))) {
      if (CM_IS_COLUMN_CONVERT(cast_mode) && !CM_IS_WARN_ON_FAIL(cast_mode)) {
        ret = OB_INVALID_DATE_VALUE;
      } else {
        dec_part = 0;
      }
    }
    if (OB_SUCC(ret)) {
      ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
      ret = ObMySQLDateTimeType == expect_type ?
              ObTimeConverter::int_to_mdatetime(int_part, dec_part, cvrt_ctx, mdt_value,
                                                date_sql_mode) :
              ObTimeConverter::int_to_datetime(int_part, dec_part, cvrt_ctx, value, date_sql_mode);
    }
    if (CAST_FAIL(ret)) {
    } else if (ObMySQLDateTimeType == expect_type) {
      SET_RES_MDATETIME(out);
    } else {
      SET_RES_DATETIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int number_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(number_datetime(expect_type, params, in, out, cast_mode))) {
  }
  return ret;
}


template <typename Value>
static int string_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  int32_t value = 0;
  ObMySQLDate md_value = 0;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || (ObDateTC != sql_type_class(expect_type)
                      && ObMySQLDateTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t int_part = 0;
    int64_t dec_part = 0;
    const number::ObNumber nmb = in.get_number();
    if (nmb.is_negative()) {
      ret = OB_INVALID_DATE_VALUE;
    } else if (!nmb.is_int_parts_valid_int64(int_part, dec_part)) {
      ret = OB_INVALID_DATE_VALUE;
    } else {
      ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
      ret = ObMySQLDateType == expect_type ?
              ObTimeConverter::int_to_mdate(int_part, md_value, date_sql_mode) :
              ObTimeConverter::int_to_date(int_part, value, date_sql_mode);
      if (OB_SUCC(ret) && OB_UNLIKELY(dec_part > 0)) {
        if (CM_IS_COLUMN_CONVERT(cast_mode) && !CM_IS_WARN_ON_FAIL(cast_mode)) {
          ret = OB_INVALID_DATE_VALUE;
        }
      }
    }
    if (CAST_FAIL(ret)) {
    } else if (ObMySQLDateType == expect_type) {
      SET_RES_MDATE(out);
    } else {
      SET_RES_DATE(out);
    }
  }
  return ret;
}

template <typename Value>
static int number_mdate(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(number_date(expect_type, params, in, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int string_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const char *value = in.get_number().format();
    if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      Value from;
      from.set_varchar(value, static_cast<ObString::obstr_size_t>(strlen(value)));
      ret = string_time(expect_type, params, from, out, cast_mode);
    }
  }
  //has set accuracy in prev string_time call
  return ret;
}

template <typename Value>
static int string_year(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int number_year(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const char *value = in.get_number().format();
    if (OB_ISNULL(value)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (in.get_number().is_negative()) {
      uint8_t value = 0;
      if (CAST_FAIL(ObTimeConverter::int_to_year(INT_MIN, value))) {
      } else {
        SET_RES_YEAR(out);
      }
    } else {
      Value from;
      from.set_varchar(value, static_cast<ObString::obstr_size_t>(strlen(value)));
      ret = string_year(expect_type, params, from, out, cast_mode);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int number_string(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    if (OB_FAIL(in.get_number().format(buf, sizeof(buf), len, in.get_scale()))) {
    }

    if (OB_SUCC(ret)) {
      ObString tmp_str;
      Value tmp_out;
      if (OB_FAIL(convert_string_collation(ObString(len, buf), ObCharset::get_system_collation(),
                                           tmp_str, params.dest_collation_, params))) {
      } else if (OB_FAIL(check_convert_string(expect_type, params, tmp_str, tmp_out))) {
      } else if (OB_FAIL(copy_string(params, expect_type, tmp_out.get_string(), out))) {
      } else {
        res_length = static_cast<ObLength>(out.get_string_len());
        out.set_collation_type(params.dest_collation_);
      }
    }
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  return ret;
}

template <typename Value>
static int number_bit(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(number_uint(ObUInt64Type, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(uint_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}





template <typename Value>
static int decimalint_string(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode);
CAST_TO_TEXT_METHOD(number, ObNumberTC);
CAST_TO_TEXT_METHOD(decimalint, ObDecimalIntTC);

template <typename Value>
static int number_json(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObNumberTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    const number::ObNumber nmb(in.get_number());
    ObJsonDecimal j_dec(nmb, -1, in.get_scale());
    ObIJsonBase *j_base = &j_dec;
    ObString raw_bin;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
    } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
    } else {
      res_length = out.get_val_len();
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}


////////////////////////////////////////////////////////////
// Datetime -> XXX

template <typename Value>
static int datetime_int(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    int64_t value = 0;
    if (OB_FAIL(ObTimeConverter::datetime_to_int(in.get_datetime(), tz_info, value))) {
    } else if (expect_type < ObIntType && CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      out.set_int(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int mdatetime_int(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = 0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_int(in.get_mysql_datetime(), value))) {
    } else if (expect_type < ObIntType && CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      out.set_int(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int datetime_uint(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    int64_t int64 = 0;
    if (OB_FAIL(ObTimeConverter::datetime_to_int(in.get_datetime(), tz_info, int64))) {
    } else {
      uint64_t value = static_cast<uint64_t>(int64);
      if (expect_type < ObUInt64Type && CAST_FAIL(uint_range_check(expect_type, int64, value))) {
      } else {
        out.set_uint(expect_type, value);
        res_precision = get_precision_for_integer(value);
      }
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int mdatetime_uint(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t int64 = 0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_int(in.get_mysql_datetime(), int64))) {
    } else {
      uint64_t value = static_cast<uint64_t>(int64);
      if (expect_type < ObUInt64Type && CAST_FAIL(uint_range_check(expect_type, int64, value))) {
      } else {
        out.set_uint(expect_type, value);
        res_precision = get_precision_for_integer(value);
      }
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int datetime_float(const SqlValueType expect_type, DatumCastParams &params,
                          const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    double value = 0.0;
    if (OB_FAIL(ObTimeConverter::datetime_to_double(in.get_datetime(), tz_info, value))) {
    } else {
      // if datetime_to_double return OB_SUCCESS, double value must be in (0, INT64_MAX),
      // so we can static_cast to float without range check.
      out.set_float(expect_type, static_cast<float>(value));
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdatetime_float(const SqlValueType expect_type, DatumCastParams &params,
                          const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    double value = 0.0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_double(in.get_mysql_datetime(), value))) {
    } else {
      // if datetime_to_double return OB_SUCCESS, double value must be in (0, INT64_MAX),
      // so we can static_cast to float without range check.
      out.set_float(expect_type, static_cast<float>(value));
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int datetime_double(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    double value = 0.0;
    if (OB_FAIL(ObTimeConverter::datetime_to_double(in.get_datetime(), tz_info, value))) {
    } else {
      out.set_double(expect_type, value);
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdatetime_double(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    double value = 0.0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_double(in.get_mysql_datetime(), value))) {
    } else {
      out.set_double(expect_type, value);
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int datetime_number(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  ObScale res_scale = -1;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t len = 0;
    number::ObNumber value;
    if (OB_FAIL(ObTimeConverter::datetime_to_str(in.get_datetime(), tz_info,
        in.get_scale(), buf, sizeof(buf), len, false))) {
    } else if (CAST_FAIL(value.from(buf, len, params, &res_precision, &res_scale))) {
    } else {
      out.set_number(expect_type, value);
    }
  }
  SET_RES_ACCURACY(res_precision, res_scale, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int mdatetime_number(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  ObScale res_scale = -1;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t len = 0;
    number::ObNumber value;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_str(in.get_mysql_datetime(), NULL,
                                                  in.get_scale(), buf, sizeof(buf), len, false))) {
    } else if (CAST_FAIL(value.from(buf, len, params, &res_precision, &res_scale))) {
    } else {
      out.set_number(expect_type, value);
    }
  }
  SET_RES_ACCURACY(res_precision, res_scale, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

// timestamp -> datetime
// datetime -> timestamp
template <typename Value>
static int datetime_datetime(const SqlValueType expect_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = in.get_datetime();
    if (ObDateTimeType == in.get_type() && ObTimestampType == expect_type) {
      ret = ObTimeConverter::datetime_to_timestamp(in.get_datetime(), params.dtc_params_.tz_info_, value);
      ret = OB_ERR_UNEXPECTED_TZ_TRANSITION == ret ? OB_INVALID_DATE_VALUE : ret;
    } else if (ObTimestampType == in.get_type() && ObDateTimeType == expect_type) {
      ret = ObTimeConverter::timestamp_to_datetime(in.get_datetime(), params.dtc_params_.tz_info_, value);
    }

    if (OB_FAIL(ret)) {
    } else {
      out.set_datetime(expect_type, value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

// mdatetime -> datetime/timestamp
template <typename Value>
static int mdatetime_datetime(const SqlValueType expect_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
    int64_t value = in.get_datetime();
    if (ObTimestampType == expect_type) {
      ret = ObTimeConverter::mdatetime_to_timestamp(in.get_mysql_datetime(),
                                                params.dtc_params_.tz_info_, value);
      ret = OB_ERR_UNEXPECTED_TZ_TRANSITION == ret ? OB_INVALID_DATE_VALUE : ret;
    } else if (ObDateTimeType == expect_type) {
      ret = ObTimeConverter::mdatetime_to_datetime(in.get_mysql_datetime(), value, date_sql_mode);
    }

    if (CAST_FAIL(ret)) {
    } else {
      out.set_datetime(expect_type, value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

// datetime/timestamp -> mdatetime
template <typename Value>
static int datetime_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                                   const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDateTime value = 0;
    if (ObDateTimeType == in.get_type()) {
      ret = ObTimeConverter::datetime_to_mdatetime(in.get_datetime(), value);
      ret = OB_ERR_UNEXPECTED_TZ_TRANSITION == ret ? OB_INVALID_DATE_VALUE : ret;
    } else if (ObTimestampType == in.get_type()) {
      ret = ObTimeConverter::timestamp_to_mdatetime(in.get_datetime(),
                                                         params.dtc_params_.tz_info_, value);
    }

    if (OB_FAIL(ret)) {
    } else {
      out.set_mysql_datetime(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}


template <typename Value>
static int datetime_date(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    int32_t value = 0;
    if (OB_FAIL(ObTimeConverter::datetime_to_date(in.get_datetime(), tz_info, value))) {
    } else {
      out.set_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int datetime_mdate(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    ObMySQLDate value = 0;
    if (OB_FAIL(ObTimeConverter::datetime_to_mdate(in.get_datetime(), tz_info, value))) {
    } else {
      out.set_mysql_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdatetime_date(const SqlValueType expect_type, DatumCastParams &params,
                               const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    int32_t value = 0;
    ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
    if (CAST_FAIL(ObTimeConverter::mdatetime_to_date(in.get_mysql_datetime(), value, date_sql_mode))) {
    } else {
      out.set_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}


template <typename Value>
static int mdatetime_mdate(const SqlValueType expect_type, DatumCastParams &params,
                               const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDate value = 0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_mdate(in.get_mysql_datetime(), value))) {
    } else {
      out.set_mysql_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

// datetime/mdatetime -> time
template <typename Value>
static int datetime_time(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    int64_t value = 0;
    if (OB_FAIL(ObTimeConverter::datetime_to_time(in.get_datetime(), tz_info, value))) {
    } else {
      out.set_time(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdatetime_time(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = 0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_time(in.get_mysql_datetime(), value))) {
    } else {
      out.set_time(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int datetime_year(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    uint8_t value = 0;
    if (CAST_FAIL(ObTimeConverter::datetime_to_year(in.get_datetime(), tz_info, value))) {
    } else {
      SET_RES_YEAR(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdatetime_year(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    uint8_t value = 0;
    if (CAST_FAIL(ObTimeConverter::mdatetime_to_year(in.get_mysql_datetime(), value))) {
    } else {
      SET_RES_YEAR(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}


// datetime/mdatetime/timestamp -> string
template <typename Value>
static int datetime_string(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (OB_UNLIKELY((ObDateTimeTC != in.get_type_class() && ObMySQLDateTimeTC != in.get_type_class())
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t len = 0;
    ret = in.is_mysql_datetime() ?
                  ObTimeConverter::mdatetime_to_str(in.get_mysql_datetime(), tz_info,
                                                   in.get_scale(), buf, sizeof(buf), len) :
                  ObTimeConverter::datetime_to_str(in.get_datetime(), tz_info,
                                                   in.get_scale(), buf, sizeof(buf), len);
    if (OB_FAIL(ret)) {
    } else {
      out.set_type(expect_type);
      ObString tmp_str;
      Value tmp_out;
      if (OB_FAIL(convert_string_collation(ObString(len, buf), ObCharset::get_system_collation(), tmp_str, params.dest_collation_, params))) {
      } else if (OB_FAIL(check_convert_string(expect_type, params, tmp_str, tmp_out))) {
      } else if (OB_FAIL(copy_string(params, expect_type, tmp_out.get_string(), out))) {
      } else {
        res_length = static_cast<ObLength>(out.get_string_len());
        out.set_collation_type(params.dest_collation_);
      }
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdatetime_string(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(datetime_string(expect_type, params, in, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int string_bit(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int datetime_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(datetime_string(ObVarcharType, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(string_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int string_bit(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int mdatetime_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(mdatetime_string(ObVarcharType, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(string_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}


CAST_TO_TEXT_METHOD(datetime, ObDateTimeTC);
CAST_TO_TEXT_METHOD(mdatetime, ObMySQLDateTimeTC);

template <typename Value>
static int datetime_json(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    int64_t in_val = in.get_datetime();
    ObTime ob_time(DT_TYPE_DATETIME);
    const ObTimeZoneInfo *tz_info = (ObTimestampType == in.get_type()) ?
                                    params.dtc_params_.tz_info_ : NULL;
    if (OB_FAIL(ObTimeConverter::datetime_to_ob_time(in_val, tz_info, ob_time))) {
    } else {
      ObJsonNodeType j_type = (ObTimestampType == in.get_type())
                                  ? ObJsonNodeType::J_TIMESTAMP
                                  : ObJsonNodeType::J_DATETIME;
      ObJsonDatetime j_datetime(j_type, ob_time);
      ObIJsonBase *j_base = &j_datetime;
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
      } else {
        res_length = out.get_val_len();
      }
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}

template <typename Value>
static int mdatetime_json(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDateTime in_val = in.get_mysql_datetime();
    ObTime ob_time(DT_TYPE_MYSQL_DATETIME);
    if (OB_FAIL(ObTimeConverter::mdatetime_to_ob_time(in_val, ob_time))) {
    } else {
      ObJsonDatetime j_datetime(ObJsonNodeType::J_MYSQL_DATETIME, ob_time);
      ObIJsonBase *j_base = &j_datetime;
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
      } else {
        res_length = out.get_val_len();
      }
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}



////////////////////////////////////////////////////////////
// Date -> XXX

template <typename Value>
static int date_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::date_to_int(in.get_date(), value))) {
  } else if (expect_type < ObInt32Type && CAST_FAIL(int_range_check(expect_type, value, value))) {
  } else {
    out.set_int(expect_type, value);
    res_precision = get_precision_for_integer(value);
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int mdate_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::mdate_to_int(in.get_mysql_date(), value))) {
  } else if (expect_type < ObInt32Type && CAST_FAIL(int_range_check(expect_type, value, value))) {
  } else {
    out.set_int(expect_type, value);
    res_precision = get_precision_for_integer(value);
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int date_uint(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  int64_t int64 = 0;
  uint64_t value = 0;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::date_to_int(in.get_date(), int64))) {
  } else {
    value = static_cast<uint64_t>(int64);
    if (expect_type < ObUInt32Type && CAST_FAIL(uint_range_check(expect_type, int64, value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int mdate_uint(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  int64_t int64 = 0;
  uint64_t value = 0;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::mdate_to_int(in.get_mysql_date(), int64))) {
  } else {
    value = static_cast<uint64_t>(int64);
    if (expect_type < ObUInt32Type && CAST_FAIL(uint_range_check(expect_type, int64, value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}


template <typename Value>
static int date_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::date_to_int(in.get_date(), value))) {
  } else {
    out.set_float(expect_type, static_cast<float>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdate_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::mdate_to_int(in.get_mysql_date(), value))) {
  } else {
    out.set_float(expect_type, static_cast<float>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::date_to_int(in.get_date(), value))) {
  } else {
    out.set_double(expect_type, static_cast<double>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdate_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::mdate_to_int(in.get_mysql_date(), value))) {
  } else {
    out.set_double(expect_type, static_cast<double>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_int;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(date_int(ObIntType, params, in, obj_int, cast_mode))) {
  } else if (OB_FAIL(int_number(expect_type, params, obj_int, out, cast_mode))) {
  }
  //has set accuracy in prev int_number
  return ret;
}

template <typename Value>
static int mdate_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_int;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(mdate_int(ObIntType, params, in, obj_int, cast_mode))) {
  } else if (OB_FAIL(int_number(expect_type, params, obj_int, out, cast_mode))) {
  }
  //has set accuracy in prev int_number
  return ret;
}

template <typename Value>
static int date_datetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    int64_t value = 0;
    if (OB_FAIL(ObTimeConverter::date_to_datetime(in.get_date(), cvrt_ctx, value))) {
    } else {
      out.set_datetime(expect_type, value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdate_datetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((ObMySQLDateTC != in.get_type_class())
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
    int64_t value = 0;
    if (CAST_FAIL(ObTimeConverter::mdate_to_datetime(in.get_mysql_date(), cvrt_ctx, value,
                                                   date_sql_mode,  params.gen_query_range_))) {
    } else {
      out.set_datetime(expect_type, value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDateTime value = 0;
    if (OB_FAIL(ObTimeConverter::date_to_mdatetime(in.get_date(), value))) {
    } else {
      out.set_mysql_datetime(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}


template <typename Value>
static int mdate_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDateTime value = 0;
    if (OB_FAIL(ObTimeConverter::mdate_to_mdatetime(in.get_mysql_date(), value))) {
    } else {
      out.set_mysql_datetime(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_time(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    out.set_time(ObTimeConverter::ZERO_TIME);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return OB_SUCCESS;
}

template <typename Value>
static int mdate_time(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((ObMySQLDateTC != in.get_type_class())
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    out.set_time(ObTimeConverter::ZERO_TIME);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return OB_SUCCESS;
}

template <typename Value>
static int date_mdate(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDate value = 0;
    if (OB_FAIL(ObTimeConverter::date_to_mdate(in.get_date(), value))) {
    } else {
      out.set_mysql_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdate_date(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObDateSqlMode date_sql_mode = get_date_sql_mode(cast_mode);
    int32_t value = 0;
    if (CAST_FAIL(ObTimeConverter::mdate_to_date(in.get_mysql_date(), value, date_sql_mode))) {
    } else {
      out.set_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_year(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint8_t value = 0;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(ObTimeConverter::date_to_year(in.get_date(), value))) {
  } else {
    SET_RES_YEAR(out);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdate_year(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint8_t value = 0;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(ObTimeConverter::mdate_to_year(in.get_mysql_date(), value))) {
  } else {
    SET_RES_YEAR(out);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::date_to_str(in.get_date(), buf, sizeof(buf), len))) {
  } else {
    out.set_type(expect_type);
    ret = copy_string(params, expect_type, buf, len, out);
    if (OB_SUCC(ret)) {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int mdate_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::mdate_to_str(in.get_mysql_date(), buf, sizeof(buf), len))) {
  } else {
    out.set_type(expect_type);
    ret = copy_string(params, expect_type, buf, len, out);
    if (OB_SUCC(ret)) {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int date_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(date_string(ObVarcharType, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(string_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int mdate_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(mdate_string(ObVarcharType, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(string_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}



CAST_TO_TEXT_METHOD(date, ObDateTC);
CAST_TO_TEXT_METHOD(mdate, ObMySQLDateTC);

template <typename Value>
static int date_json(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObDateTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    int32_t in_val = in.get_date();
    ObTime ob_time(DT_TYPE_DATE);
    if (OB_FAIL(ObTimeConverter::date_to_ob_time(in_val, ob_time))) {
    } else {
      ObJsonDatetime j_date(ObJsonNodeType::J_DATE, ob_time);
      ObIJsonBase *j_base = &j_date;
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
      } else {
        res_length = out.get_val_len();
      }
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}

template <typename Value>
static int mdate_json(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    ObMySQLDate in_val = in.get_mysql_date();
    ObTime ob_time(DT_TYPE_DATE);
    if (OB_FAIL(ObTimeConverter::mdate_to_ob_time(in_val, ob_time))) {
    } else {
      ObJsonDatetime j_date(ObJsonNodeType::J_MYSQL_DATE, ob_time);
      ObIJsonBase *j_base = &j_date;
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
      } else {
        res_length = out.get_val_len();
      }
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}



////////////////////////////////////////////////////////////
// Time -> XXX

template <typename Value>
static int time_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_int(in.get_time(), value))) {
  } else if (expect_type < ObInt32Type && CAST_FAIL(int_range_check(expect_type, value, value))) {
  } else {
    out.set_int(expect_type, value);
    res_precision = get_precision_for_integer(value);
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int time_uint(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t int64 = 0;
  uint64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_int(in.get_time(), int64))) {
  } else {
    value = static_cast<uint64_t>(int64);
    if (CM_NEED_RANGE_CHECK(cast_mode) && CAST_FAIL(uint_range_check(expect_type, int64, value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int time_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  double value = 0.0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_double(in.get_time(), value))) {
  } else if (ObUFloatType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    // if time_to_double return OB_SUCCESS, double value must be in (INT64_MIN, INT64_MAX),
    // so we can static_cast to float without range check.
    out.set_float(expect_type, static_cast<float>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int time_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  double value = 0.0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_double(in.get_time(), value))) {
  } else if (ObUDoubleType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    out.set_double(expect_type, value);
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int time_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  number::ObNumber value;
  ObPrecision res_precision = -1;
  ObScale res_scale = -1;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_str(in.get_time(), in.get_scale(), buf, sizeof(buf), len, false))) {
  } else if (CAST_FAIL(value.from(buf, len, params, &res_precision, &res_scale))) {
  } else if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
  } else {
    out.set_number(expect_type, value);
  }
  SET_RES_ACCURACY(res_precision, res_scale, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int time_datetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  const ObTimeZoneInfo *tz_info = params.dtc_params_.tz_info_;
  int64_t value = 0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_datetime(in.get_time(), params.cur_time_, tz_info, value, expect_type))) {
  } else {
    out.set_datetime(expect_type, value);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int time_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  const ObTimeZoneInfo *tz_info = params.dtc_params_.tz_info_;
  ObMySQLDateTime value = 0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_mdatetime(in.get_time(), params.cur_time_, tz_info, value))) {
  } else {
    out.set_mysql_datetime(value);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, in.get_scale(), DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int time_date(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  const ObTimeZoneInfo *tz_info = params.dtc_params_.tz_info_;
  int32_t value = 0;
  int64_t datetime_value = 0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(tz_info, false);
    if (OB_FAIL(ObTimeConverter::time_to_datetime(in.get_time(), params.cur_time_, tz_info,
                                                  datetime_value, ObDateTimeType))) {
    } else if (OB_FAIL(ObTimeConverter::datetime_to_date(datetime_value, NULL, value))) {
    } else {
      out.set_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int time_mdate(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  const ObTimeZoneInfo *tz_info = params.dtc_params_.tz_info_;
  ObMySQLDate value = 0;
  int64_t datetime_value = 0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    ObTimeConvertCtx cvrt_ctx(tz_info, false);
    if (OB_FAIL(ObTimeConverter::time_to_datetime(in.get_time(), params.cur_time_, tz_info,
                                                  datetime_value, ObDateTimeType))) {
    } else if (OB_FAIL(ObTimeConverter::datetime_to_mdate(datetime_value, NULL, value))) {
    } else {
      out.set_mysql_date(value);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int time_year(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t int_value = 0;
  uint8_t value = 0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_int(in.get_time(), int_value))) {
  } else if (CAST_FAIL(ObTimeConverter::int_to_year(int_value, value))) {
  } else {
    SET_RES_YEAR(out);
  }
  SET_RES_ACCURACY(DEFAULT_SCALE_FOR_YEAR, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int time_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::time_to_str(in.get_time(), in.get_scale(), buf, sizeof(buf), len))) {
  } else {
    out.set_type(expect_type);
    ret = copy_string(params, expect_type, buf, len, out);
    if (OB_SUCC(ret)) {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int time_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(time_string(ObVarcharType, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(string_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}



CAST_TO_TEXT_METHOD(time, ObTimeTC);

template <typename Value>
static int time_json(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObTimeTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else {
    int64_t in_val = in.get_int();
    ObTime ob_time(DT_TYPE_TIME);
    if (OB_FAIL(ObTimeConverter::time_to_ob_time(in_val, ob_time))) {
    } else {
      ObJsonDatetime j_time(ObJsonNodeType::J_TIME, ob_time);
      ObIJsonBase *j_base = &j_time;
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
      } else {
        res_length = out.get_val_len();
      }
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}


////////////////////////////////////////////////////////////
// Year -> XXX

template <typename Value>
static int year_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), value))) {
  } else if (expect_type < ObSmallIntType && CAST_FAIL(int_range_check(expect_type, value, value))) {
  } else {
    out.set_int(expect_type, value);
    res_precision = get_precision_for_integer(value);
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int year_uint(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t int64 = 0;
  uint64_t value = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), int64))) {
  } else {
    value = static_cast<uint64_t>(int64);
    if (expect_type < ObUSmallIntType && CAST_FAIL(uint_range_check(expect_type, int64, value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int year_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), value))) {
  } else {
    out.set_float(expect_type, static_cast<float>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int year_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), value))) {
  } else {
    out.set_double(expect_type, static_cast<double>(value));
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int year_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_int;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(year_int(ObIntType, params, in, obj_int, cast_mode))) {
  } else if (OB_FAIL(int_number(expect_type, params, obj_int, out, cast_mode))) {
  }
  //has set accuracy in prev int_number
  return ret;
}

template <typename Value>
static int year_datetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t int_value = 0;
  Value int64;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), int_value))) {
  } else if (FALSE_IT(int64.set_int(int_value))) {
  } else if (OB_FAIL(int_datetime(expect_type, params, int64, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int year_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t int_value = 0;
  Value int64;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), int_value))) {
  } else if (FALSE_IT(int64.set_int(int_value))) {
  } else if (OB_FAIL(int_mdatetime(expect_type, params, int64, out, cast_mode))) {
  }
  return ret;
}


template <typename Value>
static int year_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t in_value = 0;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), in_value))) {
  } else {
    int32_t value = 0;
    if (CAST_FAIL(ObTimeConverter::int_to_date(in_value, value, false))) {
    } else {
      SET_RES_DATE(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int year_mdate(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t in_value = 0;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), in_value))) {
  } else {
    ObMySQLDate md_value = 0;
    if (CAST_FAIL(ObTimeConverter::int_to_mdate(in_value, md_value, false))) {
    } else {
      SET_RES_MDATE(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int year_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    int64_t value = 0;
    int64_t year_int = 0;
    if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), year_int))) {
    } else if (CAST_FAIL(ObTimeConverter::int_to_time(year_int, value))) {
    } else {
      SET_RES_TIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int year_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || (ObStringTC != sql_type_class(expect_type)
                  && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_str(in.get_year(), buf, sizeof(buf), len))) {
  } else {
    out.set_type(expect_type);
    ret = copy_string(params, expect_type, buf, len, out);
    if (OB_SUCC(ret)) {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int year_bit(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(year_int(ObIntType, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(int_bit(expect_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}



CAST_TO_TEXT_METHOD(year, ObYearTC);

template <typename Value>
static int year_json(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  int64_t full_year = 0;
  if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObYearTC != in.get_type_class() || (ObJsonType != expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret= OB_ERR_UNEXPECTED;
    LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(ObTimeConverter::year_to_int(in.get_year(), full_year))) {
  } else {
    ObJsonInt j_year(full_year);
    ObIJsonBase *j_base = &j_year;
    ObString raw_bin;
    if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
    } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
    } else {
      res_length = out.get_val_len();
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length)
  return ret;
}


////////////////////////////////////////////////////////////
// String -> XXX

int common_string_unsigned_integer(const ObCastMode &cast_mode,
                                   const SqlValueType &in_type,
                                   const ObCollationType &in_cs_type,
                                   const ObString &in_str,
                                   const bool is_str_integer_cast,
                                   uint64_t &out_val)
{
  int ret = OB_SUCCESS;
  if (ObHexStringType == in_type) {
    out_val = hex_to_uint64(in_str);
  } else {
    int err = 0;
    char *endptr = NULL;
    if (is_str_integer_cast && CM_IS_STRING_INTEGER_TRUNC(cast_mode)) {
      out_val = ObCharset::strntoull(in_str.ptr(), in_str.length(), 10, &endptr, &err);
      if (ERANGE == err && (INT64_MIN == out_val || INT64_MAX == out_val)) {
        ret = OB_DATA_OUT_OF_RANGE;
      } else if (endptr == in_str.ptr() || endptr != in_str.ptr() + in_str.length()) {
        ret = OB_ERR_TRUNCATED_WRONG_VALUE;
        if (CM_IS_WARN_ON_FAIL(cast_mode)) {
          ObString tmp_msg("INTEGER");
          LOG_USER_WARN(OB_ERR_TRUNCATED_WRONG_VALUE, tmp_msg.length(), tmp_msg.ptr(),
                        in_str.length(), in_str.ptr());
        }
      }
    } else {
      out_val = ObCharset::strntoullrnd(in_str.ptr(), in_str.length(), true, &endptr, &err);
      if (ERANGE == err && (0 == out_val || UINT64_MAX == out_val)) {
        ret = OB_DATA_OUT_OF_RANGE;
      } else {
        ret = check_convert_str_err(in_str.ptr(), endptr, in_str.length(), err, in_cs_type);
      }
    }
  }
  return ret;
}
// Incompatible behavior with MySQL:
int common_string_integer(const ObCastMode &cast_mode,
                                 const SqlValueType &in_type,
                                 const ObCollationType &in_cs_type,
                                 const ObString &in_str,
                                 const bool is_str_integer_cast,
                                 int64_t &out_val)
{
  int ret = OB_SUCCESS;
  if (ObHexStringType == in_type) {
    const uint64_t uint_value = hex_to_uint64(in_str);
    const bool skip_cast = CM_IS_EXTERNAL_CALL(cast_mode) && CM_SKIP_CAST_INT_UINT(cast_mode);
    ObCastMode uint_int_cast_mode = cast_mode;
    if (CM_IS_EXPLICIT_CAST(cast_mode)) {
      uint_int_cast_mode |= CM_NO_RANGE_CHECK;
    }
    int64_t int_value = static_cast<int64_t>(uint_value);
    if (!skip_cast && CM_NEED_RANGE_CHECK(uint_int_cast_mode)
        && OB_FAIL(int_upper_check(ObIntType, uint_value, int_value))) {
    } else {
      out_val = int_value;
    }
  } else {
    int err = 0;
    char *endptr = NULL;
    if (is_str_integer_cast && CM_IS_STRING_INTEGER_TRUNC(cast_mode)) {
      out_val = ObCharset::strntoll(in_str.ptr(), in_str.length(), 10, &endptr, &err);
      if (ERANGE == err && (INT64_MIN == out_val || INT64_MAX == out_val)) {
        ret = OB_DATA_OUT_OF_RANGE;
      } else if (endptr == in_str.ptr() || endptr != in_str.ptr() + in_str.length()) {
        ret = OB_ERR_TRUNCATED_WRONG_VALUE;
        if (CM_IS_WARN_ON_FAIL(cast_mode)) {
          ObString tmp_msg("INTEGER");
          LOG_USER_WARN(OB_ERR_TRUNCATED_WRONG_VALUE, tmp_msg.length(), tmp_msg.ptr(),
                        in_str.length(), in_str.ptr());
        }
      }
    } else {
      out_val = static_cast<int64_t>(ObCharset::strntoullrnd(in_str.ptr(), in_str.length(),
                                                             false, &endptr, &err));
      if (ERANGE == err && (INT64_MIN == out_val || INT64_MAX == out_val)) {
        ret = OB_DATA_OUT_OF_RANGE;
      } else {
        ret = check_convert_str_err(in_str.ptr(), endptr, in_str.length(), err, in_cs_type);
      }
    }
  }
  return ret;
}

template <typename Value>
static int string_int(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObString utf8_string;

  ObPrecision res_precision = -1;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(convert_string_collation(in.get_string(), in.get_collation_type(), utf8_string, ObCharset::get_system_collation(), params))) {
  } else {
    const ObString &str = utf8_string;
    int64_t value = 0;
    const bool is_str_int_cast = true;
    if (CAST_FAIL(common_string_integer(
                cast_mode, in.get_type(), in.get_collation_type(), str, is_str_int_cast, value))) {
    } else if (expect_type < ObIntType && CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      SET_RES_INT(out);
    }
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(out.get_int());
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int string_uint(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    const ObString &str = in.get_string();
    uint64_t value = 0;
    const bool is_str_int_cast = true;
    if (CAST_FAIL(common_string_unsigned_integer(
                cast_mode, in.get_type(), in.get_collation_type(), str, is_str_int_cast, value))) {
    } else if (expect_type < ObUInt64Type && CM_NEED_RANGE_CHECK(cast_mode)
               && CAST_FAIL(uint_upper_check(expect_type, value))) {
    } else {
      SET_RES_UINT(out);
    }
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(out.get_uint64());
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int string_float(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(string_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_float(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_float call
  return ret;
}

template <typename Value>
static int string_double(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObDoubleTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else {
    double value = 0.0;
    if (ObHexStringType == in.get_type()) {
      value = static_cast<double>(hex_to_uint64(in.get_string()));
    } else {
      int err = 0;
      char *endptr = NULL;
      ObString str_utf8;
      if (OB_FAIL(convert_string_collation(in.get_string(), in.get_collation_type(), str_utf8,
                               ObCharset::get_system_collation(), params))) {
      } else {
        value = ObCharset::strntodv2(str_utf8.ptr(), str_utf8.length(), &endptr, &err);
        if (EOVERFLOW == err && (-DBL_MAX == value || DBL_MAX == value)) {
          ret = OB_DATA_OUT_OF_RANGE;
        } else {
          ObString trimed_str = str_utf8.trim();
          if (0 == trimed_str.length()) {
            if (!CM_IS_COLUMN_CONVERT(cast_mode)) {
              // In mysql mode, do not report an error when converting an empty string or a string with only spaces to double in convert_column
              // skip
            } else {
              ret = OB_ERR_DOUBLE_TRUNCATED;
            }
          } else if (OB_SUCCESS != (ret = check_convert_str_err(
                                          str_utf8.ptr(), endptr, str_utf8.length(), err, in.get_collation_type()))) {
            if (CM_IS_COLUMN_CONVERT(cast_mode) && ret == OB_ERR_DATA_TRUNCATED) {
              // do nothing, compatible mysql, retain OB_ERR_DATA_TRUNCATED error code in column_convert.
            } else {
              ret = OB_ERR_DOUBLE_TRUNCATED;
            }
            if (CM_IS_WARN_ON_FAIL(cast_mode)) {
              ret = OB_ERR_DOUBLE_TRUNCATED;
              LOG_USER_WARN(OB_ERR_DOUBLE_TRUNCATED, str_utf8.length(), str_utf8.ptr());
            }
          }
        }
      }
    }

    if (CAST_FAIL(ret)) {
    } else if (ObUDoubleType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
    } else {
      SET_RES_DOUBLE(out);
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int string_number(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = PRECISION_UNKNOWN_YET;
  ObScale res_scale = NUMBER_SCALE_UNKNOWN_YET;
  ObString utf8_string;

  if (OB_ISNULL(params.allocator_v2_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                          && ObTextTC != in.get_type_class()
                          && ObGeometryTC != in.get_type_class())
                          || ObNumberTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else {
    number::ObNumber value;
    if (ObHexStringType == in.get_type()) {
      ret = value.from(hex_to_uint64(in.get_string()), params);
    } else if (OB_UNLIKELY(0 == in.get_string().length())) {
      value.set_zero();
      ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
    } else if (OB_FAIL(convert_string_collation(in.get_string(), in.get_collation_type(), utf8_string, ObCharset::get_system_collation(), params))) {
    } else {
      const ObString &str = utf8_string;
      ret = value.from_sci_opt(str.ptr(), str.length(), params, &res_precision, &res_scale);
      // bug: 4263211. Compatibility with MySQL string to number conversion exceeding the maximum range
      // select cast('1e500' as decimal);  -> max_val
      // select cast('-1e500' as decimal); -> min_val
      if (ret == OB_NUMERIC_OVERFLOW) {
        int64_t i = 0;
        while (i < str.length() && isspace(str[i])) {
          ++i;
        }
        bool is_neg = (str[i] == '-');
        int tmp_ret = OB_SUCCESS;
        const ObAccuracy &def_acc = ObAccuracy::DDL_DEFAULT_ACCURACY[expect_type];
        const ObPrecision prec = def_acc.get_precision();
        const ObScale scale = def_acc.get_scale();
        const ObNumber *bound_num = NULL;
        if (is_neg) {
          bound_num = &(ObNumberConstValue::MYSQL_MIN[prec][scale]);
        } else {
          bound_num = &(ObNumberConstValue::MYSQL_MAX[prec][scale]);
        }
        if (OB_SUCCESS != (tmp_ret = value.from(*bound_num, *params.allocator_v2_))) {
        }
      } else if (OB_INVALID_NUMERIC == ret
          && OB_NOT_NULL(params.runtime_)) {
        if (CM_IS_COLUMN_CONVERT(params.cast_mode_)) {
          ObString decimal_type_str("decimal");
          params.runtime_->report_warning(
              ret, decimal_type_str, in.get_string(), params.cast_mode_);
        } else {
          ret = OB_ERR_TRUNCATED_WRONG_VALUE;
          ObString decimal_type_str("DECIMAL");
          params.runtime_->report_warning(
              ret, decimal_type_str, in.get_string(), params.cast_mode_);
        }
      }
    }
    if (CAST_FAIL(ret)) {
    } else if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
    } else {
      out.set_number(expect_type, value);
    }
  }
  SET_RES_ACCURACY(res_precision, res_scale, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int common_string_datetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, int64_t &value, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObScale res_scale = -1;
  ObString utf8_string;
  ObMySQLDateTime mdt_value = 0;
  if (OB_FAIL(convert_string_collation(in.get_string(), in.get_collation_type(), utf8_string, ObCharset::get_system_collation(), params))) {
  } else {
    bool need_truncate = CM_IS_COLUMN_CONVERT(cast_mode) ? CM_IS_TIME_TRUNCATE_FRACTIONAL(cast_mode) : false;
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type, need_truncate);
    {
      ObDateSqlMode date_sql_mode;
      date_sql_mode.allow_invalid_dates_ = CM_IS_ALLOW_INVALID_DATES(cast_mode);
      date_sql_mode.no_zero_date_ =
        CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_DATE(cast_mode);
      date_sql_mode.no_zero_in_date_ =
        CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_IN_DATE(cast_mode);
      ObMySQLDateTime mdt_value = 0;
      ret = ObMySQLDateTimeType == expect_type ?
              ObTimeConverter::str_to_mdatetime(utf8_string, cvrt_ctx, mdt_value, &res_scale,
                                                date_sql_mode) :
              ObTimeConverter::str_to_datetime(utf8_string, cvrt_ctx, mdt_value.datetime_,
                                               &res_scale, date_sql_mode);
      if (ret == OB_ERR_UNEXPECTED_TZ_TRANSITION && CM_IS_WARN_ON_FAIL(cast_mode)) {
        ret = OB_SUCCESS;
      } else if (CAST_FAIL(ret)) {
      }
      value = mdt_value.datetime_;
    }
  }
  return ret;
}

template <typename Value>
static int string_datetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObScale res_scale = -1;
  ObString utf8_string;
  int64_t value = 0;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObDateTimeTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(common_string_datetime(expect_type, params, in, value, cast_mode))) {
  } else {
    // check zero date for scale over mode
    if (CM_IS_ERROR_ON_SCALE_OVER(cast_mode) &&
        (value == ObTimeConverter::ZERO_DATE ||
        value == ObTimeConverter::ZERO_DATETIME)) {
      ret = OB_INVALID_DATE_VALUE;
      LOG_USER_ERROR(OB_INVALID_DATE_VALUE, utf8_string.length(), utf8_string.ptr(), "");
    }

    if (OB_SUCC(ret)) {
      SET_RES_DATETIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int string_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                           const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObScale res_scale = -1;
  ObString utf8_string;
  ObMySQLDateTime mdt_value = 0;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(common_string_datetime(expect_type, params, in, mdt_value.datetime_, cast_mode))) {
  } else {
    // check zero date for scale over mode
    if (CM_IS_ERROR_ON_SCALE_OVER(cast_mode) &&
        (mdt_value == ObTimeConverter::MYSQL_ZERO_DATE ||
        mdt_value == ObTimeConverter::MYSQL_ZERO_DATETIME)) {
      ret = OB_INVALID_DATE_VALUE;
      LOG_USER_ERROR(OB_INVALID_DATE_VALUE, utf8_string.length(), utf8_string.ptr(), "");
    }

    if (OB_SUCC(ret)) {
      SET_RES_MDATETIME(out);
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int string_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int32_t value = 0;
  ObDateSqlMode date_sql_mode;
  date_sql_mode.allow_invalid_dates_ = CM_IS_ALLOW_INVALID_DATES(cast_mode);
  date_sql_mode.no_zero_date_ = CM_IS_NO_ZERO_DATE(cast_mode);
  date_sql_mode.no_zero_in_date_ = CM_IS_NO_ZERO_IN_DATE(cast_mode);
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObDateTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(ObTimeConverter::str_to_date(in.get_string(), value, date_sql_mode))) {
  } else if (CM_IS_ERROR_ON_SCALE_OVER(cast_mode) && value == ObTimeConverter::ZERO_DATE) {
    // check zero date for scale over mode
    ret = OB_INVALID_DATE_VALUE;
    LOG_USER_ERROR(OB_INVALID_DATE_VALUE, in.get_string().length(), in.get_string().ptr(), "");
  } else {
    SET_RES_DATE(out);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int string_mdate(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObMySQLDate md_value = 0;
  ObDateSqlMode date_sql_mode;
  date_sql_mode.allow_invalid_dates_ = CM_IS_ALLOW_INVALID_DATES(cast_mode);
  date_sql_mode.no_zero_date_ =
    CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_DATE(cast_mode);
  date_sql_mode.no_zero_in_date_ =
    CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_IN_DATE(cast_mode);
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(ObTimeConverter::str_to_mdate(in.get_string(), md_value, date_sql_mode))) {
  } else if (CM_IS_ERROR_ON_SCALE_OVER(cast_mode) && md_value == ObTimeConverter::MYSQL_ZERO_DATE) {
    // check zero date for scale over mode
    ret = OB_INVALID_DATE_VALUE;
    LOG_USER_ERROR(OB_INVALID_DATE_VALUE, in.get_string().length(), in.get_string().ptr(), "");
  } else {
    SET_RES_MDATE(out);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int string_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  int64_t value = 0;
  ObScale res_scale = -1;
  bool need_truncate = CM_IS_COLUMN_CONVERT(cast_mode) ? CM_IS_TIME_TRUNCATE_FRACTIONAL(cast_mode) : false;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObTimeTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(ObTimeConverter::str_to_time(in.get_string(), value, &res_scale, need_truncate))) {
  } else {
    SET_RES_TIME(out);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int string_year(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObYearTC != sql_type_class(expect_type))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type",
         K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(string_int(ObIntType, params, in, int64,
                                CM_UNSET_STRING_INTEGER_TRUNC(CM_SET_WARN_ON_FAIL(cast_mode))))) {
  } else if (0 == int64.get_int()) {
    const uint8_t base_year = 100;
    uint8_t value = 4 == in.get_string().length() ? ObTimeConverter::ZERO_YEAR : base_year;
    SET_RES_YEAR(out);
  } else if (CAST_FAIL(int_year(ObYearType, params, int64, out, cast_mode))) {
  } else if (CAST_FAIL(params.warning_)) {
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int string_string(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                   && ObTextTC != in.get_type_class()
                   && ObGeometryTC != in.get_type_class())
      || OB_UNLIKELY(ObStringTC != sql_type_class(expect_type)
                     && ObTextTC != sql_type_class(expect_type)))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (ObTextTC == in.get_type_class() && in.is_outrow()) {
    ret = OB_NOT_SUPPORTED;
  } else {
    ObString str;
    // Consider different character set scenarios
    if (OB_FAIL(in.get_string(str))) {
    } else if (0 != str.length()
        && CS_TYPE_BINARY != in.get_collation_type()
        && CS_TYPE_BINARY != params.dest_collation_
        && (ObCharset::charset_type_by_coll(in.get_collation_type())
            != ObCharset::charset_type_by_coll(params.dest_collation_))) {
      char *buf = NULL;
      // buf_len is related to the encoding length, latin1 uses 1 byte to encode a character, utf8mb4 uses 1 to 4 bytes
      // CharConvertFactorNum is the multiple of the applied memory size
      int32_t buf_len = str.length() * ObCharset::CharConvertFactorNum;
      uint32_t result_len = 0;
      if (OB_UNLIKELY(NULL == (buf = static_cast<char*>(params.alloc(buf_len))))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc memory failed", K(ret));
      } else if (OB_FAIL(ObCharset::charset_convert(in.get_collation_type(),
                                                    str.ptr(),
                                                    str.length(),
                                                    params.dest_collation_,
                                                    buf,
                                                    buf_len,
                                                    result_len))) {
                if (params.is_ignore_ || CM_IS_IGNORE_CHARSET_CONVERT_ERR(cast_mode)) {
                    ObString question_mark = ObCharsetUtils::get_const_str(params.dest_collation_, '?');
                    int32_t str_offset = 0;
                    int64_t buf_offset = 0;
                    while (str_offset < str.length() && buf_offset + question_mark.length() <= buf_len) {
                        int64_t offset = ObCharset::charpos(in.get_collation_type(),
                str.ptr() + str_offset, str.length() - str_offset, 1);
                        if (OB_UNLIKELY(0 == offset)) {
                          break;
                        }
                        ret = ObCharset::charset_convert(in.get_collation_type(), str.ptr() + str_offset,
                static_cast<uint32_t>(offset), params.dest_collation_, buf + buf_offset, buf_len - buf_offset, result_len);
                        str_offset += offset;
                        if (OB_SUCCESS == ret) {
                            buf_offset += result_len;
                        } else {
                            MEMCPY(buf + buf_offset, question_mark.ptr(), question_mark.length());
                            buf_offset += question_mark.length();
                        }
                    }
                    if (str_offset < str.length()) {
                      ret = OB_SIZE_OVERFLOW;
                    } else {
                        result_len = static_cast<uint32_t>(buf_offset);
                        ret = OB_SUCCESS;
                    }
                }
      }

      LOG_DEBUG("convert result", K(str), "result", ObHexEscapeSqlStr(ObString(result_len, buf)));

      if (OB_SUCC(ret)) {
        if (ObTextTC == sql_type_class(expect_type)) {
          out.set_lob_value(expect_type, buf, static_cast<int32_t>(result_len));
        } else {
          out.set_string(expect_type, buf, static_cast<int32_t>(result_len));
        }
        if (CS_TYPE_INVALID != in.get_collation_type()) {
          out.set_collation_type(params.dest_collation_);
        }
      }
    } else {
      // When convert blob to other charset, need to align to mbminlen of destination charset
      // by add '\0' prefix in mysql mode. (see mysql String::copy)
      int64_t align_offset = 0;
      const ObCharsetInfo *cs = NULL;
      if (CS_TYPE_BINARY == in.get_collation_type()
          && NULL != (cs = ObCharset::get_charset(params.dest_collation_))) {
        if (cs->mbminlen > 0 && in.get_string_len() % cs->mbminlen != 0) {
          align_offset = cs->mbminlen - in.get_string_len() % cs->mbminlen;
        }
      }
      Value tmp_out;
      ObString str;
      if (OB_FAIL(check_convert_string(expect_type, params, in, tmp_out))) {
      } else if (tmp_out.is_string_or_lob_locator_type()) { // may has locator header if it is text types
        if (OB_FAIL(tmp_out.get_string(str))) {
        }
      } else { // other types, e.g. raw
        str = tmp_out.get_string();
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(copy_string(params, expect_type, str, out, align_offset))) {
      } else {
        if (CS_TYPE_INVALID != tmp_out.get_collation_type()) {
          out.set_collation_type(tmp_out.get_collation_type());
        }
        if (ObTextTC == sql_type_class(expect_type)) {
          out.set_inrow();
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    res_length = static_cast<ObLength>(out.get_string_len());
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int string_bit(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t value = 0;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class()
                  && ObTextTC != in.get_type_class()
                  && ObGeometryTC != in.get_type_class())
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    int32_t bit_len = 0;
    const ObString &str = in.get_string();
    if (OB_FAIL(get_bit_len(str, bit_len))) {
    } else if (OB_UNLIKELY(bit_len <= 0)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      if (ObHexStringType == in.get_type() && str.empty()) {
        value = 0;
        ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
      } else if (OB_UNLIKELY(bit_len > OB_MAX_BIT_LENGTH)) {
        value = UINT64_MAX;
        ret = OB_ERR_DATA_TOO_LONG;
      } else {
        //Convert str to the corresponding uint64 based on its binary value
        value = hex_to_uint64(str);
      }
      if (OB_FAIL(ret) && CM_IS_WARN_ON_FAIL(cast_mode)) {
        params.warning_ = OB_DATA_OUT_OF_RANGE;
        ret = OB_SUCCESS;
      }
      if (OB_SUCC(ret)) {
        SET_RES_BIT(out);
        SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
      }
    }
  }
  return ret;
}



template <typename Value>
static int string_text(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode,
                       const ObLobLocatorV2 *lob_locator)
{
  int ret = OB_SUCCESS;
  Value tmp_out;
  ObString res_str;
  bool is_null_result = false;
  bool is_different_charset_type = (ObCharset::charset_type_by_coll(in.get_collation_type())
                                    != ObCharset::charset_type_by_coll(params.expect_obj_collation_));
  bool has_lob_header = (expect_type != ObTinyTextType);
  if (is_different_charset_type) {
    if (OB_FAIL(string_string(expect_type, params, in, tmp_out, cast_mode))) {
    } else if (tmp_out.is_null()) {
      out.set_null();
      is_null_result = true;
    } else {
      res_str = tmp_out.get_string();
    }
  } else if (OB_FAIL(in.get_string(res_str))) {
  } else { /* do nothing */ }

  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
  } else if (has_lob_header && nullptr == lob_locator) {
    // fast path for mysql string_text
    out = tmp_out; // copy meta
    if (OB_FAIL(common::lob_helper::pack_to_disk_inrow_lob(params, res_str, expect_type, out))) {
    }
  } else {
    CastTextResult<Value> str_result(expect_type, &params, &out, has_lob_header);
    if (lob_locator == NULL) {
      if (OB_FAIL(str_result.init(res_str.length()))) {
        } else if (OB_FAIL(str_result.append(res_str.ptr(), res_str.length()))) {
        } else { /* do nothing */ }
    } else if (OB_FAIL(str_result.copy(lob_locator))) {
    } else { /* do nothing*/ }

    if (OB_SUCC(ret)) {
      out = tmp_out; // copy meta
      str_result.set_result();
    }
  }
  return ret;
}

template <typename Value>
static int string_json(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  int32_t res_length = -1;
  bool need_charset_convert = ((CS_TYPE_BINARY != in.get_collation_type()) &&
                               (ObCharset::charset_type_by_coll(in.get_collation_type()) !=
                                ObCharset::charset_type_by_coll(params.dest_collation_)));
  if ((ObCharset::charset_type_by_coll(params.dest_collation_) != CHARSET_UTF8MB4)) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  } else if (CM_IS_COLUMN_CONVERT(cast_mode) && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_UNLIKELY(ObStringTC != in.get_type_class() && ObTextTC != in.get_type_class())
      || OB_UNLIKELY(ObJsonType != expect_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (need_charset_convert && OB_FAIL(string_string(ObLongTextType, params, in, tmp_val, cast_mode))) {
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObIJsonBase *j_base = NULL;
    ObString j_text = (need_charset_convert ? tmp_val.get_string() : in.get_string());
    ObJsonOpaque j_opaque(j_text, in.get_type());
    ObJsonString j_string(j_text.ptr(), j_text.length());
    ObJsonNull j_null;
    ObJsonNode *j_tree = NULL;
    uint32_t parse_flag = ObJsonParser::JSN_RELAXED_FLAG;
    SqlValueType in_type = in.get_type();
    bool is_convert_jstr_type = (in_type == ObTinyTextType
                                 || in_type == ObTextType
                                 || in_type == ObMediumTextType
                                 || in_type == ObLongTextType);
    if (expect_type == ObJsonType && j_text.length() == 0 && cast_mode == 0) { // add column json null
      j_base = &j_null;
    } else if (CS_TYPE_BINARY == in.get_collation_type()) {
      j_base = &j_opaque;
    } else if ((CM_IS_SQL_AS_JSON_SCALAR(cast_mode) && ob_is_string_type(in_type))
                || (CM_IS_IMPLICIT_CAST(cast_mode)
                    && !CM_IS_COLUMN_CONVERT(cast_mode)
                    && !CM_IS_JSON_VALUE(cast_mode)
                    && is_convert_jstr_type)) {
      // consistent with mysql: TINYTEXT, TEXT, MEDIUMTEXT, and LONGTEXT. We want to treat them like strings
      ret = OB_SUCCESS;
      j_base = &j_string;
      if ((CM_IS_SQL_AS_JSON_SCALAR(cast_mode) && ob_is_string_type(in_type)) && j_text.compare("null") == 0) {
        j_base = &j_null;
      }
    } else if (OB_FAIL(ObJsonParser::get_tree(params.allocator_v2_, j_text,
                                              j_tree, parse_flag,
                                              params.json_max_depth_))) {
      if (CM_IS_IMPLICIT_CAST(cast_mode)
                     && !CM_IS_COLUMN_CONVERT(cast_mode)
                     && is_convert_jstr_type) {
        ret = OB_SUCCESS;
        j_base = &j_string;
      } else {
        if (CM_IS_COLUMN_CONVERT(cast_mode)) {
          ret = OB_ERR_INVALID_JSON_TEXT;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
        } else {
          ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
        }
      }
    } else {
      j_base = j_tree;
    }

    if (OB_SUCC(ret)) {
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, raw_bin))) {
      } else {
        res_length = out.get_val_len();
      }
    }
  }

  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_TEXT, res_length);
  return ret;
}

// Notice: used in PL
template <typename Value>
static int text_text(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  SqlValueType in_type = in.get_type();
  SqlValueType out_type = expect_type;
  ObCollationType in_cs_type = in.get_collation_type();
  ObCollationType out_cs_type = params.dest_collation_;
  ObString in_str;
  ObString res_str;
  Value tmp_out;
  Value tmp_in = in;
  ObString row_str = in.get_string();
  ObTextStringIter instr_iter(in.get_type(), in.get_collation_type(), in.get_string(), in.has_lob_header());

  ObLobLocatorV2 in_lob_locator;
  in_lob_locator.assign_buffer(row_str.ptr(), row_str.length(), in.has_lob_header());
  bool is_persist = in_lob_locator.is_valid() && in_lob_locator.is_persist_lob();
  bool is_delta = in_lob_locator.is_valid() && in_lob_locator.is_delta_temp_lob();
  bool is_freed = in_lob_locator.is_valid() && in_lob_locator.is_freed();
  bool is_different_charset_type = (ObCharset::charset_type_by_coll(in_cs_type)
                                    != ObCharset::charset_type_by_coll(out_cs_type));
  bool is_any_cs = (in_cs_type == CS_TYPE_ANY || out_cs_type == CS_TYPE_ANY);
  const bool is_tiny_to_others = ((in_type == ObTinyTextType && out_type != ObTinyTextType)
                                  || (in_type != ObTinyTextType && out_type == ObTinyTextType));
  if (OB_UNLIKELY(ObTextTC != in.get_type_class()
      || OB_UNLIKELY(ObTextTC != sql_type_class(expect_type)))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if ((is_different_charset_type && !is_delta && !(is_persist && is_any_cs) && !is_freed)
             || is_tiny_to_others ) {
    if (OB_FAIL(init_lob_text_iter(instr_iter, params))) {
    } else if (OB_FAIL(instr_iter.get_full_data(in_str))) {
    } else if (FALSE_IT(tmp_in.set_string(ObVarcharType, in_str))) {
    } else if (in_str.length() > 0 && OB_FAIL(string_string(ObVarcharType, params, tmp_in, tmp_out, cast_mode))) {
    } else if (in_str.length() == 0 &&
               FALSE_IT(tmp_out.set_string(tmp_in.get_type(), in_str)) &&
               FALSE_IT(tmp_out.set_collation_type(params.expect_obj_collation_))) {
    } else if (tmp_out.is_null()) {
      // for pl, should not get null for lob locator, return empty full tmp lob
      out = tmp_out; // copy meta
      out.set_lob_value(expect_type, row_str.ptr(), row_str.length());
    } else if (OB_FAIL(tmp_out.get_string(res_str))) {
    } else {
      bool has_lob_header = (expect_type != ObTinyTextType);
      CastTextResult<Value> str_result(expect_type, &params, &out, has_lob_header);
      if (OB_FAIL(str_result.init(res_str.length()))) {
      } else if (OB_FAIL(str_result.append(res_str.ptr(), res_str.length()))) {
      } else {
        out = tmp_out; // copy meta
        str_result.set_result();
      }
    }
  } else {
    out = in;
    out.set_type(expect_type); // set expect_type
  }

  return ret;
}

template <typename Value>
static int text_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObString in_str;
  ObTextStringIter instr_iter(in.get_type(), in.get_collation_type(), in.get_string(), in.has_lob_header());
  if (OB_UNLIKELY(ObTextTC != in.get_type_class()
      || OB_UNLIKELY(ObStringTC != sql_type_class(expect_type)))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(init_lob_text_iter(instr_iter, params))) {
  } else if (OB_FAIL(instr_iter.get_full_data(in_str))) {
  } else {
    Value tmp_in = in;
    tmp_in.set_string(ObVarcharType, in_str);
    if (OB_FAIL(string_string(expect_type, params, tmp_in, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int text_json(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_in = in;
  ObString in_str;
  ObTextStringIter instr_iter(in.get_type(), in.get_collation_type(), in.get_string(), in.has_lob_header());
  if (OB_UNLIKELY(ObTextTC != in.get_type_class()
      || OB_UNLIKELY(ObJsonTC != sql_type_class(expect_type)))) {
     ret = OB_ERR_UNEXPECTED;
     LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(init_lob_text_iter(instr_iter, params))) {
  } else if (OB_FAIL(instr_iter.get_full_data(in_str))) {
  } else {
    tmp_in.set_string(tmp_in.get_type(), in_str);
    tmp_in.set_inrow();
    if (OB_FAIL(string_json(expect_type, params, tmp_in, out, cast_mode))) {
    }
  }
  return ret;
}


////////////////////////////////////////////////////////////
// bit -> XXX
template <typename Value>
static int bit_int(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  int64_t value = 0;
  if (OB_UNLIKELY(ObBitTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    value =static_cast<int64_t>(in.get_bit());
    if (expect_type < ObIntType
        && CM_NEED_RANGE_CHECK(cast_mode)
        && CAST_FAIL(int_range_check(expect_type, value, value))) {
    } else {
      SET_RES_INT(out);
      res_precision = get_precision_for_integer(out.get_int());
      SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
    }
  }
  return ret;
}

template <typename Value>
static int bit_uint(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  uint64_t value = 0;
  if (OB_UNLIKELY(ObBitTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    value = static_cast<uint64_t>(in.get_bit());
    if (expect_type < ObUInt64Type
        && CM_NEED_RANGE_CHECK(cast_mode)
        && CAST_FAIL(uint_upper_check(expect_type, value))) {
    } else {
      SET_RES_UINT(out);
      res_precision = get_precision_for_integer(out.get_uint64());
      SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
    }
  }
  return ret;
}

template <typename Value>
static int bit_double(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int bit_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value dbl;
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || ObFloatTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(bit_double(ObDoubleType, params, in, dbl, cast_mode))) {
  } else if (OB_FAIL(double_float(expect_type, params, dbl, out, cast_mode))) {
  }
  //has set accuracy in prev double_float call
  return ret;
}


template <typename Value>
static int bit_double(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  double value = 0.0;
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || ObDoubleTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    value = static_cast<double>(in.get_bit());
    SET_RES_DOUBLE(out);
    SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  }
  return ret;
}

template <typename Value>
static int bit_number(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  number::ObNumber nmb;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObBitTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(nmb.from(in.get_bit(), params))) {
  } else {
    out.set_number(expect_type, nmb);
    res_precision = get_precision_for_integer(in.get_bit());
    SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  }
  return ret;
}

template <typename Value>
static int bit_datetime(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t bit_value = in.get_bit();
  int64_t value = 0;
  ObMySQLDateTime mdt_value = 0;
  ObScale res_scale = -1;
  ObDateSqlMode date_sql_mode;
  date_sql_mode.allow_invalid_dates_ = CM_IS_ALLOW_INVALID_DATES(cast_mode);
  date_sql_mode.no_zero_date_ = CM_IS_NO_ZERO_DATE(cast_mode);
  date_sql_mode.no_zero_in_date_ = CM_IS_NO_ZERO_IN_DATE(cast_mode);
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || (ObDateTimeTC != sql_type_class(expect_type)
                       && ObMySQLDateTimeTC != sql_type_class(expect_type))))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (CM_IS_COLUMN_CONVERT(cast_mode)) {
    // if cast mode is column convert, using bit as int64 to do cast.
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    ret = ObMySQLDateTimeType == expect_type ?
            ObTimeConverter::int_to_mdatetime(bit_value, 0, cvrt_ctx, mdt_value, date_sql_mode) :
            ObTimeConverter::int_to_datetime(bit_value, 0, cvrt_ctx, mdt_value.datetime_, date_sql_mode);
    if (CAST_FAIL(ret)) {
    }
  } else {
    // using bit as char array to do cast.
    ObScale scale = in.get_scale();
    const int32_t BUF_LEN = (OB_MAX_BIT_LENGTH + 7) / 8;
    int64_t pos = 0;
    char buf[BUF_LEN];
    MEMSET(buf, 0, BUF_LEN);
    if (OB_FAIL(bit_to_char_array(bit_value, scale, buf, BUF_LEN, pos))) {
    } else {
      ObString str(pos, buf);
      ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
      ret = ObMySQLDateTimeType == expect_type ?
              ObTimeConverter::str_to_mdatetime(str, cvrt_ctx, mdt_value, &res_scale, date_sql_mode) :
              ObTimeConverter::str_to_datetime(str, cvrt_ctx, value, &res_scale, date_sql_mode);
      if (CAST_FAIL(ret)) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (ObMySQLDateTimeType == expect_type) {
      SET_RES_MDATETIME(out);
    } else {
      SET_RES_DATETIME(out);
    }
    SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  }
  return ret;
}

template <typename Value>
static int bit_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(bit_datetime(expect_type, params, in, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int bit_date(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t bit_value = in.get_bit();
  int32_t value = 0;
  ObScale res_scale = -1;
  ObDateSqlMode date_sql_mode;
  date_sql_mode.allow_invalid_dates_ = CM_IS_ALLOW_INVALID_DATES(cast_mode);
  date_sql_mode.no_zero_date_ = CM_IS_NO_ZERO_DATE(cast_mode);
  date_sql_mode.no_zero_in_date_ = CM_IS_NO_ZERO_IN_DATE(cast_mode);
  ObMySQLDate md_value = 0;
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || (ObDateTC != sql_type_class(expect_type)
                       && ObMySQLDateTC != sql_type_class(expect_type))))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (CM_IS_COLUMN_CONVERT(cast_mode)) {
    // if cast mode is column convert, using bit as int64 to do cast.
    ret = ObMySQLDateType == expect_type ?
            ObTimeConverter::int_to_mdate(bit_value, md_value, date_sql_mode) :
            ObTimeConverter::int_to_date(bit_value, value, date_sql_mode);
    if (CAST_FAIL(ret)) {
    }
  } else {
    // using bit as char array to do cast.
    ObScale scale = in.get_scale();
    const int32_t BUF_LEN = (OB_MAX_BIT_LENGTH + 7) / 8;
    int64_t pos = 0;
    char buf[BUF_LEN];
    MEMSET(buf, 0, BUF_LEN);
    if (OB_FAIL(bit_to_char_array(bit_value, scale, buf, BUF_LEN, pos))) {
    } else {
      ObString str(pos, buf);
      ret = ObMySQLDateType == expect_type ?
              ObTimeConverter::str_to_mdate(str, md_value, date_sql_mode) :
              ObTimeConverter::str_to_date(str, value, date_sql_mode);
      if (CAST_FAIL(ret)) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (ObMySQLDateType == expect_type) {
      SET_RES_MDATE(out);
    } else {
      SET_RES_DATE(out);
    }
    SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  }
  return ret;
}

template <typename Value>
static int bit_mdate(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(bit_date(expect_type, params, in, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int bit_time(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t bit_value = in.get_bit();
  int64_t value = 0;
  ObScale res_scale = -1;
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || ObTimeTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (CM_IS_COLUMN_CONVERT(cast_mode)) {
    // if cast mode is column convert, using bit as int64 to do cast.
    if (CAST_FAIL(ObTimeConverter::int_to_time(bit_value, value))) {
    }
  } else {
    // using bit as char array to do cast.
    ObScale scale = in.get_scale();
    const int32_t BUF_LEN = (OB_MAX_BIT_LENGTH + 7) / 8;
    int64_t pos = 0;
    char buf[BUF_LEN];
    MEMSET(buf, 0, BUF_LEN);
    if (OB_FAIL(bit_to_char_array(bit_value, scale, buf, BUF_LEN, pos))) {
    } else {
      ObString str(pos, buf);
      if (CAST_FAIL(ObTimeConverter::str_to_time(str, value, &res_scale))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    SET_RES_TIME(out);
    SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  }
  return ret;
}

template <typename Value>
static int bit_year(const SqlValueType expect_type, DatumCastParams &params,
                        const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t bit_value = in.get_bit();
  uint8_t value = 0;
  ObScale res_scale = -1;
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || ObYearTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (CAST_FAIL(ObTimeConverter::int_to_year(bit_value, value))) {
  } else {
    SET_RES_YEAR(out);
    SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, res_scale, DEFAULT_LENGTH_FOR_TEMPORAL);
  }
  return ret;
}

template <typename Value>
static int bit_string(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t value = in.get_bit();
  ObLength res_length = -1;
  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
                   || (ObStringTC != sql_type_class(expect_type)
                       && ObTextTC != sql_type_class(expect_type))))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (CM_IS_COLUMN_CONVERT(cast_mode)) {
    // if cast mode is column convert, using bit as int64 to do cast.
    ObFastFormatInt ffi(value);
    ObString tmp_str;
    if (OB_FAIL(convert_string_collation(ObString(ffi.length(), ffi.ptr()),
                CS_TYPE_BINARY, tmp_str, params.dest_collation_, params))) {
    } else if (OB_FAIL(copy_string(params, expect_type, tmp_str.ptr(), tmp_str.length(), out))) {
    }
  } else {
    // using bit as char array to do cast.
    ObScale scale = in.get_scale();
    const int32_t BUF_LEN = (OB_MAX_BIT_LENGTH + 7) / 8;
    int64_t pos = 0;
    char tmp_buf[BUF_LEN];
    MEMSET(tmp_buf, 0, BUF_LEN);
    ObString tmp_str;
    if (OB_FAIL(bit_to_char_array(value, scale, tmp_buf, BUF_LEN, pos))) {
    } else if (OB_FAIL(convert_string_collation(ObString(pos, tmp_buf),
                CS_TYPE_BINARY, tmp_str, params.dest_collation_, params))) {
    } else if (OB_FAIL(copy_string(params, expect_type, tmp_str.ptr(), tmp_str.length(), out))) {
    }
  }
  if (OB_SUCC(ret)) {
    res_length = static_cast<ObLength>(out.get_string_len());
    out.set_collation_type(params.dest_collation_);
    SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_STRING, res_length);
  }
  return ret;
}

template <typename Value>
static int bit_bit(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  uint64_t value = in.get_bit();
  int32_t bit_len = 0;
  if (OB_UNLIKELY(ObBitTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(get_bit_len(value, bit_len))) {
  } else {
    SET_RES_BIT(out);
    SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
  }
  return ret;

}




CAST_TO_TEXT_METHOD(bit, ObBitTC);

template <typename Value>
static int bit_json(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  UNUSED(cast_mode);

  if (OB_UNLIKELY((ObBitTC != in.get_type_class()
      || (ObJsonTC != sql_type_class(expect_type))))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObIAllocator *allocator = params.allocator_v2_;
    uint64_t value = in.get_bit();
    ObScale scale = in.get_scale();
    const int32_t BUF_LEN = (OB_MAX_BIT_LENGTH + 7) / 8;
    char buf[BUF_LEN] = {0};
    int64_t pos = 0;
    if (OB_FAIL(bit_to_char_array(value, scale, buf, BUF_LEN, pos))) {
    } else if (OB_ISNULL(allocator)) {
      ret= OB_ERR_UNEXPECTED;
      LOG_ERROR("NULL allocator in json cast function", K(ret), K(in), K(expect_type));
    } else {
      common::ObString j_value(pos, buf);
      ObJsonOpaque j_opaque(j_value, ObBitType);
      ObIJsonBase *j_base = &j_opaque;
      ObString raw_bin;
      if (OB_FAIL(share::ObJsonWrapper::get_raw_binary(j_base, raw_bin, params.allocator_v2_))) {
      } else if (OB_FAIL(copy_string(params, expect_type, raw_bin.ptr(), raw_bin.length(), out))) {
      } else if (OB_FAIL(set_json_bin_res(&params, &out, out.get_string()))) {
      } else {
        ObLength res_length = static_cast<ObLength>(out.get_string_len());
        SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_STRING, DEFAULT_SCALE_FOR_STRING, res_length);
      }
    }
  }

  return ret;
}


template <typename Value>
static bool enum_set_has_subschema_value(const Value &in)
{
  return in.get_scale() == ObEnumSetMeta::MetaState::SQL;
}

template <typename Value>
static int enumset_string_value(
    const Value &in,
    DatumCastParams &params,
    ObTextStringResult &text_result)
{
  int ret = OB_SUCCESS;
  const SqlValueType input_type = in.get_type();
  if (0 == in.get_uint64()) {
    // Empty enum/set values are represented by an empty string.
  } else if (OB_ISNULL(params.runtime_)) {
    ret = OB_ERR_UNDEFINED;
  } else {
    const ObIArray<ObString> *values = nullptr;
    ObCollationType collation_type = CS_TYPE_INVALID;
    const uint16_t subschema_id = in.get_meta().get_subschema_id();
    if (OB_FAIL(params.runtime_->get_enum_set_values(
            subschema_id, values, collation_type))) {
    } else if (OB_ISNULL(values)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (ObEnumType == input_type) {
      ret = enumset_str::enum_to_str(in.get_uint64(), *values, text_result);
    } else if (ObSetType == input_type) {
      ret = enumset_str::set_to_str(
          collation_type, in.get_uint64(), *values, text_result);
    } else {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

// Temporary legacy entry points; both backends share native field access
// algorithms. These wrappers do not construct or materialize any value.


////////////////////////////////////////////////////////////////
// enum -> XXX
/*//Conversion of obj from the same enum column should use this interface, conversion from different enum columns should use enumsetinner_enum

////////////////////////////////////////////////////////////////
// set -> XXX
//Conversion of obj from the same set column should use this interface, conversion from different set columns should use enumsetinner_set


*/




////////////////////////////////////////////////////////////
// enum -> XXX

template <typename Value>
static int enumset_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  //we regard enum as uint64 when int_value is needed
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    Value uint64_in;
    uint64_in.set_uint64(in.get_uint64());
    ret = uint_int(expect_type, params, uint64_in, out, cast_mode);
  }
  return ret;
}

template <typename Value>
static int enumset_uint(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    uint64_t value = in.get_uint64();
    if (CAST_FAIL(uint_upper_check(expect_type, value))) {
    } else {
      out.set_uint(expect_type, value);
      res_precision = get_precision_for_integer(value);
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int enumset_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    out.set_float(expect_type, static_cast<float>(in.get_uint64()));
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int enumset_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else {
    out.set_double(expect_type, static_cast<double>(in.get_uint64()));
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int enumset_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  number::ObNumber nmb;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(nmb.from(in.get_uint64(), params))) {
  } else {
    out.set_number(expect_type, nmb);
    res_precision = get_precision_for_integer(in.get_uint64());
  }
  UNUSED(cast_mode);
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int enumset_datetime(const SqlValueType expect_type, DatumCastParams &params,
                            const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (OB_FAIL(string_datetime(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int enumset_date(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (OB_FAIL(string_date(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int enumset_time(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (OB_FAIL(string_time(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int enumset_year(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value int64;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type",
        K(ret), K(in), K(expect_type));
    } else if (OB_FAIL(enumset_int(ObIntType, params, in, int64, CM_UNSET_NO_CAST_INT_UINT(cast_mode)))) {
    } else if (OB_FAIL(int_year(expect_type, params, int64, out, cast_mode))) {
    }
    SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
    return ret;
  }

template <typename Value>
static int enumset_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObStringTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(expect_type, false, params.allocator_v2_);
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else {
      ObString es_str;
      text_result.get_result_buffer(es_str);
      out.set_string(expect_type, es_str);
      if (OB_SUCC(ret)) {
        res_length = static_cast<ObLength>(out.get_string_len());
        out.set_collation_type(params.dest_collation_);
      }
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  UNUSED(cast_mode);
  return ret;
}

template <typename Value>
static int enumset_text(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObTextTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (FALSE_IT(temp_obj.set_collation_type(params.dest_collation_))) {
    } else if (OB_FAIL(string_text(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int enumset_bit(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  UNUSED(cast_mode);
  int ret = OB_SUCCESS;
  uint64_t value = in.get_uint64();
  int32_t bit_len = 0;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (OB_FAIL(get_bit_len(value, bit_len))) {
  } else {
    SET_RES_BIT(out);
    SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
  }
  return ret;
}

template <typename Value>
static int enumset_json(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObJsonTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (OB_FAIL(string_json(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int enumset_mdate(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (OB_FAIL(string_mdate(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}

template <typename Value>
static int enumset_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (!enum_set_has_subschema_value(in)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class()
                  || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObTextStringResult text_result(ObVarcharType, false, params.allocator_v2_);
    ObString es_str;
    Value temp_obj;
    if (OB_FAIL(enumset_string_value(in, params, text_result))) {
    } else if (FALSE_IT(text_result.get_result_buffer(es_str))) {
    } else if (FALSE_IT(temp_obj.set_varchar(es_str))) {
    } else if (OB_FAIL(string_mdatetime(expect_type, params, temp_obj, out, cast_mode))) {
    }
  }
  return ret;
}


////////////////////////////////////////////////////////////
// Raw -> XXX

// lob locator v1 raw to text type

////////////////////////////////////////////////////////////
// rowid -> XXX

// Text -> XXX
#define CAST_TEXT_TO_OTHER_TYPE(TYPE, TYPE_CLASS)                                   \
template <typename Value>                                                           \
static int text_##TYPE(const SqlValueType expect_type, DatumCastParams &params,        \
                       const Value &in, Value &out, const ObCastMode cast_mode)     \
{                                                                                   \
  int ret = OB_SUCCESS;                                                             \
  if (OB_UNLIKELY(ObTextTC != in.get_type_class()                                   \
                  || TYPE_CLASS != sql_type_class(expect_type))) {               \
    ret = OB_ERR_UNEXPECTED;                                                        \
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));       \
  } else {                                                                          \
    ObString in_str;                                                                \
    ObTextStringIter instr_iter(in.get_type(), in.get_collation_type(), in.get_string(), in.has_lob_header());                                                \
    if (OB_FAIL(init_lob_text_iter(instr_iter, params))) {                         \
      LOG_WARN("init lob str iter failed ", K(ret), K(in));               \
    } else if (OB_FAIL(instr_iter.get_inrow_or_outrow_prefix_data(in_str))) {       \
      LOG_WARN("get full data from lob str iter failed ", K(ret), K(in)); \
    } else {                                                                        \
        Value tmp_obj;                                                              \
        tmp_obj.set_varchar(in_str);                                                \
        tmp_obj.set_collation_type(in.get_collation_type());                        \
        if (OB_FAIL(string_##TYPE(expect_type, params, tmp_obj, out, cast_mode))) { \
          LOG_WARN("Text to " #TYPE "failed", K(ret), K(tmp_obj));        \
        }                                                                           \
      }                                                                             \
    }                                                                               \
  return ret;                                                                       \
}

template <typename Value>
static int string_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode);

CAST_TEXT_TO_OTHER_TYPE(int, ObIntTC);
CAST_TEXT_TO_OTHER_TYPE(uint, ObUIntTC);
CAST_TEXT_TO_OTHER_TYPE(double, ObDoubleTC);
CAST_TEXT_TO_OTHER_TYPE(float, ObFloatTC);
CAST_TEXT_TO_OTHER_TYPE(number, ObNumberTC);
CAST_TEXT_TO_OTHER_TYPE(datetime, ObDateTimeTC);
CAST_TEXT_TO_OTHER_TYPE(date, ObDateTC);
CAST_TEXT_TO_OTHER_TYPE(mdate, ObMySQLDateTC);
CAST_TEXT_TO_OTHER_TYPE(mdatetime, ObMySQLDateTimeTC);
CAST_TEXT_TO_OTHER_TYPE(time, ObTimeTC);
CAST_TEXT_TO_OTHER_TYPE(year, ObYearTC);
CAST_TEXT_TO_OTHER_TYPE(bit, ObBitTC);
CAST_TEXT_TO_OTHER_TYPE(decimalint, ObDecimalIntTC);


////////////////////////////////////////////////////////////
// Lob -> XXX

////////////////////////////////////////////////////////////
// json -> XXX

template <typename Value>
static int json_int(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t value = 0;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_int(value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else if (expect_type < ObIntType && CAST_FAIL(int_range_check(expect_type, value, value))) {
      } else {
        SET_RES_INT(out);
        res_precision = get_precision_for_integer(value);
      }
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int json_uint(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObUIntTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    uint64_t value = 0;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_uint(value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else if (CAST_FAIL(uint_upper_check(expect_type, value))) {
      } else {
        SET_RES_UINT(out);
        res_precision = get_precision_for_integer(value);
      }
    }
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int json_double(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode);
template <typename Value>
static int json_float(const SqlValueType expect_type, DatumCastParams &params,
                      const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObFloatTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    Value obj_double;
    if (OB_FAIL(json_double(ObDoubleType, params, in, obj_double, cast_mode))) {
    } else if (OB_FAIL(double_float(expect_type, params, obj_double, out, cast_mode))) {
    }
  }

  return ret;
}

template <typename Value>
static int json_double(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObDoubleTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    double value = 0.0;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_double(value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else if (ObUDoubleType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
      } else {
        SET_RES_DOUBLE(out);
        res_precision = get_precision_for_integer(value);
      }
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int json_number(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObNumberTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    number::ObNumber value;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_number(params.allocator_v2_, value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else if (ObUNumberType == expect_type && CAST_FAIL(numeric_negative_check(value))) {
      } else {
        SET_RES_NUMBER(out);
      }
    }
  }
  SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  return ret;
}

template <typename Value>
static int json_datetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t value;
    ObString j_bin_str = in.get_string();
    ObTimeConvertCtx cvrt_ctx(params.dtc_params_.tz_info_, ObTimestampType == expect_type);
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_datetime(value, &cvrt_ctx))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        SET_RES_DATETIME(out);
      }
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int json_mdatetime(const SqlValueType expect_type, DatumCastParams &params,
                         const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObMySQLDateTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObMySQLDateTime mdt_value;
    ObString j_bin_str = in.get_string();
    ObTimeConvertCtx cvrt_ctx(NULL, false);
    cvrt_ctx.date_sql_mode_.allow_invalid_dates_ =
      CM_IS_EXPLICIT_CAST(cast_mode) ? true : CM_IS_ALLOW_INVALID_DATES(cast_mode);
    cvrt_ctx.date_sql_mode_.no_zero_date_ =
      CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_DATE(cast_mode);
    cvrt_ctx.date_sql_mode_.no_zero_in_date_ =
      CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_IN_DATE(cast_mode);
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_mdatetime(mdt_value, &cvrt_ctx))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        SET_RES_MDATETIME(out);
      }
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int json_date(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int32_t value;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_date(value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        SET_RES_DATE(out);
      }
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int json_mdate(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObMySQLDateTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObMySQLDate md_value;
    ObString j_bin_str = in.get_string();
    ObDateSqlMode date_sql_mode;
    date_sql_mode.allow_invalid_dates_ =
      CM_IS_EXPLICIT_CAST(cast_mode) ? true : CM_IS_ALLOW_INVALID_DATES(cast_mode);
    date_sql_mode.no_zero_date_ =
      CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_DATE(cast_mode);
    date_sql_mode.no_zero_in_date_ =
      CM_IS_EXPLICIT_CAST(cast_mode) ? false : CM_IS_NO_ZERO_IN_DATE(cast_mode);
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_mdate(md_value, date_sql_mode))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        SET_RES_MDATE(out);
      }
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_DATE, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int json_time(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObTimeTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t value;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_time(value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        SET_RES_TIME(out);
      }
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, MIN_SCALE_FOR_TEMPORAL, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int json_year(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() || ObYearTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    uint8_t value = 0;
    int64_t int_value = 0;
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_int(int_value, false, true))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else if (CAST_FAIL(ObTimeConverter::int_to_year(int_value, value))){
      } else {
        if (params.warning_ == OB_DATA_OUT_OF_RANGE) {
          if (CM_IS_WARN_ON_FAIL(cast_mode)) {
            value = 0;
            SET_RES_YEAR(out);
          } else {
            out.set_null(); // not change the behavior of int_year
          }
        } else {
          SET_RES_YEAR(out);
        }
      }
    }
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR, DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int json_string(const SqlValueType expect_type, DatumCastParams &params,
                       const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  // ToDo convert json to other none string type is also not allowed.
  if (OB_UNLIKELY(ObJsonType != in.get_type() ||
      OB_UNLIKELY(ObStringTC != sql_type_class(expect_type) && ObTextTC != sql_type_class(expect_type)))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if (in.is_json_outrow()) {
    ret = OB_ERR_UNEXPECTED;
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObJsonBuffer j_buf(params.allocator_v2_);
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->print(j_buf, true, j_bin_str.length()))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        // get in cs type
        bool need_charset_convert = ((CS_TYPE_BINARY == params.dest_collation_) ||
                                    (ObCharset::charset_type_by_coll(in.get_collation_type()) !=
                                      ObCharset::charset_type_by_coll(params.dest_collation_)));
        ObString temp_str_val(j_buf.length(), j_buf.ptr());

        /* this function maybe called in json generated column
        * and the generated column may has limitaion constrain the string length */
        uint64_t accuracy_max_len = 0;
        if (params.res_accuracy_ && params.res_accuracy_->get_length()) {
          accuracy_max_len = params.res_accuracy_->get_length();
        }
        if (!need_charset_convert) {
          if (accuracy_max_len > 0 && accuracy_max_len < j_buf.length()) {
            temp_str_val.assign_ptr(j_buf.ptr(), accuracy_max_len);
          }
          ret = copy_string(params, expect_type, temp_str_val, out);
          out.set_collation_type(params.dest_collation_);
        } else {
          Value tmp_obj;
          tmp_obj.set_collation_type(in.get_collation_type());
          tmp_obj.set_collation_level(in.get_collation_level());
          tmp_obj.set_string(ObLongTextType, temp_str_val);
          tmp_obj.set_inrow();
          ret = string_string(expect_type, params, tmp_obj, out, cast_mode);
          if (accuracy_max_len && accuracy_max_len < out.get_string().length()) {
            ObString tmp_str = out.get_string();
            out.set_string(expect_type, tmp_str.ptr(), static_cast<ObString::obstr_size_t>(accuracy_max_len));
          }
          if (ObTextTC == sql_type_class(expect_type)) {
            out.set_inrow();
          }
        }
      }
    }
  }
  return ret;
}

template <typename Value>
static int common_json_string(const SqlValueType expect_type,
                              DatumCastParams &params,
                              const ObCastMode cast_mode,
                              const Value &in,
                              ObString& res_str)
{
  int ret = OB_SUCCESS;
  int warning = OB_SUCCESS;
  if (OB_UNLIKELY(ObJsonType != in.get_type())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else if(OB_UNLIKELY(params.allocator_v2_ == NULL)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObJsonBuffer j_buf(params.allocator_v2_);
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      ObString j_str;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->print(j_buf, true, j_bin_str.length()))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else {
        // get json string
        uint64_t accuracy_max_len = 0;
        if (params.res_accuracy_ && params.res_accuracy_->get_length()) {
          accuracy_max_len = params.res_accuracy_->get_length();
        }
        if (accuracy_max_len > 0 && accuracy_max_len < j_buf.length()) {
          j_str.assign_ptr(j_buf.ptr(), accuracy_max_len);
        } else {
          j_str.assign_ptr(j_buf.ptr(), j_buf.length());
        }

        bool need_charset_convert = ((CS_TYPE_BINARY != params.dest_collation_) &&
                                    (ObCharset::charset_type_by_coll(in.get_collation_type()) !=
                                      ObCharset::charset_type_by_coll(params.dest_collation_)));
        if (!need_charset_convert) {
          res_str = j_str;
        } else {
          Value tmp_in;
          Value tmp_out;
          tmp_in.set_collation_type(in.get_collation_type());
          tmp_in.set_collation_level(in.get_collation_level());
          tmp_in.set_string(ObLongTextType, j_str);
          if(OB_FAIL(string_string(ObLongTextType, params, tmp_in, tmp_out, cast_mode))) {
          } else {
            if (accuracy_max_len && accuracy_max_len < tmp_out.get_string().length()) {
              res_str.assign_ptr(tmp_out.get_string().ptr(), accuracy_max_len);
            } else {
              res_str = tmp_out.get_string();
            }
          }
        }
      }
    }
  }
  return ret;
}

template <typename Value>
static int json_text(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObString res_str;
  if(OB_FAIL(common_json_string(expect_type, params, cast_mode, in, res_str))) {
  } else {
    out.set_string(expect_type, res_str);
    if (OB_FAIL(ObTextStringResult::convert_temporary_lob(out, *params.allocator_v2_))) {
    }
  }
  return ret;
}

template <typename Value>
static int json_bit(const SqlValueType expect_type, DatumCastParams &params,
                    const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  uint64_t value;
  int32_t bit_len = 0;

  if (OB_UNLIKELY(ObJsonType != in.get_type()
      || ObBitTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObString j_bin_str = in.get_string();
    if (OB_FAIL(read_real_lob_data(params, in, j_bin_str))) {
    } else {
      ObJsonBin j_bin(j_bin_str.ptr(), j_bin_str.length(), params.allocator_v2_);
      ObIJsonBase *j_base = &j_bin;
      if (OB_FAIL(j_bin.reset_iter())) {
      } else if (CAST_FAIL(j_base->to_bit(value))) {
        ret = OB_ERR_INVALID_JSON_VALUE_FOR_CAST;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_VALUE_FOR_CAST);
      } else if (OB_FAIL(get_bit_len(value, bit_len))) {
      } else {
        SET_RES_BIT(out);
        SET_RES_ACCURACY(static_cast<ObPrecision>(bit_len), DEFAULT_SCALE_FOR_BIT, DEFAULT_LENGTH_FOR_NUMERIC);
      }
    }
  }
  UNUSED(params);
  UNUSED(cast_mode);

  return ret;
}

template <typename Value>
static int json_json(const SqlValueType expect_type, DatumCastParams &params,
                     const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObJsonType != in.get_type()) || OB_UNLIKELY(ObJsonTC != sql_type_class(expect_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expect_type));
  } else {
    ObString str = in.get_string();
    out.set_json_value(expect_type, str.ptr(), str.length());
    if (in.has_lob_header()) {
      out.set_has_lob_header();
    }
    ObLength res_length = static_cast<ObLength>(out.get_string_len());
    SET_RES_ACCURACY(DEFAULT_SCALE_FOR_TEXT, DEFAULT_PRECISION_FOR_STRING, res_length);
  }
  UNUSED(cast_mode);
  return ret;
}


////////////////////////////////////////////////////////////
// geometry -> XXX
















static int geometry_geometry_datum(const SqlValueType expect_type, DatumCastParams &params,
                                   const DatumValue &in, const ObString &instr,
                                   detail::DatumCastFrame &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObLength res_length = -1;
  if (OB_ISNULL(params.allocator_v2_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObGeoType dst_geo_type = ObGeoCastUtils::get_geo_type_from_cast_mode(cast_mode);
    const char *cast_name = ObGeometryTypeCastUtil::get_cast_name(dst_geo_type);
    if (ObGeoType::GEOMETRY == dst_geo_type) {
      out = detail::DatumCastFrame(in);
    } else {
      ObIAllocator &temp_allocator = *params.allocator_v2_;
      ObGeometry *src_tree = nullptr;
      ObGeometry *dst_tree = nullptr;
      ObSrsCacheGuard srs_guard;
      const ObSrsItem *srs = nullptr;
      ObGeometryTypeCast *geo_cast = nullptr;
      ObGeoErrLogInfo log_info;
      if (OB_FAIL(common_get_srs_item(params, srs_guard, instr, srs, cast_name))) {
      } else if (OB_FAIL(ObGeometryTypeCastUtil::get_tree(temp_allocator, instr, src_tree,
          srs, log_info, cast_name))) {
        if (OB_ERR_GIS_INVALID_DATA == ret) {
          LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, cast_name);
        } else if (OB_ERR_GEOMETRY_PARAM_LONGITUDE_OUT_OF_RANGE == ret) {
          LOG_USER_ERROR(OB_ERR_GEOMETRY_PARAM_LONGITUDE_OUT_OF_RANGE, cast_name,
              log_info.value_out_of_range_, log_info.min_long_val_, log_info.max_long_val_);
        } else if (OB_ERR_GEOMETRY_PARAM_LATITUDE_OUT_OF_RANGE == ret) {
          LOG_USER_ERROR(OB_ERR_GEOMETRY_PARAM_LATITUDE_OUT_OF_RANGE, cast_name,
              log_info.value_out_of_range_, log_info.min_lat_val_, log_info.max_lat_val_);
        }
      } else {
        const ObSrsType srs_type = src_tree->get_srid() == 0
            ? ObSrsType::PROJECTED_SRS : srs->srs_type();
        if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(temp_allocator, dst_geo_type,
            ObSrsType::GEOGRAPHIC_SRS == srs_type, false, dst_tree, src_tree->get_srid()))) {
        } else if (OB_FAIL(ObGeometryTypeCastFactory::alloc(temp_allocator,
            dst_geo_type, geo_cast))) {
        } else {
          ObGeoErrLogInfo cast_log_info;
          if (ObSrsType::PROJECTED_SRS == srs_type) {
            if (OB_FAIL(geo_cast->cast_geom(*src_tree, *dst_tree, srs,
                cast_log_info, &temp_allocator))) {
              ObGeoCastUtils::geo_cast_error_handle(ret, src_tree->type(),
                  dst_geo_type, cast_log_info);
            }
          } else if (ObSrsType::GEOGRAPHIC_SRS == srs_type) {
            if (OB_FAIL(geo_cast->cast_geog(*src_tree, *dst_tree, srs,
                cast_log_info, &temp_allocator))) {
              ObGeoCastUtils::geo_cast_error_handle(ret, src_tree->type(),
                  dst_geo_type, cast_log_info);
            }
          } else {
            ret = OB_ERR_UNEXPECTED;
          }
        }
        if (OB_SUCC(ret)) {
          ObString result_wkb;
          if (OB_FAIL(ObGeoTypeUtil::to_wkb(temp_allocator, *dst_tree, srs, result_wkb))) {
          } else {
            common::ObTextStringDatumValueResult text_result(
                ObGeometryType, &params, &out, true);
            if (OB_FAIL(text_result.init(result_wkb.length(), params.allocator_v2_))) {
            } else if (OB_FAIL(text_result.append(result_wkb))) {
            } else {
              text_result.set_result();
              out.meta_.set_geometry();
              out.meta_.set_has_lob_header();
              res_length = static_cast<ObLength>(out.get_string().length());
            }
          }
        }
      }
    }
  }
  SET_RES_ACCURACY_STRING(expect_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  return ret;
}

////////////////////////////////////////////////////////////
// XXX -> UDT







////////////////////////////////////////////////////////////
// UDT -> XXX

// ================
// decimalint -> xxx
//
// decimalint -> double
template <typename Value>
static int decimalint_double(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type())
      || OB_UNLIKELY(ObDoubleTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expected_type));
  } else {
    char buffer[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t pos = 0;
    if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buffer,
                                sizeof(buffer), pos))) {
    } else {
      Value from;
      from.set_varchar(buffer, static_cast<ObString::obstr_size_t>(pos));
      ret = string_double(expected_type, params, from, out, cast_mode);
    }
  }
  if (OB_SUCC(ret)) {
    SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  }
  return ret;
}

template <typename Value>
static int decimalint_float(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                            Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type())
      || OB_UNLIKELY(ObFloatTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expected_type));
  } else {
    char buffer[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t pos = 0;
    if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buffer,
                                sizeof(buffer), pos))) {
    } else {
      Value from;
      from.set_varchar(buffer, static_cast<ObString::obstr_size_t>(pos));
      ret = string_float(expected_type, params, from, out, cast_mode);
    }
  }
  if (OB_SUCC(ret)) {
    SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, SCALE_UNKNOWN_YET, LENGTH_UNKNOWN_YET);
  }
  return ret;
}

template <typename Value>
static int decimalint_number(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()) || OB_UNLIKELY(ObNumberTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expected_type));
  } else {
    number::ObNumber value;
    if (OB_FAIL(wide::to_number(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), params,
                                value))) {
    } else if (ObUNumberType == expected_type && CAST_FAIL(numeric_negative_check(value))) {
    } else {
      out.set_number(expected_type, value);
    }
  }
  if (OB_SUCC(ret)) {
    // TODO: get precision from wide integer
    SET_RES_ACCURACY(PRECISION_UNKNOWN_YET, in.get_scale(), LENGTH_UNKNOWN_YET);
  }
  return ret;
}

template <typename Value>
static int decimalint_int(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                          Value &out, ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()) || OB_UNLIKELY(ObIntTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expected_type));
  } else {
    char buffer[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t pos = 0;
    if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buffer,
                                sizeof(buffer), pos))) {
    } else {
      Value from;
      from.set_varchar(buffer, static_cast<ObString::obstr_size_t>(pos));
      ret = string_int(expected_type, params, from, out, cast_mode);
    }
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(out.get_int());
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int decimalint_uint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()) || OB_UNLIKELY(ObUIntTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expected_type));
  } else {
    char buffer[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t pos = 0;
    if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buffer,
                                sizeof(buffer), pos))) {
    } else {
      Value from;
      from.set_varchar(buffer, static_cast<ObString::obstr_size_t>(pos));
      ret = string_uint(expected_type, params, from, out, cast_mode);
    }
  }
  if (OB_SUCC(ret)) {
    res_precision = get_precision_for_integer(out.get_uint64());
  }
  SET_RES_ACCURACY(res_precision, DEFAULT_SCALE_FOR_INTEGER, DEFAULT_LENGTH_FOR_NUMERIC);
  return ret;
}

template <typename Value>
static int decimalint_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                                 const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  int64_t pos = 0;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()) || OB_UNLIKELY(expected_type != ObDecimalIntType)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (NEED_SCALE_DECIMAL_INT(in, in.get_scale(), *params.res_accuracy_)) {
    DO_SCALE_DECIMAL_INT(in, in.get_scale(), out);
  } else if (OB_ISNULL(buf = (char *)params.alloc(in.get_deep_copy_size()))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ret = out.deep_copy(in, buf, in.get_deep_copy_size(), pos);
  }
  return ret;
}

template <typename Value>
static int decimalint_string(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t length = 0;
  ObLength res_length = -1;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || (ObStringTC != sql_type_class(expected_type)
                      && ObTextTC != sql_type_class(expected_type)))) {
    ret = OB_ERR_UNDEFINED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else {
    bool need_to_sci = false;
    if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buf,
                                sizeof(buf), length, need_to_sci))) {
    }
  }
  if (OB_SUCC(ret)) {
    ObString tmp_str;
    Value tmp_out;
    if (OB_FAIL(convert_string_collation(ObString(length, buf), ObCharset::get_system_collation(),
                                         tmp_str, params.dest_collation_, params))) {
    } else if (OB_FAIL(check_convert_string(expected_type, params, tmp_str, tmp_out))) {
    } else if (OB_FAIL(copy_string(params, expected_type, tmp_out.get_string(), out))) {
    } else {
      res_length = static_cast<ObLength>(out.get_string_len());
      out.set_collation_type(params.dest_collation_);
    }
  }
  SET_RES_ACCURACY_STRING(expected_type, DEFAULT_PRECISION_FOR_STRING, res_length);
  return ret;
}

template <typename Value>
static int decimalint_bit(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                          Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value tmp_val;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObBitTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(decimalint_uint(ObUInt64Type, params, in, tmp_val, cast_mode))) {
  } else if (OB_FAIL(uint_bit(expected_type, params, tmp_val, out, cast_mode))) {
  }
  return ret;
}

template <typename Value>
static int decimalint_datetime(const SqlValueType expected_type, DatumCastParams &params,
                               const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObNumber tmp_nmb;
  Value tmp_obj;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObDateTimeTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(wide::to_number(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(),
                                     params, tmp_nmb))) {
  } else {
    tmp_obj.set_number(tmp_nmb);
    ret = number_datetime(expected_type, params, tmp_obj, out, cast_mode);
  }
  return ret;
}

template <typename Value>
static int decimalint_mdatetime(const SqlValueType expected_type, DatumCastParams &params,
                               const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObNumber tmp_nmb;
  Value tmp_obj;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObMySQLDateTimeTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(wide::to_number(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(),
                                     params, tmp_nmb))) {
  } else {
    tmp_obj.set_number(tmp_nmb);
    ret = number_mdatetime(expected_type, params, tmp_obj, out, cast_mode);
  }
  return ret;
}

template <typename Value>
static int decimalint_date(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObNumber tmp_nmb;
  Value tmp_obj;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObDateTC != sql_type_class(expected_type))) {
    ret = OB_ERROR;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(wide::to_number(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(),
                                     params, tmp_nmb))) {
  } else {
    tmp_obj.set_number(tmp_nmb);
    ret = number_date(expected_type, params, tmp_obj, out, cast_mode);
  }
  return ret;
}

template <typename Value>
static int decimalint_mdate(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObNumber tmp_nmb;
  Value tmp_obj;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObMySQLDateTC != sql_type_class(expected_type))) {
    ret = OB_ERROR;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(wide::to_number(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(),
                                     params, tmp_nmb))) {
  } else {
    tmp_obj.set_number(tmp_nmb);
    ret = number_mdate(expected_type, params, tmp_obj, out, cast_mode);
  }
  return ret;
}

template <typename Value>
static int decimalint_time(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t length = 0;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObTimeTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buf,
                                     sizeof(buf), length))) {
  } else {
    Value from;
    from.set_varchar(buf, static_cast<ObString::obstr_size_t>(length));
    ret = string_time(expected_type, params, from, out, cast_mode);
  }
  return ret;
}

template <typename Value>
static int decimalint_year(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t length = 0;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObYearTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (wide::is_negative(in.get_decimal_int(), in.get_int_bytes())) {
    uint8_t value = 0;
    if (CAST_FAIL(ObTimeConverter::int_to_year(INT_MIN, value))) {
    } else {
      SET_RES_YEAR(out);
    }
  } else if (OB_FAIL(wide::to_string(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(), buf,
                                     sizeof(buf), length))) {
  } else {
    Value from;
    from.set_varchar(buf, static_cast<ObString::obstr_size_t>(length));
    ret = string_year(expected_type, params, from, out, cast_mode);
  }
  SET_RES_ACCURACY(DEFAULT_PRECISION_FOR_TEMPORAL, DEFAULT_SCALE_FOR_YEAR,
                   DEFAULT_LENGTH_FOR_TEMPORAL);
  return ret;
}

template <typename Value>
static int decimalint_json(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObNumber tmp_nmb;
  Value tmp_obj;
  if (OB_UNLIKELY(ObDecimalIntType != in.get_type()
                  || ObJsonTC != sql_type_class(expected_type))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (CM_IS_COLUMN_CONVERT(cast_mode)
             && is_mysql_unsupported_json_column_conversion(in.get_type())) {
    ret = OB_ERR_INVALID_JSON_TEXT;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT);
  } else if (OB_FAIL(wide::to_number(in.get_decimal_int(), in.get_int_bytes(), in.get_scale(),
                                     params, tmp_nmb))) {
  } else {
    tmp_obj.set_number(tmp_nmb);
    ret = number_json(expected_type, params, tmp_obj, out, cast_mode);
  }
  return ret;
}


template <typename Value>
static int int_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                          Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  int64_t value = in.get_int();
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObPrecision in_prec = ob_fast_digits10(value < 0 ? -value : value);
  if (OB_UNLIKELY(ObIntTC != sql_type_class(in.get_type())
                  || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(wide::from_integer(value, params, decint, int_bytes, in_prec))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
    } else {
      out.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    }
  }
  return ret;
}

template <typename Value>
static int uint_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObPrecision res_precision = -1;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  uint64_t value = in.get_uint64();
  ObPrecision in_prec = ob_fast_digits10(value);
  if (OB_UNLIKELY(ObUIntTC != sql_type_class(in.get_type())
                  || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(wide::from_integer(value, params, decint, int_bytes, in_prec))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
    } else {
      out = tmp_val;
    }
  }
  return ret;
}

template <typename Value>
static int float_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                            Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[MAX_DOUBLE_STRICT_PRINT_SIZE] = {0};
  int64_t length = 0;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObScale scale = 0;
  ObPrecision precision = 0;
  if (OB_UNLIKELY(ObFloatTC != sql_type_class(in.get_type())
                  || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    length = ob_gcvt(in.get_float(), OB_GCVT_ARG_DOUBLE, sizeof(buf) - 1, buf, NULL);
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(params.allocator_v2_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(wide::from_string(buf, length, *params.allocator_v2_, scale, precision,
                                         int_bytes, decint))) {
    } else {
      Value tmp_val;
      tmp_val.set_decimal_int(int_bytes, scale, decint);
      if (NEED_SCALE_DECIMAL_INT(tmp_val, scale, *params.res_accuracy_)) {
        DO_SCALE_DECIMAL_INT(tmp_val, scale, out);
      } else {
        out = tmp_val;
      }
    }
  }
  return ret;
}

template <typename Value>
static int double_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[MAX_DOUBLE_STRICT_PRINT_SIZE] = {0};
  int64_t length = 0;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObScale scale = 0;
  ObPrecision precision = 0;
  if (OB_UNLIKELY(ObDoubleTC != sql_type_class(in.get_type())
                  || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(params.allocator_v2_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    length = ob_gcvt_opt(in.get_double(), OB_GCVT_ARG_DOUBLE, sizeof(buf) - 1, buf, NULL,
                         TRUE);
    if (OB_FAIL(wide::from_string(buf, length, *params.allocator_v2_, scale, precision, int_bytes,
                                  decint))) {
    } else {
      Value tmp_val;
      tmp_val.set_decimal_int(int_bytes, scale, decint);
      if (NEED_SCALE_DECIMAL_INT(tmp_val, scale, *params.res_accuracy_)) {
        DO_SCALE_DECIMAL_INT(tmp_val, scale, out);
      } else {
        out = tmp_val;
      }
    }
  }
  return ret;
}

template <typename Value>
static int number_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  if (OB_UNLIKELY(ObNumberTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // A previous JSON cast may have returned a NULL warning result. Validate
    // its type before dereferencing the native NUMBER descriptor.
    ObNumber nmb = in.get_number();
    ObScale in_scale = nmb.get_scale();
    if (OB_FAIL(wide::from_number(nmb, params, in_scale, decint, int_bytes))) {
    } else {
      Value tmp_val;
      tmp_val.set_decimal_int(int_bytes, in_scale, decint);
      if (NEED_SCALE_DECIMAL_INT(tmp_val, in_scale, *params.res_accuracy_)) {
        DO_SCALE_DECIMAL_INT(tmp_val, in_scale, out);
      } else {
        out = tmp_val;
      }
    }
  }
  return ret;
}

template <typename Value>
static int datetime_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                               const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObScale scale = 0;
  ObPrecision precision = 0;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  if (OB_UNLIKELY(ObDateTimeTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.allocator_v2_) || OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObTimeZoneInfo *tz_info =
      (ObTimestampType == in.get_type()) ? params.dtc_params_.tz_info_ : NULL;
    char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t len = 0;
    if (OB_FAIL(ObTimeConverter::datetime_to_str(in.get_datetime(), tz_info,
                                                 in.get_scale(), buf, sizeof(buf), len, false))) {
    } else if (CAST_FAIL(wide::from_string(buf, len, *params.allocator_v2_, scale, precision,
                                           int_bytes, decint))) {
    } else {
      Value tmp_val;
      tmp_val.set_decimal_int(int_bytes, scale, decint);
      if (NEED_SCALE_DECIMAL_INT(tmp_val, scale, *params.res_accuracy_)) {
        DO_SCALE_DECIMAL_INT(tmp_val, scale, out);
      } else {
        out = tmp_val;
      }
    }
  }
  return ret;
}

template <typename Value>
static int mdatetime_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                               const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObScale scale = 0;
  ObPrecision precision = 0;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  if (OB_UNLIKELY(ObMySQLDateTimeTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.allocator_v2_) || OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
    int64_t len = 0;
    if (OB_FAIL(ObTimeConverter::mdatetime_to_str(in.get_datetime(), NULL,
                                                 in.get_scale(), buf, sizeof(buf), len, false))) {
    } else if (CAST_FAIL(wide::from_string(buf, len, *params.allocator_v2_, scale, precision,
                                           int_bytes, decint))) {
    } else {
      Value tmp_val;
      tmp_val.set_decimal_int(int_bytes, scale, decint);
      if (NEED_SCALE_DECIMAL_INT(tmp_val, scale, *params.res_accuracy_)) {
        DO_SCALE_DECIMAL_INT(tmp_val, scale, out);
      } else {
        out = tmp_val;
      }
    }
  }
  return ret;
}

template <typename Value>
static int date_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_int;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObPrecision res_precision = -1;
  ObAccuracy *acc = params.res_accuracy_;
  if (OB_UNLIKELY(ObDateTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(params.res_accuracy_ = NULL)) { // set accuracy to null to avoid overwrite
  } else if (OB_FAIL(date_int(ObIntType, params, in, obj_int, cast_mode))) {
  } else if (FALSE_IT(params.res_accuracy_ = acc)) { // reset accuracy for decimal int dest cast
  } else if (OB_FAIL(wide::from_integer(obj_int.get_int(), params, decint, int_bytes))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
    } else {
      out = tmp_val;
    }
  }
  return ret;
}

template <typename Value>
static int mdate_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_int;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObPrecision res_precision = -1;
  ObAccuracy *acc = params.res_accuracy_;
  if (OB_UNLIKELY(ObMySQLDateTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(params.res_accuracy_ = NULL)) { // set accuracy to null to avoid overwrite
  } else if (OB_FAIL(mdate_int(ObIntType, params, in, obj_int, cast_mode))) {
  } else if (FALSE_IT(params.res_accuracy_ = acc)) { // reset accuracy for decimal int dest cast
  } else if (OB_FAIL(wide::from_integer(obj_int.get_int(), params, decint, int_bytes))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
    } else {
      out = tmp_val;
    }
  }
  return ret;
}

template <typename Value>
static int time_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  char buf[OB_CAST_TO_VARCHAR_MAX_LENGTH] = {0};
  int64_t len = 0;
  ObPrecision res_precision = -1;
  ObScale res_scale = -1;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  if (OB_UNLIKELY(ObTimeTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input type", K(ret), K(in), K(expected_type));
  } else if (OB_ISNULL(params.allocator_v2_) || OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObTimeConverter::time_to_str(in.get_time(), in.get_scale(), buf, sizeof(buf),
                                                  len, false))) {
  } else if (OB_FAIL(wide::from_string(buf, len, *params.allocator_v2_, res_scale, res_precision,
                                       int_bytes, decint))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, res_scale, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, res_scale, *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, res_scale, out);
    } else {
      out = tmp_val;
    }
  }
  return ret;
}

template <typename Value>
static int year_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_int;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObPrecision res_precision = -1;
  if (OB_UNLIKELY(ObYearTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(year_int(ObIntType, params, in, obj_int, cast_mode))) {
  } else if (OB_FAIL(wide::from_integer(obj_int.get_int(), params, decint, int_bytes))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
    } else {
      out = tmp_val;
    }
  }
  return ret;
}

template <typename Value>
static int string_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                             const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  ObScale in_scale = NUMBER_SCALE_UNKNOWN_YET;
  ObPrecision in_precision = PRECISION_UNKNOWN_YET;
  ObString utf8_string;
  ObPrecision out_prec = params.res_accuracy_->get_precision();
  ObScale out_scale = params.res_accuracy_->get_scale();
  Value tmp_val;
  if (OB_UNLIKELY((ObStringTC != in.get_type_class() && ObTextTC != in.get_type_class()
                   && ObGeometryTC != in.get_type_class())
                  || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.allocator_v2_) || OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (ObHexStringType == in.get_type()) {
    uint64_t hex_v = hex_to_uint64(in.get_string());
    if (OB_FAIL(wide::from_integer(hex_v, params, decint, int_bytes))) {
    } else {
      tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
      in_precision = ob_fast_digits10(hex_v);
      in_scale = 0;
    }
  } else if (OB_UNLIKELY(0 == in.get_string().length())) {
    ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
    decint = nullptr;
    int_bytes = 0;
  } else if (OB_FAIL(convert_string_collation(in.get_string(), in.get_collation_type(), utf8_string,
                                              ObCharset::get_system_collation(), params))) {
  } else if (OB_FAIL(wide::from_string(utf8_string.ptr(), utf8_string.length(),
                                       *params.allocator_v2_, in_scale, in_precision, int_bytes,
                                       decint))) {
    if (OB_NUMERIC_OVERFLOW == ret) {
      // bug: 4263211. compatible with mysql behavior when value overflows type range.
      // select cast('1e500' as decimal);  -> max_val
      // select cast('-1e500' as decimal); -> min_val
      int64_t i = 0;
      while (i < utf8_string.length() && isspace(utf8_string[i])) { ++i; }
      bool is_neg = (utf8_string[i] == '-');
      const ObDecimalInt *limit_decint = nullptr;
      if (is_neg) {
        limit_decint = wide::ObDecimalIntConstValue::get_min_value(out_prec);
        int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
      } else {
        limit_decint = wide::ObDecimalIntConstValue::get_max_value(out_prec);
        int_bytes = wide::ObDecimalIntConstValue::get_int_bytes_by_precision(out_prec);
      }
      in_scale = out_scale;
      in_precision = out_prec;
      if (OB_ISNULL(limit_decint)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ISNULL(decint = (ObDecimalInt *)params.alloc(int_bytes))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        MEMCPY(decint, limit_decint, int_bytes);
      }
    } else if (OB_INVALID_NUMERIC == ret && OB_NOT_NULL(params.runtime_)) {
      if (CM_IS_COLUMN_CONVERT(params.cast_mode_)) {
        ObString decimal_type_str("decimal");
        params.runtime_->report_warning(
            ret, decimal_type_str, in.get_string(), params.cast_mode_);
      } else {
        ret = OB_ERR_TRUNCATED_WRONG_VALUE;
        ObString decimal_type_str("DECIMAL");
        params.runtime_->report_warning(
            ret, decimal_type_str, in.get_string(), params.cast_mode_);
      }
    }
  }
  int warning = ret;
  ret = OB_SUCCESS;
  if (decint != nullptr && int_bytes != 0) {
    tmp_val.set_decimal_int(int_bytes, in_scale, decint);
    // Decimal int not null means a valid decimal int was parsed regardless of wether there's
    // error or not.We then do scale and calculate res_datum as normal in order to be compatible
    // with mysql.
    // e.g.
    //  OceanBase(root@test)>set sql_mode = '';
    //  Query OK, 0 rows affected (0.00 sec)
    //
    //  OceanBase(root@test)>insert into t2 values ('1ab');
    //  Query OK, 1 row affected (0.00 sec)
    //
    //  OceanBase(root@test)>select * from t2;
    //  +-------+
    //  | a     |
    //  +-------+
    //  | 1.000 |
    //  +-------+
    //  1 row in set (0.01 sec)
    if (NEED_SCALE_DECIMAL_INT(tmp_val, in_scale, *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, in_scale, out);
    } else {
      out = tmp_val;
    }
  } else {
    // set to default zero
    const ObDecimalInt *tmp_decint = nullptr;
    if (OB_FAIL(wide::ObDecimalIntConstValue::get_zero_value_byte_precision(out_prec, tmp_decint, int_bytes))) {
    } else if (OB_ISNULL(decint = (ObDecimalInt *)params.alloc(int_bytes))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      MEMCPY(decint, tmp_decint, int_bytes);
      out.set_decimal_int(int_bytes, out_scale, decint);
    }
  }
  if (OB_SUCC(ret)) {
    if (CAST_FAIL(warning)) {
    }
  }
  return ret;
}

template <typename Value>
static int bit_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                          Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  if (OB_UNLIKELY(ObBitTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    uint64_t val = in.get_bit();
    if (OB_FAIL(wide::from_integer(val, params, decint, int_bytes))) {
    } else {
      Value tmp_val;
      tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
      if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
        DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
      } else {
        out = tmp_val;
      }
      out.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    }
  }
  return ret;
}

template <typename Value>
static int enumset_decimalint(const SqlValueType expected_type, DatumCastParams &params,
                              const Value &in, Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  ObDecimalInt *decint = nullptr;
  int32_t int_bytes = 0;
  if (OB_UNLIKELY(ObEnumSetTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_ISNULL(params.res_accuracy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(wide::from_integer(in.get_uint64(), params, decint, int_bytes))) {
  } else {
    Value tmp_val;
    tmp_val.set_decimal_int(int_bytes, DEFAULT_SCALE_FOR_INTEGER, decint);
    if (NEED_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), *params.res_accuracy_)) {
      DO_SCALE_DECIMAL_INT(tmp_val, tmp_val.get_scale(), out);
    } else {
      out = tmp_val;
    }
  }
  return ret;
}


template <typename Value>
static int json_decimalint(const SqlValueType expected_type, DatumCastParams &params, const Value &in,
                           Value &out, const ObCastMode cast_mode)
{
  int ret = OB_SUCCESS;
  Value obj_nmb;
  if (OB_UNLIKELY(ObJsonTC != in.get_type_class() || ObDecimalIntType != expected_type)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid types", K(ret), K(in.get_type()), K(expected_type));
  } else if (OB_FAIL(json_number(ObNumberType, params, in, obj_nmb, cast_mode))) {
  } else if (OB_FAIL(number_decimalint(expected_type, params, obj_nmb, out, cast_mode))) {
  }
  return ret;
}




////////////////////////////////////////////////////////////////
int get_bit_len(const ObString &str, int32_t &bit_len)
{
  int ret = OB_SUCCESS;
  if (str.empty()) {
    bit_len = 1;
  } else {
    const char *ptr = str.ptr();
    uint32_t uneven_value = reinterpret_cast<const unsigned char&>(ptr[0]);
    int32_t len = str.length();
    if (0 == uneven_value) {
      if (len > 8) {
        // Compatible with MySQL, if the length of bit string greater than 8 Bytes,
        // it would be considered too long. We set bit_len to OB_MAX_BIT_LENGTH + 1.
        bit_len = OB_MAX_BIT_LENGTH + 1;
      } else {
        bit_len = 1;
      }
    } else {
      //Built-in Function: int __builtin_clz (unsigned int x).
      //Returns the number of leading 0-bits in x, starting at the most significant bit position.
      //If x is 0, the result is undefined.
      int32_t uneven_len = static_cast<int32_t>(
          sizeof(unsigned int) * 8 - __builtin_clz(uneven_value));
      bit_len = uneven_len + 8 * (len - 1);
    }
  }
  return ret;
}

int get_bit_len(uint64_t value, int32_t &bit_len)
{
  int ret = OB_SUCCESS;
  if (0 == value) {
    bit_len = 1;
  } else {
    bit_len = static_cast<int32_t>(sizeof(unsigned long long) * 8 - __builtin_clzll(value));
  }
  return ret;
}

int ObTypeCastRules::is_cast_monotonic(SqlValueType t1, SqlValueType t2, bool &is_monotonic)
{
  int ret = OB_SUCCESS;
  SqlTypeClass tc1 = sql_type_class(t1);
  SqlTypeClass tc2 = sql_type_class(t2);
  if (OB_UNLIKELY(is_invalid_sql_type_class(tc1) || is_invalid_sql_type_class(tc2))) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    is_monotonic = CAST_MONOTONIC[tc1][tc2];
  }
  return ret;
}

int ObTypeCastRules::is_order_consistent(
    const SqlTypeMeta &from,
    const SqlTypeMeta &to,
    bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  const SqlTypeClass from_class = from.get_type_class();
  const SqlTypeClass to_class = to.get_type_class();
  if (OB_UNLIKELY(is_invalid_sql_type_class(from_class)
                  || is_invalid_sql_type_class(to_class))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (from.is_string_or_lob_locator_type()
             && to.is_string_or_lob_locator_type()) {
    ObCollationType result_type = CS_TYPE_INVALID;
    ObCollationLevel result_level = CS_LEVEL_INVALID;
    const ObCollationType from_type = from.get_collation_type();
    const ObCollationType to_type = to.get_collation_type();
    const uint32_t flags =
        OB_COLL_ALLOW_SUPERSET_CONV
        | OB_COLL_ALLOW_COERCIBLE_CONV
        | OB_COLL_ALLOW_NUMERIC_CONV;
    if (OB_FAIL(ObCharset::aggregate_collation_new(
            from.get_collation_level(),
            from_type,
            to.get_collation_level(),
            to_type,
            result_level,
            result_type,
            flags))) {
      if (OB_CANT_AGGREGATE_2COLLATIONS == ret) {
        LOG_USER_ERROR(
            OB_CANT_AGGREGATE_2COLLATIONS,
            ObCharset::collation_name(from_type),
            ObCharset::collation_level(from.get_collation_level()),
            ObCharset::collation_name(to_type),
            ObCharset::collation_level(to.get_collation_level()));
      }
    } else {
      const int64_t from_index = get_idx_of_collate(from_type);
      const int64_t to_index = get_idx_of_collate(to_type);
      const int64_t result_index = get_idx_of_collate(result_type);
      if (from_index >= 0 && from_index < VALID_OC_COLLATION_TYPES
          && to_index >= 0 && to_index < VALID_OC_COLLATION_TYPES
          && result_index >= 0 && result_index < VALID_OC_COLLATION_TYPES) {
        result = ORDER_CONSISTENT_WITH_BOTH_STRING
            [from_index][to_index][result_index];
      } else {
        result = from_type == to_type && from_type == result_type;
      }
    }
  } else {
    result = ORDER_CONSISTENT[from_class][to_class];
  }
  return ret;
}

/*
 *  c1 = 'A' check if c1 can be ordering
 */
int ObTypeCastRules::is_const_consistent(const SqlTypeMeta &const_mt,
                                     const SqlTypeMeta &column_mt,
                                     const SqlValueType calc_type,
                                     const ObCollationType calc_collation,
                                     bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  SqlTypeClass tc1 = const_mt.get_type_class();
  SqlTypeClass tc2 = column_mt.get_type_class();
  if (OB_UNLIKELY(is_invalid_sql_type_class(tc1) || is_invalid_sql_type_class(tc2))) {
    ret = OB_ERR_UNEXPECTED;
  } else if (const_mt.is_string_or_lob_locator_type()
              && column_mt.is_string_or_lob_locator_type()) {
    if (column_mt.get_collation_type() == calc_collation) {
      result = true;
    } else {
      // false
    }
  } else {
    if (OB_FAIL(ObTypeCastRules::is_cast_monotonic(column_mt.get_type(),
                                               calc_type,
                                               result))) {
    } else if (!result) {
      // false
    } else if (OB_FAIL(ObTypeCastRules::is_cast_monotonic(calc_type,
                                                      column_mt.get_type(),
                                                      result))) {
    } else {
      // do nothing
    }
  }
  return ret;
}

const bool ObTypeCastRules::CAST_MONOTONIC[ObMaxTC][ObMaxTC] =
{
  // null
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetinner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // int
  {
    false,  // null
    true,   // int
    true,   // uint
    false,   // float
    false,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // uint
  {
    false,  // null
    true,   // int
    true,   // uint
    false,   // float
    false,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // float
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // enumset
    false,  // enumsetInner
    true,   // bit
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // double
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // number
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  //lob
    false,  // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // datetime
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    true,   // date
    false,  // time
    true,   // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   //lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // date
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    true,   // date
    false,  // time
    true,   // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   // lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // time
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    false,  // date
    true,   // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   //lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    true,   // mysql datetime
  },
  // year
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    true,   // date
    false,  // time
    true ,  // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   // lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // string
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    true,   // raw
    true,  // interval
    true,   // rowid
    true,   // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // extend
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // unknown
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // text
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
   // bit
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  //lob
    false,  // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  //enumset
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  //enumsetinner
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // text
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // OTimestamp
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    true,   // date
    false,  // time
    true,   // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  //rowid
    true,   // lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // raw
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    true,   // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    true,   // raw
    false,  // interval,
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // interval
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,   // raw
    true,  // interval,
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // rowid
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    true,   // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,   // raw
    false,  // interval,
    true,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // lob
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // json
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    true,   // json
    false,  // geometry
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // geometry
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // udt
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // gis
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // decimal int
  {
    false,  // null
    true,  // int
    true,  // uint
    true,  // float
    true,  // double
    true,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    true,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    true,   // json
    false,  // gis
    false,  // udt
    true,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // collation
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // text
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    true,   // json
    false,  // gis
    false,  // udt
    true,  // decimal int
    true,   // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // mysql date
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    true,   // date
    false,  // time
    true,   // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   // lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // mysql datetime
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    true,   // datetime
    true,   // date
    false,  // time
    true,   // year
    true,   // string
    false,  // extend
    false,  // unknown
    true,   // text
    true,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,   // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    true,   //lob
    true,   // json
    false,  // gis
    false,  // udt
    true,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
};

const bool ObTypeCastRules::ORDER_CONSISTENT[ObMaxTC][ObMaxTC] =
{
  // null
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // int
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    true,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    true,   // bit
    true,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // uint
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    true,   // bit
    true,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // float
  {
    false,  // null
    false,   // int
    false,   // uint
    true,   // float
    true,   // double
    false,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // double
  {
    false,  // null
    false,   // int
    false,   // uint
    true,   // float
    true,   // double
    false,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // number
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    true,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    true,   // bit
    true,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // datetime
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    true,   // datetime
    true,   // date
    true,  // time
    true,   // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // date
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    true,   // datetime
    true,   // date
    true,  // time
    true,   // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // time
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    true,   // datetime
    true,  // date
    true,   // time
    true,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // year //0000-9999
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,   // datetime
    false,   // date
    false,  // time
    true ,  // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    true,   // bit
    true,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // string
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // extend
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // unknown
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // lob
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
    // bit
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    true,   // bit
    true,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  //enumset
  {
    false,  // null
    true,   // int
    true,   // uint
    true,   // float
    true,   // double
    true,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    true,   // bit
    true,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  //enumsetInner
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // OTimestamp
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    true,   // datetime
    true,   // date
    true,  // time
    true,   // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // raw
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // interval
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // rowid
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // lob
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // json
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // geometry
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,   // udt
    false,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // udt
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // decimal int
  {
    false,  // null
    true,  // int
    true,  // uint
    true,  // float
    true,  // double
    true,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    true,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    true,   // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // collation
  {
    false,  // null
    false,  // int
    false,  // uint
    false,  // float
    false,  // double
    false,  // number
    false,  // datetime
    false,  // date
    false,  // time
    false,  // year
    false,  // string
    false,  // extend
    false,  // unknown
    false,  // lob
    false,  // bit
    false,  // enumset
    false,  // enumsetInner
    false,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,  // decimal int
    false,  // collation
    false,  // mysql date
    false,  // mysql datetime
  },
  // mysql date
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    true,   // datetime
    true,   // date
    true,  // time
    true,   // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
  // mysql datetime
  {
    false,  // null
    false,   // int
    false,   // uint
    false,   // float
    false,   // double
    false,   // number
    true,   // datetime
    true,   // date
    true,  // time
    true,   // year
    false,   // string
    false,  // extend
    false,  // unknown
    false,   // lob
    false,   // bit
    false,  // enumset
    false,  // enumsetInner
    true,  // OTimestamp
    false,  // raw
    false,  // interval
    false,  // rowid
    false,  // lob
    false,  // json
    false,  // geometry
    false,  // udt
    false,   // decimal int
    false,  // collation
    true,   // mysql date
    true,   // mysql datetime
  },
};

const bool ObTypeCastRules::ORDER_CONSISTENT_WITH_BOTH_STRING[ObTypeCastRules::VALID_OC_COLLATION_TYPES][ObTypeCastRules::VALID_OC_COLLATION_TYPES][ObTypeCastRules::VALID_OC_COLLATION_TYPES] =
{
  //CS_TYPE_UTF8MB4_GENERAL_CI
  {
    //ci    //utf8bin //bin
    {true, true, true},
    {false, false, false},
    {false, false, false},
  },
  //CS_TYPE_UTF8MB4_BIN
  {
    //ci    //utf8bin //bin
    {true, true , true},
    {false, true , true},
    {false, true , true},
  },
  //CS_TYPE_BINARY
  {
    //ci    //utf8bin //bin
    {true, true , true},
    {false, true , true},
    {false, true , true},
  },
};

int DatumValueCaster::to_type(SqlValueType target, DatumCastParams &context,
    const DatumValue &input, ObIAllocator &result_owner, DatumValue &output)
{
  int ret = OB_SUCCESS;
  if (is_invalid_sql_value_type(target) || input.is_invalid_type()) { return OB_ERR_UNEXPECTED; }
  const ObAccuracy original_accuracy = context.res_accuracy_ != nullptr
      ? *context.res_accuracy_ : ObAccuracy();
  detail::DatumCastFrame source(input), result;
  // A zero-width DECIMAL has no coefficient. The wire decoder may reserve
  // one backing byte even for an empty Datum; that byte is not a coefficient
  // and must not change the scaler's missing-coefficient validation.
  if (!input.is_null() && input.is_decimal_int() && input.datum_.len_ == 0) {
    source.datum_.set_decimal_int_shallow(nullptr, 0);
  }
  if (input.is_null()) {
    result = source;
  } else if (input.get_type_class() != ObIntTC && input.get_type_class() != ObUIntTC
             && input.get_type_class() != ObFloatTC && input.get_type_class() != ObDoubleTC
             && input.get_type_class() != ObNumberTC && input.get_type_class() != ObStringTC
             && input.get_type_class() != ObDateTimeTC && input.get_type_class() != ObDateTC
             && input.get_type_class() != ObTimeTC && input.get_type_class() != ObYearTC
             && input.get_type_class() != ObBitTC && input.get_type_class() != ObDecimalIntTC
             && input.get_type_class() != ObEnumSetTC && input.get_type_class() != ObMySQLDateTC
             && input.get_type_class() != ObMySQLDateTimeTC && input.get_type_class() != ObTextTC
             && input.get_type_class() != ObJsonTC
             && input.get_type_class() != ObGeometryTC) {
    return OB_NOT_SUPPORTED;
  } else if (input.datum_.ptr_ == nullptr && input.datum_.len_ != 0) {
    return OB_INVALID_ARGUMENT;
  } else if (input.get_type_class() == ObNumberTC &&
             (input.datum_.len_ < sizeof(number::ObNumber::Desc)
              || input.datum_.len_ > number::ObNumber::MAX_BYTE_LEN
              || input.datum_.len_ != sizeof(number::ObNumber::Desc)
                  + sizeof(uint32_t) * input.datum_.get_number_desc().len_)) {
    return OB_INVALID_ARGUMENT;
  } else if (input.is_decimal_int() && input.datum_.len_ != 0 && input.datum_.len_ != 4
             && input.datum_.len_ != 8 && input.datum_.len_ != 16
             && input.datum_.len_ != 32 && input.datum_.len_ != 64) {
    return OB_INVALID_ARGUMENT;
  } else if (input.get_type_class() != ObNumberTC && input.get_type_class() != ObStringTC
             && input.get_type_class() != ObDecimalIntTC && input.get_type_class() != ObTextTC
             && input.get_type_class() != ObJsonTC && input.get_type_class() != ObGeometryTC &&
             (input.datum_.ptr_ == nullptr || input.datum_.len_ !=
              (input.get_type_class() == ObYearTC ? sizeof(uint8_t)
               : input.get_type_class() == ObDateTC || input.get_type_class() == ObMySQLDateTC
                   || input.get_type_class() == ObFloatTC
                   ? sizeof(int32_t) : sizeof(int64_t)))) {
    return OB_INVALID_ARGUMENT;
  } else if (target == input.get_type() && input.is_lob_storage()
             && context.res_accuracy_ == nullptr && context.zf_info_ == nullptr
             && context.dest_max_length_ < 0
             && context.dest_collation_ == input.get_collation_type()) {
    // Keep same-type LOB storage as a locator/value view. The common final
    // clone owns the payload while preserving the header and type metadata.
    result = source;
  } else if (target == input.get_type() && !input.is_string_type()
             && input.get_type_class() != ObGeometryTC
             && input.get_type_class() != ObDecimalIntTC) {
    result = source;
  } else if (input.is_varchar_or_char() && ob_is_string_tc(target)
             && (target == ObVarcharType || target == ObCharType)
             && input.get_collation_type() == context.dest_collation_
             && context.zf_info_ == nullptr && context.res_accuracy_ == nullptr) {
    result = source;
    result.meta_.set_type_simple(target);
  } else if (target == ObDecimalIntType && context.res_accuracy_ == nullptr) {
    return OB_ERR_UNEXPECTED;
  } else {
    context.warning_ = OB_SUCCESS;
    // Match the common-value API: string input supplies an unspecified
    // destination collation, including numeric destinations.
    if (input.is_string_type() && context.dest_collation_ == CS_TYPE_INVALID) {
      context.dest_collation_ = input.get_collation_type();
    }
    using Frame = detail::DatumCastFrame;
    using Function = int (*)(SqlValueType, DatumCastParams &, const Frame &, Frame &, ObCastMode);
    Function function = nullptr;
    if (input.get_type_class() == ObGeometryTC) {
      ObString wkb = input.get_string();
      if (target == ObVarcharType || target == ObCharType || ob_is_text_tc(target)) {
        ObLength res_length = -1;
        if (OB_FAIL(read_real_lob_data(context, input, wkb))) {
        } else if (wkb.length() <= WKB_OFFSET) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          int32_t length = wkb.length();
          int32_t offset = WKB_GEO_SRID_SIZE;
          const uint8_t version = static_cast<uint8_t>(wkb.ptr()[WKB_GEO_SRID_SIZE]);
          if (IS_GEO_VERSION(version)) {
            length -= WKB_VERSION_SIZE;
            offset += WKB_VERSION_SIZE;
          }
          if (is_lob_storage(target)) {
            const bool has_lob_header = target != ObTinyTextType;
            CastTextResult<Frame> text_result(
                target, &context, &result, has_lob_header);
            if (OB_FAIL(text_result.init(length, context.allocator_v2_))) {
            } else if (OB_FAIL(text_result.append(wkb.ptr(), WKB_GEO_SRID_SIZE))) {
            } else if (OB_FAIL(text_result.append(wkb.ptr() + offset,
                length - WKB_GEO_SRID_SIZE))) {
            } else {
              text_result.set_result();
              res_length = static_cast<ObLength>(result.get_string().length());
            }
          } else {
            char *bytes = static_cast<char *>(context.alloc(length));
            if (bytes == nullptr) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else {
              MEMCPY(bytes, wkb.ptr(), WKB_GEO_SRID_SIZE);
              MEMCPY(bytes + WKB_GEO_SRID_SIZE, wkb.ptr() + offset,
                  length - WKB_GEO_SRID_SIZE);
              result.set_string(target, bytes, length);
              res_length = length;
            }
          }
        }
        if (OB_SUCC(ret) && context.res_accuracy_ != nullptr) {
          context.res_accuracy_->set_precision(DEFAULT_PRECISION_FOR_STRING);
          context.res_accuracy_->set_scale(ob_is_text_tc(target)
              ? DEFAULT_SCALE_FOR_TEXT : DEFAULT_SCALE_FOR_STRING);
          context.res_accuracy_->set_length(res_length);
        }
      } else if (sql_type_class(target) == ObGeometryTC) {
        if (OB_FAIL(read_real_lob_data(context, input, wkb))) {
        } else {
          ret = geometry_geometry_datum(target, context, input, wkb,
              result, context.cast_mode_);
        }
      } else if (target == ObJsonType) {
        // The legacy cast deliberately rejects geometry-to-JSON.
        ret = OB_NOT_SUPPORTED;
      } else {
        Frame real_geometry;
        real_geometry.meta_.set_geometry();
        real_geometry.datum_.set_string(wkb);
        const SqlTypeClass target_class = sql_type_class(target);
        if (target_class == ObIntTC || target_class == ObUIntTC) {
          if (!CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                target_class == ObIntTC ? "cast_as_signed" : "cast_as_unsigned");
          } else if (target_class == ObIntTC) {
            function = string_int<Frame>;
          } else {
            function = string_uint<Frame>;
          }
        } else if (target_class == ObFloatTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_float<Frame>;
          } else {
            result.set_float(target, 0.0f);
          }
        } else if (target_class == ObDoubleTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_) && !CM_IS_WARN_ON_FAIL(context.cast_mode_)) {
            function = string_double<Frame>;
          } else {
            result.set_double(target, 0.0);
          }
        } else if (target_class == ObNumberTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_number<Frame>;
          } else {
            number::ObNumber zero;
            zero.set_zero();
            result.set_number(target, zero);
          }
        } else if (target_class == ObDateTimeTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_datetime<Frame>;
          } else {
            result.set_null();
          }
        } else if (target_class == ObDateTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_date<Frame>;
          } else {
            result.set_null();
          }
        } else if (target_class == ObTimeTC) {
          if (CM_IS_NULL_ON_WARN(context.cast_mode_)) {
            result.set_null();
          } else if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_time<Frame>;
          } else {
            result.set_null();
          }
        } else if (target_class == ObYearTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_year<Frame>;
          } else {
            ret = OB_INVALID_ARGUMENT;
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, "cast_as_year");
          }
        } else if (target_class == ObBitTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_bit<Frame>;
          } else {
            ret = OB_ERR_PARSE_SQL;
            const char *err_msg = "bit";
            int32_t str_len = static_cast<int32_t>(strlen(err_msg));
            int32_t line_no = 1;
            LOG_USER_ERROR(OB_ERR_PARSE_SQL, ob_errpkt_strerror(OB_ERR_PARSER_SYNTAX),
                str_len, err_msg, line_no);
          }
        } else if (target_class == ObDecimalIntTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_decimalint<Frame>;
          } else {
            result.set_decimal_int(0, 0, nullptr);
          }
        } else if (target_class == ObMySQLDateTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_mdate<Frame>;
          } else {
            result.set_null();
          }
        } else if (target_class == ObMySQLDateTimeTC) {
          if (CM_IS_IMPLICIT_CAST(context.cast_mode_)) {
            function = string_mdatetime<Frame>;
          } else {
            result.set_null();
          }
        } else {
          switch (target_class) {
            case ObEnumSetTC:
            case ObIntervalTC:
            case ObRowIDTC:
            case ObLobTC:
            case ObUserDefinedSQLTC:
              ret = OB_ERR_UNEXPECTED;
              break;
            case ObRawTC:
              ret = OB_ERR_INVALID_TYPE_FOR_OP;
              break;
            default:
              ret = OB_NOT_SUPPORTED;
              break;
          }
        }
        if (function != nullptr && OB_SUCC(ret)) {
          if (OB_FAIL(read_real_lob_data(context, input, wkb))) {
          } else {
            real_geometry.datum_.set_string(wkb);
            ret = function(target, context, real_geometry, result, context.cast_mode_);
          }
          if (ret != OB_SUCCESS && target_class == ObFloatTC) {
            ret = OB_ERR_WARN_DATA_OUT_OF_RANGE;
          } else if (ret != OB_SUCCESS && target_class == ObDoubleTC) {
            ret = OB_ERR_WARN_DATA_OUT_OF_RANGE;
          } else if (ret != OB_SUCCESS && target_class == ObNumberTC) {
            ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          } else if (ret != OB_SUCCESS && target_class == ObDecimalIntTC) {
            ret = OB_ERR_TRUNCATED_WRONG_VALUE_FOR_FIELD;
          }
        }
      }
    } else switch (input.get_type_class()) {
      case ObIntTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = int_int<Frame>; break;
          case ObUIntTC: function = int_uint<Frame>; break;
          case ObFloatTC: function = int_float<Frame>; break;
          case ObDoubleTC: function = int_double<Frame>; break;
          case ObNumberTC: function = int_number<Frame>; break;
          case ObDateTimeTC: function = int_datetime<Frame>; break;
          case ObDateTC: function = int_date<Frame>; break;
          case ObTimeTC: function = int_time<Frame>; break;
          case ObYearTC: function = int_year<Frame>; break;
          case ObStringTC: function = int_string<Frame>; break;
          case ObDecimalIntTC: function = int_decimalint<Frame>; break;
          case ObTextTC: function = int_text<Frame>; break;
          case ObJsonTC: function = int_json<Frame>; break;
          case ObMySQLDateTC: function = int_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = int_mdatetime<Frame>; break;
          case ObBitTC: function = int_bit<Frame>; break;
          default: break;
        }
        break;
      case ObUIntTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = uint_int<Frame>; break;
          case ObUIntTC: function = uint_uint<Frame>; break;
          case ObFloatTC: function = uint_float<Frame>; break;
          case ObDoubleTC: function = uint_double<Frame>; break;
          case ObNumberTC: function = uint_number<Frame>; break;
          case ObDateTimeTC: function = uint_datetime<Frame>; break;
          case ObDateTC: function = uint_date<Frame>; break;
          case ObTimeTC: function = uint_time<Frame>; break;
          case ObYearTC: function = uint_year<Frame>; break;
          case ObStringTC: function = uint_string<Frame>; break;
          case ObDecimalIntTC: function = uint_decimalint<Frame>; break;
          case ObTextTC: function = uint_text<Frame>; break;
          case ObJsonTC: function = uint_json<Frame>; break;
          case ObMySQLDateTC: function = uint_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = uint_mdatetime<Frame>; break;
          case ObBitTC: function = uint_bit<Frame>; break;
          default: break;
        }
        break;
      case ObFloatTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = float_int<Frame>; break;
          case ObUIntTC: function = float_uint<Frame>; break;
          case ObFloatTC: function = float_float<Frame>; break;
          case ObDoubleTC: function = float_double<Frame>; break;
          case ObNumberTC: function = float_number<Frame>; break;
          case ObDateTimeTC: function = float_datetime<Frame>; break;
          case ObDateTC: function = float_date<Frame>; break;
          case ObTimeTC: function = float_time<Frame>; break;
          case ObStringTC: function = float_string<Frame>; break;
          case ObDecimalIntTC: function = float_decimalint<Frame>; break;
          case ObTextTC: function = float_text<Frame>; break;
          case ObJsonTC: function = float_json<Frame>; break;
          case ObMySQLDateTC: function = float_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = float_mdatetime<Frame>; break;
          case ObBitTC: function = float_bit<Frame>; break;
          default: break;
        }
        break;
      case ObDoubleTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = double_int<Frame>; break;
          case ObUIntTC: function = double_uint<Frame>; break;
          case ObFloatTC: function = double_float<Frame>; break;
          case ObDoubleTC: function = double_double<Frame>; break;
          case ObNumberTC: function = double_number<Frame>; break;
          case ObDateTimeTC: function = double_datetime<Frame>; break;
          case ObDateTC: function = double_date<Frame>; break;
          case ObTimeTC: function = double_time<Frame>; break;
          case ObYearTC: function = double_year<Frame>; break;
          case ObStringTC: function = double_string<Frame>; break;
          case ObDecimalIntTC: function = double_decimalint<Frame>; break;
          case ObTextTC: function = double_text<Frame>; break;
          case ObJsonTC: function = double_json<Frame>; break;
          case ObMySQLDateTC: function = double_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = double_mdatetime<Frame>; break;
          case ObBitTC: function = double_bit<Frame>; break;
          default: break;
        }
        break;
      case ObNumberTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = number_int<Frame>; break;
          case ObUIntTC: function = number_uint<Frame>; break;
          case ObFloatTC: function = number_float<Frame>; break;
          case ObDoubleTC: function = number_double<Frame>; break;
          case ObNumberTC: function = number_number<Frame>; break;
          case ObDateTimeTC: function = number_datetime<Frame>; break;
          case ObDateTC: function = number_date<Frame>; break;
          case ObTimeTC: function = number_time<Frame>; break;
          case ObYearTC: function = number_year<Frame>; break;
          case ObStringTC: function = number_string<Frame>; break;
          case ObDecimalIntTC: function = number_decimalint<Frame>; break;
          case ObTextTC: function = number_text<Frame>; break;
          case ObJsonTC: function = number_json<Frame>; break;
          case ObMySQLDateTC: function = number_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = number_mdatetime<Frame>; break;
          case ObBitTC: function = number_bit<Frame>; break;
          default: break;
        }
        break;
      case ObStringTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = string_int<Frame>; break;
          case ObUIntTC: function = string_uint<Frame>; break;
          case ObFloatTC: function = string_float<Frame>; break;
          case ObDoubleTC: function = string_double<Frame>; break;
          case ObNumberTC: function = string_number<Frame>; break;
          case ObDateTimeTC: function = string_datetime<Frame>; break;
          case ObDateTC: function = string_date<Frame>; break;
          case ObTimeTC: function = string_time<Frame>; break;
          case ObYearTC: function = string_year<Frame>; break;
          case ObStringTC: function = string_string<Frame>; break;
          case ObDecimalIntTC: function = string_decimalint<Frame>; break;
          case ObTextTC: function = string_text<Frame>; break;
          case ObJsonTC: function = string_json<Frame>; break;
          case ObMySQLDateTC: function = string_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = string_mdatetime<Frame>; break;
          case ObBitTC: function = string_bit<Frame>; break;
          default: break;
        }
        break;
      case ObDateTimeTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = datetime_int<Frame>; break;
          case ObUIntTC: function = datetime_uint<Frame>; break;
          case ObFloatTC: function = datetime_float<Frame>; break;
          case ObDoubleTC: function = datetime_double<Frame>; break;
          case ObNumberTC: function = datetime_number<Frame>; break;
          case ObDateTimeTC: function = datetime_datetime<Frame>; break;
          case ObDateTC: function = datetime_date<Frame>; break;
          case ObTimeTC: function = datetime_time<Frame>; break;
          case ObYearTC: function = datetime_year<Frame>; break;
          case ObStringTC: function = datetime_string<Frame>; break;
          case ObDecimalIntTC: function = datetime_decimalint<Frame>; break;
          case ObTextTC: function = datetime_text<Frame>; break;
          case ObJsonTC: function = datetime_json<Frame>; break;
          case ObMySQLDateTC: function = datetime_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = datetime_mdatetime<Frame>; break;
          case ObBitTC: function = datetime_bit<Frame>; break;
          default: break;
        }
        break;
      case ObDateTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = date_int<Frame>; break;
          case ObUIntTC: function = date_uint<Frame>; break;
          case ObFloatTC: function = date_float<Frame>; break;
          case ObDoubleTC: function = date_double<Frame>; break;
          case ObNumberTC: function = date_number<Frame>; break;
          case ObDateTimeTC: function = date_datetime<Frame>; break;
          case ObTimeTC: function = date_time<Frame>; break;
          case ObYearTC: function = date_year<Frame>; break;
          case ObStringTC: function = date_string<Frame>; break;
          case ObDecimalIntTC: function = date_decimalint<Frame>; break;
          case ObTextTC: function = date_text<Frame>; break;
          case ObJsonTC: function = date_json<Frame>; break;
          case ObMySQLDateTC: function = date_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = date_mdatetime<Frame>; break;
          case ObBitTC: function = date_bit<Frame>; break;
          default: break;
        }
        break;
      case ObTimeTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = time_int<Frame>; break;
          case ObUIntTC: function = time_uint<Frame>; break;
          case ObFloatTC: function = time_float<Frame>; break;
          case ObDoubleTC: function = time_double<Frame>; break;
          case ObNumberTC: function = time_number<Frame>; break;
          case ObDateTimeTC: function = time_datetime<Frame>; break;
          case ObDateTC: function = time_date<Frame>; break;
          case ObYearTC: function = time_year<Frame>; break;
          case ObStringTC: function = time_string<Frame>; break;
          case ObDecimalIntTC: function = time_decimalint<Frame>; break;
          case ObTextTC: function = time_text<Frame>; break;
          case ObJsonTC: function = time_json<Frame>; break;
          case ObMySQLDateTC: function = time_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = time_mdatetime<Frame>; break;
          case ObBitTC: function = time_bit<Frame>; break;
          default: break;
        }
        break;
      case ObYearTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = year_int<Frame>; break;
          case ObUIntTC: function = year_uint<Frame>; break;
          case ObFloatTC: function = year_float<Frame>; break;
          case ObDoubleTC: function = year_double<Frame>; break;
          case ObNumberTC: function = year_number<Frame>; break;
          case ObDateTimeTC: function = year_datetime<Frame>; break;
          case ObDateTC: function = year_date<Frame>; break;
          case ObTimeTC: function = year_time<Frame>; break;
          case ObStringTC: function = year_string<Frame>; break;
          case ObDecimalIntTC: function = year_decimalint<Frame>; break;
          case ObTextTC: function = year_text<Frame>; break;
          case ObJsonTC: function = year_json<Frame>; break;
          case ObMySQLDateTC: function = year_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = year_mdatetime<Frame>; break;
          case ObBitTC: function = year_bit<Frame>; break;
          default: break;
        }
        break;
      case ObBitTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = bit_int<Frame>; break;
          case ObUIntTC: function = bit_uint<Frame>; break;
          case ObFloatTC: function = bit_float<Frame>; break;
          case ObDoubleTC: function = bit_double<Frame>; break;
          case ObNumberTC: function = bit_number<Frame>; break;
          case ObDateTimeTC: function = bit_datetime<Frame>; break;
          case ObDateTC: function = bit_date<Frame>; break;
          case ObTimeTC: function = bit_time<Frame>; break;
          case ObYearTC: function = bit_year<Frame>; break;
          case ObStringTC: function = bit_string<Frame>; break;
          case ObDecimalIntTC: function = bit_decimalint<Frame>; break;
          case ObTextTC: function = bit_text<Frame>; break;
          case ObJsonTC: function = bit_json<Frame>; break;
          case ObMySQLDateTC: function = bit_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = bit_mdatetime<Frame>; break;
          case ObBitTC: function = bit_bit<Frame>; break;
          default: break;
        }
        break;
      case ObDecimalIntTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = decimalint_int<Frame>; break;
          case ObUIntTC: function = decimalint_uint<Frame>; break;
          case ObFloatTC: function = decimalint_float<Frame>; break;
          case ObDoubleTC: function = decimalint_double<Frame>; break;
          case ObNumberTC: function = decimalint_number<Frame>; break;
          case ObDateTimeTC: function = decimalint_datetime<Frame>; break;
          case ObDateTC: function = decimalint_date<Frame>; break;
          case ObTimeTC: function = decimalint_time<Frame>; break;
          case ObYearTC: function = decimalint_year<Frame>; break;
          case ObStringTC: function = decimalint_string<Frame>; break;
          case ObDecimalIntTC: function = decimalint_decimalint<Frame>; break;
          case ObTextTC: function = decimalint_text<Frame>; break;
          case ObJsonTC: function = decimalint_json<Frame>; break;
          case ObMySQLDateTC: function = decimalint_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = decimalint_mdatetime<Frame>; break;
          case ObBitTC: function = decimalint_bit<Frame>; break;
          default: break;
        }
        break;
      case ObEnumSetTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = enumset_int<Frame>; break;
          case ObUIntTC: function = enumset_uint<Frame>; break;
          case ObFloatTC: function = enumset_float<Frame>; break;
          case ObDoubleTC: function = enumset_double<Frame>; break;
          case ObNumberTC: function = enumset_number<Frame>; break;
          case ObDateTimeTC: function = enumset_datetime<Frame>; break;
          case ObDateTC: function = enumset_date<Frame>; break;
          case ObTimeTC: function = enumset_time<Frame>; break;
          case ObYearTC: function = enumset_year<Frame>; break;
          case ObStringTC: function = enumset_string<Frame>; break;
          case ObDecimalIntTC: function = enumset_decimalint<Frame>; break;
          case ObTextTC: function = enumset_text<Frame>; break;
          case ObJsonTC: function = enumset_json<Frame>; break;
          case ObMySQLDateTC: function = enumset_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = enumset_mdatetime<Frame>; break;
          case ObBitTC: function = enumset_bit<Frame>; break;
          default: break;
        }
        break;
      case ObMySQLDateTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = mdate_int<Frame>; break;
          case ObUIntTC: function = mdate_uint<Frame>; break;
          case ObFloatTC: function = mdate_float<Frame>; break;
          case ObDoubleTC: function = mdate_double<Frame>; break;
          case ObNumberTC: function = mdate_number<Frame>; break;
          case ObDateTimeTC: function = mdate_datetime<Frame>; break;
          case ObDateTC: function = mdate_date<Frame>; break;
          case ObTimeTC: function = mdate_time<Frame>; break;
          case ObYearTC: function = mdate_year<Frame>; break;
          case ObStringTC: function = mdate_string<Frame>; break;
          case ObMySQLDateTimeTC: function = mdate_mdatetime<Frame>; break;
          case ObBitTC: function = mdate_bit<Frame>; break;
          case ObTextTC: function = mdate_text<Frame>; break;
          case ObJsonTC: function = mdate_json<Frame>; break;
          case ObDecimalIntTC: function = mdate_decimalint<Frame>; break;
          default: break;
        }
        break;
      case ObMySQLDateTimeTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = mdatetime_int<Frame>; break;
          case ObUIntTC: function = mdatetime_uint<Frame>; break;
          case ObFloatTC: function = mdatetime_float<Frame>; break;
          case ObDoubleTC: function = mdatetime_double<Frame>; break;
          case ObNumberTC: function = mdatetime_number<Frame>; break;
          case ObDateTimeTC: function = mdatetime_datetime<Frame>; break;
          case ObDateTC: function = mdatetime_date<Frame>; break;
          case ObTimeTC: function = mdatetime_time<Frame>; break;
          case ObYearTC: function = mdatetime_year<Frame>; break;
          case ObStringTC: function = mdatetime_string<Frame>; break;
          case ObMySQLDateTC: function = mdatetime_mdate<Frame>; break;
          case ObBitTC: function = mdatetime_bit<Frame>; break;
          case ObTextTC: function = mdatetime_text<Frame>; break;
          case ObJsonTC: function = mdatetime_json<Frame>; break;
          case ObDecimalIntTC: function = mdatetime_decimalint<Frame>; break;
          default: break;
        }
        break;
      case ObJsonTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = json_int<Frame>; break;
          case ObUIntTC: function = json_uint<Frame>; break;
          case ObFloatTC: function = json_float<Frame>; break;
          case ObDoubleTC: function = json_double<Frame>; break;
          case ObNumberTC: function = json_number<Frame>; break;
          case ObDateTimeTC: function = json_datetime<Frame>; break;
          case ObDateTC: function = json_date<Frame>; break;
          case ObTimeTC: function = json_time<Frame>; break;
          case ObYearTC: function = json_year<Frame>; break;
          case ObStringTC: function = json_string<Frame>; break;
          case ObMySQLDateTC: function = json_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = json_mdatetime<Frame>; break;
          case ObBitTC: function = json_bit<Frame>; break;
          case ObTextTC: function = json_string<Frame>; break;
          case ObJsonTC: function = json_json<Frame>; break;
          case ObDecimalIntTC: function = json_decimalint<Frame>; break;
          default: break;
        }
        break;
      case ObTextTC:
        switch (sql_type_class(target)) {
          case ObIntTC: function = text_int<Frame>; break;
          case ObUIntTC: function = text_uint<Frame>; break;
          case ObFloatTC: function = text_float<Frame>; break;
          case ObDoubleTC: function = text_double<Frame>; break;
          case ObNumberTC: function = text_number<Frame>; break;
          case ObDateTimeTC: function = text_datetime<Frame>; break;
          case ObDateTC: function = text_date<Frame>; break;
          case ObTimeTC: function = text_time<Frame>; break;
          case ObYearTC: function = text_year<Frame>; break;
          case ObStringTC: function = text_string<Frame>; break;
          case ObMySQLDateTC: function = text_mdate<Frame>; break;
          case ObMySQLDateTimeTC: function = text_mdatetime<Frame>; break;
          case ObBitTC: function = text_bit<Frame>; break;
          case ObTextTC: function = text_text<Frame>; break;
          case ObJsonTC: function = text_json<Frame>; break;
          case ObDecimalIntTC: function = text_decimalint<Frame>; break;
          default: break;
        }
        break;
      default: break;
    }
    if (input.get_type_class() != ObGeometryTC) {
      if (function == nullptr) { ret = OB_NOT_SUPPORTED; }
      if (OB_SUCC(ret)) { ret = function(target, context, source, result, context.cast_mode_); }
    }
    if (OB_SUCC(ret) && (ob_is_string_type(target) || ob_is_text_tc(target))) {
      result.set_collation_level(input.get_type_class() == ObStringTC || input.get_type_class() == ObTextTC
          ? input.get_collation_level() : CS_LEVEL_COERCIBLE);
      if (context.dest_collation_ == CS_TYPE_INVALID) { ret = OB_ERR_UNEXPECTED; }
      else { result.set_collation_type(context.dest_collation_); }
    }
  }
  // Publish only a complete, independently owned native value. This also
  // makes aliased input/output safe and prevents a scratch-pointer escape.
  if (OB_SUCC(ret)) {
    ret = clone_typed_value(result, output, result_owner);
    if (ret != OB_SUCCESS && context.res_accuracy_ != nullptr) {
      *context.res_accuracy_ = original_accuracy;
    }
  }
  return ret;
}

int DatumValueCaster::to_datetime_interval(SqlValueType target,
    DatumCastParams &context, const DatumValue &input, DatumValue &output)
{
  int ret = OB_SUCCESS;
  // Match the legacy pointer API's identity fast path, including its lack of
  // result-accuracy mutation for an already-DATETIME value.
  if (input.get_type() == ObDateTimeType) {
    output = input;
    return ret;
  }
  context.warning_ = OB_SUCCESS;
  if (sql_type_class(target) != ObDateTimeTC || input.is_null()) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t value = 0;
    ObScale result_scale = MIN_SCALE_FOR_TEMPORAL;
    switch (input.get_type_class()) {
      case ObIntTC:
        // Preserve the legacy interval interpretation: an integer is a
        // number of days, unlike to_type(..., datetime), which parses a date.
        value = input.get_int() * USECS_PER_DAY;
        break;
      case ObFloatTC:
        value = static_cast<int64_t>(static_cast<double>(input.get_float())
            * static_cast<double>(USECS_PER_DAY));
        result_scale = -1;
        break;
      case ObDoubleTC:
        value = static_cast<int64_t>(input.get_double()
            * static_cast<double>(USECS_PER_DAY));
        result_scale = -1;
        break;
      case ObNumberTC: {
        int64_t int_part = 0;
        int64_t dec_part = 0;
        const number::ObNumber number = input.get_number();
        if (!number.is_int_parts_valid_int64(int_part, dec_part)) {
          ret = OB_INVALID_DATE_FORMAT;
        } else {
          value = static_cast<int64_t>(int_part * USECS_PER_DAY)
              + (number.is_negative() ? -1 : 1)
                  * static_cast<int64_t>(static_cast<double>(dec_part) / NSECS_PER_SEC
                      * static_cast<double>(USECS_PER_DAY));
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        break;
    }
    if (OB_SUCC(ret)) {
      DatumValue result;
      if (OB_ISNULL(context.allocator_v2_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result.set_datetime(value, *context.allocator_v2_))) {
      } else {
        result.set_type(target);
        output = result;
        if (context.res_accuracy_ != nullptr) {
          context.res_accuracy_->set_scale(result_scale);
          context.res_accuracy_->set_precision(DEFAULT_PRECISION_FOR_TEMPORAL);
          context.res_accuracy_->set_length(DEFAULT_LENGTH_FOR_TEMPORAL);
        }
      }
    }
  }
  return ret;
}

namespace {
int compare_native_sql_values(const DatumValue &left, const DatumValue &right,
    DatumCastParams &context, SqlValueType comparison_type,
    ObCollationType comparison_collation, ObCmpOp op, int &result)
{
  int ret = OB_SUCCESS;
  // The comparison type matters only when the input pair requires a cast.
  // In particular, comparing integer/NUMBER directly must not round through
  // the inferred DOUBLE type before checking whether a cast is lossless.
  const bool direct = can_compare_types_without_cast(left.get_meta(), right.get_meta());
  DatumValue l = left, r = right;
  ObArenaAllocator scratch("DatumSqlCmp");
  if (!direct) {
    if (context.allocator_v2_ == nullptr) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      context.dest_collation_ = comparison_collation;
      // Without a target accuracy, DECIMAL comparison uses NUMBER as in the
      // SQL comparison operator. This is not a column-assignment conversion.
      if (comparison_type == ObDecimalIntType) { comparison_type = ObNumberType; }
      // The old pointer-returning cast entry clears warning even on a no-op.
      context.warning_ = OB_SUCCESS;
      if (OB_FAIL(DatumValueCaster::to_type(comparison_type, context, left, scratch, l))) {
      } else {
        context.warning_ = OB_SUCCESS;
        if (OB_FAIL(DatumValueCaster::to_type(comparison_type, context, right, scratch, r))) {
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    // The explicit comparison collation governs comparison of values that
    // already share a type/collation, too. These views do not alter inputs.
    if (comparison_collation != CS_TYPE_INVALID) {
      if (l.is_string_type()) { l.set_collation_type(comparison_collation); }
      if (r.is_string_type()) { r.set_collation_type(comparison_collation); }
    }
    int cmp = 0;
    if (op == CO_EQ) {
      bool equal = false;
      ret = datum_values_equal(l, r, equal);
      cmp = equal ? 1 : 0;
    } else {
      ret = compare_datum_values(l, r, cmp);
    }
    if (ret == OB_NOT_SUPPORTED) { ret = OB_ERR_UNEXPECTED; }
    if (OB_SUCC(ret)) { result = cmp; }
  }
  return ret;
}
} // namespace
int DatumValueComparator::compare(const DatumValue &left, const DatumValue &right,
    DatumCastParams &context, SqlValueType comparison_type,
    ObCollationType comparison_collation, int &result)
{
  return compare_native_sql_values(left, right, context, comparison_type,
      comparison_collation, CO_CMP, result);
}
int DatumValueComparator::equal(const DatumValue &left, const DatumValue &right,
    DatumCastParams &context, SqlValueType comparison_type,
    ObCollationType comparison_collation, bool &result)
{
  int comparison = 0;
  const int ret = compare_native_sql_values(left, right, context, comparison_type,
      comparison_collation, CO_EQ, comparison);
  if (ret == OB_SUCCESS) { result = comparison != 0; }
  return ret;
}

int DatumValueEvaluator::is_true(const DatumValue &value, ObCastMode cast_mode, bool &result)
{
  int ret = OB_SUCCESS;
  result = false;
  if (value.is_null()) {
  } else if (value.is_numeric_type()) {
    switch (value.get_type_class()) {
      case ObIntTC: result = value.get_int() != 0; break;
      case ObUIntTC: result = value.get_uint64() != 0; break;
      case ObFloatTC: result = value.get_float() != 0; break;
      case ObDoubleTC: result = value.get_double() != 0; break;
      case ObNumberTC: result = !value.get_number().is_zero(); break;
      case ObBitTC: result = value.get_bit() != 0; break;
      case ObDecimalIntTC: result = value.get_int_bytes() != 0; break;
      default: ret = OB_ERR_UNEXPECTED; break;
    }
  } else {
    ObArenaAllocator allocator(ObModIds::BLOCK_ALLOC);
    ObCastCtx context(&allocator, nullptr, cast_mode, CS_TYPE_INVALID);
    DatumValue number;
    if (OB_FAIL(DatumValueCaster::to_type(ObNumberType, context, value, allocator, number))) {
    } else if (number.is_null() || !number.is_number()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      result = !number.get_number().is_zero();
    }
  }
  return ret;
}

int DatumValueEvaluator::is_false(const DatumValue &value, ObCastMode mode, bool &result)
{
  result = false;
  bool truth = false;
  const int ret = is_true(value, mode, truth);
  if (ret == OB_SUCCESS && !value.is_null()) { result = !truth; }
  return ret;
}

int DatumBoolEvaluator::is_true(const DatumParameter &value, ObCastMode mode, bool &result)
{
  return DatumValueEvaluator::is_true(value.value_view(), mode, result);
}


ObGeoType ObGeoCastUtils::get_geo_type_from_cast_mode(uint64_t cast_mode)
{
  ObGeoType type = ObGeoType::GEOMETRY;
  if (CM_IS_GEOMETRY_GEOMETRY(cast_mode)) {
    type = ObGeoType::GEOMETRY;
  } else if (CM_IS_GEOMETRY_POINT(cast_mode)) {
    type = ObGeoType::POINT;
  } else if (CM_IS_GEOMETRY_LINESTRING(cast_mode)) {
    type = ObGeoType::LINESTRING;
  } else if (CM_IS_GEOMETRY_POLYGON(cast_mode)) {
    type = ObGeoType::POLYGON;
  } else if (CM_IS_GEOMETRY_MULTIPOINT(cast_mode)) {
    type = ObGeoType::MULTIPOINT;
  } else if (CM_IS_GEOMETRY_MULTILINESTRING(cast_mode)) {
    type = ObGeoType::MULTILINESTRING;
  } else if (CM_IS_GEOMETRY_MULTIPOLYGON(cast_mode)) {
    type = ObGeoType::MULTIPOLYGON;
  } else if (CM_IS_GEOMETRY_GEOMETRYCOLLECTION(cast_mode)) {
    type = ObGeoType::GEOMETRYCOLLECTION;
  }
  return type;
}

int ObGeoCastUtils::set_geo_type_to_cast_mode(ObGeoType geo_type, uint64_t &cast_mode)
{
  int ret = OB_SUCCESS;
  switch (geo_type) {
    case ObGeoType::GEOMETRY: {
      CM_SET_GEOMETRY_GEOMETRY(cast_mode);
      break;
    }
    case ObGeoType::POINT: {
      CM_SET_GEOMETRY_POINT(cast_mode);
      break;
    }
    case ObGeoType::LINESTRING: {
      CM_SET_GEOMETRY_LINESTRING(cast_mode);
      break;
    }
    case ObGeoType::POLYGON: {
      CM_SET_GEOMETRY_POLYGON(cast_mode);
      break;
    }
    case ObGeoType::MULTIPOINT: {
      CM_SET_GEOMETRY_MULTIPOINT(cast_mode);
      break;
    }
    case ObGeoType::MULTILINESTRING: {
      CM_SET_GEOMETRY_MULTILINESTRING(cast_mode);
      break;
    }
    case ObGeoType::MULTIPOLYGON: {
      CM_SET_GEOMETRY_MULTIPOLYGON(cast_mode);
      break;
    }
    case ObGeoType::GEOMETRYCOLLECTION: {
      CM_SET_GEOMETRY_GEOMETRYCOLLECTION(cast_mode);
      break;
    }
    default: {
      ret = OB_INVALID_ARGUMENT;
      break;
    }
  }
  return ret;
}

void ObGeoCastUtils::geo_cast_error_handle(int err_code,
                                           ObGeoType src_type,
                                           ObGeoType dst_type,
                                           const ObGeoErrLogInfo &log_info)
{
  if (OB_ERR_INVALID_CAST_TO_GEOMETRY == err_code) {
    LOG_USER_ERROR(OB_ERR_INVALID_CAST_TO_GEOMETRY,
                   ObGeoTypeUtil::get_geo_name_by_type(src_type),
                   ObGeoTypeUtil::get_geo_name_by_type(dst_type));
  } else if (OB_ERR_GEOMETRY_PARAM_LONGITUDE_OUT_OF_RANGE == err_code) {
    LOG_USER_ERROR(OB_ERR_GEOMETRY_PARAM_LONGITUDE_OUT_OF_RANGE,
                   ObGeometryTypeCastUtil::get_cast_name(dst_type),
                   log_info.value_out_of_range_,
                   -180.000000,
                   180.000000);
  } else if (OB_ERR_GEOMETRY_PARAM_LATITUDE_OUT_OF_RANGE == err_code) {
    LOG_USER_ERROR(OB_ERR_GEOMETRY_PARAM_LATITUDE_OUT_OF_RANGE,
                   ObGeometryTypeCastUtil::get_cast_name(dst_type),
                   log_info.value_out_of_range_,
                   -90.000000,
                   90.000000);
  } else if (OB_ERR_GIS_INVALID_DATA == err_code) {
    LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, ObGeometryTypeCastUtil::get_cast_name(dst_type));
  } else if (OB_ERR_INVALID_CAST_POLYGON_RING_DIRECTION == err_code) {
    LOG_USER_ERROR(OB_ERR_INVALID_CAST_POLYGON_RING_DIRECTION,
                   ObGeoTypeUtil::get_geo_name_by_type(src_type),
                   ObGeoTypeUtil::get_geo_name_by_type(dst_type));
  }
}

} // end namespace common
} // end namespace oceanbase
