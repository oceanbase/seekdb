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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_array_contains_all.h"
#include "lib/udt/ob_collection_type.h"
#include "lib/udt/ob_array_type.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_result_type_util.h"


using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::omt;

namespace oceanbase
{
namespace sql
{

ObExprArrayContainsAll::ObExprArrayContainsAll(ObIAllocator &alloc)
    : ObExprArrayOverlaps(alloc, T_FUNC_SYS_ARRAY_CONTAINS_ALL, N_ARRAY_CONTAINS_ALL, 2, ObArithExprOperator::NOT_ROW_DIMENSION)
{
}

ObExprArrayContainsAll::~ObExprArrayContainsAll()
{
}

int ObExprArrayContainsAll::eval_array_contains_all(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return eval_array_relations(expr, ctx, CONTAINS_ALL, res);
}

int ObExprArrayContainsAll::eval_array_contains_all_batch(const ObExpr &expr, ObEvalCtx &ctx,
                                                          const ObBitVector &skip, const int64_t batch_size)
{
  return eval_array_relations_batch(expr, ctx, skip, batch_size, CONTAINS_ALL);
}

int ObExprArrayContainsAll::eval_array_contains_all_vector(const ObExpr &expr, ObEvalCtx &ctx,
                                                           const ObBitVector &skip, const EvalBound &bound)
{
  return eval_array_relation_vector(expr, ctx, skip, bound, CONTAINS_ALL);
}

int ObExprArrayContainsAll::cg_expr(ObExprCGCtx &expr_cg_ctx,
                         const ObRawExpr &raw_expr,
                         ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_array_contains_all;
  rt_expr.eval_batch_func_ = eval_array_contains_all_batch;
  rt_expr.eval_vector_func_ = eval_array_contains_all_vector;

  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
