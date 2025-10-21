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

#ifndef _OB_EXPR_DATUM_CAST_
#define _OB_EXPR_DATUM_CAST_

#include "common/object/ob_object.h"
#include "common/ob_zerofill_info.h"
#include "lib/timezone/ob_timezone_info.h"
#include "lib/timezone/ob_time_convert.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/charset/ob_charset.h"
#include "share/ob_errno.h"
#include "share/datum/ob_datum.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlanCtx;
struct ObUserLoggingCtx;

class ObDataTypeCastUtil
{
public:
  static int common_string_decimalint_wrap(const ObExpr &expr, const ObString &in_str,
                                          const ObUserLoggingCtx *user_logging_ctx,
                                          ObDecimalIntBuilder &res_val);
  static int common_string_number_wrap(const ObExpr &expr,
                                      const ObString &in_str,
                                      const ObUserLoggingCtx *user_logging_ctx,
                                      ObIAllocator &alloc,
                                      number::ObNumber &nmb);
  static int common_uint_int_wrap(const ObExpr &expr, const ObObjType &out_type, uint64_t in_val,
                                  ObEvalCtx &ctx, int64_t &out_val);
  static int common_double_float_wrap(const ObExpr &expr, const double in_val, float &out_val);
  static int common_double_int_wrap(const double in, int64_t &out,
                                    const int64_t trunc_min_value, const int64_t trunc_max_value);
  static int common_number_datetime_wrap(const number::ObNumber nmb, const ObTimeConvertCtx &cvrt_ctx,
                                         int64_t &out_val, const ObCastMode cast_mode,
                                         bool is_mysql_compat_dates);
  static int common_number_date_wrap(const number::ObNumber &nmb, const ObCastMode cast_mode,
                                     int32_t &out_val, bool is_mysql_compat_dates);
  static int common_string_float_wrap(const ObExpr &expr, const ObString &in_str, float &out_val);

  template <typename IN_TYPE>
  static int common_floating_number_wrap(const IN_TYPE in_val,
                                         const ob_gcvt_arg_type arg_type,
                                         ObIAllocator &alloc,
                                         number::ObNumber &number,
                                         ObEvalCtx &ctx,
                                         const ObCastMode cast_mode);
  template<typename IN_TYPE>
  static int common_floating_decimalint_wrap(const IN_TYPE in_val,
                                             const ob_gcvt_arg_type arg_type,
                                             ObIAllocator &alloc,
                                             ObDecimalInt *&decint,
                                             int32_t &int_bytes,
                                             int16_t &scale,
                                             int16_t &precision);
  static void log_user_error_warning(const ObUserLoggingCtx *user_logging_ctx,
                                     const int64_t ret,
                                     const ObString &type_str,
                                     const ObString &input,
                                     const ObCastMode cast_mode);
};

class ObOdpsDataTypeCastUtil : public ObDataTypeCastUtil
{
public:
  static int common_string_decimalint_wrap(const ObExpr &expr, const ObString &in_str,
                                          const ObUserLoggingCtx *user_logging_ctx,
                                          ObDecimalIntBuilder &res_val);
  static int common_string_string_wrap(const ObExpr &expr,
                                      const ObObjType in_type,
                                      const ObCollationType in_cs_type,
                                      const ObObjType out_type,
                                      const ObCollationType out_cs_type,
                                      const ObString &in_str,
                                      ObEvalCtx &ctx,
                                      ObDatum &res_datum,
                                      bool& has_set_res);
  static int common_string_text_wrap(const ObExpr &expr,
                                    const ObString &in_str,
                                    ObEvalCtx &ctx,
                                    const ObLobLocatorV2 *lob_locator,
                                    ObDatum &res_datum,
                                    ObObjType &in_type,
                                    ObCollationType &in_cs_type);
  static int common_check_convert_string(const ObExpr &expr,
                                        ObEvalCtx &ctx,
                                        const ObString &in_str,
                                        ObObjType in_type,
                                        ObCollationType in_cs_type,
                                        ObDatum &res_datum,
                                        bool &has_set_res);
};

