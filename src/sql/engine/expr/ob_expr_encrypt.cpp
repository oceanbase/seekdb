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
 
#define USING_LOG_PREFIX SQL_EXE
#include "ob_expr_encrypt.h"
#include <limits>
#include "rust/crypto.h"
#include "sql/engine/ob_exec_context.h"

using namespace oceanbase::share;
using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

namespace
{
rust::Slice<const uint8_t> crypto_bytes(const ObString &value)
{
  return {reinterpret_cast<const uint8_t *>(value.empty() ? "" : value.ptr()),
          static_cast<size_t>(value.length())};
}

int crypto_result_error(const crypto::OperationResult &result)
{
  int ret = OB_SUCCESS;
  switch (result.code) {
    case crypto::Code::Ok: break;
    case crypto::Code::Allocation: ret = OB_ALLOCATE_MEMORY_FAILED; break;
    case crypto::Code::InvalidArgument: ret = OB_INVALID_ARGUMENT; break;
    case crypto::Code::Unsupported: ret = OB_NOT_SUPPORTED; break;
    default: ret = OB_ERR_UNEXPECTED; break;
  }
  if (OB_SUCCESS != ret) {
    LOG_WARN("crypto operation failed", K(ret), K(result.backend_code));
  }
  return ret;
}

int encrypt_des_value(const ObExpr &expr, ObEvalCtx &ctx, const ObString &src,
                     const ObString &password, bool has_password, uint8_t key_number,
                     Datum &res, int64_t datum_idx)
{
  int ret = OB_SUCCESS;
  const size_t src_length = static_cast<size_t>(src.length());
  if (src_length > std::numeric_limits<size_t>::max() - 9) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    const size_t res_length = (src_length / 8 + 1) * 8 + 1;
    if (res_length > static_cast<uint64_t>(INT64_MAX)) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      char *res_buf = expr.get_str_res_mem(ctx, res_length, datum_idx);
      if (OB_ISNULL(res_buf)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else {
        const auto result = crypto::des_encrypt_into(crypto_bytes(src), crypto_bytes(password),
            has_password, key_number, {reinterpret_cast<uint8_t *>(res_buf), res_length});
        if (OB_FAIL(crypto_result_error(result))) {
        } else if (result.size > res_length) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          res.set_string(res_buf, result.size);
        }
      }
    }
  }
  return ret;
}

int decrypt_des_value(const ObExpr &expr, ObEvalCtx &ctx, const ObString &src,
                     const ObString &password, bool has_password, Datum &res,
                     int64_t datum_idx)
{
  int ret = OB_SUCCESS;
  const size_t res_length = static_cast<size_t>(src.length());
  char *res_buf = expr.get_str_res_mem(ctx, res_length, datum_idx);
  if (OB_ISNULL(res_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    const auto result = crypto::des_decrypt_into(crypto_bytes(src), crypto_bytes(password),
        has_password, {reinterpret_cast<uint8_t *>(res_buf), res_length});
    if (result.code == crypto::Code::InvalidCiphertext) {
      res.set_null();
    } else if (OB_FAIL(crypto_result_error(result))) {
    } else if (result.size > res_length) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      res.set_string(res_buf, result.size);
    }
  }
  return ret;
}

int crypt_value(const ObExpr &expr, ObEvalCtx &ctx, const ObString &src,
                const char *salt, Datum &res, int64_t datum_idx)
{
  int ret = OB_SUCCESS;
  const size_t res_length = 13;
  char *res_buf = expr.get_str_res_mem(ctx, res_length, datum_idx);
  if (OB_ISNULL(res_buf)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else {
    const auto result = crypto::des_crypt_into(crypto_bytes(src),
        {reinterpret_cast<const uint8_t *>(salt), 2},
        {reinterpret_cast<uint8_t *>(res_buf), res_length});
    if (OB_FAIL(crypto_result_error(result))) {
    } else if (result.size > res_length) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      res.set_string(res_buf, result.size);
    }
  }
  return ret;
}
}

