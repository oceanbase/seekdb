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
#include "share/datum/typed_datum_compare.h"
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
  }
  for (int64_t i = start_index_; OB_SUCC(ret) && i <= end_index_; ++i) {
    if (OB_UNLIKELY(i < 0) || OB_UNLIKELY(i >= plan_ctx->get_parameter_count())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_UNLIKELY(cur_cell_idx >= cur_row->get_count())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      const DatumParameterMetadata *attributes = plan_ctx->get_parameter_metadata(i);
      ObDatum legacy;
      alignas(8) char scratch[64];
      DatumValue owned;
      if (attributes == nullptr) { ret = OB_ERR_UNEXPECTED; }
      else if (OB_FAIL(plan_ctx->export_runtime_parameter_legacy(i, legacy, scratch, sizeof(scratch)))) {
      } else {
        ret = owned.assign_legacy_owned(legacy, attributes->type_meta_,
            expr_ctx.get_exec_context().get_allocator());
      }
      if (OB_SUCC(ret)) { cur_row->get_cell(cur_cell_idx++) = owned; }
    }
  }
  if (OB_SUCC(ret)) {
    result = cur_row;
  }
  return ret;
}
}  // namespace sql
}  // namespace oceanbase