template <typename IN_TYPE, typename OUT_TYPE>
static OB_INLINE int common_floating_int(IN_TYPE &in_val, OUT_TYPE &out_val)
{
  static constexpr double ROUND_DOUBLE = 0.5;
  int ret = OB_SUCCESS;
  out_val = 0;
  if (in_val < 0) {
    out_val = static_cast<OUT_TYPE>(in_val - ROUND_DOUBLE);
  } else if (in_val > 0) {
    out_val = static_cast<OUT_TYPE>(in_val + ROUND_DOUBLE);
  } else {
    out_val = static_cast<OUT_TYPE>(in_val);
  }
  return ret;
}

template <typename IN_TYPE>
static bool is_ieee754_nan_inf(const IN_TYPE in_val,
                               char buf[], int64_t &length)
{
  bool is_nan_inf = true;
  is_nan_inf = false;
  return is_nan_inf;
}

int time_usec_scale_check(const ObCastMode &cast_mode,
                          const ObAccuracy &accuracy,
                          const int64_t value);

int string_length_check(const ObExpr &expr,
                        const ObCastMode &cast_mode,
                        const ObAccuracy &accuracy,
                        const ObObjType type,
                        const ObCollationType cs_type,
                        ObEvalCtx &ctx,
                        const ObDatum &in_datum,
                        ObDatum &res_datum,
                        int &warning);
int string_length_check(const ObExpr &expr,
                        const ObCastMode &cast_mode,
                        const ObAccuracy &accuracy,
                        const ObObjType type,
                        const ObCollationType cs_type,
                        ObEvalCtx &ctx,
                        const int64_t idx,
                        const ObString &in_str,
                        ObIVector &out_vec,
                        int &warning);

// extract accuracy info from %expr and call datum_accuracy_check() below.
int datum_accuracy_check(const ObExpr &expr,
                         const uint64_t cast_mode,
                         ObEvalCtx &ctx,
                         bool has_lob_header,
                         const common::ObDatum &in_datum,
                         ObDatum &res_datum,
                         int &warning);

// check if accuracy in %in_datum is ok. if ok, call res_datum.set_datum(in_datum).
// if not, will trunc data in %in_datum and put it in res_datum.
// this func makes sure data in %in_datum and %in_datum itself will not be changed.
int datum_accuracy_check(const ObExpr &expr,
                         const uint64_t cast_mode,
                         ObEvalCtx &ctx,
                         const common::ObAccuracy &acc,
                         bool has_lob_header,
                         const common::ObDatum &in_datum,
                         ObDatum &res_datum,
                         int &warning);

int vector_accuracy_check(const ObExpr &expr,
                          const uint64_t cast_mode,
                          ObEvalCtx &ctx,
                          bool has_lob_header,
                          const int64_t idx,
                          const ObIVector &in_vec,
                          ObIVector &out_vec,
                          int &warning);

int vector_accuracy_check(const ObExpr &expr,
                         const uint64_t cast_mode,
                         ObEvalCtx &ctx,
                         const ObAccuracy &accuracy,
                         bool has_lob_header,
                         const int64_t idx,
                         const ObIVector &in_vec,
                         ObIVector &out_vec,
                         int &warning);
// According to in_type, force_use_standard_format information, get format_str, prioritize obtaining from the local session variable list saved by rt_expr, if it does not exist then get from session
int common_get_nls_format(const ObBasicSessionInfo *session,
                          ObEvalCtx &ctx,
                          const ObExpr *rt_expr,
                          const ObObjType in_type,
                          const bool force_use_standard_format,
                          ObString &format_str);
// Check if str is valid with check_cs_type as the character set
// strict_modeunder, if the above checks fail, return error code
// Otherwise return the longest valid string with check_cs_type as the character set
int string_collation_check(const bool is_strict_mode,
                           const common::ObCollationType check_cs_type,
                           const common::ObObjType str_type,
                           common::ObString &str);
