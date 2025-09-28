/**
 * Copyright (c) 2021 OceanBase
 * OceanBase CE is licensed under Mulan PubL v2.
 * You can use this software according to the terms and conditions of the Mulan PubL v2.
 * You may obtain a copy of Mulan PubL v2 at:
 *          http://license.coscl.org.cn/MulanPubL-2.0
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PubL v2 for more details.
 */

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_substrb.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprSubstrb::ObExprSubstrb(ObIAllocator &alloc)
    : ObStringExprOperator(alloc, T_FUN_SYS_SUBSTRB, N_SUBSTRB, TWO_OR_THREE, VALID_FOR_GENERATED_COL)
{
}

ObExprSubstrb::~ObExprSubstrb()
{
}


int ObExprSubstrb::calc_result_typeN(ObExprResType &type,
                                    ObExprResType *types_array,
                                    int64_t param_num,
                                    ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  ret = OB_ERR_FUNCTION_UNKNOWN;
  LOG_WARN("substrb is only for oracle mode", K(ret));
  return ret;
}

int ObExprSubstrb::calc(ObString &res_str, const ObString &text,
                        int64_t start, int64_t length, ObCollationType cs_type,
                        ObIAllocator &alloc)
{
  int ret = OB_SUCCESS;
  res_str.reset();
  int64_t text_len = text.length();
  int64_t res_len = 0;
  if (OB_UNLIKELY(0 >= text_len || 0 >= length)) {
    // empty result string
    res_str.reset();
  } else if (OB_ISNULL(text.ptr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("text.ptr() is null", K(ret));
  } else {
    if (0 == start) {
      start = 1;// 
    }
    start = (start > 0) ? (start - 1) : (start + text_len);
    if (OB_UNLIKELY(start < 0 || start >= text_len)) {
      res_str.reset();
    } else {
      char* buf = static_cast<char *>(alloc.alloc(text_len));
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_WARN("alloc memory failed", K(text_len), K(ret));
      } else {
        MEMCPY(buf, text.ptr(), text_len);
        res_len = min(length, text_len - start);
        // 标准Oracle会将非法的byte设置为空格
        if (OB_FAIL(handle_invalid_byte(buf, text_len, start, res_len, ' ', cs_type, false))) {
          LOG_WARN("handle invalid byte failed", K(start), K(res_len), K(cs_type));
        } else {
          res_str.assign_ptr(buf + start, static_cast<int32_t>(res_len));
        }
      }
    }
  }
  LOG_DEBUG("calc substrb done", K(ret), K(res_str));
  return ret;
}

int ObExprSubstrb::handle_invalid_byte(char* ptr,
                                       const int64_t text_len,
                                       int64_t &start,
                                       int64_t &len,
                                       char reset_char,
                                       ObCollationType cs_type,
                                       const bool force_ignore_invalid_byte)
{
  int ret = OB_SUCCESS;
  int64_t mbminlen = 0;
  if (OB_FAIL(ObCharset::get_mbminlen_by_coll(cs_type, mbminlen))) {
    LOG_WARN("get mbminlen failed", K(cs_type), K(ret));
  } else {
    if (force_ignore_invalid_byte || mbminlen > 1) { // utf16: mbminlen is 2
      if (OB_FAIL(ignore_invalid_byte(ptr, text_len, start, len, cs_type))) {
        LOG_WARN("ignore_invalid_byte failed", K(ret));
      }
    } else { // utf8/gbk/gb18030: mbminlen is 1
      if (OB_FAIL(reset_invalid_byte(ptr, text_len, start, len, reset_char, cs_type))) {
        LOG_WARN("reset_invalid_byte failed", K(ret));
      }
    }

    if (OB_SUCC(ret) && OB_UNLIKELY(len % mbminlen != 0)) {
      // 防御性代码，防止hang
      // eg: a -> '0061'
      //     substrb(utf16_a, 2, 1), well_formed_len()方法认为'61'是一个合法的utf16字符
      //     是不符合预期的，这是我们底层字符集函数的缺陷
      //     由于不确定well_formed_len()是否有其他坑，这里进行防御，防止hang
      int64_t hex_len = 0;
      char hex_buf[1024] = {0}; // just print 512 bytes
      OZ(common::hex_print(ptr + start, len, hex_buf, sizeof(hex_buf), hex_len));
      if (OB_SUCC(ret)) {
        const char *charset_name = ObCharset::charset_name(cs_type);
        int64_t charset_name_len = strlen(charset_name);
        ret = OB_ERR_INVALID_CHARACTER_STRING;
        LOG_USER_ERROR(OB_ERR_INVALID_CHARACTER_STRING, static_cast<int>(charset_name_len),
                      charset_name, static_cast<int>(hex_len), hex_buf);
      }
    }
  }
  return ret;
}

