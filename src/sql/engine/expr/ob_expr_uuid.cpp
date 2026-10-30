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

#define USING_LOG_PREFIX  SQL_ENG
#include "sql/engine/expr/ob_expr_uuid.h"
#include "sql/engine/ob_exec_context.h"
#include "rust/uuid_generator.h"
using namespace oceanbase::common;
using namespace oceanbase::sql;

namespace oceanbase
{
namespace sql
{

int UuidCommon::bin2uuid(char *result, const uchar *src) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(result) || OB_ISNULL(src)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int64_t pos = 0;
    const char *HEXCHARS = "0123456789abcdef";
    for (int64_t i = 0; pos < UuidCommon::LENGTH_UUID && i < 16; ++i) {
      if (4 == i || 6 == i || 8 == i || 10 == i) {
        result[pos++] = '-';
      }
      result[pos++] = HEXCHARS[src[i] >> 4 & 0xF];
      result[pos++] = HEXCHARS[src[i] & 0xF];
    }
  }
  return ret;
}

int UuidCommon::read_section(char *res, bool &is_valid, const char *src, int64_t len) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(res) || OB_ISNULL(src)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    int cnt = 0;
    is_valid = true;
    while(is_valid && cnt < len) {
      if (!isxdigit(src[cnt * 2]) || !isxdigit(src[cnt * 2 + 1])) {
        is_valid = false;
      } else {
        char hi = common::get_xdigit(src[cnt * 2]);
        char lo = common::get_xdigit(src[cnt * 2 + 1]);
        res[cnt] = static_cast<uchar> ((hi << 4) + lo);
        cnt ++;
      }
    }
  }
  return ret;
}

//convert uuid to bin, is_valid is set to false if src is null or not a valid string-format UUID.
int UuidCommon::uuid2bin(char *result, bool &is_valid, const char *src, int64_t len) {
  int ret = OB_SUCCESS;
  is_valid = true;
  if (OB_ISNULL(result)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_ISNULL(src)) {
    is_valid = false;
  } else {
    char bin_buf[UuidCommon::BYTE_LENGTH + 1];
    switch (len) {
      case UuidCommon::LENGTH_UUID - 4:{
        if (OB_FAIL(UuidCommon::read_section(result, is_valid, src, UuidCommon::BYTE_LENGTH))) {
        }
      }
      break;
      case UuidCommon::LENGTH_UUID + 2: {
        if ('{' == src[0] && '}' == src[len - 1]) {
          src++;
        } else {
          is_valid = false;
        }
      }
      case UuidCommon::LENGTH_UUID: {
        int cnt = 0;
        for (int i = 0; OB_SUCC(ret) && i < 5 && is_valid; ++i) {
          if (OB_FAIL(UuidCommon::read_section(result + cnt, is_valid, src + cnt * 2 + i, UuidCommon::bytes_per_section[i]))) {
          } else {
            cnt += UuidCommon::bytes_per_section[i];
          }
          if (i < 4 && src[cnt * 2 + i] != '-') {
            is_valid = false;
          }
        }
      }
      break;
      default: {
        is_valid = false;
      }
    }
  }
  return ret;
}

ObExprUuid::ObExprUuid(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_UUID, N_UUID, 0, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprUuid::ObExprUuid(
  ObIAllocator &alloc,
  ObExprOperatorType type,
  const char *name,
  int32_t param_num,
  int32_t dimension)
  : ObFuncExprOperator(alloc, type, name, param_num, NOT_VALID_FOR_GENERATED_COL, dimension)
{
}

ObExprUuid::~ObExprUuid()
{
}

int ObExprUuid::init()
{
  int ret = OB_SUCCESS;
  const auto code = uuid_generator::init();
  if (code != uuid_generator::Code::Ok) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("initialize UUID generator failed", K(ret), "code", static_cast<int>(code));
  }
  return ret;
}

