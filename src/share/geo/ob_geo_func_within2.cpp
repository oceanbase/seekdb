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

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

// geog_point
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogPoint, bool)
{
  return apply_bg_within<ObWkbGeogPoint, ObWkbGeogPoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogLineString, bool)
{
  return apply_bg_within_geog<ObWkbGeogPoint, ObWkbGeogLineString>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogPoint, ObWkbGeogPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, bool)
{
  return apply_bg_within<ObWkbGeogPoint, ObWkbGeogMultiPoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogMultiLineString, bool)
{
  return apply_bg_within_geog<ObWkbGeogPoint, ObWkbGeogMultiLineString>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogMultiPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogPoint, ObWkbGeogMultiPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

static int ob_geo_func_within_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPoint, ObWkbGeogCollection, bool)
{
  return eval_within_geometry_collection<ObWkbGeogCollection>(g1, g2, context, result, ob_geo_func_within_eval_wkb_geog);
} OB_GEO_FUNC_END;

// geog_linestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogLineString, bool)
{
  return apply_bg_within_geog<ObWkbGeogLineString,
                                           ObWkbGeogLineString>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogLineString,
                                           ObWkbGeogPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, bool)
{
  return apply_bg_within_geog<ObWkbGeogLineString,
                                           ObWkbGeogMultiLineString>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogMultiPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogLineString,
                                           ObWkbGeogMultiPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogLineString, ObWkbGeogCollection, bool)
{
  return ob_caculate_ml_within_gc_geog<ObWkbGeogLineString, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geog_polygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogPolygon,
                                           ObWkbGeogPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;


OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogMultiLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogMultiPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogPolygon,
                                           ObWkbGeogMultiPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;


OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogPolygon, ObWkbGeogCollection, bool)
{
  return ob_caculate_mpl_within_gc_geog<ObWkbGeogPolygon, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geog_multipoint
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, bool)
{
  return ob_caculate_mp_within_p<ObWkbGeogMultiPoint, ObWkbGeogPoint> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogLineString, bool)
{
  return ob_caculate_mp_within_l_a_geog<ObWkbGeogMultiPoint, ObWkbGeogLineString> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogPolygon, bool)
{
  return ob_caculate_mp_within_l_a_geog<ObWkbGeogMultiPoint, ObWkbGeogPolygon> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, bool)
{
  return ob_caculate_mp_within_mp<ObWkbGeogMultiPoint> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiLineString, bool)
{
  return ob_caculate_mp_within_l_a_geog<ObWkbGeogMultiPoint,
                                        ObWkbGeogMultiLineString> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon, bool)
{
  return ob_caculate_mp_within_l_a_geog<ObWkbGeogMultiPoint,
                                        ObWkbGeogMultiPolygon> (g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPoint, ObWkbGeogCollection, bool)
{
  return ob_caculate_mp_within_gc<ObWkbGeogPoint, ObIWkbGeogPoint,
                                  ObWkbGeogCollection, ObWkbGeogMultiPoint>(g1, g2, context, result, ob_geo_func_within_eval_wkb_geog);
} OB_GEO_FUNC_END;

// geog_multilinestring
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, bool)
{
  return apply_bg_within_geog<ObWkbGeogMultiLineString,
                                           ObWkbGeogLineString>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogMultiLineString,
                                           ObWkbGeogPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, bool)
{
  return apply_bg_within_geog<ObWkbGeogMultiLineString,
                                           ObWkbGeogMultiLineString>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogMultiLineString,
                                           ObWkbGeogMultiPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiLineString, ObWkbGeogCollection, bool)
{
  return ob_caculate_ml_within_gc_geog<ObWkbGeogMultiLineString, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geog_multipolygon
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogMultiPolygon,
                                           ObWkbGeogPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPoint, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiLineString, bool)
{
  UNUSED(context);
  result = false;
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPolygon, bool)
{
  return apply_bg_within_geog<ObWkbGeogMultiPolygon,
                                           ObWkbGeogMultiPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;


OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogMultiPolygon, ObWkbGeogCollection, bool)
{
  return ob_caculate_mpl_within_gc_geog<ObWkbGeogMultiPolygon, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

// geog_geometrycollection
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogPoint, bool)
{
  return ob_caculate_gc_within_p<ObWkbGeogPoint, ObGeographGeometrycollection>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogLineString, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeogLineString *geo2 = reinterpret_cast<const ObWkbGeogLineString *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context,
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
    ret = eval_wkb_binary_geog(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between GeogMultiPoint and GeogLineString", K(ret));
    } else if (mp_within_l) {
      result = multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2);
    } else if (bg::within(*multi_line, *geo2)){
      bool covered = true;
      ObGeographMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObGeographMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = covered;
    } else {
      result = false;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogPolygon, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeogPolygon *geo2 = reinterpret_cast<const ObWkbGeogPolygon *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context,
                                                                                 geo1,
                                                                                 multi_point,
                                                                                 multi_line,
                                                                                 multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
    LOG_WARN("failed to convert geo tree to binary", K(ret));
  } else {
    bool mp_within_l = false;
    ret = eval_wkb_binary_geog(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between multipoint and polygon", K(ret));
    } else if (mp_within_l) {
      result = (multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2)) &&
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_line, *geo2)) {
      bool covered = true;
      ObGeographMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObGeographMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = !covered ? false :
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_poly, *geo2)) {
      bool covered = true;
      ObGeographMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObGeographMultipoint::value_type& point = *iter;
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

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogMultiPoint, bool)
{
  return ob_caculate_gc_within_mpt<ObGeographGeometrycollection>(g1, g2, context, result, ob_geo_func_within_eval_wkb_geog);
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogMultiLineString, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeogMultiLineString *geo2 = reinterpret_cast<const ObWkbGeogMultiLineString *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context,
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
    ret = eval_wkb_binary_geog(multi_point_bin, g2, context, mp_within_l);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between GeogMultiPoint and GeogMultiLineString", K(ret));
    } else if (mp_within_l) {
      result = multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2);
    } else if (bg::within(*multi_line, *geo2)){
      bool covered = true;
      ObGeographMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        typename ObGeographMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = covered;
    } else {
      result = false;
    }
  }
  return ret;
} OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogMultiPolygon, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *multi_point = NULL;
  ObGeographMultilinestring *multi_line = NULL;
  ObGeographMultipolygon *multi_poly = NULL;
  common::ObIAllocator *allocator = context.get_allocator();
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  const ObWkbGeogMultiPolygon *geo2 = reinterpret_cast<const ObWkbGeogMultiPolygon *>(g2->val());
  ObGeometry *multi_point_bin = NULL;
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context,
                                                                                 geo1,
                                                                                 multi_point,
                                                                                 multi_line,
                                                                                 multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
    LOG_WARN("failed to convert geo tree to binary", K(ret));
  } else {
    bool mp_within_poly = false;
    ret = eval_wkb_binary_geog(multi_point_bin, g2, context, mp_within_poly);
    if (OB_FAIL(ret)) {
      LOG_WARN("failed to do within by functor between multipoint and polygon", K(ret));
    } else if (mp_within_poly) {
      result = (multi_line->empty() ||
               bg::covered_by(*multi_line, *geo2)) &&
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_line, *geo2)){
      bool covered = true;
      ObGeographMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObGeographMultipoint::value_type& point = *iter;
        covered = bg::covered_by(point, *geo2);
      }
      result = !covered ? false :
               (multi_poly->empty() ||
               bg::covered_by(*multi_poly, *geo2));
    } else if (bg::within(*multi_poly, *geo2)) {
      bool covered = true;
      ObGeographMultipoint::iterator iter = multi_point->begin();
      for (; iter != multi_point->end() && covered; ++iter) {
        ObGeographMultipoint::value_type& point = *iter;
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

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncWithinImpl, ObWkbGeogCollection, ObWkbGeogCollection, bool)
{
  INIT_SUCC(ret);
  ObGeographMultipoint *g1_multi_point = NULL;
  ObGeographMultilinestring *g1_multi_line = NULL;
  ObGeographMultipolygon *g1_multi_poly = NULL;
  ObGeographMultipoint *g2_multi_point = NULL;
  ObGeographMultilinestring *g2_multi_line = NULL;
  ObGeographMultipolygon *g2_multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context,
                                                                          geo1,
                                                                          g1_multi_point,
                                                                          g1_multi_line,
                                                                          g1_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<ObGeographGeometrycollection>(context,
                                                                                 geo2,
                                                                                 g2_multi_point,
                                                                                 g2_multi_line,
                                                                                 g2_multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
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
        ObGeometry * res_geo6 = NULL;
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
            ret = eval_wkb_binary_geog(g1_multi_point_bin, g2, context, mp_within_gc);
            if (OB_FAIL(ret)) {
              LOG_WARN("failed to do within by functor between GeogMultiPoint and GeogCollection", K(ret));
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

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncWithinImpl, ObCartesianPoint, ObCartesianPolygon, bool)
{
  return apply_bg_within<ObCartesianPoint, ObCartesianPolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

static int ob_geo_func_within_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncWithinImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

static int ob_geo_func_within_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncWithinImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncWithinImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_within_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

int ObGeoFuncWithin::eval(const ObGeoEvalCtx &gis_context, bool &result)
{
  return ObGeoFuncWithinImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_within_eval_wkb_cart,
      ob_geo_func_within_eval_wkb_geog,
      ob_geo_func_within_eval_tree);
}

} // namespace common
} // namespace oceanbase
