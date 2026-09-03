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

#include "share/geo/ob_geo_dispatcher.h"
#include "share/geo/ob_geo_func_union.h"
#include "share/geo/ob_geo_tree.h"
#include "share/geo/ob_geo_to_tree_visitor.h"
#include "lib/oblog/ob_log_module.h"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

namespace bg = boost::geometry;
template <typename GeometryType1, typename GeometryType2>
static void apply_bg_union_inner(const GeometryType1 *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context,
                                 ObGeographMultilinestring *res)
{
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObLlLaAaStrategy line_strategy(geog_sphere);
  boost::geometry::union_(*geo1, *geo2, *res, line_strategy);
}

template <typename GeometryType1, typename GeometryType2>
static void apply_bg_union_inner(const GeometryType1 *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context,
                                 ObGeographMultipolygon *res)
{
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObLlLaAaStrategy line_strategy(geog_sphere);
  boost::geometry::union_(*geo1, *geo2, *res, line_strategy);
}

template <typename GeometryType1, typename GeometryType2, typename GeometryRes>
static void apply_bg_union_inner(const GeometryType1 *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context,
                                 GeometryRes *res)
{
  UNUSED(context);
  boost::geometry::union_(*geo1, *geo2, *res);
}

template <typename GeometryType1, typename GeometryType2, typename GeometryRes>
static int apply_bg_union(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(res)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create go by type", K(ret));
  } else {
    const GeometryType1 *geo1 = NULL;
    const GeometryType2 *geo2 = NULL;
    if (g1->is_tree()) {
      geo1 = reinterpret_cast<const GeometryType1 *>(g1);
      geo2 = reinterpret_cast<const GeometryType2 *>(g2);
    } else {
      geo1 = reinterpret_cast<const GeometryType1 *>(g1->val());
      geo2 = reinterpret_cast<const GeometryType2 *>(g2->val());
    }
    apply_bg_union_inner(geo1, geo2, context, res);
    result = res;
  }
  return ret;
}

template <typename GeometryType2>
static bool apply_bg_disjoint(const ObWkbGeogPoint *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context)
{
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObPlPaStrategy point_strategy(geog_sphere);
  return boost::geometry::disjoint(*geo1, *geo2, point_strategy);
}

template <typename GeometryType2>
static bool apply_bg_disjoint(const ObWkbGeomPoint *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context)
{
  UNUSED(context);
  return boost::geometry::disjoint(*geo1, *geo2);
}


template <typename GeometryType2>
static void apply_bg_difference(const ObWkbGeogMultiPoint *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context,
                                ObGeographMultipoint *res)
{
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObPlPaStrategy point_strategy(geog_sphere);
  boost::geometry::difference(*geo1, *geo2, *res, point_strategy);
}

template <typename GeometryType1, typename GeometryType2>
static void apply_bg_difference(const GeometryType1 *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context,
                                ObGeographMultilinestring *res)
{
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObLlLaAaStrategy line_strategy(geog_sphere);
  boost::geometry::difference(*geo1, *geo2, *res, line_strategy);
}

template <typename GeometryType1, typename GeometryType2, typename GeometryRes>
static void apply_bg_difference(const GeometryType1 *geo1, const GeometryType2 *geo2, const ObGeoEvalCtx &context,
                                GeometryRes *res)
{
  UNUSED(context);
  boost::geometry::difference(*geo1, *geo2, *res);
}

