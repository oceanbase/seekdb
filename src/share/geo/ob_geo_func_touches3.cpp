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

#include "ob_geo_func_touches_helper.ipp"
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//============================Tree Cart(only Multi)========================
// multipoint
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultipoint, ObCartesianMultipoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultipoint, ObCartesianMultipolygon, bool)
{
  return eval_touches_mpt<ObCartesianMultipoint, ObCartesianMultipolygon>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultipoint, ObCartesianMultilinestring, bool)
{
  return eval_touches_mpt<ObCartesianMultipoint, ObCartesianMultilinestring>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

// multilinestring
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultilinestring, ObCartesianMultipoint, bool)
{
  return eval_touches_mpt<ObCartesianMultipoint, ObCartesianMultilinestring>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultilinestring, ObCartesianMultipolygon, bool)
{
  UNUSED(context);
  return eval_touches_without_strategy<ObCartesianMultilinestring, ObCartesianMultipolygon>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultilinestring, ObCartesianMultilinestring, bool)
{
  UNUSED(context);
  return eval_touches_without_strategy<ObCartesianMultilinestring, ObCartesianMultilinestring>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

// multipolygon
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultipolygon, ObCartesianMultipoint, bool)
{
  return eval_touches_mpt<ObCartesianMultipoint, ObCartesianMultipolygon>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultipolygon, ObCartesianMultipolygon, bool)
{
  UNUSED(context);
  return eval_touches_without_strategy<ObCartesianMultipolygon, ObCartesianMultipolygon>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObCartesianMultipolygon, ObCartesianMultilinestring, bool)
{
  UNUSED(context);
  return eval_touches_without_strategy<ObCartesianMultipolygon, ObCartesianMultilinestring>(
      g1, g2, result);
}
OB_GEO_FUNC_END;

//============================Tree Geog(only Multi)========================
// multipoint
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObGeographMultipoint, ObGeographMultipoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultipoint, ObGeographMultipolygon, bool)
{
  return eval_touches_mpt<ObGeographMultipoint, ObGeographMultipolygon>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultipoint, ObGeographMultilinestring, bool)
{
  return eval_touches_mpt<ObGeographMultipoint, ObGeographMultilinestring>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

// multilinestring
OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultilinestring, ObGeographMultipoint, bool)
{
  return eval_touches_mpt<ObGeographMultipoint, ObGeographMultilinestring>(
      g2, g1, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultilinestring, ObGeographMultipolygon, bool)
{
  return eval_touches_with_nonpoint_strategy<ObGeographMultilinestring, ObGeographMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultilinestring, ObGeographMultilinestring, bool)
{
  return eval_touches_with_nonpoint_strategy<ObGeographMultilinestring, ObGeographMultilinestring>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// multipolygon
OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultipolygon, ObGeographMultipoint, bool)
{
  return eval_touches_mpt<ObGeographMultipoint, ObGeographMultipolygon>(
      g2, g1, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultipolygon, ObGeographMultipolygon, bool)
{
  return eval_touches_with_nonpoint_strategy<ObGeographMultipolygon, ObGeographMultipolygon>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObGeographMultipolygon, ObGeographMultilinestring, bool)
{
  return eval_touches_with_nonpoint_strategy<ObGeographMultipolygon, ObGeographMultilinestring>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_touches_eval_tree_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncTouchesImpl::eval_tree_binary_cart(g1, g2, context, result);
}

int ob_geo_func_touches_eval_tree_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncTouchesImpl::eval_tree_binary_geog(g1, g2, context, result);
}

static int ob_geo_func_touches_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncTouchesImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncTouchesImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_touches_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);
extern int ob_geo_func_touches_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

int ObGeoFuncTouches::eval(const ObGeoEvalCtx &gis_context, bool &result)
{
  return ObGeoFuncTouchesImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_touches_eval_wkb_cart,
      ob_geo_func_touches_eval_wkb_geog,
      ob_geo_func_touches_eval_tree);
}

}  // namespace common
}  // namespace oceanbase
