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

#include "ob_geo_func_crosses_helper.ipp"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========BIN GEOG==========
/*Point*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogPoint, ObWkbGeogLineString, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogPoint, ObWkbGeogPolygon, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogPoint, ObWkbGeogMultiLineString, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogPoint, ObWkbGeogMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

/*Linestring*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogLineString, ObWkbGeogLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogLineString, ObWkbGeogPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogLineString, ObWkbGeogMultiLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogMultiLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogLineString, ObWkbGeogMultiPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogLineString, ObWkbGeogMultiPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

/*Polygon*/
// return null

/*MultiPoint*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiPoint, ObWkbGeogLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeogMultiPoint, ObWkbGeogLineString>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiPoint, ObWkbGeogPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeogMultiPoint, ObWkbGeogPolygon>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeogMultiPoint, ObWkbGeogMultiLineString>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result.bret);
}
OB_GEO_FUNC_END;

/*MultiLineString*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiLineString, ObWkbGeogLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiLineString, ObWkbGeogPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogMultiLineString>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_with_nonpoint_strategy<ObWkbGeogMultiLineString, ObWkbGeogMultiPolygon>(
      g1, g2, context, result.bret);
}
OB_GEO_FUNC_END;

/*MultiPolygon*/
// return null

/*Collection*/
OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeogCollection, ObWkbGeogCollection, ObGeoFuncResWithNull)
{
  return eval_crosses_gc<ObGeographGeometrycollection, ObIWkbGeogCollection>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncCrossesImpl, ObWkbGeogCollection, ObGeoFuncResWithNull)
{
  return eval_crosses_gc<ObGeographGeometrycollection, ObIWkbGeogCollection>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncCrossesImpl, ObWkbGeogCollection, ObGeoFuncResWithNull)
{
  return eval_crosses_gc<ObGeographGeometrycollection, ObIWkbGeogCollection>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

static int ob_geo_func_crosses_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
{
  return ObGeoFuncCrossesImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

static int ob_geo_func_crosses_eval_tree(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncCrossesImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncCrossesImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_crosses_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result);

int ObGeoFuncCrosses::eval(const ObGeoEvalCtx &gis_context, ObGeoFuncResWithNull &result)
{
  return ObGeoFuncCrossesImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_crosses_eval_wkb_cart,
      ob_geo_func_crosses_eval_wkb_geog,
      ob_geo_func_crosses_eval_tree);
}
}  // namespace common
}  // namespace oceanbase
