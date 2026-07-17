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

#include "sql/engine/expr/ob_expr_part_id_pseudo_column.h"

namespace oceanbase
{
namespace sql
{
using namespace oceanbase::common;

ObExprPartIdPseudoColumn::ObExprPartIdPseudoColumn(ObIAllocator &alloc)
  : ObFuncExprOperator(alloc, T_PDML_PARTITION_ID, N_PDML_PARTITION_ID, 0, NOT_VALID_FOR_GENERATED_COL, NOT_ROW_DIMENSION,
                       INTERNAL_IN_MYSQL_MODE, INTERNAL_IN_ORACLE_MODE)
{
  // do nothing
}

ObExprPartIdPseudoColumn::~ObExprPartIdPseudoColumn()
{
  // do nothing
}

int ObExprPartIdPseudoColumn::calc_result_type0(ObExprResType &type,
                                                ObExprTypeCtx &type_ctx) const
{
  UNUSED(type_ctx);
  int ret = OB_SUCCESS;
  type.set_int(); // Default partition id data type is int64_t
  return ret;
}

int ObExprPartIdPseudoColumn::cg_expr(ObExprCGCtx &op_cg_ctx,
                                      const ObRawExpr &raw_expr,
                                      ObExpr &rt_expr) const
{
  int ret = OB_SUCCESS;
  UNUSED(op_cg_ctx);
  UNUSED(raw_expr);
  rt_expr.eval_func_ = ObExprPartIdPseudoColumn::eval_part_id;
  return ret;
}

int ObExprPartIdPseudoColumn::eval_part_id(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &expr_datum)
{
  // partition id pseudo list expression comparison is special, its corresponding value is set to the datum of expr in advance
  // Each time it is directly obtained through local_expr_dutam, direct access
  UNUSED(expr);
  UNUSED(ctx);
  UNUSED(expr_datum);
  return OB_SUCCESS;
}

} // end oceanbase
} // end sql
