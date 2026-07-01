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

#ifndef OCEANBASE_SQL_OB_EXPR_ARRAY_CONTAINS_ALL
#define OCEANBASE_SQL_OB_EXPR_ARRAY_CONTAINS_ALL

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/geo/ob_geo_utils.h"
#include "lib/udt/ob_array_type.h"
#include "sql/engine/expr/ob_expr_array_overlaps.h"


namespace oceanbase
{
namespace sql
{
class ObExprArrayContainsAll : public ObExprArrayOverlaps
{
public:
  explicit ObExprArrayContainsAll(common::ObIAllocator &alloc);
  virtual ~ObExprArrayContainsAll();
  static int eval_array_contains_all(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_array_contains_all_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size);
  static int eval_array_contains_all_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                            const ObBitVector &skip, const EvalBound &bound);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  
  DISALLOW_COPY_AND_ASSIGN(ObExprArrayContainsAll);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_ARRAY_CONTAINS_ALL
