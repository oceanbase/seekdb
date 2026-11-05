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

#define USING_LOG_PREFIX  SQL_ENG

#include "ob_expr_convert.h"

#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace oceanbase
{
namespace sql
{

ObExprConvert::ObExprConvert(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_CONVERT, N_CONVERT, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprConvert::~ObExprConvert()
{
}

int ObExprConvert::calc_result_type2(ObExprResType &type,
                                     ObExprResType &type1,
                                     ObExprResType &type2,
                                     ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);

  int ret = OB_SUCCESS;
  type.set_type(ObVarcharType); // Only convert (xx using collation) will reach here now. It must be a varchar result.
  type.set_scale(type1.get_scale()); 
  type.set_precision(type1.get_precision());
  if (ob_is_string_type(type.get_type())) {
    type.set_length(type1.get_length());
  }
  const ObObj &dest_collation = type2.get_param();
  TYPE_CHECK(dest_collation, ObVarcharType);
  if (OB_SUCC(ret)) {
    ObString cs_name = dest_collation.get_string();
    ObCharsetType charset_type = CHARSET_INVALID;
    if (CHARSET_INVALID == (charset_type = ObCharset::charset_type(cs_name.trim()))) {
      ret = OB_ERR_UNKNOWN_CHARSET;
      LOG_WARN("unknown charset", K(ret), K(cs_name));
    } else {
      type.set_collation_level(CS_LEVEL_IMPLICIT);
      type.set_collation_type(ObCharset::get_default_collation(charset_type));
      //set calc type
      //only set type2 here.
      type2.set_calc_type(ObVarcharType);
      // cast expression will cast the first child node of convert expression to type1, calculation result of convert is the first one
      // Subnode result
      type1.set_calc_meta(type.get_obj_meta());
      type1.set_calc_collation_type(type.get_collation_type());
      type1.set_calc_collation_level(type.get_collation_level());
      type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_CHARSET_CONVERT_IGNORE_ERR);
      LOG_DEBUG("in calc result type", K(ret), K(type1), K(type2), K(type));
    }
  }

  return ret;
}

int calc_convert_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *child_res = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, child_res))) {
    LOG_WARN("eval arg 0 failed", K(ret));
  } else {
    ObCollationType cs_type = expr.args_[0]->datum_meta_.cs_type_;
    int64_t mbmaxlen = 1;
    if (OB_FAIL(ObCharset::get_mbmaxlen_by_coll(cs_type, mbmaxlen))) {
      LOG_WARN("fail to get mbmaxlen", K(cs_type), K(ret));
    } else if (mbmaxlen > 1 && !child_res->is_null()) {
      ObString checked_res;
      bool is_null = false;
      const ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
      ObSQLMode sql_mode = 0;
      ObSolidifiedVarsGetter helper(expr, ctx, session);
      if (OB_ISNULL(session)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("session is null", K(ret));
      } else if (OB_FAIL(helper.get_sql_mode(sql_mode))) {
        LOG_WARN("get sql mode failed", K(ret));
      } else if (OB_FAIL(ObSQLUtils::check_well_formed_str(child_res->get_string(),
                                                           cs_type,
                                                           checked_res,
                                                           is_null,
                                                           is_strict_mode(sql_mode),
                                                           false))) {
        LOG_WARN("check_well_formed_str failed", K(ret),
                                                 K(child_res->get_string()),
                                                 K(expr.datum_meta_));
      } else if (is_null) {
        res_datum.set_null();
      } else {
        res_datum.set_string(checked_res);
      }
    } else {
      res_datum.set_datum(*child_res);
    }
  }
  return ret;
}

int ObExprConvert::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                           ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_convert_expr;
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprConvert, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(1);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_SQL_MODE);
  return ret;
}

} //namespace sql
} //namespace oceanbase
