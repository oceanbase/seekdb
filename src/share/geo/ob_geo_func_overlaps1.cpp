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

#include "ob_geo_func_overlaps_helper.ipp"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========BIN CART==========
/*Point*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomPoint, ObWkbGeomPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomPoint, ObWkbGeomMultiPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

/*Linestring*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomLineString, ObWkbGeomLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomLineString, ObWkbGeomLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomLineString, ObWkbGeomMultiLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

/*Polygon*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomPolygon, ObWkbGeomPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomPolygon, ObWkbGeomPolygon>(g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomPolygon, ObWkbGeomMultiPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

/*MultiPoint*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomMultiPoint, ObWkbGeomMultiPoint>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

/*MultiLineString*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

/*MultiPolygon*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomMultiPolygon, ObWkbGeomPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomMultiPolygon, ObWkbGeomPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

int ob_geo_func_overlaps_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
{
  return ObGeoFuncOverlapsImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

extern int ob_geo_func_overlaps_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result);

/*Collection*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeomCollection, ObWkbGeomCollection, ObGeoFuncResWithNull)
{
  return eval_overlaps_gc_other<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_overlaps_eval_tree);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncOverlapsImpl, ObWkbGeomCollection, ObGeoFuncResWithNull)
{
  return eval_overlaps_gc_other<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_overlaps_eval_tree);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncOverlapsImpl, ObWkbGeomCollection, ObGeoFuncResWithNull)
{
  return eval_overlaps_gc_other<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_overlaps_eval_tree);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
