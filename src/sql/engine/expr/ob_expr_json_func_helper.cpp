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
#include "rust/config.h"
#include "rust/json.h"
#include <memory>
#include <optional>
#include <string>
#include "ob_expr_json_func_helper.h"
#include "common/json_type/json_builder.h"
#include "ob_expr_json_native_path.h"
#include "share/ob_json_access_utils.h"
#include "ob_expr_json_utils.h"
#include "sql/engine/expr/ob_expr_cast.h"
#include "sql/ob_spi.h"
#include "lib/charset/ob_charset_string_helper.h"
#include "sql/engine/expr/ob_json_param_type.h"
#include "sql/engine/expr/ob_array_expr_utils.h"
#include "lib/utility/ob_defer.h"
#include <variant>

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

ObJsonParamCacheCtx::ObJsonParamCacheCtx(common::ObIAllocator *allocator)
  : ObExprOperatorCtx(),
    path_cache_(allocator),
    native_path_cache_(std::make_unique<JsonPathCache>()),
    is_first_exec_(true),
    is_json_path_const_(false),
    json_param_()
{}

ObJsonParamCacheCtx::~ObJsonParamCacheCtx() = default;

uint32_t ObJsonExprHelper::get_json_text_depth_limit(uint32_t configured)
{
  const uint32_t limit = configured < JSON_DOCUMENT_MAX_DEPTH ? JSON_DOCUMENT_MAX_DEPTH : configured;
  return limit == UINT32_MAX ? limit : limit + 1;
}

int ObJsonExprHelper::get_json_metadata(const ObString &data, ObJsonInType input_type, bool with_depth,
                                        uint32_t max_depth_config, JsonMetadata &metadata)
{
  INIT_SUCC(ret);
  if (data.empty() || OB_ISNULL(data.ptr())) {
    ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
  } else {
    const uint8_t *input = reinterpret_cast<const uint8_t *>(data.ptr());
    rust::Slice<const uint8_t> slice(input, data.length());
    oceanbase::common::json::BinaryMetadataResult result = input_type == ObJsonInType::JSON_BIN
        ? oceanbase::common::json::inspect_binary(slice, with_depth)
        : oceanbase::common::json::inspect_text(
              slice, with_depth, get_json_text_depth_limit(max_depth_config));
    if (result.status != oceanbase::common::json::Status::Ok) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_WARN("fail to inspect binary JSON", K(ret), K(result.byte_offset));
    } else {
      metadata.type_ = static_cast<ObJsonNodeType>(result.value_type);
      metadata.field_type_ = static_cast<ObObjType>(result.opaque_field_type);
      metadata.depth_ = result.depth;
      metadata.length_ = result.length;
    }
  }
  return ret;
}

int query_native_json_path(const ObString &data, ObJsonInType input_type,
                           const common::json::Path &path, bool auto_wrap,
                           bool first_only, uint32_t max_depth_config,
                           JsonPathMatches &matches)
{
  INIT_SUCC(ret);
  if (data.empty() || OB_ISNULL(data.ptr())) {
    ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
  } else {
    const uint8_t *input = reinterpret_cast<const uint8_t *>(data.ptr());
    rust::Slice<const uint8_t> slice(input, data.length());
    common::json::PathQueryResult result = input_type == ObJsonInType::JSON_BIN
        ? common::json::query_binary(path, slice, auto_wrap, first_only)
        : common::json::query_text(
              path, slice, auto_wrap, first_only,
              ObJsonExprHelper::get_json_text_depth_limit(max_depth_config));
    if (result.status != common::json::Status::Ok) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_WARN("fail to query JSON path", K(ret), K(result.byte_offset));
    } else {
      matches.count_ = result.match_count;
      matches.single_length_ = result.single_length;
    }
  }
  return ret;
}

int get_native_json_query(const ObExpr &expr,
                          ObEvalCtx &ctx,
                          MultimodeAlloctor &allocator,
                          uint16_t index,
                          std::optional<rust::Box<common::json::Query>> &query,
                          bool &is_null,
                          bool *is_cover_by_error)
{
  INIT_SUCC(ret);
  ObDatum *json_datum = NULL;
  ObExpr *json_arg = expr.args_[index];
  ObObjType val_type = json_arg->datum_meta_.type_;
  ObCollationType cs_type = json_arg->datum_meta_.cs_type_;
  bool allow_partial_update = false;
  ObString json_doc;
  ObJsonInType input_type = ObJsonInType::JSON_TEXT;
  ObIJsonBase *partial_document = nullptr;
  DEFER(if (partial_document != nullptr) { partial_document->reset(); });
  query.reset();
  if (OB_FAIL(allocator.eval_arg(json_arg, ctx, json_datum))) {
    if (is_cover_by_error != nullptr) {
      *is_cover_by_error = false;
    }
  } else if (val_type == ObNullType || json_datum->is_null()) {
    is_null = true;
  } else if (val_type != ObJsonType && !ob_is_string_type(val_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
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
                 json_arg, ctx, allocator, json_doc, is_null))) {
  } else {
    input_type = ObJsonExprHelper::get_json_internal_type(val_type);
  }
  if (OB_SUCC(ret) && !is_null) {
    query.emplace(common::json::new_query(
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(json_doc.ptr()), json_doc.length()),
        input_type != ObJsonInType::JSON_BIN,
        ObJsonExprHelper::get_json_text_depth_limit(
            ObJsonExprHelper::get_json_max_depth_config())));
    if ((*query)->status() != common::json::Status::Ok) {
      ret = (*query)->status() == common::json::Status::Depth
          ? OB_ERR_JSON_OUT_OF_DEPTH : OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_WARN("fail to parse JSON", K(ret), K((*query)->byte_offset()));
      if (ret == OB_ERR_JSON_OUT_OF_DEPTH) {
        if (is_cover_by_error != nullptr) {
          *is_cover_by_error = false;
        }
        LOG_USER_ERROR(OB_ERR_JSON_OUT_OF_DEPTH);
      } else {
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
      }
    }
  }
  return ret;
}

int get_native_json_path(const ObExpr &expr,
                         ObEvalCtx &ctx,
                         MultimodeAlloctor &allocator,
                         ObJsonParamCacheCtx *param_ctx,
                         uint16_t index,
                         const common::json::Path *&path,
                         bool &is_null,
                         bool &is_cover_by_error)
{
  INIT_SUCC(ret);
  ObExpr *path_expr = expr.args_[index];
  ObObjType type = path_expr->datum_meta_.type_;
  ObString path_text;
  JsonPathCache *path_cache = param_ctx->get_native_path_cache();
  path = NULL;
  if (!param_ctx->is_first_exec_ && param_ctx->is_json_path_const_ && OB_NOT_NULL(path_cache)) {
    ret = path_cache->find_and_add(path_text, 0, true, path);
  } else {
    ObDatum *path_datum = NULL;
    if (OB_FAIL(allocator.eval_arg(path_expr, ctx, path_datum))) {
      is_cover_by_error = false;
    } else if (type == ObNullType || path_datum->is_null()) {
      is_null = true;
    } else if (!ob_is_string_type(type)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("input type error", K(type));
    } else {
      path_text = path_datum->get_string();
      if (OB_FAIL(ObJsonExprHelper::get_json_or_str_data(
              path_expr, ctx, allocator, path_text, is_null))) {
      } else if (path_text.length() == 0) {
        is_null = true;
      } else if (OB_FAIL(ObJsonExprHelper::convert_string_collation_type(
                     path_expr->datum_meta_.cs_type_, CS_TYPE_UTF8MB4_BIN,
                     &ctx.exec_ctx_.get_allocator(), path_text, path_text))) {
      } else if (OB_FAIL(find_and_add_native_path_cache(
                     path_cache, path, path_text, 0, true, param_ctx->is_json_path_const_))) {
        is_cover_by_error = false;
      }
    }
  }
  return ret;
}

ObJsonInType ObJsonExprHelper::get_json_internal_type(ObObjType type)
{
  return type == ObJsonType ? ObJsonInType::JSON_BIN : ObJsonInType::JSON_TEXT;
}

int ObJsonExprHelper::ensure_collation(ObObjType type, ObCollationType cs_type)
{
  INIT_SUCC(ret);
  if (ob_is_string_type(type) && (ObCharset::charset_type_by_coll(cs_type) != CHARSET_UTF8MB4)) {
    LOG_WARN("unsuport json string type with binary charset", K(type), K(cs_type));
    ret = OB_ERR_INVALID_JSON_CHARSET;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_CHARSET);
  }

  return ret;
}

int ObJsonExprHelper::get_json_or_str_data(ObExpr *expr, ObEvalCtx &ctx, 
                                           MultimodeAlloctor &allocator,
                                           ObString& str, bool& is_null) 
{
  INIT_SUCC(ret);
  ObDatum *json_datum = NULL;
  ObObjType val_type = expr->datum_meta_.type_;
  if (OB_FAIL(allocator.eval_arg(expr, ctx, json_datum))) {
  } else if (json_datum->is_null() || val_type == ObNullType) {
    is_null = true;
  } else if (!ob_is_extend(val_type)
              && !ob_is_json(val_type)
              && !ob_is_string_type(val_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("input type error", K(val_type));
  } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, allocator, *json_datum,
                expr->datum_meta_, expr->obj_meta_.has_lob_header(), str))) {
  }
  return ret;
}


int ObJsonExprHelper::get_json_schema(const ObExpr &expr, ObEvalCtx &ctx,
                                      MultimodeAlloctor &allocator,
                                      uint16_t index, ObJsonSchemaCache*& j_schema,
                                      bool &is_null)
{
  INIT_SUCC(ret);

  ObDatum *schema_datum = nullptr;
  ObExpr *schema_arg = expr.args_[index];
  ObObjType type = schema_arg->datum_meta_.type_;
  ObCollationType cs_type = schema_arg->datum_meta_.cs_type_;
  if (OB_FAIL(allocator.eval_arg(schema_arg, ctx, schema_datum))) {
  } else if (type == ObNullType || schema_datum->is_null()) {
    is_null = true;
  } else if (!ob_is_string_type(type) && type != ObJsonType) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("input type error", K(type));
  } else if (OB_FAIL(ObJsonExprHelper::ensure_collation(type, cs_type))) {
  } else {
    ObString j_str;
    if (OB_FAIL(get_json_or_str_data(schema_arg, ctx, allocator, j_str, is_null))) {
    } else if (is_null) {
    } else {
      ObJsonInType j_in_type = ObJsonExprHelper::get_json_internal_type(type);
      ObJsonSchemaCache* schema_cache = ObJsonExprHelper::get_schema_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);

      if (OB_ISNULL(schema_cache)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(ObJsonExprHelper::find_and_add_schema_cache(schema_cache, j_schema, j_str, 1, j_in_type))) {
      }
    }
  }
  return ret;
}

namespace {
int mutation_status(int32_t status, int32_t source_status, JsonMutation operation)
{
  switch (status) {
    case 0: return OB_SUCCESS;
    case 1: return OB_ALLOCATE_MEMORY_FAILED;
    case 2: return source_status;
    case 7: return operation == JsonMutation::ArrayInsert
        ? OB_ERR_INVALID_JSON_PATH_ARRAY_CELL : OB_ERR_INVALID_JSON_PATH;
    case 8: return OB_ERR_JSON_VACUOUS_PATH;
    case 9: return OB_ERR_JSON_DOCUMENT_NULL_KEY;
    default: return OB_ERR_UNEXPECTED;
  }
}

rust::Slice<const uint8_t> mutation_bytes(const ObString &bytes)
{
  static const uint8_t empty = 0;
  return rust::Slice<const uint8_t>(bytes.empty() ? &empty
      : reinterpret_cast<const uint8_t *>(bytes.ptr()), bytes.length());
}
} // namespace

