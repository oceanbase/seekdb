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

#ifndef OCEANBASE_SQL_OB_EXPR_BETWEEN_H_
#define OCEANBASE_SQL_OB_EXPR_BETWEEN_H_

#include "lib/ob_name_def.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprBetween: public ObRelationalExprOperator
{
public:
  ObExprBetween();
  explicit  ObExprBetween(common::ObIAllocator &alloc);
  virtual ~ObExprBetween()
  {
  }

  enum EvalBetweenStage {
    BETWEEN_LEFT,
    BETWEEN_RIGHT,
    BETWEEN_MAX
  };

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
              ObExpr &rt_expr) const override;

  static int eval_between_vector(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            const ObBitVector &skip,
                            const EvalBound &bound);

  template <typename LVec, typename RVec, typename ResVec,
            EvalBetweenStage Stage>
  static int inner_eval_between_vector(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            ObBitVector &skip,
                            const EvalBound &bound);
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprBetween);
  // function members
private:
  // data members
};

} // end namespace sql
} // end namespace oceanbase

#endif //OCEANBASE_SQL_OB_EXPR_BETWEEN_H_
