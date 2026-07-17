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

#ifndef _OCEANBASE_SQL_OB_EXPR_DATE_DIFF_H_
#define _OCEANBASE_SQL_OB_EXPR_DATE_DIFF_H_
#include "lib/ob_name_def.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprDateDiff : public ObFuncExprOperator
{
public:
  explicit  ObExprDateDiff(common::ObIAllocator &alloc);
  virtual ~ObExprDateDiff();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &left,
                                ObExprResType &right,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual common::ObCastMode get_cast_mode() const { return CM_NULL_ON_WARN;}
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_date_diff(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
  static int eval_date_diff_vector(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const EvalBound &bound);
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprDateDiff);
};

inline int ObExprDateDiff::calc_result_type2(ObExprResType &type,
                                             ObExprResType &left,
                                             ObExprResType &right,
                                             common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(left);
  UNUSED(right);
  type.set_int();
  type.set_precision(common::ObAccuracy::DDL_DEFAULT_ACCURACY[common::ObIntType].precision_);
  type.set_scale(common::DEFAULT_SCALE_FOR_INTEGER);
  //set calc type
  // `DateDiff` is more friendly for `ObDateType` calculation rather than `ObMySQLDateType`,
  // so only when `ObMySQLDateType` input exists, we casting both sides to `ObMySQLDateType`
  // calculation.
  if (ob_is_mysql_compact_dates_type(left.get_type()) ||
        ob_is_mysql_compact_dates_type(right.get_type())) {
    left.set_calc_type(common::ObMySQLDateType);
    right.set_calc_type(common::ObMySQLDateType);
  } else {
    left.set_calc_type(common::ObDateType);
    right.set_calc_type(common::ObDateType);
  }
  return common::OB_SUCCESS;
}

} //sql
} //oceanbase
#endif //_OCEANBASE_SQL_OB_EXPR_DATE_DIFF_H_
