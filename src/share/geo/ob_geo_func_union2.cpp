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

#include "ob_geo_func_union_helper.ipp"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

// geographic point
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPoint, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogPoint, ObWkbGeogPoint, ObGeographMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPoint, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeogPoint, ObIWkbGeogLineString,
                                    ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPoint, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeogPoint, ObIWkbGeogPolygon,
                                    ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogPoint, ObWkbGeogMultiPoint,
                        ObGeographMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPoint, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_multi_union_collection<ObWkbGeogPoint, ObWkbGeogMultiLineString,
                                          ObIWkbGeogPoint, ObIWkbGeogLineString,
                                          ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPoint, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_multi_union_collection<ObWkbGeogPoint, ObWkbGeogMultiPolygon,
                                          ObIWkbGeogPoint, ObIWkbGeogPolygon,
                                          ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geograph linestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogLineString, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeogPoint, ObIWkbGeogLineString,
                                    ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogLineString, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogLineString, ObWkbGeogLineString, ObGeographMultilinestring>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogLineString, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeogLineString, ObWkbGeogPolygon,
    ObGeographMultilinestring, ObIWkbGeogPolygon,
    ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogLineString, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeogMultiPoint, ObWkbGeogLineString,
    ObGeographMultipoint, ObIWkbGeogLineString, ObGeographLineString,
    ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogLineString, ObWkbGeogMultiLineString, ObGeographMultilinestring>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogLineString, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeogLineString, ObWkbGeogMultiPolygon,
    ObGeographMultilinestring, ObIWkbGeogMultiPolygon,
    ObGeographMultipolygon, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geograph polygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPolygon, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeogPoint, ObIWkbGeogPolygon,
                                    ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPolygon, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeogLineString, ObWkbGeogPolygon,
    ObGeographMultilinestring, ObIWkbGeogPolygon,
    ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPolygon, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogPolygon, ObWkbGeogPolygon, ObGeographMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPolygon, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeogMultiPoint, ObWkbGeogPolygon,
    ObGeographMultipoint, ObIWkbGeogPolygon, ObGeographPolygon,
    ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPolygon, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeogMultiLineString, ObWkbGeogPolygon,
    ObGeographMultilinestring, ObIWkbGeogPolygon,
    ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogPolygon, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogPolygon, ObWkbGeogMultiPolygon, ObGeographMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geograph multipoint
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogPoint, ObWkbGeogMultiPoint, ObGeographMultipoint>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPoint, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeogMultiPoint, ObWkbGeogLineString,
    ObGeographMultipoint, ObIWkbGeogLineString,
    ObGeographLineString, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPoint, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeogMultiPoint, ObWkbGeogPolygon,
    ObGeographMultipoint, ObIWkbGeogPolygon,
    ObGeographPolygon, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, ObGeographMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeogMultiPoint,
    ObWkbGeogMultiLineString, ObGeographMultipoint, ObIWkbGeogMultiLineString,
    ObGeographMultilinestring, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeogMultiPoint,
    ObWkbGeogMultiPolygon, ObGeographMultipoint, ObIWkbGeogMultiPolygon,
    ObGeographMultipolygon, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geograph mutilinestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiLineString, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_multi_union_collection<ObWkbGeogPoint, ObWkbGeogMultiLineString,
                                          ObIWkbGeogPoint, ObIWkbGeogLineString,
                                          ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogLineString, ObWkbGeogMultiLineString, ObGeographMultilinestring>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiLineString, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeogMultiLineString, ObWkbGeogPolygon,
    ObGeographMultilinestring, ObIWkbGeogPolygon,
    ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeogMultiPoint,
    ObWkbGeogMultiLineString, ObGeographMultipoint, ObIWkbGeogMultiLineString,
    ObGeographMultilinestring, ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, ObGeographMultilinestring>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeogMultiLineString,
    ObWkbGeogMultiPolygon, ObGeographMultilinestring, ObIWkbGeogMultiPolygon,
    ObGeographMultipolygon, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geograph multipolygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPolygon, ObWkbGeogPoint, ObGeometry *)
{
    return apply_bg_multi_union_collection<ObWkbGeogPoint, ObWkbGeogMultiPolygon,
                                          ObIWkbGeogPoint, ObIWkbGeogPolygon,
                                          ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPolygon, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeogLineString, ObWkbGeogMultiPolygon,
    ObGeographMultilinestring, ObIWkbGeogMultiPolygon,
    ObGeographMultipolygon, ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPolygon, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogMultiPolygon, ObWkbGeogPolygon, ObGeographMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeogMultiPoint,
    ObWkbGeogMultiPolygon, ObGeographMultipoint, ObIWkbGeogMultiPolygon,
    ObGeographMultipolygon, ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeogMultiLineString,
    ObWkbGeogMultiPolygon, ObGeographMultilinestring, ObIWkbGeogMultiPolygon,
    ObGeographMultipolygon, ObGeographGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, ObGeographMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geograph collection
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogCollection, ObWkbGeogCollection, ObGeometry *)
{
  return eval_unions_gc<ObGeographGeometrycollection, ObGeographPoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogCollection, ObGeometry *)
{
  return eval_unions_gc<ObGeographGeometrycollection, ObGeographPoint>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncUnionImpl, ObWkbGeogCollection, ObGeometry *)
{
  return eval_unions_gc<ObGeographGeometrycollection, ObGeographPoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;


// tree cartesian polygon
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncUnionImpl, ObCartesianPolygon, ObCartesianPolygon, ObGeometry *)
{
  return apply_bg_union<ObCartesianPolygon, ObCartesianPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncUnionImpl, ObCartesianPolygon, ObCartesianMultipolygon, ObGeometry *)
{
  return apply_bg_union<ObCartesianPolygon, ObCartesianMultipolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncUnionImpl, ObCartesianMultipolygon, ObCartesianMultipolygon, ObGeometry *)
{
  return apply_bg_union<ObCartesianMultipolygon, ObCartesianMultipolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncUnionImpl, ObCartesianMultipolygon, ObCartesianPolygon, ObGeometry *)
{
  return apply_bg_union<ObCartesianPolygon, ObCartesianMultipolygon, ObCartesianMultipolygon>(g2, g1, context, result);
} OB_GEO_FUNC_END;

// tree geograph polygon
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncUnionImpl, ObGeographPolygon, ObGeographPolygon, ObGeometry *)
{
  return apply_bg_union<ObGeographPolygon, ObGeographPolygon, ObGeographMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncUnionImpl, ObGeographPolygon, ObGeographMultipolygon, ObGeometry *)
{
  return apply_bg_union<ObGeographPolygon, ObGeographMultipolygon, ObGeographMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

static int ob_geo_func_union_eval_wkb_geog(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  return ObGeoFuncUnionImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

static int ob_geo_func_union_eval_tree(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncUnionImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncUnionImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_union_eval_wkb_cart(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result);

// implement of outer class eval
// use an outer class to void implement templates in header files
int ObGeoFuncUnion::eval(const ObGeoEvalCtx &gis_context, ObGeometry *&result)
{
  return ObGeoFuncUnionImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_union_eval_wkb_cart,
      ob_geo_func_union_eval_wkb_geog,
      ob_geo_func_union_eval_tree);
}

} // sql
} // oceanbase
