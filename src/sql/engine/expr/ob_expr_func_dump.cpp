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

#include "sql/engine/expr/ob_expr_func_dump.h"
#include "sql/session/ob_sql_session_info.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

const int64_t MAX_DUMP_BUFFER_SIZE = 4000;
const char *CONST_HEADER = "Typ=%d Len=%ld: ";

enum ReturnFormat {
  RF_OB_SEPC = 0,
  RF_OCT     = 8,//8
  RF_DEC     = 10,//10
  RF_HEX     = 16,//16
  RF_ASCII   = 17,//17
};

ObExprFuncDump::ObExprFuncDump(ObIAllocator &alloc)
    : ObStringExprOperator(alloc, T_FUN_SYS_DUMP, N_DUMP, MORE_THAN_ZERO, NOT_VALID_FOR_GENERATED_COL)
{
}

ObExprFuncDump::~ObExprFuncDump()
{
}

int print_value(char *tmp_buf, const int64_t buff_size, int64_t &pos,
    const ObString &value_string, const int64_t fmt_enum,
    const int64_t start_pos, const int64_t print_value_len)
{
  int ret = common::OB_SUCCESS;
  ObString print_value_string;
  if (OB_UNLIKELY(print_value_len < 0)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("print_value_len should not less then zero", K(ret), K(print_value_len));
  } else {
    if (start_pos > 1) {
      if (start_pos > value_string.length()) {
        //empty
      } else {
        print_value_string.assign_ptr(value_string.ptr() + start_pos - 1, MIN((value_string.length() - start_pos), print_value_len));
      }
    } else if (start_pos < 0) {
      if (-start_pos > value_string.length()) {
        print_value_string.assign_ptr(value_string.ptr(), MIN((value_string.length()), print_value_len));
      } else {
        print_value_string.assign_ptr(value_string.ptr() + value_string.length() + start_pos, MIN((-start_pos), print_value_len));
      }
    } else {
      print_value_string.assign_ptr(value_string.ptr(), MIN((value_string.length()), print_value_len));
    }

    if (ReturnFormat::RF_ASCII == fmt_enum) {
      for (int64_t i = 0; i < print_value_string.length() && OB_SUCC(ret); ++i) {
        if (isprint(print_value_string[i])) {
          if (OB_FAIL(databuff_printf(tmp_buf, buff_size, pos, "%c,", print_value_string[i]))) {
            if (OB_SIZE_OVERFLOW != ret) {
              LOG_WARN("failed to databuff_printf", K(ret), K(pos));
            }
          }
        } else {
          if (OB_FAIL(databuff_printf(tmp_buf, buff_size, pos, "%x,", (unsigned)(unsigned char)print_value_string[i]))) {
            if (OB_SIZE_OVERFLOW != ret) {
              LOG_WARN("failed to databuff_printf", K(ret), K(pos));
            }
          }
        }
      }
    } else {//%u, %x, %o
      char fmt_str[4] = {0};
      fmt_str[0] = '%';
      fmt_str[1] = (ReturnFormat::RF_HEX == fmt_enum ? 'x' : (ReturnFormat::RF_OCT == fmt_enum ? 'o' : 'u'));
      fmt_str[2] = ',';

      for (int64_t i = 0; i < print_value_string.length() && OB_SUCC(ret); ++i) {
        if (OB_FAIL(databuff_printf(tmp_buf, buff_size, pos, fmt_str, (unsigned)(unsigned char)print_value_string[i]))) {
          if (OB_SIZE_OVERFLOW != ret) {
            LOG_WARN("failed to databuff_printf", K(ret), K(pos));
          }
        }
      }
    }
    if (OB_SIZE_OVERFLOW == ret) {
      ret = common::OB_SUCCESS; // result string length > MAX_DUMP_BUFFER_SIZE, cut the result string and return
    }
  }

  if (OB_SUCC(ret)) {
    pos -= 1;
    LOG_DEBUG("succ to print_value", K(value_string), K(print_value_string), K(fmt_enum), K(start_pos), K(print_value_len), K(pos));
  }
  return ret;
}

int ObExprFuncDump::calc_result_typeN(ObExprResType &type,
                                      ObExprResType *types,
                                      int64_t param_num,
                                      common::ObExprTypeCtx &type_ctx) const
{
  int ret = OB_SUCCESS;
  CK(NULL != type_ctx.get_session());
  CK(NULL != types);
  CK(param_num >= 1);
  if (OB_FAIL(ret)) {
  }  else {
    if (OB_UNLIKELY(param_num > 1)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("too many argument not support now", K(param_num), K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, "too many argument");
    } else if (types[0].is_null()) {
      type.set_null();
    } else {
      type.set_varchar();
      type.set_collation_level(common::CS_LEVEL_COERCIBLE);
      type.set_default_collation_type();
      type.set_length(types[0].get_length());
    }
  }
  return ret;
}





