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

#ifndef _OB_SQL_EXPR_ROW_COUNT_H_
#define _OB_SQL_EXPR_ROW_COUNT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprRowCount : public ObFuncExprOperator
{
public:
  explicit  ObExprRowCount(common::ObIAllocator &alloc);
  virtual ~ObExprRowCount();
  virtual int calc_result_type0(ObExprResType &type, common::ObExprTypeCtx &type_ctx) const;
  static int eval_row_count(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRowCount);
};

}
}
#endif /* _OB_SQL_EXPR_ROW_COUNT_H_ */
