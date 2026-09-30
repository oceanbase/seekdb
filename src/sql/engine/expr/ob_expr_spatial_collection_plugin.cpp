/*
 * Copyright (c) 2026 OceanBase.
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

/* Core-only adapter implementations for spatial collection constructors. */
#include "sql/engine/expr/ob_expr_spatial_collection.h"
#include "sql/engine/expr/ob_plugin_expr_utils.h"
#include <strings.h>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprSpatialCollection::ObExprSpatialCollection(ObIAllocator &alloc,
                                                 ObExprOperatorType type,
                                                 const char *name,
                                                 int32_t param_num,
                                                 int32_t dimension)
    : ObFuncExprOperator(alloc, type, name, param_num,
                         NOT_VALID_FOR_GENERATED_COL, dimension)
{}

ObExprSpatialCollection::~ObExprSpatialCollection() {}

int ObExprSpatialCollection::calc_result_typeN(ObExprResType &type,
                                                ObExprResType *types_stack,
                                                int64_t param_num,
                                                ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  for (int64_t i = 0; i < param_num; ++i) {
    if (!ob_is_null(types_stack[i].get_type()) &&
        !ob_is_geometry(types_stack[i].get_type()) &&
        !ob_is_string_type(types_stack[i].get_type())) {
      types_stack[i].set_calc_type(ObVarcharType);
      types_stack[i].set_calc_collation_type(CS_TYPE_BINARY);
    }
  }
  type.set_geometry();
  type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
  return OB_SUCCESS;
}

int ObExprSpatialCollection::calc_resultN(ObObj &result,
                                          const ObObj *objs,
                                          int64_t param_num,
                                          ObExprCtx &expr_ctx) const
{
  UNUSED(result);
  UNUSED(objs);
  UNUSED(param_num);
  UNUSED(expr_ctx);
  return OB_NOT_SUPPORTED;
}

int ObExprSpatialCollection::eval_spatial_collection(const ObExpr &expr,
                                                     ObEvalCtx &ctx,
                                                     ObDatum &res)
{
  const char *service = get_func_name();
  if (0 == strcasecmp(service, N_LINESTRING)) service = "st_linestring";
  else if (0 == strcasecmp(service, N_POLYGON)) service = "st_polygon";
  else if (0 == strcasecmp(service, N_MULTIPOINT)) service = "st_multipoint";
  else if (0 == strcasecmp(service, N_MULTILINESTRING)) service = "st_multilinestring";
  else if (0 == strcasecmp(service, N_MULTIPOLYGON)) service = "st_multipolygon";
  else if (0 == strcasecmp(service, N_GEOMCOLLECTION)) service = "st_geomcollection";
  else service = "st_geometrycollection";
  return execute_plugin_geometry_variadic(service, expr, ctx, res);
}

#define DEFINE_COLLECTION_EXPR(Class, Type, Name, Service, EvalName) \
Class::Class(ObIAllocator &alloc) : ObExprSpatialCollection(alloc, Type, Name, PARAM_NUM_UNKNOWN, NOT_ROW_DIMENSION) {} \
Class::~Class() {} \
int Class::EvalName(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res) { \
  ObEvalCtx::TempAllocGuard guard(ctx); Class value(guard.get_allocator()); \
  return value.eval_spatial_collection(expr, ctx, res); } \
int Class::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const { \
  rt_expr.eval_func_ = EvalName; return OB_SUCCESS; }

DEFINE_COLLECTION_EXPR(ObExprLineString, T_FUN_SYS_LINESTRING, N_LINESTRING,
                       "st_linestring", eval_linestring)
DEFINE_COLLECTION_EXPR(ObExprPolygon, T_FUN_SYS_POLYGON, N_POLYGON,
                       "st_polygon", eval_polygon)
DEFINE_COLLECTION_EXPR(ObExprMultiPoint, T_FUN_SYS_MULTIPOINT, N_MULTIPOINT,
                       "st_multipoint", eval_multipoint)
DEFINE_COLLECTION_EXPR(ObExprMultiLineString, T_FUN_SYS_MULTILINESTRING, N_MULTILINESTRING,
                       "st_multilinestring", eval_multilinestring)
DEFINE_COLLECTION_EXPR(ObExprMultiPolygon, T_FUN_SYS_MULTIPOLYGON, N_MULTIPOLYGON,
                       "st_multipolygon", eval_multipolygon)
DEFINE_COLLECTION_EXPR(ObExprGeomCollection, T_FUN_SYS_GEOMCOLLECTION, N_GEOMCOLLECTION,
                       "st_geomcollection", eval_geomcollection)
DEFINE_COLLECTION_EXPR(ObExprGeometryCollection, T_FUN_SYS_GEOMCOLLECTION, N_GEOMETRYCOLLECTION,
                       "st_geometrycollection", eval_geometrycollection)

#undef DEFINE_COLLECTION_EXPR

} // namespace sql
} // namespace oceanbase
