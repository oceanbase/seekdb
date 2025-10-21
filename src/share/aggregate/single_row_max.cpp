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
#define USING_LOG_PREFIX SQL

#include "single_row_max.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{
int init_single_row_max_aggregate(RuntimeContext &agg_ctx, const int col_id,
                                  ObIAllocator &allocator, IAggregate *&agg)
{
  int ret = OB_SUCCESS;
  agg = nullptr;
  ObAggrInfo &aggr_info = agg_ctx.aggr_infos_.at(col_id);
  VecValueTypeClass param_vec = aggr_info.param_exprs_.at(0)->get_vec_value_tc();
  if (aggr_info.get_expr_type() == T_FUN_MAX) {
    switch (param_vec) {
      LST_DO_CODE(INIT_GENERAL_MAX_CASE, AGG_VEC_TC_LIST);
      default: {
        ret = OB_ERR_UNEXPECTED;
        break;
      }
    }
  }
  if (OB_FAIL(ret)) {
    SQL_LOG(WARN, "init count aggregate failed", K(ret));
  }
  return ret;
}
} // end helper
} // end aggregate
} // end share
} // end oceanbase
