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

#include "ob_geo_func_within.h"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{
namespace bg = boost::geometry;

static int do_multi_difference(const ObSrsItem *srs,
                                        const ObGeoEvalCtx &context,
                                        ObGeometry *geo,
                                        ObGeometry *mpt,
                                        ObGeometry *ml,
                                        ObGeometry *mpo,
                                        ObGeometry *&res_geo)
{
  INIT_SUCC(ret);
  ObGeometry *res_geo1 = NULL;
  ObGeometry *res_geo2 = NULL;
  ObGeoEvalCtx gis_context1(context.get_mem_ctx(), srs);
  ObGeoEvalCtx gis_context2(context.get_mem_ctx(), srs);
  ObGeoEvalCtx gis_context3(context.get_mem_ctx(), srs);
  if (OB_NOT_NULL(mpt)) {
    if (OB_FAIL(gis_context1.append_geo_arg(reinterpret_cast<ObGeometry *>(geo))) || OB_FAIL(gis_context1.append_geo_arg(mpt))) {
      LOG_WARN("build gis context failed", K(ret), K(gis_context1.get_geo_count()));
    } else if (OB_FAIL(ObGeoFuncDifference::eval(gis_context1, res_geo1))) {
      LOG_WARN("eval st intersection failed", K(ret));
    }
  } else {
    res_geo1 = geo;
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_NOT_NULL(ml)) {
    if (OB_FAIL(gis_context2.append_geo_arg(res_geo1)) || OB_FAIL(gis_context2.append_geo_arg(ml))) {
      LOG_WARN("build gis context failed", K(ret), K(gis_context2.get_geo_count()));
    } else if (OB_FAIL(ObGeoFuncDifference::eval(gis_context2, res_geo2))) {
      LOG_WARN("eval st intersection failed", K(ret));
    }
  } else {
    res_geo2 = res_geo1;
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_NOT_NULL(mpo)) {
    if (OB_FAIL(gis_context3.append_geo_arg(res_geo2)) || OB_FAIL(gis_context3.append_geo_arg(mpo))) {
      LOG_WARN("build gis context failed", K(ret), K(gis_context3.get_geo_count()));
    } else if (OB_FAIL(ObGeoFuncDifference::eval(gis_context3, res_geo))) {
      LOG_WARN("eval st intersection failed", K(ret));
    }
  } else {
    res_geo = res_geo2;
  }
  return ret;
}


template <typename GeoType1, typename GeoType2>
static int apply_bg_within(const ObGeometry *g1,
                           const ObGeometry *g2,
                           const ObGeoEvalCtx &context,
                           bool &result)
{
  UNUSED(context);
  int ret = OB_SUCCESS;
  const GeoType1 *geo1 = nullptr;
  const GeoType2 *geo2 = nullptr;
  if (!g1->is_tree()) {
    geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
    geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  } else {
    geo1 = reinterpret_cast<GeoType1 *>(const_cast<ObGeometry *>(g1));
    geo2 = reinterpret_cast<GeoType2 *>(const_cast<ObGeometry *>(g2));
  }
  if (OB_ISNULL(geo1) || OB_ISNULL(geo2)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("geometry can not be null", K(ret), K(geo1), K(geo2));
  } else {
    result = bg::within(*geo1, *geo2);
  }
  return ret;
}

template <typename GeoType1, typename GeoType2>
static int apply_bg_within_geog(const ObGeometry *g1,
                                const ObGeometry *g2,
                                const ObGeoEvalCtx &context,
                                bool &result)
{
  INIT_SUCC(ret);
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else {
    const GeoType1 *geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
    const GeoType2 *geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
    result = bg::within(*geo1, *geo2);
  }
  return ret;
}

template<typename MpType, typename PType>
static int ob_caculate_mp_within_p(const ObGeometry *g1, const ObGeometry *g2,
                                   const ObGeoEvalCtx &context, bool &result)
{
  UNUSED(context);
  const MpType *geo1 = reinterpret_cast<const MpType *>(g1->val());
  const PType *geo2 = reinterpret_cast<const PType *>(g2->val());
  result = bg::equals(*geo2, *geo1);
  return OB_SUCCESS;
}

