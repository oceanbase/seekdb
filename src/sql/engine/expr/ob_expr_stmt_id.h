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

#ifndef OBDEV_SRC_SQL_ENGINE_EXPR_OB_EXPR_STMT_ID_H_
#define OBDEV_SRC_SQL_ENGINE_EXPR_OB_EXPR_STMT_ID_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprStmtId : public ObFuncExprOperator
{
public:
  explicit ObExprStmtId(common::ObIAllocator &alloc);
  virtual ~ObExprStmtId() {}

  virtual int calc_result_type0(ObExprResType &type,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_stmt_id(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprStmtId);

};
}//end namespace sql
}//end namespace oceanbase
#endif /* OBDEV_SRC_SQL_ENGINE_EXPR_OB_EXPR_STMT_ID_H_ */
