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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_QUARTER_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_QUARTER_H_
#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{
// QUARTER(date)
// Returns the quarter of date
// range from 1 to 4
class ObExprQuarter : public ObFuncExprOperator {
public:
  ObExprQuarter();
  explicit ObExprQuarter(common::ObIAllocator& alloc);
  virtual ~ObExprQuarter();
  virtual int calc_result_type1(ObExprResType& type, 
                                ObExprResType& date,
                                common::ObExprTypeCtx& type_ctx) const override;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_quater(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
  private:
    DISALLOW_COPY_AND_ASSIGN(ObExprQuarter);                         
};
}
}
#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_QUARTER_H_ */
