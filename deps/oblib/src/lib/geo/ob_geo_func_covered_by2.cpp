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
#include "lib/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

// geographic point
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPoint, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  result = false;
  const ObWkbGeogPoint *geo1 = reinterpret_cast<const ObWkbGeogPoint *>(g1->val());
  const ObWkbGeogCollection *geo2 = reinterpret_cast<const ObWkbGeogCollection *>(g2->val());
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObPlPaStrategy point_strategy(geog_sphere);
  ObWkbGeogCollection::iterator iter = geo2->begin();
  typename ObWkbGeogCollection::const_pointer sub_ptr;
  for (; iter != geo2->end() && (result == false) && OB_SUCC(ret); ++iter) {
    sub_ptr = iter.operator->();
    ObGeoType sub_type = geo2->get_sub_type(sub_ptr);
    switch (sub_type) {
      case ObGeoType::POINT :
      case ObGeoType::LINESTRING :
      case ObGeoType::MULTIPOINT :
      case ObGeoType::MULTILINESTRING :
      case ObGeoType::GEOMETRYCOLLECTION :{
        ObGeometry *sub_g2 = NULL;
        common::ObIAllocator *allocator = context.get_allocator();
        if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(*allocator, sub_type, true, true, sub_g2))) {
        } else {
          // Length is not used, cannot get real length until iter move to the next
          ObString wkb_nosrid(WKB_COMMON_WKB_HEADER_LEN, reinterpret_cast<const char *>(sub_ptr));
          sub_g2->set_data(wkb_nosrid);
          sub_g2->set_srid(g2->get_srid());
          if (OB_FAIL(eval_wkb_binary_geog(g1, sub_g2, context, result))) {
          }
        }
        break;
      }

      case ObGeoType::POLYGON : {
        ObArenaAllocator tmp_alloc;
        const ObWkbGeogPolygon *polygon = reinterpret_cast<const ObWkbGeogPolygon*>(sub_ptr);
        ObString tmp(polygon->length(), reinterpret_cast<const char*>(sub_ptr));
        ObString pol_data;
        if (OB_FAIL(ob_write_string(tmp_alloc, tmp, pol_data))) {
        } else {
          ObWkbGeogPolygon *poly_copy = reinterpret_cast<ObWkbGeogPolygon*>(pol_data.ptr());
          boost::geometry::strategy::area::geographic<> area_strategy(geog_sphere);
#ifdef USE_SPHERE_GEO
          boost::geometry::correct(*poly_copy, area_strategy);
          result = boost::geometry::covered_by(*geo1, *poly_copy, point_strategy);
#else
          boost::geometry::correct(*poly_copy);
          result = boost::geometry::covered_by(*geo1, *poly_copy);
#endif
        }
        break;
      }
      case ObGeoType::MULTIPOLYGON : {
        ObArenaAllocator tmp_alloc;
        const ObWkbGeogMultiPolygon *multi_poly = reinterpret_cast<const ObWkbGeogMultiPolygon*>(sub_ptr);
        ObString tmp(multi_poly->length(), reinterpret_cast<const char*>(sub_ptr));
        ObString multipol_data;
        if (OB_FAIL(ob_write_string(tmp_alloc, tmp, multipol_data))) {
        } else {
          ObWkbGeogMultiPolygon *multipoly_copy = reinterpret_cast<ObWkbGeogMultiPolygon*>(multipol_data.ptr());
          boost::geometry::strategy::area::geographic<> area_strategy(geog_sphere);
#ifdef USE_SPHERE_GEO
          boost::geometry::correct(*multipoly_copy, area_strategy);
          result = boost::geometry::covered_by(*geo1, *multipoly_copy, point_strategy);
#else
          boost::geometry::correct(*multipoly_copy);
          result = boost::geometry::covered_by(*geo1, *multipoly_copy);
#endif
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

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPoint, ObWkbGeogCollection, bool)
{
  return ob_caculate_mp_gc_cover_by<ObWkbGeogPoint, ObIWkbGeogPoint,
    ObWkbGeogCollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPoint, ObWkbGeogPoint, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeogPoint, ObWkbGeogPoint>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by<ObWkbGeogPoint, ObWkbGeogMultiPoint>(g1, g2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPoint, bool)
{
  result = ob_apply_bg_covered_by_with_pl_strategy<ObWkbGeogPoint, GeoType2>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, bool)
{
  UNUSED(context);
  const ObWkbGeogMultiPoint *geo1 = reinterpret_cast<const ObWkbGeogMultiPoint *>(g1->val());
  const ObWkbGeogPoint *geo2 = reinterpret_cast<const ObWkbGeogPoint *>(g2->val());
  //check if multipoint is equals to point geographically(msyql)
  result = boost::geometry::equals(*geo1, *geo2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, bool)
{
  const ObWkbGeogMultiPoint *geo1 = reinterpret_cast<const ObWkbGeogMultiPoint *>(g1->val());
  const ObWkbGeogMultiPoint *geo2 = reinterpret_cast<const ObWkbGeogMultiPoint *>(g2->val());
  // travel every point in multipoint; check if any one is not covered by geo2(postgis)
  result = true;
  FOREACH_X(item, *geo1, (result == true)) {
    result = boost::geometry::covered_by(*item, *geo2);
  }
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPoint, bool)
{
  const ObSrsItem *srs = context.get_srs();
  const ObWkbGeogMultiPoint *geo1 = reinterpret_cast<const ObWkbGeogMultiPoint *>(g1->val());
  const GeoType2 *geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  // travel every point in multipoint; check if any one is not covered by geo2(postgis)
  result = true;
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObPlPaStrategy point_strategy(geog_sphere);
  FOREACH_X(item, *geo1, (result == true)) {
#ifdef USE_SPHERE_GEO
  result = boost::geometry::covered_by(*item, *geo2, point_strategy);
#else
    result = boost::geometry::covered_by(*item, *geo2);
#endif
  }
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogLineString, ObWkbGeogLineString, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogLineString, ObWkbGeogLineString>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogLineString, ObWkbGeogPolygon, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogLineString, ObWkbGeogPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogLineString, ObWkbGeogMultiLineString>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogLineString, ObWkbGeogMultiPolygon, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogLineString, ObWkbGeogMultiPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogLineString, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  const ObSrsItem *srs = context.get_srs();
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo2, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else {
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs->get_srid();
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    const ObWkbGeogLineString *geo1 = reinterpret_cast<const ObWkbGeogLineString *>(g1->val());
    ObGeographMultilinestring res_geo1(srid, *allocator);
    ObGeographMultilinestring res_geo2(srid, *allocator);
#ifdef USE_SPHERE_GEO
    boost::geometry::difference(*geo1, *multi_line, res_geo1, line_strategy);
    boost::geometry::difference(res_geo1, *multi_poly, res_geo2, line_strategy);
#else
    boost::geometry::difference(*geo1, *multi_line, res_geo1);
    boost::geometry::difference(res_geo1, *multi_poly, res_geo2);
#endif
    result = res_geo2.is_empty();
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPolygon, ObWkbGeogPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogPolygon, ObWkbGeogPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPolygon, ObWkbGeogMultiPolygon, bool)
{
  UNUSED(context);
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogPolygon, ObWkbGeogMultiPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogPolygon, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo2, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else {
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs->get_srid();
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    const ObWkbGeogPolygon *geo1 = reinterpret_cast<const ObWkbGeogPolygon *>(g1->val());
    boost::geometry::strategy::area::geographic<> area_strategy(geog_sphere);
    boost::geometry::correct(*const_cast<ObWkbGeogPolygon *>(geo1), area_strategy);
    result = boost::geometry::covered_by(*geo1, *multi_poly);
  }
  return ret;
} OB_GEO_FUNC_END;

// wkb geog multilinestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogMultiLineString, ObWkbGeogLineString>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiLineString, ObWkbGeogPolygon, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogMultiLineString, ObWkbGeogPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogMultiLineString,
                                                   ObWkbGeogMultiLineString>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPolygon, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogMultiLineString,
                                                   ObWkbGeogMultiPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiLineString, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo2, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else {
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs->get_srid();
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    const ObWkbGeogMultiLineString *geo1 = reinterpret_cast<const ObWkbGeogMultiLineString *>(g1->val());
    ObGeographMultilinestring res_geo1(srid, *allocator);
    ObGeographMultilinestring res_geo2(srid, *allocator);
#ifdef USE_SPHERE_GEO
    boost::geometry::difference(*geo1, *multi_line, res_geo1, line_strategy);
    boost::geometry::difference(res_geo1, *multi_poly, res_geo2, line_strategy);
#else
    boost::geometry::difference(*geo1, *multi_line, res_geo1);
    boost::geometry::difference(res_geo1, *multi_poly, res_geo2);
#endif
    result = res_geo2.is_empty();
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPolygon, ObWkbGeogPolygon, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogMultiPolygon, ObWkbGeogPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, bool)
{
  result = ob_apply_bg_covered_by_with_ll_strategy<ObWkbGeogMultiPolygon , ObWkbGeogMultiPolygon>(g1, g2, context);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogMultiPolygon, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo2, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else {
    ObIAllocator *allocator = context.get_allocator();
    uint32_t srid = srs->get_srid();
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    const ObWkbGeogMultiPolygon *geo1 = reinterpret_cast<const ObWkbGeogMultiPolygon *>(g1->val());
    boost::geometry::strategy::area::geographic<> area_strategy(geog_sphere);
    boost::geometry::correct(*const_cast<ObWkbGeogMultiPolygon *>(geo1), area_strategy);
    result = boost::geometry::covered_by(*geo1, *multi_poly);
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogPoint, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else if (!multi_poly->empty() || !multi_line->empty()) {
    result = false;
  } else if (multi_point->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeogPoint *geo2 = reinterpret_cast<const ObWkbGeogPoint *>(g2->val());
    FOREACH_X(item, *multi_point, (result == true)) {
      result = boost::geometry::covered_by(*item, *geo2);
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogLineString, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else if (!multi_poly->empty()) {
    result = false;
  } else if (multi_point->empty() && multi_line->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeogLineString *geo2 = reinterpret_cast<const ObWkbGeogLineString *>(g2->val());
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    if (!multi_line->empty()) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*multi_line, *geo2, line_strategy);
#else
      result = boost::geometry::covered_by(*multi_line, *geo2);
#endif
    }
    ObPlPaStrategy point_strategy(geog_sphere);
    FOREACH_X(item, *multi_point, (result == true)) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*item, *geo2, point_strategy);
#else
      result = boost::geometry::covered_by(*item, *geo2);
#endif
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogPolygon, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else {
    result = true;
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    const ObWkbGeogPolygon *geo2 = reinterpret_cast<const ObWkbGeogPolygon *>(g2->val());
    if (!multi_poly->empty()) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*multi_poly, *geo2, line_strategy);
#else
      result = boost::geometry::covered_by(*multi_poly, *geo2);
#endif
    }
    if (result == true && !multi_line->empty()) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*multi_line, *geo2, line_strategy);
#else
      result = boost::geometry::covered_by(*multi_line, *geo2);
#endif
    }
    ObPlPaStrategy point_strategy(geog_sphere);
    FOREACH_X(item, *multi_point, (result == true)) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*item, *geo2, point_strategy);
#else
      result = boost::geometry::covered_by(*item, *geo2);
#endif
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogMultiPoint, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else if (!multi_poly->empty() || !multi_line->empty()) {
    result = false;
  } else if (multi_point->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeogMultiPoint *geo2 = reinterpret_cast<const ObWkbGeogMultiPoint *>(g2->val());
    FOREACH_X(item1, *multi_point, (result == true)) {
      bool loop_res = false;
      FOREACH_X(item2, *geo2, (loop_res == false)) {
        loop_res = boost::geometry::covered_by(*item1, *item2);
      }
      result = loop_res;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogMultiLineString, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
      const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else if (!multi_poly->empty()) {
    result = false;
  } else if (multi_point->empty() && multi_line->empty()) {
    result = true;
  } else {
    result = true;
    const ObWkbGeogMultiLineString *geo2 = reinterpret_cast<const ObWkbGeogMultiLineString *>(g2->val());
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    if (!multi_line->empty()) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*multi_line, *geo2, line_strategy);
#else
      result = boost::geometry::covered_by(*multi_line, *geo2);
#endif
    }
    ObPlPaStrategy point_strategy(geog_sphere);
    FOREACH_X(item, *multi_point, (result == true)) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*item, *geo2, point_strategy);
#else
      result = boost::geometry::covered_by(*item, *geo2);
#endif
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogMultiPolygon, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
        const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point,
                                                                                 multi_line, multi_poly))) {
  } else {
    result = true;
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObLlLaAaStrategy line_strategy(geog_sphere);
    const ObWkbGeogMultiPolygon *geo2 = reinterpret_cast<const ObWkbGeogMultiPolygon *>(g2->val());
    if (!multi_poly->empty()) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*multi_poly, *geo2, line_strategy);
