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

#ifndef SRC_SQL_ENGINE_EXPR_OB_EXPR_SYMMETRIC_ENCRYPT_H_
#define SRC_SQL_ENGINE_EXPR_OB_EXPR_SYMMETRIC_ENCRYPT_H_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprBaseEncrypt : public ObFuncExprOperator 
{
public:
  ObExprBaseEncrypt();
  explicit ObExprBaseEncrypt(common::ObIAllocator& alloc, ObItemType func_type, const char* name);
  virtual ~ObExprBaseEncrypt();
  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types,
                                int64_t param_num, 
                                common::ObExprTypeCtx& type_ctx) const override;
  static int eval_encrypt(const ObExpr &expr,
                          ObEvalCtx &ctx,
                          const share::ObCipherOpMode op_mode,
                          const ObString &func_name,
                          ObDatum &res);
};

class ObExprBaseDecrypt : public ObFuncExprOperator 
{
public:
  ObExprBaseDecrypt();
  explicit ObExprBaseDecrypt(common::ObIAllocator& alloc, ObItemType func_type, const char* name);
  virtual ~ObExprBaseDecrypt();
  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types,
                                int64_t param_num, 
                                common::ObExprTypeCtx& type_ctx) const override;
  static int eval_decrypt(const ObExpr &expr,
                          ObEvalCtx &ctx,
                          const share::ObCipherOpMode op_mode,
                          const ObString &func_name,
                          ObDatum &res);
};

class ObExprAesEncrypt : public ObExprBaseEncrypt 
{
public:
  ObExprAesEncrypt();
  explicit ObExprAesEncrypt(common::ObIAllocator& alloc);
  virtual ~ObExprAesEncrypt();
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_aes_encrypt(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAesEncrypt);
};

class ObExprAesDecrypt : public ObExprBaseDecrypt 
{
public:
  ObExprAesDecrypt();
  explicit ObExprAesDecrypt(common::ObIAllocator& alloc);
  virtual ~ObExprAesDecrypt();
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_aes_decrypt(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprAesDecrypt);
};

class ObExprSm4Encrypt : public ObExprBaseEncrypt
{
public:
  ObExprSm4Encrypt();
  explicit ObExprSm4Encrypt(common::ObIAllocator& alloc);
  virtual ~ObExprSm4Encrypt();
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_sm4_encrypt(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSm4Encrypt);
};

class ObExprSm4Decrypt : public ObExprBaseDecrypt 
{
public:
  ObExprSm4Decrypt();
  explicit ObExprSm4Decrypt(common::ObIAllocator& alloc);
  virtual ~ObExprSm4Decrypt();
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_sm4_decrypt(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSm4Decrypt);
};

}
}





#endif
