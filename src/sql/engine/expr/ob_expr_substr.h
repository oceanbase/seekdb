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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_SUBSTR_
#define OCEANBASE_SQL_ENGINE_EXPR_SUBSTR_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{

class ObExprSubstr : public ObStringExprOperator
{
public:
  explicit  ObExprSubstr(common::ObIAllocator &alloc);
  virtual ~ObExprSubstr();
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const;

  static int substr(common::ObString &output,
                    const common::ObString &input,
                    const int64_t pos,
                    const int64_t len,
                    common::ObCollationType cs_type,
                    const bool do_ascii_optimize_check,
                    const bool is_arg_batch_ascii,
                    bool &is_result_batch_ascii);


  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;

  static int eval_substr(EVAL_FUNC_ARG_DECL);
  static int eval_substr_batch(BATCH_EVAL_FUNC_ARG_DECL);
  DECLARE_SET_LOCAL_SESSION_VARS;

private:
  int calc_result_length(ObExprResType *types_array,
                         int64_t param_num,
                         common::ObCollationType cs_type,
                         int64_t &res_len) const;

  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObExprSubstr);
};

}
}
#endif /* OCEANBASE_SQL_ENGINE_EXPR_SUBSTR_ */
