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

#include "ob_geo_func_overlaps_helper.ipp"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========TREE CART (not completely)==========
// multipoint
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObCartesianMultipoint, ObCartesianMultipoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObCartesianMultipoint, ObCartesianPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

// multilinestring
OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncOverlapsImpl, ObCartesianMultilinestring,
    ObCartesianMultilinestring, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObCartesianMultilinestring, ObCartesianMultilinestring>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObCartesianMultilinestring, ObCartesianLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObCartesianMultilinestring, ObCartesianLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

// multipolygon
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObCartesianMultipolygon, ObCartesianMultipolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObCartesianMultipolygon, ObCartesianMultipolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObCartesianMultipolygon, ObCartesianPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_without_strategy<ObCartesianMultipolygon, ObCartesianPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

//===========TREE GEOG (not completely)==========
// multipoint
OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObGeographMultipoint, ObGeographMultipoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObGeographMultipoint, ObGeographPoint, ObGeoFuncResWithNull)
{
  // point is completely contained by another geometry, so they are not overlaps
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

// multilinestring
OB_GEO_GEOG_TREE_FUNC_BEGIN(ObGeoFuncOverlapsImpl, ObGeographMultilinestring,
    ObGeographMultilinestring, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObGeographMultilinestring, ObGeographMultilinestring>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObGeographMultilinestring, ObGeographLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObGeographMultilinestring, ObGeographLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

// multipolygon
OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObGeographMultipolygon, ObGeographMultipolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObGeographMultipolygon, ObGeographMultipolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncOverlapsImpl, ObGeographMultipolygon, ObGeographPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_overlaps_with_nonpoint_strategy<ObGeographMultipolygon, ObGeographPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

int ob_geo_func_overlaps_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncOverlapsImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncOverlapsImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_overlaps_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result);
extern int ob_geo_func_overlaps_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result);

int ObGeoFuncOverlaps::eval(const ObGeoEvalCtx &gis_context, ObGeoFuncResWithNull &result)
{
  return ObGeoFuncOverlapsImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_overlaps_eval_wkb_cart,
      ob_geo_func_overlaps_eval_wkb_geog,
      ob_geo_func_overlaps_eval_tree);
}
}  // namespace common
}  // namespace oceanbase
