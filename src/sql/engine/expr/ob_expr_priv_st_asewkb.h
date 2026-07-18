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

#ifndef OCEANBASE_SQL_OB_EXPR_PRIV_ST_ASEWKB_
#define OCEANBASE_SQL_OB_EXPR_PRIV_ST_ASEWKB_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprStPrivAsEwkb : public ObFuncExprOperator
{
public:
  explicit ObExprStPrivAsEwkb(common::ObIAllocator &alloc);
  virtual ~ObExprStPrivAsEwkb();
  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx& type_ctx) const override;
  static int eval_priv_st_as_ewkb(const ObExpr &expr,
                                  ObEvalCtx &ctx,
                                  ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprStPrivAsEwkb);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_PRIV_ST_ASEWKB_
