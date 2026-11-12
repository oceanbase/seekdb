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
#include "ob_expr_json_merge_patch.h"
#include "rust/json.h"
#include <optional>
#include "sql/engine/expr/ob_expr_json_func_helper.h"
#include "share/ob_json_access_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{
ObExprJsonMergePatch::ObExprJsonMergePatch(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc,
      T_FUN_SYS_JSON_MERGE_PATCH,
      N_JSON_MERGE_PATCH, 
      PARAM_NUM_UNKNOWN,
      VALID_FOR_GENERATED_COL,
      NOT_ROW_DIMENSION)
{
}

ObExprJsonMergePatch::~ObExprJsonMergePatch()
{
}

int ObExprJsonMergePatch::calc_result_typeN(ObExprResType& type,
                                            ObExprResType* types_stack,
                                            int64_t param_num,
                                            ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  INIT_SUCC(ret);
  {
    const ObString name(N_JSON_MERGE_PATCH);
    if (param_num < 2) {
      ret = OB_ERR_PARAM_SIZE;
      LOG_USER_ERROR(OB_ERR_PARAM_SIZE, name.length(), name.ptr());
    }

    type.set_json();
    type.set_length((ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType]).get_length());
    for (int64_t i = 0; OB_SUCC(ret) && i < param_num; i++) {
      if (OB_FAIL(ObJsonExprHelper::is_valid_for_json(types_stack, i, N_JSON_MERGE_PRESERVE))) {
      }
    }
  }

  return ret;
}

int ObExprJsonMergePatch::eval_json_merge_patch(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  return ObJsonExprHelper::eval_json_merge(expr, ctx, res, true);
}

#define SET_COVER_ERROR(error) \
{ \
  if (!is_cover_error) { \
    is_cover_error = true; \
    err_code = (error); \
  } \
}