int ObJsonExprHelper::push_json_merge_argument(const ObExpr &expr, ObEvalCtx &ctx,
    MultimodeAlloctor &allocator, uint16_t index, BinaryMerge &merger, bool &is_null, bool *input_error)
{
  INIT_SUCC(ret);
  ObDatum *datum = nullptr;
  ObExpr *argument = expr.args_[index];
  ObString input;
  const ObObjType type = argument->datum_meta_.type_;
  is_null = false;
  if (input_error != nullptr) { *input_error = true; }
  if (OB_FAIL(allocator.eval_arg(argument, ctx, datum))) {
  } else if (type == ObNullType || datum->is_null()) {
    is_null = true;
  } else if (type != ObJsonType && !ob_is_string_type(type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else if (OB_FAIL(ensure_collation(type, argument->datum_meta_.cs_type_))) {
  } else if (OB_FAIL(get_json_or_str_data(argument, ctx, allocator, input, is_null))) {
  }
  if (OB_SUCC(ret)) {
    const int32_t status = merger.merge_push(mutation_bytes(input), type != ObJsonType,
        is_null, get_json_text_depth_limit(get_json_max_depth_config()));
    if (input_error != nullptr) { *input_error = merger.merge_input_failed(); }
    ret = status == 0 ? OB_SUCCESS : status == 1 ? OB_ALLOCATE_MEMORY_FAILED
        : status == 9 ? OB_ERR_JSON_PATCH_INVALID : OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
    if (ret == OB_ERR_INVALID_JSON_TEXT_IN_PARAM) { LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM); }
    else if (ret == OB_ERR_JSON_PATCH_INVALID) { LOG_USER_ERROR(OB_ERR_JSON_PATCH_INVALID); }
  }
  return ret;
}

int ObJsonExprHelper::eval_json_merge(const ObExpr &expr, ObEvalCtx &ctx,
    ObDatum &res, bool patch)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard allocation_scope(ctx);
  MultimodeAlloctor allocator(allocation_scope.get_allocator());
  lib::ObMallocHookAttrGuard malloc_scope(lib::ObMemAttr("JSONModule"));
  rust::Box<BinaryMerge> merger = common::json::new_merge_accumulator(patch ? 1 : 0);
  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  }
  for (int64_t index = 0; OB_SUCC(ret) && merger->merge_needs_input() && index < expr.arg_cnt_; ++index) {
    bool is_null = false;
    ret = push_json_merge_argument(expr, ctx, allocator, index, *merger, is_null);
  }
  if (OB_SUCC(ret)) {
    common::json::MergeOutput result = common::json::finish_merge(std::move(merger));
    if (result.status != 0) {
      ret = result.status == 1 ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
    } else if (result.is_null) {
      res.set_null();
    } else {
      ObString binary(result.binary.size(), reinterpret_cast<const char *>(result.binary.data()));
      ret = pack_json_str_res(expr, ctx, res, binary);
    }
  }
  return ret;
}

int ObJsonExprHelper::eval_json_mutation(const ObExpr &expr, ObEvalCtx &ctx,
    ObDatum &res, JsonMutation operation)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard allocation_scope(ctx);
  MultimodeAlloctor allocator(allocation_scope.get_allocator());
  lib::ObMallocHookAttrGuard malloc_scope(lib::ObMemAttr("JSONModule"));
  ObIJsonBase *partial_document = nullptr;
  DEFER(if (partial_document != nullptr) { partial_document->reset(); });
  std::optional<rust::Box<common::json::SqlEditor>> editor;
  std::optional<common::json::BinarySource> source;
  ObJsonBinUpdateCtx *update = nullptr;
  ObDatum *document = nullptr;
  ObExpr *document_expr = expr.args_[0];
  bool is_null = false;
  bool allow_partial = false;
  const bool remove = operation == JsonMutation::Remove;
  const bool eager = operation == JsonMutation::Set || operation == JsonMutation::Replace;
  const bool null_type_short_circuit = eager || remove;
  if (expr.datum_meta_.cs_type_ != CS_TYPE_UTF8MB4_BIN) {
    ret = OB_ERR_INVALID_JSON_CHARSET;
  } else if (OB_FAIL(allocator.eval_arg(document_expr, ctx, document))) {
  } else if (document_expr->datum_meta_.type_ == ObNullType || document->is_null()) {
    is_null = true;
  } else if (document_expr->datum_meta_.type_ != ObJsonType
      && !ob_is_string_type(document_expr->datum_meta_.type_)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else if (OB_FAIL(ensure_collation(document_expr->datum_meta_.type_, document_expr->datum_meta_.cs_type_))) {
  } else if (ob_is_json(document_expr->datum_meta_.type_)
      && OB_FAIL(is_allow_partial_update(expr, ctx, document->get_string(), allow_partial))) {
  } else {
    ObString input;
    bool text = document_expr->datum_meta_.type_ != ObJsonType;
    if (allow_partial) {
      if (OB_FAIL(get_json_for_partial_update(expr, *document_expr, ctx, allocator, *document, partial_document))) {
      } else if (partial_document->is_bin()
          && (update = static_cast<ObJsonBin *>(partial_document)->get_update_ctx()) != nullptr) {
        source.emplace(*update->cursor_);
        editor.emplace(common::json::new_sql_editor_external(source.value()));
      } else if (OB_FAIL(partial_document->get_raw_binary(input, &allocator))) {
      } else {
        text = false;
      }
    } else if (OB_FAIL(get_json_or_str_data(document_expr, ctx, allocator, input, is_null))) {
    }
    if (OB_SUCC(ret) && !is_null && !editor.has_value()) {
      editor.emplace(common::json::new_sql_editor_owned(mutation_bytes(input), text,
          get_json_text_depth_limit(get_json_max_depth_config())));
    }
    if (OB_SUCC(ret) && !is_null && (*editor)->editor_status() != 0) {
      const int32_t status = (*editor)->editor_status();
      ret = status == 1 ? OB_ALLOCATE_MEMORY_FAILED
          : status == 2 ? (*editor)->editor_source_status() : OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      if (ret == OB_ERR_INVALID_JSON_TEXT_IN_PARAM) { LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM); }
    }
  }
  JsonPathCache local_cache;
  JsonPathCache *path_cache = get_native_path_cache_ctx(expr.expr_ctx_id_, &ctx.exec_ctx_);
  if (path_cache == nullptr) { path_cache = &local_cache; }
  for (int64_t index = 1; OB_SUCC(ret) && !is_null && index < expr.arg_cnt_; index += remove ? 1 : 2) {
    ObExpr *path_expr = expr.args_[index];
    ObDatum *path_datum = nullptr;
    const common::json::Path *path = nullptr;
    if (null_type_short_circuit && path_expr->datum_meta_.type_ == ObNullType) {
      is_null = true;
    } else if (OB_FAIL(allocator.eval_arg(path_expr, ctx, path_datum))) {
      if (remove) { ret = OB_ERR_INVALID_JSON_PATH; }
    } else if (path_datum->is_null()) {
      if (null_type_short_circuit) { ret = OB_ERR_INVALID_JSON_PATH; LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH); }
      else { is_null = true; }
    } else {
      ObString path_text = path_datum->get_string();
      if (OB_FAIL(get_json_or_str_data(path_expr, ctx, allocator, path_text, is_null))) {
      } else if (OB_FAIL(find_and_add_native_path_cache(path_cache, path, path_text, index, false))) {
        if (operation == JsonMutation::Replace) {
          ret = OB_ERR_INVALID_JSON_PATH;
          LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH);
        }
      } else {
        rust::Box<common::json::PreparedMutation> prepared = source.has_value()
            ? (*editor)->prepare_external(source.value(), *path, static_cast<uint8_t>(operation))
            : (*editor)->prepare_owned(*path, static_cast<uint8_t>(operation));
        if (OB_FAIL(mutation_status(prepared->prepared_status(), prepared->prepared_source_status(), operation))) {
          if (ret == OB_ERR_JSON_VACUOUS_PATH) { LOG_USER_ERROR(OB_ERR_JSON_VACUOUS_PATH); }
        } else {
          ObString binary;
          if (prepared->prepared_needs_value()
              && OB_FAIL(get_json_binary(expr, ctx, allocator, index + 1, binary))) {
            if (!eager) { ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM; }
          }
          if (OB_SUCC(ret)) {
            const int32_t status = source.has_value()
                ? (*editor)->apply_external(source.value(), std::move(prepared), mutation_bytes(binary))
                : (*editor)->apply_owned(std::move(prepared), mutation_bytes(binary));
            ret = mutation_status(status, (*editor)->editor_source_status(), operation);
          }
        }
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (is_null) {
    res.set_null();
  } else {
    rust::Box<BinaryEdit> edit = source.has_value()
        ? common::json::finish_external(std::move(editor.value()), source.value(), update->is_rebuild_all(), update->is_no_update())
        : common::json::finish_owned(std::move(editor.value()));
    if (OB_FAIL(mutation_status(edit->edit_status(), edit->edit_source_status(), operation))) {
    } else if (!edit->edit_is_partial()) {
      const rust::Slice<const uint8_t> binary = edit->edit_binary();
      const ObString data(binary.size(), reinterpret_cast<const char *>(binary.data()));
      ret = pack_json_str_res(expr, ctx, res, data);
    } else if (OB_ISNULL(update)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(update->merge_edit(*edit))) {
    } else {
      for (size_t index = 0; OB_SUCC(ret) && index < edit->edit_write_count(); ++index) {
        const rust::Slice<const uint8_t> bytes = edit->edit_write_bytes(index);
        const uint64_t offset = edit->edit_write_offset(index);
        if (offset == update->cursor_->get_length()) {
          ret = update->cursor_->append(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        } else {
          ret = update->cursor_->set(offset, reinterpret_cast<const char *>(bytes.data()), bytes.size());
        }
      }
      if (OB_SUCC(ret)) { ret = pack_json_diff_res(expr, ctx, allocator, partial_document, res); }
    }
  }
  return ret;
}

int ObJsonExprHelper::get_json_doc(const ObExpr &expr, ObEvalCtx &ctx,
                                    MultimodeAlloctor &allocator,
                                    uint16_t index, ObIJsonBase*& j_base,
                                    bool &is_null, bool preserve_dup)
{
  INIT_SUCC(ret);
  ObDatum *json_datum = NULL;
  ObExpr *json_arg = expr.args_[index];
  ObObjType val_type = json_arg->datum_meta_.type_;
  ObCollationType cs_type = json_arg->datum_meta_.cs_type_;
  bool allow_partial_update = false;

  if (OB_UNLIKELY(OB_FAIL(allocator.eval_arg(json_arg, ctx, json_datum)))) {
  } else if (val_type == ObNullType || json_datum->is_null()) {
    is_null = true;
  } else if (val_type != ObJsonType && !ob_is_string_type(val_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
    LOG_WARN("input type error", K(val_type));
  } else if (OB_FAIL(ObJsonExprHelper::ensure_collation(val_type, cs_type))) {
  } else if (ob_is_json(val_type)
      && OB_FAIL(ObJsonExprHelper::is_allow_partial_update(expr, ctx, json_datum->get_string(), allow_partial_update))) {
  } else if (allow_partial_update) {
    if (OB_FAIL(get_json_for_partial_update(expr, *json_arg, ctx, allocator, *json_datum, j_base))) {
    }
  } else {
    ObString j_str;
    if (OB_FAIL(get_json_or_str_data(json_arg, ctx, allocator, j_str, is_null))) {
    } else if (is_null) {
    } else {
      ObJsonInType j_in_type = ObJsonExprHelper::get_json_internal_type(val_type);

      uint32_t parse_flag = ObJsonParser::JSN_DEFAULT_FLAG;
      ADD_FLAG_IF_NEED(preserve_dup, parse_flag, ObJsonParser::JSN_PRESERVE_DUP_FLAG);
      if (OB_FAIL(ObJsonBaseFactory::get_json_base(&allocator, j_str, j_in_type, j_base, parse_flag))) {
        ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
      }
    }
  }
  return ret;
}

int ObJsonExprHelper::get_const_json_schema(const common::ObObj &data,
                                            const char *func_name,
                                            ObExecContext &exec_ctx,
                                            common::ObIAllocator *allocator,
                                            ObString &schema)
{
  INIT_SUCC(ret);
  ObObjType val_type = data.get_type();
  ObCollationType cs_type = data.get_collation_type();
  const common::ObLobReadOptions *lob_options = nullptr;
  if (OB_FAIL(exec_ctx.get_lob_read_options(lob_options))) {
  } else if (!ObJsonExprHelper::is_convertible_to_json(val_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, 1, func_name);
  } else if (val_type != ObJsonType && OB_FAIL(ObJsonExprHelper::ensure_collation(val_type, cs_type))) {
  } else {
    ObString input = data.get_string();
    if (OB_FAIL(common::lob_helper::read_real_string_data(
            allocator, val_type, cs_type, data.has_lob_header(), input, lob_options))) {
    } else if (OB_FAIL(compile_json_schema(*allocator, input,
                   ObJsonExprHelper::get_json_internal_type(val_type),
                   get_json_text_depth_limit(get_json_max_depth_config()), schema))) {
      if (ret == OB_ERR_NULL_VALUE || ret == OB_ERR_INVALID_JSON_TEXT) {
        ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
        LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
      }
    }
  }
  return ret;
}

int ObJsonExprHelper::get_partial_json_bin(
    ObIAllocator &allocator,
    data_plane::ObJsonLobHandle &lob_handle,
    ObJsonBinUpdateCtx *update_ctx, 
    ObIJsonBase *&j_base)
{
  INIT_SUCC(ret);
  ObJsonBinCtx *bin_ctx = nullptr;
  ObJsonBin *j_bin = nullptr;
  bool update_context_allocated = false;
  if (OB_ISNULL(update_ctx)) {
    if (OB_ISNULL(update_ctx =
            OB_NEWx(ObJsonBinUpdateCtx, &allocator, &allocator))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else {
      update_context_allocated = true;
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(update_ctx->init_status())) {
  } else if (OB_FAIL(data_plane::bind_json_lob(lob_handle, *update_ctx))) {
  } else if (OB_ISNULL(bin_ctx = OB_NEWx(ObJsonBinCtx, &allocator))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FALSE_IT(bin_ctx->update_ctx_ = update_ctx)) {
  } else if (OB_FALSE_IT(bin_ctx->is_update_ctx_alloc_ = true)) {
  } else if (OB_ISNULL(j_bin = OB_NEWx(ObJsonBin, &allocator, &allocator, bin_ctx, true))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(j_bin->reset_iter())) {
  } else {
    j_base = j_bin;
  }
  if (OB_FAIL(ret) && update_context_allocated && OB_NOT_NULL(update_ctx)) {
    update_ctx->~ObJsonBinUpdateCtx();
    update_ctx = nullptr;
  }
  return ret;
}

int ObJsonExprHelper::get_json_for_partial_update(
    const ObExpr &expr,
    const ObExpr &json_expr,
    ObEvalCtx &ctx,
    ObIAllocator &allocator,
    ObDatum &json_datum,
    ObIJsonBase *&j_base)
{
  INIT_SUCC(ret);
  ObString lob_str = json_datum.get_string();
  ObLobLocatorV2 locator(lob_str);
  data_plane::ObJsonLobHandle *lob_handle = nullptr;
  int64_t query_timeout_ts = ObTimeUtility::current_time() + 60 * USECS_PER_SEC;
  uint8_t root_type = 0;
  if (lob_str.empty()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(get_session_query_timeout_ts(ctx, query_timeout_ts))) {
  } else if (locator.is_delta_temp_lob()) {
    ObJsonDeltaLob delta_lob;
    if (OB_FAIL(delta_lob.init(&allocator, locator, query_timeout_ts))) {
    } else {
      j_base = delta_lob.get_json_bin();
    }
    if (OB_FAIL(ret)) {
      delta_lob.reset();
    }
  } else if (! locator.is_persist_lob() || locator.is_inrow()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(data_plane::open_json_lob(
                 allocator, locator, query_timeout_ts, lob_handle))) {
  } else if (OB_FAIL(data_plane::read_json_lob_root_type(
                 *lob_handle, root_type))) {
  } else if (! common::json::is_document_header(root_type)) {
    // if root type not doc header, means that old json data.
    // old json is not binary charset in lob, so can not use partial lob
    ObString j_str;
    bool is_single_chunk = false;
    if (OB_FAIL(data_plane::try_get_single_chunk_json_lob(
            *lob_handle, is_single_chunk, j_str))) {
    } else if (is_single_chunk) {
    } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(ctx.exec_ctx_, allocator, json_datum,
                json_expr.datum_meta_, json_expr.obj_meta_.has_lob_header(), j_str))) {
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(
          &allocator, j_str, ObJsonInType::JSON_BIN, j_base, 0, ObJsonExprHelper::get_json_max_depth_config()))) {
    }
  } else if (OB_FAIL(get_partial_json_bin(allocator, *lob_handle, nullptr, j_base))) {
  } else if (OB_ISNULL(j_base)) {
    ret = OB_ERR_UNEXPECTED;
  }

  if (OB_FAIL(ret)) {
    if (OB_NOT_NULL(j_base)) {
      j_base->reset();
      j_base = nullptr;
    }
  }
  data_plane::destroy_json_lob_handle(lob_handle);
  return ret;
}

int ObJsonExprHelper::refine_range_json_value_const(
  const common::ObObj &data, ObExecContext &ctx,
  bool is_bool, common::ObIAllocator *allocator,
  ObIJsonBase*& j_base)
{
  UNUSED(is_bool);
  int ret = OB_SUCCESS;
  ObObjType type = data.get_type();
  const common::ObLobReadOptions *lob_options = nullptr;
  ObString input;
  if (!is_convertible_to_json(type)) {
    ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
  } else if (OB_FAIL(ctx.get_lob_read_options(lob_options))) {
  } else if (data.is_null()) {
    auto encoded = common::json::encode_scalar(0, 0, 0, 0, rust::Slice<const uint8_t>(), 0);
    ret = encoded.status == 1 ? OB_ALLOCATE_MEMORY_FAILED : encoded.status == 0
        ? copy_json_binary(*allocator, ObString(encoded.binary.size(),
              reinterpret_cast<char *>(encoded.binary.data())), j_base) : OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ensure_collation(type, data.get_collation_type()))) {
  } else if (FALSE_IT(input = data.get_string())) {
  } else if (OB_FAIL(common::lob_helper::read_real_string_data(allocator, type,
        data.get_collation_type(), data.has_lob_header(), input, lob_options))) {
  } else if (OB_FAIL(ObJsonBaseFactory::get_json_base(allocator, input,
        get_json_internal_type(type), j_base,
        ObJsonParser::JSN_SCHEMA_FLAG, get_json_max_depth_config()))) {
    if (type == ObJsonType && ret != OB_ERR_UNSUPPROTED_REF_IN_JSON_SCHEMA) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
    }
  }
  return ret;
}




