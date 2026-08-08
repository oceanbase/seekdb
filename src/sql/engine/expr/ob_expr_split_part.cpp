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

#include "sql/engine/expr/ob_expr_split_part.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
ObExprSplitPart::ObExprSplitPart(ObIAllocator &alloc)
    : ObStringExprOperator(alloc, T_FUN_SYS_SPLIT_PART, N_SPLIT_PART, MORE_THAN_TWO,
                           VALID_FOR_GENERATED_COL)
{
  need_charset_convert_ = false;
}

ObExprSplitPart::~ObExprSplitPart()
{
}

int ObExprSplitPart::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types,
                                      int64_t param_num,
                                      ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  if (param_num != 3 && param_num != 4) {
    ret = OB_ERR_PARAM_SIZE;
    LOG_WARN("the param number of split_part should be 2 or 4", K(ret), K(param_num));
  } else if (ObJsonType == types[0].get_type()) {
    ObString func_name("SPLIT_PART");
    ret = OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE;
    LOG_WARN("The first argument type is incorrect", K(ret), K(types[0].get_type()));
    LOG_USER_ERROR(OB_ERR_WRONG_FUNC_ARGUMENTS_TYPE, func_name.length(), func_name.ptr());
  } else {
    if (ObTextType == types[0].get_type()
        || ObMediumTextType == types[0].get_type()
        || ObLongTextType == types[0].get_type()) {
      type.set_type(types[0].get_type());
    } else {
      type.set_varchar();
    }
    if (OB_FAIL(aggregate_charsets_for_string_result(type, types, 1, type_ctx))) {
      LOG_WARN("aggregate_charsets_for_string_result failed", K(ret));
    } else {
      types[0].set_calc_meta(type);
      types[1].set_calc_type(ObVarcharType);
      for (int i = 0; OB_SUCC(ret) && i < 2; i++) {
        types[i].set_calc_collation_type(type.get_collation_type());
        types[i].set_calc_collation_level(type.get_collation_level());
      }
      types[2].set_calc_type(ObIntType);
      if (param_num == 4) {
        types[3].set_calc_type(ObIntType);
      }
    }
  }
  if (OB_SUCC(ret)) {
    type.set_length(types[0].get_length());
  }
  return ret;
}

int ObExprSplitPart::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_split_part_expr;
  return ret;
}

int ObExprSplitPart::calc_split_part_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                        ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *delimiter_datum = NULL;
  ObDatum *str_datum = NULL;
  ObDatum *start_part_datum = NULL;
  ObDatum *end_part_datum = NULL;
  if (OB_UNLIKELY(3 != expr.arg_cnt_ && 4 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid arg cnt", K(ret), K(expr.arg_cnt_));
  } else if (4 == expr.arg_cnt_ &&
             OB_FAIL(expr.eval_param_value(ctx, str_datum, delimiter_datum,
                                           start_part_datum, end_part_datum))) {
    LOG_WARN("eval arg failed", K(ret));
  } else if (3 == expr.arg_cnt_) {
    if (OB_FAIL(expr.eval_param_value(ctx, str_datum, delimiter_datum, start_part_datum))) {
      LOG_WARN("eval arg failed", K(ret));
    } else {
      end_part_datum = start_part_datum;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (str_datum->is_null() ||
             delimiter_datum->is_null() ||
             start_part_datum->is_null() ||
             end_part_datum->is_null()) {
    res.set_null();
  } else {
    int64_t start_part = start_part_datum->get_int() == 0 ? 1 : start_part_datum->get_int();
    int64_t end_part = 3 == expr.arg_cnt_ ? start_part : end_part_datum->get_int();
    const ObString delimiter = delimiter_datum->get_string();
    ObCollationType calc_cs_type = expr.args_[0]->datum_meta_.cs_type_;
    if (!ob_is_text_tc(expr.args_[0]->datum_meta_.type_)) {
      const ObString &str = str_datum->get_string();
      bool null_res = false;
      ObString res_str;
      ret = calc_split_part(calc_cs_type, str, delimiter,
                                                start_part, end_part, null_res, res_str);
      if (OB_FAIL(ret)) {
        LOG_WARN("clac split part fialed", K(ret));
      } else {
        res.set_string(res_str);
      }
    } else {
      ObEvalCtx::TempAllocGuard alloc_guard(ctx);
      ObIAllocator &tmp_alloc = alloc_guard.get_allocator();
      ObString str;
      ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &res);
      if (OB_FAIL(ObTextStringHelper::get_string(ctx.exec_ctx_, expr, tmp_alloc, 0, str_datum, str))) {
        LOG_WARN("get full text string failed ", K(ret));
      } else {
        bool null_res = false;
        ObString res_str;
        ret = calc_split_part(calc_cs_type, str, delimiter,
                                                start_part, end_part, null_res, res_str);
        if (OB_FAIL(ret)) {
          LOG_WARN("clac split part fialed", K(ret));
        } else {
          if (OB_FAIL(output_result.init(res_str.length()))) {
            LOG_WARN("init TextString result failed", K(ret));
          } else {
            output_result.append(res_str);
            output_result.set_result();
          }
        }
      }
    }
  }
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprSplitPart, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(1);
  EXPR_ADD_LOCAL_SYSVAR(share::SYS_VAR_COLLATION_CONNECTION);
  return ret;
}

}
}
