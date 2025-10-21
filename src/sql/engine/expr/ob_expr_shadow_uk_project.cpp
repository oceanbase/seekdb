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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/expr/ob_expr_shadow_uk_project.h"

namespace oceanbase
{
using namespace common;
namespace sql
{
int ObExprShadowUKProject::cg_expr(ObExprCGCtx &,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(raw_expr);
  CK(rt_expr.arg_cnt_ >= 2);
  rt_expr.eval_func_ = ObExprShadowUKProject::shadow_uk_project;

  return ret;
}

int ObExprShadowUKProject::shadow_uk_project(const ObExpr &expr,
                                             ObEvalCtx &ctx,
                                             ObDatum &datum)
{
  int ret = OB_SUCCESS;
  bool need_shadow_columns = false;
  if (OB_FAIL(expr.eval_param_value(ctx))) {
    LOG_WARN("evaluate parameters values failed", K(ret));
  } else if (lib::is_mysql_mode()){
    // mysql compatibility: as long as there is a null column in the unique index key, the shadow column needs to be filled
    bool rowkey_has_null = false;
    for (int64_t i = 0; !rowkey_has_null && i < expr.arg_cnt_ - 1; i++) {
      ObDatum &v = expr.locate_param_datum(ctx, i);
      rowkey_has_null = v.is_null();
    }
    need_shadow_columns = rowkey_has_null;
  } else {
    // oracle compatible: Only when all columns of unique index key are null, do we need to fill the shadow column
    bool is_rowkey_all_null = true;
    for (int64_t i = 0; is_rowkey_all_null && i < expr.arg_cnt_ - 1; i++) {
      ObDatum &v = expr.locate_param_datum(ctx, i);
      is_rowkey_all_null = v.is_null();
    }
    need_shadow_columns = is_rowkey_all_null;
  }
  if (!need_shadow_columns) {
    datum.set_null();
  } else {
    ObDatum &v = expr.locate_param_datum(ctx, expr.arg_cnt_ - 1);
    datum.set_datum(v);
  }

  return ret;
}

}  // namespace sql
}  // namespace oceanbase
