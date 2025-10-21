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

#include "ob_expr_least.h"
#include "sql/engine/expr/ob_expr_greatest.h"
#include "sql/engine/expr/ob_datum_cast.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprLeastGreatest::ObExprLeastGreatest(ObIAllocator &alloc, ObExprOperatorType type,
                                                const char *name, int32_t param_num)
    : ObMinMaxExprOperator(alloc,
                           type,
                           name,
                           param_num,
                           NOT_ROW_DIMENSION)
{
}

int ObExprLeastGreatest::calc_result_typeN(ObExprResType &type,
                                           ObExprResType *types_stack,
                                           int64_t param_num,
                                           ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ret = calc_result_typeN_mysql(type, types_stack, param_num, type_ctx);
  return ret;
}

/* Behavior of greatest in MySQL:
 * *. cached_field_type is derived using standard logic agg_field_type(), as the result column type.
 * *. Calculation process of collation: If the parameters include numeric types, it is binary; otherwise, it is calculated through rules.
 * *. The calculation process is based on get_cmp_type() to find an intermediate result, then all numbers are converted to the intermediate result for comparison operations.
 *    That is to say, the comparison process is unrelated to cached_field_type. The logic of get_calc_type() is: if all parameters are STRING,
 *    the return type is STRING; otherwise, it returns a numeric type. When returning a numeric type, all parameters are converted to numeric types for comparison.
 */
int ObExprLeastGreatest::calc_result_typeN_mysql(ObExprResType &type,
                                                 ObExprResType *types,
                                                 int64_t param_num,
                                                 common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num <= 1)) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("not enough param", K(ret));
    ObString func_name(get_name());
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name.length(), func_name.ptr());
  } else {
    const ObSQLSessionInfo *session = static_cast<const ObSQLSessionInfo*>(type_ctx.get_session());
    ObExprOperator::calc_result_flagN(type, types, param_num);
    // If all parameters are IntTC or UIntTC, then do not cast the parameters, and the result type is promoted based on the length of the parameters.
    // Otherwise cast all parameters to the derived calc_type
    bool all_integer = true;
    for (int i = 0; i < param_num && all_integer; ++i) {
      ObObjType obj_type = types[i].get_type();
      if (!ob_is_integer_type(obj_type) && ObNullType != obj_type) {
        all_integer = false;
      }
    }
    bool enable_decimalint = false;
    if (OB_FAIL(calc_result_meta_for_comparison(type, types, param_num, type_ctx, enable_decimalint))) {
      LOG_WARN("calc result meta for comparison failed");
    }
    if (OB_SUCC(ret)) {
      // can't cast origin parameters.
      for (int64_t i = 0; i < param_num; i++) {
        types[i].set_calc_meta(types[i].get_obj_meta());
      }
      if (all_integer && type.is_integer_type()) {
        type.set_calc_type(ObNullType);
      } else if (all_integer && ob_is_number_or_decimal_int_tc(type.get_type())) {
        // the args type is integer and result type is number/decimal, there are unsigned bigint in
        // args, set expr meta here.
        type.set_accuracy(common::ObAccuracy::DDL_DEFAULT_ACCURACY2[0/*is_oracle*/][ObUInt64Type]);
      } else {
        for (int64_t i = 0; i < param_num; i++) {
          if (ob_is_enum_or_set_type(types[i].get_type())) {
            types[i].set_calc_type(type.get_calc_type());
          }
        }
      }
      LOG_DEBUG("least calc_result_typeN", K(type), K(type.get_calc_accuracy()));
    }
  }
  return ret;
}

