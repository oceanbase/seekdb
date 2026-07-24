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

#if defined(__x86_64__)
#include <immintrin.h>
#endif

#include "sql/engine/expr/ob_expr_lower.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/expr/ob_expr_lob_utils.h"

namespace oceanbase {
using namespace common;
namespace sql {

ObExprLowerUpper::ObExprLowerUpper(ObIAllocator &alloc, ObExprOperatorType type, const char *name, int32_t param_num)
    : ObStringExprOperator(alloc, type, name, param_num, VALID_FOR_GENERATED_COL)
{
}

ObExprLower::ObExprLower(ObIAllocator &alloc)
    : ObExprLowerUpper(alloc, T_FUN_SYS_LOWER, N_LOWER, 1)
{}

ObExprUpper::ObExprUpper(ObIAllocator &alloc)
    : ObExprLowerUpper(alloc, T_FUN_SYS_UPPER, N_UPPER, 1)
{}

int ObExprLowerUpper::calc_result_type1(ObExprResType &type, ObExprResType &text,
    common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = type_ctx.get_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else {
    if (ObTinyTextType == text.get_type()) {
      type.set_type(ObVarcharType);
    } else if (text.is_lob()) {
      type.set_type(ObLongTextType);
    } else {
      type.set_varchar();
    }
    text.set_calc_type(type.get_type());
    const common::ObLengthSemantics default_length_semantics = (OB_NOT_NULL(type_ctx.get_session())
        ? type_ctx.get_session()->get_actual_length_semantics()
        : common::LS_BYTE);
    ret = aggregate_charsets_for_string_result(type, &text, 1, type_ctx);
    OX(text.set_calc_collation_type(type.get_collation_type()));
    OX(text.set_calc_collation_level(type.get_collation_level()));
    OX(type.set_length(text.get_length()));
  }

  return ret;
}

// For NLS-aware lower/upper functions.
int ObExprLowerUpper::calc_result_typeN(ObExprResType &type,
                                        ObExprResType *texts,
                                        int64_t param_num,
                                        ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  const ObSQLSessionInfo *session = type_ctx.get_session();
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret));
  } else if (param_num <= 0) {
    ret = OB_ERR_NOT_ENOUGH_ARGS_FOR_FUN;
    LOG_WARN("nls_lower/nls_upper require at least one parameter", K(ret), K(param_num));
  } else if (param_num > 2) {
    ret = OB_ERR_TOO_MANY_ARGS_FOR_FUN;
    LOG_WARN("nls_lower/nls_upper require at most two parameters", K(ret), K(param_num));
  } else if (OB_ISNULL(texts)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("Invalid argument", K(ret), K(param_num), K(texts));
  } else {
    // Calculate based on the first parameter
    ObSEArray<ObExprResType*, 1, ObNullAllocator> param;
    OZ(param.push_back(&texts[0]));
    OZ(aggregate_string_type_and_charset_extended(*session, param, type));
    OZ(deduce_string_param_calc_type_and_charset(*session, type, param));
    OX(type.set_length(texts[0].get_calc_length() * ObCharset::MAX_CASE_MULTIPLY));
  }

  return ret;
}


int ObExprLower::calc(const ObCollationType cs_type, char *src, int32_t src_len,
                      char *dst, int32_t dst_len, int32_t &out_len) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src) || OB_ISNULL(dst)) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("src or dst is null", K(ret));
  } else {
    out_len = static_cast<int32_t>(ObCharset::casedn(cs_type, src, src_len, dst, dst_len));
  }
  return ret;
}


int32_t ObExprLower::get_case_mutiply(const ObCollationType cs_type) const
{
  int32_t mutiply_num = 0;
  if (OB_UNLIKELY(!ObCharset::is_valid_collation(cs_type))) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid charset", K(cs_type));
  } else {
    mutiply_num = ObCharset::get_charset(cs_type)->casedn_multiply;
  }
  return mutiply_num;
}

