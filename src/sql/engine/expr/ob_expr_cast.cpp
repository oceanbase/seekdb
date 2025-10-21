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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_cast.h"
#include "lib/geo/ob_geometry_cast.h"
#include "sql/engine/expr/ob_expr_subquery_ref.h"
#include "sql/engine/subquery/ob_subplan_filter_op.h"
#include "pl/ob_pl_resolver.h"
#include "sql/engine/expr/vector_cast/vector_cast.h"

// from sql_parser_base.h
#define DEFAULT_STR_LENGTH -1

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprCast::ObExprCast(ObIAllocator &alloc)
    : ObFuncExprOperator::ObFuncExprOperator(alloc, T_FUN_SYS_CAST,
                                             N_CAST,
                                             2,
                                             VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
  extra_serialize_ = 0;
  disable_operand_auto_cast();
}

int ObExprCast::get_cast_inttc_len(ObExprResType &type1,
                                   ObExprResType &type2,
                                   ObExprTypeCtx &type_ctx,
                                   int32_t &res_len,
                                   int16_t &length_semantics,
                                   ObCollationType conn,
                                   ObCastMode cast_mode) const
{
  int ret = OB_SUCCESS;
  if (type1.is_literal()) { // literal
    if (ObStringTC == type1.get_type_class()) {
      res_len = type1.get_accuracy().get_length();
      length_semantics = type1.get_length_semantics();
    } else if (OB_FAIL(ObField::get_field_mb_length(type1.get_type(),
        type1.get_accuracy(), type1.get_collation_type(), res_len))) {
      LOG_WARN("failed to get filed mb length");
    }
  } else {
    res_len = CAST_STRING_DEFUALT_LENGTH[type1.get_type()];
    ObObjTypeClass tc1 = type1.get_type_class();
    int16_t scale = type1.get_accuracy().get_scale();
    if (ObDoubleTC == tc1) {
      res_len -= 1;
    } else if ((ObDateTimeTC == tc1 || ObMySQLDateTimeTC == tc1) && scale > 0) {
      res_len += scale - 1;
    } else if (OB_FAIL(get_cast_string_len(type1, type2, type_ctx, res_len, length_semantics, conn, cast_mode))) {
      LOG_WARN("fail to get cast string length", K(ret));
    } else {
      // do nothing
    }
  }
  return ret;
}


// @res_len: the result length of any type be cast to string
// for column type
// such as c1(int),  cast(c1 as binary), the result length is 11.
int ObExprCast::get_cast_string_len(ObExprResType &type1,
                                    ObExprResType &type2,
                                    ObExprTypeCtx &type_ctx,
                                    int32_t &res_len,
                                    int16_t &length_semantics,
                                    ObCollationType conn,
                                    ObCastMode cast_mode) const
{
  int ret = OB_SUCCESS;
  const ObObj &val = type1.get_param();
  if (!type1.is_literal()) { // column
    res_len = CAST_STRING_DEFUALT_LENGTH[type1.get_type()];
    int16_t prec = type1.get_accuracy().get_precision();
    int16_t scale = type1.get_accuracy().get_scale();
    switch(type1.get_type()) {
    case ObTinyIntType:
    case ObSmallIntType:
    case ObMediumIntType:
    case ObInt32Type:
    case ObIntType:
    case ObUTinyIntType:
    case ObUSmallIntType:
    case ObUMediumIntType:
    case ObUInt32Type:
    case ObUInt64Type: {
        int32_t prec = static_cast<int32_t>(type1.get_accuracy().get_precision());
        res_len = prec > res_len ? prec : res_len;
        break;
      }
    case ObNumberType:
    case ObUNumberType:
    case ObDecimalIntType: {
        if (0 < prec) {
          if (0 < scale) {
            res_len =  prec + 2;
          } else {
            res_len = prec + 1;
          }
        }
        break;
      }
    case ObDateTimeType:
    case ObMySQLDateTimeType:
    case ObTimestampType: {
        if (scale > 0) {
          res_len += scale + 1;
        }
        break;
      }
    case ObTimeType: {
        if (scale > 0) {
          res_len += scale + 1;
        }
        break;
      }
    // TODO@hanhui text share with varchar temporarily
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType:
    case ObVarcharType:
    case ObCharType:
    case ObHexStringType:
    case ObEnumType:
    case ObSetType:
    case ObEnumInnerType:
    case ObSetInnerType:
    case ObJsonType:
    case ObGeometryType: {
      res_len = type1.get_length();
      length_semantics = type1.get_length_semantics();
      break;
    }
    case ObBitType: {
      if (scale > 0) {
        res_len = scale;
      }
      res_len = (res_len + 7) / 8;
      break;
    }
    default: {
        break;
      }
    }
  } else if (type1.is_null()) {
    res_len = 0;//compatible with mysql;
  } else if (OB_ISNULL(type_ctx.get_session())) {
    // calc type don't set ret, just print the log. by design.
    LOG_WARN("my_session is null");
  } else { // literal
    ObArenaAllocator oballocator(ObModIds::BLOCK_ALLOC);
    ObCollationType cast_coll_type = (CS_TYPE_INVALID != type2.get_collation_type())
        ? type2.get_collation_type()
        : conn;
    ObDataTypeCastParams dtc_params;
    ObTimeZoneInfoWrap tz_wrap;
    bool is_valid = false;
    ObRawExpr *raw_expr = NULL;
    if (OB_ISNULL(raw_expr = type_ctx.get_raw_expr())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null", K(ret));
    } else {
      ObCastCtx cast_ctx(&oballocator,
                        &type_ctx.get_dtc_params(),
                        0,
                        cast_mode,
                        cast_coll_type);
      ObString val_str;
      EXPR_GET_VARCHAR_V2(val, val_str);
      // Here the len is set to the number of characters
      if (OB_SUCC(ret) && NULL != val_str.ptr()) {
        int32_t len_byte = val_str.length();
        res_len = len_byte;
        length_semantics = LS_CHAR;
        if (NULL != val_str.ptr()) {
          int32_t trunc_len_byte = static_cast<int32_t>(ObCharset::strlen_byte_no_sp(cast_coll_type,
              val_str.ptr(), len_byte));
          res_len = static_cast<int32_t>(ObCharset::strlen_char(cast_coll_type,
              val_str.ptr(), trunc_len_byte));
        }
        if (type1.is_numeric_type() && !type1.is_integer_type()) {
          res_len += 1;
        }
      }
    }
  }
  return ret;
}

