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

#ifndef OCEANBASE_EXPR_TIMESTAMP_H
#define OCEANBASE_EXPR_TIMESTAMP_H

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_to_temporal_base.h"

namespace oceanbase
{
namespace sql
{
class ObExprTimestamp : public ObFuncExprOperator
{
public:
  explicit ObExprTimestamp(common::ObIAllocator &alloc);
  virtual ~ObExprTimestamp();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual common::ObCastMode get_cast_mode() const { return CM_NULL_ON_WARN;}
  static int calc_timestamp1(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result);
  static int calc_timestamp2(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private :
  //disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprTimestamp);
};

}
}

#endif // OCEANBASE_EXPR_TIMESTAMP_H
