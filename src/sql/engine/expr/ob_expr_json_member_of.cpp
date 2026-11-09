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
#include "ob_expr_json_func_helper.h"
#include <optional>
#include "ob_expr_json_member_of.h"
#include "ob_expr_json_native_path.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprJsonMemberOf::ObExprJsonMemberOf(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_MEMBER_OF, N_JSON_MEMBER_OF, 2, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonMemberOf::~ObExprJsonMemberOf()
{
}

int ObExprJsonMemberOf::calc_result_type2(ObExprResType &type,
                                          ObExprResType &type1,
                                          ObExprResType &type2,
                                          ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  UNUSED(type2);
  int ret = OB_SUCCESS;
  type.set_int32();
  type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
  if (type1.get_type() == ObNullType) {
  } else if (ob_is_string_type(type1.get_type()) && type1.get_charset_type() != CHARSET_UTF8MB4) {
    type1.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  }
  return ret;
}

int ObExprJsonMemberOf::eval_json_member_of(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("JSONModule"));
  bool is_null_result = expr.args_[0]->datum_meta_.type_ == ObNullType;
  std::optional<rust::Box<common::json::Query>> candidate;
  std::optional<rust::Box<common::json::Query>> target;
  if (!is_null_result) {
    ObDatum *json_datum = NULL;
    ObExpr *json_arg = expr.args_[0];
    ObObjType target_type = expr.args_[1]->datum_meta_.type_;
    ObString candidate_binary;
    if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
    } else if (json_datum->is_null()) {
      is_null_result = true;
    } else if (OB_FAIL(ObJsonExprHelper::get_json_binary(
                   expr, ctx, temp_allocator, 0, candidate_binary))) {
    } else if (!ObJsonExprHelper::is_convertible_to_json(target_type)) {
      ret = OB_ERR_INVALID_TYPE_FOR_JSON;
      LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, 2, N_JSON_MEMBER_OF);
    } else if (OB_FAIL(get_native_json_query(
                   expr, ctx, temp_allocator, 1, target, is_null_result))) {
    } else if (!is_null_result) {
      candidate.emplace(common::json::new_query(
          rust::Slice<const uint8_t>(
              reinterpret_cast<const uint8_t *>(candidate_binary.ptr()), candidate_binary.length()),
          false,
          ObJsonExprHelper::get_json_text_depth_limit(
              ObJsonExprHelper::get_json_max_depth_config())));
      if ((*candidate)->status() != common::json::Status::Ok) {
        ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      }
    }
  }
  if (OB_FAIL(ret) || is_null_result) {
    if (OB_SUCC(ret)) {
      res.set_null();
    }
  } else {
    common::json::BoolResult comparison = (*candidate)->member_of(**target);
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
  return ret;
}

int ObExprJsonMemberOf::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                const ObRawExpr &raw_expr,
                                ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_member_of;
  return OB_SUCCESS;
}

}
}