ObExprDesEncrypt::ObExprDesEncrypt(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_DES_ENCRYPT,
                         N_DES_ENCRYPT,
                         ONE_OR_TWO,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}
ObExprDesEncrypt::~ObExprDesEncrypt() {}

int ObExprDesEncrypt::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  int64_t len = 0;
  if (OB_ISNULL(types_stack)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(param_num > 2 || param_num < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param num is not correct", K(param_num));
  } else if (OB_FAIL(ObCharset::get_mbmaxlen_by_coll(types_stack[0].get_collation_type(), len))) {
  } else {
    len = len * types_stack[0].get_length();
    types_stack[0].set_calc_type(common::ObVarcharType);
    types_stack[0].set_calc_collation_type(types_stack[0].get_collation_type());
    types_stack[0].set_calc_collation_level(types_stack[0].get_collation_level());
    if (2 == param_num) {
      if (ob_is_integer_type(types_stack[1].get_type()) || 
          ObBitType == types_stack[1].get_type() ||
          ObYearType == types_stack[1].get_type()) {
        types_stack[1].set_calc_type(common::ObIntType);
        types_stack[1].set_calc_collation_type(types_stack[1].get_collation_type());
        types_stack[1].set_calc_collation_level(types_stack[1].get_collation_level());
      } else {
        types_stack[1].set_calc_type(common::ObVarcharType);
        types_stack[1].set_calc_collation_type(types_stack[1].get_collation_type());
        types_stack[1].set_calc_collation_level(types_stack[1].get_collation_level());
      }
    } else {}
    type.set_varbinary();
    type.set_length(len + 9);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprDesEncrypt::eval_des_encrypt_with_default(const ObExpr &expr, ObEvalCtx &ctx, Datum &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    Datum &src = expr.locate_param_datum(ctx, 0);
    bool is_null = false;
    uint key_number = 0;
    if (src.is_null()) {
      res.set_null();
      is_null = true;
    } else if (2 == expr.arg_cnt_) {
      Datum &key = expr.locate_param_datum(ctx, 1);
      if (key.is_null()) {
        key_number = 0;
      } else if (key.get_int() > 9 || key.get_int() < 0) {
        ObString func_name(N_DES_ENCRYPT);
        res.set_null();
        is_null = true;
        LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
        LOG_WARN("invalid param to procedure des_encrypt", K(ret), K(key.get_int()));
      } else {
        key_number = key.get_int();
      }
    }
    if (OB_SUCC(ret) && !is_null) {
      ObString src_str = src.get_string();
      if (src_str.empty()) {
        res.set_string(src_str);
      } else {
        ret = encrypt_des_value(expr, ctx, src_str, ObString(), false, key_number,
                                res, expr.get_datum_idx(ctx));
      }
    }
  }
  return ret;
}

int ObExprDesEncrypt::eval_des_encrypt_batch_with_default(const ObExpr &expr, ObEvalCtx &ctx,
                                    const ObBitVector &skip, const int64_t batch_size) {
  int ret = OB_SUCCESS;
  DatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  bool has_key = (2 == expr.arg_cnt_);
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (has_key) {
    if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
    }
  } else {
  }
  if (OB_SUCC(ret)) {
    DatumVector src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    DatumVector key_array;
    if (has_key) {
      key_array = expr.args_[1]->locate_expr_datumvector(ctx);
    } else {}
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else {
        ObString src_str = src_array.at(j)->get_string();
        uint key_number = 0;
        if (has_key) {
          if (key_array.at(j)->is_null()) {
            key_number = 0;
          } else {
            key_number = key_array.at(j)->get_int();
          }
        } else {}
        if (key_number > 9 || key_number < 0) {
          ObString func_name(N_DES_ENCRYPT);
          res_datum.at(j)->set_null();
          LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
          LOG_WARN("invalid param to procedure des_encrypt", K(ret), K(key_number));
        } else if (src_str.empty()) {
          res_datum.at(j)->set_string(src_str);
        } else {
          ret = encrypt_des_value(expr, ctx, src_str, ObString(), false, key_number,
                                  *res_datum.at(j), j);
        }
      }
    }
  }
  return ret;
}

