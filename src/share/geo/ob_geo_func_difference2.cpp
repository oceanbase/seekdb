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

#include "ob_geo_func_difference_helper.ipp"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

// geographic point
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObWkbGeogPoint, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeogPoint, ObWkbGeogPoint, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPoint, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogPoint, ObWkbGeogLineString, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPoint, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogPoint, ObWkbGeogPolygon, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeogPoint, ObWkbGeogMultiPoint, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPoint, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogPoint,
      ObWkbGeogMultiLineString,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPoint, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogPoint,
      ObWkbGeogMultiPolygon,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// geographic linestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogLineString, ObWkbGeogPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogLineString, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogLineString,
      ObWkbGeogLineString,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogLineString, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogLineString,
      ObWkbGeogPolygon,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogLineString, ObWkbGeogMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogLineString,
      ObWkbGeogMultiLineString,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogLineString, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogLineString,
      ObWkbGeogMultiPolygon,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// gepgraphic polygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPolygon, ObWkbGeogPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPolygon, ObWkbGeogLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPolygon, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogPolygon,
      ObWkbGeogPolygon,
      ObGeographMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPolygon, ObWkbGeogMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPolygon, ObWkbGeogMultiLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogPolygon, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogPolygon,
      ObWkbGeogMultiPolygon,
      ObGeographMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// geographic multipoint
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeogMultiPoint, ObWkbGeogPoint, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPoint, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogMultiPoint,
      ObWkbGeogLineString,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPoint, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogMultiPoint,
      ObWkbGeogPolygon,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogMultiPoint,
      ObWkbGeogMultiLineString,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObWkbGeogMultiPoint,
      ObWkbGeogMultiPolygon,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// geographic multilinestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiLineString, ObWkbGeogPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogMultiLineString,
      ObWkbGeogLineString,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiLineString, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogMultiLineString,
      ObWkbGeogPolygon,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogMultiLineString,
      ObWkbGeogMultiLineString,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogMultiLineString,
      ObWkbGeogMultiPolygon,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// geographic mutlipolygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPolygon, ObWkbGeogPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPolygon, ObWkbGeogLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPolygon, ObWkbGeogPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogMultiPolygon,
      ObWkbGeogPolygon,
      ObGeographMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObWkbGeogMultiPolygon,
      ObWkbGeogMultiPolygon,
      ObGeographMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_difference_eval_wkb_geog(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  return ObGeoFuncDifferenceImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

// geographic geometry collection
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeogCollection, ObWkbGeogCollection, ObGeometry *)
{
  return eval_difference_gc_other<ObGeographGeometrycollection,
      ObWkbGeogCollection>(g1, g2, context, result,
      ob_geo_func_difference_eval_wkb_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncDifferenceImpl, ObWkbGeogCollection, ObGeometry *)
{
  return eval_difference_gc_other<ObGeographGeometrycollection,
      ObWkbGeogCollection>(g1, g2, context, result,
      ob_geo_func_difference_eval_wkb_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncDifferenceImpl, ObWkbGeogCollection, ObGeometry *)
{
  return eval_difference_gc_other<ObGeographGeometrycollection,
      ObWkbGeogCollection>(g1, g2, context, result,
      ob_geo_func_difference_eval_wkb_geog);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
