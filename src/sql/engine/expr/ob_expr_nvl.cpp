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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_nvl.h"
#include "sql/engine/expr/ob_expr_promotion_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_is.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

int ObExprNvlUtil::calc_result_type(ObExprResType &type,
                                     ObExprResType &type1,
                                     ObExprResType &type2,
                                     ObExprTypeCtx &type_ctx)
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprPromotionUtil::get_nvl_type(type, type1, type2))) {
    LOG_WARN("get nvl type failed", K(ret), K(type1), K(type2));
  } else if (OB_UNLIKELY(type.is_invalid())) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else if (ob_is_string_type(type.get_type())) {
    ObCollationLevel res_cs_level = CS_LEVEL_INVALID;
    ObCollationType res_cs_type = CS_TYPE_INVALID;
    ObExprResTypes res_types;
    if (OB_FAIL(res_types.push_back(type1))) {
      LOG_WARN("fail to push back res type", K(ret));
    } else if (OB_FAIL(res_types.push_back(type2))) {
      LOG_WARN("fail to push back res type", K(ret));
    } else if (OB_FAIL(ObExprOperator::aggregate_charsets_for_comparison(type, &res_types.at(0), 2, type_ctx))) {
      LOG_WARN("failed to aggregate_charsets_for_comparison", K(ret));
    } else {
      res_cs_type = type.get_calc_collation_type();
      res_cs_level = type.get_calc_collation_level();
    }
    if (OB_SUCC(ret)) {
      type.set_collation_level(res_cs_level);
      type.set_collation_type(res_cs_type);
    }
  } else if (ob_is_json(type.get_type())) {
    type.set_collation_level(CS_LEVEL_IMPLICIT);
  } else if (ob_is_geometry(type.get_type())) {
    type.set_geometry();
  }
  if (OB_SUCC(ret)) {
    type.set_length(MAX(type1.get_length(), type2.get_length()));
    // flag. if both type1 and type2 are NULL, type is null
    if (type1.has_result_flag(NOT_NULL_FLAG) || type2.has_result_flag(NOT_NULL_FLAG)) {
      type.set_result_flag(NOT_NULL_FLAG);
    }
  }
  return ret;
}

ObExprNvl::ObExprNvl(ObIAllocator &alloc)
 : ObFuncExprOperator(alloc, T_FUN_SYS_NVL, N_NVL, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprNvl::~ObExprNvl()
{}

void set_calc_type(const ObExprResType &in_type, ObExprResType &out_type)
{
  out_type.set_calc_meta(in_type.get_obj_meta());
  out_type.set_calc_accuracy(in_type.get_accuracy());
  out_type.set_calc_collation_type(in_type.get_collation_type());
  out_type.set_calc_collation_level(in_type.get_collation_level());
}

int ObExprNvl::calc_result_type2(ObExprResType &type,
                                 ObExprResType &type1,
                                 ObExprResType &type2,
                                 ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session =
    dynamic_cast<const ObSQLSessionInfo*>(type_ctx.get_session());
  if (OB_FAIL(ObExprNvlUtil::calc_result_type(type, type1, type2, type_ctx))) {
    LOG_WARN("calc_result_type failed", K(ret), K(type1), K(type2));
  } else if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cast basic session to sql session failed", K(ret));
  } else {
    // accuracy.
    if (type.get_type() == type1.get_type()) {
      type.set_accuracy(type1.get_accuracy());
    } else {
      type.set_accuracy(type2.get_accuracy());
    }
    ObScale scale1 = type1.is_null() ? 0 : type1.get_scale();
    ObScale scale2 = type2.is_null() ? 0 : type2.get_scale();
    if (-1 != scale1 && -1 != scale2) {
      type.set_scale(static_cast<ObScale>(max(scale1, scale2)));
    } else {
      type.set_scale(-1);
    }
    if (lib::is_mysql_mode() && SCALE_UNKNOWN_YET != type.get_scale()) {
      if (ob_is_real_type(type.get_type())) {
        type.set_precision(static_cast<ObPrecision>(ObMySQLUtil::float_length(type.get_scale())));
      } else if (ob_is_number_or_decimal_int_tc(type.get_type())) {
        const int16_t intd1 = type1.get_precision() - type1.get_scale();
        const int16_t intd2 = type2.get_precision() - type2.get_scale();
        const int16_t prec = MIN(OB_MAX_DECIMAL_POSSIBLE_PRECISION, MAX(type.get_precision(), MAX(intd1, intd2) + type.get_scale()));
        type.set_precision(static_cast<ObPrecision>(prec));
      }
    }
    type.set_length(MAX(type1.get_length(), type2.get_length()));
    //对于 int 和uint64的混合类型，需要提升类型至decimal
    if (lib::is_mysql_mode()
        && (ObUInt64Type == type1.get_type() || ObUInt64Type == type2.get_type())
        && ObIntType == type.get_type()) {
      bool enable_decimalint = false;
      if (OB_FAIL(ObSQLUtils::check_enable_decimalint(session, enable_decimalint))) {
        LOG_WARN("fail to check_enable_decimalint",
            K(ret), K(session->get_effective_tenant_id()));
      } else if (enable_decimalint) {
        type.set_type(ObDecimalIntType);
      } else {
        type.set_type(ObNumberType);
      }
      type.set_accuracy(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].get_accuracy());
    }
    if (OB_SUCC(ret)) {
      if (ObDecimalIntType == type.get_type()) {
        type.set_scale(static_cast<ObScale>(max(scale1, scale2)));
        type.set_precision(max(type1.get_precision() - type1.get_scale(),
                               type2.get_precision() - type2.get_scale())
                         + max(type1.get_scale(), type2.get_scale()));
        type1.set_calc_accuracy(type.get_accuracy());
        type2.set_calc_accuracy(type.get_accuracy());
      }
      // enumset.
      const bool type1_is_enumset = ob_is_enumset_tc(type1.get_type());
      const bool type2_is_enumset = ob_is_enumset_tc(type2.get_type());
      if (type1_is_enumset || type2_is_enumset) {
        ObObjType calc_type = get_enumset_calc_type(type.get_type(), OB_INVALID_INDEX);
        if (OB_UNLIKELY(ObMaxType == calc_type)) {
          ret = OB_ERR_UNEXPECTED;
          SQL_ENG_LOG(WARN, "invalid type of parameter ", K(type1), K(type2), K(ret));
        } else if (ObVarcharType == calc_type) {
          if (type1_is_enumset) {
            type1.set_calc_type(get_enumset_calc_type(type.get_type(), 0));
            type1.set_calc_collation_type(type.get_collation_type());
          } else {
            set_calc_type(type, type1);
          }
          if (type2_is_enumset) {
            type2.set_calc_type(get_enumset_calc_type(type.get_type(), 1));
            type2.set_calc_collation_type(type.get_collation_type());
          } else {
            set_calc_type(type, type2);
          }
        } else {
          set_calc_type(type, type1);
          set_calc_type(type, type2);
        }
      } else {
        set_calc_type(type, type1);
        set_calc_type(type, type2);
      }
    }
  }
  return ret;
}