int ObExprDesEncrypt::eval_des_encrypt_with_key(const ObExpr &expr, ObEvalCtx &ctx, Datum &res)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    Datum &src = expr.locate_param_datum(ctx, 0);
    Datum &key = expr.locate_param_datum(ctx, 1);
    if (src.is_null()) {
      res.set_null();
    } else if (key.is_null()) {
      ObString func_name(N_DES_ENCRYPT);
      res.set_null();
      LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
      LOG_WARN("invalid param to procedure des_encrypt", K(ret));
    } else if (src.get_string().empty()) {
      res.set_string(src.get_string());
    } else {
      const ObString key_str = key.get_string();
      ObString src_str = src.get_string();
      ret = encrypt_des_value(expr, ctx, src_str, key_str, true, 127,
                              res, expr.get_datum_idx(ctx));
    }
  }
  return ret;
}

int ObExprDesEncrypt::eval_des_encrypt_batch_with_key(const ObExpr &expr, ObEvalCtx &ctx,
                                    const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  DatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
  } else {
    DatumVector src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    DatumVector key_array = expr.args_[1]->locate_expr_datumvector(ctx);
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (key_array.at(j)->is_null()) {
        ObString func_name(N_DES_ENCRYPT);
        res_datum.at(j)->set_null();
        LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
        LOG_WARN("invalid param to procedure des_encrypt", K(ret));
      } else if (src_array.at(j)->get_string().empty()) {
        res_datum.at(j)->set_string(src_array.at(j)->get_string());
      } else {
        const ObString key_str = key_array.at(j)->get_string();
        ObString src_str = src_array.at(j)->get_string();
        ret = encrypt_des_value(expr, ctx, src_str, key_str, true, 127,
                                *res_datum.at(j), j);
      }
    }
  }
  return ret;
}

int ObExprDesEncrypt::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  if (2 == raw_expr.get_param_count() && common::ObIntType != raw_expr.get_param_expr(1)->get_result_type().get_type()) {
    rt_expr.eval_func_ = eval_des_encrypt_with_key;
    rt_expr.eval_batch_func_ = eval_des_encrypt_batch_with_key;
  } else {
    rt_expr.eval_func_ = eval_des_encrypt_with_default;
    rt_expr.eval_batch_func_ = eval_des_encrypt_batch_with_default;
  }
  return ret;
}


ObExprDesDecrypt::ObExprDesDecrypt(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_DES_DECRYPT,
                         N_DES_DECRYPT,
                         ONE_OR_TWO,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}
ObExprDesDecrypt::~ObExprDesDecrypt() {}

