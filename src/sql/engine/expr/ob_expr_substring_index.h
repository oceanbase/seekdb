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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_SUBSTRING_INDEX_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_SUBSTRING_INDEX_

#include "lib/container/ob_array.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprSubstringIndex : public ObStringExprOperator
{
public:
  explicit  ObExprSubstringIndex(common::ObIAllocator &alloc);
  virtual ~ObExprSubstringIndex();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &str,
                                ObExprResType &delim,
                                ObExprResType &count,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_substring_index(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  static int eval_substring_index_batch(const ObExpr &expr,
                                        ObEvalCtx &ctx,
                                        const ObBitVector &skip,
                                        const int64_t batch_size);
  virtual bool need_rt_ctx() const override { return true; }
  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSubstringIndex);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_SUBSTRING_INDEX_
