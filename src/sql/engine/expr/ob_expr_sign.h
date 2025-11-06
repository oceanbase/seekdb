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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_SIGN_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_SIGN_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprSign : public ObFuncExprOperator
{
public:
  explicit  ObExprSign(common::ObIAllocator &alloc);
  virtual ~ObExprSign();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &text,
                                common::ObExprTypeCtx &type_ctx) const;
  int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
              ObExpr &rt_expr) const;
private:
  //help func
  DISALLOW_COPY_AND_ASSIGN(ObExprSign);
private:
};

}
}
#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_SIGN_H_ */