// Convert the value in T to ob_time structure
template<typename T>
int ob_datum_to_ob_time_with_date(const T &datum,
                                  const common::ObObjType type,
                                  const ObScale scale,
                                  const common::ObTimeZoneInfo* tz_info,
                                  common::ObTime& ob_time,
                                  const int64_t cur_ts_value,
                                  const ObDateSqlMode date_sql_mode,
                                  const bool has_lob_header)
{
  int ret = OB_SUCCESS;
  switch (ob_obj_type_class(type)) {
    case ObIntTC:
      // fallthrough.
    case ObUIntTC: {
      ret = ObTimeConverter::int_to_ob_time_with_date(datum.get_int(), ob_time, date_sql_mode);
      break;
    }
    case ObOTimestampTC: {
      ret = ObTimeConverter::otimestamp_to_ob_time(type, datum.get_otimestamp_tiny(),
                                                     tz_info, ob_time);
      break;
    }
    case ObDateTimeTC: {
      ret = ObTimeConverter::datetime_to_ob_time(datum.get_datetime(),
          (ObTimestampType == type) ? tz_info : NULL, ob_time);
      break;
    }
    case ObDateTC: {
      ret = ObTimeConverter::date_to_ob_time(datum.get_date(), ob_time);
      break;
    }
    case ObTimeTC: {
      int64_t dt_value = 0;
      if (OB_FAIL(ObTimeConverter::timestamp_to_datetime(cur_ts_value, tz_info, dt_value))) {
        SQL_ENG_LOG(WARN, "convert timestamp to datetime failed", K(ret));
      } else {
        const int64_t usec_per_day = 3600 * 24 * USECS_PER_SEC;
        // Extract the date from datetime, then convert to microseconds
        int64_t day_usecs = dt_value - dt_value % usec_per_day;
        ret = ObTimeConverter::datetime_to_ob_time(datum.get_time() + day_usecs, NULL, ob_time);
      }
      break;
    }
    case ObTextTC: // TODO@hanhui texttc share with the stringtc temporarily
    case ObStringTC: {
      ObScale res_scale = -1;
      ObArenaAllocator lob_allocator(ObModIds::OB_LOB_ACCESS_BUFFER,
                                     OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID());
      ObString str = datum.get_string();
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(
              &lob_allocator, type, CS_TYPE_BINARY, has_lob_header, str))) {
        SQL_ENG_LOG(WARN, "fail to get real string data", K(ret), K(datum));
      } else {
        ret = ObTimeConverter::str_to_ob_time_with_date(str, ob_time, &res_scale, date_sql_mode);
      }
      break;
    }
    case ObDecimalIntTC:
    case ObNumberTC: {
      int64_t int_part = 0;
      int64_t dec_part = 0;
      number::ObNumber num;
      ObNumStackOnceAlloc tmp_alloc;
      if (ob_is_decimal_int(type)) {
        if (OB_FAIL(wide::to_number(datum.get_decimal_int(),
                                    datum.get_int_bytes(), scale, tmp_alloc,
                                    num))) {
          SQL_ENG_LOG(WARN, "failed to cast decimal int to number", K(ret));
        }
      } else {
        num = datum.get_number();
      }
      if (OB_FAIL(ret)) { // do nothing
      } else if (num.is_negative()) {
        ret = OB_INVALID_DATE_FORMAT;
        SQL_ENG_LOG(WARN, "invalid date format", K(ret), K(num));
      } else if (!num.is_int_parts_valid_int64(int_part, dec_part)) {
        ret = OB_INVALID_DATE_FORMAT;
        SQL_ENG_LOG(WARN, "invalid date format", K(ret), K(num));
      } else {
        ret = ObTimeConverter::int_to_ob_time_with_date(int_part, ob_time, date_sql_mode);
        if (OB_SUCC(ret)) {
          ob_time.parts_[DT_USEC] = (dec_part + 500) / 1000;
          ObTimeConverter::adjust_ob_time(ob_time, true);
        }
      }
      break;
    }
    case ObMySQLDateTC: {
      ob_time.mode_ |= DT_TYPE_DATE;
      ret = ObTimeConverter::mdate_to_ob_time<true>(datum.get_mysql_date(), ob_time);
      break;
    }
    case ObMySQLDateTimeTC: {
      ob_time.mode_ |= DT_TYPE_DATETIME;
      ret = ObTimeConverter::mdatetime_to_ob_time<true>(datum.get_mysql_datetime(), ob_time);
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "cast to time with date");
    }
  }
  SQL_ENG_LOG(DEBUG, "end ob_datum_to_ob_time_with_date", K(type),
              K(cur_ts_value), K(ob_time), K(ret));
  return ret;
}
template<typename T>
int ob_datum_to_ob_time_without_date(const T &datum,
                                    const common::ObObjType type,
                                    const ObScale scale,
                                    const common::ObTimeZoneInfo *tz_info,
                                    common::ObTime &ob_time,
                                    const bool has_lob_header)
{
  int ret = OB_SUCCESS;
  switch (ob_obj_type_class(type)) {
    case ObIntTC:
      // fallthrough.
    case ObUIntTC: {
      if (OB_FAIL(ObTimeConverter::int_to_ob_time_without_date(datum.get_int(), ob_time))) {
        SQL_ENG_LOG(WARN, "int to ob time without date failed", K(ret));
      } else {
        // When converting intTC to time in mysql, if hour exceeds 838, then time should be null, rather than the maximum value.
        const int64_t time_max_val = TIME_MAX_VAL;    // 838:59:59.
        int64_t value = ObTimeConverter::ob_time_to_time(ob_time);
        if (value > time_max_val) {
          ret = OB_INVALID_DATE_VALUE;
        }
      }
      break;
    }
    case ObOTimestampTC: {
      ret = ObTimeConverter::otimestamp_to_ob_time(type, datum.get_otimestamp_tiny(),
                                                     tz_info, ob_time);
      break;
    }
    case ObDateTimeTC: {
      ret = ObTimeConverter::datetime_to_ob_time(datum.get_datetime(),  (ObTimestampType == type)
                                                ? tz_info : NULL, ob_time);
      break;
    }
    case ObDateTC: {
      ret = ObTimeConverter::date_to_ob_time(datum.get_date(), ob_time);
      break;
    }
    case ObTimeTC: {
      ret = ObTimeConverter::time_to_ob_time(datum.get_time(), ob_time);
      break;
    }
    case ObTextTC: // TODO@hanhui texttc share with the stringtc temporarily
    case ObStringTC: {
      ObArenaAllocator lob_allocator(ObModIds::OB_LOB_ACCESS_BUFFER, OB_MALLOC_NORMAL_BLOCK_SIZE, MTL_ID());
      ObString str = datum.get_string();
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(
              &lob_allocator, type, CS_TYPE_BINARY, has_lob_header, str))) {
        SQL_ENG_LOG(WARN, "fail to get real string data", K(ret), K(datum));
      } else {
        ret = ObTimeConverter::str_to_ob_time_without_date(str, ob_time);
        if (OB_SUCC(ret)) {
          int64_t value = ObTimeConverter::ob_time_to_time(ob_time);
          int64_t tmp_value = value;
          ObTimeConverter::time_overflow_trunc(value);
          if (value != tmp_value) {
            ObTimeConverter::time_to_ob_time(value, ob_time);
          }
        }
      }
      break;
    }
    case ObDecimalIntTC:
    case ObNumberTC: {
      int64_t int_part = 0;
      int64_t dec_part = 0;
      ObNumStackOnceAlloc tmp_alloc;
      number::ObNumber num;
      if (ob_is_decimal_int(type)) {
        if (OB_FAIL(wide::to_number(datum.get_decimal_int(),
                                    datum.get_int_bytes(), scale, tmp_alloc,
                                    num))) {
          SQL_ENG_LOG(WARN, "failed to cast decimal int to number", K(ret));
        }
      } else {
        num = datum.get_number();
      }
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (!num.is_int_parts_valid_int64(int_part, dec_part)) {
        ret = OB_INVALID_DATE_FORMAT;
        SQL_ENG_LOG(WARN, "invalid date format", K(ret), K(num));
      } else {
        if (OB_FAIL(ObTimeConverter::int_to_ob_time_without_date(int_part, ob_time, dec_part))) {
          SQL_ENG_LOG(WARN, "int to ob time without date failed", K(ret));
        } else {
          if ((!ob_time.parts_[DT_YEAR]) && (!ob_time.parts_[DT_MON]) &&
              (!ob_time.parts_[DT_MDAY])) {
            // When converting intTC to time in mysql, if it exceeds 838:59:59, then time should be null, rather than the maximum value.
            const int64_t time_max_val = TIME_MAX_VAL; // 838:59:59.
            int64_t value = ObTimeConverter::ob_time_to_time(ob_time);
            if (value > time_max_val) {
              ret = OB_INVALID_DATE_VALUE;
              SQL_ENG_LOG(WARN, "invalid date value", K(ob_time), K(value));
            }
          }
        }
      }
      break;
    }
    case ObMySQLDateTC: {
      ret = ObTimeConverter::mdate_to_ob_time<true>(datum.get_mysql_date(), ob_time);
      break;
    }
    case ObMySQLDateTimeTC: {
      ret = ObTimeConverter::mdatetime_to_ob_time<true>(datum.get_mysql_datetime(), ob_time);
      break;
    }
    default: {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "cast to time without date");
    }
  }
  SQL_ENG_LOG(DEBUG, "end ob_datum_to_ob_time_without_date", K(type), K(ob_time), K(ret));
  return ret;
}