template<typename MpType>
static int ob_caculate_mp_within_mp(const ObGeometry *g1, const ObGeometry *g2,
                                    const ObGeoEvalCtx &context, bool &result)
{
  bool within = true;
  const MpType *geo1 = reinterpret_cast<const MpType *>(g1->val());
  const MpType *geo2 = reinterpret_cast<const MpType *>(g2->val());
  typename MpType::iterator iter = geo1->begin();
  for (; iter != geo1->end() && within; ++iter) {
    typename MpType::value_type& point = *iter;
    within = bg::within(point, *geo2);
  }
  result = within;
  return OB_SUCCESS;
}

template<typename MpType, typename GEO_TYPE2>
static int ob_caculate_mp_within_l_a(const ObGeometry *g1, const ObGeometry *g2,
                                     const ObGeoEvalCtx &context, bool &result)
{
  bool within = false;
  bool intersects = false;
  const MpType *geo1 = reinterpret_cast<const MpType *>(g1->val());
  const GEO_TYPE2 *geo2 = reinterpret_cast<const GEO_TYPE2 *>(g2->val());
  typename MpType::iterator iter = geo1->begin();
  for (; iter != geo1->end(); ++iter) {
    typename MpType::value_type& point = *iter;
    if (!within) {
      within = bg::within(point, *geo2);
      if (!within) {
        intersects = bg::intersects(point, *geo2);
      } else {
        intersects = true;
      }
    } else {
      intersects = bg::intersects(point, *geo2);
    }
    if (!intersects) break;
  }
  result = (within && intersects);
  return OB_SUCCESS;
}

template<typename MpType, typename GEO_TYPE2>
static int ob_caculate_mp_within_l_a_geog(const ObGeometry *g1, const ObGeometry *g2,
                                          const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  bool within = false;
  bool intersects = false;
  const MpType *geo1 = reinterpret_cast<const MpType *>(g1->val());
  const GEO_TYPE2 *geo2 = reinterpret_cast<const GEO_TYPE2 *>(g2->val());
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else {
    typename MpType::iterator iter = geo1->begin();
    for (; iter != geo1->end(); ++iter) {
      typename MpType::value_type& point = *iter;
      if (!within) {
        within = bg::within(point, *geo2);
        if (!within) {
          intersects = bg::intersects(point, *geo2);
        } else {
          intersects = true;
        }
      } else {
        intersects = bg::intersects(point, *geo2);
      }
      if (!intersects) break;
    }
    result = (within && intersects);
  }
  return ret;
}

template<typename GEO_TYPE1, typename GCType>
static int ob_caculate_ml_within_gc(const ObGeometry *g1, const ObGeometry *g2,
                                    const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  typename GCType::sub_mpt_type *multi_point = NULL;
  typename GCType::sub_ml_type *multi_line = NULL;
  typename GCType::sub_mp_type *multi_poly = NULL;
  GEO_TYPE1 *geo1 = const_cast<GEO_TYPE1 *>(reinterpret_cast<const GEO_TYPE1 *>(g1->val()));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GCType>(context,
                                                    geo2,
                                                    multi_point,
                                                    multi_line,
                                                    multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    ObGeometry *res_geo2 = NULL;
    ObGeoToTreeVisitor visitor(allocator);
    ObGeometry *i_geo1 = const_cast<ObGeometry *>(g1);
    if (OB_FAIL(i_geo1->do_visit(visitor))) {
      LOG_WARN("failed to do geo2 to_tree visit", K(ret));
    } else if (OB_FAIL(do_multi_difference(srs, context, visitor.get_geometry(),
                                           NULL,
                                           reinterpret_cast<ObGeometry *>(multi_line),
                                           reinterpret_cast<ObGeometry *>(multi_poly),
                                           res_geo2))) {
      LOG_WARN("failed to do mulit difference", K(ret));
    } else {
      bg::de9im::mask mask("T********");
      result = res_geo2->is_empty() &&
              (bg::relate(*geo1, *multi_line, mask) ||
              bg::relate(*geo1, *multi_poly, mask));
    }
  }
  return ret;
}

