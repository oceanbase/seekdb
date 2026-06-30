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
#include "share/geo/ob_geo_func_equals.h"
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
template<typename GeoType1, typename GeoType2>
int eval_equals_without_strategy(const ObGeometry *g1, const ObGeometry *g2, bool &result)
{
  INIT_SUCC(ret);
  const GeoType1 *geo1 = nullptr;
  const GeoType2 *geo2 = nullptr;
  if (g1->is_tree()) {
    geo1 = reinterpret_cast<const GeoType1 *>(g1);
    geo2 = reinterpret_cast<const GeoType2 *>(g2);
  } else {
    geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
    geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  }
  if (OB_ISNULL(geo1) || OB_ISNULL(geo2)) {
    ret = OB_ERR_INVALID_NULL_SDO_GEOMETRY;
    LOG_WARN("input geomery is null", K(ret), K(geo1), K(geo2), K(g1->is_tree()));
  } else {
    result = bg::equals(*geo1, *geo2);
  }
  return OB_SUCCESS;
}

template<typename GeoType1, typename GeoType2>
int eval_equals_with_nonpoint_strategy(
    const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
{
  INIT_SUCC(ret);
  const ObSrsItem *srs = context.get_srs();
  if (OB_ISNULL(srs)) {
    ret = OB_ERR_NULL_VALUE;
    LOG_WARN("srs is null", K(ret), K(g1->get_srid()), K(g1), K(g2));
  } else {
    const GeoType1 *geo1 = nullptr;
    const GeoType2 *geo2 = nullptr;
    if (g1->is_tree()) {
      geo1 = reinterpret_cast<const GeoType1 *>(g1);
      geo2 = reinterpret_cast<const GeoType2 *>(g2);
    } else {
      geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
      geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
    }
    if (OB_ISNULL(geo1) || OB_ISNULL(geo2)) {
      ret = OB_ERR_INVALID_NULL_SDO_GEOMETRY;
      LOG_WARN("input geomery is null", K(ret), K(geo1), K(geo2), K(g1->is_tree()));
    } else {
      bg::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
      bg::strategy::intersection::geographic_segments<> nonpoint_strategy(geog_sphere);
      result = bg::equals(*geo1, *geo2, nonpoint_strategy);
    }
  }
  return ret;
}

// ----- ObGeoFuncEqualsImpl -----
class ObGeoFuncEqualsImpl : public ObIGeoDispatcher<bool, ObGeoFuncEqualsImpl>
{
public:
  ObGeoFuncEqualsImpl();
  virtual ~ObGeoFuncEqualsImpl() = default;

  // template for unary
  OB_GEO_UNARY_FUNC_DEFAULT(bool, OB_ERR_GIS_INVALID_DATA);
  OB_GEO_TREE_UNARY_FUNC_DEFAULT(bool, OB_ERR_GIS_INVALID_DATA);

  // template for binary
  // default cases for cartesian
  template<typename GeoType1, typename GeoType2>
  struct EvalWkbBi
  {
    static int eval(
        const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context);
      result = false;
      return OB_SUCCESS;
    }
  };

  // default case for geography
  template<typename GeoType1, typename GeoType2>
  struct EvalWkbBiGeog
  {
    static int eval(
        const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context);
      result = false;
      return OB_SUCCESS;
    }
  };

  // template for tree
  template<typename GeoType1, typename GeoType2>
  struct EvalTreeBi
  {
    static int eval(
        const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context);
      result = false;
      return OB_SUCCESS;
    }
  };

  // default case for geography
  template<typename GeoType1, typename GeoType2>
  struct EvalTreeBiGeog
  {
    static int eval(
        const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context);
      result = false;
      return OB_SUCCESS;
    }
  };