// this is only for engine 3.0. old engine will get cast mode from expr_ctx.
// only for explicit cast, implicit cast's cm is setup while deduce type(in type_ctx.cast_mode_)
int ObExprCast::get_explicit_cast_cm(const ObExprResType &src_type,
                              const ObExprResType &dst_type,
                              const ObSQLSessionInfo &session,
                              ObSQLMode sql_mode,
                              const ObRawExpr &cast_raw_expr,
                              ObCastMode &cast_mode) const
{
  int ret = OB_SUCCESS;
  cast_mode = CM_NONE;
  const bool is_explicit_cast = CM_IS_EXPLICIT_CAST(cast_raw_expr.get_cast_mode());
  const int32_t result_flag = src_type.get_result_flag();
  const ObObjTypeClass dst_tc = ob_obj_type_class(dst_type.get_type());
  const ObObjTypeClass src_tc = ob_obj_type_class(src_type.get_type());
  ObSQLUtils::get_default_cast_mode(is_explicit_cast, result_flag,
                                    session.get_stmt_type(),
                                    session.is_ignore_stmt(),
                                    sql_mode, cast_mode);
  if (ObDateTimeTC == dst_tc || ObDateTC == dst_tc || ObTimeTC == dst_tc || ObMySQLDateTC == dst_tc
      || ObMySQLDateTimeTC == dst_tc) {
    cast_mode |= CM_NULL_ON_WARN;
  } else if (ob_is_int_uint(src_tc, dst_tc)) {
    cast_mode |= CM_NO_RANGE_CHECK;
  }
  if (CM_IS_EXPLICIT_CAST(cast_mode)) {
    // CM_STRING_INTEGER_TRUNC is only for string to int cast in mysql mode
    if (ob_is_string_type(src_type.get_type()) &&
        (ob_is_int_tc(dst_type.get_type()) || ob_is_uint_tc(dst_type.get_type()))) {
      cast_mode |= CM_STRING_INTEGER_TRUNC;
    }
    if (CM_IS_EXPLICIT_CAST(cast_mode)) {
      // CM_STRING_INTEGER_TRUNC is only for string to int cast in mysql mode
      if (ob_is_string_type(src_type.get_type()) &&
          (ob_is_int_tc(dst_type.get_type()) || ob_is_uint_tc(dst_type.get_type()))) {
        cast_mode |= CM_STRING_INTEGER_TRUNC;
      }
      // select cast('1e500' as decimal);  -> max_val
      // select cast('-1e500' as decimal); -> min_val
      if (ob_is_string_type(src_type.get_type())
          && (ob_is_number_tc(dst_type.get_type()) || ob_is_decimal_int_tc(dst_type.get_type()))) {
        cast_mode |= CM_SET_MIN_IF_OVERFLOW;
      }
      if (!is_called_in_sql() && CM_IS_WARN_ON_FAIL(cast_raw_expr.get_cast_mode())) {
        cast_mode |= CM_WARN_ON_FAIL;
      }
    }
    OZ (ObRawExprUtils::wrap_cm_warn_on_fail_if_need(cast_raw_expr.get_param_expr(0), dst_type,
                                                     &session, cast_mode));
  }
  return ret;
}

