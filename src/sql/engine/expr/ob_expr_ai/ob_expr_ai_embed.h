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

#ifndef OCEANBASE_SQL_OB_EXPR_AI_EMBED_H_
#define OCEANBASE_SQL_OB_EXPR_AI_EMBED_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/ob_exec_context.h"
#include "ob_ai_func_client.h"
#include "ob_ai_func.h"
#include "ob_ai_func_utils.h"

namespace oceanbase 
{
namespace sql 
{
class ObExprAIEmbed : public ObFuncExprOperator 
{
public:
  explicit ObExprAIEmbed(common::ObIAllocator &alloc);
  virtual ~ObExprAIEmbed();
  virtual int calc_result_typeN(ObExprResType &type, 
                                ObExprResType *types_array,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_ai_embed(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, 
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  virtual bool need_rt_ctx() const override { return true; }
private:
  static constexpr int MODEL_IDX = 0;
  static constexpr int CONTENT_IDX = 1;
  static constexpr int DIM_IDX = 2;
  DISALLOW_COPY_AND_ASSIGN(ObExprAIEmbed);
};

} // namespace sql
} // namespace oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_AI_EMBED_H_
