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

#include "sql/engine/expr/ob_expr_lrpad.h"
#include "sql/engine/ob_exec_context.h"

#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
using namespace oceanbase::common;
using namespace oceanbase::sql;
namespace sql
{
/* ObExprBaseLRpad {{{1 */
/* common util {{{2 */
ObExprBaseLRpad::ObExprBaseLRpad(ObIAllocator &alloc,
                                 ObExprOperatorType type,
                                 const char *name,
                                 int32_t param_num)
    : ObStringExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL)
{
}

ObExprBaseLRpad::~ObExprBaseLRpad()
{
}
// Reference ObExprBaseLRpad::calc_mysql
int ObExprBaseLRpad::calc_type_length_mysql(const ObExprResType result_type,
                                            const ObObj &text,
                                            const ObObj &pad_text,
                                            const ObObj &len,
                                            const ObExprTypeCtx &type_ctx,
                                            int64_t &result_size)
{
  int ret = OB_SUCCESS;
  int64_t max_result_size = OB_MAX_VARCHAR_LENGTH;
  int64_t int_len = 0;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;
  int64_t text_len = 0;

  ObString str_text;
  ObString str_pad;
  result_size = max_result_size;
  ObExprCtx expr_ctx;
  ObArenaAllocator allocator(common::ObModIds::OB_SQL_EXPR_CALC);
  expr_ctx.calc_buf_ = &allocator;
  max_result_size = type_ctx.get_max_allowed_packet();
  if (OB_FAIL(ret)) {
    //do nothing
  } else if (OB_FAIL(ObExprUtil::get_round_int64(len, expr_ctx, int_len))) {
    LOG_WARN("get_round_int64 failed and ignored", K(ret));
    ret = OB_SUCCESS;
  } else {
    if (!ob_is_string_type(text.get_type())) {
      result_size = int_len;
    } else if (OB_FAIL(text.get_string(str_text))) {
      LOG_WARN("Failed to get str_text", K(ret), K(text));
    } else if (FALSE_IT(text_len = ObCharset::strlen_char(
                result_type.get_collation_type(),
                const_cast<const char *>(str_text.ptr()),
                str_text.length()))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else if (text_len >= int_len) {
      // only substr needed
      result_size = ObCharset::charpos(result_type.get_collation_type(), str_text.ptr(), str_text.length(), int_len);
    } else {
      if (!ob_is_string_type(pad_text.get_type())) {
        result_size = int_len;
      } else if (OB_FAIL(pad_text.get_string(str_pad))) {
        LOG_WARN("Failed to get str_text", K(ret), K(pad_text));
      } else if (str_pad.length() == 0) {
        result_size = int_len;
      } else if (OB_FAIL(get_padding_info_mysql(
                result_type.get_collation_type(), str_text, int_len, str_pad,
                max_result_size, repeat_count, prefix_size, result_size))) {
        LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(int_len), K(str_pad), K(max_result_size));
      } else {
        result_size = str_text.length() + str_pad.length() * repeat_count + prefix_size;
      }
    }
  }
  if (result_size > max_result_size) {
    result_size = max_result_size;
  }
  return ret;
}
// Reference ObExprBaseLRpad::calc_oracle

