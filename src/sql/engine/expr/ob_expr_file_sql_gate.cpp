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
#include "sql/engine/expr/ob_expr_file_sql_gate.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_basic_session_info.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace sql
{

ObExprFileSqlGate::ObExprFileSqlGate(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_FILE_SQL_GATE, "__file_sql_gate", 0,
                       NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprFileSqlGate::~ObExprFileSqlGate()
{
}

int ObExprFileSqlGate::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_int();
  type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
  return OB_SUCCESS;
}

int ObExprFileSqlGate::eval_file_sql_gate(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  const ObSQLSessionInfo *session_info = nullptr;
  if (OB_ISNULL(session_info = ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    const ObObj *value = session_info->get_user_variable_value(
        ObString::make_string("__enable_file_sql"));
    int64_t flag = 0;
    if (nullptr != value && !value->is_null()) {
      if (value->is_integer_type()) {
        flag = value->get_int();
      } else if (value->is_string_type()) {
        const ObString text = value->get_string();
        flag = (0 == text.case_compare("on") || 0 == text.case_compare("true") ||
                0 == text.compare("1")) ? 1 : 0;
      }
    }
    if (0 == flag) {
      ret = OB_NOT_SUPPORTED;
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "file sql is not enabled in this session");
      LOG_WARN("file sql is disabled, enable_file_sql must be set to on", K(ret));
    } else {
      expr_datum.set_int(1);
    }
  }
  return ret;
}

int ObExprFileSqlGate::cg_expr(ObExprCGCtx &op_cg_ctx,
                               const ObRawExpr &raw_expr,
                               ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprFileSqlGate::eval_file_sql_gate;
  return OB_SUCCESS;
}

ObExprFileSqlReject::ObExprFileSqlReject(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_FILE_SQL_REJECT, "__file_sql_reject", 0,
                       NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprFileSqlReject::~ObExprFileSqlReject()
{
}

int ObExprFileSqlReject::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_int();
  type.set_scale(DEFAULT_SCALE_FOR_INTEGER);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].precision_);
  return OB_SUCCESS;
}

int ObExprFileSqlReject::eval_file_sql_reject(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(expr_datum);
  ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "this file sql option is not supported");
  LOG_WARN("unsupported file sql option", K(ret));
  return ret;
}

int ObExprFileSqlReject::cg_expr(ObExprCGCtx &op_cg_ctx,
                                 const ObRawExpr &raw_expr,
                                 ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprFileSqlReject::eval_file_sql_reject;
  return OB_SUCCESS;
}

}  // namespace sql
}  // namespace oceanbase
