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

extern int ob_geo_func_symdifference_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result);

// cartisian multipoint
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pt_pt<ObWkbGeomPoint, ObWkbGeomMultiPoint, ObCartesianMultipoint>(
      g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomLineString,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_pt_pt<ObWkbGeomMultiPoint,
      ObWkbGeomMultiPoint,
      ObCartesianMultipoint>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomMultiLineString,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomCollection, ObGeometry *)
{
  return apply_bg_symdifference_pt_coll<ObCartesianMultipoint, ObCartesianGeometrycollection>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

// cartisian mutilinestring
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomMultiLineString,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomLineString,
      ObWkbGeomMultiLineString,
      ObCartesianMultilinestring>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomMultiLineString,
      ObWkbGeomPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomMultiLineString,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomMultiLineString,
      ObWkbGeomMultiLineString,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomMultiLineString,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomCollection, ObGeometry *)
{
  return apply_bg_symdifference_line_coll<ObCartesianMultilinestring,
      ObCartesianGeometrycollection>(g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

// cartisian multipolygon
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pl_pa<ObWkbGeomPoint,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomLineString,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObCartesianMultipolygon>(
      g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_mpl_mpa<ObWkbGeomMultiPoint,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference_la<ObWkbGeomMultiLineString,
      ObWkbGeomMultiPolygon,
      ObCartesianGeometrycollection>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference<ObWkbGeomMultiPolygon,
      ObWkbGeomMultiPolygon,
      ObCartesianMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomCollection, ObGeometry *)
{
  return apply_bg_symdifference_poly_coll<ObCartesianMultipolygon, ObCartesianGeometrycollection, ObWkbGeomMultiPolygon>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

// cartesian collection
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_symdifference_pt_coll<ObCartesianPoint, ObCartesianGeometrycollection>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_symdifference_line_coll<ObCartesianLineString, ObCartesianGeometrycollection>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_symdifference_poly_coll<ObCartesianPolygon, ObCartesianGeometrycollection, ObWkbGeomPolygon>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_symdifference_pt_coll<ObCartesianMultipoint, ObCartesianGeometrycollection>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_symdifference_line_coll<ObCartesianMultilinestring,
      ObCartesianGeometrycollection>(g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_symdifference_poly_coll<ObCartesianMultipolygon, ObCartesianGeometrycollection, ObWkbGeomMultiPolygon>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncSymDifferenceImpl, ObWkbGeomCollection, ObWkbGeomCollection, ObGeometry *)
{
  return eval_symdifference_gc_gc<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_symdifference_eval_wkb_cart);
}
OB_GEO_FUNC_END;

int eval_wkb_cart_part2(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  int ret = OB_SUCCESS;
  switch (g1->type()) {
  case common::ObGeoType::MULTIPOINT:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPoint, ObWkbGeomCollection>::eval(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
      break;
    }
    break;
  case common::ObGeoType::MULTILINESTRING:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiLineString, ObWkbGeomCollection>::eval(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
      break;
    }
    break;
  case common::ObGeoType::MULTIPOLYGON:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomMultiPolygon, ObWkbGeomCollection>::eval(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
      break;
    }
    break;
  case common::ObGeoType::GEOMETRYCOLLECTION:
    switch (g2->type()) {
    case common::ObGeoType::POINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::LINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::POLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOINT:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomMultiPoint>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTILINESTRING:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomMultiLineString>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::MULTIPOLYGON:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomMultiPolygon>::eval(g1, g2, context, result);
      break;
    case common::ObGeoType::GEOMETRYCOLLECTION:
      ret = ObGeoFuncSymDifferenceImpl::template EvalWkbBi<ObWkbGeomCollection, ObWkbGeomCollection>::eval(g1, g2, context, result);
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

}  // namespace common
}  // namespace oceanbase