int ObExprBaseLRpad::get_origin_len_obj(ObObj &len_obj) const
{
  int ret = OB_SUCCESS;
  ObRawExpr *expr = NULL;
  if (OB_ISNULL(expr = get_raw_expr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to get_raw_expr", K(ret));
  } else if (expr->get_param_count() >= 2 && OB_NOT_NULL(expr = expr->get_param_expr(1))
             && expr->get_expr_type() == T_FUN_SYS_CAST && CM_IS_IMPLICIT_CAST(expr->get_cast_mode())) {
    do {
      if (expr->get_param_count() >= 1
          && OB_ISNULL(expr = expr->get_param_expr(0))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("fail to get_param_expr", K(ret));
      }
    } while (OB_SUCC(ret) && T_FUN_SYS_CAST == expr->get_expr_type()
             && CM_IS_IMPLICIT_CAST(expr->get_cast_mode()));
    if (OB_FAIL(ret)) {
    } else if (!expr->is_const_raw_expr()) {
      len_obj.set_null();
    } else {
      len_obj = static_cast<ObConstRawExpr *>(expr)->get_param();
    }
  }
  return ret;
}

int ObExprBaseLRpad::calc_type(ObExprResType &type,
                               ObExprResType &text,
                               ObExprResType &len,
                               ObExprResType *pad_text,
                               ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ObObjType text_type = ObNullType;
  ObObjType len_type = ObNullType;
  int64_t max_len = OB_MAX_VARCHAR_LENGTH;
  int64_t text_len = text.get_length();
  if (lib::is_mysql_mode()) {
    len_type = ObIntType;
    text_type = ObVarcharType;
    max_len = OB_MAX_VARCHAR_LENGTH;
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error compat mode", K(ret));
  }

  const ObSQLSessionInfo *session = type_ctx.get_session();
  CK(OB_NOT_NULL(session));

  if (OB_SUCC(ret)) {
    ObObj length_obj = len.get_param();
    ObObj text_obj = text.get_param();
    ObObj pad_obj;
    ObString default_pad_str = ObString(" "); // default is ' '
    type.set_length(static_cast<ObLength>(max_len));
    len.set_calc_type(len_type);
    if (is_mysql_mode()) {
      pad_obj = pad_text->get_param();
      ObSEArray<ObExprResType, 2> types;
      OZ(types.push_back(text));
      OZ(types.push_back(*pad_text));
      OZ(aggregate_charsets_for_string_result(type, &types.at(0), 2, type_ctx));
      OX(text.set_calc_collation_type(type.get_collation_type()));
      OX(pad_text->set_calc_collation_type(type.get_collation_type()));
      if (OB_SUCC(ret)) {
        // len expr may add cast, search real len obj
        if (OB_FAIL(get_origin_len_obj(length_obj))) {
          LOG_WARN("fail to get ori len obj", K(ret));
        } else if (!length_obj.is_null()) {
          if (OB_FAIL(calc_type_length_mysql(type, text_obj, pad_obj, length_obj, type_ctx, text_len))) {
            LOG_WARN("failed to calc result type length mysql mode", K(ret));
          }
        } else {
          text_len = max_len;
        }
        if (OB_SUCC(ret)) {
          text_type = get_result_type_mysql(text_len);
          type.set_type(text_type);
          if (!ob_is_text_tc(text.get_type())) {
            text.set_calc_type(text_type);
          }
          if (!ob_is_text_tc(pad_text->get_type())) {
            pad_text->set_calc_type(text_type);
          }
        }
      }
    } else {
      ObSEArray<ObExprResType*, 2> types;
      OZ(types.push_back(&text));
      OZ(aggregate_string_type_and_charset_oracle(*session, types, type,
          PREFER_VAR_LEN_CHAR | PREFER_NLS_LENGTH_SEMANTICS));
      if (NULL != pad_text) {
        OZ(types.push_back(pad_text));
        OX(pad_obj = pad_text->get_param());
      } else {
        OX(pad_obj.set_string(ObVarcharType, default_pad_str));
        OX(pad_obj.set_collation_type(ObCharset::get_system_collation()));
      }
      OZ(deduce_string_param_calc_type_and_charset(*session, type, types));
    }

    const int64_t buf_len = pad_obj.is_character_type() ? pad_obj.get_string_len() * 4 : 0;
    char buf[buf_len];

    if (OB_SUCC(ret)) {
      if (length_obj.is_null()) {
        text_len = max_len;
      }
      text_len = (text_len > max_len)? max_len: text_len;
      type.set_length(static_cast<ObLength>(text_len));

      LOG_DEBUG("ObExprBaseLRpad::calc_type()", K(ret), K(text), K(text_obj), K(pad_obj), K(len), K(length_obj), KP(pad_text),
                                K(type), K(text_len), K(max_len), K(type.get_length_semantics()));
    }
  }
  return ret;
}

int ObExprBaseLRpad::padding_inner(LRpadType type,
                                   const char *text,
                                   const int64_t &text_size,
                                   const char *pad,
                                   const int64_t &pad_size,
                                   const int64_t &prefix_size,
                                   const int64_t &repeat_count,
                                   const bool &pad_space,
                                   ObString &space_str,
                                   char* &result)
{
  int ret = OB_SUCCESS;
  char *text_start_pos = NULL;
  char *pad_start_pos = NULL;
  char *sp_start_pos = NULL;
  if (OB_ISNULL(result)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("Failed to alloc", K(ret));
  } else if (type == LPAD_TYPE) {
    // lpad: [sp] + padtext * t + padprefix + text
    if (pad_space) {
      sp_start_pos = result;
      pad_start_pos = result + space_str.length();
    } else {
      pad_start_pos = result;
    }
    text_start_pos = pad_start_pos + (pad_size * repeat_count + prefix_size);
  } else if (type == RPAD_TYPE) {
    // rpad: text + padtext * t + padprefix + [sp]
    text_start_pos = result;
    pad_start_pos = text_start_pos + text_size;
    if (pad_space) {
      sp_start_pos = pad_start_pos + (pad_size * repeat_count + prefix_size);
    }
  }

  if (OB_SUCC(ret)) {
    // place pad string
    for (int64_t i = 0; i < repeat_count; i++) {
      MEMCPY(pad_start_pos + i * pad_size, pad, pad_size);
    }

    // place pad string prefix
    MEMCPY(pad_start_pos + repeat_count * pad_size, pad, prefix_size);

    // place text string
    MEMCPY(text_start_pos, text, text_size);
    if (pad_space) {
      MEMCPY(sp_start_pos, space_str.ptr(), space_str.length());
    }
  }
  return ret;
}

int ObExprBaseLRpad::padding(LRpadType type,
                             const ObCollationType coll_type,
                             const char *text,
                             const int64_t &text_size,
                             const char *pad,
                             const int64_t &pad_size,
                             const int64_t &prefix_size,
                             const int64_t &repeat_count,
                             const bool &pad_space, // for oracle
                             ObIAllocator *allocator,
                             char* &result,
                             int64_t &size,
                             ObObjType res_type,
                             bool has_lob_header)
{
  int ret = OB_SUCCESS;
  ObString space_str = ObCharsetUtils::get_const_str(coll_type, ' ');
  // start pos
  char *text_start_pos = NULL;
  char *pad_start_pos = NULL;
  char *sp_start_pos = NULL;

  size = text_size + pad_size * repeat_count + prefix_size + (pad_space? space_str.length(): 0);
  if (OB_UNLIKELY(size <= 0)
      || OB_UNLIKELY(repeat_count < 0)
      || OB_UNLIKELY(pad_size <= 0)
      || OB_UNLIKELY(prefix_size >= pad_size)
      || (OB_ISNULL(text) && text_size != 0)
      || OB_ISNULL(pad)
      || OB_ISNULL(allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Wrong param", K(ret), K(size), K(repeat_count), K(pad_size), K(prefix_size),
             K(text), K(text_size), K(pad), K(allocator));
  } else {
    if (!ob_is_text_tc(res_type)) {
      result = static_cast<char *>(allocator->alloc(size));
      // result validation inside padding_inner
      ret = padding_inner(type, text, text_size, pad, pad_size, prefix_size,
                          repeat_count, pad_space, space_str, result);
    } else { // res is text tc
      ObTextStringResult result_buffer(res_type, has_lob_header, allocator);
      int64_t buffer_len = 0;
      if (OB_FAIL(result_buffer.init(size))) {
        LOG_WARN("init stringtextbuffer failed", K(ret), K(size));
      } else if (OB_FAIL(result_buffer.get_reserved_buffer(result, buffer_len))) {
        LOG_WARN("get empty buffer len failed", K(ret), K(buffer_len));
      } else if (OB_FAIL(padding_inner(type, text, text_size, pad, pad_size, prefix_size,
                                       repeat_count, pad_space, space_str, result))) {
      } else if (OB_FAIL(result_buffer.lseek(buffer_len, 0))) {
        LOG_WARN("temp lob lseek failed", K(ret));
      } else {
        ObString output;
        result_buffer.get_result_buffer(output);
        result = output.ptr();
        size = output.length();
      }
    }
  }
  return ret;
}

int ObExprBaseLRpad::get_padding_info_mysql(const ObCollationType &cs,
                                            const ObString &str_text,
                                            const int64_t &len,
                                            const ObString &str_padtext,
                                            const int64_t max_result_size,
                                            int64_t &repeat_count,
                                            int64_t &prefix_size,
                                            int64_t &size)
{
  // lpad: [sp] + padtext * t + padprefix + text
  // rpad: text + padtext * t + padprefix + [sp]
  int ret = OB_SUCCESS;
  int64_t text_size = str_text.length();
  int64_t pad_size = str_padtext.length();

  // GOAL: get repeat_count, prefix_size and pad space.
  int64_t text_len = ObCharset::strlen_char(cs, const_cast<const char *>(str_text.ptr()), str_text.length());
  int64_t pad_len = ObCharset::strlen_char(cs, const_cast<const char *>(str_padtext.ptr()), str_padtext.length());

  if (OB_UNLIKELY(len <= text_len)
             || OB_UNLIKELY(len <= 0)
             || OB_UNLIKELY(pad_len <= 0)
             || OB_UNLIKELY(pad_size <= 0)) {
    // this should been resolve outside
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong len", K(ret), K(len), K(text_len), K(pad_len), K(pad_size));
  } else {
    repeat_count = std::min((len - text_len) / pad_len, (max_result_size - text_size) / pad_size);
    int64_t remain_len = len - (text_len + pad_len * repeat_count);
    prefix_size = ObCharset::charpos(cs, const_cast<const char *>(str_padtext.ptr()),
                                     str_padtext.length(), remain_len);

    size = text_size + pad_size * repeat_count + prefix_size;
  }
  return ret;
}

// for engine 3.0
int ObExprBaseLRpad::calc_mysql_pad_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                           LRpadType pad_type, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *text = NULL;
  ObDatum *len = NULL;
  ObDatum *pad_text = NULL;
  if (OB_UNLIKELY(3 != expr.arg_cnt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("arg cnt must be 3", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, text, len, pad_text))) {
    LOG_WARN("eval param value failed", K(ret));
  } else if (OB_ISNULL(text) || OB_ISNULL(len) || OB_ISNULL(pad_text)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected datum", K(ret), KP(text), KP(len), KP(pad_text));
  } else {
    const ObSQLSessionInfo *session = ctx.exec_ctx_.get_my_session();
    ObExprStrResAlloc res_alloc(expr, ctx); // make sure alloc() is called only once
    if (OB_ISNULL(session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is NULL", K(ret));
    } else if (OB_FAIL(calc_mysql(pad_type, expr, ctx, *text, *len, *pad_text, *session,
                                  res_alloc, res))) {
      LOG_WARN("calc_mysql failed", K(ret));
    }
  }
  return ret;
}

int ObExprBaseLRpad::calc_mysql_inner(const LRpadType pad_type,
                                      const ObExpr &expr,
                                      const ObDatum &len,
                                      int64_t &max_result_size,
                                      const ObString &str_text,
                                      const ObString &str_pad,
                                      ObIAllocator &res_alloc,
                                      ObDatum &res)
{
  int ret = OB_SUCCESS;
  int64_t repeat_count = 0;
  int64_t prefix_size = 0;
  int64_t text_len = 0;

  char *result_ptr = NULL;
  int64_t result_size = 0;
  const ObCollationType cs_type = expr.datum_meta_.cs_type_;
  const ObObjType type = expr.datum_meta_.type_;
  bool has_lob_header = expr.obj_meta_.has_lob_header();
  bool has_set_to_lob_locator = false;
  int64_t int_len = len.get_int();
  if (int_len < 0) {
    res.set_null();
  } else if (int_len == 0) {
    res.set_string(ObString::make_empty_string());
  } else if (FALSE_IT(text_len = ObCharset::strlen_char(cs_type,
             const_cast<const char *>(str_text.ptr()), str_text.length()))) {
    LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
  } else if (text_len >= int_len ) {
    // only substr needed
    result_size = ObCharset::charpos(cs_type, str_text.ptr(), str_text.length(), int_len);
    res.set_string(ObString(result_size, str_text.ptr()));
  } else if (str_pad.length() == 0) {
    res.set_null(); // mysql 5.7 return null while mysql 8.0 return empty string
  } else {
    has_set_to_lob_locator = true;
    if (OB_FAIL(get_padding_info_mysql(cs_type, str_text, int_len, str_pad,
                max_result_size, repeat_count, prefix_size, result_size))) {
      LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(int_len),
                                              K(str_pad), K(max_result_size));
    } else if (result_size > max_result_size) {
      res.set_null();
      if (pad_type == RPAD_TYPE) {
        LOG_USER_WARN(OB_ERR_FUNC_RESULT_TOO_LARGE, "rpad", static_cast<int>(max_result_size));
      } else {
        LOG_USER_WARN(OB_ERR_FUNC_RESULT_TOO_LARGE, "lpad", static_cast<int>(max_result_size));
      }
    } else if (OB_FAIL(padding(pad_type, cs_type, str_text.ptr(), str_text.length(), str_pad.ptr(),
                                str_pad.length(), prefix_size, repeat_count, false, &res_alloc,
                                result_ptr, result_size, type, has_lob_header))) {
      LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size), K(repeat_count));
    } else {
      if (NULL == result_ptr || 0 == result_size) {
        res.set_null();
      } else {
        res.set_string(result_ptr, result_size);
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (ob_is_text_tc(type) && !res.is_null() &&
      has_lob_header && !has_set_to_lob_locator) {
    ObString data = res.get_string();
    ObTextStringResult result_buffer(type, has_lob_header, &res_alloc);
    int64_t buffer_len = 0;
    if (OB_FAIL(result_buffer.init(data.length()))) {
      LOG_WARN("init stringtextbuffer failed", K(ret), K(data));
    } else if (OB_FAIL(result_buffer.append(data))) {
      LOG_WARN("temp lob lseek failed", K(ret));
    } else {
      ObString output;
      result_buffer.get_result_buffer(output);
      res.set_string(output);
    }
  } else if (ob_is_text_tc(expr.args_[0]->datum_meta_.type_) && ob_is_string_tc(type)
      && ! has_set_to_lob_locator && ! res.is_null()) {
    // in mysql mode, input is lob, but output is varchar
    // if lob is outrow, may cause return data that's allocated from tmp allocator
    ObString data = res.get_string();
    ObString output;
    if (data.empty()) { // skip empty string
    } else if (OB_FAIL(ob_write_string(res_alloc, data, output))) {
      LOG_WARN("ob_write_string fail", K(ret));
    } else {
      res.set_string(output);
    }
  }
  return ret;
}

int ObExprBaseLRpad::calc_mysql(const LRpadType pad_type, const ObExpr &expr, ObEvalCtx &ctx, 
                                const ObDatum &text, const ObDatum &len, const ObDatum &pad_text,
                                const ObSQLSessionInfo &session, ObIAllocator &res_alloc,
                                ObDatum &res)
{
  int ret = OB_SUCCESS;
  int64_t max_result_size = -1;
  ObSolidifiedVarsGetter helper(expr, ctx, &session);
  if (OB_FAIL(helper.get_max_allowed_packet(max_result_size))) {
    LOG_WARN("Failed to get max allow packet size", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (text.is_null() || len.is_null() || pad_text.is_null()) {
    res.set_null();
  } else {
    if (!ob_is_text_tc(expr.args_[0]->datum_meta_.type_)
        && !ob_is_text_tc(expr.args_[2]->datum_meta_.type_)) {
      const ObString &str_text = text.get_string();
      const ObString &str_pad = pad_text.get_string();
      if (OB_FAIL(calc_mysql_inner(pad_type, expr, len, max_result_size, 
                                   str_text, str_pad, res_alloc, res))) {
        LOG_WARN("Failed to eval base lrpad", K(ret));
      }
    } else { // text tc
      ObEvalCtx::TempAllocGuard tmp_alloc_g(ctx);
      common::ObArenaAllocator &temp_allocator = tmp_alloc_g.get_allocator();
      ObString str_text;
      ObString str_pad;
      if (OB_FAIL(ObTextStringHelper::read_real_string_data(temp_allocator,
                                                            text,
                                                            expr.args_[0]->datum_meta_,
                                                            expr.args_[0]->obj_meta_.has_lob_header(),
                                                            str_text))) {
        LOG_WARN("failed to read lob data text", K(ret), K(text));
      } else if (OB_FAIL(ObTextStringHelper::read_real_string_data(temp_allocator,
                                                                   pad_text,
                                                                   expr.args_[2]->datum_meta_,
                                                                   expr.args_[2]->obj_meta_.has_lob_header(),
                                                                   str_pad))) { 
        LOG_WARN("failed to read lob data pattern", K(ret), K(pad_text));
      } else if (OB_FAIL(calc_mysql_inner(pad_type, expr, len, max_result_size,
                                          str_text, str_pad, res_alloc, res))) {
        LOG_WARN("Failed to eval base lrpad", K(ret));
      }
    }
  }
  return ret;
}
/* mysql util END }}} */

int ObExprBaseLRpad::get_padding_info_oracle(const ObCollationType cs,
                                             const ObString &str_text,
                                             const int64_t &width,
                                             const ObString &str_padtext,
                                             const int64_t max_result_size,
                                             int64_t &repeat_count,
                                             int64_t &prefix_size,
                                             bool &pad_space)
{
  // lpad: [sp] + padtext * t + padprefix + text
  // rpad: text + padtext * t + padprefix + [sp]
  int ret = OB_SUCCESS;
  int64_t text_size = str_text.length();
  int64_t pad_size = str_padtext.length();

  int64_t text_width = 0;
  int64_t pad_width = 0;
  pad_space = false;

  // GOAL: get repeat_count, prefix_size and pad space.
  if (OB_FAIL(ObCharset::display_len(cs, str_text, text_width))) {
    LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
  } else if (OB_FAIL(ObCharset::display_len(cs, str_padtext, pad_width))) {
    LOG_WARN("Failed to get displayed length", K(ret), K(str_padtext));
  } else if (OB_UNLIKELY(width <= text_width)
             || OB_UNLIKELY(width <= 0)
             || OB_UNLIKELY(pad_size <= 0)
             || OB_UNLIKELY(pad_width <= 0)) {
    // this should been resolve outside
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("wrong width", K(ret), K(width), K(text_width), K(pad_size), K(pad_width));
  } else {
    repeat_count = std::min((width - text_width) / pad_width, (max_result_size - text_size) / pad_size);
    int64_t remain_width = width - (text_width + repeat_count * pad_width);
    int64_t remain_size = max_result_size - (text_size + repeat_count * pad_size);
    int64_t total_width = 0;
    LOG_DEBUG("calc pad", K(remain_width), K(width), K(text_width), K(pad_width),
              K(max_result_size), K(text_size), K(pad_size), K(ret), K(remain_size));

    if (remain_width > 0 && remain_size > 0) {
      // has pad prefix or pad space
      if (OB_FAIL(ObCharset::max_display_width_charpos(
                  cs, str_padtext.ptr(), std::min(remain_size, pad_size),
                  remain_width, prefix_size, &total_width))) {
        LOG_WARN("Failed to get max display width", K(ret), K(str_text), K(remain_width));
      } else if (remain_width != total_width && remain_size != prefix_size) {
        // Not reached the specified width, add a space
        pad_space = true;
      }
    }

  }
  return ret;
}

// for engine 3.0

int ObExprBaseLRpad::calc_oracle(LRpadType pad_type, const ObExpr &expr,
                                 const ObDatum &text, const ObDatum &len,
                                 const ObDatum &pad_text, ObIAllocator &res_alloc,
                                 ObDatum &res, bool &is_unchanged_clob)
{
  int ret = OB_SUCCESS;
  ObObjType res_type = expr.datum_meta_.type_;
  if (text.is_null() || len.is_null() || pad_text.is_null()) {
    res.set_null();
  } else {
    int64_t width = 0;
    int64_t repeat_count = 0;
    int64_t prefix_size = 0;
    bool pad_space = false;
    int64_t text_width = 0;
    const ObString &str_text = text.get_string();
    const ObString &str_pad = pad_text.get_string();
    const ObCollationType cs_type = expr.datum_meta_.cs_type_;

    // Max VARCHAR2 size is 32767 in Oracle PL/SQL.
    // However, Oracle SQL allow user to determine max VARCHAR2 size through `MAX_STRING_SIZE` parameter.
    // The max VARCHAR2 size is 4000 when `MAX_STRING_SIZE` is set to `STANDARD`, and 32767 when set to `EXTENDED`.
    // OB does not support `MAX_STRING_SIZE` parameter for now, but behaves compatibly with `EXTENDED` mode.
    const ObExprOracleLRpadInfo *info = nullptr;
    int64_t max_varchar2_size = OB_MAX_ORACLE_VARCHAR_LENGTH;
    if (OB_NOT_NULL(info = static_cast<ObExprOracleLRpadInfo *>(expr.extra_info_)) &&
        info->is_called_in_sql_) {
      const int64_t ORACLE_EXTENDED_MAX_VARCHAR_LENGTH = 32767;
      max_varchar2_size = ORACLE_EXTENDED_MAX_VARCHAR_LENGTH;
    }
    int64_t max_result_size = ob_is_text_tc(expr.datum_meta_.type_)
        ? OB_MAX_LONGTEXT_LENGTH
        : max_varchar2_size;

    number::ObNumber len_num(len.get_number());
    int64_t decimal_parts = -1;
    if (len_num.is_negative()) {
      width = -1;
    } else if (!len_num.is_int_parts_valid_int64(width, decimal_parts)) {
      // LOB max is 4G, here use UINT32_MAX.
      // Negative numbers have already been filtered out.
      width = UINT32_MAX;
    }
    if (width <= 0) {
      res.set_null();
    } else if (OB_FAIL(ObCharset::display_len(cs_type, str_text, text_width))) {
      LOG_WARN("Failed to get displayed length", K(ret), K(str_text));
    } else if ((3 == expr.arg_cnt_)
               && expr.args_[0]->datum_meta_.is_clob()
               && (0 == str_pad.length())
               && (text_width <= width)) {
      // pad_text is empty_clob, text is clob, if not going through truncation logic, the result is directly set to the original clob
      res.set_datum(text);
      is_unchanged_clob = ob_is_text_tc(res_type);
    } else if (text_width == width) {
      res.set_datum(text);
      is_unchanged_clob = ob_is_text_tc(res_type);
    } else {
      char *result_ptr = NULL;
      int64_t result_size = 0;
      bool has_lob_header = expr.obj_meta_.has_lob_header();
      if (text_width > width) {
        // substr
        int64_t total_width = 0;
        if (OB_FAIL(ObCharset::max_display_width_charpos(cs_type, str_text.ptr(),
                str_text.length(), width, prefix_size, &total_width))) {
          LOG_WARN("Failed to get max display width", K(ret));
        } else if (OB_FAIL(padding(pad_type, cs_type, "", 0, str_text.ptr(), str_text.length(),
                                    prefix_size, 0, (total_width != width), &res_alloc,
                                    result_ptr, result_size, res_type, has_lob_header))) {
          LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size),
                                    K(repeat_count), K(pad_space));
        }
      } else if (OB_FAIL(get_padding_info_oracle(cs_type, str_text, width, str_pad,
                  max_result_size, repeat_count, prefix_size, pad_space))) {
        LOG_WARN("Failed to get padding info", K(ret), K(str_text), K(width),
                                              K(str_pad), K(max_result_size));
      } else if (OB_FAIL(padding(pad_type, cs_type, str_text.ptr(), str_text.length(), str_pad.ptr(),
                                str_pad.length(), prefix_size, repeat_count, pad_space,
                                &res_alloc, result_ptr, result_size, res_type, has_lob_header))) {
        LOG_WARN("Failed to pad", K(ret), K(str_text), K(str_pad), K(prefix_size),
                                  K(repeat_count), K(pad_space));
      }

      if (OB_SUCC(ret)) {
        if (NULL == result_ptr || 0 == result_size) {
          res.set_null();
        } else {
          res.set_string(result_ptr, result_size);
        }
      }
    }
  }
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprBaseLRpad, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(2);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_MAX_ALLOWED_PACKET);
  EXPR_ADD_LOCAL_SYSVAR(SYS_VAR_COLLATION_CONNECTION);
  return ret;
}