bool ObExprCast::check_cast_allowed(const ObObjType orig_type,
                                    const ObCollationType orig_cs_type,
                                    const ObObjType expect_type,
                                    const ObCollationType expect_cs_type,
                                    const bool is_explicit_cast) const
{
  UNUSED(expect_cs_type);
  bool res = true;
  ObObjTypeClass ori_tc = ob_obj_type_class(orig_type);
  ObObjTypeClass expect_tc = ob_obj_type_class(expect_type);
  bool is_expect_lob_tc = (ObLobTC == expect_tc || ObTextTC == expect_tc);
  bool is_ori_lob_tc = (ObLobTC == ori_tc || ObTextTC == ori_tc);
  return res;
}

int ObExprCast::calc_result_type2(ObExprResType &type,
                                  ObExprResType &type1,
                                  ObExprResType &type2,
                                  ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObExprResType dst_type;
  ObRawExpr *cast_raw_expr = NULL;
  sql::ObSQLSessionInfo *session = NULL;
  ObExecContext *exec_ctx = NULL;
  bool is_explicit_cast = false;
  ObCollationLevel cs_level = CS_LEVEL_INVALID;
  bool enable_decimalint = false;
  if (OB_ISNULL(session = const_cast<sql::ObSQLSessionInfo*>(type_ctx.get_session())) ||
      OB_ISNULL(exec_ctx = session->get_cur_exec_ctx()) ||
      OB_ISNULL(cast_raw_expr = get_raw_expr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ptr is NULL", K(ret), KP(session), KP(cast_raw_expr));
  } else if (OB_UNLIKELY(NOT_ROW_DIMENSION != row_dimension_)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("invalid row_dimension_", K(row_dimension_), K(ret));
  } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session, enable_decimalint))) {
    LOG_WARN("fail to check_enable_decimalint", K(ret), K(session->get_effective_tenant_id()));
  } else if (OB_FAIL(get_cast_type(enable_decimalint,
                                   type2, cast_raw_expr->get_cast_mode(), type_ctx, dst_type))) {
    LOG_WARN("get cast dest type failed", K(ret));
  } else if (OB_FAIL(ObSQLUtils::get_cs_level_from_cast_mode(cast_raw_expr->get_cast_mode(),
                                                             type1.get_collation_level(),
                                                             cs_level))) {
    LOG_WARN("failed to get collation level", K(ret));
  } else if (!dst_type.is_collection_sql_type() && FALSE_IT(dst_type.set_collation_level(cs_level))) {
  } else if (OB_UNLIKELY(!cast_supported(type1.get_type(), type1.get_collation_type(),
                                        dst_type.get_type(), dst_type.get_collation_type()))) {
    if (session->is_varparams_sql_prepare()) {
      type.set_null();
      LOG_TRACE("ps prepare phase ignores type deduce error");
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("transition does not support", "src", ob_obj_type_str(type1.get_type()),
                "dst", ob_obj_type_str(dst_type.get_type()));
    }
  } else if (FALSE_IT(is_explicit_cast = CM_IS_EXPLICIT_CAST(cast_raw_expr->get_cast_mode()))) {
  // check cast supported in cast_map but not support here.
  } else if (!check_cast_allowed(type1.get_type(), type1.get_collation_type(),
                                 dst_type.get_type(), dst_type.get_collation_type(),
                                 is_explicit_cast)) {
    if (session->is_varparams_sql_prepare()) {
      type.set_null();
      LOG_TRACE("ps prepare phase ignores type deduce error");
    } else {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("explicit cast to lob type not allowed", K(ret), K(dst_type));
    }
  } else {
    // always cast to user requested type
    if (is_explicit_cast &&
        ObCharType == dst_type.get_type()) {
      // cast(x as binary(10)), in parser,binary->T_CHAR+bianry, but, result type should be varchar, so set it.
      type.set_type(ObVarcharType);
    } else if (lib::is_mysql_mode() && ObFloatType == dst_type.get_type()) {
      // Compatible with mysql. If the precision p is not specified, produces a result of type FLOAT. 
      // If p is provided and 0 <=  p <= 24, the result is of type FLOAT. If 25 <= p <= 53, 
      // the result is of type DOUBLE. If p < 0 or p > 53, an error is returned
      // however, ob use -1 as default precision, so it is a valid value
      type.set_collation_type(dst_type.get_collation_type());
      ObPrecision float_precision = dst_type.get_precision();
      ObScale float_scale = dst_type.get_scale();
      if (OB_UNLIKELY(float_scale > OB_MAX_DOUBLE_FLOAT_SCALE)) {
        ret = OB_ERR_TOO_BIG_SCALE;
        LOG_USER_ERROR(OB_ERR_TOO_BIG_SCALE, float_scale, "CAST", OB_MAX_DOUBLE_FLOAT_SCALE);
        LOG_WARN("scale of float overflow", K(ret), K(float_scale), K(float_precision));
      } else if (float_precision < -1 ||
          (SCALE_UNKNOWN_YET == float_scale && float_precision > OB_MAX_DOUBLE_FLOAT_PRECISION)) {
        ret = OB_ERR_TOO_BIG_PRECISION;
        LOG_USER_ERROR(OB_ERR_TOO_BIG_PRECISION, float_precision, "CAST", OB_MAX_DOUBLE_FLOAT_PRECISION);
      } else if (SCALE_UNKNOWN_YET == float_scale) {
        if (float_precision <= OB_MAX_FLOAT_PRECISION) {
          type.set_type(ObFloatType);
        } else {
          type.set_type(ObDoubleType);
        }
      } else {
        type.set_type(ObFloatType);
        type.set_precision(float_precision);
        type.set_scale(float_scale);
      }
    } else if (dst_type.is_collection_sql_type()) {
      type.set_type(dst_type.get_type());
      type.set_subschema_id(dst_type.get_subschema_id());
    } else if (dst_type.is_enum_set_with_subschema()) {
      type.set_type(dst_type.get_type());
      type.set_subschema_id(dst_type.get_subschema_id());
      type.set_accuracy(dst_type.get_accuracy());
    } else {
      type.set_type(dst_type.get_type());
      type.set_collation_type(dst_type.get_collation_type());
    }
    int16_t scale = dst_type.get_scale();
    if (is_explicit_cast
        && (ObTimeType == dst_type.get_type()
          || ob_is_datetime_or_mysql_datetime(dst_type.get_type()))
        && scale > 6) {
      ret = OB_ERR_TOO_BIG_PRECISION;
      LOG_USER_ERROR(OB_ERR_TOO_BIG_PRECISION, scale, "CAST", OB_MAX_DATETIME_PRECISION);
    }
    if (OB_SUCC(ret)) {
      ObCompatibilityMode compatibility_mode = get_compatibility_mode();
      ObCollationType collation_connection = type_ctx.get_coll_type();
      ObCollationType collation_nation = session->get_nls_collation_nation();
      type1.set_calc_type(get_calc_cast_type(type1.get_type(), dst_type.get_type()));
      int32_t length = 0;
      if (ob_is_string_or_lob_type(dst_type.get_type())
          || ob_is_json(dst_type.get_type())
          || ob_is_geometry(dst_type.get_type())
          || ob_is_roaringbitmap(dst_type.get_type())) {
        type.set_collation_level(dst_type.get_collation_level());
        int32_t len = dst_type.get_length();
        int16_t length_semantics = ((dst_type.is_string_or_lob_locator_type() || dst_type.is_json())
            ? dst_type.get_length_semantics()
            : (OB_NOT_NULL(type_ctx.get_session())
                ? type_ctx.get_session()->get_actual_nls_length_semantics()
                : LS_BYTE));
        if (len > 0) { // cast(1 as char(10))
          type.set_full_length(len, length_semantics);
        } else if (OB_FAIL(get_cast_string_len(type1, dst_type, type_ctx, len, length_semantics,
                                               collation_connection,
                                               cast_raw_expr->get_cast_mode()))) { // cast (1 as char)
          LOG_WARN("fail to get cast string length", K(ret));
        } else {
          type.set_full_length(len, length_semantics);
        }
        if (CS_TYPE_INVALID != dst_type.get_collation_type()) {
          // cast as binary
          type.set_collation_type(dst_type.get_collation_type());
        } else {
          // use collation of current session
          type.set_collation_type(collation_connection);
        }
      } else if (ob_is_extend(dst_type.get_type())
                 || dst_type.is_collection_sql_type()) {
        type.set_udt_id(type2.get_udt_id());
      } else {
        type.set_length(length);
        if ((ObNumberTC == dst_type.get_type_class() || ObDecimalIntTC == dst_type.get_type_class())
            && 0 == dst_type.get_precision()) {
          // MySql:cast (1 as decimal(0)) = cast(1 as decimal)
          // Oracle: cast(1.4 as number) = cast(1.4 as number(-1, -1))
          type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY2[compatibility_mode][ObNumberType].get_precision());
        } else if ((ObIntTC == dst_type.get_type_class() || ObUIntTC == dst_type.get_type_class())
                   && dst_type.get_precision() <= 0) {
          // for int or uint , the precision = len
          int32_t len = 0;
          int16_t length_semantics = LS_BYTE;//unused
          if (OB_FAIL(get_cast_inttc_len(type1, dst_type, type_ctx, len, length_semantics,
                                         collation_connection, cast_raw_expr->get_cast_mode()))) {
            LOG_WARN("fail to get cast inttc length", K(ret));
          } else {
            len = len > OB_LITERAL_MAX_INT_LEN ? OB_LITERAL_MAX_INT_LEN : len;
            type.set_precision(static_cast<int16_t>(len));
          }
        } else if (ORACLE_MODE == compatibility_mode && ObDoubleType == dst_type.get_type()) {
          ObAccuracy acc = ObAccuracy::DDL_DEFAULT_ACCURACY2[compatibility_mode][dst_type.get_type()];
          type.set_accuracy(acc);
          if (type1.is_decimal_int()) {
            acc = type1.get_accuracy();
          }
          type1.set_accuracy(acc);
        } else if (ObYearType == dst_type.get_type()) {
          ObAccuracy acc = ObAccuracy::DDL_DEFAULT_ACCURACY2[compatibility_mode][dst_type.get_type()];
          type.set_accuracy(acc);
          if (type1.is_decimal_int() && acc.precision_ < type1.get_precision()) {
            acc.precision_ = type1.get_precision();
          }
          type1.set_accuracy(acc);
        } else {
          type.set_precision(dst_type.get_precision());
        }
        type.set_scale(dst_type.get_scale());
      }
    }
    CK(OB_NOT_NULL(type_ctx.get_session()));
    if (OB_SUCC(ret)) {
      // interval expr need NOT_NULL_FLAG
      // bug: 
      calc_result_flag2(type, type1, type2);
      if (CM_IS_ADD_ZEROFILL(cast_raw_expr->get_cast_mode())) {
        type.set_result_flag(ZEROFILL_FLAG);
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObCastMode explicit_cast_cm = CM_NONE;
    if (OB_FAIL(get_explicit_cast_cm(type1, dst_type, *session,
                                     type_ctx.get_sql_mode(),
                                     *cast_raw_expr,
                                     explicit_cast_cm))) {
      LOG_WARN("set cast mode failed", K(ret));
    } else if (CM_IS_EXPLICIT_CAST(explicit_cast_cm)) {
      // cast_raw_expr.extra_ store explicit cast's cast mode
      cast_raw_expr->set_cast_mode(explicit_cast_cm);
      // type_ctx.cast_mode_ sotre implicit cast's cast mode.
      // cannot use def cm, because it may change explicit cast behavior.
      // eg: select cast(18446744073709551615 as signed) -> -1
      //     because exprlicit case need CM_NO_RANGE_CHECK
      type_ctx.set_cast_mode(explicit_cast_cm & ~CM_EXPLICIT_CAST);
      // in engine 3.0, let implicit cast do the real cast
      bool need_extra_cast_for_src_type = false;
      bool need_extra_cast_for_dst_type = false;
      ObRawExprUtils::need_extra_cast(type1, type, need_extra_cast_for_src_type,
                                      need_extra_cast_for_dst_type);
      if (need_extra_cast_for_src_type) {
        ObExprResType src_type_utf8;
        OZ(ObRawExprUtils::setup_extra_cast_utf8_type(type1, src_type_utf8));
        OX(type1.set_calc_meta(src_type_utf8.get_obj_meta()));
      } else if (need_extra_cast_for_dst_type) {
        ObExprResType dst_type_utf8;
        OZ(ObRawExprUtils::setup_extra_cast_utf8_type(dst_type, dst_type_utf8));
        OX(type1.set_calc_meta(dst_type_utf8.get_obj_meta()));
      } else {
        bool need_wrap = false;
        if (ob_is_enumset_tc(type1.get_type())) {
          // For enum/set type, need to check whether warp to string is required.
          if (OB_FAIL(ObRawExprUtils::need_wrap_to_string(type1, type1.get_calc_type(),
                                          false, need_wrap,
                                          exec_ctx->support_enum_set_type_subschema(*session)))) {
            LOG_WARN("need_wrap_to_string failed", K(ret), K(type1));
          } else if (!need_wrap) {
            // need_wrap is false, set calc_type to type1 itself.
            type1.set_calc_meta(type1.get_obj_meta());
            type1.set_calc_accuracy(type1.get_calc_accuracy());
          }
        } else if (OB_LIKELY(need_wrap)) {
          // need_wrap is true, no-op and keep type1's calc_type is dst_type. It will be wrapped
          // to string in ObRawExprWrapEnumSet::visit(ObSysFunRawExpr &expr) later.
        } else {
          if (ob_is_geometry_tc(dst_type.get_type())) {
            ObCastMode cast_mode = cast_raw_expr->get_cast_mode();
            const ObObj &param = type2.get_param();
            ParseNode parse_node;
            parse_node.value_ = param.get_int();
            ObGeoType geo_type = static_cast<ObGeoType>(parse_node.int16_values_[OB_NODE_CAST_GEO_TYPE_IDX]);
            if (OB_FAIL(ObGeoCastUtils::set_geo_type_to_cast_mode(geo_type, cast_mode))) {
              LOG_WARN("fail to set geometry type to cast mode", K(ret), K(geo_type));
            } else {
              cast_raw_expr->set_cast_mode(cast_mode);
            }
          }
          if (OB_SUCC(ret)) {
            // need_wrap is false, set calc_type to type1 itself.
            type1.set_calc_meta(type1.get_obj_meta());
          }
        }
      }
      if (OB_SUCC(ret)) {
        if (lib::is_mysql_mode() && !ob_is_numeric_type(type.get_type()) && type1.is_double()) {
          // for double type cast non-numeric type, no need set calc accuracy to dst type.
        } else if (ObDecimalIntType == type1.get_type()
                   && ob_is_decimal_int(type1.get_calc_type())) {
          // set type1's calc accuracy with param's accuracy
          type1.set_calc_accuracy(type1.get_accuracy());
        } else {
          type1.set_calc_accuracy(type.get_accuracy());
        }
      }
    } else {
      // no need to set cast mode, already setup while deduce type.
      //
      // implicit cast, no need to add cast again, but the ObRawExprWrapEnumSet depend on this
      // to add enum_to_str(), so we still set the calc type but skip add implicit cast in decuding.
      type1.set_calc_type(type.get_type());
      type1.set_calc_collation_type(type.get_collation_type());
      type1.set_calc_collation_level(type.get_collation_level());
      type1.set_calc_accuracy(type.get_accuracy());
    }
  }
  LOG_DEBUG("calc result type", K(type1), K(type2), K(type), K(dst_type),
            K(type1.get_calc_accuracy()));
  return ret;
}

