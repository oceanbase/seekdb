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
#include "ob_expr_json_search.h"
#include <optional>
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_native_path.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprJsonSearch::ObExprJsonSearch(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_SEARCH, N_JSON_SEARCH, MORE_THAN_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonSearch::~ObExprJsonSearch()
{
}

int ObExprJsonSearch::calc_result_typeN(ObExprResType &type,
                                        ObExprResType *types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  INIT_SUCC(ret);
  if (OB_UNLIKELY(param_num < 3)) {
    ret = OB_ERR_PARAM_SIZE;
  } else {
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, "json_search"))) {
    }
    for (int64_t i = 1; OB_SUCC(ret) && i < param_num; i++) {
      if (types_stack[i].get_type() == ObNullType) {
      } else if (ob_is_string_type(types_stack[i].get_type())) {
        types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      } else {
        types_stack[i].set_calc_type(ObLongTextType);
        types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }
  }
  return ret;
}

int ObExprJsonSearch::eval_json_search(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  bool is_null = false;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  std::optional<rust::Box<common::json::Query>> query;
  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  } else if (OB_FAIL(get_native_json_query(expr, ctx, temp_allocator, 0, query, is_null))) {
  }

  bool one_flag = false;
  ObDatum *json_datum = NULL;
  if (OB_SUCC(ret) && !is_null) {
    ObExpr *json_arg = expr.args_[1];
    ObObjType val_type = json_arg->datum_meta_.type_;
    if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
    } else if (val_type == ObNullType || json_datum->is_null()) {
      is_null = true;
    } else if (!ob_is_string_type(val_type)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("input type error", K(val_type));
    } else {
      ObString target_str = json_datum->get_string();
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
              json_arg, ctx, temp_allocator, target_str, is_null))) {
      } else if (json_arg->datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN
                 && OB_FAIL(ObJsonExprHelper::convert_string_collation_type(
                        json_arg->datum_meta_.cs_type_, CS_TYPE_UTF8MB4_BIN,
                        &temp_allocator, target_str, target_str))) {
      } else if (target_str.case_compare("one") == 0) {
        one_flag = true;
      } else if (target_str.case_compare("all") == 0) {
        one_flag = false;
      } else {
        ret = OB_ERR_JSON_BAD_ONE_OR_ALL_ARG;
        LOG_USER_ERROR(OB_ERR_JSON_BAD_ONE_OR_ALL_ARG);
      }
    }
  }

  int32_t escape_wc = static_cast<int32_t>('\\');
  if (OB_SUCC(ret) && expr.arg_cnt_ >= 4 && !is_null) {
    ObExpr *json_arg = expr.args_[3];
    ObObjType val_type = json_arg->datum_meta_.type_;
    if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
    } else if (val_type == ObNullType || json_datum->is_null()) {
    } else if (!ob_is_string_type(val_type)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("input type error", K(val_type));
    } else {
      ObString escape = json_datum->get_string();
      bool is_null_str = false;
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
              json_arg, ctx, temp_allocator, escape, is_null_str))) {
      } else if (json_arg->datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN
                 && OB_FAIL(ObJsonExprHelper::convert_string_collation_type(
                        json_arg->datum_meta_.cs_type_, CS_TYPE_UTF8MB4_BIN,
                        &temp_allocator, escape, escape))) {
      } else if (escape.length() > 0) {
        const ObCollationType escape_coll = json_arg->datum_meta_.cs_type_;
        size_t length = ObCharset::strlen_char(escape_coll, escape.ptr(), escape.length());
        if (length != 1) {
          ret = OB_INVALID_ARGUMENT;
        } else if (OB_FAIL(ObCharset::mb_wc(escape_coll, escape, escape_wc))) {
          ret = OB_INVALID_ARGUMENT;
        }
      }
    }
  }

  ObString target;
  if (OB_SUCC(ret) && !is_null) {
    ObExpr *json_arg = expr.args_[2];
    ObObjType val_type = json_arg->datum_meta_.type_;
    if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
    } else if (val_type == ObNullType || json_datum->is_null()) {
      is_null = true;
    } else if (!ob_is_string_type(val_type)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("input type error", K(val_type));
    } else if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
                   json_arg, ctx, temp_allocator, target, is_null))) {
    } else if (json_arg->datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN
               && OB_FAIL(ObJsonExprHelper::convert_string_collation_type(
                      json_arg->datum_meta_.cs_type_, CS_TYPE_UTF8MB4_BIN,
                      &temp_allocator, target, target))) {
    }
  }

  if (OB_SUCC(ret) && !is_null && expr.arg_cnt_ >= 5) {
    JsonPathCache ctx_cache;
    JsonPathCache *path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    path_cache = path_cache != NULL ? path_cache : &ctx_cache;
    for (uint64_t i = 4; OB_SUCC(ret) && !is_null && i < expr.arg_cnt_; i++) {
      ObExpr *json_arg = expr.args_[i];
      ObObjType val_type = json_arg->datum_meta_.type_;
      if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
      } else if (val_type == ObNullType || json_datum->is_null()) {
        is_null = true;
      } else if (!ob_is_string_type(val_type)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("input type error", K(val_type));
      } else {
        ObString path_text = json_datum->get_string();
        const common::json::Path *json_path = NULL;
        if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
                json_arg, ctx, temp_allocator, path_text, is_null))) {
        } else if (path_text.length() == 0) {
          is_null = true;
        } else if (json_arg->datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN
                   && OB_FAIL(ObJsonExprHelper::convert_string_collation_type(
                          json_arg->datum_meta_.cs_type_, CS_TYPE_UTF8MB4_BIN,
                          &temp_allocator, path_text, path_text))) {
        } else if (OB_FAIL(find_and_add_native_path_cache(
                       path_cache, json_path, path_text, i, true))) {
        } else if ((*query)->add_path(*json_path) != common::json::Status::Ok) {
          ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
        }
      }
    }
  }

  common::json::BinaryResult search_result = common::json::BinaryResult();
  if (OB_SUCC(ret) && !is_null) {
    rust::Slice<const uint8_t> pattern(
        reinterpret_cast<const uint8_t *>(target.ptr()), target.length());
    search_result = (*query)->search(pattern, static_cast<uint32_t>(escape_wc), one_flag);
    if (search_result.status != common::json::Status::Ok) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
    } else if (search_result.binary.empty()) {
      is_null = true;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (is_null) {
    res.set_null();
  } else {
    const char *data = reinterpret_cast<const char *>(search_result.binary.data());
    ObString raw_bin(search_result.binary.size(), data);
    ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, res, raw_bin);
  }
  return ret;
}

int ObExprJsonSearch::cg_expr(ObExprCGCtx &expr_cg_ctx,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_search;
  return OB_SUCCESS;
}

} // namespace sql
} // namespace oceanbase
