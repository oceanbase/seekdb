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

#ifndef OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_PREFIX_PATTERN_
#define OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_PREFIX_PATTERN_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace sql
{
class ObExprPrefixPattern : public ObStringExprOperator
{
public:
  explicit  ObExprPrefixPattern(common::ObIAllocator &alloc);
  virtual ~ObExprPrefixPattern() {};

  virtual int calc_result_type3(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprResType &type3,
                                common::ObExprTypeCtx &type_ctx) const;
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const override;
  static int eval_prefix_pattern(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum);

private:
  static int calc_prefix_pattern(const ObString &pattern,
                                  ObCollationType pattern_coll,
                                  const ObString &escape,
                                  ObCollationType escape_coll,
                                  int64_t prefix_len,
                                  int64_t &result_len,
                                  bool &is_valid);
  DISALLOW_COPY_AND_ASSIGN(ObExprPrefixPattern) const;
};
} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_ENGINE_EXPR_OB_EXPR_POWER_
