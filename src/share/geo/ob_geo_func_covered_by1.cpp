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

#include "ob_geo_func_covered_by_helper.ipp"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomPoint, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  const ObWkbGeomPoint *geo1 = reinterpret_cast<const ObWkbGeomPoint *>(g1->val());
  const ObWkbGeomCollection *geo2 = reinterpret_cast<const ObWkbGeomCollection *>(g2->val());
  ObWkbGeomCollection::iterator iter = geo2->begin();
  typename ObWkbGeomCollection::const_pointer sub_ptr;
  for (result = false; iter != geo2->end() && (result == false) && OB_SUCC(ret); ++iter) {
    sub_ptr = iter.operator->();
    ObGeoType sub_type = geo2->get_sub_type(sub_ptr);
    switch (sub_type) {
      case ObGeoType::POINT :
      case ObGeoType::LINESTRING :
      case ObGeoType::MULTIPOINT :
      case ObGeoType::MULTILINESTRING :
      case ObGeoType::GEOMETRYCOLLECTION : {
        ObGeometry *sub_g2 = NULL;
        common::ObIAllocator *allocator = context.get_allocator();
        if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(*allocator, sub_type, false, true, sub_g2))) {
          LOG_WARN("failed to create wkb", K(ret), K(sub_type));
        } else {
          // Length is not used, cannot get real length until iter move to the next
          ObString wkb_nosrid(WKB_COMMON_WKB_HEADER_LEN, reinterpret_cast<const char *>(sub_ptr));
          sub_g2->set_data(wkb_nosrid);
          sub_g2->set_srid(g2->get_srid());
          if (OB_FAIL(eval_wkb_binary(g1, sub_g2, context, result))) {
            LOG_WARN("failed to eval sub geo", K(ret), K(sub_type));
          }
        }
        break;
      }
      case ObGeoType::POLYGON : {
        ObArenaAllocator tmp_alloc;
        const ObWkbGeomPolygon *polygon = reinterpret_cast<const ObWkbGeomPolygon*>(sub_ptr);
        ObString tmp(polygon->length(), reinterpret_cast<const char*>(sub_ptr));
        ObString pol_data;
        if (OB_FAIL(ob_write_string(tmp_alloc, tmp, pol_data))) {
          LOG_WARN("failed to copy polygon geo", K(ret));
        } else {
          ObWkbGeomPolygon *poly_copy = reinterpret_cast<ObWkbGeomPolygon*>(pol_data.ptr());
          boost::geometry::correct(*poly_copy);
          result = boost::geometry::covered_by(*geo1, *poly_copy);
        }
        break;
      }
      case ObGeoType::MULTIPOLYGON : {
        ObArenaAllocator tmp_alloc;
        const ObWkbGeomMultiPolygon *multi_poly = reinterpret_cast<const ObWkbGeomMultiPolygon*>(sub_ptr);
        ObString tmp(multi_poly->length(), reinterpret_cast<const char*>(sub_ptr));
        ObString multipol_data;
        if (OB_FAIL(ob_write_string(tmp_alloc, tmp, multipol_data))) {
          LOG_WARN("failed to copy multi_poly geo", K(ret));
        } else {
          ObWkbGeomMultiPolygon *multipoly_copy = reinterpret_cast<ObWkbGeomMultiPolygon*>(multipol_data.ptr());
          boost::geometry::correct(*multipoly_copy);
          result = boost::geometry::covered_by(*geo1, *multipoly_copy);
        }
        break;
      }
      default : {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected geometry type", K(ret));
      }
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomPoint, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomPoint, GeoType2>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, bool)
{
  UNUSED(context);
  const ObWkbGeomMultiPoint *geo1 = reinterpret_cast<const ObWkbGeomMultiPoint *>(g1->val());
  const ObWkbGeomPoint *geo2 = reinterpret_cast<const ObWkbGeomPoint *>(g2->val());
  //check if multipoint is equals to point geographically(msyql)
  result = boost::geometry::equals(*geo1, *geo2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiPoint, bool)
{
  UNUSED(context);
  INIT_SUCC(ret);
  const ObWkbGeomMultiPoint *geo1 = reinterpret_cast<const ObWkbGeomMultiPoint *>(g1->val());
  const GeoType2 *geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  // travel every point in multipoint; check if any one is not covered by geo2(postgis)
  result = true;
  FOREACH_X(item, *geo1, (result == true)) {
    result = boost::geometry::covered_by(*item, *geo2);
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiPoint, ObWkbGeomCollection, bool)
{
  return ob_caculate_mp_gc_cover_by<ObWkbGeomPoint, ObIWkbGeomPoint,
    ObWkbGeomCollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomLineString, ObWkbGeomLineString, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomLineString, ObWkbGeomLineString>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomLineString, ObWkbGeomPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomLineString, ObWkbGeomPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomLineString, ObWkbGeomMultiLineString>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomLineString, ObWkbGeomMultiPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomLineString, ObWkbGeomMultiPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomLineString, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo2, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to prepare gc", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs != NULL ? srs->get_srid() : 0;
    const ObWkbGeomLineString *geo1 = reinterpret_cast<const ObWkbGeomLineString *>(g1->val());
    ObCartesianMultilinestring res_geo1(srid, *allocator);
    boost::geometry::difference(*geo1, *cart_multi_line, res_geo1);
    ObCartesianMultilinestring res_geo2(srid, *allocator);
    boost::geometry::difference(res_geo1, *cart_multi_poly, res_geo2);
    result = res_geo2.is_empty();
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomPolygon, ObWkbGeomPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomPolygon, ObWkbGeomPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomPolygon, ObWkbGeomMultiPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomPolygon, ObWkbGeomMultiPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomPolygon, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));

  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo2, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to prepare gc", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs != NULL ? srs->get_srid() : 0;
    const ObWkbGeomPolygon *geo1 = reinterpret_cast<const ObWkbGeomPolygon *>(g1->val());
    boost::geometry::correct(*const_cast<ObWkbGeomPolygon *>(geo1));
    result = boost::geometry::covered_by(*geo1, *cart_multi_poly);
  }
  return ret;
} OB_GEO_FUNC_END;

