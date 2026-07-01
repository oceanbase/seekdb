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

#include "ob_geo_func_covered_by.h"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

template<typename GeoType1, typename GeoType2>
static bool ob_apply_bg_covered_by(const ObGeometry *g1, const ObGeometry *g2)
{
  const GeoType1 *geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
  const GeoType2 *geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  return boost::geometry::covered_by(*geo1, *geo2);
}

template<typename GeoType1, typename GeoType2>
static bool ob_apply_bg_covered_by_with_pl_strategy(const ObGeometry *g1, const ObGeometry *g2,
                                                    const ObGeoEvalCtx &context)
{
  const GeoType1 *geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
  const GeoType2 *geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObPlPaStrategy point_strategy(geog_sphere);
#ifdef USE_SPHERE_GEO
  return boost::geometry::covered_by(*geo1, *geo2, point_strategy);
#else
  return boost::geometry::covered_by(*geo1, *geo2);
#endif
}

template<typename GeoType1, typename GeoType2>
static bool ob_apply_bg_covered_by_with_ll_strategy(const ObGeometry *g1, const ObGeometry *g2,
                                                    const ObGeoEvalCtx &context)
{
  const GeoType1 *geo1 = reinterpret_cast<const GeoType1 *>(g1->val());
  const GeoType2 *geo2 = reinterpret_cast<const GeoType2 *>(g2->val());
  const ObSrsItem *srs = context.get_srs();
  boost::geometry::srs::spheroid<double> geog_sphere(srs->semi_major_axis(), srs->semi_minor_axis());
  ObLlLaAaStrategy line_strategy(geog_sphere);
#ifdef USE_SPHERE_GEO
  return boost::geometry::covered_by(*geo1, *geo2, line_strategy);
#else
  return boost::geometry::covered_by(*geo1, *geo2);
#endif
}

// ----- ObGeoFuncCoveredByImpl -----
class ObGeoFuncCoveredByImpl : public ObIGeoDispatcher<bool, ObGeoFuncCoveredByImpl>
{
public:
  ObGeoFuncCoveredByImpl();
  virtual ~ObGeoFuncCoveredByImpl() = default;

  // defaults
  OB_GEO_UNARY_FUNC_DEFAULT(bool, OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS);
  OB_GEO_TREE_UNARY_FUNC_DEFAULT(bool, OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS);
  OB_GEO_CART_TREE_FUNC_DEFAULT(bool, OB_ERR_NOT_IMPLEMENTED_FOR_CARTESIAN_SRS);
  OB_GEO_GEOG_TREE_FUNC_DEFAULT(bool, OB_ERR_NOT_IMPLEMENTED_FOR_GEOGRAPHIC_SRS);

  template<typename PointType, typename IPointType, typename GCType>
  static int ob_caculate_mp_gc_cover_by(const ObGeometry *g1, const ObGeometry *g2,
                                        const ObGeoEvalCtx &context, bool &result)
  {
    INIT_SUCC(ret);
    const typename GCType::sub_mpt_type *geo1 = reinterpret_cast<const typename GCType::sub_mpt_type *>(g1->val());
    // travel every point in multipoint; check if any one is not covered by geo2(postgis)
    FOREACH_X(item, *geo1, (OB_SUCC(ret))) {
      PointType point;
      point.byteorder(ObGeoWkbByteOrder::LittleEndian);
      point.template set<0>(item->template get<0>());
      point.template set<1>(item->template get<1>());
      ObString data(sizeof(PointType), reinterpret_cast<char *>(&point));
      IPointType i_point;
      i_point.set_data(data);
      if (g2->crs() == ObGeoCRS::Cartesian) {
        ret = EvalWkbBi<PointType, GCType>::eval(&i_point, g2, context, result);
      } else {
        ret = EvalWkbBiGeog<PointType, GCType>::eval(&i_point, g2, context, result);
      }
      if (OB_FAIL(ret)) {
        LOG_WARN("failed to do point_gc covered by functor", K(ret));
      } else if (!result) {
        break;
      }
    }
    return ret;
  }

  template <typename GeoType1, typename GeoType2>
  struct EvalWkbBi
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context);
      result = false;
      return OB_SUCCESS;
    }
  };

  template <typename GeoType1, typename GeoType2>
  struct EvalWkbBiGeog
  {
    static int eval(const ObGeometry *g1, const ObGeometry *g2, const ObGeoEvalCtx &context, bool &result)
    {
      UNUSEDx(g1, g2, context);
      result = false;
      return OB_SUCCESS;
    }
  };

};

}  // namespace common
}  // namespace oceanbase