int get_accuracy_from_parse_node(const ObExpr &expr, ObEvalCtx &ctx,
                                 ObAccuracy &accuracy, ObObjType &dest_type);

int common_string_double(const ObExpr &expr,
                         const ObObjType &in_type,
                         const ObCollationType &in_cs_type,
                         const ObObjType &out_type,
                         const ObString &in_str,
                         ObDatum &res_datum);


int get_cast_ret_wrap(const ObCastMode &cast_mode, int ret, int &warning);
// Perform datetime to string conversion, except for ob_datum_cast.cpp which needs to use it, some expressions also need to convert the result
// From datetime to string, for example ObExprTimeStampAdd
int common_datetime_string(const ObExpr &expr,
                           const common::ObObjType in_type,
                           const common::ObObjType out_type,
                           const common::ObScale in_scale,
                           bool force_use_std_nls_format,
                           const int64_t in_val, ObEvalCtx &ctx, char *buf,
                           int64_t buf_len, int64_t &out_len);
int padding_char_for_cast(int64_t padding_cnt, 
                          const common::ObCollationType &padding_cs_type,
                          common::ObIAllocator &alloc, 
                          common::ObString &padding_res);

int check_decimalint_accuracy(const ObCastMode cast_mode,
                              const ObDecimalInt *res_decint, const int32_t int_bytes,
                              const ObPrecision precision, const ObScale scale,
                              ObDecimalIntBuilder &res_val, int &warning);

