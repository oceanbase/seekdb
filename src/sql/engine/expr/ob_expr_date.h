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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_DATE_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_DATE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprDate : public ObFuncExprOperator
{
public:
  explicit  ObExprDate(common::ObIAllocator &alloc);
  virtual ~ObExprDate();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual common::ObCastMode get_cast_mode() const { return CM_NULL_ON_WARN;}
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_date(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
  static int eval_date_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);
private :
  //disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprDate);
};

inline int ObExprDate::calc_result_type1(ObExprResType &type,
                                         ObExprResType &type1,
                                         common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(type1);
  const ObObjType res_type = type_ctx.enable_mysql_compatible_dates() ?
      common::ObMySQLDateType : common::ObDateType;
  type.set_type(res_type);
  type.set_scale(common::DEFAULT_SCALE_FOR_DATE);
  //set calc type
  type1.set_calc_type(res_type);
  type_ctx.set_cast_mode(type_ctx.get_cast_mode() | CM_NULL_ON_WARN);
  return common::OB_SUCCESS;
}
}
}


#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_DATE_H_ */