int ObExprJsonMergePatch::eval_ora_json_merge_patch(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  INIT_SUCC(ret);
  ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
  
  MultimodeAlloctor temp_allocator(tmp_alloc_g.get_allocator());
  lib::ObMallocHookAttrGuard malloc_guard(lib::ObMemAttr("JSONModule"));

  bool is_cover_error = false;
  int err_code = 0;

  // eval option original int64 type value
  int64_t opt_array[OPT_MAX_ID] = {0};
  for (size_t i = 2; OB_SUCC(ret) && i < expr.arg_cnt_; i++) {
    ObDatum *opt_datum = NULL;
    ObExpr *opt_expr = expr.args_[i];
    ObObjType val_type = opt_expr->datum_meta_.type_;
    ObCollationType cs_type = opt_expr->datum_meta_.cs_type_;
    if (OB_FAIL(temp_allocator.eval_arg(opt_expr, ctx, opt_datum))) {
    } else if (val_type == ObNullType || opt_datum->is_null()) {
    } else if (!ob_is_integer_type(val_type)) {
      ret = OB_ERR_INVALID_TYPE_FOR_OP;
      LOG_WARN("input type error", K(val_type));
    } else {
      opt_array[i-2] = opt_datum->get_int();
    }
  }

  const int64_t& is_pretty = opt_array[OPT_PRETTY_ID];
  const int64_t& is_trunc = opt_array[OPT_TRUNC_ID];
  const int64_t& is_asc = opt_array[OPT_ASCII_ID];
  const int64_t& err_type = opt_array[OPT_ERROR_ID];
  const int64_t& return_type = opt_array[OPT_RES_TYPE_ID];

  // some constraint check
  ObObjType dst_type;
  int32_t dst_len;
  if (OB_FAIL(ret)) {
  } else if (return_type == 0) {
    dst_type = ObJsonType;
    const ObAccuracy &default_accuracy = ObAccuracy::DDL_DEFAULT_ACCURACY[ObJsonType];
    dst_len = default_accuracy.get_length();
    // dst_type = expr.args_[0]->datum_meta_.type_;
    // const ObAccuracy &default_accuracy = ObAccuracy::DDL_DEFAULT_ACCURACY[dst_type];
    // dst_len = dst_type == ObVarcharType ? OB_MAX_EXTENDED_VARCHAR_LENGTH : default_accuracy.get_length();
  } else if (OB_FAIL(ObJsonExprHelper::eval_and_check_res_type(return_type, dst_type, dst_len))) {
  } else if ((expr.datum_meta_.cs_type_ == CS_TYPE_BINARY || dst_type == ObJsonType) && (opt_array[OPT_PRETTY_ID] > 0 || opt_array[OPT_ASCII_ID] > 0)) {
    // ascii or pretty only support text
    ret = OB_ERR_NON_TEXT_RET_NOTSUPPORT;
  }

  rust::Box<BinaryMerge> merger = common::json::new_merge_accumulator(2);
  bool argument_null = false;
  bool target_failed = false;
  if (OB_SUCC(ret)) {
    ret = ObJsonExprHelper::push_json_merge_argument(expr, ctx, temp_allocator, 0, *merger, argument_null);
    target_failed = ret != OB_SUCCESS;
    if (target_failed) { SET_COVER_ERROR(ret); }
  }
  if ((!is_cover_error && OB_FAIL(ret)) || (!target_failed && !merger->merge_needs_input())) {
  } else {
    bool patch_null = false;
    bool input_error = true;
    int patch_status = OB_SUCCESS;
    if (target_failed) {
      rust::Box<BinaryMerge> patch_probe = common::json::new_merge_accumulator(0);
      patch_status = ObJsonExprHelper::push_json_merge_argument(
          expr, ctx, temp_allocator, 1, *patch_probe, patch_null, &input_error);
    } else {
      patch_status = ObJsonExprHelper::push_json_merge_argument(
          expr, ctx, temp_allocator, 1, *merger, patch_null, &input_error);
    }
    if (patch_status != OB_SUCCESS) {
      ret = patch_status;
      if (input_error) { is_cover_error = false; }
      else { SET_COVER_ERROR(ret); }
      if (ret == OB_ERR_JSON_SYNTAX_ERROR) { ret = OB_ERR_JSON_PATCH_INVALID; }
    } else if (patch_null) {
      ret = OB_ERR_JSON_PATCH_INVALID;
      is_cover_error = false;
      LOG_USER_ERROR(OB_ERR_JSON_PATCH_INVALID);
    }
  }

  if (OB_FAIL(ret)) {
    if (is_cover_error && err_type) {
      res.set_null();
      ret = OB_SUCCESS;
    }
  } else if (OB_SUCC(ret)) {
    common::json::MergeOutput merged = common::json::finish_merge(std::move(merger));
    if (merged.status != 0) {
      ret = merged.status == 1 ? OB_ALLOCATE_MEMORY_FAILED : OB_ERR_UNEXPECTED;
    } else if (merged.is_null) {
      res.set_null();
    } else {
      std::optional<common::json::FormatResult> formatted;
      ObString res_string;
      bool is_res_blob = expr.datum_meta_.cs_type_ == CS_TYPE_BINARY && dst_type == ObLongTextType;

      const rust::Slice<const uint8_t> binary(merged.binary.data(), merged.binary.size());
      if (dst_type == ObJsonType) {
        res_string.assign_ptr(reinterpret_cast<const char *>(merged.binary.data()),
                              static_cast<ObString::obstr_size_t>(merged.binary.size()));
      } else {
        ObString tmp_val;
        ObString result_str;
        const common::json::BinaryMetadataResult metadata = common::json::inspect_binary(binary, false);
        const bool is_quote = metadata.value_type == static_cast<uint8_t>(ObJsonNodeType::J_STRING);
        formatted.emplace(common::json::format_binary(binary, is_quote, is_pretty > 0, 0));
        if (formatted->platform_error != 0) {
          ret = formatted->platform_error;
        } else if (metadata.status != common::json::Status::Ok
            || formatted->status != common::json::Status::Ok || formatted->text.empty()) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          tmp_val.assign_ptr(reinterpret_cast<char *>(formatted->text.data()),
                             static_cast<ObString::obstr_size_t>(formatted->text.size()));
          ObCollationType in_cs_type = CS_TYPE_UTF8MB4_BIN;
          ObCollationType dst_cs_type = expr.obj_meta_.get_collation_type();
          result_str = tmp_val;
          
          if (OB_FAIL(ObJsonExprHelper::convert_string_collation_type(in_cs_type,
                                                                      dst_cs_type, 
                                                                      &temp_allocator,
                                                                      tmp_val,
                                                                      result_str))) {
          } else {
            tmp_val = result_str;
          }
        }

        
        if (OB_SUCC(ret) && is_asc && !is_res_blob /* clob varchar */ ) {
          if (OB_FAIL(ObJsonExprHelper::character2_ascii_string(&temp_allocator, expr, ctx, tmp_val, 1))) {
          }
        }
        
        
        if (is_trunc && dst_type != ObLongTextType) {
          if (tmp_val.length() > dst_len) {
            if (ob_is_string_type(dst_type)) {
              int64_t char_len; // not used
              int64_t real_dst_len;
              real_dst_len = ObCharset::max_bytes_charpos(expr.datum_meta_.cs_type_, tmp_val.ptr(),
                                                          tmp_val.length(), dst_len, char_len);

              tmp_val.assign_ptr(tmp_val.ptr(), real_dst_len);
              // Keep truncation at a valid character boundary.
              if (real_dst_len != dst_len && real_dst_len > 0) {
                // get last char
                // must be utf8
                bool append_quote = false;
                int64_t last_char_len = 0;
                if (OB_FAIL(ObCharset::last_valid_char(expr.datum_meta_.cs_type_,
                                                       tmp_val.ptr(), tmp_val.length(),
                                                       last_char_len))) {
                } else if (last_char_len == 1) {
                  if (real_dst_len > 1) {
                    if (OB_FAIL(ObCharset::last_valid_char(expr.datum_meta_.cs_type_,
                                                          tmp_val.ptr(), tmp_val.length() - 1,
                                                          last_char_len))) {
                    } else if (last_char_len == 1) {
                      if ((tmp_val.ptr()[real_dst_len - 1] == '"') && (tmp_val.ptr()[real_dst_len - 2] != '"')) {
                        append_quote = true;
                      }
                    }
                  } else {
                    append_quote = true;
                  }
                }
                if (OB_FAIL(ret)) {
                } else if (append_quote) {
                  // already reserve 1 byte calling character2_ascii_string
                  *(tmp_val.ptr() + tmp_val.length()) = '\"';
                  tmp_val.assign_ptr(tmp_val.ptr(), tmp_val.length() + 1);
                }
              }
            } else {
              tmp_val.assign_ptr(tmp_val.ptr(), dst_len);
            }
          }
        }
        res_string.assign_ptr(tmp_val.ptr(), tmp_val.length());
      }

      if (OB_SUCC(ret)) {
        uint64_t length = res_string.length();
        if (dst_type == ObVarcharType && length > dst_len) {
          char res_ptr[OB_MAX_DECIMAL_PRECISION] = {0};
          if (OB_ISNULL(ObCharset::lltostr(dst_len, res_ptr, 10, 1))) {
            ret = OB_ERR_UNEXPECTED;
          }
          if (!err_type) { 
            ret = OB_ERR_VALUE_EXCEEDED_MAX;
            LOG_USER_ERROR(OB_ERR_VALUE_EXCEEDED_MAX, static_cast<int>(length), static_cast<int>(dst_len));
          } else {
            ret = OB_SUCCESS;
            res.set_null();
          }
        } else if (OB_FAIL(ObJsonExprHelper::pack_json_str_res(expr, ctx, res, res_string))) {
        }
      }
    }
  }


  return ret;
}

int ObExprJsonMergePatch::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_json_merge_patch;
  return OB_SUCCESS;
}

}
}
