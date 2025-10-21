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

#ifndef OCEANBASE_SQL_OB_EXPR_ST_ASTEXT_
#define OCEANBASE_SQL_OB_EXPR_ST_ASTEXT_

#include "sql/engine/expr/ob_expr_operator.h"

namespace oceanbase
{
namespace common
{
class ObGeometry;
}
namespace sql
{
class ObExprSTAsText : public ObFuncExprOperator
{
public:
  explicit ObExprSTAsText(common::ObIAllocator &alloc,
                          ObExprOperatorType type, 
                          const char *name,
                          int32_t param_num,
                          int32_t dimension);
  explicit ObExprSTAsText(common::ObIAllocator &alloc);
  virtual ~ObExprSTAsText() {}
  virtual int calc_result_typeN(ObExprResType &type,
                                ObExprResType *types_stack,
                                int64_t param_num,
                                common::ObExprTypeCtx &type_ctx) const override;
  static int eval_st_astext(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            ObDatum &res);
  static int eval_st_astext_common(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &res,
                                   const char *func_name);
  virtual int cg_expr(ObExprCGCtx &expr_cg_ctx,
                      const ObRawExpr &raw_expr,
                      ObExpr &rt_expr) const override;
  static int to_wkt(ObIAllocator &allocator, ObGeometry *geo, ObString &res_wkt, const char *func_name);
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSTAsText);
};

class ObExprSTAsWkt : public ObExprSTAsText
{
public:
  explicit ObExprSTAsWkt(common::ObIAllocator &alloc)
    : ObExprSTAsText(alloc,
                     T_FUN_SYS_ST_ASWKT,
                     N_ST_ASWKT,
                     MORE_THAN_ZERO,
                     NOT_ROW_DIMENSION) {}
  virtual ~ObExprSTAsWkt() {}
  static int eval_st_astext(const ObExpr &expr,
                            ObEvalCtx &ctx,
                            ObDatum &res);
  int cg_expr(ObExprCGCtx &expr_cg_ctx,
              const ObRawExpr &raw_expr,
              ObExpr &rt_expr) const override;
private:
  DISALLOW_COPY_AND_ASSIGN(ObExprSTAsWkt);
};


} // sql
} // oceanbase
#endif // OCEANBASE_SQL_OB_EXPR_ST_ASTEXT_
