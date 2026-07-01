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

#include "lib/geo/ob_geo_dispatcher.h"
#include "lib/geo/ob_geo_func_crosses.h"
#include "lib/geo/ob_geo_tree.h"
#include "lib/geo/ob_geo_to_tree_visitor.h"
#include "lib/oblog/ob_log_module.h"
#include "lib/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{
namespace bg = boost::geometry;

template<typename GeometryType1, typename GeometryType2>
static void disjoint_with_point(const GeometryType1 *geo1, const GeometryType2 *geo2,
    ObBGStrategyType strategy, const ObGeoEvalCtx &context, bool &res)
{
  if (strategy == ObBGStrategyType::DEFAULT_NONE) {
    res = bg::disjoint(*geo1, *geo2);
  } else {
    const ObSrsItem *srs = context.get_srs();
    bg::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    bg::strategy::within::geographic_winding<ObWkbGeogPoint> point_strategy(geog_sphere);
    res = bg::disjoint(*geo1, *geo2, point_strategy);
  }
}

template<typename GeoType1, typename GeoType2>
static int eval_crosses_without_strategy(const ObGeometry *g1, const ObGeometry *g2, bool &result)
{
  int ret = OB_SUCCESS;
  const GeoType1 *geo1 = NULL;
  const GeoType2 *geo2 = NULL;
  if (g1->is_tree()) {
    geo1 = reinterpret_cast<const GeoType1 *>(g1);
    geo2 = reinterpret_cast<const GeoType2 *>(g2);
  } else {
    geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
    geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  }
  if (OB_ISNULL(geo1) || OB_ISNULL(geo2)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("multipoint or linestring pointer is null", K(ret), K(geo1), K(geo2));
  } else {
    result = bg::crosses(*geo1, *geo2);
  }
  return ret;
}

template<typename GeoType1, typename GeoType2>
static int eval_crosses_with_nonpoint_strategy(
    const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("srs is null", K(ret), K(g1->get_srid()), K(g1), K(g2));
  } else {
    bg::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
    bg::strategy::intersection::geographic_segments<> nonpoint_strategy(geog_sphere);
    const GeoType1 *geo1 = NULL;
    const GeoType2 *geo2 = NULL;
    if (g1->is_tree()) {
      geo1 = reinterpret_cast<const GeoType1 *>(g1);
      geo2 = reinterpret_cast<const GeoType2 *>(g2);
    } else {
      geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
      geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
    }
    if (OB_ISNULL(geo1) || OB_ISNULL(geo2)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("multipoint or linestring pointer is null", K(ret), K(geo1), K(geo2));
    } else {
      result = bg::crosses(*geo1, *geo2, nonpoint_strategy);
    }
  }
  return ret;
}

// ----- ObGeoFuncCrossesImpl -----
class ObGeoFuncCrossesImpl : public ObIGeoDispatcher<ObGeoFuncResWithNull, ObGeoFuncCrossesImpl>
{
public:
  ObGeoFuncCrossesImpl();
  virtual ~ObGeoFuncCrossesImpl() = default;

  // template for unary
  OB_GEO_UNARY_FUNC_DEFAULT(ObGeoFuncResWithNull, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_TREE_UNARY_FUNC_DEFAULT(ObGeoFuncResWithNull, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_CART_TREE_FUNC_DEFAULT(ObGeoFuncResWithNull, OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS);
  OB_GEO_GEOG_TREE_FUNC_DEFAULT(ObGeoFuncResWithNull, OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS);

  // template for binary
  // default cases for cartesian
  template<typename GeoType1, typename GeoType2>
  struct EvalWkbBi
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context,
        ObGeoFuncResWithNull &result)
    {
      // If g1 or g2 is dimension 0, return NULL (SQL/MM 2015, Sect. 5.1.51)
      UNUSEDx(g1, g2, context);
      result.is_null = true;
      return OB_SUCCESS;
    }
  };

