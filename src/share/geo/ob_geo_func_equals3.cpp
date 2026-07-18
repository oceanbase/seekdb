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

// cartesian tree
// Geom Point
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianPoint, ObCartesianPoint, bool)
{
  return eval_equals_without_strategy<ObCartesianPoint, ObCartesianPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianPoint, ObCartesianMultipoint, bool)
{
  return eval_equals_without_strategy<ObCartesianPoint, ObCartesianMultipoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom LineString
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianLineString, ObCartesianLineString, bool)
{
  return eval_equals_without_strategy<ObCartesianLineString, ObCartesianLineString>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObCartesianLineString, ObCartesianMultilinestring, bool)
{
  return eval_equals_without_strategy<ObCartesianLineString, ObCartesianMultilinestring>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom Polygon
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianPolygon, ObCartesianPolygon, bool)
{
  return eval_equals_without_strategy<ObCartesianPolygon, ObCartesianPolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianPolygon, ObCartesianMultipolygon, bool)
{
  return eval_equals_without_strategy<ObCartesianPolygon, ObCartesianMultipolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom MultiPoint
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianMultipoint, ObCartesianPoint, bool)
{
  return eval_equals_without_strategy<ObCartesianMultipoint, ObCartesianPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianMultipoint, ObCartesianMultipoint, bool)
{
  return eval_equals_without_strategy<ObCartesianMultipoint, ObCartesianMultipoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom MultiLineString
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObCartesianMultilinestring, ObCartesianLineString, bool)
{
  return eval_equals_without_strategy<ObCartesianMultilinestring, ObCartesianLineString>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObCartesianMultilinestring, ObCartesianMultilinestring, bool)
{
  return eval_equals_without_strategy<ObCartesianMultilinestring, ObCartesianMultilinestring>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

// Geom MultiPolygon
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianMultipolygon, ObCartesianPolygon, bool)
{
  return eval_equals_without_strategy<ObCartesianMultipolygon, ObCartesianPolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObCartesianMultipolygon, ObCartesianMultipolygon, bool)
{
  return eval_equals_without_strategy<ObCartesianMultipolygon, ObCartesianMultipolygon>(g1, g2, result);
}
OB_GEO_FUNC_END;

extern int ob_geo_func_equals_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

int ob_geo_func_equals_eval_tree_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncEqualsImpl::eval_tree_binary_cart(g1, g2, context, result);
}

// Geom Collection
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObCartesianGeometrycollection, ObCartesianGeometrycollection, bool)
{
  return eval_equals_geometry_collection<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_cart, ob_geo_func_equals_eval_tree_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_GEO2_BEGIN(ObGeoFuncEqualsImpl, ObCartesianGeometrycollection, bool)
{
  return eval_equals_geometry_collection<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_cart, ob_geo_func_equals_eval_tree_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_GEO1_BEGIN(ObGeoFuncEqualsImpl, ObCartesianGeometrycollection, bool)
{
  return eval_equals_geometry_collection<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_cart, ob_geo_func_equals_eval_tree_cart);
}
OB_GEO_FUNC_END;

// Geog tree
// Geog Point
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographPoint, ObGeographPoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObGeographPoint, ObGeographPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographPoint, ObGeographMultipoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObGeographPoint, ObGeographMultipoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geog LineString
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographLineString, ObGeographLineString, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographLineString, ObGeographLineString>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObGeographLineString, ObGeographMultilinestring, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographLineString, ObGeographMultilinestring>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// Geog Polygon
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographPolygon, ObGeographPolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographPolygon, ObGeographPolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographPolygon, ObGeographMultipolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographPolygon, ObGeographMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// Geog MultiPoint
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographMultipoint, ObGeographPoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObGeographMultipoint, ObGeographPoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographMultipoint, ObGeographMultipoint, bool)
{
  // Default strategy is OK. P/P computations do not depend on shape of ellipsoid.
  return eval_equals_without_strategy<ObGeographMultipoint, ObGeographMultipoint>(g1, g2, result);
}
OB_GEO_FUNC_END;

// Geog MultiLineString
OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObGeographMultilinestring, ObGeographLineString, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographMultilinestring, ObGeographLineString>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObGeographMultilinestring, ObGeographMultilinestring, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographMultilinestring, ObGeographMultilinestring>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// Geog MultiPolygon
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographMultipolygon, ObGeographPolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographMultipolygon, ObGeographPolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncEqualsImpl, ObGeographMultipolygon, ObGeographMultipolygon, bool)
{
  return eval_equals_with_nonpoint_strategy<ObGeographMultipolygon, ObGeographMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

extern int ob_geo_func_equals_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

int ob_geo_func_equals_eval_tree_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncEqualsImpl::eval_tree_binary_geog(g1, g2, context, result);
}

// Geog Collection
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncEqualsImpl, ObGeographGeometrycollection, ObGeographGeometrycollection, bool)
{
  return eval_equals_geometry_collection<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_geog, ob_geo_func_equals_eval_tree_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_GEO2_BEGIN(ObGeoFuncEqualsImpl, ObGeographGeometrycollection, bool)
{
  return eval_equals_geometry_collection<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_geog, ob_geo_func_equals_eval_tree_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_GEO1_BEGIN(ObGeoFuncEqualsImpl, ObGeographGeometrycollection, bool)
{
  return eval_equals_geometry_collection<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_equals_eval_wkb_geog, ob_geo_func_equals_eval_tree_geog);
}
OB_GEO_FUNC_END;

static int ob_geo_func_equals_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncEqualsImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncEqualsImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_equals_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);
extern int ob_geo_func_equals_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

int ObGeoFuncEquals::eval(const ObGeoEvalCtx &gis_context, bool &result)
{
  return ObGeoFuncEqualsImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_equals_eval_wkb_cart,
      ob_geo_func_equals_eval_wkb_geog,
      ob_geo_func_equals_eval_tree);
}

}  // namespace common
}  // namespace oceanbase