int ObExprDesDecrypt::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
   int64_t len = 0;
  if (OB_ISNULL(types_stack)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(param_num > 2 || param_num < 1)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObCharset::get_mbmaxlen_by_coll(types_stack[0].get_collation_type(), len))) {
  } else {
    types_stack[0].set_calc_type(common::ObVarcharType);
    types_stack[0].set_calc_collation_type(CS_TYPE_BINARY);
    types_stack[0].set_calc_collation_level(types_stack[0].get_collation_level());
    if (2 == param_num) {
      types_stack[1].set_calc_type(common::ObVarcharType);
      types_stack[1].set_calc_collation_type(types_stack[1].get_collation_type());
      types_stack[1].set_calc_collation_level(types_stack[1].get_collation_level());
    }
    type.set_varbinary();
    len = len * types_stack[0].get_length();
    if (len < 9) {
      type.set_length(len);
    } else {
      type.set_length(len - 9);
    }
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprDesDecrypt::eval_des_decrypt(const ObExpr &expr, ObEvalCtx &ctx,
                                       Datum &res)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    bool need_decrypt = false;
    Datum &src = expr.locate_param_datum(ctx, 0);
    ObString src_str;
    //get key and check if res should be null or empty
    if (src.is_null()) {
      res.set_null();
    } else {
      src_str = src.get_string();
      if (!crypto::is_des_encrypted(crypto_bytes(src_str))) {
        //src is not encrypted
        res.set_string(src_str);
      } else if (1 == expr.arg_cnt_) {
        uint key_num = static_cast<uint>(src_str.ptr()[0] & 127);
        ObSQLSessionInfo *session = nullptr;
        if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (key_num > 9 || !session->has_user_super_privilege()) {
          ObString func_name(N_DES_DECRYPT);
          res.set_null();
          LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
          LOG_WARN("invalid param to procedure des_encrypt", K(ret), K(key_num));
        } else {
          need_decrypt = true;
        }
      } else {
        Datum &key = expr.locate_param_datum(ctx, 1);
        if (key.is_null()) {
          ObString func_name(N_DES_DECRYPT);
          res.set_null();
          LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
          LOG_WARN("invalid param to procedure des_encrypt", K(ret));
        } else {
          need_decrypt = true;
        }
      }
    }
    //do descrypt
    if (OB_SUCC(ret) && need_decrypt) {
      const bool has_password = 2 == expr.arg_cnt_;
      const ObString password = has_password ? expr.locate_param_datum(ctx, 1).get_string() : ObString();
      ret = decrypt_des_value(expr, ctx, src_str, password, has_password,
                              res, expr.get_datum_idx(ctx));
    }
  }
  return ret;
}

int ObExprDesDecrypt::eval_des_decrypt_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  DatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  DatumVector src_array;
  DatumVector key_array;

  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    if (2 == expr.arg_cnt_) {
      if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
      } else {
        key_array = expr.args_[1]->locate_expr_datumvector(ctx);
      }
    } else {}
  }
  if (OB_SUCC(ret)) {
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      //get key and check if res should be null or empty
      if (src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else {
        const ObString src_str = src_array.at(j)->get_string();
        bool need_descrypt = false;
        if (!crypto::is_des_encrypted(crypto_bytes(src_str))) {
          //src is not encrypted
          res_datum.at(j)->set_string(src_str);
        } else if (1 == expr.arg_cnt_) {
          uint key_num = static_cast<uint>(src_str.ptr()[0] & 127);
          ObSQLSessionInfo *session = nullptr;
          if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
            ret = OB_ERR_UNEXPECTED;
          } else if (key_num > 9 || !session->has_user_super_privilege()) {
            ObString func_name(N_DES_DECRYPT);
            res_datum.at(j)->set_null();
            LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
            LOG_WARN("invalid param to procedure des_encrypt", K(ret), K(key_num));
          } else {
            need_descrypt = true;
          }
        } else if (key_array.at(j)->is_null()) {
          ObString func_name(N_DES_DECRYPT);
          res_datum.at(j)->set_null();
          LOG_USER_WARN(OB_ERR_INVALID_PARAM_TO_PROCEDURE,func_name.length(),func_name.ptr());
          LOG_WARN("invalid param to procedure des_encrypt", K(ret));
        } else {
          need_descrypt = true;
        }
        //do descrypt
        if (OB_SUCC(ret) && need_descrypt) {
          const bool has_password = 2 == expr.arg_cnt_;
          const ObString password = has_password ? key_array.at(j)->get_string() : ObString();
          ret = decrypt_des_value(expr, ctx, src_str, password, has_password,
                                  *res_datum.at(j), j);
        }
      }
    }
  }
  return ret;
}

int ObExprDesDecrypt::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_des_decrypt;
  rt_expr.eval_batch_func_ = eval_des_decrypt_batch;
  return ret;
}

ObExprEncrypt::ObExprEncrypt(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_ENCRYPT,
                         N_ENCRYPT,
                         ONE_OR_TWO,
                         NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}
ObExprEncrypt::~ObExprEncrypt() {}