int ObJsonExprHelper::cast_to_res(ObIAllocator &allocator, 
                                  ObDatum &src_datum, 
                                  const ObExpr &expr, 
                                  const ObExpr &default_expr, 
                                  ObEvalCtx &ctx, 
                                  ObDatum &res, 
                                  bool xt_need_acc_check)
{
  int ret = OB_SUCCESS;
  ObCastMode def_cm = CM_NONE;
  ObSQLSessionInfo *session = NULL;
  ObObj dst_obj, buf_obj, src_obj;
  const ObObj *res_obj = NULL;
  ObAccuracy out_acc;
  if (src_datum.is_null()) {
    res.set_null();
  } else if (OB_FAIL(src_datum.to_obj(src_obj, default_expr.obj_meta_, default_expr.obj_datum_map_))) {
  } else {
    // to type
    if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObSQLUtils::get_default_cast_mode(session->get_stmt_type(),
                                                  session, def_cm))) {
    } else {
      ObObjType obj_type = expr.datum_meta_.type_;
      ObCollationType cs_type = expr.datum_meta_.cs_type_;
      ObPhysicalPlanCtx *phy_plan_ctx = ctx.exec_ctx_.get_physical_plan_ctx();
      const ObDataTypeCastParams dtc_params = ObBasicSessionInfo::create_dtc_params(session);
      ObCastCtx cast_ctx(&allocator, &dtc_params, get_cur_time(phy_plan_ctx), def_cm,
                         cs_type, NULL, NULL);
      session->configure_obj_cast(
          cast_ctx,
          ctx.exec_ctx_.get_srs_provider(),
          ctx.exec_ctx_.get_lob_read_service());
      if (OB_FAIL(ObObjCaster::to_type(obj_type, cs_type, cast_ctx, src_obj, dst_obj))) {
      } else if (FALSE_IT(get_accuracy_from_expr(expr, out_acc))) {
      } else if (FALSE_IT(res_obj = &dst_obj)) {
      } else if (OB_FAIL(obj_accuracy_check(cast_ctx, out_acc, cs_type, dst_obj, buf_obj, res_obj))) {
        if (!xt_need_acc_check && (ob_is_varchar_or_char(obj_type, cs_type)) && ret == OB_ERR_DATA_TOO_LONG) {
          ObLengthSemantics ls = LS_CHAR;
          const char* str = dst_obj.get_string_ptr();
          int32_t str_len_byte = dst_obj.get_string_len();
          int64_t char_len = 0;
          int32_t trunc_len_byte = 0;
          trunc_len_byte = (ls == LS_BYTE ?
                ObCharset::max_bytes_charpos(cs_type, str, str_len_byte,
                                             expr.max_length_, char_len):
                ObCharset::charpos(cs_type, str, str_len_byte, expr.max_length_));
          if (trunc_len_byte == 0) {
            (const_cast<ObObj*>(res_obj))->set_null();
          } else {
            (const_cast<ObObj*>(res_obj))->set_common_value(ObString(trunc_len_byte, str));
          }
          ret = OB_SUCCESS;
        } else {
        }
      } else if (OB_FAIL(ObSPIService::spi_pad_char_or_varchar(session, obj_type, out_acc, &allocator, const_cast<ObObj *>(res_obj)))) {
      }

      if (OB_SUCC(ret)) {
        if (OB_NOT_NULL(res_obj)) {
          res.from_obj(*res_obj);
          ObExprStrResAlloc res_alloc(expr, ctx);
          if (OB_FAIL(res.deep_copy(res, res_alloc))) {
          }
        } else {
          ret = OB_ERR_UNEXPECTED;
        }
      }
    }
  }
  return ret;
}

void ObJsonExprHelper::get_accuracy_from_expr(const ObExpr &expr, ObAccuracy &accuracy)
{
  accuracy.set_length(expr.max_length_);
  accuracy.set_scale(expr.datum_meta_.scale_);
  const ObObjTypeClass &dst_tc = ob_obj_type_class(expr.datum_meta_.type_);
  if (ObStringTC == dst_tc || ObTextTC == dst_tc) {
    accuracy.set_length_semantics(expr.datum_meta_.length_semantics_);
  } else {
    accuracy.set_precision(expr.datum_meta_.precision_);
  }
}

namespace {
struct ConvertedJson {
  std::variant<ObString, rust::Vec<uint8_t>> bytes;

  int get_binary(ObIAllocator &allocator, ObString &binary) const
  {
    if (const auto *owned = std::get_if<rust::Vec<uint8_t>>(&bytes)) {
      return deep_copy_ob_string(allocator, ObString(owned->size(),
          reinterpret_cast<const char *>(owned->data())), binary);
    }
    binary = std::get<ObString>(bytes);
    return OB_SUCCESS;
  }

  int append(JsonContainerBuilder &builder, const ObString &key)
  {
    if (auto *owned = std::get_if<rust::Vec<uint8_t>>(&bytes)) {
      common::json::BuildResult encoded{0, std::move(*owned), 0, 0};
      return builder.append_encoded(key, std::move(encoded));
    }
    return builder.append(key, std::get<ObString>(bytes));
  }
};
}

