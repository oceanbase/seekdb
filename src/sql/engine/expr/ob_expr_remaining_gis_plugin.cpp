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

/* Plugin adapters for the remaining legacy GIS operators. */
#include "sql/engine/expr/ob_expr_priv_st_transform.h"
#include "sql/engine/expr/ob_expr_st_bestsrid.h"
#include "sql/engine/expr/ob_expr_st_buffer.h"
#include "sql/engine/expr/ob_expr_priv_st_clipbybox2d.h"
#include "sql/engine/expr/ob_expr_st_union.h"
#include "sql/engine/expr/ob_expr_st_difference.h"
#include "sql/engine/expr/ob_expr_st_symdifference.h"
#include "sql/engine/expr/ob_expr_priv_st_asmvtgeom.h"
#include "sql/engine/expr/ob_expr_priv_st_makevalid.h"
#include "sql/engine/expr/ob_expr_priv_st_point.h"
#include "sql/engine/expr/ob_plugin_expr_utils.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

namespace
{
inline void set_geometry_result_type(ObExprResType &type)
{
  type.set_geometry();
  type.set_length(ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType].get_length());
}

inline int set_geometry_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                     ObExprTypeCtx &)
{
  set_geometry_result_type(type);
  return OB_SUCCESS;
}
}

ObExprPrivSTTransform::ObExprPrivSTTransform(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_TRANSFORM, N_PRIV_ST_TRANSFORM,
                       TWO_OR_THREE, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
int ObExprPrivSTTransform::calc_result_typeN(ObExprResType &type, ObExprResType *types,
                                             int64_t n, ObExprTypeCtx &ctx) const
{ return set_geometry_result_typeN(type, types, n, ctx); }
int ObExprPrivSTTransform::eval_priv_st_transform(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_priv_transform", e, c, r); }
int ObExprPrivSTTransform::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_priv_st_transform; return OB_SUCCESS; }

ObExprPrivSTBestsrid::ObExprPrivSTBestsrid(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_BESTSRID, N_PRIV_ST_BESTSRID,
                       ONE_OR_TWO, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
ObExprPrivSTBestsrid::~ObExprPrivSTBestsrid() {}
int ObExprPrivSTBestsrid::calc_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                            ObExprTypeCtx &) const
{ type.set_int32(); return OB_SUCCESS; }
int ObExprPrivSTBestsrid::eval_st_bestsrid(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_geometry_int32("st_bestsrid", e, c, r); }
int ObExprPrivSTBestsrid::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_st_bestsrid; return OB_SUCCESS; }

ObExprSTBufferStrategy::ObExprSTBufferStrategy(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_ST_BUFFER_STRATEGY, N_ST_BUFFER_STRATEGY,
                       ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
int ObExprSTBufferStrategy::calc_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                              ObExprTypeCtx &) const
{ type.set_varchar(); type.set_length(BUF_STRATEGY_RES_LENGTH); type.set_collation_type(CS_TYPE_BINARY); return OB_SUCCESS; }
int ObExprSTBufferStrategy::eval_st_buffer_strategy(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_buffer_strategy", e, c, r); }
int ObExprSTBufferStrategy::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_st_buffer_strategy; return OB_SUCCESS; }
ObGeoBufferStrategyType ObExprSTBufferStrategy::get_strategy_type_by_name(const ObString &)
{ return ObGeoBufferStrategyType::INVALID; }