int ObExprEncrypt::calc_result_typeN(ObExprResType& type,
                                        ObExprResType* types_stack,
                                        int64_t param_num,
                                        ObExprTypeCtx& type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  if (OB_ISNULL(types_stack)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_UNLIKELY(param_num > 2 || param_num < 1)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("param num is not correct", K(param_num));
  } else {
    for (int i = 0; i < param_num; ++i) {
      types_stack[i].set_calc_type(common::ObVarcharType);
      types_stack[i].set_calc_collation_type(types_stack[i].get_collation_type());
      types_stack[i].set_calc_collation_level(types_stack[i].get_collation_level());
    }
    type.set_varbinary();
    type.set_length(13);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprEncrypt::eval_encrypt(const ObExpr &expr, ObEvalCtx &ctx, Datum &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    char salt[3];
    Datum &src = expr.locate_param_datum(ctx, 0);
    bool need_encrypt = false;

    if (src.is_null()) {
      res.set_null();
    } else if (src.get_string().empty()) {
      res.set_string(src.get_string());
    } else if (1 == expr.arg_cnt_) {
      ObSQLSessionInfo *session = NULL;
      if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int64_t tv_sec = session->get_query_start_time()/1000000;
        salt[0] = bin_to_ascii(tv_sec & 0x3f);
        salt[1] = bin_to_ascii((tv_sec >> 5) & 0x3f);
        salt[2] = 0;
        need_encrypt = true;
      }
    } else {
      Datum &key = expr.locate_param_datum(ctx, 1);
      if (key.is_null() || key.get_string().length() < 2) {
        res.set_null();
      } else {
        MEMCPY(salt, key.get_string().ptr(), 2);
        salt[2] = 0;
        if (!check_key_valid(salt[0],salt[1])) {
          res.set_null();
        } else {
          need_encrypt = true;
        }
      }
    }
    if (OB_SUCC(ret) && need_encrypt) {
      ret = crypt_value(expr, ctx, src.get_string(), salt, res, expr.get_datum_idx(ctx));
    }
  }
  return ret;
}

int ObExprEncrypt::eval_encrypt_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size) {
  int ret = OB_SUCCESS;
  DatumVector src_array;
  DatumVector key_array;
  DatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  char salt[3];
  
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else {
    src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    if (2 == expr.arg_cnt_) {
      if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
      } else {
        key_array = expr.args_[1]->locate_expr_datumvector(ctx);
      }
    } else {
      ObSQLSessionInfo *session = nullptr;
      if (OB_ISNULL(session = ctx.exec_ctx_.get_my_session())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        int64_t timestamp = session->get_query_start_time();
        salt[0] = bin_to_ascii(timestamp & 0x3f);
        salt[1] = bin_to_ascii((timestamp >> 5) & 0x3f);
        salt[2] = 0;
      }
    }
  }
  if (OB_SUCC(ret)) {
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      bool need_encrypt = false;
      if (src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (src_array.at(j)->get_string().empty()) {
        res_datum.at(j)->set_string(src_array.at(j)->get_string());
      } else if (2 == expr.arg_cnt_) {
        if (key_array.at(j)->is_null() || (key_array.at(j)->get_string().length() < 2)) {
          res_datum.at(j)->set_null();
        } else {
          MEMCPY(salt, key_array.at(j)->get_string().ptr(), 2);
          if (!check_key_valid(salt[0],salt[1])) {
            res_datum.at(j)->set_null();
          } else {
            need_encrypt = true;
          }
        }
      } else {
        need_encrypt = true;
      }
      if (OB_SUCC(ret) && need_encrypt) {
        ret = crypt_value(expr, ctx, src_array.at(j)->get_string(), salt, *res_datum.at(j), j);
      }
    }
  }
  return ret;
}

int ObExprEncrypt::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_encrypt;
  rt_expr.eval_batch_func_ = eval_encrypt_batch;
  return ret;
}