int ObExprSubstrb::ignore_invalid_byte(char* ptr,
                                       const int64_t text_len,
                                       int64_t &start,
                                       int64_t &len,
                                       ObCollationType cs_type)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ptr is null", K(ret), K(len));
  } else if (0 > len) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("len should be greater than zero", K(len), K(ret));
  } else if (len == 0) {
    // do nothing
  } else {
    int64_t end = start + len;
    int64_t boundary_pos = 0;
    int64_t boundary_len = 0;
    OZ(get_well_formatted_boundary(
            cs_type, ptr, text_len, start, boundary_pos, boundary_len));
    if (OB_SUCC(ret)) {
      if (boundary_len < 0) {
        // invalid character found, do nothing.
      } else {
        if (start > boundary_pos) {
          start = boundary_pos + boundary_len;
        }
        if (start >= end) {
          len = 0;
        } else {
          len = end - start;
          OZ(get_well_formatted_boundary(
                  cs_type, ptr + start, text_len - start, len, boundary_pos, boundary_len));
          if (OB_SUCC(ret)) {
            if (boundary_len < 0) {
              // invalid character found, do nothing.
            } else {
              len = boundary_pos;
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObExprSubstrb::reset_invalid_byte(char* ptr,
                                      const int64_t text_len,
                                      int64_t start,
                                      int64_t len,
                                      char reset_char,
                                      ObCollationType cs_type)
{
  int ret = OB_SUCCESS;
  int64_t well_formatted_start = start;
  int64_t well_formatted_len = len;
  if (OB_ISNULL(ptr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ptr is null", K(ret), K(len));
  } else if (0 > len) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("len should be greater than zero", K(len), K(ret));
  } else if (0 == len) {
    // do nothing
  } else if (OB_FAIL(ignore_invalid_byte(
              ptr, text_len, well_formatted_start, well_formatted_len, cs_type))) {
    LOG_WARN("ignore invalid byte failed", K(ret));
  } else {
    for (int64_t i = start; i < well_formatted_start; i++) {
      ptr[i] = reset_char;
    }
    for (int64_t i = well_formatted_start + well_formatted_len; i < start + len; i++) {
      ptr[i] = reset_char;
    }
  }
  return ret;
}

int ObExprSubstrb::get_well_formatted_boundary(ObCollationType cs_type,
                                               char *ptr,
                                               const int64_t len,
                                               int64_t pos,
                                               int64_t &boundary_pos,
                                               int64_t &boundary_len)
{
  const int64_t MAX_CHAR_LEN = 8;
  int ret = OB_SUCCESS;
  if (pos < 0 || pos > len) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid position", K(ret), K(pos), K(len));
  } else if (NULL == ptr || len == 0) {
    boundary_pos = 0;
    boundary_len = 0;
  } else {
    int32_t error = 0;
    OZ(ObCharset::well_formed_len(cs_type, ptr, pos, boundary_pos, error));
    if (OB_SUCC(ret)) {
      boundary_len = 0;
      for (int64_t i = 1; OB_SUCC(ret) && i <= min(MAX_CHAR_LEN, len - boundary_pos); i++) {
        OZ(ObCharset::well_formed_len(cs_type, ptr + boundary_pos, i, boundary_len, error));
        if (OB_SUCC(ret)) {
          if (i == boundary_len) {
            break;
          }
        }
        boundary_len = 0;
      }
      if (boundary_pos + boundary_len < pos) {
        // invalid character found
        boundary_len = -1;
      }
    }
  }
  return ret;
}

int ObExprSubstrb::calc_substrb_expr(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res)
{
  int ret = OB_SUCCESS;
  ObDatum *src = NULL;
  ObDatum *start = NULL;
  ObDatum *len = NULL;
  if (OB_UNLIKELY(2 != expr.arg_cnt_ && 3 != expr.arg_cnt_)) {
    ret = OB_INVALID_ARGUMENT_NUM;
    LOG_WARN("arg_cnt must be 2 or 3", K(ret), K(expr.arg_cnt_));
  } else if (OB_FAIL(expr.eval_param_value(ctx, src, start, len))) {
    LOG_WARN("eval arg failed", K(ret));
  } else if (src->is_null() || start->is_null() || (3 == expr.arg_cnt_ && len->is_null())) {
    res.set_null();
  } else {
    const ObString &src_str = src->get_string();
    ObString res_str;
    int64_t start_int = 0;
    int64_t len_int = src_str.length();
    const ObCollationType &cs_type = expr.args_[0]->datum_meta_.cs_type_;
    ObExprStrResAlloc res_alloc(expr, ctx);
    if (expr.is_called_in_sql_ 
         && (OB_FAIL(ObExprUtil::trunc_num2int64(*start, start_int)) ||
        (3 == expr.arg_cnt_ && OB_FAIL(ObExprUtil::trunc_num2int64(*len, len_int))))) {
      LOG_WARN("trunc_num2int64 failed", K(ret));
    } else if (!expr.is_called_in_sql_  
                && (OB_FAIL(ObExprUtil::round_num2int64(*start, start_int))
                || (3 == expr.arg_cnt_ && OB_FAIL(ObExprUtil::round_num2int64(*len, len_int))))) {
      LOG_WARN("round_num2int64 failed", K(ret));
    } else if (!ob_is_text_tc(expr.args_[0]->datum_meta_.type_)) {
      if (OB_FAIL(calc(res_str, src_str, start_int, len_int, cs_type, res_alloc))) {
        LOG_WARN("calc substrb failed", K(ret), K(src_str), K(start_int), K(len_int), K(cs_type));
      } else if (res_str.empty() && !expr.args_[0]->datum_meta_.is_clob()) {
        res.set_null();
      } else {
        res.set_string(res_str);
      }
    } else { // text tc
      if (0 == start_int) {
        // 
        start_int = 1;
      }
      ObEvalCtx::TempAllocGuard alloc_guard(ctx);
      ObIAllocator &calc_alloc = alloc_guard.get_allocator();
      int64_t total_byte_len = 0;

      // read as binary
      const bool has_lob_header = expr.args_[0]->obj_meta_.has_lob_header();
      ObTextStringIter input_iter(expr.args_[0]->datum_meta_.type_, CS_TYPE_BINARY, src->get_string(), has_lob_header);
      if (OB_FAIL(input_iter.init(0, NULL, &calc_alloc))) {
        LOG_WARN("Lob: init input_iter failed ", K(ret), K(input_iter));
      } else if (OB_FAIL(input_iter.get_byte_len(total_byte_len))) {
        LOG_WARN("Lob: get input byte len failed", K(ret));
      } else {
        len_int = len == NULL ? total_byte_len : len_int;
      }
      int64_t result_byte_len = MIN((start_int >= 0 ? total_byte_len - start_int + 1 : -start_int), len_int);
      if (!input_iter.is_outrow_lob()) {
        // ObExprSubstrb::calc alloc memory from result buffer at least as input text len.
        result_byte_len = MAX(result_byte_len, total_byte_len);
      }
      ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &res);
      char *buf = NULL;
      int64_t buf_size = 0;
      if (OB_FAIL(ret)) {
      } else if (len_int < 0 || start_int > total_byte_len) {
        if (OB_FAIL(output_result.init(0))) { // fill empty lob result
          LOG_WARN("Lob: init stringtext result failed", K(ret));
        } else {
          output_result.set_result();
        }
      } else if (OB_FAIL(output_result.init(result_byte_len))) {
        LOG_WARN("Lob: init stringtext result failed", K(ret));
      } else if (OB_FAIL(output_result.get_reserved_buffer(buf, buf_size))) {
        LOG_WARN("Lob: stringtext result reserve buffer failed", K(ret));
      } else {
        input_iter.set_start_offset((start_int >= 0 ? (start_int - 1) : total_byte_len + start_int));
        input_iter.set_access_len(len_int);
        ObTextStringIterState state;
        ObString src_block_data;
        while (OB_SUCC(ret)
               && buf_size > 0
               && (state = input_iter.get_next_block(src_block_data)) == TEXTSTRING_ITER_NEXT) {
          ObDataBuffer data_buf(buf, buf_size);
          if (!input_iter.is_outrow_lob()) {
            ObString inrow_result;
            if (OB_FAIL(calc(inrow_result, src_block_data, start_int, len_int, cs_type, data_buf))) {
              LOG_WARN("get substr failed", K(ret));
            } else if (FALSE_IT(MEMMOVE(buf, inrow_result.ptr(), inrow_result.length()))) {
            } else if (OB_FAIL(output_result.lseek(inrow_result.length(), 0))) {
              LOG_WARN("Lob: append result failed", K(ret), K(output_result), K(src_block_data));
            }
          // outrow lobs, only use calc for handle invalid bytes
          } else {
            if (OB_FAIL(calc(res_str, src_block_data, 0, src_block_data.length(), cs_type, data_buf))) {
              LOG_WARN("calc substrb failed", K(ret), K(src_str), K(start_int), K(len_int), K(cs_type));
            } else if (OB_FAIL(output_result.lseek(res_str.length(), 0))) {
              LOG_WARN("result lseek failed", K(ret));
            } else {
              buf += res_str.length();
              buf_size -= res_str.length();
            }
          }
        }
        if (OB_FAIL(ret)) {
        } else if (state != TEXTSTRING_ITER_NEXT && state != TEXTSTRING_ITER_END) {
          ret = (input_iter.get_inner_ret() != OB_SUCCESS) ? 
                input_iter.get_inner_ret() : OB_INVALID_DATA;
          LOG_WARN("iter state invalid", K(ret), K(state), K(input_iter)); 
        } else {
          output_result.set_result();
        }
      }
    }
  }
  return ret;
}

int ObExprSubstrb::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
                        ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  LOG_WARN("is called sql", K(is_called_in_sql_));
  rt_expr.eval_func_ = calc_substrb_expr;
  return ret;
}

} /* sql */
} /* oceanbase */
