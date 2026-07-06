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

#include "ob_geo_func_equals_helper.ipp"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========BIN GEOG==========
// Geog Point
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogPoint, ObWkbGeogPoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObWkbGeogPoint, ObWkbGeogPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObWkbGeogPoint, ObWkbGeogMultiPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geog LineString
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogLineString, ObWkbGeogLineString, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogLineString>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogMultiLineString>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// Geog Polygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogPolygon, ObWkbGeogPolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogPolygon, ObWkbGeogPolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogPolygon, ObWkbGeogMultiPolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogPolygon, ObWkbGeogMultiPolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// Geog MultiPoint
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObWkbGeogMultiPoint, ObWkbGeogPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObWkbGeogMultiPoint, ObWkbGeogMultiPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geog MultiLineString
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogLineString>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogMultiLineString>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// Geog MultiPolygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogMultiPolygon, ObWkbGeogPolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogMultiPolygon, ObWkbGeogPolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_equals_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncEqualsImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

extern int ob_geo_func_equals_eval_tree_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

// Geog Collection
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogCollection, ObWkbGeogCollection, bool)
{
  return eval_equals_geometry_collection<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_geog, ob_geo_func_equals_eval_tree_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogCollection, bool)
{
  return eval_equals_geometry_collection<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_geog, ob_geo_func_equals_eval_tree_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeogCollection, bool)
{
  return eval_equals_geometry_collection<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_geog, ob_geo_func_equals_eval_tree_geog);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