inline bool decimal_int_truncated_check(const ObDecimalInt *decint, const int32_t int_bytes,
                                       const unsigned scale)
{
#define TRUNC_CHECK(int_type) \
  if (wide::ObDecimalIntConstValue::get_int_bytes_by_precision(scale) > int_bytes) {  \
    bret = (*reinterpret_cast<const int_type *>(decint)) != 0;              \
  } else {                                                                  \
    const int_type sf = get_scale_factor<int_type>(scale);                  \
    bret = (((*reinterpret_cast<const int_type *>(decint)) % sf) != 0);     \
  }

  int ret = OB_SUCCESS;
  bool bret = false;
  DISPATCH_WIDTH_TASK(int_bytes, TRUNC_CHECK);
  return bret;
#undef TRUNC_CHECK
}
// copied from ob_obj_cast.cpp, function logic has not been modified, only changed the input parameter from ObObj to ObDatum
class ObDatumHexUtils
{
public:
static int hextoraw_string(const ObExpr &expr,
                    const common::ObString &in_str,
                    ObEvalCtx &ctx,
                    ObDatum &res_datum,
                    bool &has_set_res);
static int get_uint(const common::ObObjType &in_type,
                    const int8_t scale,
                    const common::ObDatum &in,
                    common::ObIAllocator &alloc, common::number::ObNumber &out);
static int uint_to_raw(const common::number::ObNumber &uint_num, const ObExpr &expr,
                           ObEvalCtx &ctx, ObDatum &res_datum);
static int unhex(const ObExpr &expr,
                 const common::ObString &in_str,
                 ObEvalCtx &ctx,
                 ObDatum &res_datum,
                 bool &has_set_res);
static int hex(const ObExpr &expr,
               const common::ObString &in_str,
               ObEvalCtx &ctx,
               common::ObIAllocator &calc_alloc,
               ObDatum &res_datum,
               bool upper_case = true);
};

