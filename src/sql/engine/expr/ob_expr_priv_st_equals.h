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

#ifndef OCEANBASE_SQL_OB_EXPR_PRIV_ST_EQUALS_H_
#define OCEANBASE_SQL_OB_EXPR_PRIV_ST_EQUALS_H_

#include "sql/engine/expr/ob_expr_operator.h"
#include "observer/omt/ob_tenant_srs.h"
#include "sql/engine/expr/ob_geo_expr_utils.h"

namespace oceanbase
{
namespace sql
{
class ObExprPrivSTEquals : public ObFuncExprOperator
{
public:
  explicit ObExprPrivSTEquals(common::ObIAllocator &alloc);
  virtual ~ObExprPrivSTEquals();
  virtual int calc_result_type2(ObExprResType &type, ObExprResType &type1, ObExprResType &type2,
      common::ObExprTypeCtx &type_ctx) const override;
  static int eval_priv_st_equals(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &res);
  virtual int cg_expr(
      ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr, ObExpr &rt_expr) const override;

private:
  static int get_input_geometry(omt::ObSrsCacheGuard &srs_guard, MultimodeAlloctor &allocator, ObEvalCtx &ctx, ObExpr *gis_arg,
      ObDatum *gis_datum, const ObSrsItem *&srs, ObGeometry *&geo, bool &is_geo_empty);
  DISALLOW_COPY_AND_ASSIGN(ObExprPrivSTEquals);
};
}  // namespace sql
}  // namespace oceanbase
#endif  // OCEANBASE_SQL_OB_EXPR_PRIV_ST_EQUALS_H_
