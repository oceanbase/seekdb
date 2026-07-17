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
#include "sql/engine/expr/ob_iter_expr_range_param.h"
#include "sql/engine/ob_exec_context.h"
namespace oceanbase
{
using namespace common;
namespace sql
{
OB_SERIALIZE_MEMBER((ObIterExprRangeParam, ObIterExprOperator), start_index_, end_index_);

int ObIterExprRangeParam::get_next_row(ObIterExprCtx &expr_ctx, const common::ObNewRow *&result) const
{
  int ret = OB_SUCCESS;
  int64_t cur_cell_idx = 0;
  ObNewRow *cur_row = expr_ctx.get_cur_row();
  ObPhysicalPlanCtx *plan_ctx = expr_ctx.get_exec_context().get_physical_plan_ctx();
  if (OB_ISNULL(cur_row) || OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("current row is null", K(ret), K(cur_row), K(plan_ctx));
  }
  for (int64_t i = start_index_; OB_SUCC(ret) && i <= end_index_; ++i) {
    if (OB_UNLIKELY(i < 0) || OB_UNLIKELY(i >= plan_ctx->get_param_store().count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid param index", K(ret), K(i), K_(start_index), K_(end_index), K(plan_ctx->get_param_store().count()));
    } else if (OB_UNLIKELY(cur_cell_idx >= cur_row->get_count())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("current cell index is invalid", K(ret), K(cur_cell_idx), K(cur_row->get_count()));
    } else {
      cur_row->get_cell(cur_cell_idx++) = plan_ctx->get_param_store().at(i);
    }
  }
  if (OB_SUCC(ret)) {
    result = cur_row;
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
