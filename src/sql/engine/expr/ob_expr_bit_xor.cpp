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
#include "sql/engine/expr/ob_expr_bit_xor.h"

namespace oceanbase
{
using namespace oceanbase::common;
namespace sql
{

ObExprBitXor::ObExprBitXor(ObIAllocator &alloc)
    : ObBitwiseExprOperator(alloc, T_OP_BIT_XOR, N_BIT_XOR, 2, NOT_ROW_DIMENSION)
{
}

int ObExprBitXor::cg_expr(ObExprCGCtx &expr_cg_ctx, const ObRawExpr &raw_expr,
                       ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  BitOperator op = BIT_XOR;
  if (OB_FAIL(cg_bitwise_expr(expr_cg_ctx, raw_expr, rt_expr, op))) {
    LOG_WARN("cg_bitwise_expr failed", K(ret), K(rt_expr), K(op));
  }

  return ret;
}
} // namespace sql
} // namespace oceanbase