static int convert_datum_to_json(const ObDatum &datum, const ObObjMeta &meta,
    ObScale scale, bool is_bool, bool format_json, bool strict, bool oracle,
    ObIAllocator &allocator, ObBasicSessionInfo *session,
    const common::ObLobReadOptions *lob_options, ConvertedJson &converted)
{
  int ret = OB_SUCCESS;
  ObObjType type = meta.get_type();
  ObCollationType cs_type = meta.get_collation_type();
  uint8_t kind = 0;
  int64_t integer = 0;
  uint64_t unsigned_integer = 0;
  double real = 0;
  uint16_t field_type = 0;
  ObString payload;
  ObJsonBuffer database_payload(&allocator);
  bool has_binary = false;
  if (datum.is_null() || type == ObNullType) {
  } else if (is_bool) {
    kind = 8;
    integer = datum.get_bool();
  } else if (ObJsonExprHelper::is_convertible_to_json(type)) {
    payload = datum.get_string();
    if (!oracle && type != ObJsonType && OB_FAIL(ObJsonExprHelper::ensure_collation(type, cs_type))) {
    } else if (OB_FAIL(common::lob_helper::read_real_string_data(
        &allocator, type, cs_type, meta.has_lob_header(), payload, lob_options))) {
    } else if (oracle && type != ObJsonType && cs_type != CS_TYPE_INVALID && cs_type != CS_TYPE_BINARY
        && OB_FAIL(ObExprUtil::convert_string_collation(payload, cs_type, payload, CS_TYPE_UTF8MB4_BIN, allocator))) {
    } else if (type == ObJsonType) {
      converted.bytes.emplace<ObString>(payload);
      has_binary = true;
    } else if (format_json) {
      auto parsed = common::json::parse_binary(
          rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(payload.ptr()), payload.length()),
          common::json::Duplicate::Overwrite, ObJsonExprHelper::get_json_text_depth_limit(ObJsonExprHelper::get_json_max_depth_config()), false);
      if (parsed.status == common::json::Status::Ok) {
        converted.bytes.emplace<rust::Vec<uint8_t>>(std::move(parsed.binary));
        has_binary = true;
      } else if (strict) {
        ret = oracle ? OB_ERR_JSON_SYNTAX_ERROR : OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      } else { kind = 5; }
    } else { kind = 5; }
  } else if (oracle && format_json) {
    ret = OB_ERR_INVALID_TYPE_FOR_OP;
  } else {
    switch (type) {
      case ObTinyIntType: case ObSmallIntType: case ObMediumIntType: case ObInt32Type: case ObIntType:
        kind = 2; integer = datum.get_int(); break;
      case ObUTinyIntType: case ObUSmallIntType: case ObUMediumIntType: case ObUInt32Type: case ObUInt64Type:
        kind = 3; unsigned_integer = datum.get_uint64(); break;
      case ObYearType:
        kind = 2; ret = ObTimeConverter::year_to_int(datum.get_year(), integer); break;
      case ObFloatType: case ObUFloatType: case ObDoubleType: case ObUDoubleType:
        real = type == ObFloatType || type == ObUFloatType ? datum.get_float() : datum.get_double();
        if (isnan(real)) { kind = 5; payload = ObString("Nan"); }
        else if (isinf(real)) { kind = 5; payload = real > 0 ? ObString("Inf") : ObString("-Inf"); }
        else { kind = 4; }
        break;
      case ObDecimalIntType: case ObUNumberType: case ObNumberType: {
        kind = 1;
        number::ObNumber number;
        if (ob_is_decimal_int(type)) {
          ret = wide::to_number(datum.get_decimal_int(), datum.get_int_bytes(), scale, allocator, number);
        } else { ret = number.deep_copy_v3(datum.get_number(), allocator); }
        int64_t pos = 0;
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(database_payload.reserve(number.get_serialize_size() + 4))) {
        } else if (OB_FAIL(serialization::encode_i16(database_payload.ptr(), database_payload.capacity(), pos, -1))) {
        } else if (OB_FAIL(serialization::encode_i16(database_payload.ptr(), database_payload.capacity(), pos, scale))) {
        } else if (OB_FAIL(number.serialize(database_payload.ptr(), database_payload.capacity(), pos))) {
        } else if (OB_FAIL(database_payload.set_length(pos))) {
        } else { payload = database_payload.string(); }
        break;
      }
      case ObDateType: case ObMySQLDateType: case ObTimeType: case ObDateTimeType:
      case ObMySQLDateTimeType: case ObTimestampType: {
        ObTime time;
        int64_t value = 0;
        if (type == ObDateType) {
          kind = 9; time.mode_ = DT_TYPE_DATE;
          ret = ObTimeConverter::date_to_ob_time(datum.get_date(), time);
          int32_t date = ObTimeConverter::ob_time_to_date(time);
          if (OB_SUCC(ret)) { ret = database_payload.append(reinterpret_cast<const char *>(&date), sizeof(date)); }
        } else if (type == ObMySQLDateType) {
          kind = 32; time.mode_ = DT_TYPE_DATE;
          ret = ObTimeConverter::mdate_to_ob_time(datum.get_date(), time);
          ObMySQLDate date = ObTimeConverter::ob_time_to_mdate(time);
          if (OB_SUCC(ret)) { ret = database_payload.append(reinterpret_cast<const char *>(&date), sizeof(date)); }
        } else {
          if (type == ObTimeType) {
            kind = 10; time.mode_ = DT_TYPE_TIME;
            ret = ObTimeConverter::time_to_ob_time(datum.get_time(), time);
            if (OB_SUCC(ret)) { value = ObTimeConverter::ob_time_to_time(time); }
          } else if (type == ObMySQLDateTimeType) {
            kind = 33; time.mode_ = DT_TYPE_MYSQL_DATETIME;
            ret = ObTimeConverter::mdatetime_to_ob_time(datum.get_datetime(), time);
            ObMySQLDateTime datetime;
            if (OB_SUCC(ret)) { ret = ObTimeConverter::ob_time_to_mdatetime(time, datetime); }
            if (OB_SUCC(ret)) { MEMCPY(&value, &datetime, sizeof(value)); }
          } else {
            kind = type == ObDateTimeType ? 11 : 12;
            time.mode_ = DT_TYPE_DATETIME;
            const ObTimeZoneInfo *timezone = kind == 12 && session != nullptr ? session->get_timezone_info() : nullptr;
            ret = ObTimeConverter::datetime_to_ob_time(kind == 12 ? datum.get_timestamp() : datum.get_datetime(), timezone, time);
            ObTimeConvertCtx context(nullptr, false);
            if (OB_SUCC(ret)) { ret = ObTimeConverter::ob_time_to_datetime(time, context, value); }
          }
          if (OB_SUCC(ret)) { ret = database_payload.append(reinterpret_cast<const char *>(&value), sizeof(value)); }
        }
        payload = database_payload.string();
        break;
      }
      case ObHexStringType:
        kind = 13; field_type = static_cast<uint16_t>(type); payload = datum.get_string(); break;
      case ObBitType: {
        kind = 13; field_type = static_cast<uint16_t>(type);
        const int32_t capacity = (OB_MAX_BIT_LENGTH + 7) / 8;
        char *buffer = static_cast<char *>(allocator.alloc(capacity));
        int64_t pos = 0;
        if (buffer == nullptr) { ret = OB_ALLOCATE_MEMORY_FAILED; }
        else if (OB_FAIL(bit_to_char_array(datum.get_uint64(), scale, buffer, capacity, pos))) {
        } else { payload.assign_ptr(buffer, pos); }
        break;
      }
      case ObGeometryType: ret = OB_ERR_INVALID_JSON_TEXT; break;
      case ObUserDefinedSQLType: ret = OB_ERR_INVALID_CAST_TO_JSON; break;
      default: ret = OB_INVALID_ARGUMENT; break;
    }
  }
  if (OB_SUCC(ret) && !has_binary) {
    auto encoded = common::json::encode_scalar(kind, integer, unsigned_integer, real,
        rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(payload.ptr()), payload.length()), field_type);
    if (encoded.status != 0) {
      ret = encoded.status == 1 ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
    } else {
      converted.bytes.emplace<rust::Vec<uint8_t>>(std::move(encoded.binary));
    }
  }
  return ret;
}

int ObJsonExprHelper::datum_to_json_binary(const ObDatum &datum, const ObObjMeta &meta,
    ObScale scale, bool is_bool, bool format_json, bool strict, bool oracle,
    ObIAllocator &allocator, ObBasicSessionInfo *session,
    const common::ObLobReadOptions *lob_options, ObString &binary)
{
  ConvertedJson converted;
  const int ret = convert_datum_to_json(datum, meta, scale, is_bool, format_json, strict,
      oracle, allocator, session, lob_options, converted);
  return ret != OB_SUCCESS ? ret : converted.get_binary(allocator, binary);
}

static int get_converted_json_value(const ObExpr &expr, ObEvalCtx &ctx,
    MultimodeAlloctor &allocator, uint16_t index, ConvertedJson &converted)
{
  int ret = OB_SUCCESS;
  ObExpr *arg = expr.args_[index];
  ObDatum *datum = nullptr;
  const common::ObLobReadOptions *lob_options = nullptr;
  if (OB_FAIL(ctx.exec_ctx_.get_lob_read_options(lob_options))) {
  } else if (OB_FAIL(allocator.eval_arg(arg, ctx, datum))) {
  } else if (arg->datum_meta_.type_ == ObCollectionSQLType && !datum->is_null()) {
    ObString text;
    if (OB_FAIL(ObArrayExprUtils::convert_to_string(allocator, ctx, arg->obj_meta_.get_subschema_id(), datum->get_string(), text))) {
    } else {
      auto encoded = common::json::encode_scalar(5, 0, 0, 0,
          rust::Slice<const uint8_t>(reinterpret_cast<const uint8_t *>(text.ptr()), text.length()), 0);
      if (encoded.status != 0) { ret = OB_ERR_UNEXPECTED; }
      else { converted.bytes.emplace<rust::Vec<uint8_t>>(std::move(encoded.binary)); }
    }
  } else {
    ObScale scale = arg->datum_meta_.type_ == ObBitType ? arg->datum_meta_.length_semantics_ : arg->datum_meta_.scale_;
    ret = convert_datum_to_json(*datum, arg->obj_meta_, scale, arg->is_boolean_, false, false, false,
        allocator, ctx.exec_ctx_.get_my_session(), lob_options, converted);
  }
  return ret;
}

int ObJsonExprHelper::get_json_binary(const ObExpr &expr, ObEvalCtx &ctx,
    MultimodeAlloctor &allocator, uint16_t index, ObString &binary)
{
  ConvertedJson converted;
  const int ret = get_converted_json_value(expr, ctx, allocator, index, converted);
  return ret != OB_SUCCESS ? ret : converted.get_binary(allocator, binary);
}

int ObJsonExprHelper::append_json_value(const ObExpr &expr, ObEvalCtx &ctx,
    MultimodeAlloctor &allocator, uint16_t index, JsonContainerBuilder &builder,
    const ObString &key, bool &input_error)
{
  ConvertedJson converted;
  input_error = true;
  const int ret = get_converted_json_value(expr, ctx, allocator, index, converted);
  if (ret != OB_SUCCESS) { return ret; }
  input_error = false;
  return converted.append(builder, key);
}






int ObJsonExprHelper::convert_string_collation_type(ObCollationType in_cs_type, 
                                                    ObCollationType dst_cs_type,
                                                    ObIAllocator* allocator,
                                                    ObString &in_str,
                                                    ObString &out_str)
{
  INIT_SUCC(ret);

  bool is_need_convert = ((CS_TYPE_BINARY == dst_cs_type)
          || (ObCharset::charset_type_by_coll(in_cs_type) != ObCharset::charset_type_by_coll(dst_cs_type)));

  if (is_need_convert) {
    if (CS_TYPE_BINARY != in_cs_type && CS_TYPE_BINARY != dst_cs_type
        && (ObCharset::charset_type_by_coll(in_cs_type) != ObCharset::charset_type_by_coll(dst_cs_type))) {
      char *buf = nullptr;
      int64_t buf_len = (in_str.length() == 0 ? 1 : in_str.length()) * ObCharset::CharConvertFactorNum;
      uint32_t result_len = 0;
      
      if (OB_ISNULL(buf = reinterpret_cast<char*>(allocator->alloc(buf_len)))) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(ObCharset::charset_convert(in_cs_type,
                                                    in_str.ptr(),
                                                    in_str.length(),
                                                    dst_cs_type, 
                                                    buf,
                                                    buf_len, 
                                                    result_len))) {
      } else {
        out_str.assign_ptr(buf, result_len);
      }
    } else {
      if (CS_TYPE_BINARY == in_cs_type || CS_TYPE_BINARY == dst_cs_type) {
        // just copy string when in_cs_type or out_cs_type is binary
        const ObCharsetInfo *cs = NULL;
        int64_t align_offset = 0;
        if (CS_TYPE_BINARY == in_cs_type && (NULL != (cs = ObCharset::get_charset(dst_cs_type)))) {
          if (cs->mbminlen > 0 && in_str.length() % cs->mbminlen != 0) {
            align_offset = cs->mbminlen - in_str.length() % cs->mbminlen;
          }
        }
        int64_t len = align_offset + in_str.length();
        char *buf = nullptr;

        if (OB_ISNULL(buf = static_cast<char*>(allocator->alloc(len)))) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } else {
          MEMMOVE(buf + align_offset, in_str.ptr(), len - align_offset);
          MEMSET(buf, 0, align_offset);
          out_str.assign_ptr(buf, len);
        }
      } else {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  } else {
    out_str = in_str;
  }

  return ret;
}



// get json expr param cache context, if not exists cache context do nothing
ObJsonParamCacheCtx* ObJsonExprHelper::get_param_cache_ctx(const uint64_t& id, ObExecContext *exec_ctx)
{
  INIT_SUCC(ret);
  ObJsonParamCacheCtx* cache_ctx = NULL;
  if (ObExpr::INVALID_EXP_CTX_ID != id) {
    cache_ctx = static_cast<ObJsonParamCacheCtx*>(exec_ctx->get_expr_op_ctx(id));
    if (OB_ISNULL(cache_ctx)) {
      // if pathcache not exist, create one
      void *cache_ctx_buf = NULL;
      ret = exec_ctx->create_expr_op_ctx(id, sizeof(ObJsonParamCacheCtx), cache_ctx_buf);
      if (OB_SUCC(ret) && OB_NOT_NULL(cache_ctx_buf)) {
        cache_ctx = new (cache_ctx_buf) ObJsonParamCacheCtx(&exec_ctx->get_allocator());
      }
    }
  }
  return (cache_ctx == NULL) ? NULL : cache_ctx;
}

