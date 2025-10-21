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

#ifndef OCEANBASE_SQL_OB_EXPR_RB_TO_ARRAY_
#define OCEANBASE_SQL_OB_EXPR_RB_TO_ARRAY_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/roaringbitmap/ob_roaringbitmap.h"


namespace oceanbase
{
namespace sql
{
class ObExprRbToArray : public ObFuncExprOperator
{
public:
  explicit ObExprRbToArray(common::ObIAllocator &alloc);
  virtual ~ObExprRbToArray();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx)
                                const override;
  static int eval_rb_to_array(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_rb_to_array_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                          const ObBitVector &skip, const EvalBound &bound);
  static int rb_to_array(const ObExpr &expr, 
                         ObEvalCtx &ctx,
                         ObIAllocator &alloc, 
                         ObRoaringBitmap *&rb,
                         ObIArrayType *&arr_res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRbToArray);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_RB_TO_ARRAY_
