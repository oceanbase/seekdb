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

#ifndef OCEANBASE_SQL_OB_EXPR_JSON_ARRAY_H_
#define OCEANBASE_SQL_OB_EXPR_JSON_ARRAY_H_

#include "sql/engine/expr/ob_expr_operator.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{
class ObExprJsonArray : public ObFuncExprOperator
{
public:
  enum {
    OPT_NULL_ID,
    OPT_TYPE_ID,
    OPT_STRICT_ID,
    OPT_MAX_ID,
  };

  explicit ObExprJsonArray(common::ObIAllocator &alloc);
  virtual ~ObExprJsonArray();

  virtual int calc_result_typeN(ObExprResType& type,
                                ObExprResType* types,
                                int64_t param_num, 
                                common::ObExprTypeCtx& type_ctx) const override;
                          
  static int eval_json_array(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprJsonArray);
};

} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_JSON_ARRAY_H_
