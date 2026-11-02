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

#include "sql/engine/expr/ob_expr_sql_mode_convert.h"
#include "common/sql_mode/ob_sql_mode_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprSqlModeConvert::ObExprSqlModeConvert(ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_SYS_SQL_MODE_CONVERT, N_SQL_MODE_CONVERT, 1, NOT_VALID_FOR_GENERATED_COL)
{
}

ObExprSqlModeConvert::~ObExprSqlModeConvert()
{
}

int ObExprSqlModeConvert::calc_result_type1(ObExprResType &type,
                                            ObExprResType &type1,
                                            common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(type1);

  int ret = OB_SUCCESS;
  type.set_varchar();
  type.set_default_collation_type();
  type.set_collation_level(CS_LEVEL_SYSCONST);
  type.set_length(static_cast<common::ObLength>(OB_MAX_SYS_VAR_VAL_LENGTH));
  return ret;
}

int ObExprSqlModeConvert::sql_mode_convert(const ObExpr &expr,
                                           ObEvalCtx &ctx,
                                           ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *arg = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, arg))) {
    LOG_WARN("eval arg failed", K(ret));
  } else if (arg->is_null()) {
    expr_datum.set_null();
  } else {
    ObObj int_value;
    ObObj str_value;
    int_value.set_uint64(arg->get_uint64());
    if (OB_FAIL(common::ob_sql_mode_to_str(int_value, str_value, &ctx.get_expr_res_alloc()))) {
      LOG_WARN("convert sql mode failed", K(ret));
    } else {
      expr_datum.set_string(str_value.get_string());
    }
  }
  return ret;
}

int ObExprSqlModeConvert::cg_expr(ObExprCGCtx &op_cg_ctx, 
                                  const ObRawExpr &raw_expr,
                                  ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprSqlModeConvert::sql_mode_convert;
  return OB_SUCCESS;
}

}
}
