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

#ifndef OCEANBASE_SQL_OB_EXPR_INSERT_H_
#define OCEANBASE_SQL_OB_EXPR_INSERT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprInsert : public ObStringExprOperator
{
public:
  explicit  ObExprInsert(common::ObIAllocator &alloc);
  virtual ~ObExprInsert();
  virtual int calc_result(common::ObObj &result,
                          const common::ObObj &text,
                          const common::ObObj &start_pos,
                          const common::ObObj &length,
                          const common::ObObj &replace_text,
                          common::ObExprCtx &expr_ctx) const;
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  static int calc(common::ObObj &result,
                  const common::ObObj &text,
                  const common::ObObj &start_pos,
                  const common::ObObj &length,
                  const common::ObObj &replace_text,
                  common::ObExprCtx &expr_ctx,
                  common::ObCollationType cs_type);
  static int calc(common::ObString &result,
                  const common::ObString &text,
                  const int64_t start_pos,
                  const int64_t expect_length_of_str,
                  const common::ObString &replace_text,
                  common::ObIAllocator &allocator,
                  common::ObCollationType cs_type);
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int calc_expr_insert(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);
  DECLARE_SET_LOCAL_SESSION_VARS;
  
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprInsert);
};
}
}

#endif /* OCEANBASE_SQL_OB_EXPR_INSERT_H_ */