int ObExprUuid::calc(unsigned char *scratch)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(scratch)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected error. pointer is null", K(ret), K(scratch));
  } else {
    const auto code = uuid_generator::generate(rust::Slice<uint8_t>(scratch, UuidCommon::BYTE_LENGTH));
    if (code != uuid_generator::Code::Ok) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("generate UUID failed", K(ret), "code", static_cast<int>(code));
    }
  }
  return ret;
}

int ObExprUuid::eval_uuid(const ObExpr &expr, ObEvalCtx &ctx,
    ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  UNUSED(expr);
  unsigned char scratch[UuidCommon::LENGTH_UUID] = {0};
  if (OB_FAIL(calc(scratch))) {
  } else {
    char *buf = expr.get_str_res_mem(ctx, UuidCommon::LENGTH_UUID);
    int64_t pos = 0;
    if (OB_ISNULL(buf)) {
      ret = OB_ERR_UNEXPECTED;
      SERVER_LOG(WARN, "buff is null", K(ret));
    } else if (OB_FAIL(UuidCommon::bin2uuid(buf, scratch))) {
    } else {
      expr_datum.set_string(buf, UuidCommon::LENGTH_UUID);
    }
  }
  return ret;
}

int ObExprUuid::cg_expr(ObExprCGCtx &op_cg_ctx, const ObRawExpr &raw_expr,
    ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  rt_expr.eval_func_ = ObExprUuid::eval_uuid;
  return OB_SUCCESS;
}

int ObExprUuid::gen_server_uuid(char *server_uuid, const int64_t uuid_len)
{
  int ret = OB_SUCCESS;
  unsigned char scratch[UuidCommon::BYTE_LENGTH] = {0};
  if (OB_ISNULL(server_uuid) || OB_UNLIKELY(uuid_len != UuidCommon::LENGTH_UUID)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get unexpected error", K(ret), K(server_uuid), K(uuid_len));
  } else if (OB_FAIL(calc(scratch))) {
  } else if (OB_FAIL(UuidCommon::bin2uuid(server_uuid, scratch))) {
  }
  return ret;
}

ObExprUuid2bin::ObExprUuid2bin(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_UUID2BIN, N_UUID_TO_BIN, ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprUuid2bin::~ObExprUuid2bin()
{
}


int ObExprUuid2bin::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types,
                                      int64_t type_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  if (OB_UNLIKELY(NULL == types || type_num <= 0 || type_num > 2)) {
    ret = OB_ERR_PARAM_SIZE;
  } else {
    types[0].set_calc_type(common::ObVarcharType);
    if (2 == type_num) {
      types[1].set_calc_type(types[1].get_type());
    }
  }
  type.set_varbinary();
  type.set_length(UuidCommon::BYTE_LENGTH);
  type.set_collation_level(common::CS_LEVEL_COERCIBLE);
  return ret;
}

int ObExprUuid2bin::cg_expr(ObExprCGCtx &op_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_ && 2 != rt_expr.arg_cnt_)) {
    ret = OB_ERR_PARAM_SIZE;
  } else if (raw_expr.get_param_count() == 2) {
    const ObRawExpr *flag_expr = raw_expr.get_param_expr(1);
    if (OB_ISNULL(flag_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      SqlValueType flag_expr_type = flag_expr->get_result_type().get_type();
      if (OB_UNLIKELY(ObNullType != flag_expr_type &&
                  !ob_is_integer_type(flag_expr_type))) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }

  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = ObExprUuid2bin::uuid2bin;
    // Only implement vectorization when parameter 0 is batch and parameter 1 is constant or null
    {
      OB_ASSERT(rt_expr.args_[0] != nullptr);
      if (rt_expr.args_[0]->is_batch_result()) {
        if (rt_expr.arg_cnt_ != 2 || !rt_expr.args_[1]->is_batch_result()) {
          rt_expr.eval_batch_func_ = ObExprUuid2bin::uuid2bin_batch;
        } else {
        }
      } else {
      }
    }
  }
  return ret;
}

