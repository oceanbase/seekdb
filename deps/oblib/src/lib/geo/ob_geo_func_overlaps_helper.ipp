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
#include "lib/geo/ob_geo_func_overlaps.h"
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
template<typename GeoType1, typename GeoType2>
int eval_overlaps_without_strategy(const ObGeometry *g1, const ObGeometry *g2, bool &result)
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
    result = bg::overlaps(*geo1, *geo2);
  }
  return ret;
}

template<typename GeoType1, typename GeoType2>
int eval_overlaps_with_nonpoint_strategy(
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
      result = bg::overlaps(*geo1, *geo2, nonpoint_strategy);
    }
  }
  return ret;
}

// ----- ObGeoFuncOverlapsImpl -----
class ObGeoFuncOverlapsImpl : public ObIGeoDispatcher<ObGeoFuncResWithNull, ObGeoFuncOverlapsImpl>
{
public:
  ObGeoFuncOverlapsImpl();
  virtual ~ObGeoFuncOverlapsImpl() = default;

  // function pointer type for eval_tree_binary dispatch
  using OverlapsTreeBinaryFn = int (*)(const common::ObGeometry *, const common::ObGeometry *,
                                        const ObGeoEvalCtx &, ObGeoFuncResWithNull &);

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
      // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
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
      // If dim(g1) != dim(g2), return NULL (SQL/MM 2015, Part 3, Sect. 5.1.54).
      UNUSEDx(g1, g2, context);
      result.is_null = true;
      return OB_SUCCESS;
    }
  };

