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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_IP2INT_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_IP2INT_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprIp2int : public ObFuncExprOperator
{
public:
  explicit  ObExprIp2int(common::ObIAllocator &alloc);
  virtual ~ObExprIp2int();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &text,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int ip2int_varchar(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  // helper func
  template <typename T>
  static int ip2int(T &result, const common::ObString &text);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprIp2int);
};

inline int ObExprIp2int::calc_result_type1(ObExprResType &type,
                                           ObExprResType &text,
                                           common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(text);
  type.set_int();
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);
  type.set_scale(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].scale_);
  //set calc type
  text.set_calc_type(common::ObVarcharType);
  return common::OB_SUCCESS;
}
}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_IP2INT_ */
