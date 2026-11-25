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
#include "ob_expr_json_extract.h"
#include "ob_expr_json_func_helper.h"
#include "ob_expr_json_native_path.h"
#include "lib/utility/ob_defer.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonExtract::ObExprJsonExtract(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_JSON_EXTRACT, N_JSON_EXTRACT, MORE_THAN_ONE, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprJsonExtract::~ObExprJsonExtract()
{
}

int ObExprJsonExtract::calc_result_typeN(ObExprResType& type,
                                         ObExprResType* types_stack,
                                         int64_t param_num,
                                         ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(param_num < 2)) {
    ret = OB_ERR_PARAM_SIZE;
  } else {
    // 1st param is json doc
    ObObjType in_type = types_stack[0].get_type();
    bool is_null_result = false;

    if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, 0, N_JSON_EXTRACT))) {
    } else if (in_type == ObNullType) {
      is_null_result = true;
    } else if (in_type == ObJsonType) {
      // do nothing
    } else if (ob_is_string_type(in_type) && types_stack[0].get_collation_type() != CS_TYPE_BINARY) {
      if (types_stack[0].get_charset_type() != CHARSET_UTF8MB4) {
        types_stack[0].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }

    // following params are path strings
    for (int64_t i = 1; i < param_num && OB_SUCC(ret); i++) {
      if (types_stack[i].get_type() == ObNullType) {
        is_null_result = true;
      } else if (ob_is_string_type(types_stack[i].get_type())) {
        if (types_stack[i].get_charset_type() != CHARSET_UTF8MB4) {
          types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
        }
      } else {
        types_stack[i].set_calc_type(ObLongTextType);
        types_stack[i].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }

    if (OB_SUCC(ret)) {
      if (is_null_result) {
        type.set_null();
      } else {
        type.set_json();
        type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
      }
    }
  }
  return ret;
}

int ObExprJsonExtract::eval_json_extract_null(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  UNUSED(expr);
  UNUSED(ctx);
  res.set_null();
  return OB_SUCCESS;
}

int ObExprJsonExtract::eval_json_extract(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObExpr *json_arg = expr.args_[0];
  ObObjType val_type = json_arg->datum_meta_.type_;
  ObCollationType cs_type = json_arg->datum_meta_.cs_type_;
  bool is_null_result = false;
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  MultimodeAlloctor allocator(tmp_alloc_g.get_allocator());
  ObString json_doc;
  ObJsonInType input_type = ObJsonInType::JSON_TEXT;
  ObIJsonBase *partial_document = nullptr;
  DEFER(if (partial_document != nullptr) { partial_document->reset(); });
  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  } else {
    ObDatum *json_datum = NULL;
    bool allow_partial_update = false;
    if (OB_FAIL(allocator.eval_arg(json_arg, ctx, json_datum))) {
    } else if (val_type == ObNullType || json_datum->is_null()) {
      is_null_result = true;
    } else if (val_type != ObJsonType && !ob_is_string_type(val_type)) {
      ret = OB_ERR_INVALID_TYPE_FOR_JSON;
      LOG_WARN("input type error", K(val_type));
    } else if (OB_FAIL(ObJsonExprHelper::ensure_collation(val_type, cs_type))) {
    } else if (ob_is_json(val_type)
        && OB_FAIL(ObJsonExprHelper::is_allow_partial_update(
               expr, ctx, json_datum->get_string(), allow_partial_update))) {
    } else if (allow_partial_update) {
      if (OB_FAIL(ObJsonExprHelper::get_json_for_partial_update(
              expr, *json_arg, ctx, allocator, *json_datum, partial_document))) {
      } else if (OB_FAIL(partial_document->get_raw_binary(json_doc, &allocator))) {
      } else {
        input_type = ObJsonInType::JSON_BIN;
      }
    } else if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
                   json_arg, ctx, allocator, json_doc, is_null_result))) {
    } else {
      input_type = ObJsonExprHelper::get_json_internal_type(val_type);
    }
  }

  if (OB_UNLIKELY(OB_FAIL(ret))) {
    if (ret == OB_ERR_INVALID_TYPE_FOR_JSON) {
      LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, 1, "json_extract");
    } else if (ret == OB_ERR_INVALID_JSON_CHARSET) {
    } else {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
    }
  } else if (!is_null_result) {
    rust::Box<common::json::Query> extraction = common::json::new_query(
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(json_doc.ptr()), json_doc.length()),
        input_type != ObJsonInType::JSON_BIN,
        ObJsonExprHelper::get_json_text_depth_limit(
            ObJsonExprHelper::get_json_max_depth_config()));
    if (extraction->status() != common::json::Status::Ok) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_WARN("fail to parse JSON for extraction", K(ret), K(extraction->byte_offset()));
    }
    JsonPathCache ctx_cache;
    JsonPathCache* path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
    path_cache = ((path_cache != NULL) ? path_cache : &ctx_cache);
    for (int64_t i = 1; OB_SUCC(ret) && (!is_null_result) && i < expr.arg_cnt_; i++) {
      ObDatum *path_data = NULL;
      if (OB_FAIL(allocator.eval_arg(expr.args_[i], ctx, path_data))) {
      } else if (path_data->is_null()) {
        is_null_result = true;
      } else {
        ObString path_text = path_data->get_string();
        const common::json::Path *j_path = NULL;
        if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(expr.args_[i], ctx, allocator, path_text, is_null_result))) {
        } else if (OB_FAIL(find_and_add_native_path_cache(path_cache, j_path, path_text, i, true))) {
        } else if (extraction->add_path(*j_path) != common::json::Status::Ok) {
          ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
        }
      }
    }
    if (OB_SUCC(ret) && !is_null_result) {
      common::json::BinaryResult extracted = extraction->finish();
      if (extracted.status != common::json::Status::Ok) {
        ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
        LOG_WARN("fail to construct JSON extraction result", K(ret), K(extracted.byte_offset));
      } else if (extracted.binary.empty()) {
        res.set_null();
      } else {
        const char *data = reinterpret_cast<const char *>(extracted.binary.data());
        ObString extracted_string(extracted.binary.size(), data);
        ret = ObJsonExprHelper::pack_json_str_res(expr, ctx, res, extracted_string);
      }
    } else if (OB_SUCC(ret)) {
      res.set_null();
    }
  } else if (OB_SUCC(ret)) {
    res.set_null();
  }

  return ret;
}

int ObExprJsonExtract::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                               ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  if (rt_expr.datum_meta_.type_ == ObNullType) {
      rt_expr.eval_func_ = eval_json_extract_null;
  } else {
      rt_expr.eval_func_ = eval_json_extract;
  }
  return OB_SUCCESS;
}

}
}
