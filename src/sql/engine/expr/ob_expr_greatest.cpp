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

#include "sql/engine/expr/ob_expr_greatest.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

ObExprGreatest::ObExprGreatest(ObIAllocator &alloc)
    : ObExprLeastGreatest(alloc,
                           T_FUN_SYS_GREATEST,
                           N_GREATEST,
                           MORE_THAN_ZERO)
{
}

//same type params
int ObExprGreatest::calc_greatest(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  int ret = OB_SUCCESS;
  ret = ObExprLeastGreatest::calc_mysql(expr, ctx, expr_datum, false);
  return ret;
}

}
}