int ObExprLeastGreatest::cg_expr(ObExprCGCtx &op_cg_ctx,
                                 const ObRawExpr &raw_expr,
                                 ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  const uint32_t param_num = rt_expr.arg_cnt_;
  if (OB_UNLIKELY(param_num < 2)
    || OB_ISNULL(rt_expr.args_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("args_ is null or too few arguments", K(ret), K(rt_expr.args_), K(param_num));
  } else if (OB_ISNULL(op_cg_ctx.session_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else {
    const bool string_result = ob_is_string_or_lob_type(rt_expr.datum_meta_.type_);
    for (int i = 0; OB_SUCC(ret) && i < param_num; ++i) {
      if (OB_ISNULL(rt_expr.args_[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("child of expr is null", K(ret), K(i));
      } else if (OB_ISNULL(rt_expr.args_[i]->basic_funcs_)
          || OB_ISNULL(rt_expr.args_[i]->basic_funcs_->null_first_cmp_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("basic func of or cmp func is null", K(ret), K(rt_expr.args_[i]->basic_funcs_));
      }
    }
    if (OB_SUCC(ret)) {
      if (T_FUN_SYS_GREATEST == type_) {
        rt_expr.eval_func_ = ObExprGreatest::calc_greatest;
      } else {
        rt_expr.eval_func_ = ObExprLeast::calc_least;
      }
      const ObObjMeta &cmp_meta = raw_expr.get_extra_calc_meta();
      const bool is_explicit_cast = false;
      const int32_t result_flag = 0;
      ObCastMode cm = CM_NONE;
      if (!cmp_meta.is_null()) {
        DatumCastExtraInfo *info = OB_NEWx(DatumCastExtraInfo, op_cg_ctx.allocator_, *(op_cg_ctx.allocator_), type_);
        ObSQLMode sql_mode = op_cg_ctx.session_->get_sql_mode();
        const ObLocalSessionVar *local_vars = NULL;
        if (OB_ISNULL(info)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("alloc memory failed", K(ret));
        } else if (OB_FAIL(ObSQLUtils::get_solidified_vars_from_ctx(raw_expr, local_vars))) {
          LOG_WARN("failed to get local session var", K(ret));
        } else if (OB_FAIL(ObSQLUtils::merge_solidified_var_into_sql_mode(local_vars, sql_mode))) {
          LOG_WARN("try get local sql mode failed", K(ret));
        } else if (CS_TYPE_INVALID == cmp_meta.get_collation_type()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("compare cs type is invalid", K(ret), K(cmp_meta));
        } else {
          ObSQLUtils::get_default_cast_mode(is_explicit_cast, result_flag,
                                            op_cg_ctx.session_->get_stmt_type(),
                                            op_cg_ctx.session_->is_ignore_stmt(),
                                            sql_mode, cm);
          info->cmp_meta_.type_ = cmp_meta.get_type();
          info->cmp_meta_.cs_type_ = cmp_meta.get_collation_type();
          info->cmp_meta_.scale_ = cmp_meta.get_scale();
          info->cmp_meta_.precision_ = PRECISION_UNKNOWN_YET;
          info->cm_ = cm;
          rt_expr.extra_info_ = info;

          bool has_lob_header = false;
          if (is_lob_storage(info->cmp_meta_.type_)) {
            has_lob_header = true;
          }
          ObDatumCmpFuncType cmp_func = ObDatumFuncs::get_nullsafe_cmp_func(cmp_meta.get_type(),
                                                                            cmp_meta.get_type(),
                                                                            NULL_LAST,
                                                                            cmp_meta.get_collation_type(),
                                                                            info->cmp_meta_.scale_,
                                                                            false,
                                                                            has_lob_header);
          if (OB_ISNULL(cmp_func)) {
            ret = OB_INVALID_ARGUMENT;
            LOG_WARN("invalid cmp type of params", K(ret), K(cmp_meta));
          } else if (OB_ISNULL(rt_expr.inner_functions_ =
                            reinterpret_cast<void**>(op_cg_ctx.allocator_->alloc(sizeof(void*))))) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            LOG_WARN("alloc memory failed", K(ret));
          } else {
            rt_expr.inner_func_cnt_ = 1;
            rt_expr.inner_functions_[0] = reinterpret_cast<void*>(cmp_func);
          }
        }
      }
      LOG_DEBUG("least cg", K(result_type_));
    }
  }
  return ret;
}

int ObExprLeastGreatest::cast_param(const ObExpr &src_expr, ObEvalCtx &ctx,
                                    const ObDatumMeta &dst_meta,
                                    const ObCastMode &cm, ObIAllocator &allocator,
                                    ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  const bool string_type = ob_is_string_type(dst_meta.type_);
  const bool decimal_int_type = ob_is_decimal_int(dst_meta.type_);
  if (src_expr.datum_meta_.type_ == dst_meta.type_
      && (!string_type || src_expr.datum_meta_.cs_type_ == dst_meta.cs_type_)
      && (!decimal_int_type || src_expr.datum_meta_.scale_ == dst_meta.scale_)) {
    res_datum = src_expr.locate_expr_datum(ctx);
  } else if (OB_ISNULL(ctx.datum_caster_) && OB_FAIL(ctx.init_datum_caster())) {
    LOG_WARN("init datum caster failed", K(ret));
  } else {
    ObDatum *cast_datum = NULL;
    if (OB_FAIL(ctx.datum_caster_->to_type(dst_meta, src_expr, cm, cast_datum, ctx.get_batch_idx()))) {
      LOG_WARN("fail to dynamic cast", K(ret), K(cm));
    } else if (OB_FAIL(res_datum.deep_copy(*cast_datum, allocator))) {
      LOG_WARN("deep copy datum failed", K(ret));
    } else {
      LOG_DEBUG("cast_param", K(src_expr), KP(ctx.frames_[src_expr.frame_idx_]),
                K(&(src_expr.locate_expr_datum(ctx))),
                K(ObToStringExpr(ctx, src_expr)), K(dst_meta), K(res_datum));
    }
  }
  return ret;
}

int ObExprLeastGreatest::cast_result(const ObExpr &src_expr, const ObExpr &dst_expr, ObEvalCtx &ctx,
                                     const ObCastMode &cm, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  const bool string_type = ob_is_string_type(src_expr.datum_meta_.type_);
  const bool decimal_int_type = ob_is_decimal_int(src_expr.datum_meta_.type_);
  if (src_expr.datum_meta_.type_ == dst_expr.datum_meta_.type_
      && (!string_type || src_expr.datum_meta_.cs_type_ == dst_expr.datum_meta_.cs_type_)
      && (!decimal_int_type
          || (src_expr.datum_meta_.scale_ == dst_expr.datum_meta_.scale_
              && src_expr.datum_meta_.precision_ == dst_expr.datum_meta_.precision_))) {
    ObDatum *res_datum = nullptr;
    if (OB_FAIL(src_expr.eval(ctx, res_datum))) {
      LOG_WARN("eval param value failed", K(ret));
    } else {
      expr_datum = *res_datum;
    }
  } else if (OB_ISNULL(ctx.datum_caster_) && OB_FAIL(ctx.init_datum_caster())) {
    LOG_WARN("init datum caster failed", K(ret));
  } else {
    ObDatum *cast_datum = NULL;
    if (OB_FAIL(ctx.datum_caster_->to_type(dst_expr.datum_meta_, src_expr, cm, cast_datum, ctx.get_batch_idx()))) {
      LOG_WARN("fail to dynamic cast", K(ret));
    } else if (OB_FAIL(dst_expr.deep_copy_datum(ctx, *cast_datum))) {
      LOG_WARN("deep copy datum failed", K(ret));
    }
  }
  return ret;
}

int ObExprLeastGreatest::calc_mysql(const ObExpr &expr, ObEvalCtx &ctx,
                                    ObDatum &expr_datum, bool least)
{
  int ret = OB_SUCCESS;
  uint32_t param_num = expr.arg_cnt_;
  bool has_null = false;
  int64_t cmp_res_offset = 0;
  // Here we need to process the parameters according to the required values, if a parameter value is null, we will no longer calculate the values of the subsequent parameters
  //create table t(c1 int, c2 varchar(10));
  //insert into t values(null, 'a');
  // select least(c2, c1) from t; mysql will report warning, oracle will report error
  // select least(c1, c2) from t; mysql will not report warning, oracle normally outputs null
  for (int i = 0; OB_SUCC(ret) && !has_null && i < param_num; ++i) {
    ObDatum *tmp_datum = NULL;
    if (OB_FAIL(expr.args_[i]->eval(ctx, tmp_datum))) {
      LOG_WARN("eval param value failed", K(ret), K(i));
    } else {
      if (tmp_datum->is_null()) {
        has_null = true;
        expr_datum.set_null();
      }
    }
  }
  // if all params and integer and result type is also integer, there is no inner_function of least expr.
  // otherwise, inner_func_cnt_ will be one. It stores the cmp function for parameters.
  if (!has_null && OB_SUCC(ret)) {
    const bool all_integer = 0 == expr.inner_func_cnt_;
    // compare all params.
    if (all_integer) {
      if (ob_is_int_tc(expr.datum_meta_.type_)) {
        int64_t minmax_value = expr.locate_param_datum(ctx, 0).get_int();
        for (int i = 1; i < param_num; ++i) {
          int64_t new_value =  expr.locate_param_datum(ctx, i).get_int();
          if (least != (minmax_value < new_value)) {
            minmax_value = new_value;
          }
        }
        expr_datum.set_int(minmax_value);
      } else {
        uint64_t minmax_value = expr.locate_param_datum(ctx, 0).get_uint();
        for (int i = 1; i < param_num; ++i) {
          uint64_t new_value =  expr.locate_param_datum(ctx, i).get_uint();
          if (least != (minmax_value < new_value)) {
            minmax_value = new_value;
          }
        }
        expr_datum.set_uint(minmax_value);
      }
    } else if (OB_ISNULL(expr.extra_info_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("extra info is null", K(ret));
    } else {
      DatumCastExtraInfo *cast_info = static_cast<DatumCastExtraInfo *>(expr.extra_info_);
      int res_idx = 0;
      ObDatum minmax_datum;
      ObTempExprCtx::TempAllocGuard tmp_alloc_guard(ctx);
      ObDatumCmpFuncType cmp_func = reinterpret_cast<ObDatumCmpFuncType>(expr.inner_functions_[0]);
      if (OB_SUCC(ret) &&
          OB_FAIL(cast_param(*expr.args_[0], ctx, cast_info->cmp_meta_, cast_info->cm_,
                             tmp_alloc_guard.get_allocator(), minmax_datum))) {
        LOG_WARN("cast param failed", K(ret));
      }
      for (int i = 1; OB_SUCC(ret) && i < param_num; ++i) {
        ObDatum cur_datum;
        if (OB_FAIL(cast_param(*expr.args_[i], ctx, cast_info->cmp_meta_, cast_info->cm_,
                              tmp_alloc_guard.get_allocator(), cur_datum))) {
          LOG_WARN("cast param failed", K(ret));
        } else {
          int cmp_res = 0;
          if (OB_FAIL(cmp_func(minmax_datum, cur_datum, cmp_res))) {
            LOG_WARN("compare failed", K(ret));
          } else if((!least && cmp_res < 0) || (least && cmp_res > 0)) {
            res_idx = i;
            minmax_datum = cur_datum;
          }
        }
      }
      // ok, we got the least / greatest param.
      if (OB_SUCC(ret)) {
        if (OB_FAIL(cast_result(*expr.args_[res_idx], expr, ctx, cast_info->cm_, expr_datum))) {
          LOG_WARN("cast result failed", K(ret));
        }
      }
    }
  }
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprLeastGreatest, raw_expr) {
  int ret = OB_SUCCESS;
  if (is_mysql_mode()) {
    SET_LOCAL_SYSVAR_CAPACITY(2);
    EXPR_ADD_LOCAL_SYSVAR(share::SYS_VAR_SQL_MODE);
    EXPR_ADD_LOCAL_SYSVAR(share::SYS_VAR_COLLATION_CONNECTION);
  }
  return ret;
}

ObExprLeast::ObExprLeast(common::ObIAllocator &alloc)
    : ObExprLeastGreatest(alloc,
                           T_FUN_SYS_LEAST,
                           N_LEAST,
                           MORE_THAN_ZERO)
{
}

//same type params
int ObExprLeast::calc_least(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ret = ObExprLeastGreatest::calc_mysql(expr, ctx, expr_datum, true);
  return ret;
}

}
}

