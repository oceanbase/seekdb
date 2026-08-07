/*
 * Copyright (c) 2026 OceanBase.
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

/* Core-only adapter for the plugin-owned SRS transform service. */
#include "sql/engine/expr/ob_expr_st_transform.h"
#include "sql/engine/expr/ob_plugin_expr_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprSTTransform::ObExprSTTransform(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_TRANSFORM, N_ST_TRANSFORM, 2,
                         VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}

int ObExprSTTransform::calc_result_type2(ObExprResType &type,
                                         ObExprResType &type1,
                                         ObExprResType &type2,
                                         ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  if (!ob_is_null(type1.get_type()) && !ob_is_geometry(type1.get_type()) &&
      !ob_is_string_type(type1.get_type())) {
    return OB_ERR_GIS_INVALID_DATA;
  }
  type2.set_calc_type(ObIntType);
  type.set_geometry();
  type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
  return OB_SUCCESS;
}

int ObExprSTTransform::eval_st_transform(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return execute_plugin_geometry_transform("st_transform", expr, ctx, res);
}

int ObExprSTTransform::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{
  rt_expr.eval_func_ = eval_st_transform;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
