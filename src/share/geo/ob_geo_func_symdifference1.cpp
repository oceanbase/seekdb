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

#define USING_LOG_PREFIX LIB

#include "ob_geo_func_symdifference_helper.ipp"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

int ob_geo_func_symdifference_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result);

// cartesian point
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pt_pt<ObWkbGeomPoint, ObWkbGeomPoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomLineString,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_pt_pt<ObWkbGeomPoint, ObWkbGeomMultiPoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomMultiLineString,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPoint, ObWkbGeomCollection, ObGeometry *)
{
  return apply_bg_symdifference_pt_coll<ObCartesianPoint, ObCartesianGeometrycollection>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

// cartisian linestring
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomLineString,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomLineString,
      ObWkbGeomLineString,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomLineString,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomLineString,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomLineString,
      ObWkbGeomMultiLineString,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomLineString,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomLineString, ObWkbGeomCollection, ObGeometry *)
{
  return apply_bg_symdifference_line_coll<ObCartesianLineString, ObCartesianGeometrycollection>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

// cartisian polygon
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomLineString,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomPolygon, ObWkbGeomPolygon, ObCartesianMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomMultiLineString,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObCartesianMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomCollection, ObGeometry *)
{
  return apply_bg_symdifference_poly_coll<ObCartesianPolygon, ObCartesianGeometrycollection, ObWkbGeomPolygon>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

static int eval_wkb_cart_part1(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  int ret = OB_SUCCESS;
  switch (g1->type()) {
  case common::ObGeoType::POINT:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPoint, ObWkbGeomCollection>::eval(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
      break;
    }
    break;
  case common::ObGeoType::LINESTRING:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomLineString, ObWkbGeomCollection>::eval(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
      break;
    }
    break;
  case common::ObGeoType::POLYGON:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomPolygon, ObWkbGeomCollection>::eval(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
      break;
    }
    break;
  default:
    ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
    break;
  }
  return ret;
}

int eval_wkb_cart_part2(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result);

int ob_geo_func_symdifference_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  int ret = eval_wkb_cart_part1(g1, g2, context, result);
  if (ret == OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS) {
    ret = eval_wkb_cart_part2(g1, g2, context, result);
  }
  return ret;
}

}  // namespace common
}  // namespace oceanbase
