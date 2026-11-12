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
#include "ob_expr_json_contains.h"
#include <optional>
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "ob_expr_json_native_path.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprJsonContains::ObExprJsonContains(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_CONTAINS, N_JSON_CONTAINS, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonContains::~ObExprJsonContains()
{
}

int ObExprJsonContains::calc_result_typeN(ObExprResType &type,
                                          ObExprResType *types_stack,
                                          int64_t param_num,
                                          ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num > 3)) {
    ret = OB_ERR_PARAM_SIZE;
    ObString func_name_(N_JSON_CONTAINS);
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    type.set_int32();
    type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
    type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
    for (int64_t i = 0; OB_SUCC(ret) && i < 2; i++) {
      ObObjType in_type = types_stack[i].get_type();
      if (!ob_is_string_type(in_type)) {
      } else if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, i, N_JSON_CONTAINS))) {
      }
    }
    if (OB_SUCC(ret) && param_num == 3) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, 2))) {
      }
    }
  }
  return ret;
}

int ObExprJsonContains::eval_json_contains(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  bool is_null_result = false;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  std::optional<rust::Box<common::json::Query>> json_target;
  std::optional<rust::Box<common::json::Query>> json_candidate;
  if (OB_FAIL(get_native_json_query(
          expr, ctx, temp_allocator, 0, json_target, is_null_result))) {
  } else if (!ObJsonExprHelper::is_convertible_to_json(expr.args_[1]->datum_meta_.type_)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, 2, N_JSON_CONTAINS);
  } else if (!is_null_result && OB_FAIL(get_native_json_query(
                 expr, ctx, temp_allocator, 1, json_candidate, is_null_result))) {
  }

  common::json::BoolResult comparison = common::json::BoolResult();
  if (!is_null_result && OB_SUCC(ret) && expr.arg_cnt_ == 3) {
    JsonPathCache ctx_cache;
    JsonPathCache *path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    path_cache = ((path_cache != NULL) ? path_cache : &ctx_cache);
    ObDatum *path_data = NULL;
    if (OB_FAIL(temp_allocator.eval_arg(expr.args_[2], ctx, path_data))) {
    } else if (expr.args_[2]->datum_meta_.type_ == ObNullType || path_data->is_null()) {
      is_null_result = true;
    } else {
      ObString path_val = path_data->get_string();
      const common::json::Path *json_path = NULL;
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
              expr.args_[2], ctx, temp_allocator, path_val, is_null_result))) {
      } else if (OB_FAIL(find_and_add_native_path_cache(
                     path_cache, json_path, path_val, 2, false))) {
      } else {
        comparison = (*json_target)->contains_at_path(**json_candidate, *json_path);
      }
    }
  } else if (!is_null_result && OB_SUCC(ret)) {
    comparison = (*json_target)->contains(**json_candidate);
  }

  if (OB_FAIL(ret)) {
  } else if (comparison.platform_error != OB_SUCCESS) {
    ret = comparison.platform_error;
  } else if (comparison.status != common::json::Status::Ok) {
    ret = comparison.status == common::json::Status::NumericOverflow
              ? OB_NUMERIC_OVERFLOW
              : comparison.status == common::json::Status::UnsupportedComparison
                    ? OB_OP_NOT_ALLOW : OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
  } else if (comparison.is_null || is_null_result) {
    res.set_null();
  } else {
    res.set_int(static_cast<int64_t>(comparison.value));
  }
  return ret;
}

int ObExprJsonContains::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                const ObRawExpr &raw_expr,
                                ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_contains;
  return OB_SUCCESS;
}

}
}