private:
  using EqualsBinaryFn = int (*)(const common::ObGeometry *, const common::ObGeometry *,
                                  const ObGeoEvalCtx &, bool &);
  // geometry collection
  template<typename GcTreeType>
  static int eval_equals_geometry_collection(
      const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result,
      EqualsBinaryFn wkb_fn = nullptr, EqualsBinaryFn tree_fn = nullptr)
  {
    int ret = OB_SUCCESS;
    result = false;
    if (g1->type() == ObGeoType::GEOMETRYCOLLECTION) {
      bool is_g1_empty = false;
      bool is_g2_empty = false;
      if (OB_FAIL(ObGeoTypeUtil::check_empty(const_cast<ObGeometry *>(g1), is_g1_empty))) {
        LOG_WARN("fail to check is geometry empty", K(ret));
      } else if (OB_FAIL(ObGeoTypeUtil::check_empty(const_cast<ObGeometry *>(g2), is_g2_empty))) {
        LOG_WARN("fail to check is geometry empty", K(ret));
      } else if (is_g1_empty || is_g2_empty) {
        result = is_g1_empty && is_g2_empty;
      } else {
        typename GcTreeType::sub_mpt_type *mpt1 = NULL;
        typename GcTreeType::sub_ml_type *mls1 = NULL;
        typename GcTreeType::sub_mp_type *mpy1 = NULL;
        ObGeometry *geo1 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
        if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GcTreeType>(context, geo1, mpt1, mls1, mpy1))) {
          LOG_WARN("failed to prepare gc", K(ret));
        } else if (OB_ISNULL(mpt1) || OB_ISNULL(mls1) || OB_ISNULL(mpy1)) {
          ret = OB_ERR_GIS_INVALID_DATA;
          LOG_WARN("unexpected null geometry collection split", K(ret));
        } else if (g2->type() == ObGeoType::GEOMETRYCOLLECTION) {
          // both collection
          typename GcTreeType::sub_mpt_type *mpt2 = NULL;
          typename GcTreeType::sub_ml_type *mls2 = NULL;
          typename GcTreeType::sub_mp_type *mpy2 = NULL;
          ObGeometry *geo2 = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g2));
          if (OB_FAIL(ObGeoFuncUtils::ob_gc_prepare<GcTreeType>(context, geo2, mpt2, mls2, mpy2))) {
            LOG_WARN("failed to prepare gc", K(ret));
          } else if (OB_ISNULL(mpt2) || OB_ISNULL(mls2) || OB_ISNULL(mpt2)) {
            ret = OB_ERR_GIS_INVALID_DATA;
            LOG_WARN("unexpected null geometry collection split", K(ret));
          } else if ((mpt1->is_empty() != mpt2->is_empty()) || (mls1->is_empty() != mls2->is_empty())
                    || (mpy1->is_empty() != mpy2->is_empty())) {
            result = false;
          } else {
            bool mpt_result = mpt1->is_empty() && mpt2->is_empty();
            if (!mpt_result && OB_FAIL(tree_fn(mpt1, mpt2, context, mpt_result))) {
              LOG_WARN("fail to do eval", K(ret), K(result));
            } else {
              result = mpt_result;
            }
            if (OB_SUCC(ret) && result) {
              bool mls_result = mls1->is_empty() && mls2->is_empty();
              if (!mls_result && OB_FAIL(tree_fn(mls1, mls2, context, mls_result))) {
                LOG_WARN("fail to do eval", K(ret), K(result));
              } else {
                result = result && mls_result;
              }
            }
            if (OB_SUCC(ret) && result) {
              bool mpy_result = mpy1->is_empty() && mpy2->is_empty();
              if (!mpy_result && OB_FAIL(tree_fn(mpy1, mpy2, context, mpy_result))) {
                LOG_WARN("fail to do eval", K(ret), K(result));
              } else {
                result = result && mpy_result;
              }
            }
          }
        } else {
          switch (g2->type()) {
            case ObGeoType::POINT:
            case ObGeoType::MULTIPOINT: {
              if (!mls1->is_empty() || !mpy1->is_empty()) {
                result = false;
              } else {
                ObGeometry *mpt_bin = NULL;
                if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(
                        *context.get_allocator(), mpt1, mpt_bin, context.get_srs()))) {
                  LOG_WARN("failed to convert geo tree to binary", K(ret));
                } else {
                  ret = wkb_fn(mpt_bin, g2, context, result);
                }
              }
              break;
            }
            case ObGeoType::LINESTRING:
            case ObGeoType::MULTILINESTRING: {
              if (!mpt1->is_empty() || !mpy1->is_empty()) {
                result = false;
              } else {
                ObGeometry *mls_bin = NULL;
                if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(
                        *context.get_allocator(), mls1, mls_bin, context.get_srs()))) {
                  LOG_WARN("failed to convert geo tree to binary", K(ret));
                } else {
                  ret = wkb_fn(mls_bin, g2, context, result);
                }
              }
              break;
            }
            case ObGeoType::POLYGON:
            case ObGeoType::MULTIPOLYGON: {
              if (!mls1->is_empty() || !mpt1->is_empty()) {
                result = false;
              } else {
                ObGeometry *mpy_bin = NULL;
                if (OB_FAIL(ObGeoTypeUtil::tree_to_bin(
                        *context.get_allocator(), mpy1, mpy_bin, context.get_srs()))) {
                  LOG_WARN("failed to convert geo tree to binary", K(ret));
                } else {
                  ret = wkb_fn(mpy_bin, g2, context, result);
                }
              }
              break;
            }
            default: {
              ret = OB_ERR_GIS_INVALID_DATA;
              LOG_WARN("invalid geometry type", K(ret), K(g2->type()));
            }
          }
        }
      }
    } else if (g2->type() == ObGeoType::GEOMETRYCOLLECTION) {
      ret = eval_equals_geometry_collection<GcTreeType>(g2, g1, context, result, wkb_fn, tree_fn);
    } else {
      // none of the two geometries are collection type
      // not supposed to go to this branch
      ret = wkb_fn(g1, g2, context, result);
    }
    return ret;
  }
};

}  // namespace common
}  // namespace oceanbase
