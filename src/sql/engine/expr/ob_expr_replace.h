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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_REPLACE_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_REPLACE_

#include "lib/ob_name_def.h"
#include "share/object/ob_obj_cast.h"
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprReplace : public ObStringExprOperator
{
public:
  explicit  ObExprReplace(common::ObIAllocator &alloc);
  virtual ~ObExprReplace();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_replace(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

  // helper func
  static int replace(common::ObString &result,
                     const ObCollationType cs_type,
                     const common::ObString &text,
                     const common::ObString &from,
                     const common::ObString &to,
                     common::ObExprStringBuf &string_buf,
                     const int64_t max_len = OB_MAX_VARCHAR_LENGTH);

  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprReplace);
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_REPLACE_
