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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_BIT_AND_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_BIT_AND_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprBitAnd : public ObBitwiseExprOperator
{
public:
  explicit ObExprBitAnd(common::ObIAllocator &alloc);
  ObExprBitAnd(common::ObIAllocator &alloc,
               ObExprOperatorType type,
               const char *name,
               int32_t param_num,
               int32_t dimension);
  virtual ~ObExprBitAnd() {};
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprBitAnd);
};

}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_BIT_AND_ */

