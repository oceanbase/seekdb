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
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

// cartesian point
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPoint, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomPoint, ObWkbGeomPoint, ObCartesianMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPoint, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeomPoint, ObIWkbGeomLineString,
                                   ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPoint, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeomPoint, ObIWkbGeomPolygon,
                                    ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPoint, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomPoint, ObWkbGeomMultiPoint,
                        ObCartesianMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPoint, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_multi_union_collection<ObWkbGeomPoint, ObWkbGeomMultiLineString,
                                          ObIWkbGeomPoint, ObIWkbGeomLineString,
                                          ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPoint, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_multi_union_collection<ObWkbGeomPoint, ObWkbGeomMultiPolygon,
                                          ObIWkbGeomPoint, ObIWkbGeomPolygon,
                                          ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cartisian linestring
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomLineString, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeomPoint, ObIWkbGeomLineString,
                                    ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomLineString, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomLineString, ObWkbGeomLineString, ObCartesianMultilinestring>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomLineString, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeomLineString, ObWkbGeomPolygon,
    ObCartesianMultilinestring, ObIWkbGeomPolygon,
    ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomLineString, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeomMultiPoint, ObWkbGeomLineString,
    ObCartesianMultipoint, ObIWkbGeomLineString, ObCartesianLineString,
    ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomLineString, ObWkbGeomMultiLineString, ObCartesianMultilinestring>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomLineString, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeomLineString, ObWkbGeomMultiPolygon,
    ObCartesianMultilinestring, ObIWkbGeomMultiPolygon,
    ObCartesianMultipolygon, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cartisian polygon
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPolygon, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_union_collection<ObIWkbGeomPoint, ObIWkbGeomPolygon,
                                    ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPolygon, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeomLineString, ObWkbGeomPolygon,
    ObCartesianMultilinestring, ObIWkbGeomPolygon,
    ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPolygon, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomPolygon, ObWkbGeomPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPolygon, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeomMultiPoint, ObWkbGeomPolygon,
    ObCartesianMultipoint, ObIWkbGeomPolygon, ObCartesianPolygon,
    ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPolygon, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeomMultiLineString, ObWkbGeomPolygon,
    ObCartesianMultilinestring, ObIWkbGeomPolygon,
    ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cartisian multipoint
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomPoint, ObWkbGeomMultiPoint, ObCartesianMultipoint>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPoint, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeomMultiPoint, ObWkbGeomLineString,
    ObCartesianMultipoint, ObIWkbGeomLineString,
    ObCartesianLineString, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPoint, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_union_multipoint_geo<ObWkbGeomMultiPoint, ObWkbGeomPolygon,
    ObCartesianMultipoint, ObIWkbGeomPolygon,
    ObCartesianPolygon, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, ObCartesianMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeomMultiPoint,
    ObWkbGeomMultiLineString, ObCartesianMultipoint, ObIWkbGeomMultiLineString,
    ObCartesianMultilinestring, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeomMultiPoint,
    ObWkbGeomMultiPolygon, ObCartesianMultipoint, ObIWkbGeomMultiPolygon,
    ObCartesianMultipolygon, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cartisian mutilinestring
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiLineString, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_multi_union_collection<ObWkbGeomPoint, ObWkbGeomMultiLineString,
                                          ObIWkbGeomPoint, ObIWkbGeomLineString,
                                          ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomLineString, ObWkbGeomMultiLineString, ObCartesianMultilinestring>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiLineString, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_diff_union_collection<ObWkbGeomMultiLineString, ObWkbGeomPolygon,
    ObCartesianMultilinestring, ObIWkbGeomPolygon,
    ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeomMultiPoint,
    ObWkbGeomMultiLineString, ObCartesianMultipoint, ObIWkbGeomMultiLineString,
    ObCartesianMultilinestring, ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, ObCartesianMultilinestring>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeomMultiLineString,
    ObWkbGeomMultiPolygon, ObCartesianMultilinestring, ObIWkbGeomMultiPolygon,
    ObCartesianMultipolygon, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cartisian multipolygon
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPolygon, ObWkbGeomPoint, ObGeometry *)
{
    return apply_bg_multi_union_collection<ObWkbGeomPoint, ObWkbGeomMultiPolygon,
                                          ObIWkbGeomPoint, ObIWkbGeomPolygon,
                                          ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPolygon, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeomLineString, ObWkbGeomMultiPolygon,
    ObCartesianMultilinestring, ObIWkbGeomMultiPolygon,
    ObCartesianMultipolygon, ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPolygon, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomMultiPolygon, ObWkbGeomPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_union_multipoint_multigeo<ObWkbGeomMultiPoint,
    ObWkbGeomMultiPolygon, ObCartesianMultipoint, ObIWkbGeomMultiPolygon,
    ObCartesianMultipolygon, ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_union_multiline_multipolygon<ObWkbGeomMultiLineString,
    ObWkbGeomMultiPolygon, ObCartesianMultilinestring, ObIWkbGeomMultiPolygon,
    ObCartesianMultipolygon, ObCartesianGeometrycollection>(g2, g1, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_union<ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cartisian collection
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomCollection, ObWkbGeomCollection, ObGeometry *)
{
  return eval_unions_gc<ObCartesianGeometrycollection, ObCartesianPoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomCollection, ObGeometry *)
{
  return eval_unions_gc<ObCartesianGeometrycollection, ObCartesianPoint>(g2, g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncUnionImpl, ObWkbGeomCollection, ObGeometry *)
{
  return eval_unions_gc<ObCartesianGeometrycollection, ObCartesianPoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_union_eval_wkb_cart(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  return ObGeoFuncUnionImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

} // sql
} // oceanbase
