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

#ifndef OCEANBASE_SQL_OB_EXPR_RB_CONTAINS_
#define OCEANBASE_SQL_OB_EXPR_RB_CONTAINS_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprRbContains : public ObFuncExprOperator
{
public:
  explicit ObExprRbContains(common::ObIAllocator &alloc);
  virtual ~ObExprRbContains();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx)
                                const override;
  static int eval_rb_contains(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_rb_contains_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                          const ObBitVector &skip, const EvalBound &bound);
  static int rb_contains(ObIAllocator &allocator,
                         ObString &rb1_bin, 
                         ObString &rb2_bin, 
                         bool &is_contains, 
                         bool is_offset = false, 
                         uint64_t offset = 0);
  
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbContains);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_RB_CONTAINS_