int ObExprCast::get_cast_type(const bool enable_decimal_int,
                              const ObExprResType &param_type2,
                              const ObCastMode cast_mode,
                              const ObExprTypeCtx &type_ctx,
                              ObRawExprResType &dst_type)
{
  int ret = OB_SUCCESS;
  if (!param_type2.is_int() && !param_type2.get_param().is_int()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cast param type is unexpected", K(param_type2));
  } else {
    const ObObj &param = param_type2.get_param();
    ParseNode parse_node;
    parse_node.value_ = param.get_int();
    ObObjType obj_type = static_cast<ObObjType>(parse_node.int16_values_[OB_NODE_CAST_TYPE_IDX]);
    bool is_explicit_cast = CM_IS_EXPLICIT_CAST(cast_mode);
    dst_type.set_collation_type(static_cast<ObCollationType>(parse_node.int16_values_[OB_NODE_CAST_COLL_IDX]));
    dst_type.set_type(obj_type);
    int64_t maxblen = ObCharset::CharConvertFactorNum;
    if (ob_is_string_type(obj_type)) {
      dst_type.set_full_length(parse_node.int32_values_[OB_NODE_CAST_C_LEN_IDX], param_type2.get_accuracy().get_length_semantics());
      if (lib::is_mysql_mode() && is_explicit_cast && !dst_type.is_binary() && !dst_type.is_varbinary()) {
        if (dst_type.get_length() > OB_MAX_CAST_CHAR_VARCHAR_LENGTH && dst_type.get_length() <= OB_MAX_CAST_CHAR_TEXT_LENGTH) {
          dst_type.set_type(ObTextType);
          dst_type.set_length(OB_MAX_CAST_CHAR_TEXT_LENGTH);
        } else if (dst_type.get_length() > OB_MAX_CAST_CHAR_TEXT_LENGTH && dst_type.get_length() <= OB_MAX_CAST_CHAR_MEDIUMTEXT_LENGTH) {
          dst_type.set_type(ObMediumTextType);
          dst_type.set_length(OB_MAX_CAST_CHAR_MEDIUMTEXT_LENGTH);
        } else if (dst_type.get_length() > OB_MAX_CAST_CHAR_MEDIUMTEXT_LENGTH) {
          dst_type.set_type(ObLongTextType);
          dst_type.set_length(OB_MAX_LONGTEXT_LENGTH / maxblen);
        }
      }
    } else if (ob_is_extend(obj_type)
               || ob_is_collection_sql_type(obj_type)) {
      dst_type.set_udt_id(param_type2.get_udt_id());
      if (ob_is_collection_sql_type(obj_type)) {
        // recover subschema id
        dst_type.set_cs_type(static_cast<ObCollationType>(parse_node.int16_values_[OB_NODE_CAST_COLL_IDX]));
        dst_type.set_cs_level(static_cast<ObCollationLevel>(parse_node.int16_values_[OB_NODE_CAST_CS_LEVEL_IDX]));
      }
    } else if (lib::is_mysql_mode() && ob_is_json(obj_type)) {
      dst_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (ob_is_geometry(obj_type) || ob_is_roaringbitmap(obj_type)) {
      dst_type.set_collation_type(CS_TYPE_BINARY);
      dst_type.set_collation_level(CS_LEVEL_IMPLICIT);
    } else if (ObNumberType == obj_type) {
      dst_type.set_precision(parse_node.int16_values_[OB_NODE_CAST_N_PREC_IDX]);
      dst_type.set_scale(parse_node.int16_values_[OB_NODE_CAST_N_SCALE_IDX]);
      if (enable_decimal_int && CM_IS_EXPLICIT_CAST(cast_mode)) {
        if (is_mysql_mode()) {
          // in mysql mode, if cast is explicit, change dst type from NumberType to DecimalIntType
          dst_type.set_type(ObDecimalIntType);
        } else {
          if (is_decimal_int_accuracy_valid(dst_type.get_precision(), dst_type.get_scale())) {
            // in oracle mode, if cast is explicit and p >= s and s >= 0
            // change dest type from NumberType to DecimalIntType
            dst_type.set_type(ObDecimalIntType);
          } else {
            dst_type.set_type(ObNumberType);
          }
        }
      }
    } else {
      dst_type.set_precision(parse_node.int16_values_[OB_NODE_CAST_N_PREC_IDX]);
      dst_type.set_scale(parse_node.int16_values_[OB_NODE_CAST_N_SCALE_IDX]);
    }
    if (OB_SUCC(ret) && CM_IS_EXPLICIT_CAST(cast_mode) && lib::is_mysql_mode()) {
      if (type_ctx.enable_mysql_compatible_dates()) {
        if (ObDateType == dst_type.get_type()) {
          dst_type.set_type(ObMySQLDateType);
        } else if (ObDateTimeType == dst_type.get_type()) {
          dst_type.set_type(ObMySQLDateTimeType);
        }
      }
    }
    LOG_DEBUG("get_cast_type", K(dst_type), K(param_type2));
  }
  return ret;
}

int ObExprCast::eval_cast_multiset(const sql::ObExpr &expr,
                                   sql::ObEvalCtx &ctx,
                                   sql::ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "eval cast multiset");
  return ret;
}