#else
      result = boost::geometry::covered_by(*multi_poly, *geo2);
#endif
    }
    if (result == true && !multi_line->empty()) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*multi_line, *geo2, line_strategy);
#else
      result = boost::geometry::covered_by(*multi_line, *geo2);
#endif
    }
    ObPlPaStrategy point_strategy(geog_sphere);
    FOREACH_X(item, *multi_point, (result == true)) {
#ifdef USE_SPHERE_GEO
      result = boost::geometry::covered_by(*item, *geo2, point_strategy);
#else
      result = boost::geometry::covered_by(*item, *geo2);
#endif
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObWkbGeogCollection, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point1 = NULL;
  ObGeographMultilinestring *multi_line1 = NULL;
  ObGeographMultipolygon *multi_poly1 = NULL;
  ObGeographMultipoint *multi_point2 = NULL;
  ObGeographMultilinestring *multi_line2 = NULL;
  ObGeographMultipolygon *multi_poly2 = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo1, multi_point1,
                                                                                 multi_line1, multi_poly1))) {
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context, geo2, multi_point2,
                                                                                 multi_line2, multi_poly2))) {
  } else {
    result = true;
    ObIAllocator *allocator = context.get_allocator();
    boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    ObPlPaStrategy point_strategy(geog_sphere);
    uint32_t srid = srs->get_srid();
    ObGeographMultipoint diff_geo1(srid, *allocator);
    boost::geometry::difference(*multi_point1, *multi_point2, diff_geo1);
    ObGeographMultipoint diff_geo2(srid, *allocator);
    ObGeographMultipoint diff_geo3(srid, *allocator);
#ifdef USE_SPHERE_GEO
    boost::geometry::difference(diff_geo1, *multi_line2, diff_geo2, point_strategy);
    boost::geometry::difference(diff_geo2, *multi_poly2, diff_geo3, point_strategy);
#else
    boost::geometry::difference(diff_geo1, *multi_line2, diff_geo2);
    boost::geometry::difference(diff_geo2, *multi_poly2, diff_geo3);
#endif
    if (!diff_geo3.empty()) {
      result = false;
    } else {
      ObLlLaAaStrategy line_strategy(geog_sphere);
      ObGeographMultilinestring diff_line1(srid, *allocator);
      ObGeographMultilinestring diff_line2(srid, *allocator);
#ifdef USE_SPHERE_GEO
      boost::geometry::difference(*multi_line1, *multi_line2, diff_line1, line_strategy);
      boost::geometry::difference(diff_line1, *multi_poly2, diff_line2, line_strategy);
#else
      boost::geometry::difference(*multi_line1, *multi_line2, diff_line1);
      boost::geometry::difference(diff_line1, *multi_poly2, diff_line2);
#endif
      if (!diff_line2.empty()) {
        result = false;
      } else {
        ObGeographMultipolygon diff_poly(srid, *allocator);
#ifdef USE_SPHERE_GEO
        boost::geometry::difference(*multi_poly1, *multi_poly2, diff_poly, line_strategy);
#else
        boost::geometry::difference(*multi_poly1, *multi_poly2, diff_poly);
#endif
        if (!diff_poly.empty()) {
          result = false;
        }
      }
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncCoveredByImpl, ObCartesianLineString, ObCartesianPolygon, bool)
{
  UNUSED(context);
  const ObCartesianLineString *geo1 = reinterpret_cast<const ObCartesianLineString *>(g1);
  const ObCartesianPolygon *geo2 = reinterpret_cast<const ObCartesianPolygon *>(g2);
  result = boost::geometry::covered_by(*geo1, *geo2);
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

static int ob_geo_func_covered_by_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncCoveredByImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

static int ob_geo_func_covered_by_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncCoveredByImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncCoveredByImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_covered_by_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

int ObGeoFuncCoveredBy::eval(const ObGeoEvalCtx &gis_context, bool &result)
{
  return ObGeoFuncCoveredByImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_covered_by_eval_wkb_cart,
      ob_geo_func_covered_by_eval_wkb_geog,
      ob_geo_func_covered_by_eval_tree);
}

} // sql
} // oceanbase
