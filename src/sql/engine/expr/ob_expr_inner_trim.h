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

#ifndef _OB_SQL_EXPR_INNER_TRIM_H_
#define _OB_SQL_EXPR_INNER_TRIM_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprInnerTrim : public ObStringExprOperator
{
public:
  explicit  ObExprInnerTrim(common::ObIAllocator &alloc);
  virtual ~ObExprInnerTrim();
  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &trim_type,
                                ObExprResType &trim_pattern,
                                ObExprResType &text,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprInnerTrim);
};

}
}
#endif /* _OB_SQL_EXPR_TRIM_H_ */
