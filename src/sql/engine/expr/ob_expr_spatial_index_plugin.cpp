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

/* Core-only adapters for plugin-owned spatial cell/Mbr/geohash services. */
#include "sql/engine/expr/ob_expr_spatial_cellid.h"
#include "sql/engine/expr/ob_expr_spatial_mbr.h"
#include "sql/engine/expr/ob_expr_priv_st_geohash.h"
#include "sql/engine/expr/ob_plugin_expr_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprSpatialCellid::ObExprSpatialCellid(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_SPATIAL_CELLID, N_SPATIAL_CELLID, 1,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}
ObExprSpatialCellid::~ObExprSpatialCellid() {}
int ObExprSpatialCellid::calc_result_type1(ObExprResType &type, ObExprResType &type1,
                                           ObExprTypeCtx &type_ctx) const
{
  UNUSED(type1); UNUSED(type_ctx);
  type.set_type(ObUInt64Type);
  type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].precision_);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObUInt64Type].scale_);
  return OB_SUCCESS;
}
int ObExprSpatialCellid::calc_result1(ObObj &result, const ObObj &obj, ObExprCtx &ctx) const
{ UNUSED(result); UNUSED(obj); UNUSED(ctx); return OB_NOT_SUPPORTED; }
int ObExprSpatialCellid::eval_spatial_cellid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{ return execute_plugin_geometry_uint64("spatial_cellid", expr, ctx, res); }
int ObExprSpatialCellid::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{ rt_expr.eval_func_ = eval_spatial_cellid; return OB_SUCCESS; }

ObExprSpatialMbr::ObExprSpatialMbr(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_SPATIAL_MBR, N_SPATIAL_MBR, 1,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}
ObExprSpatialMbr::~ObExprSpatialMbr() {}
int ObExprSpatialMbr::calc_result_type1(ObExprResType &type, ObExprResType &type1,
                                        ObExprTypeCtx &type_ctx) const
{
  UNUSED(type1); UNUSED(type_ctx);
  type.set_varchar();
  type.set_length(OB_DEFAULT_MBR_SIZE);
  type.set_collation_type(CS_TYPE_BINARY);
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  return OB_SUCCESS;
}
int ObExprSpatialMbr::calc_result1(ObObj &result, const ObObj &obj, ObExprCtx &ctx) const
{ UNUSED(result); UNUSED(obj); UNUSED(ctx); return OB_NOT_SUPPORTED; }
int ObExprSpatialMbr::eval_spatial_mbr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{ return execute_plugin_geometry_bytes("spatial_mbr", expr, ctx, res, true); }
int ObExprSpatialMbr::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{ rt_expr.eval_func_ = eval_spatial_mbr; return OB_SUCCESS; }

ObExprPrivSTGeoHash::ObExprPrivSTGeoHash(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_GEOHASH, N_PRIV_ST_GEOHASH,
                         MORE_THAN_ZERO, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{}
ObExprPrivSTGeoHash::~ObExprPrivSTGeoHash() {}
int ObExprPrivSTGeoHash::calc_result_typeN(ObExprResType &type, ObExprResType *types,
                                           int64_t param_num, ObExprTypeCtx &type_ctx) const
{
  UNUSED(types); UNUSED(param_num);
  type.set_varchar();
  type.set_collation_type(type_ctx.get_coll_type());
  type.set_collation_level(CS_LEVEL_IMPLICIT);
  return OB_SUCCESS;
}
int ObExprPrivSTGeoHash::eval_priv_st_geohash(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{ return execute_plugin_geometry_bytes("st_geohash", expr, ctx, res, true); }
int ObExprPrivSTGeoHash::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{ rt_expr.eval_func_ = eval_priv_st_geohash; return OB_SUCCESS; }

} // namespace sql
} // namespace oceanbase