int ObExprCast::cg_cast_multiset(ObExprCGCtx &op_cg_ctx,
                                 const ObRawExpr &raw_expr,
                                 ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "cast multiset");
  return ret;
}

OB_SERIALIZE_MEMBER(ObExprCast::CastMultisetExtraInfo,
                    pl_type_, not_null_, elem_type_, capacity_, udt_id_);

int ObExprCast::CastMultisetExtraInfo::deep_copy(common::ObIAllocator &allocator,
                                                    const ObExprOperatorType type,
                                                    ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  OZ(ObExprExtraInfoFactory::alloc(allocator, type, copied_info));
  CastMultisetExtraInfo &other = *static_cast<CastMultisetExtraInfo *>(copied_info);
  if (OB_SUCC(ret)) {
    other = *this;
  }
  return ret;
}


int ObExprCast::cg_expr(ObExprCGCtx &op_cg_ctx,
                        const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  OB_ASSERT(2 == rt_expr.arg_cnt_);
  OB_ASSERT(NULL != rt_expr.args_);
  OB_ASSERT(NULL != rt_expr.args_[0]);
  OB_ASSERT(NULL != rt_expr.args_[1]);
  ObObjType in_type = rt_expr.args_[0]->datum_meta_.type_;
  ObCollationType in_cs_type = rt_expr.args_[0]->datum_meta_.cs_type_;
  ObObjType out_type = rt_expr.datum_meta_.type_;
  ObCollationType out_cs_type = rt_expr.datum_meta_.cs_type_;

  bool fast_cast_decint = false;
  ObPrecision out_prec = rt_expr.datum_meta_.precision_;
  ObScale out_scale = rt_expr.datum_meta_.scale_;
  ObPrecision in_prec = rt_expr.args_[0]->datum_meta_.precision_;
  ObScale in_scale = rt_expr.args_[0]->datum_meta_.scale_;
  if (ob_is_integer_type(in_type)) {
    in_scale = 0;
    in_prec = ObAccuracy::MAX_ACCURACY2[MYSQL_MODE][in_type].get_precision();
  }
  // suppose we have (P1, S1) -> (P2, S2)
  // if S2 > S1 && P1 + S2 - S1 <= P2, sizeof(result_type) is wide enough to store result value
  // if S2 <= S1 && width_of_prec(P2) >= width_of_prec(P1), result type is wide enough
  if (ob_is_decimal_int_tc(out_type) && !CM_IS_CONST_TO_DECIMAL_INT(raw_expr.get_cast_mode())
      && (ob_is_int_tc(in_type) || ob_is_uint_tc(in_type) || ob_is_decimal_int_tc(in_type))) {
    if ((out_scale > in_scale && (in_prec + out_scale - in_scale <= out_prec))
        || (out_scale <= in_scale
            && get_decimalint_type(out_prec) >= get_decimalint_type(in_prec))) {
      fast_cast_decint = true;
    }
  }
  LOG_DEBUG("fast decimal int cast", K(fast_cast_decint),
            K(in_prec), K(in_scale), K(out_prec), K(out_scale));

  if (OB_UNLIKELY(ObMaxType == in_type || ObMaxType == out_type) ||
      OB_ISNULL(op_cg_ctx.allocator_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("in_type or out_type or allocator is invalid", K(ret),
             K(in_type), K(out_type), KP(op_cg_ctx.allocator_));
  } else {
    // setup cast mode for explicit cast.
    // Implicit cast's cast mode has already been set when creating cast expr, directly get it from raw_expr.get_cast_mode()
    ObCastMode cast_mode = raw_expr.get_cast_mode();
    if (cast_mode & CM_ZERO_FILL) {
      // Put zerofill information inside scale
      const ObRawExpr *src_raw_expr = NULL;
      CK(OB_NOT_NULL(src_raw_expr = raw_expr.get_param_expr(0)));
      if (OB_SUCC(ret)) {
        const ObRawExprResType &src_res_type = src_raw_expr->get_result_type();
        if (ob_is_string_or_lob_type(in_type)) {
          // do nothing, setting the zerofill length only makes sense when in_type is a numeric type
        } else if (OB_UNLIKELY(UINT_MAX8 < src_res_type.get_length())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected zerofill length", K(ret), K(src_res_type.get_length()));
        } else if (ob_is_string_or_lob_type(out_type)) {
          // The zerofill information will only be used when cast to string/lob type.
          // for these types, scale is unused, so the previous design is to save child length
          // to the scale of the rt_expr.
          rt_expr.datum_meta_.scale_ = static_cast<int8_t>(src_res_type.get_length());
        }
      }
    }
    rt_expr.is_called_in_sql_ = is_called_in_sql();
    if (OB_SUCC(ret)) {
      bool just_eval_arg = false;
      const ObRawExpr *src_raw_expr = raw_expr.get_param_expr(0);
      if (OB_ISNULL(src_raw_expr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null", K(ret));
      } else if (src_raw_expr->is_multiset_expr()) {
        if (OB_FAIL(cg_cast_multiset(op_cg_ctx, raw_expr, rt_expr))) {
          LOG_WARN("failed to cg cast multiset", K(ret));
        }
      } else if (fast_cast_decint) {
        if (CM_IS_EXPLICIT_CAST(cast_mode)) {
          ObDatumCast::get_decint_cast(ob_obj_type_class(in_type), in_prec, in_scale, out_prec,
                                       out_scale, true, rt_expr.eval_batch_func_,
                                       rt_expr.eval_func_);
        } else {
          ObDatumCast::get_decint_cast(ob_obj_type_class(in_type), in_prec, in_scale, out_prec,
                                       out_scale, false, rt_expr.eval_batch_func_,
                                       rt_expr.eval_func_);
        }
        OB_ASSERT(rt_expr.eval_func_ != nullptr);
        OB_ASSERT(rt_expr.eval_batch_func_ != nullptr);
      } else {
        if (OB_FAIL(ObDatumCast::choose_cast_function(in_type, in_cs_type, out_type, out_cs_type,
                                                      cast_mode, *(op_cg_ctx.allocator_),
                                                      just_eval_arg, rt_expr))) {
          LOG_WARN("choose_cast_func failed", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        int tmp_ret = OB_E(EventTable::EN_ENABLE_VECTOR_CAST) OB_SUCCESS;
        rt_expr.eval_vector_func_ =
          tmp_ret == OB_SUCCESS ?
            VectorCasterUtil::get_vector_cast(rt_expr.args_[0]->get_vec_value_tc(),
                                              rt_expr.get_vec_value_tc(), just_eval_arg,
                                              rt_expr.eval_func_, cast_mode) :
            nullptr;
        // Some VEC_TC_XXXX classes still have some types not yet implemented for vectorization, and they still use the non-vectorized interface
        if (ObTinyTextType == in_type || ObTinyTextType == out_type) {
          rt_expr.eval_vector_func_ = nullptr;
        }
      }
    }
    if (OB_SUCC(ret)) {
      rt_expr.extra_ = cast_mode;
    }
  }
  return ret;
}


DEF_SET_LOCAL_SESSION_VARS(ObExprCast, raw_expr) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(raw_expr) || OB_ISNULL(raw_expr->get_param_expr(0))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected null expr", K(ret));
  } else {
    ObObjType src = raw_expr->get_param_expr(0)->get_result_type().get_type();
    ObObjType dst = raw_expr->get_result_type().get_type();
    if (is_mysql_mode()) {
      SET_LOCAL_SYSVAR_CAPACITY(3);
      EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_SQL_MODE);
    } else {
      SET_LOCAL_SYSVAR_CAPACITY(5);
    }
    EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_COLLATION_CONNECTION);
    if (ob_is_datetime_tc(src)
        || ob_is_datetime_tc(dst)
        || ob_is_otimestampe_tc(src)
        || ob_is_otimestampe_tc(dst)) {
      EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_TIME_ZONE);
    }
  }
  return ret;
}

OB_DEF_SERIALIZE(ObExprCast)
{
  int ret = OB_SUCCESS;
  ret = ObExprOperator::serialize_(buf, buf_len, pos);
  return ret;
}

OB_DEF_DESERIALIZE(ObExprCast)
{
  int ret = OB_SUCCESS;
  ret = ObExprOperator::deserialize_(buf, data_len, pos);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExprCast)
{
  int64_t len = 0;
  len = ObExprOperator::get_serialize_size_();
  return len;
}

}
}

