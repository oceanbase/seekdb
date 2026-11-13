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
#include "ob_expr_json_keys.h"
#include <optional>
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_native_path.h"
#include "lib/utility/ob_defer.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonKeys::ObExprJsonKeys(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_KEYS, N_JSON_KEYS, ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonKeys::~ObExprJsonKeys()
{
}

int ObExprJsonKeys::calc_result_typeN(ObExprResType& type,
                                      ObExprResType* types_stack,
                                      int64_t param_num,
                                      ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx); 
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num > 2)) {
    ObString func_name_(N_JSON_KEYS);
    ret = OB_ERR_PARAM_SIZE;
    LOG_USER_ERROR(OB_ERR_PARAM_SIZE, func_name_.length(), func_name_.ptr());
  } else {
    // set result to json
    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    // set type of json_doc
    if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_KEYS))) {
    }

    // set type of json_path
    if (OB_SUCC(ret) && param_num == 2) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_path(types_stack, 1))) {
      }
    }

  }
  return ret;
}

int ObExprJsonKeys::eval_json_keys(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
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
  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  } else if (OB_FAIL(temp_allocator.eval_arg(doc_arg, ctx, doc_datum))) {
  } else if (doc_type == ObNullType || doc_datum->is_null()) {
    is_null_result = true;
  } else if (doc_type != ObJsonType && !ob_is_string_type(doc_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
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
    }
  }

  if (!is_null_result && OB_SUCC(ret) && expr.arg_cnt_ == 2) {
    JsonPathCache ctx_cache;
    JsonPathCache* path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    path_cache = ((path_cache != NULL) ? path_cache : &ctx_cache);

    ObDatum *path_data = NULL;
    if (expr.args_[1]->datum_meta_.type_ == ObNullType) {
      is_null_result = true;
    } else if (OB_FAIL(temp_allocator.eval_arg(expr.args_[1], ctx, path_data))) {
    } else {
      const common::json::Path *json_path = NULL;
      ObString path_val = path_data->get_string();
      common::json::BinaryResult keys = common::json::BinaryResult();
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(expr.args_[1], ctx, temp_allocator, path_val, is_null_result))) {
      } else if (OB_FAIL(find_and_add_native_path_cache(path_cache, json_path, path_val, 1, false))) {
      } else {
        keys = (*query)->object_keys_at_path(*json_path);
      }
      if (OB_FAIL(ret) || is_null_result) {
      } else if (keys.status != common::json::Status::Ok) {
        ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      } else if (keys.binary.empty()) {
        is_null_result = true;
      } else {
        const char *data = reinterpret_cast<const char *>(keys.binary.data());
        ObString str(keys.binary.size(), data);
        ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, res, str);
      }
    }
  }

  if (!is_null_result && OB_SUCC(ret) && expr.arg_cnt_ == 1) {
    common::json::BinaryResult keys = (*query)->object_keys();
    if (keys.status != common::json::Status::Ok) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
    } else if (keys.binary.empty()) {
      is_null_result = true;
    } else {
      const char *data = reinterpret_cast<const char *>(keys.binary.data());
      ObString str(keys.binary.size(), data);
      ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, res, str);
    }
  }

  // set result
  if (OB_UNLIKELY(OB_FAIL(ret))) {
  } else if (is_null_result){
    res.set_null();
  }

  return ret;
}

int ObExprJsonKeys::cg_expr(ObExprCGCtx &expr_cg_ctx,
                            const ObRawExpr &raw_expr,
                            ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_keys;
  return OB_SUCCESS;
}


}
}