/* oracle util END }}} */
/* ObExprBaseLRpad END }}} */

/* ObExprLpad {{{1 */
ObExprLpad::ObExprLpad(ObIAllocator &alloc)
  : ObExprBaseLRpad(alloc, T_FUN_SYS_LPAD, N_LPAD, 3)
{
}

ObExprLpad::~ObExprLpad()
{
}

int ObExprLpad::calc_result_type3(ObExprResType &type,
                                  ObExprResType &text,
                                  ObExprResType &len,
                                  ObExprResType &pad_text,
                                  ObExprTypeCtx &type_ctx) const
{
  return ObExprBaseLRpad::calc_type(type, text, len, &pad_text, type_ctx);
}

int ObExprLpad::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_mysql_lpad_expr;
  return ret;
}

int ObExprLpad::calc_mysql_lpad_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                          ObDatum &res)
{
  return calc_mysql_pad_expr(expr, ctx, LPAD_TYPE, res);
}
/* ObExprLpad END }}} */

/* ObExprRpad {{{1 */
ObExprRpad::ObExprRpad(ObIAllocator &alloc)
  : ObExprBaseLRpad(alloc, T_FUN_SYS_RPAD, N_RPAD, 3)
{
}

ObExprRpad::ObExprRpad(ObIAllocator &alloc,
                       ObExprOperatorType type,
                       const char *name)
    : ObExprBaseLRpad(alloc, type, name, 3)
{
}