// get json expr path cache context, if not exists cache context do nothing
ObJsonPathCache* ObJsonExprHelper::get_path_cache_ctx(const uint64_t& id, ObExecContext *exec_ctx)
{
  INIT_SUCC(ret);
  ObJsonPathCacheCtx* cache_ctx = NULL;
  if (ObExpr::INVALID_EXP_CTX_ID != id) {
    cache_ctx = static_cast<ObJsonPathCacheCtx*>(exec_ctx->get_expr_op_ctx(id));
    if (OB_ISNULL(cache_ctx)) {
      // if pathcache not exist, create one
      void *cache_ctx_buf = NULL;
      ret = exec_ctx->create_expr_op_ctx(id, sizeof(ObJsonPathCacheCtx), cache_ctx_buf);
      if (OB_SUCC(ret) && OB_NOT_NULL(cache_ctx_buf)) {
        cache_ctx = new (cache_ctx_buf) ObJsonPathCacheCtx(&exec_ctx->get_allocator());
      }
    }
  }
  return (cache_ctx == NULL) ? NULL : cache_ctx->get_path_cache();
}

namespace
{
class ObJsonNativePathCacheCtx : public ObExprOperatorCtx
{
public:
  ObJsonNativePathCacheCtx() : ObExprOperatorCtx(), path_cache_() {}
  virtual ~ObJsonNativePathCacheCtx() {}
  JsonPathCache *get_path_cache() { return &path_cache_; }
private:
  JsonPathCache path_cache_;
};
}

JsonPathCache* get_native_path_cache_ctx(const uint64_t& id, ObExecContext *exec_ctx)
{
  INIT_SUCC(ret);
  ObJsonNativePathCacheCtx* cache_ctx = NULL;
  if (ObExpr::INVALID_EXP_CTX_ID != id) {
    cache_ctx = static_cast<ObJsonNativePathCacheCtx*>(exec_ctx->get_expr_op_ctx(id));
    if (OB_ISNULL(cache_ctx)) {
      void *cache_ctx_buf = NULL;
      ret = exec_ctx->create_expr_op_ctx(id, sizeof(ObJsonNativePathCacheCtx), cache_ctx_buf);
      if (OB_SUCC(ret) && OB_NOT_NULL(cache_ctx_buf)) {
        cache_ctx = new (cache_ctx_buf) ObJsonNativePathCacheCtx();
      }
    }
  }
  return (cache_ctx == NULL) ? NULL : cache_ctx->get_path_cache();
}

// get json expr schema cache context, if not exists cache context do nothing
ObJsonSchemaCache* ObJsonExprHelper::get_schema_cache_ctx(const uint64_t& id, ObExecContext *exec_ctx)
{
  INIT_SUCC(ret);
  ObJsonSchemaCacheCtx* cache_ctx = NULL;
  if (ObExpr::INVALID_EXP_CTX_ID != id) {
    cache_ctx = static_cast< ObJsonSchemaCacheCtx*>(exec_ctx->get_expr_op_ctx(id));
    if (OB_ISNULL(cache_ctx)) {
      // if pathcache not exist, create one
      void *cache_ctx_buf = NULL;
      ret = exec_ctx->create_expr_op_ctx(id, sizeof( ObJsonSchemaCacheCtx), cache_ctx_buf);
      if (OB_SUCC(ret) && OB_NOT_NULL(cache_ctx_buf)) {
        cache_ctx = new (cache_ctx_buf)  ObJsonSchemaCacheCtx(&exec_ctx->get_allocator());
      }
    }
  }
  return (cache_ctx == NULL) ? NULL : cache_ctx->get_schema_cache();
}

int ObJsonExprHelper::find_and_add_schema_cache(ObJsonSchemaCache* schema_cache, ObJsonSchemaCache*& res_schema,
                                      ObString& schema_str, int arg_idx, const ObJsonInType& in_type)
{
  INIT_SUCC(ret);
  if (OB_FAIL(schema_cache->prepare(schema_str, arg_idx, in_type))) {
    if (ret == OB_ERR_INVALID_JSON_TEXT) {
      ret = OB_ERR_INVALID_JSON_TEXT_IN_PARAM;
      LOG_USER_ERROR(OB_ERR_INVALID_JSON_TEXT_IN_PARAM);
    }
  }
  if (OB_SUCC(ret)) { res_schema = schema_cache; }
  return ret;
}

int ObJsonExprHelper::find_and_add_cache(ObJsonPathCache* path_cache, ObJsonPath*& res_path,
                                         ObString& path_str, int arg_idx, bool enable_wildcard, 
                                         bool is_const)
{
  INIT_SUCC(ret);
  if (OB_FAIL(path_cache->find_and_add_cache(res_path, path_str, arg_idx, is_const))) {
    ret = OB_ERR_INVALID_JSON_PATH;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH);
  } else if (!enable_wildcard && res_path->can_match_many()) {
    ret = OB_ERR_INVALID_JSON_PATH_WILDCARD;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH_WILDCARD);
  }
  return ret;
}

int JsonPathCache::find_and_add(ObString &path_text, int64_t arg_idx, bool is_const,
                                const common::json::Path *&path)
{
  INIT_SUCC(ret);
  if (arg_idx < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    const size_t index = static_cast<size_t>(arg_idx);
    bool found = false;
    if (index < entries_.size() && entries_[index].has_value()) {
      const Entry &entry = entries_[index].value();
      found = is_const || (entry.text_.size() == static_cast<size_t>(path_text.length())
          && (path_text.length() == 0
              || MEMCMP(entry.text_.data(), path_text.ptr(), path_text.length()) == 0));
      if (found) {
        path = &entry.parsed_->path();
      }
    }
    if (OB_SUCC(ret) && !found) {
      std::string text;
      if (path_text.length() > 0) {
        text.assign(path_text.ptr(), path_text.length());
      }
      const uint8_t *input = reinterpret_cast<const uint8_t *>(path_text.ptr());
      rust::Box<common::json::PathParseResult> parsed = common::json::parse_mysql_path(
          rust::Slice<const uint8_t>(input, path_text.length()));
      if (parsed->status() != common::json::Status::Ok) {
        ret = OB_ERR_INVALID_JSON_PATH;
      } else {
        path = &parsed->path();
        if (index >= entries_.size()) {
          entries_.resize(index + 1);
        }
        entries_[index].emplace(std::move(text), std::move(parsed));
      }
    }
  }
  return ret;
}

int find_and_add_native_path_cache(JsonPathCache* path_cache,
                                   const common::json::Path*& res_path,
                                   ObString& path_str, int64_t arg_idx, bool enable_wildcard,
                                   bool is_const)
{
  INIT_SUCC(ret);
  if (OB_FAIL(path_cache->find_and_add(path_str, arg_idx, is_const, res_path))) {
    ret = OB_ERR_INVALID_JSON_PATH;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH);
  } else if (!enable_wildcard && res_path->can_match_many()) {
    ret = OB_ERR_INVALID_JSON_PATH_WILDCARD;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH_WILDCARD);
  }
  return ret;
}

bool ObJsonExprHelper::is_convertible_to_json(ObObjType &type)
{
  bool val = false;
  switch (type) {
    case ObNullType:
    case ObJsonType:
    case ObVarcharType:
    case ObCharType:
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType: {
      val = true;
      break;
    }
    default:
      break;
  }
  return val;
}

int ObJsonExprHelper::is_valid_for_json(ObExprResType* types_stack,
                                        uint32_t index,
                                        const char* func_name)
{
  INIT_SUCC(ret);
  ObObjType in_type = types_stack[index].get_type();

  if (!is_convertible_to_json(in_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, index + 1, func_name);
  } else if (ob_is_string_type(in_type)) {
    if (types_stack[index].get_collation_type() == CS_TYPE_BINARY) {
      types_stack[index].set_calc_collation_type(CS_TYPE_BINARY);
    } else {
      if (types_stack[index].get_charset_type() != CHARSET_UTF8MB4) {
        types_stack[index].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
      }
    }
  }

  return ret;
}