inline char ObExprEncrypt::bin_to_ascii(int64_t c) {
  return static_cast<char> ((c)>= 38 ? ((c) - 38 + 'a') : (c) >= 12 ? ((c) - 12 + 'A'): (c) + '.');
}

inline bool ObExprEncrypt::check_key_valid(char salt0, char salt1) {
  return ((salt0 >= 'a' && salt0 <= 'z') || (salt0 >= 'A' && salt0 <= 'Z') || (salt0 >= '.' && salt0 <= '9')) &&
         ((salt1 >= 'a' && salt1 <= 'z') || (salt1 >= 'A' && salt1 <= 'Z') || (salt1 >= '.' && salt1 <= '9'));
}

ObExprEncode::ObExprEncode(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_ENCODE,
                         N_ENCODE,
                         2,
                         VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}
ObExprEncode::~ObExprEncode() {}

int ObExprEncode::calc_result_type2(ObExprResType &type,
                                 ObExprResType &type1,
                                 ObExprResType &type2,
                                 ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  int64_t mbmaxlen = 0;
  if (OB_FAIL(ObCharset::get_mbmaxlen_by_coll(type1.get_collation_type(), mbmaxlen))) {
  } else {
    type1.set_calc_type(common::ObVarcharType);
    type1.set_calc_collation_type(type1.get_collation_type());
    type1.set_calc_collation_level(type1.get_collation_level());
    type2.set_calc_type(common::ObVarcharType);
    type2.set_calc_collation_type(type2.get_collation_type());
    type2.set_calc_collation_level(type2.get_collation_level());
    type.set_varbinary();
    type.set_length(type1.get_length() * mbmaxlen);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprEncode::eval_encode(const ObExpr &expr, ObEvalCtx &ctx, Datum &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    Datum &src = expr.locate_param_datum(ctx, 0);
    Datum &key = expr.locate_param_datum(ctx, 1);
    if (src.is_null() || key.is_null()) {
      res.set_null();
    } else {
      ObString src_str = src.get_string();
      ObString key_str = key.get_string();
      uint32_t password[2];
      ObCrypt crypt;
      if (src_str.empty()) {
        res.set_string(src_str);
      } else {
        char * res_buf = expr.get_str_res_mem(ctx, src_str.length());
        crypt.init(key_str);
        if (OB_FAIL(crypt.encode(src_str, res_buf))) {
        } else {
          res.set_string(res_buf, src_str.length());
        }
      }
    }
  }
  return ret;
}

int ObExprEncode::eval_encode_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  DatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  uint32_t password[2];
  ObCrypt crypt;
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
  } else if (!expr.args_[1]->is_batch_result()) {
    DatumVector src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    Datum &key = expr.locate_param_datum(ctx, 1);
    if (!key.is_null()) {
      ObString key_str = key.get_string();
      crypt.init(key_str);
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (key.is_null() || src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (src_array.at(j)->get_string().empty()) {
        res_datum.at(j)->set_string(src_array.at(j)->get_string());
      } else {
        ObString src_str = src_array.at(j)->get_string();
        char * res_buf = expr.get_str_res_mem(ctx, src_str.length(), j);
        if (OB_FAIL(crypt.encode(src_str, res_buf))) {
        } else {
          res_datum.at(j)->set_string(res_buf, src_str.length());
        }
        crypt.reinit();
      }
    }
  } else {
    DatumVector src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    DatumVector key_array = expr.args_[1]->locate_expr_datumvector(ctx);
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (key_array.at(j)->is_null() || src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (src_array.at(j)->get_string().empty()) {
        res_datum.at(j)->set_string(src_array.at(j)->get_string());
      } else {
        ObString src_str = src_array.at(j)->get_string();
        ObString key_str = key_array.at(j)->get_string();
        
        char * res_buf = expr.get_str_res_mem(ctx, src_str.length(), j);
        crypt.init(key_str);
        if (OB_FAIL(crypt.encode(src_str, res_buf))) {
        } else {
          res_datum.at(j)->set_string(res_buf, src_str.length());
        }
      }
    }
  }
  return ret;
}

