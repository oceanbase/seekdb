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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_LENGTH_
#define OCEANBASE_SQL_ENGINE_EXPR_LENGTH_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprLength : public ObFuncExprOperator
{
public:
  explicit  ObExprLength(common::ObIAllocator &alloc);
  virtual ~ObExprLength();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &text,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_null(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int calc_mysql_mode(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int calc_mysql_length_vector(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      const ObBitVector &skip,
                                      const EvalBound &bound);

private:
  template <typename ArgVec, typename ResVec>
  static int calc_mysql_length_vector_dispatch(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      const ObBitVector &skip,
                                      const EvalBound &bound);
  template <typename ArgVec, typename ResVec>
  static int calc_oracle_length_vector_dispatch(const ObExpr &expr,
                                      ObEvalCtx &ctx,
                                      const ObBitVector &skip,
                                      const EvalBound &bound);
  DISALLOW_COPY_AND_ASSIGN(ObExprLength);
};
}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_LENGTH_ */