template<typename GEO_TYPE1, typename GCType>
static int ob_caculate_ml_within_gc_geog(const ObGeometry *g1, const ObGeometry *g2,
                                         const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  typename GCType::sub_mpt_type *multi_point = NULL;
  typename GCType::sub_ml_type *multi_line = NULL;
  typename GCType::sub_mp_type *multi_poly = NULL;
  GEO_TYPE1 *geo1 = const_cast<GEO_TYPE1 *>(reinterpret_cast<const GEO_TYPE1 *>(g1->val()));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GCType>(context,
                                                           geo2,
                                                           multi_point,
                                                           multi_line,
                                                           multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    const ObSrsItem *srs = context.get_srs();
    ObIAllocator *allocator = context.get_allocator();
    ObGeometry *res_geo2 = NULL;
    ObGeoToTreeVisitor visitor(allocator);
    ObGeometry *i_geo1 = const_cast<ObGeometry *>(g1);
    if (OB_FAIL(i_geo1->do_visit(visitor))) {
      LOG_WARN("failed to do geo2 to_tree visit", K(ret));
    } else if (OB_FAIL(do_multi_difference(srs, context, visitor.get_geometry(),
                                           NULL,
                                           reinterpret_cast<ObGeometry *>(multi_line),
                                           reinterpret_cast<ObGeometry *>(multi_poly),
                                           res_geo2))) {
      LOG_WARN("failed to do mulit difference", K(ret));
    } else {
      // Checks relation between a pair of geometries defined by a mask.
      bg::de9im::mask mask("T********");
      result = res_geo2->is_empty() &&
              (bg::relate(*geo1, *multi_line, mask) ||
              bg::relate(*geo1, *multi_poly, mask));
    }
  }
  return ret;
}

template<typename GEO_TYPE1, typename GCType>
static int ob_caculate_mpl_within_gc(const ObGeometry *g1, const ObGeometry *g2,
                                     const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  typename GCType::sub_mpt_type *multi_point = NULL;
  typename GCType::sub_ml_type *multi_line = NULL;
  typename GCType::sub_mp_type *multi_poly = NULL;
  GEO_TYPE1 *geo1 = const_cast<GEO_TYPE1 *>(reinterpret_cast<const GEO_TYPE1 *>(g1->val()));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GCType>(context,
                                                    geo2,
                                                    multi_point,
                                                    multi_line,
                                                    multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    result = bg::within(*geo1, *multi_poly);
  }
  return ret;
}

template<typename GEO_TYPE1, typename GCType>
static int ob_caculate_mpl_within_gc_geog(const ObGeometry *g1, const ObGeometry *g2,
                                          const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  typename GCType::sub_mpt_type *multi_point = NULL;
  typename GCType::sub_ml_type *multi_line = NULL;
  typename GCType::sub_mp_type *multi_poly = NULL;
  GEO_TYPE1 *geo1 = const_cast<GEO_TYPE1 *>(reinterpret_cast<const GEO_TYPE1 *>(g1->val()));
  ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("srs is null in geographic eval", K(ret));
  } else if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GCType>(context,
                                                           geo2,
                                                           multi_point,
                                                           multi_line,
                                                           multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    result = bg::within(*geo1, *multi_poly);
  }
  return ret;
}