int ObExprUuid2bin::uuid2bin(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *text = nullptr;
  ObDatum *swap_flag = nullptr;
  bool need_swap = false;
  bool is_null = false;
  if (OB_FAIL(expr.args_[0]->eval(ctx, text))) {
  } else if (OB_ISNULL(text)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (text->is_null()) {
    expr_datum.set_null();
    is_null = true;
  } else if (expr.arg_cnt_ == 2) {
    if (OB_ISNULL(expr.args_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr.args_[1]->eval(ctx, swap_flag))) {
    } else if (OB_ISNULL(swap_flag)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (swap_flag->is_null()) {
      need_swap = false;
    } else {
      need_swap = swap_flag->get_bool();
    }
  } else {
    need_swap = false;
  }

  if (OB_SUCC(ret) && !is_null) {
    ObString uuid_text = text->get_string();
    char *res_buf = expr.get_str_res_mem(ctx, UuidCommon::BYTE_LENGTH);
    const char *text_ptr = uuid_text.ptr();
    bool is_valid = true;
    
    if (OB_ISNULL(res_buf)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(UuidCommon::uuid2bin(res_buf, is_valid, text_ptr, uuid_text.length()))) {
    } else if (!is_valid) {
      ret = OB_ERR_INCORRECT_VALUE_FOR_FUNCTION;
      ObString string_type_str("string");
      ObString func_str(N_UUID_TO_BIN);
      LOG_USER_ERROR(OB_ERR_INCORRECT_VALUE_FOR_FUNCTION,
                      string_type_str.length(), string_type_str.ptr(),
                      uuid_text.length(), uuid_text.ptr(),
                      func_str.length(), func_str.ptr());
    } else if (need_swap) { //check if need swap
      char swap_buf[8];
      MEMCPY(swap_buf, res_buf + 6, 2);
      MEMCPY(swap_buf + 2, res_buf + 4, 2);
      MEMCPY(swap_buf + 4, res_buf, 4);
      MEMCPY(res_buf, swap_buf, 8);
    } else {
    }
    if (OB_SUCC(ret)) {
      expr_datum.set_string(res_buf, UuidCommon::BYTE_LENGTH);
    }
  }
  return ret;
}


int ObExprUuid2bin::uuid2bin_batch(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   const ObBitVector &skip,
                                   const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  bool need_swap = false;
  if (OB_ISNULL(expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (expr.arg_cnt_ == 2) {
    ObDatum *swap_flag = nullptr;
    if (OB_ISNULL(expr.args_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr.args_[1]->eval(ctx, swap_flag))) {
    } else if (OB_ISNULL(swap_flag)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (swap_flag->is_null()) {
      need_swap = false;
    } else {
      need_swap = swap_flag->get_bool();
    }
  } else {
    need_swap = false;
  }

  if (OB_SUCC(ret)) {
    ObDatum *res_datum = expr.locate_batch_datums(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      ObDatum &text= expr.args_[0]->locate_expr_datum(ctx, i);
      if (text.is_null()) {
        res_datum[i].set_null();
      } else {
        ObString uuid_text = text.get_string();
        char *res_buf = expr.get_str_res_mem(ctx, UuidCommon::BYTE_LENGTH, i);
        const char *text_ptr = uuid_text.ptr();
        bool is_valid = true;
        if (OB_ISNULL(res_buf)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(UuidCommon::uuid2bin(res_buf, is_valid, text_ptr, uuid_text.length()))) {
        } else if (!is_valid) {
          ret = OB_ERR_INCORRECT_VALUE_FOR_FUNCTION;
          ObString string_type_str("string");
          ObString func_str(N_UUID_TO_BIN);
          LOG_USER_ERROR(OB_ERR_INCORRECT_VALUE_FOR_FUNCTION,
                        string_type_str.length(), string_type_str.ptr(),
                        uuid_text.length(), uuid_text.ptr(),
                        func_str.length(), func_str.ptr());
        } else if (need_swap) {
          char swap_buf[8];
          MEMCPY(swap_buf, res_buf + 6, 2);
          MEMCPY(swap_buf + 2, res_buf + 4, 2);
          MEMCPY(swap_buf + 4, res_buf, 4);
          MEMCPY(res_buf, swap_buf, 8);
        }
        if (OB_SUCC(ret)) {
          res_datum[i].set_string(res_buf, UuidCommon::BYTE_LENGTH);
        }
      }
      eval_flags.set(i);
    }
  }
  return ret;
}

ObExprIsUuid::ObExprIsUuid(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_IS_UUID, N_IS_UUID, 1, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprIsUuid::~ObExprIsUuid()
{
}

int ObExprIsUuid::cg_expr(ObExprCGCtx &op_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_)) {
    ret = OB_ERR_PARAM_SIZE;
  } else {
    rt_expr.eval_func_ = ObExprIsUuid::is_uuid;
    rt_expr.eval_batch_func_ = ObExprIsUuid::is_uuid_batch;
  }
  return ret;
}


int ObExprIsUuid::is_uuid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *text = nullptr;
  if (OB_ISNULL(expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, text))) {
  } else if (OB_ISNULL(text)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (text->is_null()) {
    expr_datum.set_null();
  } else {
    ObString uuid_text = text->get_string();
    const char *text_ptr = uuid_text.ptr();
    char result_buf[UuidCommon::BYTE_LENGTH];
    bool is_valid = true;
    if (OB_ISNULL(text_ptr)) {
      expr_datum.set_bool(false);
    } else if (OB_FAIL(UuidCommon::uuid2bin(result_buf, is_valid, text_ptr, uuid_text.length()))) {
    } else {
      expr_datum.set_bool(is_valid);
    }
  }
  return ret;
}

int ObExprIsUuid::is_uuid_batch(const ObExpr &expr,
                                ObEvalCtx &ctx,
                                const ObBitVector &skip,
                                const int64_t batch_size) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    ObDatum *res_datum = expr.locate_batch_datums(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      ObDatum &text= expr.args_[0]->locate_expr_datum(ctx, i);
      if (text.is_null()) {
        res_datum[i].set_null();
      } else {
        ObString uuid_text = text.get_string();
        const char *text_ptr = uuid_text.ptr();
        char result_buf[UuidCommon::BYTE_LENGTH];
        bool is_valid = true;
        if (OB_FAIL(UuidCommon::uuid2bin(result_buf, is_valid, text_ptr, uuid_text.length()))) {
        } else {
          res_datum[i].set_bool(is_valid);
        }
      }
      eval_flags.set(i);
    }
  }
  return ret;            
}

ObExprBin2uuid::ObExprBin2uuid(ObIAllocator &alloc)
    : ObFuncExprOperator(alloc, T_FUN_SYS_BIN2UUID, N_BIN_TO_UUID, ONE_OR_TWO, VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}

ObExprBin2uuid::~ObExprBin2uuid()
{
}

int ObExprBin2uuid::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types,
                                      int64_t type_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  UNUSED(type_ctx);
  if (OB_UNLIKELY(NULL == types || type_num <= 0 || type_num > 2)) {
    ret = OB_ERR_PARAM_SIZE;
  } else {
    types[0].set_calc_type(common::ObVarcharType);
    types[0].set_calc_collation_type(CS_TYPE_BINARY);
    if (2 == type_num) {
      types[1].set_calc_type(types[1].get_type());
    }
  }
  type.set_varchar();
  type.set_collation_level(common::CS_LEVEL_IMPLICIT);
  type.set_collation_type(common::ObCharset::get_default_collation(common::ObCharset::get_default_charset()));
  type.set_length(UuidCommon::LENGTH_UUID);
  return ret;
}

