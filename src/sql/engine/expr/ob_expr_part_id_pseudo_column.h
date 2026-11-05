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

#ifndef _OCEANBASE_SQL_ENGINE_EXPR_PART_ID_PSEUDO_COLUMN_FOR_PDML_H_
#define _OCEANBASE_SQL_ENGINE_EXPR_PART_ID_PSEUDO_COLUMN_FOR_PDML_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "sql/engine/expr/ob_expr_res_type.h"
#include "share/ob_i_sql_expression.h"
// Provide the function to calculate partition id for pdml feature, the specific calculation method is:
// 1. child operator (for example Table scan) in calculating a row, fills the corresponding partition id into ObExecContext in ObExprCtx
// 2. ObExprPartIdPseudoColumn expression directly obtains the corresponding partition id from ObExprCtx
namespace oceanbase
{
namespace sql
{
class ObExprPartIdPseudoColumn: public ObFuncExprOperator
{
public:
  explicit ObExprPartIdPseudoColumn(common::ObIAllocator &alloc);
  virtual ~ObExprPartIdPseudoColumn();

  virtual int calc_result_type0(ObExprResType &type,
                                common::ObExprTypeCtx &type_ctx) const;

  // 3.0 engine
  virtual int cg_expr(ObExprCGCtx &op_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int eval_part_id(const ObExpr &expr, ObEvalCtx &ctx, common::ObDatum &expr_datum);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprPartIdPseudoColumn);
};
} // namespace sql
} // namespace oceanbase

#endif