static int databuff_print_decimalint(
    const ObDatumMeta datum_meta, const ObDatum &datum,
    char *buffer, int64_t length, int64_t &pos)
{
  int ret = OB_SUCCESS;

  if (ObDecimalIntType != datum_meta.type_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("type is not decimal int", K(ret), K(datum_meta));
  } else {
    const int16_t precision = datum_meta.precision_;
    const int16_t scale = datum_meta.scale_;
    const int32_t int_bytes = datum.get_int_bytes();
    const ObDecimalInt *decint = datum.get_decimal_int();
    if (OB_FAIL(databuff_printf(buffer, length, pos,
                "\"precision=%hd scale=%hd int_bytes=%d items=[",
                precision, scale, int_bytes))) {
      LOG_WARN("failed to databuff_printf", K(ret), K(precision), K(scale), K(int_bytes));
    } else {
      switch (int_bytes) {
        case sizeof(int32_t): {
          if (OB_FAIL(databuff_printf(buffer, length, pos, "%d", *(decint->int32_v_)))) {
            LOG_WARN("failed to databuff_printf", K(ret), K(*(decint->int32_v_)));
          }
          break;
        }
        case sizeof(int64_t): {
          if (OB_FAIL(databuff_printf(buffer, length, pos, "%ld", *(decint->int64_v_)))) {
            LOG_WARN("failed to databuff_printf", K(ret), K(*(decint->int64_v_)));
          }
          break;
        }
        case sizeof(int128_t): {
          for (int i = 0; OB_SUCC(ret) && i < 2; ++i) {
            if (OB_FAIL(databuff_printf(
                buffer, length, pos, "%lu,", decint->int128_v_->items_[i]))) {
              LOG_WARN("failed to databuff_printf", K(ret), K(i), K(decint->int128_v_->items_[i]));
            }
          }
          break;
        }
        case sizeof(int256_t): {
          for (int i = 0; OB_SUCC(ret) && i < 4; ++i) {
            if (OB_FAIL(databuff_printf(
                buffer, length, pos, "%lu,", decint->int256_v_->items_[i]))) {
              LOG_WARN("failed to databuff_printf", K(ret), K(i), K(decint->int128_v_->items_[i]));
            }
          }
          break;
        }
        case sizeof(int512_t): {
          for (int i = 0; OB_SUCC(ret) && i < 8; ++i) {
            if (OB_FAIL(databuff_printf(
                buffer, length, pos, "%lu,", decint->int512_v_->items_[i]))) {
              LOG_WARN("failed to databuff_printf", K(ret), K(i), K(decint->int128_v_->items_[i]));
            }
          }
          break;
        }
        default: {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid integer width", K(ret), K(int_bytes));
          break;
        }
      }
      if (OB_SUCC(ret)) {
        if (OB_FAIL(databuff_printf(buffer, length, pos, "]\""))) {
          LOG_WARN("failed to databuff_printf", K(ret));
        }
      }
    }
  }
  return ret;
}


int ObExprFuncDump::cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  CK(rt_expr.arg_cnt_ >= 1 && rt_expr.arg_cnt_ <= 4);
  rt_expr.eval_func_ = &ObExprFuncDump::eval_dump;
  return ret;
}

int ObExprFuncDump::eval_dump(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ObDatum *input = NULL;
  if (OB_FAIL(expr.eval_param_value(ctx, input))) {
    LOG_WARN("evaluate parameters failed", K(ret));
  } else if (input->is_null()) {
    expr_datum.set_null();
  } else {
    switch (expr.args_[0]->datum_meta_.type_) {
      case ObNumberType: {
        number::ObNumber nmb(input->get_number());
        ObCStringHelper helper;
        const char *nmb_str = helper.convert(nmb);
        if (OB_ISNULL(nmb_str)) {
          ret = OB_ERR_NULL_VALUE;
          LOG_WARN("nmb_str is NULL, maybe convert nmb failed", K(ret), K(nmb));
        } else {
          ObString src_str(0, (int32_t)strlen(nmb_str), const_cast<char *>(nmb_str));
          if (OB_FAIL(ObExprUtil::set_expr_ascii_result(
                      expr, ctx, expr_datum, src_str))) {
            LOG_WARN("set ASCII result failed", K(ret));
          }
        }
        break;
      }
      case ObDecimalIntType: {
        char buf[MAX_DUMP_BUFFER_SIZE] = {0};
        int64_t buf_pos = 0;
        if (OB_FAIL(databuff_print_decimalint(
            expr.args_[0]->datum_meta_, *input, buf, sizeof(buf), buf_pos))) {
          LOG_WARN("failed to databuff_print_decimalint", K(ret));
        } else if (OB_FAIL(ObExprUtil::set_expr_ascii_result(
                            expr, ctx, expr_datum, ObString(buf_pos, buf)))) {
          LOG_WARN("set ASCII result failed", K(ret));
        }
        break;
      }
      case ObNullType: {
        expr_datum.set_null();
      }
      break;
      default: {
        ret = OB_NOT_SUPPORTED;
        LOG_WARN("type not support now", K(expr.args_[0]->datum_meta_.type_), K(ret));
        LOG_USER_ERROR(OB_NOT_SUPPORTED, "The input type of the DUMP function");
      }
      break;
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