template<typename PType, typename GCType>
static int ob_caculate_gc_within_p(const ObGeometry *g1, const ObGeometry *g2,
                                   const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  typename GCType::sub_mpt_type *multi_point = NULL;
  typename GCType::sub_ml_type *multi_line = NULL;
  typename GCType::sub_mp_type *multi_poly = NULL;
  ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  PType *geo2 = const_cast<PType *>(reinterpret_cast<const PType *>(g2->val()));
  if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GCType>(context,
                                                    geo1,
                                                    multi_point,
                                                    multi_line,
                                                    multi_poly))) {
    LOG_WARN("failed to do gc prepare", K(ret));
  } else {
    bool ml_empty = !multi_line || multi_line->empty();
    bool mpy_empty = !multi_poly || multi_poly->empty();
    result = ml_empty && mpy_empty && bg::equals(*geo2, *multi_point);
  }
  return ret;
}

// ----- ObGeoFuncWithinImpl -----
class ObGeoFuncWithinImpl : public ObIGeoDispatcher<bool, ObGeoFuncWithinImpl>
{
public:
  ObGeoFuncWithinImpl();
  virtual ~ObGeoFuncWithinImpl() = default;

  // function pointer type for eval_wkb_binary dispatch
  using WithinBinaryFn = int (*)(const common::ObGeometry *, const common::ObGeometry *,
                                  const ObGeoEvalCtx &, bool &);

  // template for unary
  OB_GEO_UNARY_FUNC_DEFAULT(bool, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_TREE_UNARY_FUNC_DEFAULT(bool, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_CART_TREE_FUNC_DEFAULT(bool, OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS);
  OB_GEO_GEOG_TREE_FUNC_DEFAULT(bool, OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS);

  // template for binary
  // default cases for cartesian
  template <typename GeoType1, typename GeoType2>
  struct EvalWkbBi
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      return apply_bg_within<GeoType1, GeoType2>(g1, g2, context, result);
    }
  };

  // default case for geography
  template <typename GeoType1, typename GeoType2>
  struct EvalWkbBiGeog
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context, result);
      return OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS;
    }
  };
