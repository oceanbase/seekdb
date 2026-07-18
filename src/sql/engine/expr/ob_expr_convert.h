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

#include "sql/engine/expr/ob_expr_operator.h"
#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_CONVERT_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_CONVERT_H_

namespace oceanbase
{
namespace sql
{
class ObExprConvert : public ObFuncExprOperator
{
public:
  explicit  ObExprConvert(common::ObIAllocator &alloc);
  virtual ~ObExprConvert();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                common::ObExprTypeCtx &type_ctx) const;

  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                           ObExpr &rt_expr) const override;
  virtual common::ObCastMode get_cast_mode() const override { return CM_CHARSET_CONVERT_IGNORE_ERR; }; 
  DECLARE_SET_LOCAL_SESSION_VARS;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprConvert);
};
}
}

#endif /* SRC_SQL_ENGINE_EXPR_OB_EXPR_CONVERT_H_ */
