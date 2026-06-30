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

//===========BIN GEOM==========
// Geom Point/MultiPoint: cartesian cases return false (PG) / NULL (MySQL)
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomPoint, ObWkbGeomPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomPoint, ObWkbGeomMultiPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomMultiPoint, ObWkbGeomPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomMultiPoint, ObWkbGeomMultiPoint, bool)
{
  UNUSEDx(g1, g2, context);
  result = false;
  return OB_SUCCESS;
}
OB_GEO_FUNC_END;

// cartesian cases with eval_touches_mpt (MultiPoint)
OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomMultiPoint, bool)
{
  return eval_touches_mpt<ObWkbGeomMultiPoint, GeoType1>(
      g2, g1, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomMultiPoint, bool)
{
  return eval_touches_mpt<ObWkbGeomMultiPoint, GeoType2>(
      g1, g2, context, ObBGStrategyType::DEFAULT_NONE, result);
}
OB_GEO_FUNC_END;

int ob_geo_func_touches_eval_wkb_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result)
{
  return ObGeoFuncTouchesImpl::eval_wkb_binary_cart(g1, g2, context, result);
}

extern int ob_geo_func_touches_eval_tree_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, bool &result);

// Geom Collection
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomCollection, ObWkbGeomCollection, bool)
{
  return eval_touches_gc_gc<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_tree_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO2_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomCollection, bool)
{
  return eval_touches_gc_other_cart<ObCartesianGeometrycollection>(g2, g1, context, result,
      ob_geo_func_touches_eval_wkb_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_GEO1_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomCollection, bool)
{
  return eval_touches_gc_other_cart<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_cart);
}
OB_GEO_FUNC_END;

// handle ambiguous partial specializations
OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomCollection, ObWkbGeomMultiPoint, bool)
{
  return eval_touches_gc_other_cart<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_cart);
}
OB_GEO_FUNC_END;

OB_GEO_CART_BINARY_FUNC_BEGIN(ObGeoFuncTouchesImpl, ObWkbGeomMultiPoint, ObWkbGeomCollection, bool)
{
  return eval_touches_gc_other_cart<ObCartesianGeometrycollection>(g1, g2, context, result,
      ob_geo_func_touches_eval_wkb_cart);
}
OB_GEO_FUNC_END;

}  // namespace common
}  // namespace oceanbase
