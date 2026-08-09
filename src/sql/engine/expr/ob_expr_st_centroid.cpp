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

#include "ob_expr_st_centroid.h"
#if SEEKDB_ENABLE_CORE_GIS
#include "share/geo/ob_geo_func_register.h"
#include "sql/engine/expr/ob_geo_expr_utils.h"
#else
#include "sql/engine/expr/ob_plugin_expr_utils.h"
#endif

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprSTCentroid::ObExprSTCentroid(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_ST_CENTROID, N_ST_CENTROID, 1, VALID_FOR_GENERATED_COL,
          NOT_ROW_DIMENSION)
{}

int ObExprSTCentroid::calc_result_type1(
    ObExprResType &type, ObExprResType &type1, common::ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (ob_is_null(type1.get_type())) {
    // do nothing
  } else if (ob_is_numeric_type(type1.get_type())) {
    type1.set_calc_type(ObLongTextType);
  } else if (!ob_is_geometry(type1.get_type()) && !ob_is_string_type(type1.get_type())) {
    ret = OB_ERR_GIS_INVALID_DATA;
    LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, get_name());
  }
  if (OB_SUCC(ret)) {
    type.set_geometry();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
  }
  return ret;
}

int ObExprSTCentroid::eval_st_centroid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
#if !SEEKDB_ENABLE_CORE_GIS
  return execute_plugin_geometry_bytes("st_centroid", expr, ctx, res, true);
#else
  int ret = OB_SUCCESS;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor tmp_allocator(tmp_alloc_g.get_allocator());
  ObDatum *datum = NULL;
  bool is_null_result = false;
  common::ObSrsCacheGuard srs_guard;
  const ObSrsItem *srs = NULL;
  ObGeometry *geo = NULL;
  ObGeometry *res_geo = nullptr;

  if (OB_FAIL(tmp_allocator.eval_arg(expr.args_[0], ctx, datum))) {
  } 
  ObGeoBoostAllocGuard guard{};
  lib::MemoryContext *mem_ctx = nullptr;
  if (OB_FAIL(ret)) {
  } else if (datum->is_null()) {
    is_null_result = true;
  } else {
    ObString wkb = datum->get_string();
    if (OB_FAIL(ObTextStringHelper::read_real_string_data_with_copy(ctx.exec_ctx_, tmp_allocator,
            *datum,
            expr.args_[0]->datum_meta_,
            expr.args_[0]->obj_meta_.has_lob_header(),
            wkb))) {
    } else if (OB_FAIL(
                   ObGeoExprUtils::get_srs_item(ctx, srs_guard, wkb, srs, true, N_ST_CENTROID))) {
    } else if (OB_FAIL(ObGeoExprUtils::build_geometry(tmp_allocator,
                   wkb,
                   geo,
                   srs,
                   N_ST_CENTROID,
                   GEO_NORMALIZE | GEO_CHECK_RANGE | GEO_NOT_COPY_WKB))) {
    } else if (OB_FAIL(ObGeoExprUtils::check_empty(geo, is_null_result))) {
    } else if (is_null_result) {
    } else if (OB_FAIL(guard.init())) {
    } else if (OB_ISNULL(mem_ctx = guard.get_memory_ctx())) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("fail to get mem ctx", K(ret));
    } else {
      bool is_valid = true;
      if (geo->crs() == ObGeoCRS::Cartesian
        && OB_FAIL((ObGeoTypeUtil::is_polygon_valid_simple<ObCartesianPolygon, ObCartesianMultipolygon, ObCartesianGeometrycollection>(geo, is_valid)))) {
        LOG_WARN("fail to check if geometry contain polygon", K(ret));
      } else if (geo->crs() == ObGeoCRS::Geographic
        && OB_FAIL((ObGeoTypeUtil::is_polygon_valid_simple<ObGeographPolygon, ObGeographMultipolygon, ObGeographGeometrycollection>(geo, is_valid)))) {
        LOG_WARN("fail to check if geometry contain polygon", K(ret));
      } else if (!is_valid) {
        ret = OB_ERR_GIS_INVALID_DATA;
        LOG_USER_ERROR(OB_ERR_GIS_INVALID_DATA, N_ST_CENTROID);
        LOG_WARN("input geometry is invalid", K(ret));
      }
      ObGeoEvalCtx gis_context(*mem_ctx, srs);
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_FAIL(ObGeoTypeUtil::correct_polygon(tmp_allocator, srs, true, *geo))) {
      } else if (OB_FAIL(gis_context.append_geo_arg(geo))) {
      } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Centroid>::geo_func::eval(gis_context, res_geo))) {
        LOG_WARN("eval geo func centroid failed", K(ret), K(geo->type()));
        if (ret == OB_ERR_BOOST_GEOMETRY_CENTROID_EXCEPTION) {
          ret = OB_SUCCESS;
          is_null_result = true;
        } else {
          ObGeoExprUtils::geo_func_error_handle(ret, N_ST_CENTROID);
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    if (is_null_result) {
      res.set_null();
    } else {
      ObString res_wkb;
      if (OB_FAIL(ObGeoExprUtils::geo_to_wkb(*res_geo, expr, ctx, srs, res_wkb, geo->get_srid()))) {
      } else {
        res.set_string(res_wkb);
      }
    }
  }

  return ret;
#endif
}

int ObExprSTCentroid::cg_expr(
    ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const
{
  UNUSEDx(expr_cg_ctx, raw_expr);
  rt_expr.eval_func_ = eval_st_centroid;
  return OB_SUCCESS;
}

}  // namespace sql
}  // namespace oceanbase
