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

#include "ob_geo_func_difference_helper.ipp"

using namespace oceanbase::common;
namespace oceanbase
{
namespace common
{

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianMultipoint, ObCartesianMultipoint, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultipoint, ObCartesianMultipoint, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// tree cartesian polygon
OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianMultipoint, ObCartesianMultilinestring, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultipoint,
      ObCartesianMultilinestring,
      ObCartesianMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianPolygon, ObCartesianPolygon, ObGeometry *)
{
  return apply_bg_difference<ObCartesianPolygon, ObCartesianPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianPolygon, ObCartesianMultipolygon, ObGeometry *)
{
  return apply_bg_difference<ObCartesianPolygon, ObCartesianMultipolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianMultipolygon, ObCartesianPolygon, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultipolygon, ObCartesianPolygon, ObCartesianMultipolygon>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianMultipoint, ObCartesianMultipolygon, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultipoint, ObCartesianMultipolygon, ObCartesianMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianLineString, ObCartesianMultilinestring, ObGeometry *)
{
  return apply_bg_difference<ObCartesianLineString,
      ObCartesianMultilinestring,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianMultilinestring, ObCartesianMultilinestring, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultilinestring,
      ObCartesianMultilinestring,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianMultilinestring, ObCartesianMultipolygon, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultilinestring,
      ObCartesianMultipolygon,
      ObCartesianMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianPoint, ObCartesianMultilinestring, ObGeometry *)
{
  return apply_bg_difference<ObCartesianPoint, ObCartesianMultilinestring, ObCartesianMultipoint>(g1, g2, context, result);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianPolygon, ObCartesianMultilinestring, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just return g1
  result = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianMultipolygon, ObCartesianMultilinestring, ObGeometry *)
{
  // if g1.dimension > g2.dimension, g1 - g2 is equal g1(mysql/postgis)
  // so just return g1
  result = const_cast<ObGeometry *>(reinterpret_cast<const ObGeometry *>(g1));
  return OB_SUCCESS;
} OB_GEO_FUNC_END;

static int ob_geo_func_difference_eval_tree_cart(const common::ObGeometry *g1, const common::ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  return ObGeoFuncDifferenceImpl::eval_tree_binary_cart(g1, g2, context, result);
}

OB_GEO_CART_TREE_FUNC_BEGIN(ObGeoFuncDifferenceImpl, ObCartesianGeometrycollection, ObCartesianMultilinestring, ObGeometry *)
{
  return apply_bg_difference_collection<ObCartesianGeometrycollection, ObCartesianMultipoint, ObCartesianMultilinestring,
                                        ObCartesianMultipolygon>(g1, g2, context, result,
                                        ob_geo_func_difference_eval_tree_cart);
} OB_GEO_FUNC_END;

OB_GEO_CART_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObCartesianMultipolygon, ObCartesianMultipolygon, ObGeometry *)
{
  return apply_bg_difference<ObCartesianMultipolygon,
      ObCartesianMultipolygon,
      ObCartesianMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographMultipoint, ObGeographMultipoint, ObGeometry *)
{
  return apply_bg_difference<ObGeographMultipoint, ObGeographMultipoint, ObGeographMultipoint>(
      g1, g2, context, result);
}
OB_GEO_FUNC_END;

// tree geograph polygon
OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographMultipoint, ObGeographMultilinestring, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObGeographMultipoint,
      ObGeographMultilinestring,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographMultipoint, ObGeographMultipolygon, ObGeometry *)
{
  return apply_bg_difference_pl_strategy<ObGeographMultipoint,
      ObGeographMultipolygon,
      ObGeographMultipoint>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographLineString, ObGeographMultilinestring, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObGeographLineString,
      ObGeographMultilinestring,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographMultilinestring, ObGeographMultilinestring, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObGeographMultilinestring,
      ObGeographMultilinestring,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographMultilinestring, ObGeographMultipolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObGeographMultilinestring,
      ObGeographMultipolygon,
      ObGeographMultilinestring>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

OB_GEO_GEOG_TREE_FUNC_BEGIN(
    ObGeoFuncDifferenceImpl, ObGeographMultipolygon, ObGeographMultipolygon, ObGeometry *)
{
  return apply_bg_difference_ll_strategy<ObGeographMultipolygon,
      ObGeographMultipolygon,
      ObGeographMultipolygon>(g1, g2, context, result);
}
OB_GEO_FUNC_END;

static int ob_geo_func_difference_eval_tree(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result)
{
  int ret = OB_SUCCESS;
  if (g1->crs() != g2->crs()) {
    ret = OB_ERR_GIS_DIFFERENT_SRIDS;
  } else {
    switch (g1->crs()) {
    case common::ObGeoCRS::Cartesian:
      ret = ObGeoFuncDifferenceImpl::eval_tree_binary_cart(g1, g2, context, result);
      break;
    case common::ObGeoCRS::Geographic:
      ret = ObGeoFuncDifferenceImpl::eval_tree_binary_geog(g1, g2, context, result);
      break;
    default:
      ret = OB_ERR_GIS_INVALID_DATA;
      break;
    }
  }
  return ret;
}

extern int ob_geo_func_difference_eval_wkb_cart(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result);
extern int ob_geo_func_difference_eval_wkb_geog(const ObGeometry *g1, const ObGeometry *g2,
    const ObGeoEvalCtx &context, ObGeometry *&result);

// implement of outer class eval
// use an outer class to void implement templates in header files
int ObGeoFuncDifference::eval(const ObGeoEvalCtx &gis_context, ObGeometry *&result)
{
  return ObGeoFuncDifferenceImpl::eval_geo_func_split(gis_context, result,
      ob_geo_func_difference_eval_wkb_cart,
      ob_geo_func_difference_eval_wkb_geog,
      ob_geo_func_difference_eval_tree);
}

}  // namespace common
}  // namespace oceanbase
