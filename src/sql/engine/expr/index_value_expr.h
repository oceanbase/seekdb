/*
 * Copyright (c) 2026 OceanBase.
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
#ifndef SEEKDB_SQL_INDEX_VALUE_EXPR_H_
#define SEEKDB_SQL_INDEX_VALUE_EXPR_H_
#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase { namespace sql {
// Storage expands the source into index rows and fills this typed slot. The
// second operand is a type witness, not a value to evaluate or copy.
class IndexValueExpr final : public ObFuncExprOperator
{
public:
  explicit IndexValueExpr(common::ObIAllocator &alloc);
  int calc_result_type2(ObExprResType &type, ObExprResType &source,
                       ObExprResType &witness, common::ObExprTypeCtx &ctx) const override;
  int cg_expr(ObExprCGCtx &, const ObRawExpr &, ObExpr &expr) const override;
  static int eval_index_value(const ObExpr &, ObEvalCtx &, common::ObDatum &result);
private:
  DISALLOW_COPY_AND_ASSIGN(IndexValueExpr);
};
} }
#endif
