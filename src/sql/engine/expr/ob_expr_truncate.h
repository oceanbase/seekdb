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

#ifndef _OB_EXPR_TRUNCATE_H_
#define _OB_EXPR_TRUNCATE_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprTruncate: public ObFuncExprOperator
{
public:
  explicit ObExprTruncate(common::ObIAllocator &alloc);
  virtual ~ObExprTruncate() {};

  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const override;

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const;

  static int do_trunc_decimalint(
      const int16_t in_prec, const int16_t in_scale,
      const int16_t out_prec, const int64_t trunc_scale, const int64_t out_scale,
      const ObDatum &in_datum, ObDecimalIntBuilder &res_val);

  static int calc_trunc_decimalint(
      const int16_t in_prec, const int16_t in_scale,
      const int16_t out_prec, const int64_t trunc_scale, const int16_t out_scale,
      const ObDatum &in_datum, ObDatum &res_datum);

  static int set_trunc_val(common::ObObj &result,
                    common::number::ObNumber &nmb,
                    common::ObExprCtx &expr_ctx,
                    common::ObObjType res_type);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:

  DISALLOW_COPY_AND_ASSIGN(ObExprTruncate);
};
} // namespace sql
} // namespace oceanbase

#endif // _OB_EXPR_TRUNCATE_H_
