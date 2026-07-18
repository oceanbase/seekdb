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
#include "share/geo/ob_geo_func_utils.h"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

//===========BIN GEOM==========
/*Point*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomPoint, ObWkbGeomLineString, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomPoint, ObWkbGeomPolygon, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomPoint, ObWkbGeomMultiLineString, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomPoint, ObWkbGeomMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSEDx(g1, g2, context);
  result.bret = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

/*Linestring*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomLineString, ObWkbGeomLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomLineString, ObWkbGeomLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomLineString, ObWkbGeomPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomLineString, ObWkbGeomPolygon>(g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomLineString, ObWkbGeomMultiLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomLineString, ObWkbGeomMultiLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomLineString, ObWkbGeomMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomLineString, ObWkbGeomMultiPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

/*Polygon*/
// return null

/*MultiPoint*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiPoint, ObWkbGeomLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeomMultiPoint, ObWkbGeomLineString>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiPoint, ObWkbGeomPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeomMultiPoint, ObWkbGeomPolygon>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiLineString, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeomMultiPoint, ObWkbGeomMultiLineString>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon, ObGeoFuncResWithNull)
{
  return eval_crosses_mpt<ObWkbGeomMultiPoint, ObWkbGeomMultiPolygon>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result.bret);
}
OB_GEO_FUNC_END;

/*MultiLineString*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiLineString, ObWkbGeomLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiLineString, ObWkbGeomPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiLineString, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomMultiLineString>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon, ObGeoFuncResWithNull)
{
  UNUSED(context);
  return eval_crosses_without_strategy<ObWkbGeomMultiLineString, ObWkbGeomMultiPolygon>(
      g1, g2, result.bret);
}
OB_GEO_FUNC_END;

/*MultiPolygon*/
// return null

/*Collection*/
OB_GEO_CART_BINARY_FUNC_BEGIN(
    ObGeoFuncCrossesImpl, ObWkbGeomCollection, ObWkbGeomCollection, ObGeoFuncResWithNull)
{
  return eval_crosses_gc<ObCartesianGeometrycollection, ObIWkbGeomCollection>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncCrossesImpl, ObWkbGeomCollection, ObGeoFuncResWithNull)
{
  return eval_crosses_gc<ObCartesianGeometrycollection, ObIWkbGeomCollection>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncCrossesImpl, ObWkbGeomCollection, ObGeoFuncResWithNull)
{
  return eval_crosses_gc<ObCartesianGeometrycollection, ObIWkbGeomCollection>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_crosses_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeoFuncResWithNull &result)
{
  return ObGeoFuncCrossesImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

}  // namespace common
}  // namespace oceanbase
