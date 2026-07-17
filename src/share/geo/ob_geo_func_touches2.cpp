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

//===========BIN GEOG==========

int ob_geo_func_touches_eval_wkb_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncTouchesImpl::eval_wkb_binary_geog(g1, g2, context, result);
}

extern int ob_geo_func_touches_eval_tree_geog(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

// handle ambiguous partial specializations
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogCollection, ObWkbGeogPoint, bool)
{
  return eval_touches_gc_other_geog<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogPoint, ObWkbGeogCollection, bool)
{
  return eval_touches_gc_other_geog<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_geog);
}
OB_GEO_FUNC_END;

// geometrycollection for geography
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogCollection, ObWkbGeogCollection, bool)
{
  return eval_touches_gc_gc<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_tree_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogCollection, bool)
{
  return eval_touches_gc_other_geog<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_geog);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogCollection, bool)
{
  return eval_touches_gc_other_geog<ObGeographGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_geog);
}
OB_GEO_FUNC_END;

// cases use eval_touches_mpt (multi point)
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogMultiPoint, ObWkbGeogPolygon, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogPolygon>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogPolygon, ObWkbGeogMultiPoint, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogPolygon>(
      g2, g1, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObWkbGeogMultiPolygon, ObWkbGeogMultiPoint, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogMultiPolygon>(
      g2, g1, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogMultiPoint, ObWkbGeogLineString, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogLineString>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogLineString, ObWkbGeogMultiPoint, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogLineString>(
      g2, g1, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiLineString, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogMultiLineString>(
      g1, g2, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(
    ObGeoFuncTouchesImpl, ObWkbGeogMultiLineString, ObWkbGeogMultiPoint, bool)
{
  return eval_touches_mpt<ObWkbGeogMultiPoint, ObWkbGeogMultiLineString>(
      g2, g1, context, ObBGStrategyType::PL_PA_STRATEGY, result);
}
OB_GEO_FUNC_END;

// geograpyic cases return false (PG) / NULL (MySQL)
OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogPoint, ObWkbGeogPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogMultiPoint, ObWkbGeogMultiPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogPoint, ObWkbGeogMultiPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogMultiPoint, ObWkbGeogPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

// geograpyic cases using point strategy (point and nonpoint types)
OB_GEO_GEOG_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogPoint, bool)
{
  return eval_touches_with_point_strategy<GeoType1, ObWkbGeogPoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeogPoint, bool)
{
  return eval_touches_with_point_strategy<ObWkbGeogPoint, GeoType2>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