int ObExprUpper::calc(const ObCollationType cs_type, char *src, int32_t src_len,
                      char *dst, int32_t dst_len, int32_t &out_len) const
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(src) || OB_ISNULL(dst)) {
    ret = OB_BAD_NULL_ERROR;
    LOG_WARN("src or dst is null", K(ret));
  } else {
    out_len = static_cast<int32_t>(ObCharset::caseup(cs_type, src, src_len, dst, dst_len));
  }
  return ret;
}


int32_t ObExprUpper::get_case_mutiply(const ObCollationType cs_type) const
{
  int32_t mutiply_num = 0;
  if (OB_UNLIKELY(!ObCharset::is_valid_collation(cs_type))) {
    LOG_WARN_RET(OB_INVALID_ARGUMENT, "invalid charset", K(cs_type));
  } else {
    mutiply_num = ObCharset::get_charset(cs_type)->caseup_multiply;
  }
  return mutiply_num;
}

int ObExprLowerUpper::cg_expr_common(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  ObObjType text_type = ObMaxType;
  if (rt_expr.arg_cnt_ != 1) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("lower/upper expr should have one param", K(ret), K(rt_expr.arg_cnt_));
  } else if (OB_ISNULL(rt_expr.args_) || OB_ISNULL(rt_expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("children of lower/upper expr is null", K(ret), K(rt_expr.args_));
  } else if (FALSE_IT(text_type = rt_expr.args_[0]->datum_meta_.type_)) {
  } else if (ObVarcharType != text_type && ObLongTextType != text_type) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(text_type), K(ret));
  }
  return ret;
}

int ObExprLower::cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(cg_expr_common(op_cg_ctx, raw_expr, rt_expr))) {
    LOG_WARN("lower expr cg expr failed", K(ret));
  } else {
    rt_expr.eval_func_ = ObExprLower::calc_lower;
  }
  return ret;
}

int ObExprUpper::cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(cg_expr_common(op_cg_ctx, raw_expr, rt_expr))) {
    LOG_WARN("upper expr cg expr failed", K(ret));
  } else {
    rt_expr.eval_func_ = ObExprUpper::calc_upper;
  }
  return ret;
}

static inline int32_t calc_common_inner(char *buf,
                                        const int32_t &buf_len, 
                                        const ObString &m_text,
                                        const ObCollationType &cs_type,
                                        const bool &is_lower)
{
  MEMCPY(buf, m_text.ptr(), m_text.length());
  //gb18030 may expand in size, src_str and dst_str should has different buf, other cs_type can use the same buf
  char *src_str = (buf_len != m_text.length()) ? const_cast<char*>(m_text.ptr()) : buf;
  return (is_lower
          ? static_cast<int32_t>(ObCharset::casedn(cs_type, src_str, m_text.length(), buf, buf_len))
          : static_cast<int32_t>(ObCharset::caseup(cs_type, src_str, m_text.length(), buf, buf_len))
          );
}