template <typename IGeometryType1, typename IGeometryType2, typename GeometryRes>
static int apply_bg_union_collection(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(res)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create go by type", K(ret));
  } else {
    const typename IGeometryType1::value_type *geo1 = reinterpret_cast<const typename IGeometryType1::value_type *>(g1->val());
    const typename IGeometryType2::value_type *geo2 = reinterpret_cast<const typename IGeometryType2::value_type *>(g2->val());
    ObGeoToTreeVisitor geo2_visitor(context.get_allocator());
    IGeometryType2 *i_geo2 = const_cast<IGeometryType2 *>(reinterpret_cast<const IGeometryType2 *>(g2));
    if (OB_FAIL(i_geo2->do_visit(geo2_visitor))) {
      LOG_WARN("failed to do geo2 to_tree visit", K(ret));
    } else if (OB_FAIL(res->push_back(*geo2_visitor.get_geometry()))) {
      LOG_WARN("failed to push geo2 to collection", K(ret));
    } else if (apply_bg_disjoint(geo1, geo2, context)) {
      ObGeoToTreeVisitor geo1_visitor(context.get_allocator());
      IGeometryType1 *i_geo1 = const_cast<IGeometryType1 *>(reinterpret_cast<const IGeometryType1 *>(g1));
      if (OB_FAIL(i_geo1->do_visit(geo1_visitor))) {
        LOG_WARN("failed to do geo1 to_tree visit", K(ret));
      } else if (OB_FAIL(res->push_back(*geo1_visitor.get_geometry()))) {
        LOG_WARN("failed to push geo1 to collection", K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      result = res;
    }
  }
  return ret;
}

template <typename GeometryType1, typename GeometryType2,
          typename IGeometryType1, typename IGeometryType2,
          typename GeometryRes>
static int apply_bg_multi_union_collection(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(res)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create go by type", K(ret));
  } else {
    const GeometryType1 *geo1 = reinterpret_cast<const GeometryType1 *>(g1->val());
    const GeometryType2 *geo2 = reinterpret_cast<const GeometryType2 *>(g2->val());
    typename GeometryType2::iterator it = geo2->begin();
    for ( ; it != geo2->end() && OB_SUCC(ret); it++) {
      ObGeoToTreeVisitor geo2_visitor(context.get_allocator());
      IGeometryType2 i_geo2;
      i_geo2.set_data(ObString(sizeof(*it), reinterpret_cast<char *>(it.operator->())));
      if (OB_FAIL(i_geo2.do_visit(geo2_visitor))) {
        LOG_WARN("failed to do geo2 to_tree visit", K(ret));
      } else if (OB_FAIL(res->push_back(*geo2_visitor.get_geometry()))) {
        LOG_WARN("failed to push geo2 to collection", K(ret));
      }
    }

    if (OB_SUCC(ret) && apply_bg_disjoint(geo1, geo2, context)) {
      ObGeoToTreeVisitor geo1_visitor(context.get_allocator());
      IGeometryType1 *i_geo1 = const_cast<IGeometryType1 *>(reinterpret_cast<const IGeometryType1 *>(g1));
      if (OB_FAIL(i_geo1->do_visit(geo1_visitor))) {
        LOG_WARN("failed to do geo1 to_tree visit", K(ret));
      } else if (OB_FAIL(res->push_back(*geo1_visitor.get_geometry()))) {
        LOG_WARN("failed to push geo1 to collection", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      result = res;
    }
  }
  return ret;
}

template<typename GeometryType1, typename GeometryType2, typename GeometryDiffType,
         typename IGeometryType2, typename GeometryRes>
static int apply_bg_diff_union_collection(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = NULL;

  GeometryDiffType *diff_geo = OB_NEWx(GeometryDiffType, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(diff_geo)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create go by type", K(ret));
  } else {
    const GeometryType1 *geo1 = reinterpret_cast<const GeometryType1 *>(g1->val());
    const GeometryType2 *geo2 = reinterpret_cast<const GeometryType2 *>(g2->val());
    apply_bg_difference(geo1, geo2, context, diff_geo);
    res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
    if (OB_ISNULL(res)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to create go by type", K(ret));
    } else {
      ObGeoToTreeVisitor visitor(context.get_allocator());
      IGeometryType2 *i_geo2 = const_cast<IGeometryType2 *>(reinterpret_cast<const IGeometryType2 *>(g2));
      if (OB_FAIL(i_geo2->do_visit(visitor))) {
        LOG_WARN("failed to do geo visit", K(ret));
      } else if (OB_FAIL(res->push_back(*visitor.get_geometry()))) {
        LOG_WARN("failed to push geo2 to collection", K(ret));
      } else {
        FOREACH_X(item, *diff_geo, OB_SUCC(ret)) {
          if (OB_FAIL(res->push_back(*item))) {
            LOG_WARN("failed to add geo to collection", K(ret));
          }
        }
      }
    }
  }

  if (OB_SUCC(ret)) {
    result = res;
  }
  return ret;
}

static int push_back_innerpoint(const ObWkbGeomInnerPoint &innerpoint, const ObGeoEvalCtx &context,
  ObCartesianGeometrycollection &res)
{
  INIT_SUCC(ret);
  ObCartesianPoint *point = OB_NEWx(ObCartesianPoint, context.get_allocator(), res.get_srid());
  if (OB_ISNULL(point)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create cartesian point", K(ret));
  } else {
    point->x(innerpoint.get<0>());
    point->y(innerpoint.get<1>());
    if (OB_FAIL(res.push_back(*point))) {
      LOG_WARN("failed to add geo to collection", K(ret));
    }
  }
  return ret;
}

static int push_back_innerpoint(const ObWkbGeogInnerPoint &innerpoint, const ObGeoEvalCtx &context,
  ObGeographGeometrycollection &res)
{
  INIT_SUCC(ret);
  ObGeographPoint *point = OB_NEWx(ObGeographPoint, context.get_allocator(), res.get_srid());
  if (OB_ISNULL(point)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create geograph point", K(ret));
  } else {
    point->x(innerpoint.get<0>());
    point->y(innerpoint.get<1>());
    if (OB_FAIL(res.push_back(*point))) {
      LOG_WARN("failed to add geo to collection", K(ret));
    }
  }
  return ret;
}

template<typename GeometryType1, typename GeometryType2, typename GeometryDiffType,
         typename IGeometryType2, typename GeometryTreeType2, typename GeometryRes>
static int apply_bg_union_multiline_multipolygon(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = NULL;
  GeometryDiffType *diff_geo = OB_NEWx(GeometryDiffType, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(diff_geo)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create go by type", K(ret));
  } else {
    const GeometryType1 *geo1 = reinterpret_cast<const GeometryType1 *>(g1->val());
    const GeometryType2 *geo2 = reinterpret_cast<const GeometryType2 *>(g2->val());
    apply_bg_difference(geo1, geo2, context, diff_geo);
    res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
    if (OB_ISNULL(res)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to create go by type", K(ret));
    } else {
      ObGeoToTreeVisitor visitor(context.get_allocator());
      IGeometryType2 *i_geo2 = const_cast<IGeometryType2 *>(reinterpret_cast<const IGeometryType2 *>(g2));
      if (OB_FAIL(i_geo2->do_visit(visitor))) {
        LOG_WARN("failed to do geo visit", K(ret));
      } else {
        GeometryTreeType2 *geo2_tree = static_cast<GeometryTreeType2 *>(visitor.get_geometry());
        if (diff_geo->is_empty()) {
          if (OB_FAIL(res->push_back(*geo2_tree))) {
            LOG_WARN("failed to add geo to collection", K(ret));
          }
        } else {
          FOREACH_X(item, *geo2_tree, OB_SUCC(ret)) {
            if (OB_FAIL(res->push_back(*item))) {
              LOG_WARN("failed to add geo to collection", K(ret));
            }
          }
          FOREACH_X(diff_item, *diff_geo, OB_SUCC(ret)) {
            if (OB_FAIL(res->push_back(*diff_item))) {
              LOG_WARN("failed to add geo to collection", K(ret));
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    result = res;
  }
  return ret;
}

template<typename GeometryType1, typename GeometryType2, typename GeometryDiffType,
         typename IGeometryType2, typename GeometryTreeType2, typename GeometryRes>
static int apply_bg_union_multipoint_multigeo(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = NULL;
  GeometryDiffType *diff_geo = OB_NEWx(GeometryDiffType, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(diff_geo)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create go by type", K(ret));
  } else {
    const GeometryType1 *geo1 = reinterpret_cast<const GeometryType1 *>(g1->val());
    const GeometryType2 *geo2 = reinterpret_cast<const GeometryType2 *>(g2->val());
    apply_bg_difference(geo1, geo2, context, diff_geo);
    res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
    if (OB_ISNULL(res)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to create go by type", K(ret));
    } else {
      ObGeoToTreeVisitor visitor(context.get_allocator());
      IGeometryType2 *i_geo2 = const_cast<IGeometryType2 *>(reinterpret_cast<const IGeometryType2 *>(g2));
      if (OB_FAIL(i_geo2->do_visit(visitor))) {
        LOG_WARN("failed to do geo visit", K(ret));
      } else {
        GeometryTreeType2 *geo2_tree = static_cast<GeometryTreeType2 *>(visitor.get_geometry());
        if (diff_geo->is_empty()) {
          if (OB_FAIL(res->push_back(*geo2_tree))) {
            LOG_WARN("failed to add geo to collection", K(ret));
          }
        } else {
          FOREACH_X(item, *geo2_tree, OB_SUCC(ret)) {
            if (OB_FAIL(res->push_back(*item))) {
              LOG_WARN("failed to add geo to collection", K(ret));
            }
          }
          FOREACH_X(diff_item, *diff_geo, OB_SUCC(ret)) {
            if (OB_FAIL(push_back_innerpoint(*diff_item, context, *res))) {
              LOG_WARN("failed to add geo to collection", K(ret));
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    result = res;
  }
  return ret;
}

template<typename GeometryType1, typename GeometryType2, typename GeometryDiffType,
         typename IGeometryType2, typename GeometryTreeType2, typename GeometryRes>
static int apply_bg_union_multipoint_geo(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
{
  INIT_SUCC(ret);
  GeometryRes *res = NULL;
  GeometryDiffType *diff_geo = OB_NEWx(GeometryDiffType, context.get_allocator(), g1->get_srid(), *context.get_allocator());
  if (OB_ISNULL(diff_geo)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to create geo by type", K(ret));
  } else {
    const GeometryType1 *geo1 = reinterpret_cast<const GeometryType1 *>(g1->val());
    const GeometryType2 *geo2 = reinterpret_cast<const GeometryType2 *>(g2->val());
    apply_bg_difference(geo1, geo2, context, diff_geo);
    res = OB_NEWx(GeometryRes, context.get_allocator(), g1->get_srid(), *context.get_allocator());
    if (OB_ISNULL(res)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_WARN("fail to create go by type", K(ret));
    } else {
      ObGeoToTreeVisitor visitor(context.get_allocator());
      IGeometryType2 *i_geo2 = const_cast<IGeometryType2 *>(reinterpret_cast<const IGeometryType2 *>(g2));
      if (OB_FAIL(i_geo2->do_visit(visitor))) {
        LOG_WARN("failed to do geo visit", K(ret));
      } else if (OB_FAIL(res->push_back(*visitor.get_geometry()))) {
        LOG_WARN("failed to add geo to collection", K(ret));
      } else {
        FOREACH_X(diff_item, *diff_geo, OB_SUCC(ret)) {
          if (OB_FAIL(push_back_innerpoint(*diff_item, context, *res))) {
            LOG_WARN("failed to add geo to collection", K(ret));
          }
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    result = res;
  }
  return ret;
}

class ObGeoFuncUnionImpl : public ObIGeoDispatcher<ObGeometry *, ObGeoFuncUnionImpl>
{
public:
  ObGeoFuncUnionImpl();
  virtual ~ObGeoFuncUnionImpl() = default;
  // template for unary
  OB_GEO_UNARY_FUNC_DEFAULT(ObGeometry *, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_TREE_UNARY_FUNC_DEFAULT(ObGeometry *, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_CART_TREE_FUNC_DEFAULT(ObGeometry *, OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS);
  OB_GEO_GEOG_TREE_FUNC_DEFAULT(ObGeometry *, OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS);

  template <typename GeometyType1, typename GeometyType2>
  struct EvalWkbBi
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context,
        ObGeometry *&result)
    {
      UNUSEDx(g1, g2, context, result);
      return OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS;
    }
  };

  template <typename GeometyType1, typename GeometyType2>
  struct EvalWkbBiGeog
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context,
        ObGeometry *&result)
    {
      UNUSEDx(g1, g2, context, result);
      return OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS;
    }
  };

  template<typename GcTreeType, typename PtType>
  static int eval_unions_gc(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, ObGeometry *&result)
  {
    int ret = OB_SUCCESS;
    if (g1->type() != ObGeoType::GEOMETRYCOLLECTION && g2->type() != ObGeoType::GEOMETRYCOLLECTION) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_WARN("both g1 or g2 are not geometry collection", K(ret), K(g1->type()), K(g2->type()));
    } else {
      ObIAllocator *allocator = context.get_allocator();
      bool is_g1_empty = false;
      bool is_g2_empty = false;
      uint32_t srid = g1->get_srid();
      GcTreeType *res_coll = OB_NEWx(GcTreeType, allocator, srid, *allocator);
      ObGeometry *geo1 = const_cast<ObGeometry *>(g1);
      ObGeometry *geo2 = const_cast<ObGeometry *>(g2);
      if (OB_ISNULL(res_coll)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("failt alloc memory for geometry", K(ret));
      } else if (OB_FAIL(ObGeoTypeUtil::check_empty(geo1, is_g1_empty))) {
        LOG_WARN("fail to check is g1 empty", K(ret));
      } else if (OB_FAIL(ObGeoTypeUtil::check_empty(geo2, is_g2_empty))) {
        LOG_WARN("fail to check is g2 empty", K(ret));
      } else if (!is_g1_empty || !is_g2_empty) {
        typename GcTreeType::sub_mpt_type *mpt = NULL;
        typename GcTreeType::sub_ml_type *mls = NULL;
        typename GcTreeType::sub_mp_type *mpy = NULL;
        ObGeoToTreeVisitor tree_visitor1(allocator);
        ObGeoToTreeVisitor tree_visitor2(allocator);
        GcTreeType *geo_coll = OB_NEWx(GcTreeType, allocator, srid, *allocator);
        ObGeometry *g1_tree = nullptr;
        ObGeometry *g2_tree = nullptr;
        if (OB_ISNULL(geo_coll)) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
          LOG_WARN("failt alloc memory for geometry", K(ret));
        } else if (OB_FAIL(geo1->do_visit(tree_visitor1))) {
          LOG_WARN("fail to do visit", K(ret));
        } else if (FALSE_IT(g1_tree = tree_visitor1.get_geometry())) {
        } else if (OB_FAIL(geo_coll->push_back(*g1_tree))) {
          LOG_WARN("fail to push back geometry", K(ret));
        } else if (OB_FAIL(geo2->do_visit(tree_visitor2))) {
          LOG_WARN("fail to do visit", K(ret));
        } else if (FALSE_IT(g2_tree = tree_visitor2.get_geometry())) {
        } else if (OB_FAIL(geo_coll->push_back(*g2_tree))) {
          LOG_WARN("fail to push back geometry", K(ret));
        } else if (OB_FAIL(ObGeoFuncUtils::ob_geo_gc_split(*allocator, *geo_coll, mpt, mls, mpy))) {
          LOG_WARN("failed to do gc split", K(ret));
        } else if (OB_FAIL(ObGeoFuncUtils::ob_geo_gc_union(context.get_mem_ctx(), context.get_srs(), mpt, mls, mpy))) {
          LOG_WARN("failed to do gc union", K(ret));
        } else {
          for (int i = 0; OB_SUCC(ret) && i < mpy->size(); ++i) {
            if (OB_FAIL(
                    res_coll->push_back(reinterpret_cast<const ObGeometry &>((*mpy)[i])))) {
              LOG_WARN("fail to push back geometry", K(ret));
            }
          }
          for (int i = 0; OB_SUCC(ret) && i < mls->size(); ++i) {
            if (OB_FAIL(
                    res_coll->push_back(reinterpret_cast<const ObGeometry &>((*mls)[i])))) {
              LOG_WARN("fail to push back geometry", K(ret));
            }
          }
          for (int i = 0; OB_SUCC(ret) && i < mpt->size(); ++i) {
            typename GcTreeType::sub_mpt_type::value_type &pt = (*mpt)[i];
            PtType *pt_tree = OB_NEWx(PtType,
                allocator,
                pt.template get<0>(),
                pt.template get<1>(),
                srid);
            if (OB_ISNULL(pt_tree)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
              LOG_WARN("fail to allocate memory", K(ret));
            } else if (OB_FAIL(res_coll->push_back(
                            reinterpret_cast<const ObGeometry &>(*pt_tree)))) {
              LOG_WARN("fail to push back geometry", K(ret));
            }
          }
        }
      }
      if (OB_SUCC(ret)) {
        result = res_coll;
      }
    }
    return ret;
  }
};

}  // namespace common
}  // namespace oceanbase
