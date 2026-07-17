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
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObWkbGeomPoint, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPoint, ObWkbGeomPoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPoint, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPoint, ObWkbGeomLineString, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPoint, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPoint, ObWkbGeomPolygon, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPoint, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPoint, ObWkbGeomMultiPoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPoint, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPoint, ObWkbGeomMultiLineString, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPoint, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPoint, ObWkbGeomMultiPolygon, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// cartisian linestring
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomLineString, ObWkbGeomPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomLineString, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomLineString, ObWkbGeomLineString, ObCartesianMultilinestring>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomLineString, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomLineString, ObWkbGeomPolygon, ObCartesianMultilinestring>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomLineString, ObWkbGeomMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomLineString,
      ObWkbGeomMultiLineString,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomLineString, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomLineString,
      ObWkbGeomMultiPolygon,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// cartisian polygon
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPolygon, ObWkbGeomPolygon, ObCartesianMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomMultiLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomPolygon, ObWkbGeomMultiPolygon, ObCartesianMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// cartisian multipoint
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPoint, ObWkbGeomPoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPoint, ObWkbGeomLineString, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPoint, ObWkbGeomPolygon, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPoint, ObWkbGeomMultiLineString, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// cartisian mutilinestring
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiLineString,
      ObWkbGeomLineString,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiLineString,
      ObWkbGeomPolygon,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiLineString,
      ObWkbGeomMultiLineString,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiLineString,
      ObWkbGeomMultiPolygon,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

// cartisian multipolygon
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPolygon, ObWkbGeomPolygon, ObCartesianMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPoint, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiLineString, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just convert g1 to tree
  return ObGeoFuncUtils::apply_bg_to_tree(g1, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, ObGeometry *)
{
  return apply_bg_difference<ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, ObCartesianMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_difference_eval_wkb_cart(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  return ObGeoFuncDifferenceImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

// cartesian geometry collection
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObWkbGeomCollection, ObWkbGeomCollection, ObGeometry *)
{
  return eval_difference_gc_other<ObCartesianGeometrycollection,
      ObWkbGeomCollection>(g1, g2, context, result,
      ob_geo_func_difference_eval_wkb_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncDifferenceImpl, ObWkbGeomCollection, ObGeometry *)
{
  return eval_difference_gc_other<ObCartesianGeometrycollection,
      ObWkbGeomCollection>(g1, g2, context, result,
      ob_geo_func_difference_eval_wkb_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncDifferenceImpl, ObWkbGeomCollection, ObGeometry *)
{
  return eval_difference_gc_other<ObCartesianGeometrycollection,
      ObWkbGeomCollection>(g1, g2, context, result,
      ob_geo_func_difference_eval_wkb_cart);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