  // default case for geography
  template<typename GeoType1, typename GeoType2>
  struct EvalWkbBiGeog
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context,
        ObGeoFuncResWithNull &result)
    {
      // If g1 or g2 is dimension 0, return NULL (SQL/MM 2015, Sect. 5.1.51)
      UNUSEDx(g1, g2, context);
      result.is_null = true;
      return OB_SUCCESS;
    }
  };

private:
  template<typename GeometryType1, typename GeometryType2>
  static void within_with_point(const GeometryType1 *geo1, const GeometryType2 *geo2,
      ObBGStrategyType strategy, const ObGeoEvalCtx &context, bool &res)
  {
    if (strategy == ObBGStrategyType::DEFAULT_NONE) {
      res = bg::within(*geo1, *geo2);
    } else {
      const ObSrsItem *srs = context.get_srs();
      bg::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
      bg::strategy::within::geographic_winding<ObWkbGeogPoint> point_strategy(geog_sphere);
      res = bg::within(*geo1, *geo2, point_strategy);
    }
  }

  template<typename MptType, typename GeoType>
  static int eval_crosses_mpt(const ObGeometry *mpt, const ObGeometry *geo, const ObGeoEvalCtx &context,
      ObBGStrategyType strategy, bool &result)
  {
    int ret = OB_SUCCESS;
    const MptType *mpt_bin = NULL;
    const GeoType *geo_bin = NULL;
    if (mpt->is_tree()) {
      mpt_bin = reinterpret_cast<const MptType *>(mpt);
      geo_bin = reinterpret_cast<const GeoType *>(geo);
    } else {
      mpt_bin = reinterpret_cast<const MptType *>(mpt->val());
      geo_bin = reinterpret_cast<const GeoType *>(geo->val());
    }
    result = false;
    if (OB_ISNULL(mpt_bin) || OB_ISNULL(geo_bin)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("multipoint or linestring pointer is null", K(ret), K(mpt_bin), K(geo_bin));
    } else {
      // g1 at least have one point within g2
      //  and at least have one point disjoint from g2
      typename MptType::const_iterator iter = mpt_bin->begin();
      bool is_disjoint = false;
      bool is_within = false;
      for (; iter != mpt_bin->end() && (!is_within || !is_disjoint); iter++) {
        typename MptType::value_type &point = *iter;
        bool pt_disjoint = false;
        if (!is_disjoint) {
          disjoint_with_point(&point, geo_bin, strategy, context, pt_disjoint);
          is_disjoint = pt_disjoint;
        }
        if (!pt_disjoint && !is_within) {
          within_with_point(&point, geo_bin, strategy, context, is_within);
        }
      }
      result = is_within && is_disjoint;
    }
    return ret;
  }

  static int eval_difference(
      const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&res)
  {
    int ret = OB_SUCCESS;
    ObGeometry *last_res = res;
    ObGeoEvalCtx diff_ctx(context.get_mem_ctx(), context.get_srs());
    if (OB_FAIL(diff_ctx.append_geo_arg(g1)) || OB_FAIL(diff_ctx.append_geo_arg(g2))) {
      LOG_WARN("failed to append geo to ctx", K(ret));
    } else if (OB_FAIL(ObGeoFunc<ObGeoFuncType::Difference>::geo_func::eval(diff_ctx, res))) {
    } else if (OB_NOT_NULL(last_res)) {
      context.get_allocator()->free(last_res);
      last_res = nullptr;
    }
    return ret;
  }

  template<typename GcTreeType>
  static int is_part_difference_gc_gc(typename GcTreeType::sub_mpt_type *mpt1,
      typename GcTreeType::sub_ml_type *mls1, typename GcTreeType::sub_mpt_type *mpt2,
      typename GcTreeType::sub_ml_type *mls2, typename GcTreeType::sub_mp_type *mpy2,
      const ObGeoEvalCtx &context, bool &is_part_difference)
  {
    int ret = OB_SUCCESS;
    ObGeometry *diff_result = nullptr;
    is_part_difference = false;
    if (OB_FAIL(eval_difference(mpt1, mpt2, context, diff_result))) {
    } else if (OB_FAIL(eval_difference(diff_result, mls2, context, diff_result))) {
    } else if (OB_FAIL(eval_difference(diff_result, mpy2, context, diff_result))) {
    } else if (diff_result->type() != ObGeoType::MULTIPOINT) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("result of func difference might be invalid", K(ret), K(diff_result->type()));
    } else {
      typename GcTreeType::sub_mpt_type *mpt_diff =
          static_cast<typename GcTreeType::sub_mpt_type *>(diff_result);
      if (!mpt_diff->is_empty()) {
        is_part_difference = true;
      } else {
        if (OB_FAIL(eval_difference(mls1, mls2, context, diff_result))) {
        } else if (OB_FAIL(eval_difference(diff_result, mpy2, context, diff_result))) {
        } else {
          typename GcTreeType::sub_ml_type *mls_diff =
              static_cast<typename GcTreeType::sub_ml_type *>(diff_result);
          is_part_difference = !mls_diff->is_empty();
        }
      }
    }
    return ret;
  }

  template<typename GcTreeType>
  static int has_common_interior_gc_gc(typename GcTreeType::sub_mpt_type *mpt1,
      typename GcTreeType::sub_ml_type *mls1, typename GcTreeType::sub_mp_type *mpy1,
      typename GcTreeType::sub_mpt_type *mpt2, typename GcTreeType::sub_ml_type *mls2,
      typename GcTreeType::sub_mp_type *mpy2, const ObGeoEvalCtx &context,
      bool &has_common_interior)
  {
    int ret = OB_SUCCESS;
    has_common_interior = false;
    bg::de9im::mask mask_0("0********");
    bg::de9im::mask mask_T("T********");
    bool is_both_dim_1 =
        mpy1->is_empty() && !mls1->is_empty() && mpy2->is_empty() && !mls2->is_empty();
    if (mpt1->crs() == ObGeoCRS::Cartesian || is_both_dim_1) {
      has_common_interior =
          is_both_dim_1 ? bg::relate(*mpt1, *mpt2, mask_0) : bg::relate(*mpt1, *mpt2, mask_T);
      typename GcTreeType::sub_mpt_type::iterator iter = mpt1->begin();
      for (; !has_common_interior && iter != mpt1->end(); iter++) {
        typename GcTreeType::sub_mpt_type::value_type &pt = *iter;
        if (is_both_dim_1) {
          has_common_interior = bg::relate(pt, *mls2, mask_0);
        } else {
          has_common_interior = bg::relate(pt, *mpt2, mask_T) || bg::relate(pt, *mpy2, mask_T);
        }
      }
      typename GcTreeType::sub_mpt_type::iterator iter2 = mpt2->begin();
      for (; !has_common_interior && iter2 != mpt2->end(); iter2++) {
        typename GcTreeType::sub_mpt_type::value_type &pt = *iter2;
        has_common_interior =
            is_both_dim_1 ? bg::relate(pt, *mls1, mask_0) : bg::relate(pt, *mpt1, mask_T);
      }
      if (is_both_dim_1) {
        bg::de9im::mask mask_1("1********");
        if (bg::relate(*mls1, *mls2, mask_0)) {
          has_common_interior = true;
        } else if (bg::relate(*mls1, *mls2, mask_1)) {
          // common interior should not be dimension 1
          has_common_interior = false;
        }
      } else {
        if (!has_common_interior) {
          has_common_interior =
              bg::relate(*mls1, *mls2, mask_T) || bg::relate(*mls1, *mpy2, mask_T);
        }
      }
    } else {
      // mpt1->crs() == ObGeoCRS::Geographic && !is_both_dim_1
      const ObSrsItem *srs = context.get_srs();
      if (OB_ISNULL(srs)) {
        ret = OB_ERR_NULL_VALUE;
        LOG_WARN("srs is null", K(ret));
      } else {
        bg::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
        bg::strategy::within::geographic_winding<ObWkbGeogPoint> point_strategy(geog_sphere);
        has_common_interior = bg::relate(*mpt1, *mpt2, mask_T);
        typename GcTreeType::sub_mpt_type::iterator iter = mpt1->begin();
        for (; !has_common_interior && iter != mpt1->end(); iter++) {
          typename GcTreeType::sub_mpt_type::value_type &pt = *iter;
          if (bg::relate(pt, *mls2, mask_T, point_strategy)
              || bg::relate(pt, *mpy2, mask_T, point_strategy)) {
            has_common_interior = true;
          }
        }
        typename GcTreeType::sub_mpt_type::iterator iter2 = mpt2->begin();
        for (; !has_common_interior && iter2 != mpt2->end(); iter2++) {
          typename GcTreeType::sub_mpt_type::value_type &pt = *iter2;
          if (bg::relate(pt, *mls1, mask_T, point_strategy)) {
            has_common_interior = true;
          }
        }
        bg::strategy::intersection::geographic_segments<> nonpoint_strategy(geog_sphere);
        if (!has_common_interior) {
          has_common_interior = bg::relate(*mls1, *mls2, mask_T, nonpoint_strategy)
                                || bg::relate(*mls1, *mpy2, mask_T, nonpoint_strategy);
        }
      }
    }
    return ret;
  }

  // assume that g1 g2 both collection
  template<typename GcTreeType>
  static int eval_crosses_gc_gc(const ObGeometry *g1, const ObGeometry *g2,
      const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
  {
    int ret = OB_SUCCESS;
    const ObSrsItem *srs = context.get_srs();
    if (g1->type() != ObGeoType::GEOMETRYCOLLECTION
        || g2->type() != ObGeoType::GEOMETRYCOLLECTION) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_WARN("input geometry should be GEOMETRYCOLLECTION", K(ret), K(g1->type()), K(g2->type()));
    } else if (g1->crs() == ObGeoCRS::Geographic && OB_ISNULL(srs)) {
      ret = OB_ERR_NULL_VALUE;
      LOG_WARN("srs is null", K(ret));
    } else {
      result.bret = false;
      typename GcTreeType::sub_mpt_type *mpt1 = NULL;
      typename GcTreeType::sub_ml_type *mls1 = NULL;
      typename GcTreeType::sub_mp_type *mpy1 = NULL;
      ObGeometry *geo1 = const_cast<ObGeometry *>(g1);
      ObIAllocator *allocator = context.get_allocator();
      ObGeoToTreeVisitor tree_visitor(allocator);
      if (OB_FAIL(geo1->do_visit(tree_visitor))) {
      } else if (OB_FAIL(ObGeoFuncUtils::ob_geo_gc_split(*allocator,
                  *static_cast<const GcTreeType *>(tree_visitor.get_geometry()),
                  mpt1, mls1, mpy1))) {
      } else if (OB_ISNULL(mpt1) || OB_ISNULL(mls1) || OB_ISNULL(mpy1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null geometry collection split", K(ret));
      } else if (!mpy1->empty()) {
        result.is_null = true;
      } else if (OB_FAIL(ObGeoFuncUtils::ob_geo_gc_union(context.get_mem_ctx(), *srs, mpt1, mls1, mpy1))) {
      } else if (OB_ISNULL(mpt1) || OB_ISNULL(mls1) || OB_ISNULL(mpy1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null geometry collection union", K(ret));
      } else {
        typename GcTreeType::sub_mpt_type *mpt2 = NULL;
        typename GcTreeType::sub_ml_type *mls2 = NULL;
        typename GcTreeType::sub_mp_type *mpy2 = NULL;
        ObGeometry *geo2 = const_cast<ObGeometry *>(g2);
        bool has_common_interior = false;  // Check that if g1 and g2 has common interior
        ObGeoToTreeVisitor tree_visitor2(allocator);
        if (OB_FAIL(geo2->do_visit(tree_visitor2))) {
        } else if (OB_FAIL(ObGeoFuncUtils::ob_geo_gc_split(*allocator,
                    *static_cast<const GcTreeType *>(tree_visitor2.get_geometry()),
                    mpt2, mls2, mpy2))) {
        } else if (!mpt2->is_empty() && mls2->is_empty() && mpy2->is_empty()) {
          // MySQL return NULL, PG return false
          result.is_null = true;
        } else if (OB_ISNULL(mpt2) || OB_ISNULL(mls2) || OB_ISNULL(mpy2)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null geometry collection split", K(ret));
        } else if (OB_FAIL(ObGeoFuncUtils::ob_geo_gc_union(context.get_mem_ctx(), *srs, mpt2, mls2, mpy2))) {
        } else if (OB_ISNULL(mpt2) || OB_ISNULL(mls2) || OB_ISNULL(mpy2)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null geometry collection union", K(ret));
        } else if (OB_FAIL((has_common_interior_gc_gc<GcTreeType>(
                       mpt1, mls1, mpy1, mpt2, mls2, mpy2, context, has_common_interior)))) {
        } else if (!has_common_interior) {
          result.bret = false;
        } else {
          // Check that if g1 has at least one point in g2's exterior
          if (OB_FAIL((is_part_difference_gc_gc<GcTreeType>(
                  mpt1, mls1, mpt2, mls2, mpy2, context, result.bret)))) {
          }
        }
      }
    }
    return ret;
  }

  template<typename GcBinType>
  static int make_collection(const ObGeometry *g, ObIAllocator *allocator, const ObGeometry *&geo)
  {
    int ret = OB_SUCCESS;
    GcBinType *gc_bin = OB_NEWx(GcBinType, allocator, g->get_srid());
    ObWkbBuffer buffer(*allocator);
    if (OB_ISNULL(gc_bin)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected null geometry", K(ret));
    } else if (OB_FAIL(buffer.append(static_cast<char>(ObGeoWkbByteOrder::LittleEndian)))) {
    } else if (OB_FAIL(buffer.append(static_cast<uint32_t>(ObGeoType::GEOMETRYCOLLECTION)))) {
    } else if (OB_FAIL(buffer.append(static_cast<uint32_t>(1)))) {
    } else if (OB_FAIL(buffer.append(g->val(), g->length()))) {
    } else {
      gc_bin->set_data(buffer.string());
      geo = gc_bin;
    }
    return ret;
  }

  template<typename GcTreeType, typename GcBinType>
  static int eval_crosses_gc(const ObGeometry *g1, const ObGeometry *g2,
      const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
  {
    int ret = OB_SUCCESS;
    const ObGeometry *gc1 = nullptr;
    const ObGeometry *gc2 = nullptr;
    if (g1->type() == ObGeoType::GEOMETRYCOLLECTION
        && g2->type() == ObGeoType::GEOMETRYCOLLECTION) {
      gc1 = g1;
      gc2 = g2;
    } else if (g1->type() == ObGeoType::GEOMETRYCOLLECTION) {
      gc1 = g1;
      if (OB_FAIL((make_collection<GcBinType>(g2, context.get_allocator(), gc2)))) {
      }
    } else if (g2->type() == ObGeoType::GEOMETRYCOLLECTION) {
      gc2 = g2;
      if (OB_FAIL((make_collection<GcBinType>(g1, context.get_allocator(), gc1)))) {
      }
    } else {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_WARN("At least one of g1 and g2 is collection", K(ret), K(g1->type()), K(g2->type()));
    }
    if (OB_SUCC(ret) && OB_FAIL((eval_crosses_gc_gc<GcTreeType>(gc1, gc2, context, result)))) {
      LOG_WARN("fail to do eval_crosses_gc_gc", K(ret));
    }
    return ret;
  }
};

}  // namespace common
}  // namespace oceanbase
