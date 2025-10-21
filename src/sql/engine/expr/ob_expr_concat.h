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

#ifndef _OB_SQL_EXPR_CONCAT_H_
#define _OB_SQL_EXPR_CONCAT_H_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprConcat : public ObStringExprOperator
{
public:
  explicit  ObExprConcat(common::ObIAllocator &alloc);
  virtual ~ObExprConcat();

  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;
  // Check result length with %max_result_len (if %max_result_len greater than zero)
  // or max length of varchar.
  // %result type is set to varchar.
  static int calc(common::ObObj &result,
                  const common::ObString obj1,
                  const common::ObString obj2,
                  common::ObIAllocator *allocator,
                  bool is_oracle_mode,
                  const int64_t max_result_len);
  // Check result length with OB_MAX_PACKET_LENGTH.
  // %result type is set to ObLongTextType
  static int calc(common::ObObj &result,
                         const char *obj1_ptr,
                         const int32_t this_len,
                         const char *obj2_ptr,
                         const int32_t other_len,
                         common::ObIAllocator *allocator);
  static int calc_text(common::ObObj &result,
                                     const common::ObObj obj1,
                                     const common::ObObj obj2,
                                     ObIAllocator *allocator);

  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_concat(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

  DECLARE_SET_LOCAL_SESSION_VARS;

private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprConcat);
};

}
}
#endif /* _OB_SQL_EXPR_CONCAT_H_ */
