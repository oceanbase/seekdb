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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_spatial_cellid.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprSpatialCellid::ObExprSpatialCellid(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_SPATIAL_CELLID,
                         N_SPATIAL_CELLID,
                         1,
                         NOT_VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{

}

ObExprSpatialCellid::~ObExprSpatialCellid()
{

}

int ObExprSpatialCellid::calc_result_type1(ObExprResType &type,
                                           ObExprResType &type1,
                                           common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  type.set_type(ObUInt64Type);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].precision_);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].scale_);
  return ret;
}

// for old sql engine
int ObExprSpatialCellid::calc_result1(common::ObObj &result,
                                      const common::ObObj &obj,
                                      common::ObExprCtx &expr_ctx) const
{
  UNUSED(obj);
  UNUSED(expr_ctx);
  result.set_null();
  return OB_SUCCESS;
}

int ObExprSpatialCellid::eval_spatial_cellid(const ObExpr &expr,
                                             ObEvalCtx &ctx,
                                             ObDatum &res)
{
  UNUSED(expr);
  UNUSED(ctx);
  res.set_null();
  return OB_SUCCESS;
}

int ObExprSpatialCellid::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                 const ObRawExpr &raw_expr,
                                 ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_spatial_cellid;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
