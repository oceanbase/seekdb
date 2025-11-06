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

#ifndef SQL_ENGINE_EXPR_OB_EXPR_IS_SERVING_TENANT_
#define SQL_ENGINE_EXPR_OB_EXPR_IS_SERVING_TENANT_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprIsServingTenant : public ObFuncExprOperator
{
public:
  explicit  ObExprIsServingTenant(common::ObIAllocator &alloc);
  virtual ~ObExprIsServingTenant();

  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprResType &type3,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_is_serving_tenant(const ObExpr &expr,
                                    ObEvalCtx &ctx,
                                    ObDatum &expr_datum);
private:
  static int check_serving_tenant(bool &serving,
                                  ObExecContext &exec_ctx,
                                  const common::ObString &ip,
                                  const int64_t port,
                                  const uint64_t tenant_id);
  /* functions */
  /* variables */
  DISALLOW_COPY_AND_ASSIGN(ObExprIsServingTenant);
};
}
}
#endif /* SQL_ENGINE_EXPR_OB_EXPR_IS_SERVING_TENANT_ */

