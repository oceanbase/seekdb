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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR__CONV_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR__CONV_H_
#include "sql/engine/expr/ob_expr_operator.h"
namespace oceanbase
{
namespace sql
{
class ObExprConv : public ObStringExprOperator
{
public:
  explicit  ObExprConv(common::ObIAllocator &alloc);
  virtual ~ObExprConv();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprResType &type3,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_conv(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  static const int16_t MIN_BASE = 2;
  static const int16_t MAX_BASE = 36;
  static const int16_t MAX_LENGTH = 65;
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprConv);
};
}
}

#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR__CONV_H_ */
