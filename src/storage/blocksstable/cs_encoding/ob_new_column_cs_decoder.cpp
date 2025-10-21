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

#define USING_LOG_PREFIX STORAGE

#include "ob_new_column_cs_decoder.h"
#include "storage/access/ob_aggregate_base.h"

namespace oceanbase
{
namespace blocksstable
{
using namespace common;

int ObNewColumnCSDecoder::get_aggregate_result(
    const ObColumnCSDecoderCtx &ctx,
    const ObPushdownRowIdCtx &pd_row_id_ctx,
    storage::ObAggCellBase &agg_cell) const
{
  UNUSED(pd_row_id_ctx);
  int ret = OB_SUCCESS;
  ObStorageDatum datum;
  if (OB_FAIL(common_decoder_.decode(ctx.get_col_param(), ctx.get_allocator(), datum))) {
    LOG_WARN("Failed to decode datum", K(ret), K(ctx.get_col_param()[0].get_orig_default_value()));
  } else if (OB_FAIL(agg_cell.eval(datum))) {
    LOG_WARN("Failed to eval datum", K(ret), K(datum));
  }
  LOG_DEBUG("[NEW_COLUMN_DECODE] get aggregate(min/max) result", K(datum), K(lbt()));
  return ret;
}

} // end of namespace oceanbase
} // end of namespace oceanbase
