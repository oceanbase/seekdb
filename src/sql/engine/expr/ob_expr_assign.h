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

#ifndef _OCEANBASE_SQL_OB_EXPR_ASSIGN_H_
#define _OCEANBASE_SQL_OB_EXPR_ASSIGN_H_
#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/ob_name_def.h"
namespace oceanbase
{
namespace sql
{
class ObExprAssign : public ObFuncExprOperator
{
public:
  explicit  ObExprAssign(common::ObIAllocator &alloc);
  virtual ~ObExprAssign();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &key,
                                ObExprResType &value,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const override;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprAssign);
};

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_ASSIGN_H_