int ObJsonExprHelper::is_valid_for_json(ObExprResType& type,
                                        uint32_t index,
                                        const char* func_name)
{
  INIT_SUCC(ret);
  ObObjType in_type = type.get_type();
  
  if (!is_convertible_to_json(in_type)) {
    ret = OB_ERR_INVALID_TYPE_FOR_JSON;
    LOG_USER_ERROR(OB_ERR_INVALID_TYPE_FOR_JSON, index, func_name);
  } else if (ob_is_string_type(in_type) && type.get_collation_type() != CS_TYPE_BINARY) {
    if (type.get_charset_type() != CHARSET_UTF8MB4) {
      type.set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
  }

  return ret;
}

int ObJsonExprHelper::is_valid_for_path(ObExprResType* types_stack, uint32_t index)
{
  INIT_SUCC(ret);
  ObObjType in_type = types_stack[index].get_type();
  if (in_type == ObNullType || in_type == ObJsonType) {
  } else if (ob_is_string_type(in_type)) {
    if (types_stack[index].get_charset_type() != CHARSET_UTF8MB4) {
      types_stack[index].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
  } else {
    ret = OB_ERR_INVALID_JSON_PATH;
    LOG_USER_ERROR(OB_ERR_INVALID_JSON_PATH);
  }
  return ret;
}

void ObJsonExprHelper::set_type_for_value(ObExprResType* types_stack, uint32_t index)
{
  ObObjType in_type = types_stack[index].get_type();
  if (in_type == ObNullType) {
  } else if (ob_is_string_type(in_type)) {
    if (in_type == ObVarcharType && types_stack[index].get_collation_type() == CS_TYPE_BINARY) {
      types_stack[index].set_calc_type(ObHexStringType);
      types_stack[index].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else if (types_stack[index].get_charset_type() != CHARSET_UTF8MB4) {
      types_stack[index].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
    }
  } else if  (in_type == ObJsonType) {
    types_stack[index].set_calc_collation_type(CS_TYPE_UTF8MB4_BIN);
  }
}

int ObJsonExprHelper::is_json_zero(const ObString& data, int& result)
{
  INIT_SUCC(ret);
  int tmp_result = 0;
  ObJsonBinCtx ctx;
  ObJsonBin j_bin(data.ptr(), data.length(), &ctx);
  if (data.length() == 0) {
    result = 1; 
  } else if (OB_FAIL(j_bin.reset_iter())) {
  } else if (OB_FAIL(ObJsonBaseUtil::compare_int_json(0, &j_bin, tmp_result))) {
  } else {
    result = (tmp_result == 0) ? 0 : 1;
  }
  return ret;
}




#define PRINT_OB_DATETIME(ob_time, value, j_buf) \
  const int64_t tmp_buf_len = DATETIME_MAX_LENGTH + 1; \
  char tmp_buf[tmp_buf_len] = {0}; \
  int64_t pos = 0; \
  const int16_t print_scale = 6; \
  if (OB_FAIL(j_buf.append("\""))) { \
    LOG_WARN("fail to append \"", K(ret)); \
  } else if (OB_FAIL(ObTimeConverter::datetime_to_ob_time(value, tz_info, ob_time))) { \
  } else if (OB_FAIL(ObTimeConverter::ob_time_to_str(ob_time, ob_time.mode_, print_scale,  \
                                                      tmp_buf, tmp_buf_len, pos, true))) { \
    LOG_WARN("fail to change time to string", K(ret), K(ob_time), K(pos)); \
  } else if (OB_FAIL(j_buf.append(tmp_buf))) { \
    LOG_WARN("fail to append date_buf to j_buf", K(ret), KCSTRING(tmp_buf)); \
  } else if (OB_FAIL(j_buf.append("\""))) { \
    LOG_WARN("fail to append \"", K(ret)); \
  }

#define PRINT_OB_TIME(ob_time, value, TO_OB_TIME_METHOD, j_buf) \
  const int64_t tmp_buf_len = DATETIME_MAX_LENGTH + 1; \
  char tmp_buf[tmp_buf_len] = {0}; \
  int64_t pos = 0; \
  const int16_t print_scale = 6; \
  if (OB_FAIL(j_buf.append("\""))) { \
    LOG_WARN("fail to append \"", K(ret)); \
  } else if (OB_FAIL(ObTimeConverter::TO_OB_TIME_METHOD(value, ob_time))) { \
  } else if (OB_FAIL(ObTimeConverter::ob_time_to_str(ob_time, ob_time.mode_, print_scale,  \
                                                      tmp_buf, tmp_buf_len, pos, true))) { \
    LOG_WARN("fail to change time to string", K(ret), K(ob_time), K(pos)); \
  } else if (OB_FAIL(j_buf.append(tmp_buf))) { \
    LOG_WARN("fail to append date_buf to j_buf", K(ret), KCSTRING(tmp_buf)); \
  } else if (OB_FAIL(j_buf.append("\""))) { \
    LOG_WARN("fail to append \"", K(ret)); \
  }

struct ObFindDoubleEscapeFunc {
  ObFindDoubleEscapeFunc() {}

};




int ObJsonExprHelper::get_sql_scalar_type(
  ObEvalCtx& ctx,
  int64_t origin, 
  ObObjType& scalar_type, 
  int32_t& scalar_len,
  int32_t& precision,
  int32_t& scale,
  ObAccuracy& accuracy,
  ObLengthSemantics& length_semantics)
{
  int ret = OB_SUCCESS;
  ParseNode parse_node;
  parse_node.value_ = origin;

  scalar_type = static_cast<ObObjType>(parse_node.int16_values_[0]);
  scalar_len = parse_node.int32_values_[OB_NODE_CAST_C_LEN_IDX];

  if (ob_is_string_tc(scalar_type) && OB_FAIL(ObJsonUtil::get_accuracy_internal(
    accuracy, ctx, scalar_type, origin, length_semantics))) {
  } else if (ob_is_number_or_decimal_int_tc(scalar_type)) {
    precision = parse_node.int16_values_[OB_NODE_CAST_N_PREC_IDX];
    scale = parse_node.int16_values_[OB_NODE_CAST_N_SCALE_IDX];
    
    if (ObNumberType == scalar_type && is_decimal_int_accuracy_valid(precision, scale)) {
      bool enable_decimalint = false;
      if (OB_ISNULL(ctx.exec_ctx_.get_my_session())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(
        ctx.exec_ctx_.get_my_session(), enable_decimalint))) {
      } else if (enable_decimalint) {
        scalar_type = ObDecimalIntType;
      }
    }
  }

  return ret;
}

int ObJsonExprHelper::get_cast_type(const ObExprResType param_type2, 
                                    ObExprResType &dst_type,
                                    ObExprTypeCtx &type_ctx)
{
  INIT_SUCC(ret);
    if (!param_type2.is_int() && !param_type2.get_param().is_int()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cast param type is unexpected", K(param_type2));
  } else {
    const ObObj &param = param_type2.get_param();
    ParseNode parse_node;
    parse_node.value_ = param.get_int();
    ObObjType obj_type = static_cast<ObObjType>(parse_node.int16_values_[OB_NODE_CAST_TYPE_IDX]);
    dst_type.set_collation_type(static_cast<ObCollationType>(parse_node.int16_values_[OB_NODE_CAST_COLL_IDX]));
    dst_type.set_type(obj_type);
    if (ob_is_string_type(obj_type)) {
      // cast(x as char(10)) or cast(x as binary(10))
      dst_type.set_full_length(parse_node.int32_values_[OB_NODE_CAST_C_LEN_IDX],
                               param_type2.get_accuracy().get_length_semantics());
    } else if (ObFloatType == dst_type.get_type()) {
      // Compatible with mysql. If the precision p is not specified, produces a result of type FLOAT. 
      // If p is provided and 0 <= < p <= 24, the result is of type FLOAT. If 25 <= p <= 53, 
      // the result is of type DOUBLE. If p < 0 or p > 53, an error is returned
      // however, ob use -1 as default precision, so it is a valid value
      ObPrecision float_precision = parse_node.int16_values_[OB_NODE_CAST_N_PREC_IDX];
      if (float_precision < -1 || float_precision > OB_MAX_DOUBLE_FLOAT_PRECISION) {
        ret = OB_ERR_TOO_BIG_PRECISION;
        LOG_USER_ERROR(OB_ERR_TOO_BIG_PRECISION, float_precision, "CAST", OB_MAX_DOUBLE_FLOAT_PRECISION);
      } else if (float_precision <= OB_MAX_FLOAT_PRECISION) {
        dst_type.set_type(ObFloatType);
      } else {
        dst_type.set_type(ObDoubleType);
      }
      dst_type.set_precision(-1);
      dst_type.set_scale(parse_node.int16_values_[OB_NODE_CAST_N_SCALE_IDX]);
    } else if (ObJsonType == dst_type.get_type()) {
      dst_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    } else {
      dst_type.set_precision(parse_node.int16_values_[OB_NODE_CAST_N_PREC_IDX]);
      dst_type.set_scale(parse_node.int16_values_[OB_NODE_CAST_N_SCALE_IDX]);
      if (ObNumberType == dst_type.get_type()
          && is_decimal_int_accuracy_valid(dst_type.get_precision(), dst_type.get_scale())) {
        bool enable_decimalint = false;
        if (OB_ISNULL(type_ctx.get_session())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(ObSQLUtils::check_enable_decimalint(type_ctx.get_session(),
                                                               enable_decimalint))) {
        } else if (enable_decimalint) {
          dst_type.set_type(ObDecimalIntType);
        }
      }
    }
  }
  return ret;
}

int ObJsonExprHelper::set_dest_type(ObExprResType &type1, 
                                  ObExprResType &type, 
                                  ObExprResType &dst_type, 
                                  ObExprTypeCtx &type_ctx)
{
  INIT_SUCC(ret);
  const sql::ObSQLSessionInfo *session = type_ctx.get_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // always cast to user requested type
    if (ObCharType == dst_type.get_type()) {
      // cast(x as binary(10)), in parser,binary->T_CHAR+bianry, but, result type should be varchar, so set it.
      type.set_type(ObVarcharType);
    } else {
      type.set_type(dst_type.get_type());
      type.set_collation_type(dst_type.get_collation_type());
    }
    int16_t scale = dst_type.get_scale();
    if ((ObTimeType == dst_type.get_type() || ObDateTimeType == dst_type.get_type())
        && scale > MAX_SCALE_FOR_TEMPORAL) {
      ret = OB_ERR_TOO_BIG_PRECISION;
      LOG_USER_ERROR(OB_ERR_TOO_BIG_PRECISION, scale, "CAST", OB_MAX_DATETIME_PRECISION);
    }
    if (OB_SUCC(ret)) {
      ObCollationType collation_connection = type_ctx.get_coll_type();
      int32_t length = 0;
      if (ob_is_string_type(dst_type.get_type()) || ob_is_json(dst_type.get_type())) {
        type.set_collation_level(CS_LEVEL_IMPLICIT);
        int32_t len = dst_type.get_length();
        int16_t length_semantics = ((dst_type.is_string_type())
            ? dst_type.get_length_semantics()
            : (OB_NOT_NULL(type_ctx.get_session())
                ? type_ctx.get_session()->get_actual_length_semantics()
                : LS_BYTE));
        if (len > 0) { // cast(1 as char(10))
          type.set_full_length(len, length_semantics);
        } else if (OB_FAIL(get_cast_string_len(type1, dst_type, type_ctx, len, length_semantics,
                                               collation_connection))) {
        } else {
          type.set_full_length(len, length_semantics);
        }
        if (CS_TYPE_INVALID != dst_type.get_collation_type()) {
          // cast as binary
          type.set_collation_type(dst_type.get_collation_type());
        } else {
          // use collation of current session
          type.set_collation_type(collation_connection);
        }
      } else {
        type.set_length(length);
        if (ObNumberTC == dst_type.get_type_class() && 0 == dst_type.get_precision()) {
          // A zero precision number uses the default accuracy for the current
          // compatibility mode.
          type.set_precision(ObAccuracy::DDL_DEFAULT_ACCURACY2[0][ObNumberType].get_precision());
        } else if (ObDecimalIntTC == dst_type.get_type_class() && 0 == dst_type.get_precision()) {
          ret = OB_ERR_UNEXPECTED;
        } else if (ObIntTC == dst_type.get_type_class() || ObUIntTC == dst_type.get_type_class()) {
          // for int or uint , the precision = len
          int32_t len = 0;
          int16_t length_semantics = LS_BYTE;//unused
          if (OB_FAIL(get_cast_inttc_len(type1, dst_type, type_ctx, len, length_semantics, collation_connection))) {
          } else {
            len = len > OB_LITERAL_MAX_INT_LEN ? OB_LITERAL_MAX_INT_LEN : len;
            type.set_precision(static_cast<int16_t>(len));
          }
        } else {
          type.set_precision(dst_type.get_precision());
        }
        type.set_scale(dst_type.get_scale());
      }
    }
  }

  return ret;
}

int ObJsonExprHelper::get_cast_inttc_len(ObExprResType &type1,
                                        ObExprResType &type2,
                                        ObExprTypeCtx &type_ctx,
                                        int32_t &res_len,
                                        int16_t &length_semantics,
                                        ObCollationType conn)
{
  INIT_SUCC(ret);

  if (type1.is_literal()) { // literal
    if (ObStringTC == type1.get_type_class()) { 
      res_len = type1.get_accuracy().get_length();
      length_semantics = type1.get_length_semantics();
    } else if (OB_FAIL(ObField::get_field_mb_length(type1.get_type(),
        type1.get_accuracy(), type1.get_collation_type(), res_len))) {
    }
  } else {
    res_len = CAST_STRING_DEFUALT_LENGTH[type1.get_type()];
    ObObjTypeClass tc1 = type1.get_type_class();
    int16_t scale = type1.get_accuracy().get_scale();
    if (ObDoubleTC == tc1) {
      res_len -= 1;
    } else if (ObDateTimeTC == tc1 && scale > 0) {
      res_len += scale - 1;
    } else if (OB_FAIL(get_cast_string_len(type1, type2, type_ctx, res_len, length_semantics, conn))) {
    } else {
      // do nothing
    }
  }

  return ret;
}


int ObJsonExprHelper::get_clause_opt(ObExpr *expr,
                                    ObEvalCtx &ctx,
                                    int8_t &type)
{
  INIT_SUCC(ret);
  ObDatum *json_datum = NULL;
  if (OB_FAIL(expr->eval(ctx, json_datum))) {
  } else {
    int64_t option_type = json_datum->get_int();
    type = static_cast<int8_t>(option_type);
  }
  return ret;
}

int ObJsonExprHelper::get_cast_string_len(ObExprResType &type1,
                        ObExprResType &type2,
                        common::ObExprTypeCtx &type_ctx,
                        int32_t &res_len,
                        int16_t &length_semantics,
                        common::ObCollationType conn)
{
  INIT_SUCC(ret);
  const ObObj &val = type1.get_param();

  if (!type1.is_literal()) { // column
    res_len = CAST_STRING_DEFUALT_LENGTH[type1.get_type()];
    int16_t prec = type1.get_accuracy().get_precision();
    int16_t scale = type1.get_accuracy().get_scale();
    switch(type1.get_type()) {
      case ObTinyIntType:
      case ObSmallIntType:
      case ObMediumIntType:
      case ObInt32Type:
      case ObIntType:
      case ObUTinyIntType:
      case ObUSmallIntType:
      case ObUMediumIntType:
      case ObUInt32Type:
      case ObUInt64Type: {
        int32_t prec = static_cast<int32_t>(type1.get_accuracy().get_precision());
        res_len = prec > res_len ? prec : res_len;
        break;
      }
      case ObNumberType:
      case ObDecimalIntType: 
      case ObUNumberType: {
        if (0 < prec) {
          if (0 < scale) {
            res_len =  prec + 2;
          } else {
            res_len = prec + 1;
          }
        }
        break;
      }
      case ObDateTimeType:
      case ObTimestampType: {
        if (scale > 0) {
          res_len += scale + 1;
        }
        break;
      }
      case ObTimeType: {
        if (scale > 0) {
          res_len += scale + 1;
        }
        break;
      }
      case ObTinyTextType:
      case ObTextType:
      case ObMediumTextType:
      case ObLongTextType:
      case ObVarcharType:
      case ObCharType:
      case ObHexStringType: {
        res_len = type1.get_length();
        length_semantics = type1.get_length_semantics();
        break;
      }
      default: {
        break;
      }
    }
  } else if (type1.is_null()) {
    res_len = 0;//compatible with mysql;
  } else if (OB_ISNULL(type_ctx.get_session())) {
    // calc type don't set ret, just print the log. by design.
  } else { // literal
    ObArenaAllocator oballocator(ObModIds::BLOCK_ALLOC);
    ObCastMode cast_mode = CM_NONE;
    ObCollationType cast_coll_type = (CS_TYPE_INVALID != type2.get_collation_type())
        ? type2.get_collation_type()
        : conn;
    const ObDataTypeCastParams dtc_params =
          ObBasicSessionInfo::create_dtc_params(type_ctx.get_session());
    ObCastCtx cast_ctx(&oballocator,
                       &dtc_params,
                       0,
                       cast_mode,
                       cast_coll_type);
    ObString val_str;
    EXPR_GET_VARCHAR_V2(val, val_str);
    if (OB_SUCC(ret) && NULL != val_str.ptr()) {
      int32_t len_byte = val_str.length();
      res_len = len_byte;
      length_semantics = LS_CHAR;
      if (NULL != val_str.ptr()) {
        int32_t trunc_len_byte = static_cast<int32_t>(ObCharset::strlen_byte_no_sp(cast_coll_type,
            val_str.ptr(), len_byte));
        res_len = static_cast<int32_t>(ObCharset::strlen_char(cast_coll_type,
            val_str.ptr(), trunc_len_byte));
      }
      if (type1.is_numeric_type() && !type1.is_integer_type()) {
        res_len += 1;
      }
    }
  }
  return ret;
}

int ObJsonExprHelper::parse_res_type(ObExprResType& type1,
                                     ObExprResType& res_type,
                                     ObExprResType& result_type,
                                     ObExprTypeCtx& type_ctx)
{
  INIT_SUCC(ret);
  const int32_t VARCHAR2_DEFAULT_LEN = 4000;
  ObExprResType dst_type;
  const ObObj &param = res_type.get_param();

  if (param.get_int() == 0) {
    //     result_type.set_type(type1.get_type());
    //     result_type.set_collation_type(type1.get_collation_type());
    //     result_type.set_accuracy(type1.get_accuracy());
    //     ObObjType obj_type = type1.get_type();

    ObObjType obj_type = ObJsonType;
    result_type.set_type(ObJsonType);
    result_type.set_collation_type(CS_TYPE_UTF8MB4_BIN);
    int16_t length_semantics = (OB_NOT_NULL(type_ctx.get_session())
                ? type_ctx.get_session()->get_actual_length_semantics() : LS_BYTE);
    result_type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());

    result_type.set_collation_level(CS_LEVEL_IMPLICIT);
  } else if (OB_FAIL(ObJsonExprHelper::get_cast_type(res_type, dst_type, type_ctx))) {
  } else if (OB_FAIL(ObJsonExprHelper::set_dest_type(type1, result_type, dst_type, type_ctx))) {
  } else {
    result_type.set_calc_collation_type(result_type.get_collation_type());
  }

  return ret;
}

