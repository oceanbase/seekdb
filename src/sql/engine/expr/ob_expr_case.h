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

#ifndef OCEANBASE_OB_SQL_EXPR_CASE_H_
#define OCEANBASE_OB_SQL_EXPR_CASE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

#define CHECK_WHEN_EXPR_TYPE (1ULL)
#define NEED_CHECK_WHEN_EXPR_TYPE(flag) (((flag) & CHECK_WHEN_EXPR_TYPE) != 0)

class ObExprCase : public ObExprOperator
{
public:
  explicit ObExprCase(common::ObIAllocator &alloc);
  virtual ~ObExprCase();

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc_case_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_case_batch(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const int64_t batch_size);
  static int eval_case_vector(const ObExpr &expr,
                             ObEvalCtx &ctx,
                             const ObBitVector &skip,
                             const EvalBound &bound);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprCase);
};
}
}
#endif // OCEANBASE_OB_SQL_EXPR_CASE_H_