int ObExprLowerUpper::calc_common(const ObExpr &expr, ObEvalCtx &ctx,
                                  ObDatum &expr_datum, bool lower, ObCollationType cs_type)
{
  int ret = OB_SUCCESS;
  ObDatum *text_datum = NULL;
  if (OB_FAIL(expr.args_[0]->eval(ctx, text_datum))) {
    LOG_WARN("eval param value failed", K(ret));
  } else if (text_datum->is_null()) {
    expr_datum.set_null();
  } else {
    ObString m_text = text_datum->get_string();
    if (cs_type == CS_TYPE_INVALID) {
      cs_type = expr.datum_meta_.cs_type_;
    }
    ObString str_result;
    bool has_lob_header = expr.args_[0]->obj_meta_.has_lob_header();
    ObDatumMeta text_meta = expr.args_[0]->datum_meta_;
    uchar multiply = 0;
    if (m_text.empty() && !ob_is_text_tc(text_meta.type_)) {
      str_result.reset();
    } else if (OB_UNLIKELY(!ObCharset::is_valid_collation(cs_type))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("charset is null", K(ret), K(cs_type));
    } else if (FALSE_IT(multiply = (lower ? ObCharset::get_charset(cs_type)->casedn_multiply
                                          : ObCharset::get_charset(cs_type)->caseup_multiply))) {
    } else if (!ob_is_text_tc(text_meta.type_)) {
      int32_t buf_len = m_text.length() * multiply;
      char *buf = expr.get_str_res_mem(ctx, buf_len);
      if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc memory failed", "size", buf_len);
      } else {
        int32_t out_len = calc_common_inner(buf, buf_len, m_text, cs_type, lower);
        str_result.assign(buf, static_cast<int32_t>(out_len));
      }
    } else { // text tc only
      ObEvalCtx::TempAllocGuard alloc_guard(ctx);
      ObIAllocator &calc_alloc = alloc_guard.get_allocator();
      ObTextStringIter src_iter(text_meta.type_, text_meta.cs_type_, text_datum->get_string(), has_lob_header);
      ObTextStringDatumResult output_result(expr.datum_meta_.type_, &expr, &ctx, &expr_datum);

      ObString dst;
      char *buf = NULL; // res buffer
      int64_t src_byte_len = 0;
      int64_t buf_size = 0;
      int32_t buf_len = 0;
      if (OB_UNLIKELY(!ObCharset::is_valid_collation(cs_type))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("charset is null", K(ret), K(cs_type));
      } else if (OB_FAIL(src_iter.init(0, NULL, &calc_alloc))) {
        LOG_WARN("init src_iter failed ", K(ret), K(src_iter));
      } else if (OB_FAIL(src_iter.get_byte_len(src_byte_len))) {
        LOG_WARN("get input byte len failed", K(ret));
      } else if (FALSE_IT(buf_len = multiply * src_byte_len)) {
      } else if (OB_FAIL(output_result.init(buf_len))) {
        LOG_WARN("init stringtext result failed", K(ret));
      } else if (buf_len == 0) {
        output_result.set_result();
        output_result.get_result_buffer(str_result);
      } else if (OB_FAIL(output_result.get_reserved_buffer(buf, buf_size))) {
        LOG_WARN("stringtext result reserve buffer failed", K(ret));
      } else if (OB_ISNULL(buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        LOG_ERROR("alloc memory failed", "size", buf_len);
      } else {
        OB_ASSERT(buf_size == buf_len);
        ObTextStringIterState state;
        ObString src_block_data;

        while (OB_SUCC(ret) 
               && buf_size > 0
               && (state = src_iter.get_next_block(src_block_data)) == TEXTSTRING_ITER_NEXT) {
          int32_t out_len = calc_common_inner(buf,
                                              multiply == 1 ? src_block_data.length() : buf_size,
                                              src_block_data,
                                              cs_type,
                                              lower);
          buf += out_len;
          buf_size -= out_len;
          if (OB_FAIL(output_result.lseek(out_len, 0))) {
            LOG_WARN("result lseek failed", K(ret));
          }
        }
        if (OB_FAIL(ret)) {
        } else if (state != TEXTSTRING_ITER_NEXT && state != TEXTSTRING_ITER_END) {
          ret = (src_iter.get_inner_ret() != OB_SUCCESS) ? 
                src_iter.get_inner_ret() : OB_INVALID_DATA;
          LOG_WARN("iter state invalid", K(ret), K(state), K(src_iter)); 
        } else {
          output_result.get_result_buffer(str_result);
        }
      }
    }
    if (OB_SUCC(ret)) {
        expr_datum.set_string(str_result);
    }
  }
  return ret;
}

DEF_SET_LOCAL_SESSION_VARS(ObExprLowerUpper, raw_expr) {
  int ret = OB_SUCCESS;
  SET_LOCAL_SYSVAR_CAPACITY(1);
  EXPR_ADD_LOCAL_SYSVAR(share::SYS_VAR_COLLATION_CONNECTION);
  return ret;
}

int ObExprLower::calc_lower(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  return calc_common(expr, ctx, expr_datum, true, CS_TYPE_INVALID);
}

int ObExprUpper::calc_upper(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  return calc_common(expr, ctx, expr_datum, false, CS_TYPE_INVALID);
}

}
}