int ObJsonExprHelper::eval_and_check_res_type(int64_t value, ObObjType& type, int32_t& dst_len)
{
  INIT_SUCC(ret);
  ParseNode node;
  node.value_ = value;
  ObObjType dst_type = static_cast<ObObjType>(node.int16_values_[0]);
  dst_len = node.int32_values_[OB_NODE_CAST_C_LEN_IDX];
  if (dst_type != ObVarcharType 
      && dst_type != ObLongTextType 
      && dst_type != ObJsonType) {
    ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
    LOG_USER_ERROR(OB_ERR_INVALID_DATA_TYPE_RETURNING);
  } else {
    type = dst_type;
  }
  return ret;
}
// JSON_EXPR_FLAG 0 json value , 1 json query
int ObJsonExprHelper::check_item_func_with_return(ObJsonPathNodeType path_type, ObObjType dst_type, common::ObCollationType dst_coll_type, int8_t JSON_EXPR_FLAG)
{
  INIT_SUCC(ret);
  switch (path_type) {
    case JPN_ABS :{
      break;
    }
    case JPN_BOOLEAN :
    case JPN_BOOL_ONLY :{
      if (dst_type == ObVarcharType) {
      } else {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
      break;
    }
    case JPN_DATE :{
      if (dst_type == ObDateTimeType || dst_type == ObMySQLDateTimeType) {
      } else {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
      break;
    }
    case JPN_DOUBLE :{
      if (dst_type == ObDoubleType || dst_type == ObUDoubleType) {
      } else {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
      break;
    }
    case JPN_FLOOR :
    case JPN_CEILING :
    case JPN_LENGTH :
    case JPN_NUMBER :
    case JPN_NUM_ONLY :
    case JPN_SIZE :{
      if (JSON_EXPR_FLAG == 1 || (JSON_EXPR_FLAG == 0 && ob_is_numeric_type(dst_type))) {
      } else {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
      break;
    }
    
    case JPN_TIMESTAMP :{
      if (dst_type != ObTimestampType) {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
      break;
    }
    case JPN_TYPE :{
      if (dst_type == ObJsonType && JSON_EXPR_FLAG != 1) {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
    }
    case JPN_STRING :
    case JPN_STR_ONLY :
    case JPN_LOWER :
    case JPN_UPPER :{
      if (dst_type == ObVarcharType || (dst_type == ObLongTextType && dst_coll_type != CS_TYPE_BINARY)) {
      } else {
        ret = OB_ERR_INVALID_DATA_TYPE_RETURNING;
      }
      break;
    }
    default :{
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObJsonExprHelper::get_expr_option_value(const ObExprResType param_type2, int8_t &dst_type)
{
  INIT_SUCC(ret);
  if (!param_type2.is_int() && !param_type2.get_param().is_int()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("cast param type is unexpected", K(param_type2));
  } else {
    dst_type = param_type2.get_param().get_int();
  }
  return ret;
}

int ObJsonExprHelper::calc_asciistr_in_expr(const ObString &src,
                                            const ObCollationType src_cs_type,
                                            const ObCollationType dst_cs_type,
                                            char* buf, const int64_t buf_len, int32_t &pos)
{
  int ret = OB_SUCCESS;
  struct Functor {
    Functor(char* buf, int32_t& pos, int64_t buf_len, ObCollationType dst_cs_type)
      : buf(buf), pos(pos), buf_len(buf_len), dst_cs_type(dst_cs_type) {}
    char* buf;
    int32_t& pos;
    int64_t buf_len;
    const ObCollationType dst_cs_type;
    int operator() (const ObString &encoding, ob_wc_t wchar) {
      int ret = OB_SUCCESS;
      if (ob_isascii(wchar) && '\\' != wchar) {
        int32_t written_bytes = 0;

        if (OB_FAIL(ObCharset::wc_mb(dst_cs_type, wchar,
                                    buf + pos, buf_len - pos, written_bytes))) {
        } else {
          pos += written_bytes;
        }
      } else {
        const int64_t temp_buf_len = 4;
        char temp_buf[temp_buf_len];
        int32_t temp_written_bytes = 0;

        if (OB_FAIL(ObCharset::wc_mb(CS_TYPE_UTF16_BIN, wchar,
                                    temp_buf, temp_buf_len, temp_written_bytes))) {
        } else {
          const int utf16_minmb_len = 2;

          if (OB_UNLIKELY(ObCharset::is_cs_nonascii(dst_cs_type))) {
            // not support non-ascii database charset for now
            ret = OB_NOT_SUPPORTED;
            LOG_USER_ERROR(OB_NOT_SUPPORTED, "charset except ascii");
          } else {
            for (int i = 0; OB_SUCC(ret) && i < temp_written_bytes/utf16_minmb_len; ++i) {
              if (OB_UNLIKELY(pos >= buf_len)) {
                ret = OB_SIZE_OVERFLOW;
              } else {
                buf[pos++] = '\\';
              }
              if (OB_SUCC(ret) && '\\' != wchar) {
                if (OB_UNLIKELY(pos >= buf_len)) {
                  ret = OB_SIZE_OVERFLOW;
                } else {
                  buf[pos++] = 'u';
                }
                int64_t hex_writtern_bytes = 0;
                if (OB_FAIL(ret)) {
                } else if (OB_FAIL(hex_print(temp_buf + i*utf16_minmb_len, utf16_minmb_len,
                                      buf + pos, buf_len - pos, hex_writtern_bytes))) {
                } else {
                  pos += hex_writtern_bytes;
                }
              }
            }
          }
        }
      }
      return ret;
    };
  };
  
  ObCharsetType src_charset_type = ObCharset::charset_type_by_coll(src_cs_type);
  struct Functor temp_handle(buf, pos, buf_len, dst_cs_type);
  OZ(ObFastStringScanner::foreach_char(src, src_charset_type, temp_handle));
  return ret;
}

int ObJsonExprHelper::parse_asc_option(ObExprResType& asc_type,
                                       ObExprResType& type1,
                                       ObExprResType& res_type,
                                       ObExprTypeCtx& type_ctx)
{
  INIT_SUCC(ret);
  ObExprResType temp_type;
  ObObjType doc_type = type1.get_type();
  int8_t asc_option = 0;

  if (asc_type.get_type() != ObIntType) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("<ASCII type> param type is unexpected", K(asc_type.get_type()));
  } else if (OB_FAIL(ObJsonExprHelper::get_expr_option_value(asc_type, asc_option))) {
  } else if (asc_option == 1 
             && ob_is_string_type(doc_type) 
             && ((res_type.is_character_type() && (res_type.get_length_semantics() == LS_CHAR || res_type.get_length_semantics() == LS_BYTE)) 
                  || res_type.is_lob())) {
    type1.set_calc_length_semantics(res_type.get_length_semantics());
    ObLength length = 0;
    ObExprResType temp_type;
    temp_type.set_meta(type1.get_calc_meta());
    temp_type.set_length_semantics(res_type.get_length_semantics());

    if (!temp_type.is_blob() && OB_FAIL(ObExprResultTypeUtil::deduce_max_string_length_extended(
      type_ctx.get_session()->get_dtc_params(), type1, temp_type, length))) {
    } else {
      type1.set_calc_length(length);
      res_type.set_length(length * 10);
      if (res_type.is_lob()) {
        res_type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObLongTextType]).get_length());
      }
    }
  }

  return ret;
}

int ObJsonExprHelper::character2_ascii_string(common::ObIAllocator *allocator, 
                                              const ObExpr &expr,
                                              ObEvalCtx &ctx,
                                              ObString& result,
                                              int32_t reserve_len)
{
  INIT_SUCC(ret);
  char *buf = NULL;
  int64_t buf_len = result.length() * ObCharset::MAX_MB_LEN * 2;
  int32_t length = 0;

  if ((OB_NOT_NULL(allocator) && OB_ISNULL(buf = static_cast<char*>(allocator->alloc(buf_len + reserve_len + 1))))
       || (OB_ISNULL(allocator) && OB_ISNULL(buf = static_cast<char*>(expr.get_str_res_mem(ctx, buf_len + reserve_len + 1))))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ObJsonExprHelper::calc_asciistr_in_expr(result, 
                                                             expr.datum_meta_.cs_type_,
                                                             expr.datum_meta_.cs_type_,
                                                             buf, buf_len, length))) {
  } else {
    buf[length] = 0;
    result.assign_ptr(buf, length);
  }
  return ret;
}

int ObJsonExprHelper::pre_default_value_check(ObObjType dst_type, ObString val_str, ObObjType val_type, size_t length) {
  INIT_SUCC(ret);
  size_t len;
  switch (dst_type) {
    case ObTinyTextType:
    case ObTextType:
    case ObMediumTextType:
    case ObLongTextType:
    case ObVarcharType:
    case ObCharType:
    case ObHexStringType: {
      break;
    }
    case ObNumberType:
    case ObDecimalIntType: {
      if (val_type == ObNumberType || val_type == ObDecimalIntType) {
      } else {
        len = val_str.length();
        for(size_t i = 0; i < len; i++) {
          if (val_str[i] == '-' || val_str[i] == '+') {
          } else if (val_str[i] > '9' || val_str[i] < '0') {
            ret = OB_ERR_INVALID_DEFAULT_VALUE_PROVIDED;
            LOG_WARN("number check fail");
          }
        }
      }
      break;
    }
    case ObDateType:
    case ObDateTimeType: {
      if (dst_type == val_type) {
      } else if (val_type != ObCharType) {
        ret = OB_ERR_INVALID_DEFAULT_VALUE_PROVIDED;
      } else {
        len = val_str.length();
        if(len >= 5 && val_str[4] != '-') {
          ret = OB_ERR_INVALID_DEFAULT_VALUE_PROVIDED;
        } else if(len >= 8 && val_str[7] != '-') {
          ret = OB_ERR_INVALID_DEFAULT_VALUE_PROVIDED;
        }
      }
      break;
    }
    case ObJsonType: {
      if (ob_is_number_tc(val_type)) {
        ret = OB_ERR_DEFAULT_VALUE_NOT_MATCH;
      }
      break;
    }
    default:
      break;
  }
  return ret;
}

int ObJsonExprHelper::get_json_max_depth_config()
{
  uint32_t json_max_depth = JSON_DOCUMENT_MAX_DEPTH;

  json_max_depth = config::json_document_max_depth();
  if (json_max_depth < JSON_DOCUMENT_MAX_DEPTH || json_max_depth > 1024) {
    json_max_depth = JSON_DOCUMENT_MAX_DEPTH;
  }

  return json_max_depth;
}

/********** ObJsonExprHelper for json partial update  ****************/
int ObJsonExprHelper::pack_json_diff_res(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    ObIAllocator &temp_allocator,
    ObIJsonBase *json_doc,
    ObDatum &res)
{
  INIT_SUCC(ret);
  ObJsonBin *bin = nullptr;
  ObJsonDeltaLob json_delta_lob;
  char *res_buf = nullptr;
  int64_t res_buf_len = 0;
  int64_t pos = 0;
  if (OB_ISNULL(json_doc)) {
    res.set_null();
  } else if (! json_doc->is_bin()) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FALSE_IT(bin = static_cast<ObJsonBin*>(json_doc))) {
  } else if (OB_FAIL(json_delta_lob.init(bin))) {
  } else if (OB_FAIL(json_delta_lob.check_binary_diff())) {
  } else if (OB_FALSE_IT(res_buf_len = json_delta_lob.get_serialize_size())) {
  } else if (OB_ISNULL(res_buf = expr.get_str_res_mem(ctx, res_buf_len))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(json_delta_lob.serialize(res_buf, res_buf_len, pos))) {
  } else {
    res.set_string(res_buf, res_buf_len);
  }
  return ret;
}

int ObJsonExprHelper::pack_json_res(
    const ObExpr &expr,
    ObEvalCtx &ctx,
    ObIAllocator &temp_allocator,
    ObIJsonBase *json_doc,
    ObDatum &res)
{
  INIT_SUCC(ret);
  bool shoudl_pack_diff = false;
  ObJsonBin *json_bin = nullptr;
  if (OB_ISNULL(json_doc)) {
  } else if (! json_doc->is_bin()) {
  } else if (OB_FALSE_IT(json_bin = static_cast<ObJsonBin*>(json_doc))) {
  } else if (OB_FAIL(json_bin->should_pack_diff(shoudl_pack_diff))) {
  }

  if (OB_ISNULL(json_doc)) {
    res.set_null();
  } else if (shoudl_pack_diff) {
    if (OB_FAIL(ObJsonExprHelper::pack_json_diff_res(expr, ctx, temp_allocator, json_doc, res))) {
    }
  } else {
    ObString str;
    if (OB_FAIL(ObJsonWrapper::get_raw_binary(json_doc, str, &temp_allocator))) {
    } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, str))) {
    }
  }
  return ret;
}


