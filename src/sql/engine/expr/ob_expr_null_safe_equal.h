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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_NULL_SAFE_EQUAL_H_
#define OCEANBASE_SQL_ENGINE_EXPR_NULL_SAFE_EQUAL_H_

#include "lib/ob_name_def.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprNullSafeEqual: public ObRelationalExprOperator
{
public:
  ObExprNullSafeEqual();
  explicit  ObExprNullSafeEqual(common::ObIAllocator &alloc);
  virtual ~ObExprNullSafeEqual() {};

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
              ObExpr &rt_expr) const override;

  static int ns_equal_eval(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datm);
  static int row_ns_equal_eval(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datm);

  static int ns_equal(const ObExpr &expr, ObDatum &res,
                      ObExpr **left, ObEvalCtx &lctx, ObExpr **right, ObEvalCtx &rctx);

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprNullSafeEqual);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_SQL_ENGINE_EXPR_NULL_SAFE_EQUAL_H_ */
