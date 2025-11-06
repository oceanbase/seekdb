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

#ifndef OCEANBASE_SQL_OB_EXPR_SCN_TO_TIMESTAMP_H_
#define OCEANBASE_SQL_OB_EXPR_SCN_TO_TIMESTAMP_H_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprScnToTimestamp : public ObFuncExprOperator
{
public:
  explicit ObExprScnToTimestamp(common::ObIAllocator &alloc);
  virtual ~ObExprScnToTimestamp();
  virtual int calc_result_type1(ObExprResType &type, ObExprResType &usec,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprScnToTimestamp);
};

} //sql
} //oceanbase
#endif //OCEANBASE_SQL_OB_EXPR_SCN_TO_TIMESTAMP_H_