int ObExprNvlUtil::calc_nvl_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                 ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  // nvl(arg0, arg1)
  ObDatum *arg0 = NULL;
  ObDatum *arg1 = NULL;
  constexpr bool is_udt_type = false;
  bool v = false;

  if (OB_FAIL(expr.eval_param_value(ctx, arg0, arg1))) {
    LOG_WARN("eval args failed", K(ret));
  } else if (OB_FAIL(pl::ObPLDataType::datum_is_null(arg0, is_udt_type, v))) {
    LOG_WARN("failed to check datum null", K(ret), K(arg0), K(is_udt_type));
  } else if (!v) {
    res_datum.set_datum(*arg0);
  } else {
    res_datum.set_datum(*arg1);
  }
  return ret;
}

int ObExprNvlUtil::calc_nvl_expr_batch(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      const ObBitVector &skip,
                                      const int64_t batch_size) {
  LOG_DEBUG("eval nvl batch mode", K(batch_size));
  int ret = OB_SUCCESS;
  ObDatum* results = expr.locate_batch_datums(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  ObDatumVector args0;
  ObDatumVector args1;
  constexpr bool is_udt_type = false;
  bool v = false;
  if (OB_FAIL(expr.eval_batch_param_value(ctx, skip, batch_size, args0,
                                          args1))) {
    LOG_WARN("eval batch args failed", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      eval_flags.set(i);
      ObDatum *arg0 = args0.at(i);
      ObDatum *arg1 = args1.at(i);
      if (OB_FAIL(pl::ObPLDataType::datum_is_null(arg0, is_udt_type, v))) {
        LOG_WARN("failed to check datum null", K(ret), K(arg0), K(is_udt_type));
      } else if (!v) {
        results[i].set_datum(*arg0);
      } else {
        results[i].set_datum(*arg1);
      }
    }
  }

  return ret;
}

int ObExprNvlUtil::calc_nvl_expr2(const ObExpr &expr, ObEvalCtx &ctx,
                                  ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  // nvl(arg0, arg1, arg2)
  ObDatum *arg0 = NULL;
  ObDatum *arg1 = NULL;
  ObDatum *arg2 = NULL;
  constexpr bool is_udt_type = false;
  bool v = false;

  if (OB_FAIL(expr.eval_param_value(ctx, arg0, arg1, arg2))) {
    LOG_WARN("eval args failed", K(ret));
  } else if (OB_FAIL(pl::ObPLDataType::datum_is_null(arg0, is_udt_type, v))) {
    LOG_WARN("failed to check datum null", K(ret), K(arg0), K(is_udt_type));
  } else if (!v) {
    res_datum.set_datum(*arg1);
  } else {
    res_datum.set_datum(*arg2);
  }
  return ret;
}

int ObExprNvl::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprNvlUtil::calc_nvl_expr;
  rt_expr.eval_batch_func_ = ObExprNvlUtil::calc_nvl_expr_batch;
  return ret;
}

}//namespace sql
}//namespace oceanbase