int ObExprBin2uuid::cg_expr(ObExprCGCtx &op_cg_ctx,
                          const ObRawExpr &raw_expr,
                          ObExpr &rt_expr) const
{
  UNUSED(op_cg_ctx);
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(1 != rt_expr.arg_cnt_ && 2 != rt_expr.arg_cnt_)) {
    ret = OB_ERR_PARAM_SIZE;
  } else if (raw_expr.get_param_count() == 2) {
    const ObRawExpr *flag_expr = raw_expr.get_param_expr(1);
    if (OB_ISNULL(flag_expr)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      SqlValueType flag_expr_type = flag_expr->get_result_type().get_type();
      if (OB_UNLIKELY(ObNullType != flag_expr_type &&
                  !ob_is_integer_type(flag_expr_type))) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }

  if (OB_SUCC(ret)) {
    rt_expr.eval_func_ = ObExprBin2uuid::bin2uuid;
    // Only implement vectorization when parameter 0 is batch and parameter 1 is constant or null
    {
      OB_ASSERT(rt_expr.args_[0] != nullptr);
      if (rt_expr.args_[0]->is_batch_result()) {
        if (rt_expr.arg_cnt_ == 2) {
          {
            OB_ASSERT(rt_expr.args_[1] != nullptr);
            if (!rt_expr.args_[1]->is_batch_result()) {
              rt_expr.eval_batch_func_ = ObExprBin2uuid::bin2uuid_batch;
            }
          }
        } else {
          rt_expr.eval_batch_func_ = ObExprBin2uuid::bin2uuid_batch;
        }
      } else {
      }
    }
  }
  return ret;
}

