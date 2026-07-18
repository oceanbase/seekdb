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

#ifndef OCEANBASE_SQL_OB_EXPR_SPATIAL_MBR_
#define OCEANBASE_SQL_OB_EXPR_SPATIAL_MBR_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprSpatialMbr : public ObFuncExprOperator
{
public:
  explicit ObExprSpatialMbr(common::ObIAllocator &alloc);
  virtual ~ObExprSpatialMbr();
  virtual int calc_result_type1(ObExprResType &type,
                                ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const override;
  virtual int calc_result1(common::ObObj &result,
                           const common::ObObj &obj,
                           common::ObExprCtx &expr_ctx) const;
  static int eval_spatial_mbr(const ObExpr &expr,
                              ObEvalCtx &ctx,
                              ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSpatialMbr);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_SPATIAL_MBR_
