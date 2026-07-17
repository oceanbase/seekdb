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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_POW_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_POW_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprPow : public ObExprOperator
{
public:
  explicit  ObExprPow(common::ObIAllocator &alloc);
  virtual ~ObExprPow() {};

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc(common::ObObj &result,
                  const common::ObObj &obj1,
                  const common::ObObj &obj2,
                  common::ObExprCtx &expr_ctx);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
  static int safe_set_double(common::ObDatum &datum, double value);
  static int calc_pow_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprPow) const;
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_POW_
