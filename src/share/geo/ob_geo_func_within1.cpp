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

#include "ob_geo_func_within_helper.ipp"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

int ob_geo_func_within_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

// cart_point
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomPoint, ObWkbGeomCollection, bool)
{
  return eval_within_geometry_collection<ObWkbGeomCollection>(g1, g2, context, result, ob_geo_func_within_eval_wkb_cart);
} OB_GEO_FUNC_END;

// cart_linestring
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomLineString, ObWkbGeomPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomLineString, ObWkbGeomMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomLineString, ObWkbGeomCollection, bool)
{
  return ob_caculate_ml_within_gc<ObWkbGeomLineString, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cart_polygon
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomPolygon, ObWkbGeomPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomPolygon, ObWkbGeomLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomPolygon, ObWkbGeomMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomPolygon, ObWkbGeomMultiLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;


OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomPolygon, ObWkbGeomCollection, bool)
{
  return ob_caculate_mpl_within_gc<ObWkbGeomPolygon, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// multipoint
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, bool)
{
  return ob_caculate_mp_within_p<ObWkbGeomMultiPoint, ObWkbGeomPoint> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomLineString, bool)
{
  return ob_caculate_mp_within_l_a<ObWkbGeomMultiPoint, ObWkbGeomLineString> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomPolygon, bool)
{
  return ob_caculate_mp_within_l_a<ObWkbGeomMultiPoint, ObWkbGeomPolygon> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, bool)
{
  return ob_caculate_mp_within_mp<ObWkbGeomMultiPoint> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiLineString, bool)
{
  return ob_caculate_mp_within_l_a<ObWkbGeomMultiPoint, ObWkbGeomMultiLineString> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon, bool)
{
  return ob_caculate_mp_within_l_a<ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPoint, ObWkbGeomCollection, bool)
{
  return ob_caculate_mp_within_gc<ObWkbGeomPoint, ObIWkbGeomPoint,
    ObWkbGeomCollection, ObWkbGeomMultiPoint>(g1, g2, context, result, ob_geo_func_within_eval_wkb_cart);
} OB_GEO_FUNC_END;

