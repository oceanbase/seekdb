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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_DES_HEX_STR_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_DES_HEX_STR_

#include "common/expression/ob_expr_string_buf.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprDesHexStr : public ObFuncExprOperator
{
public:
  explicit ObExprDesHexStr(common::ObIAllocator &alloc);
  virtual ~ObExprDesHexStr() {}

  virtual int calc_result_type1(ObExprResType &type, ObExprResType &type1,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_des_hex_str(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
private:
  static int deserialize_hex_cstr(const char *buf,
                                  int64_t buf_len,
                                  common::ObExprStringBuf &string_buf,
                                  common::ObObj &obj);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprDesHexStr);
};
} // namespace sql
} // namespace oceanbase
#endif //OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_DES_HEX_STR_