// wkb cart multilinestring
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomMultiLineString, ObWkbGeomLineString>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiLineString, ObWkbGeomPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomMultiLineString, ObWkbGeomPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiLineString, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo2, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs != NULL ? srs->get_srid() : 0;
    const ObWkbGeomMultiLineString *geo1 = reinterpret_cast<const ObWkbGeomMultiLineString *>(g1->val());
    ObCartesianMultilinestring res_geo1(srid, *allocator);
    boost::geometry::difference(*geo1, *cart_multi_line, res_geo1);
    ObCartesianMultilinestring res_geo2(srid, *allocator);
    boost::geometry::difference(res_geo1, *cart_multi_poly, res_geo2);
    result = res_geo2.is_empty();
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiPolygon, ObWkbGeomPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomMultiPolygon, ObWkbGeomPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiPolygon, ObWkbGeomMultiPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeomMultiPolygon , ObWkbGeomMultiPolygon>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomMultiPolygon, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo2, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs != NULL ? srs->get_srid() : 0;
    const ObWkbGeomMultiPolygon *geo1 = reinterpret_cast<const ObWkbGeomMultiPolygon *>(g1->val());
    ObCartesianMultipolygon res_geo1(srid, *allocator);
    boost::geometry::correct(*const_cast<ObWkbGeomMultiPolygon *>(geo1));
    result = boost::geometry::covered_by(*geo1, *cart_multi_poly);
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomPoint, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (!cart_multi_poly->empty() || !cart_multi_line->empty()) {
    result = false;
  } else if (cart_multi_point->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeomPoint *geo2 = reinterpret_cast<const ObWkbGeomPoint *>(g2->val());
    FOREACH_X(item, *cart_multi_point, (result == true)) {
      result = boost::geometry::covered_by(*item, *geo2);
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomLineString, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (!cart_multi_poly->empty()) {
    result = false;
  } else if (cart_multi_point->empty() && cart_multi_line->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeomLineString *geo2 = reinterpret_cast<const ObWkbGeomLineString *>(g2->val());
    if (!cart_multi_line->empty()) {
      result = boost::geometry::covered_by(*cart_multi_line, *geo2);
    }
    FOREACH_X(item, *cart_multi_point, (result == true)) {
      result = boost::geometry::covered_by(*item, *geo2);
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomPolygon, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    result = true;
    const ObWkbGeomPolygon *geo2 = reinterpret_cast<const ObWkbGeomPolygon *>(g2->val());
    if (!cart_multi_poly->empty()) {
      result = boost::geometry::covered_by(*cart_multi_poly, *geo2);
    }
    if (result == true && !cart_multi_line->empty()) {
      result = boost::geometry::covered_by(*cart_multi_line, *geo2);
    }
    FOREACH_X(item, *cart_multi_point, (result == true)) {
      result = boost::geometry::covered_by(*item, *geo2);
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomMultiPoint, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (!cart_multi_poly->empty() || !cart_multi_line->empty()) {
    result = false;
  } else if (cart_multi_point->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeomMultiPoint *geo2 = reinterpret_cast<const ObWkbGeomMultiPoint *>(g2->val());
    FOREACH_X(item1, *cart_multi_point, (result == true)) {
      bool loop_res = false;
      FOREACH_X(item2, *geo2, (loop_res == false)) {
        loop_res = boost::geometry::covered_by(*item1, *item2);
      }
      result = loop_res;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomMultiLineString, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (!cart_multi_poly->empty()) {
    result = false;
  } else if (cart_multi_point->empty() && cart_multi_line->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeomMultiLineString *geo2 = reinterpret_cast<const ObWkbGeomMultiLineString *>(g2->val());
    if (!cart_multi_line->empty()) {
      result = boost::geometry::covered_by(*cart_multi_line, *geo2);
    }
    FOREACH_X(item, *cart_multi_point, (result == true)) {
      result = boost::geometry::covered_by(*item, *geo2);
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomMultiPolygon, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point = NULL;
  ObCartesianMultilinestring *cart_multi_line = NULL;
  ObCartesianMultipolygon *cart_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point,
                                                                           cart_multi_line, cart_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    result = true;
    const ObWkbGeomMultiPolygon *geo2 = reinterpret_cast<const ObWkbGeomMultiPolygon *>(g2->val());
    if (!cart_multi_poly->empty()) {
      result = boost::geometry::covered_by(*cart_multi_poly, *geo2);
    }
    if (result == true && !cart_multi_line->empty()) {
      result = boost::geometry::covered_by(*cart_multi_line, *geo2);
    }
    FOREACH_X(item, *cart_multi_point, (result == true)) {
      result = boost::geometry::covered_by(*item, *geo2);
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeomCollection, ObWkbGeomCollection, bool)
{
  INIT_SUCC(ret);
  ObCartesianMultipoint *cart_multi_point1 = NULL;
  ObCartesianMultilinestring *cart_multi_line1 = NULL;
  ObCartesianMultipolygon *cart_multi_poly1 = NULL;
  ObCartesianMultipoint *cart_multi_point2 = NULL;
  ObCartesianMultilinestring *cart_multi_line2 = NULL;
  ObCartesianMultipolygon *cart_multi_poly2 = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo1, cart_multi_point1,
                                                                           cart_multi_line1, cart_multi_poly1))) {
    LOG_WARN("failed to do gc1 prepare", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObCartesianGeometrycollection>(context, geo2, cart_multi_point2,
                                                                                  cart_multi_line2, cart_multi_poly2))) {
    LOG_WARN("failed to do gc2 prepare", K(ret));
  } else {
    result = true;
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs != NULL ? srs->get_srid() : 0;
    ObCartesianMultipoint diff_geo1(srid, *allocator);
    boost::geometry::difference(*cart_multi_point1, *cart_multi_point2, diff_geo1);
    ObCartesianMultipoint diff_geo2(srid, *allocator);
    boost::geometry::difference(diff_geo1, *cart_multi_line2, diff_geo2);
    ObCartesianMultipoint diff_geo3(srid, *allocator);
    boost::geometry::difference(diff_geo2, *cart_multi_poly2, diff_geo3);
    if (!diff_geo3.empty()) {
      result = false;
    } else {
      ObCartesianMultilinestring diff_line1(srid, *allocator);
      boost::geometry::difference(*cart_multi_line1, *cart_multi_line2, diff_line1);
      ObCartesianMultilinestring diff_line2(srid, *allocator);
      boost::geometry::difference(diff_line1, *cart_multi_poly2, diff_line2);
      if (!diff_line2.empty()) {
        result = false;
      } else {
        ObCartesianMultipolygon diff_poly(srid, *allocator);
        boost::geometry::difference(*cart_multi_poly1, *cart_multi_poly2, diff_poly);
        if (!diff_poly.empty()) {
          result = false;
        }
      }
    }
  }
  return ret;
} OB_GEO_FUNC_END;

int ob_geo_func_covered_by_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncCoveredByImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

}  // namespace common
}  // namespace oceanbase