int ObJsonExprHelper::is_allow_partial_update(
    const ObExpr &expr, 
    ObEvalCtx &ctx,
    const ObString &locator_str,
    bool &allow_partial_update)
{
  INIT_SUCC(ret);
  ObString option;
  ObLobLocatorV2 locator(locator_str);
  sql::ObSQLSessionInfo *session = nullptr;
  if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  // 1. is delta lob
  // 2. perisit outrow lob
  } else if (! locator.is_delta_temp_lob() && ! (locator.is_persist_lob() && ! locator.is_inrow())) {
  } else if (is_json_partial_update_mode(expr)) {
    option = session->get_log_row_value_option();
    allow_partial_update = option.case_compare(OB_LOG_ROW_VALUE_PARTIAL_JSON) == 0
        || option.case_compare(OB_LOG_ROW_VALUE_PARTIAL_ALL) == 0;
  }
  return ret;
}

bool ObJsonExprHelper::is_json_partial_update_mode(const ObExpr &expr)
{
  return (expr.extra_ & OB_JSON_PARTIAL_UPDATE_ALLOW) != 0;
}


int ObJsonExprHelper::init_json_expr_extra_info(
    ObIAllocator *allocator,
    const ObRawExpr &raw_expr,
    const ObExprOperatorType type, 
    ObExpr &rt_expr)
{
  int ret = OB_SUCCESS;
  const uint64_t extra = raw_expr.get_json_partial_update_flag();
  if (! is_json_partial_update_mode(extra)) { // only used for json partial update now
  } else {
    rt_expr.extra_ = extra;
  }
  return ret;  
}

int ObJsonExprHelper::get_session_query_timeout_ts(ObEvalCtx &ctx, int64_t &timeout_ts)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = nullptr;
  if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    timeout_ts = session->get_query_timeout_ts();
  }
  return ret;
}

bool ObJsonExprHelper::check_json_path_can_pushdown(const ObRawExpr &path_expr)
{
  bool res = false;
  if (! path_expr.is_const_raw_expr()) {
    LOG_INFO("path expr is not const expr, so not support", K(path_expr));
  } else {
    const ObConstRawExpr &const_param = static_cast<const ObConstRawExpr &>(path_expr);
    const ObObj &path_value = const_param.get_value();
    share::ObSubColumnPath sub_col_path;
    if (! path_value.is_string_type() || path_value.is_null()) {
      LOG_INFO("path expr is not string, so not support", K(path_value), K(const_param));
    } else if (OB_SUCCESS != share::ObSubColumnPath::parse_sub_column_path(path_value.get_string(), sub_col_path)) {
    } else {
      res = true;
    }
  }
  return res;
}

bool ObJsonExprHelper::check_json_expr_can_pushdown(const ObRawExpr &json_expr)
{
  bool res = false;
  if (T_FUN_SYS_JSON_VALUE == json_expr.get_expr_type() && json_expr.get_param_count() == OB_JSON_VALUE_EXPR_PARAM_COUNT) {
    const ObRawExpr *doc_expr = json_expr.get_param_expr(JSN_VAL_DOC);
    const ObRawExpr *path_expr = json_expr.get_param_expr(JSN_VAL_PATH);
    const ObRawExpr *type_expr = json_expr.get_param_expr(JSN_VAL_RET);
    const ObRawExpr *trunc_expr = json_expr.get_param_expr(JSN_VAL_TRUNC);
    const ObRawExpr *ascii_expr = json_expr.get_param_expr(JSN_VAL_ASCII);
    const ObRawExpr *empty_expr = json_expr.get_param_expr(JSN_VAL_EMPTY);
    const ObRawExpr *empty_def_expr = json_expr.get_param_expr(JSN_VAL_EMPTY_DEF);
    const ObRawExpr *error_expr = json_expr.get_param_expr(JSN_VAL_ERROR);
    const ObRawExpr *error_def_expr = json_expr.get_param_expr(JSN_VAL_ERROR_DEF);
    const ObRawExpr *mismatch_expr = json_expr.get_param_expr(JSN_VAL_MISMATCH);

    if (OB_ISNULL(doc_expr) || OB_ISNULL(path_expr) || OB_ISNULL(type_expr) || OB_ISNULL(trunc_expr)
        || OB_ISNULL(ascii_expr) || OB_ISNULL(empty_expr) || OB_ISNULL(empty_def_expr)
        || OB_ISNULL(error_expr) || OB_ISNULL(error_def_expr) || OB_ISNULL(mismatch_expr)) {
      LOG_INFO("param expr is null, so do not pushdown", KP(doc_expr), KP(path_expr), KP(type_expr), KP(trunc_expr),
          KP(ascii_expr), K(empty_expr), KP(empty_def_expr), KP(error_expr), KP(error_def_expr), KP(mismatch_expr), K(json_expr));
    } else if (json_expr.get_result_meta().is_json()) {
      LOG_INFO("result type is json, so do not pushdown", KPC(type_expr), K(json_expr));
    // check doc_expr, it must be json cloumn ref
    } else if (ObRawExpr::EXPR_COLUMN_REF != doc_expr->get_expr_class() ||  ! doc_expr->get_result_meta().is_json()) {
      LOG_INFO("doc expr is not json column ref, so do not pushdown", KPC(doc_expr), K(json_expr));
    // check path_expr
    } else if (! check_json_path_can_pushdown(*path_expr)) {
      LOG_INFO("json path is not support pushdown", KPC(path_expr), K(json_expr));
    // check return type expr
    } else if (! type_expr->is_const_raw_expr()) {
      LOG_INFO("returning type is not const, so do not pushdown", KPC(type_expr), K(json_expr));
    // check trunc expr
    } else if (! trunc_expr->is_const_raw_expr() || static_cast<const ObConstRawExpr *>(trunc_expr)->get_value().get_int() != 0) {
      LOG_INFO("truncate expr is not default value, so do not support pushdown", KPC(trunc_expr), K(json_expr));
    // check ascii expr
    } else if (! ascii_expr->is_const_raw_expr() || static_cast<const ObConstRawExpr *>(ascii_expr)->get_value().get_int() != 0) {
      LOG_INFO("ascii expr not default value, so do not support pushdown", KPC(ascii_expr), K(json_expr));
    } else if (! empty_expr->is_const_raw_expr() || static_cast<const ObConstRawExpr *>(empty_expr)->get_value().get_int() != JSN_VALUE_IMPLICIT) {
      LOG_INFO("empty clause expr not default value, so do not support pushdown", KPC(empty_expr), K(json_expr));
    } else if (! empty_def_expr->is_const_raw_expr() || ! static_cast<const ObConstRawExpr *>(empty_def_expr)->get_value().is_null()) {
      LOG_INFO("empty define clause expr not default value, so do not support pushdown", KPC(empty_def_expr), K(json_expr));
    } else if (! error_expr->is_const_raw_expr() || static_cast<const ObConstRawExpr *>(error_expr)->get_value().get_int() != JSN_VALUE_IMPLICIT) {
      LOG_INFO("error clause expr not default value, so do not support pushdown", KPC(error_expr), K(json_expr));
    } else if (! error_def_expr->is_const_raw_expr() || ! static_cast<const ObConstRawExpr *>(error_def_expr)->get_value().is_null()) {
      LOG_INFO("error define clause expr not default value, so do not support pushdown", KPC(error_def_expr), K(json_expr));
    } else {
      res = true;
    }
  } else {
    LOG_INFO("not support pushdown json expr", K(json_expr));
  }
  return res;
}

int ObJsonExprHelper::get_sub_column_path_from_json_expr(ObIAllocator& allocator, const ObRawExpr &json_expr, share::ObSubColumnPath& sub_col_path)
{
  int ret = OB_SUCCESS;
  if (T_FUN_SYS_JSON_VALUE == json_expr.get_expr_type()) {
    const ObRawExpr *path_expr = json_expr.get_param_expr(JSN_VAL_PATH);
    ObString path_str;
    if (OB_ISNULL(path_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (! path_expr->is_const_raw_expr()) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const ObConstRawExpr *const_param = static_cast<const ObConstRawExpr *>(path_expr);
      const ObObj &path_value = const_param->get_value();
      if (! path_value.is_string_type() || path_value.is_null()) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ob_write_string(allocator, path_value.get_string(), path_str))) {
      } else if (OB_FAIL(share::ObSubColumnPath::parse_sub_column_path(path_str, sub_col_path))) {
      }
    }
  } else {
    ret = OB_NOT_SUPPORTED;
  }
  return ret; 
}

/********** ObJsonExprHelper for json partial update  ****************/


/********** ObJsonDeltaLob ****************/

int ObJsonDeltaLob::init(ObJsonBin *j_bin)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(j_bin)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(update_ctx_ = j_bin->get_update_ctx())) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObJsonDeltaLob::init(
    ObIAllocator *allocator,
    ObLobLocatorV2 locator,
    int64_t query_timeout_ts)
{
  int ret = OB_SUCCESS;
  allocator_ = allocator;
  query_timeout_ts_ = query_timeout_ts;
  if (OB_ISNULL(allocator_)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_ISNULL(
                 update_ctx_ =
                     OB_NEWx(ObJsonBinUpdateCtx, allocator_, allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(data_plane::restore_json_lob_delta(
                 *allocator_, locator, query_timeout_ts_,
                 *update_ctx_, lob_handle_))) {
  } else if (OB_FAIL(ObJsonExprHelper::get_partial_json_bin(
                 *allocator_, *lob_handle_, update_ctx_, j_base_))) {
  }
  return ret;
}

void ObJsonDeltaLob::reset()
{
  if (OB_NOT_NULL(j_base_)) {
    j_base_->reset();
    j_base_ = nullptr;
    update_ctx_ = nullptr;
  } else if (OB_NOT_NULL(update_ctx_)) {
    update_ctx_->~ObJsonBinUpdateCtx();
    update_ctx_ = nullptr;
  }
  data_plane::destroy_json_lob_handle(lob_handle_);
}

int64_t ObJsonDeltaLob::get_serialize_size() const
{
  return OB_ISNULL(update_ctx_)
      ? 0
      : data_plane::get_json_lob_delta_serialize_size(*update_ctx_);
}

int ObJsonDeltaLob::serialize(
    char *buf,
    int64_t buf_len,
    int64_t &pos) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(update_ctx_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(data_plane::serialize_json_lob_delta(
                 *update_ctx_, buf, buf_len, pos))) {
  }
  return ret;
}

int ObJsonDeltaLob::check_binary_diff() const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(update_ctx_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(data_plane::validate_json_lob_delta(*update_ctx_))) {
  }
  return ret;
}

/********** ObJsonDeltaLob ****************/

}
}
