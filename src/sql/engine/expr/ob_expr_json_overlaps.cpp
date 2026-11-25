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
#include "ob_expr_json_overlaps.h"
#include <optional>
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_native_path.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprJsonOverlaps::ObExprJsonOverlaps(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_OVERLAPS, N_JSON_OVERLAPS, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonOverlaps::~ObExprJsonOverlaps()
{
}

int ObExprJsonOverlaps::calc_result_type2(ObExprResType &type,
                                          ObExprResType &type1,
                                          ObExprResType &type2,
                                          ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  type.set_int32();
  type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
  if (!ob_is_string_type(type1.get_type())) {
  } else if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(type1, 1, N_JSON_OVERLAPS))) {
  }
  if (OB_FAIL(ret)) {
  } else if (!ob_is_string_type(type2.get_type())) {
  } else if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(type2, 2, N_JSON_OVERLAPS))) {
  }
  return ret;
}

int ObExprJsonOverlaps::eval_json_overlaps(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  bool is_null_result = false;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  std::optional<rust::Box<common::json::Query>> json_a;
  std::optional<rust::Box<common::json::Query>> json_b;
  if (!ObJsonExprHelper::is_convertible_to_json(expr.args_[0]->datum_meta_.type_)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, 1, N_JSON_OVERLAPS);
  } else if (!ObJsonExprHelper::is_convertible_to_json(expr.args_[1]->datum_meta_.type_)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, 2, N_JSON_OVERLAPS);
  } else if (OB_FAIL(get_native_json_query(
                 expr, ctx, temp_allocator, 0, json_a, is_null_result))) {
  } else if (OB_FAIL(get_native_json_query(
                 expr, ctx, temp_allocator, 1, json_b, is_null_result))) {
  } else if (!is_null_result) {
    common::json::BoolResult comparison = (*json_a)->overlaps(**json_b);
    if (comparison.platform_error != OB_SUCCESS) {
      ret = comparison.platform_error;
    } else if (comparison.status != common::json::Status::Ok) {
      ret = comparison.status == common::json::Status::NumericOverflow
                ? OB_NUMERIC_OVERFLOW
                : comparison.status == common::json::Status::UnsupportedComparison
                    ? OB_OP_NOT_ALLOW : OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
    } else {
      res.set_int(static_cast<int64_t>(comparison.value));
    }
  }
  if (OB_SUCC(ret) && is_null_result) {
    res.set_null();
  }
  return ret;
}

int ObExprJsonOverlaps::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                const ObRawExpr &raw_expr,
                                ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_overlaps;
  return OB_SUCCESS;
}

}
}
