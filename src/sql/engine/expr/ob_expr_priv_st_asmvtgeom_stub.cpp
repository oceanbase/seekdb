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
#include "sql/engine/expr/ob_expr_priv_st_asmvtgeom.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
namespace
{
int mvt_not_supported()
{
  int ret = OB_NOT_SUPPORTED;
  LOG_USER_ERROR(OB_NOT_SUPPORTED, N_PRIV_ST_ASMVTGEOM);
  return ret;
}
} // namespace

ObExprPrivSTAsMVTGeom::ObExprPrivSTAsMVTGeom(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_ASMVTGEOM, N_PRIV_ST_ASMVTGEOM, MORE_THAN_ONE,
          NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}

ObExprPrivSTAsMVTGeom::~ObExprPrivSTAsMVTGeom()
{}

int ObExprPrivSTAsMVTGeom::calc_result_typeN(ObExprResType &type, ObExprResType *types_stack,
    int64_t param_num, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type);
  UNUSED(types_stack);
  UNUSED(param_num);
  UNUSED(type_ctx);
  return mvt_not_supported();
}

int ObExprPrivSTAsMVTGeom::eval_priv_st_asmvtgeom(
    const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(res);
  return mvt_not_supported();
}

int ObExprPrivSTAsMVTGeom::cg_expr(
    ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_priv_st_asmvtgeom;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
