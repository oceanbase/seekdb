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

#define USING_LOG_PREFIX SQL_ENG
#include "sql/engine/expr/ob_expr_point.h"
#include "sql/engine/expr/ob_geo_expr_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprPoint::ObExprPoint(common::ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_POINT,
                         N_POINT,
                         2,
                         VALID_FOR_GENERATED_COL,
                         NOT_ROW_DIMENSION)
{
}

ObExprPoint::~ObExprPoint()
{

}

int ObExprPoint::calc_result_type2(ObExprResType &type,
                                   ObExprResType &type1,
                                   ObExprResType &type2,
                                   common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  SqlValueType type_x = type1.get_type();
  SqlValueType type_y = type2.get_type();

  if (ob_is_geometry_tc(type_x) || ob_is_geometry_tc(type_y)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "input type should not be geometry type");
  } else {
    if (!ob_is_double_tc(type_x)) {
      type1.set_calc_type(ObDoubleType);
    }
    if (!ob_is_double_tc(type_y)) {
      type2.set_calc_type(ObDoubleType);
    }
    type.set_geometry();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObGeometryType]).get_length());
  }

  return ret;
}

// for old sql engine

int ObExprPoint::eval_point(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            ObDatum &res)
{
	int ret = OB_SUCCESS;
  bool is_null_result = false;
  ObDatum *datum_x = NULL;
  ObDatum *datum_y = NULL;
  ObExpr *arg_x = expr.args_[0];
  ObExpr *arg_y = expr.args_[1];
  SqlValueType type_x = arg_x->datum_meta_.type_;
  SqlValueType type_y = arg_y->datum_meta_.type_;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  common::ObArenaAllocator &tmp_allocator = tmp_alloc_g.get_allocator();
  ObWkbBuffer res_wkb_buf(tmp_allocator);

  if (ob_is_null(type_x) || ob_is_null(type_y)) {
    is_null_result = true;
  } else if (OB_FAIL(arg_x->eval(ctx, datum_x))) {
  } else if (OB_FAIL(arg_y->eval(ctx, datum_y))) {
  } else if (datum_x->is_null() || datum_y->is_null()) {
    is_null_result = true;
  } else {
    double x = datum_x->get_double();
    double y = datum_y->get_double();
    uint32_t srid = 0;
    if (OB_FAIL(res_wkb_buf.append(srid))) {
    } else if (OB_FAIL(res_wkb_buf.append(static_cast<char>(ENCODE_GEO_VERSION(GEO_VESION_1))))) {
    } else if (OB_FAIL(res_wkb_buf.append(static_cast<char>(ObGeoWkbByteOrder::LittleEndian)))) {
    } else if (OB_FAIL(res_wkb_buf.append(static_cast<uint32_t>(ObGeoType::POINT)))) {
    } else if (OB_FAIL(res_wkb_buf.append(x))) {
    } else if (OB_FAIL(res_wkb_buf.append(y))) {
    }
  }

  if (OB_SUCC(ret)) {
    if (is_null_result) {
      res.set_null();
    } else if (OB_FAIL(ObGeoExprUtils::pack_geo_res(expr, ctx, res, res_wkb_buf.string()))) {
    }
  }

  return ret;
}

int ObExprPoint::cg_expr(ObExprCGCtx &expr_cg_ctx,
                         const ObRawExpr &raw_expr,
                         ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_point;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
