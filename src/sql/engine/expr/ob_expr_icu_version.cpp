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

#include "sql/engine/expr/ob_expr_icu_version.h"
#include "sql/engine/expr/ob_expr_regexp_context.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObExprICUVersion::ObExprICUVersion(ObIAllocator &alloc)
  : ObStringExprOperator(alloc, T_FUN_SYS_ICU_VERSION, N_ICU_VERSION, 0, NOT_VALID_FOR_GENERATED_COL)
{
}

ObExprICUVersion::~ObExprICUVersion()
{
}

int ObExprICUVersion::calc_result_type0(ObExprResType &type, ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  type.set_varchar();
  type.set_length(static_cast<common::ObLength>(
                    strlen(ObExprRegexContext::icu_version_string())));
  type.set_default_collation_type();
  type.set_collation_level(CS_LEVEL_SYSCONST);
  return OB_SUCCESS;
}

int ObExprICUVersion::eval_version(const ObExpr &expr,
                                   ObEvalCtx &ctx,
                                   ObDatum &expr_datum)
{
  UNUSED(expr);
  UNUSED(ctx);
  int ret = OB_SUCCESS;
  expr_datum.set_string(common::ObString(ObExprRegexContext::icu_version_string()));
  return ret;
}

int ObExprICUVersion::cg_expr(ObExprCGCtx &op_cg_ctx,
                              const ObRawExpr &raw_expr,
                              ObExpr &rt_expr) const
{
  UNUSED(raw_expr);
  UNUSED(op_cg_ctx);
  int ret = OB_SUCCESS;
  rt_expr.eval_func_ = ObExprICUVersion::eval_version;
  return ret;
}

} // namespace sql
} // namespace oceanbase
