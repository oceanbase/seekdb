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

#ifndef _OB_EXPR_NOT_H_
#define _OB_EXPR_NOT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprNot : public ObLogicalExprOperator
{
public:
  explicit  ObExprNot(common::ObIAllocator &alloc);
  virtual ~ObExprNot() {};

  virtual int calc_result_type1(ObExprResType &type,
                        ObExprResType &type1,
                        common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_not(const ObExpr &expr,
                      ObEvalCtx &ctx,
                      ObDatum &expr_datum);
  static int eval_not_batch(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const int64_t batch_size);
  static int eval_not_vector( const ObExpr &expr,
                              ObEvalCtx &ctx,
                              const ObBitVector &skip,
                              const EvalBound &bound);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprNot) const;
};
}
}
#endif  /* _OB_EXPR_NOT_H_ */
