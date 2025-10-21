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

#ifndef OCEANBASE_SQL_EXPR_FUNC_RIGHT_
#define OCEANBASE_SQL_EXPR_FUNC_RIGHT_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprRight : public ObStringExprOperator
{
public:
  explicit  ObExprRight(common::ObIAllocator &alloc);
  virtual ~ObExprRight();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &text,
                                ObExprResType &start_pos,
                                common::ObExprTypeCtx &type_ctx) const;
  int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const;
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprRight);
};

}
}

#endif //OCEANBASE_SQL_EXPR_FUNC_RIGHT_
