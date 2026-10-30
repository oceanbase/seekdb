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

#define USING_LOG_PREFIX SQL_OPT

#include "ob_opt_est_utils.h"
#include "share/datum/datum_value_cast.h"
#include "share/datum/typed_datum_compare.h"
#include "sql/optimizer/ob_log_plan.h"
#include "sql/engine/expr/ob_expr_equal.h"

namespace oceanbase
{
using namespace common;
using namespace number;
using namespace share::schema;
namespace sql
{
bool ObOptEstUtils::is_monotonic_op(const ObItemType type)
{
  return  (T_OP_ADD == type || T_OP_MINUS == type || T_OP_MUL == type || T_FUN_SYS_CAST == type);
}

int ObOptEstUtils::extract_column_exprs_with_op_check(
    const ObRawExpr *raw_expr,
    ObIArray<const ObColumnRefRawExpr*> &column_exprs,
    bool &only_monotonic_op,
    const int64_t level /* = 0 */)
{
  int ret = OB_SUCCESS;
  bool is_stack_overflow = false;
  if (0 == level) {
    only_monotonic_op = true;
  }
  if (OB_ISNULL(raw_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(check_stack_overflow(is_stack_overflow))) {
  } else if (is_stack_overflow) {
    ret = OB_SIZE_OVERFLOW;
  } else if (raw_expr->is_column_ref_expr()) {
    ret = column_exprs.push_back(static_cast<const ObColumnRefRawExpr *>(raw_expr));
  } else if (raw_expr->is_const_expr()) {
    //do nothing
  } else {
    if (!is_monotonic_op(raw_expr->get_expr_type())) {
      only_monotonic_op = false;
    }
    int64_t N = raw_expr->get_param_count();
    for (int64_t i = 0; OB_SUCC(ret) && i < N; ++i) {
      if (OB_FAIL(SMART_CALL(extract_column_exprs_with_op_check(raw_expr->get_param_expr(i),
                                                                column_exprs,
                                                                only_monotonic_op,
                                                                level + 1)))) {
      }
    }
  }
  return ret;
}


int ObOptEstUtils::is_range_expr(const ObRawExpr *qual, bool &is_simple_filter)
{
  int ret = OB_SUCCESS;
  is_simple_filter = true;
  if (OB_ISNULL(qual)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (IS_RANGE_CMP_OP(qual->get_expr_type()) ||
             T_OP_BTW == qual->get_expr_type() ||
             T_OP_NOT_BTW == qual->get_expr_type()) {
    // c1 > 1 , 1 < c1, c1 (not) between 1 and '2'
    const ObRawExpr *var = NULL;
    const ObRawExpr *const_expr1 = NULL;
    const ObRawExpr *const_expr2 = NULL;
    ObItemType dummy = T_INVALID;
    if (OB_FAIL(extract_var_op_const(qual, var, const_expr1, const_expr2, dummy, is_simple_filter))) {
    } else if (!is_simple_filter) {
      // do nothing
    } else if (OB_FAIL(ObOptimizerUtil::get_expr_without_lossless_cast(var, var))) {
    } else if (!var->is_column_ref_expr()) {
      is_simple_filter = false;
    }
  } else if (T_OP_AND == qual->get_expr_type() || T_OP_OR == qual->get_expr_type()) {
    const ObOpRawExpr *op_expr = static_cast<const ObOpRawExpr *>(qual);
    for (int idx = 0 ; idx < op_expr->get_param_count() && is_simple_filter && OB_SUCC(ret); ++idx) {
      if (OB_FAIL(is_range_expr(op_expr->get_param_expr(idx), is_simple_filter))) {
      }
    }
  } else {
    is_simple_filter = false;
  }
  return ret;
}

int ObOptEstUtils::extract_var_op_const(const ObRawExpr *qual,
                                        const ObRawExpr *&var_expr,
                                        const ObRawExpr *&const_expr1,
                                        const ObRawExpr *&const_expr2,
                                        ObItemType &type,
                                        bool &is_valid)
{
  int ret = OB_SUCCESS;
  type = T_INVALID;
  is_valid = false;
  var_expr = NULL;
  const_expr1 = NULL;
  const_expr2 = NULL;
  if (OB_ISNULL(qual)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (FALSE_IT(type = qual->get_expr_type())) {
  } else if (IS_RANGE_CMP_OP(type) || T_OP_EQ == type || T_OP_NSEQ == type || T_OP_NE == type) {
    if (OB_UNLIKELY(qual->get_param_count() != 2) ||
        OB_ISNULL(qual->get_param_expr(0)) ||
        OB_ISNULL(qual->get_param_expr(1))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected param", KPC(qual));
    } else if (!qual->get_param_expr(0)->is_const_expr() &&
               qual->get_param_expr(1)->is_const_expr()) {
      var_expr = qual->get_param_expr(0);
      const_expr1 = qual->get_param_expr(1);
      is_valid = true;
    } else if (!qual->get_param_expr(1)->is_const_expr() &&
               qual->get_param_expr(0)->is_const_expr()) {
      var_expr = qual->get_param_expr(1);
      const_expr1 = qual->get_param_expr(0);
      is_valid = true;
      type = get_opposite_compare_type(type);
    }
  } else if (T_OP_IS == type || T_OP_IS_NOT == type) {
    if (OB_UNLIKELY(qual->get_param_count() != 2) ||
        OB_ISNULL(qual->get_param_expr(0)) ||
        OB_ISNULL(qual->get_param_expr(1))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected param", KPC(qual));
    } else if (!qual->get_param_expr(0)->is_const_expr() &&
               qual->get_param_expr(1)->is_const_expr()) {
      var_expr = qual->get_param_expr(0);
      const_expr1 = qual->get_param_expr(1);
      is_valid = true;
    }
  } else if (T_OP_BTW == type || T_OP_NOT_BTW == type) {
    if (OB_UNLIKELY(3 != qual->get_param_count()) || OB_ISNULL(qual->get_param_expr(0)) ||
        OB_ISNULL(qual->get_param_expr(1)) || OB_ISNULL(qual->get_param_expr(2))) {
      ret = OB_ERR_UNEXPECTED;
    } else if (!qual->get_param_expr(0)->is_const_expr() &&
               qual->get_param_expr(1)->is_const_expr() &&
               qual->get_param_expr(2)->is_const_expr()) {
      var_expr = qual->get_param_expr(0);
      const_expr1 = qual->get_param_expr(1);
      const_expr2 = qual->get_param_expr(2);
      is_valid = true;
    }
  }
  return ret;
}


bool ObOptEstUtils::is_calculable_expr(const ObRawExpr &expr, const int64_t param_count)
{
  UNUSED(param_count);
  return expr.is_static_const_expr();
}

int ObOptEstUtils::get_expr_value(const ParamStore *params,
                                  const ObRawExpr &expr,
                                  ObExecContext *exec_ctx,
                                  ObIAllocator &allocator,
                                  bool &get_value,
                                  DatumValue &value)
{
  int ret = OB_SUCCESS;
  get_value = false;
  if (OB_ISNULL(params)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (is_calculable_expr(expr, params->count())) {
    if (OB_FAIL(ObSQLUtils::calc_const_or_calculable_expr(exec_ctx,
                                                          &expr,
                                                          value,
                                                          get_value,
                                                          allocator))) {
    }
  } else { }//do nothing
  return ret;
}

int ObOptEstUtils::if_expr_value_null(const ParamStore *params,
                                      const ObRawExpr &expr,
                                      ObExecContext *exec_ctx,
                                      ObIAllocator &allocator,
                                      bool &is_null)
{
  int ret = OB_SUCCESS;
  is_null = false;
  bool get_value = false;
  DatumValue value;
  if (OB_ISNULL(params)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_expr_value(params, expr, exec_ctx,
                                    allocator, get_value, value))) {
  } else if (get_value) {
    is_null = value.is_null();
  } else { }//do nothing
  return ret;
}

int ObOptEstUtils::if_expr_start_with_patten_sign(const ParamStore *params,
                                                  const ObRawExpr *expr,
                                                  const ObRawExpr *esp_expr,
                                                  ObExecContext *exec_ctx,
                                                  ObIAllocator &allocator,
                                                  bool &is_start_with,
                                                  bool &all_is_percent_sign)
{
  int ret = OB_SUCCESS;
  is_start_with = false;
  all_is_percent_sign = false;
  bool get_value = false;
  bool valid_escape = true;
  char escape;
  DatumValue value;
  DatumValue esp_value;
  if (OB_ISNULL(params) || OB_ISNULL(expr) || OB_ISNULL(esp_expr)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(get_expr_value(params, *esp_expr, exec_ctx,
                                    allocator, get_value, esp_value))) {
  } else if (!get_value || !esp_value.is_string_type()) {
    // do nothing
  } else {
    size_t escape_length = ObCharset::strlen_char(esp_expr->get_collation_type(),
                                                  esp_value.get_string().ptr(),
                                                  esp_value.get_string().length());
    int32_t escape_wc = 0;
    if (1 != escape_length) {
      valid_escape = false;
    } else if (OB_FAIL(ObCharset::mb_wc(esp_expr->get_collation_type(), esp_value.get_string(), escape_wc))) {
      ret = OB_SUCCESS;
      valid_escape = false;
    }
    if (OB_FAIL(get_expr_value(params, *expr, exec_ctx, allocator, get_value, value))) {
    } else if (get_value && value.is_string_type() && value.get_string().length() > 0) {
      // 1. patten not start with `escape sign`
      // 2. patten start with `%` or `_` && `%` or `_` is not `escape sign`
      ObStringScanner scanner(value.get_string(), expr->get_collation_type());
      ObString encoding;
      int32_t wc = 0;
      ObString first_c;
      bool is_first_char = true;
      all_is_percent_sign = true;
      while (OB_SUCC(ret)
            && scanner.next_character(encoding, wc, ret)
            && all_is_percent_sign) {
        if (is_first_char) {
          bool is_wild = (static_cast<int32_t>('%') == wc || static_cast<int32_t>('_') == wc);
          if (!valid_escape) {
            is_start_with = is_wild;
          } else {
            is_start_with = (escape_wc != wc && is_wild);
          }
          is_first_char = false;
        }
        if (static_cast<int32_t>('%') != wc) {
          all_is_percent_sign = false;
        }
      }
      if (OB_FAIL(ret)) {
        ret = OB_SUCCESS;
        all_is_percent_sign = false;
      }
    } else { /* do nothing */ }
  }
  return ret;
}

int ObOptEstUtils::if_expr_value_equal(ObOptimizerContext &opt_ctx,
                                       const ObDMLStmt *stmt,
                                       const ObRawExpr &first_expr,
                                       const ObRawExpr &second_expr,
                                       const bool null_safe,
                                       bool &equal)
{
  int ret = OB_SUCCESS;
  equal = false;
  bool get_first = false;
  bool get_second = false;
  DatumValue first_value;
  DatumValue second_value;
  const ParamStore *params = opt_ctx.get_params();
  ObSQLSessionInfo *session = opt_ctx.get_session_info();
  ObIAllocator &allocator = opt_ctx.get_allocator();
  ObExecContext *exec_ctx = opt_ctx.get_exec_ctx();
  CK( OB_NOT_NULL(exec_ctx),
      OB_NOT_NULL(params),
      OB_NOT_NULL(stmt));
  if (OB_FAIL(get_expr_value(
                params,
                first_expr,
                exec_ctx,
                allocator,
                get_first, first_value))) {
  } else if (!get_first) {
    equal = false;
  } else if (OB_FAIL(get_expr_value(params, second_expr,
                                    exec_ctx, allocator,
                                    get_second, second_value))) {
  } else if (!get_second) {
    equal = false;
  } else if (first_value.is_null() && second_value.is_null() && null_safe) {
    equal = true;
  } else if (first_value.is_null() || second_value.is_null()) {
    equal = false;
  } else if (first_value.is_min_value() || second_value.is_min_value()) {
    equal = (first_value.is_min_value() && second_value.is_min_value());
  } else if (first_value.is_max_value() || second_value.is_max_value()) {
    equal = (first_value.is_max_value() && second_value.is_max_value());
  } else if (can_compare_datum_values(first_value, second_value)
             && first_value.get_collation_type() == second_value.get_collation_type()) {
    ret = datum_values_equal(first_value, second_value, equal);
  } else {
    //When type of value or collation_type is different.We need to use
    //ObExprEqual to check whether values equal.
    //(When realize histogram, we need to considering this in more cases.)
    //'a' = 'A' with general_ci collation, true
    //'a' = 'A' without general_ci collation, false
    //'1' = 1, true
    //'a' = 0, true
    //'a' = 1, false
    ObExprCtx expr_ctx;
    //As this function may called in places that we don't have op expr, but want to check
    //whether the value of cacluable expr is equal. So we calc result type here.
    ObExprTypeCtx type_ctx;
//    type_ctx.my_session_ = exec_ctx->get_my_session();
    ObSQLUtils::init_type_ctx(session, type_ctx);

    ObExprEqual equal_op(allocator);
    ObExprResType result_type;
    ObExprResType first_type = first_expr.get_result_type();
    ObExprResType second_type = second_expr.get_result_type();
    ObOpRawExpr equal_expr(const_cast<ObRawExpr *>(&first_expr), const_cast<ObRawExpr *>(&second_expr), T_OP_EQ);
    type_ctx.set_raw_expr(&equal_expr);
    equal_op.set_raw_expr(&equal_expr);
    if (OB_FAIL(ObSQLUtils::wrap_expr_ctx(stmt->get_stmt_type(), *exec_ctx, allocator, expr_ctx))) {
    } else if (OB_FAIL(equal_op.calc_result_type2(result_type, first_type, second_type, type_ctx))) {
    } else {
      //cast_mode is CM_WARN_ON_FAIL in select_stmt||explain_stmt||not_strict_sql_mode
      //CM_WARN_ON_FAIL would cast 'a' to 0 without report error.
      //CM_NONE, the cast 'a' to int would return error.
      //Here we just use CM_WARN_ON_FAIL, as if CM_NONE, exectution would report the error.
      EXPR_DEFINE_CAST_CTX(expr_ctx, CM_WARN_ON_FAIL);
      if (OB_FAIL(DatumValueComparator::equal(first_value, second_value, cast_ctx,
                         result_type.get_type(), result_type.get_collation_type(), equal))) {
        ret = OB_SUCCESS;
      }
    }

  }
  return ret;
}


int ObOptEstObjToScalar::convert_obj_to_scalar(const DatumValue *obj, double &scalar)
{
  int ret = OB_SUCCESS;
  scalar = 0.0;
  ObDateSqlMode date_sql_mode;
  date_sql_mode.allow_invalid_dates_ = true;
  int32_t date = 0;
  int64_t datetime = 0;

  if (NULL == obj) {
    //NULL obj means a double 0.0 as scalar to return
  } else if (obj->is_null()) {
    scalar = 0.0;
  } else {
    switch (obj->get_type()) {
    case ObNullType:
        scalar = 0;
        break;
    case ObTinyIntType:              // int8, aka mysql boolean type
        scalar = static_cast<double>(static_cast<int8_t>(obj->get_int()));
        break;
    case ObSmallIntType:               // int16
        scalar = static_cast<double>(static_cast<int16_t>(obj->get_int()));
        break;
    case ObMediumIntType:              // int24
        scalar = static_cast<double>(static_cast<int32_t>(obj->get_int()));
        break;
    case ObInt32Type:                 // int32
        scalar = static_cast<double>(obj->get_int32());
        break;
    case ObIntType:                    // int64, aka bigint
        scalar = static_cast<double>(obj->get_int());
        break;
    case ObUTinyIntType:                // uint8
        scalar = static_cast<double>(static_cast<uint8_t>(obj->get_uint64()));
        break;
    case ObUSmallIntType:               // uint16
        scalar = static_cast<double>(static_cast<uint16_t>(obj->get_uint64()));
        break;
    case ObUMediumIntType:              // uint24
        scalar = static_cast<double>(static_cast<uint32_t>(obj->get_uint64()));
        break;
    case ObUInt32Type:                    // uint32
        scalar = static_cast<double>(static_cast<uint32_t>(obj->get_uint64()));
        break;
    case ObUInt64Type:                 // uint64
        scalar = static_cast<double>(obj->get_uint64());
        break;
    case ObFloatType:                  // single-precision floating point
        scalar = static_cast<double>(obj->get_float());
        break;
    case ObDoubleType:                 // double-precision floating point
        scalar = obj->get_double();
        break;
    case ObUFloatType:            // unsigned single-precision floating point
        scalar = static_cast<double>(obj->get_float());
        break;
    case ObUDoubleType:           // unsigned double-precision floating point
        scalar = static_cast<double>(obj->get_double());
        break;
    case ObNumberType:
    case ObUNumberType:
    case ObDecimalIntType:
        // aka decimal/numeric, already converted to double in convert_obj_to_double
        scalar = static_cast<double>(obj->get_double());
        break;
    case ObDateTimeType:
    case ObTimestampType:
        scalar = static_cast<double>(obj->get_datetime());
        break;
    case ObDateType:
        scalar = static_cast<double>(obj->get_date());
        break;
    case ObTimeType:
        scalar = static_cast<double>(obj->get_time());
        break;
    case ObYearType:
        scalar = static_cast<double>(obj->get_year());
        break;
    // TODO@hanhui text share with varchar temporarily
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType:
    case ObVarcharType: {  // charset: utf-8, collation: utf8_general_ci
        const ObString &str = obj->get_varchar();
        ret = convert_string_to_scalar(obj->get_collation_type(), str, scalar);
        break;
    }
    case ObCharType: {    // charset: utf-8, collation: utf8_general_ci
        const ObString &str = obj->get_string();
        ret = convert_string_to_scalar(obj->get_collation_type(), str, scalar);
        break;
    }
    case ObHexStringType: {
        const ObString &str = obj->get_string();
        ret = convert_string_to_scalar(obj->get_collation_type(), str, scalar);
        break;
    }
    case ObMySQLDateType:
        ObTimeConverter::mdate_to_date(obj->get_mysql_date(), date, date_sql_mode);
        scalar = static_cast<double>(date);
        break;
    case ObMySQLDateTimeType:
        ObTimeConverter::mdatetime_to_datetime(obj->get_mysql_datetime(), datetime, date_sql_mode);
        scalar = static_cast<double>(datetime);
        break;
    case ObExtendType:                 // Min, Max, NOP etc.
    case ObUnknownType:                // For question mark(?) in prepared statement, no need to serialize
      //TODO:
        break;
    default:
        break;
    }
  }

  return ret;
}

int ObOptEstObjToScalar::convert_obj_to_double(const DatumValue *obj, double &num)
{
  int ret = OB_SUCCESS;
  num = 0.0;
  if (OB_ISNULL(obj)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (obj->is_null()) {
    num = 0.0;
  } else if (ObNumberType == obj->get_type() || ObUNumberType == obj->get_type()
             || ObDecimalIntType == obj->get_type()) {
    ObArenaAllocator calc_buffer(ObModIds::OB_BUFFER);
    // tz_info is UNUSED in converting number to double
    ObCastCtx cast_ctx(&calc_buffer, NULL, CM_NONE, obj->get_collation_type());
    DatumValue converted;
    if (OB_FAIL(DatumValueCaster::to_type(ObDoubleType, cast_ctx, *obj, calc_buffer, converted))) {
    } else {
      num = converted.get_double();
    }
  } else if (OB_FAIL(convert_obj_to_scalar(obj, num))) {
  }
  return ret;
}
int ObOptEstObjToScalar::convert_values_to_scalars_impl(
    const DatumValue *min,
    const DatumValue *max,
    const DatumValue *start,
    const DatumValue *end,
    DatumValue *min_out,
    DatumValue *max_out,
    DatumValue *start_out,
    DatumValue *end_out,
    ObIAllocator &owner,
    bool convert2sortkey)
{
  int ret = OB_SUCCESS;
  const static int64_t START_POS = 0;
  const static int64_t END_POS = 1;
  const static int64_t MIN_POS = 2;
  const static int64_t MAX_POS = 3;
  const static int64_t OBJ_COUNT = 4;
  int64_t skip_count = 0;
  const DatumValue *input_ptrs[OBJ_COUNT] = {start, end, min, max};
  DatumValue *output_ptrs[OBJ_COUNT] = {start_out, end_out, min_out, max_out};
  ObSEArray<double, 4> string_scalars;
  //this map is for recording which obj is converted using new method
  uint64_t str_conv_map = 0;
  if (OB_ISNULL(start) || OB_ISNULL(end) || OB_ISNULL(start_out) || OB_ISNULL(end_out)) {
    ret = OB_INVALID_ARGUMENT;
  } else if ((NULL == min) != (NULL == max)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (((NULL == min) != (NULL == min_out))
      || ((NULL == max) != (NULL == max_out))) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    bool with_min_max = (NULL != min);
    //check whether to use string conversion method : all string except for min / max
    const bool null_first_check = start->is_null(); // for mysql
    bool use_dynamic_base = (start->is_string_type() || start->is_min_value() || start->is_max_value() || null_first_check)
        && (end->is_string_type() || end->is_min_value() || end->is_max_value());
    if (use_dynamic_base && with_min_max) {
      use_dynamic_base &= ((min->is_string_type() || min->is_min_value() || min->is_max_value()));
      use_dynamic_base &= ((max->is_string_type() || max->is_min_value() || max->is_max_value()));
    }
    if (use_dynamic_base) {
      //Special case for All String : truncate common header and use dynamic base
      ObString str;
      ObSEArray<ObString, 4> strs;
      ObSEArray<ObCollationType, 4> cs_type;
      if (start->is_string_type()
          && OB_FAIL(add_to_string_conversion_array(*start, cs_type, strs, str_conv_map, START_POS))) {
      } else if (end->is_string_type()
          && OB_FAIL(add_to_string_conversion_array(*end, cs_type, strs, str_conv_map, END_POS))) {
      } else if (with_min_max) {
        if (min->is_string_type()
            && OB_FAIL(add_to_string_conversion_array(*min, cs_type, strs, str_conv_map, MIN_POS))) {
        } else if (max->is_string_type()
            && OB_FAIL(add_to_string_conversion_array(*max, cs_type, strs, str_conv_map, MAX_POS))) {
        } else {
          //do nothing
        }
      }
      if (OB_SUCC(ret)) {
        if (strs.count() > 0) {
          if (!convert2sortkey) {
            for (int64_t i = 0; i < cs_type.count(); i ++) {
              cs_type.at(i) = CS_TYPE_BINARY;
            }
          }
          if (OB_FAIL(convert_strings_to_scalar(cs_type, strs, string_scalars))) {
          } else if (string_scalars.count() != strs.count()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("Scalars and strings not match",
                K(string_scalars.count()), K(strs.count()), K(ret));
          }
        }
      }
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < OBJ_COUNT; ++i) {
      DatumValue *out_ptr = output_ptrs[i];
      const DatumValue *in_ptr = input_ptrs[i];
      if ((START_POS == i || END_POS == i) && (OB_ISNULL(in_ptr) || OB_ISNULL(out_ptr))) {
        ret = OB_ERR_UNEXPECTED;
      } else if ((MIN_POS == i || MAX_POS == i) && ((NULL == in_ptr) != (NULL == out_ptr))) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        if (str_conv_map & (0x1 << i)) {
          //this obj is already converted using string special method:
          ret = out_ptr->set_double(string_scalars.at(i - skip_count), owner);
        } else {
          //this obj is to be converted using normal method:
          ++skip_count;
          if (NULL != out_ptr && NULL != in_ptr) {
            ret = convert_value_to_scalar(in_ptr, out_ptr, owner);
          }
        }
      }
    }
    if (start->is_null() && !end->is_null()) {
      start_out->set_min_value();//TODO temporarily set this to min value but this is not quite right,
      // behind needs stronger distinction capability, can distinguish whether it contains NULL, calculate NULL sel differently
    }
  }
  return ret;
}
int ObOptEstObjToScalar::convert_values_to_scalars(const DatumValue *minimum,
    const DatumValue *maximum, const DatumValue *start, const DatumValue *end,
    DatumValue *minimum_out, DatumValue *maximum_out,
    DatumValue *start_out, DatumValue *end_out, ObIAllocator &owner,
    bool convert_to_sort_key)
{
  return convert_values_to_scalars_impl(minimum, maximum, start, end,
      minimum_out, maximum_out, start_out, end_out, owner, convert_to_sort_key);
}
int ObOptEstObjToScalar::convert_value_to_scalar(const DatumValue *value,
    DatumValue *output, ObIAllocator &owner)
{
  int ret = OB_SUCCESS;
  if (value == nullptr || output == nullptr) {
    ret = OB_INVALID_ARGUMENT;
  } else if (value->is_ext() || value->is_unknown()) {
    ret = clone_typed_value(*value, *output, owner);
  } else if (!value->is_null() && (value->is_number() || value->is_unumber() || value->is_decimal_int())) {
    ObArenaAllocator scratch(ObModIds::OB_BUFFER);
    ObCastCtx context(&scratch, nullptr, CM_NONE, value->get_collation_type());
    DatumValue converted;
    if (OB_FAIL(DatumValueCaster::to_type(ObDoubleType, context, *value, scratch, converted))) {
      if (ret == OB_DATA_OUT_OF_RANGE) {
        const bool negative = value->is_decimal_int()
            ? value->get_int_bytes() > 0 && wide::is_negative(value->get_decimal_int(), value->get_int_bytes())
            : value->get_number().is_negative();
        if (negative) { output->set_min_value(); } else { output->set_max_value(); }
      }
    } else {
      ret = output->set_double(converted.get_double(), owner);
    }
  } else {
    double scalar = 0.0;
    if (OB_FAIL(convert_obj_to_scalar(value, scalar))) {
    } else {
      ret = output->set_double(scalar, owner);
    }
  }
  return ret;
}

int ObOptEstObjToScalar::add_to_string_conversion_array(
    const DatumValue &strobj,
    common::ObIArray<ObCollationType> &cs_type,
    ObIArray<common::ObString> &arr,
    uint64_t &convertable_map,
    int64_t pos)
{
  int ret = OB_SUCCESS;
  ObString str;
  if (!strobj.is_string_type()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (convertable_map & (0x1 << pos)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(strobj.get_string(str))) {
  } else if (OB_FAIL(arr.push_back(str))) {
  } else if (OB_FAIL(cs_type.push_back(strobj.get_collation_type()))) {
  } else {
    convertable_map |= (0x1 << pos);
  }
  return ret;
}

int ObOptEstObjToScalar::convert_strings_to_scalar(
    const common::ObIArray<ObCollationType> &cs_type,
    const common::ObIArray<common::ObString> &origin_strs,
    common::ObIArray<double> &scalars)
{
  int ret = OB_SUCCESS;
  ObString str;
  double base = 256.0;
  uint8_t offset = 0;
  int64_t common_prefix_length = 0;
  ObArenaAllocator tmp_alloc("ObOptEstUtils");
  common::ObSEArray<common::ObString, 4> sort_keys;
  if (OB_UNLIKELY(origin_strs.count() != cs_type.count())) {
    ret = OB_ERR_UNEXPECTED;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < origin_strs.count(); i ++)
  {
    ObString *sort_key = sort_keys.alloc_place_holder();
    if (OB_ISNULL(sort_key)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(get_string_sort_key(tmp_alloc, cs_type.at(i), origin_strs.at(i), *sort_key))) {
    }
  }
  if (FAILEDx(find_common_prefix_len(sort_keys, common_prefix_length))) {
  } else if (OB_FAIL(find_string_scalar_offset_base(sort_keys, common_prefix_length, offset, base))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < origin_strs.count(); ++i) {
      double scalar = convert_string_to_scalar(sort_keys.at(i),
                                               common_prefix_length,
                                               offset,
                                               base);
      if (OB_FAIL(scalars.push_back(scalar))) {
      }
    }
  }
  return ret;
}

int ObOptEstObjToScalar::find_common_prefix_len(
    const ObIArray<ObString> &strs,
    int64_t &length)
{
  int ret = OB_SUCCESS;
  length = 0;
  if (strs.count() == 0) {
    length = 0;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < strs.count(); ++i) {
      const ObString &str = strs.at(i);
      if (str.length() < 0
          || (str.length() > 0 && str.ptr() == NULL)) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    if (OB_SUCC(ret)) {
      int64_t i = 0;
      bool found = false;
      while (OB_SUCC(ret) && !found) {
        char target_char = '\0';
        for (int64_t stri = 0; !found && stri < strs.count(); ++stri) {
          const ObString &str = strs.at(stri);
          if (str.length() == i) {
            found = true;
            length = i;
            //end of one string, common prefix = str[0 : i - 1], len = i
          } else {
            if (0 == stri) {
              target_char = str[i];
            } else {
              if (str[i] == target_char) {
                //same char on this posision, check next str
              } else {
                found = true;
                length = i;
                //different char found, common prefix = str[0 : i - 1], len = i
              }
            }
          }
        }
        ++i;
      }
    }
  }
  return ret;
}

int ObOptEstObjToScalar::find_string_scalar_offset_base(
    const ObIArray<ObString> &strs,
    int64_t prefix_len,
    uint8_t &offset,
    double &base)
{
  int ret = OB_SUCCESS;
  if (prefix_len < 0) {
    ret = OB_INVALID_ARGUMENT;
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < strs.count(); ++i) {
    const ObString &str = strs.at(i);
    if (str.length() < 0
        || (str.length() > 0 && str.ptr() == NULL)) {
      ret = OB_INVALID_ARGUMENT;
    }
  }
  if (OB_SUCC(ret)) {
    uint8_t min = UINT8_MAX;
    uint8_t max = 0;
    for (int64_t stri = 0; OB_SUCC(ret) && stri < strs.count(); ++stri) {
      const ObString &str = strs.at(stri);
      //start from the char after common prefix, find min max of all bytes of all strs
      for (int64_t i = prefix_len; i < str.length(); ++i) {
        if (isdigit(str[i])) {
          expand_range(min, max, '0', '9');
        } else if (islower(str[i])) {
          expand_range(min, max, 'a', 'z');
        } else if (isupper(str[i])) {
          expand_range(min, max, 'A', 'Z');
        } else {
          expand_range(min, max, str[i], str[i]);
        }
      }
    }
    if (max == min || (UINT8_MAX == min && 0 == max)) {
      //if no char processed, or only one non-digit non-upper-or-lower char processed,
      // use the full byte range when a dynamic base cannot be derived
      offset = 0;
      base = 256;
    } else {
      offset = min;
      base = static_cast<double>(max - min + 1);
    }
  }
  return ret;
}

int ObOptEstObjToScalar::get_string_sort_key(ObIAllocator &alloc, ObCollationType cs_type,
                                             const common::ObString &str, common::ObString &sort_key)
{
  int ret = OB_SUCCESS;
  const ObCharsetInfo *cs = ObCharset::get_charset(cs_type);
  if (ObCharset::is_bin_sort(cs_type) || str.empty() ||
      NULL == cs || NULL == cs->coll) {
    sort_key = str;
  } else {
    size_t buf_len = cs->coll->strnxfrmlen(cs, str.length()) * cs->mbmaxlen;
    ObArrayWrap<char> buffer;
    bool is_valid_character = false;
    if (OB_FAIL(buffer.allocate_array(alloc, buf_len))) {
    } else {
      size_t sort_key_len = ObCharset::sortkey(cs_type, str.ptr(), str.length(),
                                               buffer.get_data(), buf_len, is_valid_character);
      sort_key.assign_ptr(buffer.get_data(), static_cast<ObString::obstr_size_t>(sort_key_len));
    }
  }
  return ret;
}

int ObOptEstObjToScalar::convert_string_to_scalar(ObCollationType cs_type, const common::ObString &str, double &scalar)
{
  int ret = OB_SUCCESS;
  ObString sort_key;
  ObArenaAllocator tmp_alloc("ObOptEstUtils");
  if (OB_FAIL(get_string_sort_key(tmp_alloc, cs_type, str, sort_key))) {
  } else {
    scalar = convert_string_to_scalar(sort_key);
  }
  return ret;
}

double ObOptEstObjToScalar::convert_string_to_scalar(
    const common::ObString &str,
    int64_t prefix_len,
    uint8_t offset,
    double base)
{
  if (prefix_len < 0) {
    prefix_len = 0;
  }
  if (fabs(base - 0.0) < OB_DOUBLE_EPSINON) {
    //base is 0, fallback to base 256
    base = 256.0;
    offset = 0;
  }
  double scalar = 0;
  double weight = base;
  const char* ptr = str.ptr();
  for (int64_t i = prefix_len; i < str.length(); ++i) {
    scalar += ((uint8_t)ptr[i] - offset) / weight;
    weight *= base;
  }
  return scalar;
}

int ObOptEstObjToScalar::convert_string_to_scalar_for_number(
    const common::ObString &str, double &scalar)
{
  int ret = OB_SUCCESS;
  scalar = 0;
  if (NULL != str.ptr()) {
    if (1 != sscanf(str.ptr(), "%lf", &scalar)) {
    	ret = OB_INVALID_DATA;
    } else { /* do nothing*/ }
  }
  return ret;
}

}//end of namespace sql
}//end of namespace oceanbase