#define DEFINE_GEOMETRY_BINARY_ADAPTER(Class, CtorType, NName, EvalName, Service) \
Class::Class(ObIAllocator &alloc) : ObFuncExprOperator(alloc, CtorType, NName, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {} \
Class::~Class() {} \
int Class::calc_result_type2(ObExprResType &type, ObExprResType &, ObExprResType &, ObExprTypeCtx &) const { set_geometry_result_type(type); return OB_SUCCESS; } \
int Class::EvalName(const ObExpr &e, ObEvalCtx &c, ObDatum &r) { return execute_plugin_gis_values(Service, e, c, r); } \
int Class::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const { rt.eval_func_ = EvalName; return OB_SUCCESS; }

DEFINE_GEOMETRY_BINARY_ADAPTER(ObExprPrivSTClipByBox2D, T_FUN_SYS_PRIV_ST_CLIPBYBOX2D, N_PRIV_ST_CLIPBYBOX2D, eval_priv_st_clipbybox2d, "st_clipbybox2d")
DEFINE_GEOMETRY_BINARY_ADAPTER(ObExprSTUnion, T_FUN_SYS_ST_UNION, N_ST_UNION, eval_st_union, "st_union")
DEFINE_GEOMETRY_BINARY_ADAPTER(ObExprSTDifference, T_FUN_SYS_ST_DIFFERENCE, N_ST_DIFFERENCE, eval_st_difference, "st_difference")
DEFINE_GEOMETRY_BINARY_ADAPTER(ObExprSTSymDifference, T_FUN_SYS_ST_SYMDIFFERENCE, N_ST_SYMDIFFERENCE, eval_st_symdifference, "st_symdifference")

#undef DEFINE_GEOMETRY_BINARY_ADAPTER

ObExprSTBuffer::ObExprSTBuffer(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_ST_BUFFER, N_ST_BUFFER, MORE_THAN_ONE,
                       VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
ObExprSTBuffer::ObExprSTBuffer(ObIAllocator &alloc, ObExprOperatorType type,
                               const char *name, int32_t param_num,
                               ObValidForGeneratedColFlag valid_for_generated_col,
                               int32_t dimension)
  : ObFuncExprOperator(alloc, type, name, param_num, valid_for_generated_col, dimension) {}
int ObExprSTBuffer::calc_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                      ObExprTypeCtx &) const
{ set_geometry_result_type(type); return OB_SUCCESS; }
int ObExprSTBuffer::eval_st_buffer(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_buffer", e, c, r); }
int ObExprSTBuffer::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_st_buffer; return OB_SUCCESS; }

ObExprPrivSTBuffer::ObExprPrivSTBuffer(ObIAllocator &alloc)
  : ObExprSTBuffer(alloc, T_FUN_SYS_PRIV_ST_BUFFER, N_PRIV_ST_BUFFER, TWO_OR_THREE,
                   NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
int ObExprPrivSTBuffer::calc_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                          ObExprTypeCtx &) const
{ set_geometry_result_type(type); return OB_SUCCESS; }
int ObExprPrivSTBuffer::eval_priv_st_buffer(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_priv_buffer", e, c, r); }
int ObExprPrivSTBuffer::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_priv_st_buffer; return OB_SUCCESS; }

ObExprPrivSTPoint::ObExprPrivSTPoint(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_POINT, N_PRIV_ST_POINT,
                       TWO_OR_THREE, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
ObExprPrivSTPoint::~ObExprPrivSTPoint() {}
int ObExprPrivSTPoint::calc_result_typeN(ObExprResType &type, ObExprResType *types,
                                         int64_t param_num, ObExprTypeCtx &) const
{
  UNUSED(types);
  UNUSED(param_num);
  set_geometry_result_type(type);
  return OB_SUCCESS;
}
int ObExprPrivSTPoint::eval_priv_st_point(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_point", e, c, r); }
int ObExprPrivSTPoint::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_priv_st_point; return OB_SUCCESS; }

ObExprPrivSTAsMVTGeom::ObExprPrivSTAsMVTGeom(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_ASMVTGEOM, N_PRIV_ST_ASMVTGEOM,
                       MORE_THAN_ONE, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
ObExprPrivSTAsMVTGeom::~ObExprPrivSTAsMVTGeom() {}
int ObExprPrivSTAsMVTGeom::calc_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                             ObExprTypeCtx &) const
{ set_geometry_result_type(type); return OB_SUCCESS; }
int ObExprPrivSTAsMVTGeom::eval_priv_st_asmvtgeom(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_asmvtgeom", e, c, r); }
int ObExprPrivSTAsMVTGeom::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_priv_st_asmvtgeom; return OB_SUCCESS; }

ObExprPrivSTMakeValid::ObExprPrivSTMakeValid(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_FUN_SYS_PRIV_ST_MAKE_VALID, N_PRIV_ST_MAKEVALID,
                       ZERO_OR_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION) {}
ObExprPrivSTMakeValid::~ObExprPrivSTMakeValid() {}
int ObExprPrivSTMakeValid::calc_result_typeN(ObExprResType &type, ObExprResType *, int64_t,
                                             ObExprTypeCtx &) const
{ set_geometry_result_type(type); return OB_SUCCESS; }
int ObExprPrivSTMakeValid::eval_priv_st_makevalid(const ObExpr &e, ObEvalCtx &c, ObDatum &r)
{ return execute_plugin_gis_values("st_makevalid", e, c, r); }
int ObExprPrivSTMakeValid::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt) const
{ rt.eval_func_ = eval_priv_st_makevalid; return OB_SUCCESS; }

} // namespace sql
} // namespace oceanbase