private:
  template <typename CollectonType>
  static int eval_within_geometry_collection(const ObGeometry *g1,
                                             const ObGeometry *g2,
                                             const ObGeoEvalCtx &context,
                                             bool &result,
                                             WithinBinaryFn eval_wkb_fn)
  {
    INIT_SUCC(ret);
    result = false;
    common::ObIAllocator *allocator = context.get_allocator();
    typename CollectonType::iterator iter;
    if (OB_ISNULL(allocator)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("Null allocator", K(ret));
    } else if (g1->type() == ObGeoType::GEOMETRYCOLLECTION) {
      const CollectonType *geo1 = reinterpret_cast<const CollectonType *>(g1->val());
      iter = geo1->begin();
      for (; iter != geo1->end() && OB_SUCC(ret) && (result != true); iter++) {
        typename CollectonType::const_pointer sub_ptr = iter.operator->();
        ObGeoType sub_type = geo1->get_sub_type(sub_ptr);
        ObGeometry *sub_g1 = NULL;
        bool is_geog = (g1->crs() == oceanbase::common::ObGeoCRS::Geographic);
        if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(*allocator, sub_type, is_geog, true, sub_g1))) {
          LOG_WARN("failed to create wkb", K(ret), K(sub_type));
        } else {
          ObString wkb_nosrid(WKB_COMMON_WKB_HEADER_LEN, reinterpret_cast<const char *>(sub_ptr));
          sub_g1->set_data(wkb_nosrid);
          sub_g1->set_srid(g1->get_srid());
        }
        ret = eval_within_geometry_collection<CollectonType>(sub_g1, g2, context, result, eval_wkb_fn);
      }
    } else if (g2->type() == ObGeoType::GEOMETRYCOLLECTION) {
      const CollectonType *geo2 = reinterpret_cast<const CollectonType *>(g2->val());
      iter = geo2->begin();
      for (; iter != geo2->end() && OB_SUCC(ret) && (result != true); iter++) {
        typename CollectonType::const_pointer sub_ptr = iter.operator->();
        ObGeoType sub_type = geo2->get_sub_type(sub_ptr);
        ObGeometry *sub_g2 = NULL;
        bool is_geog = (g2->crs() == oceanbase::common::ObGeoCRS::Geographic);
        if (OB_FAIL(ObGeoTypeUtil::create_geo_by_type(*allocator, sub_type, is_geog, true, sub_g2))) {
          LOG_WARN("failed to create wkb", K(ret), K(sub_type));
        } else {
          ObString wkb_nosrid(WKB_COMMON_WKB_HEADER_LEN, reinterpret_cast<const char *>(sub_ptr));
          sub_g2->set_data(wkb_nosrid);
          sub_g2->set_srid(g2->get_srid());
        }
        ret = eval_within_geometry_collection<CollectonType>(g1, sub_g2, context, result, eval_wkb_fn);
      }
    } else {
      // none of the two geometries are collection type
      ret = eval_wkb_fn(g1, g2, context, result);
    }
    return ret;
  }

  template<typename PointType, typename IPointType, typename GCType, typename MpType>
  static int ob_caculate_mp_within_gc(const ObGeometry *g1, const ObGeometry *g2,
                                      const ObGeoEvalCtx &context, bool &result,
                                      WithinBinaryFn eval_wkb_fn)
  {
    INIT_SUCC(ret);
    bool within = false;
    bool intersects = false;
    const MpType *geo1 = reinterpret_cast<const MpType *>(g1->val());
    FOREACH_X(item, *geo1, OB_SUCC(ret)) {
      PointType point;
      point.byteorder(ObGeoWkbByteOrder::LittleEndian);
      point.template set<0>(item->template get<0>());
      point.template set<1>(item->template get<1>());
      ObString data(sizeof(PointType), reinterpret_cast<char *>(&point));
      IPointType i_point;
      i_point.set_data(data);

      ObGeoEvalCtx intersects_context(context.get_mem_ctx(), context.get_srs());
      intersects_context.append_geo_arg(&i_point);
      intersects_context.append_geo_arg(g2);
      if (!within) {
        ret = eval_wkb_fn(&i_point, g2, context, within);
        if (OB_FAIL(ret)) {
          LOG_WARN("failed to do within by functor between Point and collection", K(ret));
        } else {
          if (!within) {
            if (OB_FAIL(ObGeoFuncIntersects::eval(intersects_context, intersects))) {
              LOG_WARN("eval intersects for within failed", K(ret));
            }
          } else {
            intersects = true;
          }
        }
      } else if (OB_FAIL(ObGeoFuncIntersects::eval(intersects_context, intersects))) {
        LOG_WARN("eval intersects for within failed", K(ret));
      }
      if (!intersects) break;
    }
    result = (within && intersects);
    return ret;
  }

  template<typename GCType>
  static int ob_caculate_gc_within_mpt(const ObGeometry *g1, const ObGeometry *g2,
                                       const ObGeoEvalCtx &context, bool &result,
                                       WithinBinaryFn eval_wkb_fn)
  {
    INIT_SUCC(ret);
    typename GCType::sub_mpt_type *multi_point = NULL;
    typename GCType::sub_ml_type *multi_line = NULL;
    typename GCType::sub_mp_type *multi_poly = NULL;
    common::ObIAllocator *allocator = context.get_allocator();
    ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
    const ObSrsItem *srs = context.get_srs();
    if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GCType>(context, geo1, multi_point, multi_line, multi_poly))) {
      LOG_WARN("failed to do gc prepare", K(ret));
    } else {
      result = multi_line->empty() &&
               multi_poly->empty();
      if (result) {
        ObGeometry *multi_point_bin = NULL;
        if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(*allocator, multi_point, multi_point_bin, srs))) {
          LOG_WARN("failed to convert geo tree to binary", K(ret));
        } else {
          bool mp_within_mp = false;
          ret = eval_wkb_fn(multi_point_bin, g2, context, mp_within_mp);
          if (OB_FAIL(ret)) {
            LOG_WARN("failed to do within by functor between MultiPoint and MultiPoint", K(ret));
          } else {
            result &= mp_within_mp;
          }
        }
      }
    }
    return ret;
  }
};

} // namespace common
} // namespace oceanbase