class ObDatumCast 
{
public:
  static int get_implicit_cast_function(const common::ObObjType in_type,
                                        const common::ObCollationType in_cs_type,
                                        const common::ObObjType out_type,
                                        const common::ObCollationType out_cs_type,
                                        const int64_t cast_mode,
                                        ObExpr::EvalFunc &eval_func);
  // According to in_type/out_type etc. information, get cast func
  static int choose_cast_function(const common::ObObjType in_type,
                                  const common::ObCollationType in_cs_type,
                                  const common::ObObjType out_type,
                                  const common::ObCollationType out_cs_type,
                                  const int64_t cast_mode,
                                  common::ObIAllocator &allocator,
                                  bool &just_eval_arg,
                                  ObExpr &rt_expr);
  static int get_enumset_cast_function(const common::ObObjTypeClass in_tc,
                                       const common::ObObjType out_type,
                                       ObExpr::EvalEnumSetFunc &eval_func);
  // Check if the conversion is valid, some checks cannot be reflected in the conversion matrix
  // Example: string/text -> string/text when it is nonblob -> blob,
  // Requirement nonblob must be char/varchar type, this check cannot be reflected in the conversion matrix
  // These checks will be performed in this method
  static int check_can_cast(const common::ObObjType in_type,
                            const common::ObCollationType in_cs_type,
                            const common::ObObjType out_type,
                            const common::ObCollationType out_cs_type);
  // Some cast do nothing, for example int->bit, directly call cast_eval_arg() to calculate the value of the argument
  // CG phase will use this method to determine if result space does not need to be allocated for the cast expression, and can directly point to the result of the parameter instead
  static int is_trivial_cast(const common::ObObjType in_type,
                                const common::ObCollationType in_cs_type,
                                const common::ObObjType out_type,
                                const common::ObCollationType out_cs_type,
                                const common::ObCastMode &cast_mode,
                                bool &just_eval_arg);
  // Function same as:
  //    EXPR_DEFINE_CAST_CTX(expr_ctx, cast_mode)
  //    EXPR_CAST_OBJ_V2(obj_type, obj, res_obj)
  static int cast_obj(ObEvalCtx &ctx, common::ObIAllocator &alloc,
                      const common::ObObjType &dst_type,
                      common::ObAccuracy &dst_acc,
                      const common::ObCollationType &dst_cs_type,
                      const common::ObObj &src_obj,
                      common::ObObj &dst_obj);

  static bool is_implicit_cast(const ObExpr &expr)
  {
    bool bret = false;
    if (T_FUN_SYS_CAST == expr.type_ && CM_IS_IMPLICIT_CAST(expr.extra_)) {
      bret = true;
    }
    return bret;
  }

  static bool is_explicit_cast(const ObExpr &expr)
  {
    bool bret = false;
    if (T_FUN_SYS_CAST == expr.type_ && CM_IS_EXPLICIT_CAST(expr.extra_)) {
      bret = true;
    }
    return bret;
  }

  inline static bool need_scale_decimalint(const ObScale in_scale,
                                           const ObPrecision in_precision,
                                           const ObScale out_scale,
                                           const ObPrecision out_precision) {
    bool ret = false;
    if (in_scale != out_scale) {
      ret = true;
    } else if (get_decimalint_type(in_precision) !=
               get_decimalint_type(out_precision)) {
      ret = true;
    }
    return ret;
  }

  inline static bool need_scale_decimalint(const ObScale in_scale,
                                           const int32_t in_bytes,
                                           const ObScale out_scale,
                                           const int32_t out_bytes) {
    bool ret = false;
    if (in_scale != out_scale) {
      ret = true;
    } else if (in_bytes != out_bytes) {
      ret = true;
    }
    return ret;
  }

  static int common_scale_decimalint(const ObDecimalInt *decint, const int32_t int_bytes,
                                     const ObScale in_scale, const ObScale out_scale,
                                     const ObPrecision out_prec,
                                     const ObCastMode cast_mode, ObDecimalIntBuilder &val,
                                     const ObUserLoggingCtx *column_conv_ctx = NULL);