private:
  // assume that g1 g2 both collection
  template<typename GcTreeType>
  static int eval_overlaps_gc_gc(const ObGeometry *g1, const ObGeometry *g2,
      const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result,
      OverlapsTreeBinaryFn eval_tree_fn)
  {
    int ret = OB_SUCCESS;
    if (g1->type() != ObGeoType::GEOMETRYCOLLECTION
        || g2->type() != ObGeoType::GEOMETRYCOLLECTION) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_WARN("input geometry should be GEOMETRYCOLLECTION", K(ret), K(g1->type()), K(g2->type()));
    } else {
      result.bret = false;
      typename GcTreeType::sub_mpt_type *mpt1 = NULL;
      typename GcTreeType::sub_ml_type *mls1 = NULL;
      typename GcTreeType::sub_mp_type *mpy1 = NULL;
      ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
      uint8_t dim1 = -1;
      uint8_t dim2 = -1;
      if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GcTreeType>(context, geo1, mpt1, mls1, mpy1))) {
      } else if (OB_ISNULL(mpt1) || OB_ISNULL(mls1) || OB_ISNULL(mpy1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null geometry collection split", K(ret));
      } else {
        if (!mpy1->empty()) {
          dim1 = 2;
        } else if (!mls1->empty()) {
          dim1 = 1;
        } else if (!mpt1->empty()) {
          dim1 = 0;
        } else {
          result.is_null = true;
        }
      }

      typename GcTreeType::sub_mpt_type *mpt2 = NULL;
      typename GcTreeType::sub_ml_type *mls2 = NULL;
      typename GcTreeType::sub_mp_type *mpy2 = NULL;
      ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
      if (OB_SUCC(ret) && !result.is_null) {
        // bool has_common_interior = false;  // Check that if g1 and g2 has common interior
        if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GcTreeType>(context, geo2, mpt2, mls2, mpy2))) {
        } else if (OB_ISNULL(mpt2) || OB_ISNULL(mls2) || OB_ISNULL(mpy2)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected null geometry collection split", K(ret));
        } else {
          if (!mpy2->empty()) {
            dim2 = 2;
          } else if (!mls2->empty()) {
            dim2 = 1;
          } else if (!mpt2->empty()) {
            dim2 = 0;
          } else {
            result.is_null = true;
          }
        }
      }

      if (OB_FAIL(ret) || result.is_null) {
        // do nothing
      } else if (dim1 == -1 || dim1 != dim2) {
        result.is_null = true;
      } else {
        ObGeoFuncResWithNull mpt_res;
        ObGeoFuncResWithNull mls_res;
        ObGeoFuncResWithNull mpy_res;
        switch (dim1) {
          case 2:
            if (OB_FAIL(eval_tree_fn(mpy1, mpy2, context, mpy_res))) {
            }
          case 1:
            if (OB_FAIL(eval_tree_fn(mls1, mls2, context, mls_res))) {
            }
          case 0:
            if (OB_FAIL(eval_tree_fn(mpt1, mpt2, context, mpt_res))) {
            }
            break;
          default: {
            // should not go here
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected dim provided to overlaps", K(ret), K(dim1));
          }
        }
        result.bret |= mpy_res.bret || mpt_res.bret || mls_res.bret;
      }
    }
    return ret;
  }

  template<typename GcTreeType>
  static int eval_overlaps_gc_other(const ObGeometry *g1, const ObGeometry *g2,
      const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result,
      OverlapsTreeBinaryFn eval_tree_fn)
  {
    int ret = OB_SUCCESS;
    if (g1->type() != ObGeoType::GEOMETRYCOLLECTION
        && g2->type() != ObGeoType::GEOMETRYCOLLECTION) {
      ret = OB_ERR_GIS_INVALID_DATA;
      LOG_WARN("At least one of g1 and g2 is collection", K(ret), K(g1->type()), K(g2->type()));
    } else if (g2->type() == ObGeoType::GEOMETRYCOLLECTION
               && g1->type() == ObGeoType::GEOMETRYCOLLECTION) {
      if (OB_FAIL(eval_overlaps_gc_gc<GcTreeType>(g1, g2, context, result, eval_tree_fn))) {
      }
    } else if (g2->type() == ObGeoType::GEOMETRYCOLLECTION) {
      ret = eval_overlaps_gc_other<GcTreeType>(g2, g1, context, result, eval_tree_fn);
    } else {
      // now assert g1 is colletion and g2 is not collection.
      result.bret = false;
      typename GcTreeType::sub_mpt_type *mpt1 = NULL;
      typename GcTreeType::sub_ml_type *mls1 = NULL;
      typename GcTreeType::sub_mp_type *mpy1 = NULL;
      ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
      uint8_t dim1 = -1;
      uint8_t dim2 = -1;
      if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GcTreeType>(context, geo1, mpt1, mls1, mpy1))) {
      } else if (OB_ISNULL(mpt1) || OB_ISNULL(mls1) || OB_ISNULL(mpy1)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected null geometry collection split", K(ret));
      } else {
        if (!mpy1->empty()) {
          dim1 = 2;
        } else if (!mls1->empty()) {
          dim1 = 1;
        } else if (!mpt1->empty()) {
          dim1 = 0;
        } else {
          result.is_null = true;
        }
      }

      ObGeoToTreeVisitor to_tree(context.get_allocator());
      ObGeometry *geo2 = const_cast<ObGeometry *>(g2);
      if (OB_FAIL(ret) || result.is_null) {
        // do nothing
      } else if (OB_FAIL(geo2->do_visit(to_tree))) {
      } else {
        ObGeometry *g2_tree = to_tree.get_geometry();
        switch (g2_tree->type()) {
          case ObGeoType::POINT:
          case ObGeoType::MULTIPOINT: {
            if (dim1 != 0) {
              result.is_null = true;
            } else if (OB_FAIL(eval_tree_fn(mpt1, g2_tree, context, result))) {
            }
            break;
          }
          case ObGeoType::LINESTRING:
          case ObGeoType::MULTILINESTRING: {
            if (dim1 != 1) {
              result.is_null = true;
            } else if (OB_FAIL(eval_tree_fn(mls1, g2_tree, context, result))) {
            }
            break;
          }
          case ObGeoType::POLYGON:
          case ObGeoType::MULTIPOLYGON: {
            if (dim1 != 2) {
              result.is_null = true;
            } else if (OB_FAIL(eval_tree_fn(mpy1, g2_tree, context, result))) {
            }
            break;
          }
          default: {
            // should not go here
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("unexpected dim provided to overlaps", K(ret), K(dim1));
            break;
          }
        }
      }
    }
    return ret;
  }
};

}  // namespace common
}  // namespace oceanbase