int ObExprEncode::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_encode;
  rt_expr.eval_batch_func_ = eval_encode_batch;
  return ret;
}

ObExprDecode::ObExprDecode(ObIAllocator& alloc)
    : ObFuncExprOperator(alloc,
                         T_FUN_SYS_DECODE,
                         N_DECODE,
                         2,
                         VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION)
{
}
ObExprDecode::~ObExprDecode() {}

int ObExprDecode::calc_result_type2(ObExprResType &type,
                                 ObExprResType &type1,
                                 ObExprResType &type2,
                                 ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  int64_t mbmaxlen = 0;
  if (OB_FAIL(ObCharset::get_mbmaxlen_by_coll(type1.get_collation_type(), mbmaxlen))) {
  } else {
    type1.set_calc_type(common::ObVarcharType);
    type1.set_calc_collation_type(type1.get_collation_type());
    type1.set_calc_collation_level(type1.get_collation_level());
    type2.set_calc_type(common::ObVarcharType);
    type2.set_calc_collation_type(type2.get_collation_type());
    type2.set_calc_collation_level(type2.get_collation_level());
    type.set_varbinary();
    type.set_length(type1.get_length() * mbmaxlen);
    type.set_collation_level(CS_LEVEL_COERCIBLE);
  }
  return ret;
}

int ObExprDecode::eval_decode(const ObExpr &expr, ObEvalCtx &ctx, Datum &res) {
  int ret = OB_SUCCESS;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
  } else {
    Datum &src = expr.locate_param_datum(ctx, 0);
    Datum &key = expr.locate_param_datum(ctx, 1);
    if (src.is_null() || key.is_null()) {
      res.set_null();
    } else {
      ObString src_str = src.get_string();
      ObString key_str = key.get_string();
      uint32_t password[2];
      ObCrypt crypt;
      if (src_str.empty()) {
        res.set_string(src_str);
      } else {
        char * res_buf = expr.get_str_res_mem(ctx, src_str.length());
        crypt.init(key_str);
        if (OB_FAIL(crypt.decode(src_str, res_buf))) {
        } else {
          res.set_string(res_buf, src_str.length());
        }
      }
    }
  }
  return ret;
}

int ObExprDecode::eval_decode_batch(const ObExpr &expr, ObEvalCtx &ctx, const ObBitVector &skip, const int64_t batch_size)
{
  int ret = OB_SUCCESS;
  DatumVector res_datum = expr.locate_expr_datumvector(ctx);
  ObBitVector &eval_flags = expr.get_evaluated_flags(ctx);
  uint32_t password[2];
  ObCrypt crypt;
  if (OB_FAIL(expr.args_[0]->eval_batch(ctx, skip, batch_size))) {
  } else if (OB_FAIL(expr.args_[1]->eval_batch(ctx, skip, batch_size))) {
  } else if (!expr.args_[1]->is_batch_result()) {
    DatumVector src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    Datum &key = expr.locate_param_datum(ctx, 1);
    if (!key.is_null()) {
      ObString key_str = key.get_string();
      crypt.init(key_str);
    }
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (key.is_null() || src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (src_array.at(j)->get_string().empty()) {
        res_datum.at(j)->set_string(src_array.at(j)->get_string());
      } else {
        ObString src_str = src_array.at(j)->get_string();
        char * res_buf = expr.get_str_res_mem(ctx, src_str.length(), j);
        if (OB_FAIL(crypt.decode(src_str, res_buf))) {
        } else {
          res_datum.at(j)->set_string(res_buf, src_str.length());
        }
        crypt.reinit();
      }
    }
  } else {
    DatumVector src_array = expr.args_[0]->locate_expr_datumvector(ctx);
    DatumVector key_array = expr.args_[1]->locate_expr_datumvector(ctx);
    for (int64_t j = 0; OB_SUCC(ret) && j < batch_size; ++j) {
      if (skip.at(j) || eval_flags.at(j)) {
        continue;
      }
      eval_flags.set(j);
      if (key_array.at(j)->is_null() || src_array.at(j)->is_null()) {
        res_datum.at(j)->set_null();
      } else if (src_array.at(j)->get_string().empty()) {
        res_datum.at(j)->set_string(src_array.at(j)->get_string());
      } else {
        ObString src_str = src_array.at(j)->get_string();
        ObString key_str = key_array.at(j)->get_string();
        char * res_buf = expr.get_str_res_mem(ctx, src_str.length(), j);
        crypt.init(key_str);
        if (OB_FAIL(crypt.decode(src_str, res_buf))) {
        } else {
          res_datum.at(j)->set_string(res_buf, src_str.length());
        }
      }
    }
  }
  return ret;
}