  static int align_decint_precision_unsafe(const ObDecimalInt *decint, const int32_t int_bytes,
                                           const int32_t expected_int_bytes,
                                           ObDecimalIntBuilder &res);
  static void get_decint_cast(ObObjTypeClass in_tc, ObPrecision in_prec, ObScale in_scale,
                              ObPrecision out_prec, ObScale out_scale, bool is_explicit,
                              ObExpr::EvalBatchFunc &batch_cast_func, ObExpr::EvalFunc &cast_func);
};

class ObDatumCaster {
public:
  ObDatumCaster()
    : inited_(false),
      eval_ctx_(NULL),
      cast_expr_(NULL),
      extra_cast_expr_(NULL) {}
  ~ObDatumCaster() {}

  // init eval_ctx_/cast_expr_/extra_cast_expr_/frame. all mem comes from ObExecContext.
  // frame layout:
  // ObDatum | ObDatum | ObDynReserveBuf | res_buf | ObDynReserveBuf | res_buf
  // res_buf_len is 128
  int init(ObExecContext &ctx);

  // same with ObObjCaster::to_type().
  // input is ObExpr, and output is ObDatum, it's better if input is also ObDatum.
  // we will do this later if necessary.
  // subschema id from sql udt type is a combination of (cs_type_ and cs_level_), 
  // datum meta does not have cs_level_, or we can use ObDatum precision_ as subschema id? 
  int to_type(const ObDatumMeta &dst_type,
              const ObExpr &src_expr,
              const common::ObCastMode &cm,
              common::ObDatum *&res,
              int64_t batch_idx = 0,
              const uint16_t subschema_id = 0,
              const int32_t max_length = LENGTH_UNKNOWN_YET);
  // for xxx -> enumset.
  int to_type(const ObDatumMeta &dst_type,
              const common::ObIArray<common::ObString> &str_values,
              const ObExpr &src_expr,
              const common::ObCastMode &cm,
              common::ObDatum *&res,
              int64_t batch_idx = 0);

  int destroy();
private:
  DISALLOW_COPY_AND_ASSIGN(ObDatumCaster);

  // setup following data member of ObExpr:
  // datum_meta_, obj_meta_, obj_datum_map_, eval_func_,
  // args_, arg_cnt_, parents_, parent_cnt_, basic_funcs_.
  int setup_cast_expr(const ObDatumMeta &dst_type,
                      const ObExpr &src_expr,
                      const common::ObCastMode cm,
                      ObExpr &cast_expr,
                      const uint16_t subschema_id = 0,
                      const int32_t max_length = LENGTH_UNKNOWN_YET);
  bool inited_;
  ObEvalCtx *eval_ctx_;
  ObExpr *cast_expr_;
  ObExpr *extra_cast_expr_;
};

struct ObBatchCast
{
  using batch_func_ = int (*)(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                              const int64_t batch_size);
  static batch_func_ get_explicit_cast_func(const ObObjTypeClass tc1, const ObObjTypeClass tc2);

  static batch_func_ get_implicit_cast_func(const ObObjTypeClass tc1, const ObObjTypeClass tc2);

  template <ObObjTypeClass in_tc, ObObjTypeClass out_tc>
  static int explicit_batch_cast(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                 const int64_t batch_size);

  template <ObObjTypeClass in_tc, ObObjTypeClass out_tc>
  static int implicit_batch_cast(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                 const int64_t batch_size);

private:
  template<ObObjTypeClass tc1, ObObjTypeClass tc2>
  friend struct init_batch_func;
  static batch_func_ implicit_batch_funcs_[ObMaxTC][ObMaxTC];
  static batch_func_ explicit_batch_funcs_[ObMaxTC][ObMaxTC];
};

struct QuestionmarkDynEvalInfo
{
  union
  {
    struct
    {
      int16_t in_scale_;
      int16_t in_precision_;
      int32_t param_idx_;
    };
    int64_t extra_;
  };
  QuestionmarkDynEvalInfo() : extra_(0){};
  QuestionmarkDynEvalInfo(int64_t extra): extra_(extra) {}
  explicit operator int64_t()
  {
    return extra_;
  }
};

} // namespace sql
} // namespace oceanbase

#endif // _OB_EXPR_DATUM_CAST_
