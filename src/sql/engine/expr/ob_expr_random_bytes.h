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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_RANDOM_BYTES_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_RANDOM_BYTES_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprRandomBytes : public ObFuncExprOperator
{
public:
  explicit ObExprRandomBytes(common::ObIAllocator &alloc);
  virtual ~ObExprRandomBytes();
  virtual int calc_result_type1(ObExprResType &type, 
                                ObExprResType &len,
                                common::ObExprTypeCtx &type_ctx) const;
  
  virtual int calc_result1(common::ObObj &result,
                           const common::ObObj &len,
                           common::ObExprCtx &expr_ctx) const;
  
  static int generate_random_bytes(const ObExpr &expr, ObEvalCtx &ctx, 
                                   ObDatum &expr_datum);
  
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRandomBytes);
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_RANDOM_BYTES_