ObExprRpad::~ObExprRpad()
{
}

int ObExprRpad::calc_result_type3(ObExprResType &type,
                                  ObExprResType &text,
                                  ObExprResType &len,
                                  ObExprResType &pad_text,
                                  ObExprTypeCtx &type_ctx) const
{
  return ObExprBaseLRpad::calc_type(type, text, len, &pad_text, type_ctx);
}

int ObExprRpad::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = calc_mysql_rpad_expr;
  return ret;
}

int ObExprRpad::calc_mysql_rpad_expr(const ObExpr &expr, ObEvalCtx &ctx,
                                           ObDatum &res)
{
  return calc_mysql_pad_expr(expr, ctx, RPAD_TYPE, res);
}
/* ObExprRpad END }}} */

OB_DEF_SERIALIZE(ObExprOracleLRpadInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_ENCODE, is_called_in_sql_);
  return ret;
}

OB_DEF_DESERIALIZE(ObExprOracleLRpadInfo)
{
  int ret = OB_SUCCESS;
  LST_DO_CODE(OB_UNIS_DECODE, is_called_in_sql_);
  return ret;
}

OB_DEF_SERIALIZE_SIZE(ObExprOracleLRpadInfo)
{
  int64_t len = 0;
  LST_DO_CODE(OB_UNIS_ADD_LEN, is_called_in_sql_);
  return len;
}

int ObExprOracleLRpadInfo::deep_copy(common::ObIAllocator &allocator,
                                     const ObExprOperatorType type,
                                     ObIExprExtraInfo *&copied_info) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ObExprExtraInfoFactory::alloc(allocator, type, copied_info))) {
    LOG_WARN("Failed to allocate memory for ObExprOracleLRpadInfo", K(ret));
  } else if (OB_ISNULL(copied_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("extra_info should not be nullptr", K(ret));
  } else {
    ObExprOracleLRpadInfo *other = static_cast<ObExprOracleLRpadInfo *>(copied_info);
    other->is_called_in_sql_ = is_called_in_sql_;
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
