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
#include "ob_expr_json_contains_path.h"
#include <optional>
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "sql/engine/expr/ob_expr_json_native_path.h"
#include "lib/utility/ob_defer.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObExprJsonContainsPath::ObExprJsonContainsPath(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_CONTAINS_PATH, N_JSON_CONTAINS_PATH, MORE_THAN_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonContainsPath::~ObExprJsonContainsPath()
{
}

int ObExprJsonContainsPath::calc_result_typeN(ObExprResType& type,
                                              ObExprResType* types_stack,
                                              int64_t param_num,
                                              ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx); 
  int ret = OB_SUCCESS;
  // set result to bool
  type.set_int32();
  type.set_precision(DEFAULT_PRECISION_FOR_BOOL);
  type.set_scale(ObAccuracy::DDL_DEFAULT_ACCURACY[ObIntType].scale_);
  
  if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_CONTAINS_PATH))) {
  }

  // set type of one or all flag
  if (OB_SUCC(ret)) {
    if (types_stack[1].get_type() == ObNullType) {
    } else if (ob_is_string_type(types_stack[1].get_type())) {
      if (types_stack[1].get_charset_type() != CHARSET_UTF8MB4) {
        types_stack[1].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    } else {
      ret = OB_ERR_JSON_BAD_ONE_OR_ALL_ARG;
      LOG_USER_ERROR(OB_ERR_JSON_BAD_ONE_OR_ALL_ARG);
    }
  }

  for (int64_t i = 1; OB_SUCC(ret) && i < param_num; i++) {
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, i))) {
    }
  }

  return ret;
}

int ObExprJsonContainsPath::eval_json_contains_path(const ObExpr &expr,
                                                    ObEvalCtx &ctx,
                                                    ObDatum &res)
{
  int ret = OB_SUCCESS;
  bool is_null_result = false;
  ObString json_doc;
  ObJsonInType input_type = ObJsonInType::JSON_TEXT;
  ObIJsonBase *partial_document = nullptr;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  DEFER(if (partial_document != nullptr) { partial_document->reset(); });
  ObDatum *doc_datum = NULL;
  ObExpr *doc_arg = expr.args_[0];
  ObObjType doc_type = doc_arg->datum_meta_.type_;
  ObCollationType doc_cs_type = doc_arg->datum_meta_.cs_type_;
  bool allow_partial_update = false;
  if (OB_FAIL(temp_allocator.eval_arg(doc_arg, ctx, doc_datum))) {
  } else if (doc_type == ObNullType || doc_datum->is_null()) {
    is_null_result = true;
  } else if (doc_type != ObJsonType && !ob_is_string_type(doc_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("input type error", K(doc_type));
  } else if (OB_FAIL(ObJsonExprHelper::ensure_collation(doc_type, doc_cs_type))) {
  } else if (ob_is_json(doc_type)
      && OB_FAIL(ObJsonExprHelper::is_allow_partial_update(
             expr, ctx, doc_datum->get_string(), allow_partial_update))) {
  } else if (allow_partial_update) {
    if (OB_FAIL(ObJsonExprHelper::get_json_for_partial_update(
            expr, *doc_arg, ctx, temp_allocator, *doc_datum, partial_document))) {
    } else if (OB_FAIL(partial_document->get_raw_binary(json_doc, &temp_allocator))) {
    } else {
      input_type = ObJsonInType::JSON_BIN;
    }
  } else if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
                 doc_arg, ctx, temp_allocator, json_doc, is_null_result))) {
  } else {
    input_type = ObJsonExprHelper::get_json_internal_type(doc_type);
  }
  std::optional<rust::Box<common::json::Query>> query;
  if (OB_SUCC(ret) && !is_null_result) {
    query.emplace(common::json::new_query(
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(json_doc.ptr()), json_doc.length()),
        input_type != ObJsonInType::JSON_BIN,
        ObJsonExprHelper::get_json_text_depth_limit(
            ObJsonExprHelper::get_json_max_depth_config())));
    if ((*query)->status() != common::json::Status::Ok) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_WARN("fail to parse JSON for path query", K(ret), K((*query)->byte_offset()));
    }
  }
  bool is_contains = false;
  if (OB_SUCC(ret) && !is_null_result) {
    // get one_or_all flag
    bool one_flag;
    ObDatum *json_datum = NULL;
    ObExpr *json_arg = expr.args_[1];
    ObObjType val_type = json_arg->datum_meta_.type_;
    if (OB_FAIL(temp_allocator.eval_arg(json_arg, ctx, json_datum))) {
    } else if (val_type == ObNullType || json_datum->is_null()) {
      is_null_result = true;
    } else {
      ObString target_str = json_datum->get_string();
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(json_arg, ctx, temp_allocator, target_str, is_null_result))) {
      } else if (0 == target_str.case_compare("one")) {
        one_flag = true;
      } else if (0 == target_str.case_compare("all")) {
        one_flag = false;
      } else {
        ret = OB_ERR_JSON_BAD_ONE_OR_ALL_ARG;
        LOG_USER_ERROR(OB_ERR_JSON_BAD_ONE_OR_ALL_ARG);
      }
    }

    if ((OB_SUCC(ret)) && !is_null_result) {
      JsonPathCache ctx_cache;
      JsonPathCache* path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
      path_cache = ((path_cache != NULL) ? path_cache : &ctx_cache);

      for (int64_t i = 2; OB_SUCC(ret) && i < expr.arg_cnt_ && !is_null_result; i++) {
        ObDatum *path_data = NULL;
        if (OB_FAIL(temp_allocator.eval_arg(expr.args_[i], ctx, path_data))) {
        } else if (expr.args_[i]->datum_meta_.type_ == ObNullType || path_data->is_null()) {
          is_null_result = true;
        } else {
          ObString path_val = path_data->get_string();
          const common::json::Path *json_path = NULL;
          if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(expr.args_[i], ctx, temp_allocator, path_val, is_null_result))) {
          } else if (OB_FAIL(find_and_add_native_path_cache(path_cache, json_path, path_val, i, true))) {
          }

          if (OB_SUCC(ret)) {
            common::json::PathQueryResult matches = (*query)->query(*json_path, true, true);
            if (matches.status != common::json::Status::Ok) {
              ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
            } else if (matches.match_count == 0) {
              if (!one_flag) {
                is_contains = false;
                break;
              }
            } else {
              is_contains = true;
              if (one_flag) {
                break;
              }
            }
          }
        }
      }
    }

  }

  // set result
  if (OB_FAIL(ret)) {
  } else if (is_null_result) {
    res.set_null();
  } else {
    res.set_int(static_cast<int64_t>(is_contains));
  }

  return ret;
}

int ObExprJsonContainsPath::cg_expr(ObExprCGCtx &expr_cg_ctx,
                                    const ObRawExpr &raw_expr,
                                    ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_contains_path;
  return OB_SUCCESS;
}

}
}