int ObExprDecode::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(expr_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = eval_decode;
  rt_expr.eval_batch_func_ = eval_decode_batch;
  return ret;
}

void ObCrypt::init(ObString &key) {
  uint32_t nr=1345345333L, add=7, nr2=0x12345671L;
  if (!key.empty()) {
    uint32_t tmp = 0;
    const char *str_ptr = key.ptr();
    for (int64_t i = 0; i < key.length(); ++i) {
      if (' ' == str_ptr[i] || '\t' == str_ptr[i]) {
        // do nothing
      } else {
        tmp = static_cast<uint32_t>(static_cast<uchar>(str_ptr[i]));
        nr ^= (((nr & 63) + add) * tmp)+ (nr << 8);
        nr2 += (nr2 << 8) ^ nr;
        add += tmp;
      }
    }
  }
  rand_.init(nr & (((uint32_t) 1L << 31) -1L), nr2 & (((uint32_t) 1L << 31) -1L));
  for (int32_t i = 0 ; i <= 255; i++) {
    decode_buff_[i]= i;
  }
  for (int32_t i = 0 ; i<= 255 ; i++)
  {
    uint idx = static_cast<uint>(rand_.get_double() * 255.0);
    uchar a = decode_buff_[idx & 0xff];
    decode_buff_[idx & 0xff] = decode_buff_[i];
    decode_buff_[i] = a;
  }
  for (int32_t i = 0 ; i <= 255 ; i++) {
   encode_buff_[decode_buff_[i]] = i;
  }
  orig_rand_seed_[0] = rand_.seed1_;
  orig_rand_seed_[1] = rand_.seed2_;
  shift_ = 0;
}

int ObCrypt::encode(ObString &src, char *res) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(res)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (src.empty()) {
    res[0] = 0;
  } else {
    const char *str = src.ptr();
    uint idx = 0;
    for (size_t i = 0; i < src.length(); i++) {
      shift_ ^= static_cast<uint>(rand_.get_double() * 255.0);
      idx = static_cast<uint>(static_cast<uchar>(*str));
      *res = static_cast<char>(encode_buff_[idx & 0xff] ^ shift_);
      shift_ ^= idx;
      str ++;
      res ++;
    }
  }
  return ret;
}

int ObCrypt::decode(ObString &src, char *res) {
  int ret = OB_SUCCESS;
  if (OB_ISNULL(res)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (src.empty()) {
    res[0] = 0;
  } else {
    const char *str = src.ptr();
    uint idx = 0;
    for (size_t i = 0; i < src.length(); i++) {
      shift_ ^= static_cast<uint> (rand_.get_double() * 255.0);
      idx = static_cast<uint> (static_cast<uchar>(*str) ^ shift_);
      *res = decode_buff_[idx & 0xff];
      shift_ ^= static_cast<uint>(static_cast<uchar>(*res));
      str ++;
      res ++;
    }
  }
  return ret;
}

void ObCrypt::reinit() {
  shift_ = 0;
  rand_.seed1_ = orig_rand_seed_[0];
  rand_.seed2_ = orig_rand_seed_[1];
}

}
}
