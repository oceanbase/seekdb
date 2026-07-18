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

#ifndef _OB_EXPR_GET_SYS_VAR_H
#define _OB_EXPR_GET_SYS_VAR_H 1
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
// get_sys_var(name, scope)
class ObExprGetSysVar: public ObFuncExprOperator
{
public:
  static const ObExprOperatorType op_type_;
  ObExprGetSysVar();
  explicit  ObExprGetSysVar(common::ObIAllocator &alloc);
  virtual ~ObExprGetSysVar();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  // for engine 3.0
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
  static int calc_get_sys_val_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                 ObDatum &res_datum);
private:
  static int calc_(common::ObObj &result, const common::ObString &var_name, 
                   const int64_t var_scope, ObSQLSessionInfo *session, 
                   ObExecContext *exec_ctx, common::ObIAllocator &alloc);
  static int get_session_var(common::ObObj &result, const common::ObString &var_name,
                             common::ObIAllocator &alloc, ObSQLSessionInfo *session, 
                             ObExecContext *exec_ctx);
  static int get_sys_var_disp_obj(common::ObIAllocator &allocator,
                           const ObSQLSessionInfo &session,
                           const common::ObString &var_name,
                           common::ObObj &disp_obj);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprGetSysVar);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* _OB_EXPR_GET_SYS_VAR_H */
