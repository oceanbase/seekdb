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

#ifndef OCEANBASE_SQL_OB_EXPR_JSON_OVERLAPS_H_
#define OCEANBASE_SQL_OB_EXPR_JSON_OVERLAPS_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "lib/json_type/ob_json_tree.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObExprJsonOverlaps : public ObFuncExprOperator
{
public:
  explicit ObExprJsonOverlaps(common::ObIAllocator &alloc);
  virtual ~ObExprJsonOverlaps();
  virtual int calc_result_type2(ObExprResType &type,
                                ObExprResType &type1,
                                ObExprResType &type2,
                                ObExprTypeCtx &type_ctx) const override;
                                
  static int eval_json_overlaps(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  static int json_overlaps_object(ObIJsonBase *json_a, ObIJsonBase *json_b, bool *result);
  static int json_overlaps_array(ObIJsonBase *json_a, ObIJsonBase *json_b, bool *result);
  static int json_overlaps(ObIJsonBase *json_a, ObIJsonBase *json_b, bool *result);
  DISALLOW_COPY_AND_ASSIGN(ObExprJsonOverlaps);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_JSON_OVERLAPS_H_
