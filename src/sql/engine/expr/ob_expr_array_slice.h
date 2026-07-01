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

#ifndef OCEANBASE_SQL_OB_EXPR_ARRAY_SLICE
#define OCEANBASE_SQL_OB_EXPR_ARRAY_SLICE

#include "lib/udt/ob_array_type.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprArraySlice : public ObFuncExprOperator
{
public:
  explicit ObExprArraySlice(common::ObIAllocator &alloc);
  virtual ~ObExprArraySlice();

  virtual int calc_result_typeN(ObExprResType &type, ObExprResType *types, int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_array_slice(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  static int eval_array_slice_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                    const int64_t batch_size);
  static int eval_array_slice_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip,
                                     const EvalBound &bound);

  static int get_subarray(ObIArrayType *&res_arr, ObIArrayType *src_arr, int64_t offset,
                          int64_t len, bool has_len);

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &expr) const override;

private:
  DISALLOW_COPY_AND_ASSIGN(ObExprArraySlice);
};

} // namespace sql
} // namespace oceanbase

#endif