int ObExprBin2uuid::bin2uuid(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *text = nullptr;
  ObDatum *swap_flag = nullptr;
  bool need_swap = false;
  bool is_null = false;
  if (OB_ISNULL(expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[0]->eval(ctx, text))) {
  } else if (OB_ISNULL(text)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (text->is_null()) {
    expr_datum.set_null();
    is_null = true;
  } else if (expr.arg_cnt_ == 2) {
    if (OB_ISNULL(expr.args_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr.args_[1]->eval(ctx, swap_flag))) {
    } else if (OB_ISNULL(swap_flag)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (swap_flag->is_null()) {
      need_swap = false;
    } else {
      need_swap = swap_flag->get_bool();
    }
  } else {
    need_swap = false;
  }

  if (OB_SUCC(ret) && !is_null) {
    ObString bin_text = text->get_string();
    char *res_buf = expr.get_str_res_mem(ctx, UuidCommon::LENGTH_UUID);
    const char *bin_text_ptr = bin_text.ptr();
    uchar new_bin_text[UuidCommon::BYTE_LENGTH];
    if (OB_ISNULL(res_buf)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_ISNULL(bin_text_ptr) || UuidCommon::BYTE_LENGTH != bin_text.length()) {
      ObString string_type_str("binary");
      ObString func_str(N_BIN_TO_UUID);
      int text_len = std::min(bin_text.length(), UuidCommon::BYTE_LENGTH * 2);
      SMART_VAR(char[UuidCommon::BYTE_LENGTH * 4], hex_buf) {
        int64_t pos = 0;
        if (OB_SUCC(common::hex_print(bin_text.ptr(), text_len, hex_buf, text_len*2, pos))) {
          ret = OB_ERR_INCORRECT_VALUE_FOR_FUNCTION;
          LOG_USER_ERROR(OB_ERR_INCORRECT_VALUE_FOR_FUNCTION,
                        string_type_str.length(), string_type_str.ptr(),
                        text_len*2, hex_buf,
                        func_str.length(), func_str.ptr());
        } else {
        }
      }
    } else  if (need_swap) {
      // The first 4 bytes are restored to "time-low".
      MEMCPY(new_bin_text, bin_text_ptr + 4, 4);
      // Bytes starting with 4th will be restored to "time-mid".
      MEMCPY(new_bin_text + 4, bin_text_ptr + 2, 2);
      // Bytes starting with 6th will be restored to "time-high".
      MEMCPY(new_bin_text + 6, bin_text_ptr, 2);
      // The last 8 bytes were not changed so we just copy them.
      MEMCPY(new_bin_text + 8, bin_text_ptr + 8, 8);
    } else {
      MEMCPY(new_bin_text, bin_text_ptr, UuidCommon::BYTE_LENGTH);
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(UuidCommon::bin2uuid(res_buf, new_bin_text))) {
      } else {
        expr_datum.set_string(res_buf, UuidCommon::LENGTH_UUID);
      }
    }
  }
  return ret;
}

int ObExprBin2uuid::bin2uuid_batch(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   const ObBitVector &skip,
                                   const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  bool need_swap = false;
  if (OB_ISNULL(expr.args_[0])) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (expr.arg_cnt_ == 2) {
    ObDatum *swap_flag = nullptr;
    if (OB_ISNULL(expr.args_[1])) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(expr.args_[1]->eval(ctx, swap_flag))) {
    } else if (OB_ISNULL(swap_flag)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (swap_flag->is_null()) {
      need_swap = false;
    } else {
      need_swap = swap_flag->get_bool();
    }
  } else {
    need_swap = false;
  }

  if (OB_SUCC(ret)) {
    ObDatum *res_datum = expr.locate_batch_datums(ctx);
    ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
    for (int64_t i = 0; OB_SUCC(ret) && i < batch_size; ++i) {
      if (skip.at(i) || eval_flags.at(i)) {
        continue;
      }
      ObDatum &text = expr.args_[0]->locate_expr_datum(ctx, i);
      if (text.is_null()) {
        res_datum[i].set_null();
      } else {
        ObString bin_text = text.get_string();
        char *res_buf = expr.get_str_res_mem(ctx, UuidCommon::LENGTH_UUID, i);
        const char *bin_text_ptr = bin_text.ptr();
        uchar new_bin_text[UuidCommon::BYTE_LENGTH];
        if (OB_ISNULL(res_buf)) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_ISNULL(bin_text_ptr) || UuidCommon::BYTE_LENGTH != bin_text.length()) {
          ObString string_type_str("binary");
          ObString func_str(N_BIN_TO_UUID);
          int text_len = std::min(bin_text.length(), UuidCommon::BYTE_LENGTH * 2);
          SMART_VAR(char[UuidCommon::BYTE_LENGTH * 4], hex_buf) {
            int64_t pos = 0;
            if (OB_SUCC(common::hex_print(bin_text.ptr(), text_len, hex_buf, text_len*2, pos))) {
              ret = OB_ERR_INCORRECT_VALUE_FOR_FUNCTION;
              LOG_USER_ERROR(OB_ERR_INCORRECT_VALUE_FOR_FUNCTION,
                            string_type_str.length(), string_type_str.ptr(),
                            text_len*2, hex_buf,
                            func_str.length(), func_str.ptr());
            } else {
            }
          }
        } else  if (need_swap) {
          // The first 4 bytes are restored to "time-low".
          MEMCPY(new_bin_text, bin_text_ptr + 4, 4);
          // Bytes starting with 4th will be restored to "time-mid".
          MEMCPY(new_bin_text + 4, bin_text_ptr + 2, 2);
          // Bytes starting with 6th will be restored to "time-high".
          MEMCPY(new_bin_text + 6, bin_text_ptr, 2);
          // The last 8 bytes were not changed so we just copy them.
          MEMCPY(new_bin_text + 8, bin_text_ptr + 8, 8);
        } else {
          MEMCPY(new_bin_text, bin_text_ptr, UuidCommon::BYTE_LENGTH);
        }
        if (OB_SUCC(ret)) {
          if (OB_FAIL(UuidCommon::bin2uuid(res_buf, new_bin_text))) {
          } else {
            res_datum[i].set_string(res_buf, UuidCommon::LENGTH_UUID);
          }
        }
      }
      eval_flags.set(i);
    }
  }
  return ret;
}

} //namespace sql
} //namespace oceanbase
