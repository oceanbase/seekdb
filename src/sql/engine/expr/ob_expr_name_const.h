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
 
#ifndef _OCEANBASE_SQL_OB_EXPR_NAME_CONST_H_
#define _OCEANBASE_SQL_OB_EXPR_NAME_CONST_H_
#include "lib/ob_name_def.h"
#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
namespace sql
{
class ObExprNameConst : public ObFuncExprOperator
{
public:
  explicit  ObExprNameConst(common::ObIAllocator &alloc);
  virtual ~ObExprNameConst();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &expr) const override;
  static int eval_name_const(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprNameConst);
};

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_NAME_CONST_H_
