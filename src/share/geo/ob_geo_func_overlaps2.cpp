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

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========BIN GEOG==========
/*Point*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogPoint, ObWkbGeogPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

/*Linestring*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogLineString, ObWkbGeogLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogMultiLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

/*Polygon*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogPolygon, ObWkbGeogPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogPolygon, ObWkbGeogPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogPolygon, ObWkbGeogMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogPolygon, ObWkbGeogMultiPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

/*MultiPoint*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogMultiPoint, ObWkbGeogMultiPoint>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

/*MultiLineString*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogMultiLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

/*MultiPolygon*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogMultiPolygon, ObWkbGeogPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogMultiPolygon, ObWkbGeogPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

int ob_geo_func_overlaps_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
{
  return ObGeoFuncOverlapsImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

extern int ob_geo_func_overlaps_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result);

/*Collection*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObWkbGeogCollection, ObWkbGeogCollection, ObGeoFuncResWithNull)
{
  return eval_overlaps_gc_other<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_overlaps_eval_tree);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncOverlapsImpl, ObWkbGeogCollection, ObGeoFuncResWithNull)
{
  return eval_overlaps_gc_other<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_overlaps_eval_tree);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncOverlapsImpl, ObWkbGeogCollection, ObGeoFuncResWithNull)
{
  return eval_overlaps_gc_other<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_overlaps_eval_tree);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
