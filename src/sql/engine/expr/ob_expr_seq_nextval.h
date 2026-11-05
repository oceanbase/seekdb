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

#ifndef _OB_EXPR_SEQ_VALUE_H
#define _OB_EXPR_SEQ_VALUE_H
#include "sql/engine/expr/ob_expr_operator.h"
#include "share/ob_i_sql_expression.h"

namespace oceanbase
{
namespace sql
{
class ObPhysicalPlanCtx;
class ObExprSeqNextval : public ObFuncExprOperator
  {
  public:
    explicit  ObExprSeqNextval(common::ObIAllocator &alloc);
    virtual ~ObExprSeqNextval();
    int calc_result_type1(ObExprResType &type,
                          ObExprResType &type1,
                          common::ObExprTypeCtx &type_ctx) const;
    int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const;
    static int calc_sequence_nextval(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  private:
    // disallow copy
    DISALLOW_COPY_AND_ASSIGN(ObExprSeqNextval);

  };
}//end namespace sql
}//end namespace oceanbase
#endif
