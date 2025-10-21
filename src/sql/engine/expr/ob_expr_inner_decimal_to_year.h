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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_INNER_DECIMAL_TO_YEAR_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_INNER_DECIMAL_TO_YEAR_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
//This expression is used to extract double to int range.
//inner_decimal_to_year(val)
//for example: inner_decimal_to_year(1991) = 1991 
class ObExprInnerDecimalToYear : public ObFuncExprOperator
{
public:
  explicit  ObExprInnerDecimalToYear(common::ObIAllocator &alloc);
  virtual ~ObExprInnerDecimalToYear() {};

  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_inner_decimal_to_year(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprInnerDecimalToYear) const;
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_INNER_DECIMAL_TO_YEAR_