// multilinestring
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiLineString, ObWkbGeomPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiLineString, ObWkbGeomCollection, bool)
{
  return ob_caculate_ml_within_gc<ObWkbGeomMultiLineString, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// multipolygon
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPolygon, ObWkbGeomPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPolygon, ObWkbGeomLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomMultiPolygon, ObWkbGeomCollection, bool)
{
  return ob_caculate_mpl_within_gc<ObWkbGeomMultiPolygon, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// cart_geometrycollection
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomPoint, bool)
{
  return ob_caculate_gc_within_p<ObWkbGeomPoint, ObCartesianGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomLineString, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *multi_point = NULL;
  ObCartesianMultilinestring *multi_line = NULL;
  ObCartesianMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeomLineString *geo2 = reinterpret_cast<const ObWkbGeomLineString *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context,
                                                                           geo1,
                                                                           multi_point,
                                                                           multi_line,
                                                                           multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (!multi_poly->empty()) {
    result = false;
  } else if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
    LOG_WARN("failed to convert geo tree to binary", K(ret));
  } else {
    bool mp_within_l = false;
    ret = eval_wkb_binary(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between ObWkbGeomMultiPoint and ObWkbGeomLineString", K(ret));
    } else if (mp_within_l) {
      result = multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2);
    } else if (bg::within(*multi_line, *geo2)){
      bool covered = true;
      ObCartesianMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObCartesianMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = covered;
    } else {
      result = false;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomPolygon, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *multi_point = NULL;
  ObCartesianMultilinestring *multi_line = NULL;
  ObCartesianMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeomPolygon *geo2 = reinterpret_cast<const ObWkbGeomPolygon *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context,
                                                                           geo1,
                                                                           multi_point,
                                                                           multi_line,
                                                                           multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
    LOG_WARN("failed to convert geo tree to binary", K(ret));
  } else {
    bool mp_within_l = false;
    ret = eval_wkb_binary(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between multipoint and polygon", K(ret));
    } else if (mp_within_l) {
      result = (multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2)) &&
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_line, *geo2)) {
      bool covered = true;
      ObCartesianMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObCartesianMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = !covered ? false :
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_poly, *geo2)){
      bool covered = true;
      ObCartesianMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObCartesianMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = !covered ? false :
               (multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2));
    } else {
      result = false;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomMultiPoint, bool)
{
  return ob_caculate_gc_within_mpt<ObCartesianGeometrycollection>(g1, g2, context, result, ob_geo_func_within_eval_wkb_cart);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomMultiLineString, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *multi_point = NULL;
  ObCartesianMultilinestring *multi_line = NULL;
  ObCartesianMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeomMultiLineString *geo2 = reinterpret_cast<const ObWkbGeomMultiLineString *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context,
                                                                           geo1,
                                                                           multi_point,
                                                                           multi_line,
                                                                           multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (!multi_poly->empty()) {
    result = false;
  } else if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
    LOG_WARN("failed to convert geo tree to binary", K(ret));
  } else {
    bool mp_within_l = false;
    ret = eval_wkb_binary(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between GeomMultiPoint and GeomMultiLineString", K(ret));
    } else if (mp_within_l) {
      result = multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2);
    } else if (bg::within(*multi_line, *geo2)){
      bool covered = true;
      ObCartesianMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        typename ObCartesianMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = covered;
    } else {
      result = false;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomMultiPolygon, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *multi_point = NULL;
  ObCartesianMultilinestring *multi_line = NULL;
  ObCartesianMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeomMultiPolygon *geo2 = reinterpret_cast<const ObWkbGeomMultiPolygon *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context,
                                                                           geo1,
                                                                           multi_point,
                                                                           multi_line,
                                                                           multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
    LOG_WARN("failed to convert geo tree to binary", K(ret));
  } else {
    bool mp_within_l = false;
    ret = eval_wkb_binary(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between multipoint and polygon", K(ret));
    } else if (mp_within_l) {
      result = (multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2)) &&
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_line, *geo2)){
      bool covered = true;
      ObCartesianMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObCartesianMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = !covered ? false :
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_poly, *geo2)){
      bool covered = true;
      ObCartesianMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObCartesianMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = !covered ? false :
               (multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2));
    } else {
      result = false;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeomCollection, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *g1_multi_point = NULL;
  ObCartesianMultilinestring *g1_multi_line = NULL;
  ObCartesianMultipolygon *g1_multi_poly = NULL;

  ObCartesianMultipoint *g2_multi_point = NULL;
  ObCartesianMultilinestring *g2_multi_line = NULL;
  ObCartesianMultipolygon *g2_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context,
                                                                           geo1,
                                                                           g1_multi_point,
                                                                           g1_multi_line,
                                                                           g1_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context,
                                                                                  geo2,
                                                                                  g2_multi_point,
                                                                                  g2_multi_line,
                                                                                  g2_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    ObGeometry *res_geo3 = NULL;
    if (OB_FAIL(do_multi_difference(srs, context, reinterpret_cast<ObGeometry *>(g1_multi_point),
                                                      reinterpret_cast<ObGeometry *>(g2_multi_point),
                                                      reinterpret_cast<ObGeometry *>(g2_multi_line),
                                                      reinterpret_cast<ObGeometry *>(g2_multi_poly),
                                                      res_geo3))) {
      LOG_WARN("failed to do mulit difference", K(ret));
    } else if (!res_geo3->is_empty()) {
      result = false;
    } else {
      ObGeometry *res_geo5 = NULL;
      if (OB_FAIL(do_multi_difference(srs, context, reinterpret_cast<ObGeometry *>(g1_multi_line),
                                                        NULL,
                                                        reinterpret_cast<ObGeometry *>(g2_multi_line),
                                                        reinterpret_cast<ObGeometry *>(g2_multi_poly),
                                                        res_geo5))) {
          LOG_WARN("failed to do mulit difference", K(ret));
      } else if (!res_geo5->is_empty()) {
        result = false;
      } else {
        ObGeometry *res_geo6 = NULL;
        if (OB_FAIL(do_multi_difference(srs, context, reinterpret_cast<ObGeometry *>(g1_multi_poly),
                                                          NULL,
                                                          NULL,
                                                          reinterpret_cast<ObGeometry *>(g2_multi_poly),
                                                          res_geo6))) {
          LOG_WARN("failed to do mulit difference", K(ret));
        } else if (!res_geo6->is_empty()) {
          result = false;
        } else {
          ObGeometry *g1_multi_point_bin = NULL;
          if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, g1_multi_point, g1_multi_point_bin, srs))) {
            LOG_WARN("failed to convert geo tree to binary", K(ret));
          } else {
            bool mp_within_gc = false;
            ret = eval_wkb_binary(g1_multi_point_bin, g2, context, mp_within_gc);
            if (OB_FAIL(ret)) {
              LOG_WARN("failed to do within by functor between GeomMultiPoint and GeomCollection", K(ret));
            } else {
              // Checks relation between a pair of geometries defined by a mask.
              bg::de9im::mask mask("T********");
              result = mp_within_gc ||
                       bg::relate(*g1_multi_line, *g2_multi_line, mask) ||
                       bg::relate(*g1_multi_line, *g2_multi_poly, mask) ||
                       bg::relate(*g1_multi_poly, *g2_multi_poly, mask);
            }
          }
        }
      }
    }
  }
  return ret;
} OB_GEO_FUNC_END;

int ob_geo_func_within_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncWithinImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

} // namespace common
} // namespace oceanbase
