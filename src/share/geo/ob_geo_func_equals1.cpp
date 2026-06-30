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
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========BIN GEOM==========
// Geom Point
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomPoint, ObWkbGeomPoint, bool)
{
  return eval_equals_without_strategy<ObWkbGeomPoint, ObWkbGeomPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomPoint, ObWkbGeomMultiPoint, bool)
{
  return eval_equals_without_strategy<ObWkbGeomPoint, ObWkbGeomMultiPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom LineString
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomLineString, ObWkbGeomLineString, bool)
{
  return eval_equals_without_strategy<ObWkbGeomLineString, ObWkbGeomLineString>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, bool)
{
  return eval_equals_without_strategy<ObWkbGeomLineString, ObWkbGeomMultiLineString>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom Polygon
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomPolygon, ObWkbGeomPolygon, bool)
{
  return eval_equals_without_strategy<ObWkbGeomPolygon, ObWkbGeomPolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomPolygon, ObWkbGeomMultiPolygon, bool)
{
  return eval_equals_without_strategy<ObWkbGeomPolygon, ObWkbGeomMultiPolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom MultiPoint
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, bool)
{
  return eval_equals_without_strategy<ObWkbGeomMultiPoint, ObWkbGeomPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, bool)
{
  return eval_equals_without_strategy<ObWkbGeomMultiPoint, ObWkbGeomMultiPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom MultiLineString
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, bool)
{
  return eval_equals_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomLineString>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, bool)
{
  return eval_equals_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom MultiPolygon
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomMultiPolygon, ObWkbGeomPolygon, bool)
{
  return eval_equals_without_strategy<ObWkbGeomMultiPolygon, ObWkbGeomPolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, bool)
{
  return eval_equals_without_strategy<ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_equals_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncEqualsImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

extern int ob_geo_func_equals_eval_tree_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

// Geom Collection
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomCollection, ObWkbGeomCollection, bool)
{
  return eval_equals_geometry_collection<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_cart, ob_geo_func_equals_eval_tree_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomCollection, bool)
{
  return eval_equals_geometry_collection<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_cart, ob_geo_func_equals_eval_tree_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncEqualsImpl, ObWkbGeomCollection, bool)
{
  return eval_equals_geometry_collection<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_cart, ob_geo_func_equals_eval_tree_cart);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
