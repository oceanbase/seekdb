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

#include "approx_count_distinct.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{
extern int init_approx_count_distinct_mysql_aggregate(RuntimeContext &agg_ctx,
                                                      const int64_t agg_col_id,
                                                      ObIAllocator &allocator, IAggregate *&agg);
extern int init_approx_count_distinct_oracle_aggregate(RuntimeContext &agg_ctx,
                                                       const int64_t agg_col_id,
                                                       ObIAllocator &allocator, IAggregate *&agg);
int init_approx_count_distinct_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                                         ObIAllocator &allocator, IAggregate *&agg)
{
  int ret = OB_SUCCESS;
  if (lib::is_mysql_mode()) {
    ret = init_approx_count_distinct_mysql_aggregate(agg_ctx, agg_col_id, allocator, agg);
  } else {
    ret = init_approx_count_distinct_oracle_aggregate(agg_ctx, agg_col_id, allocator, agg);
  }
  return ret;
}

int init_approx_count_distinct_synopsis_merge_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                                         ObIAllocator &allocator, IAggregate *&agg)
{
  int ret = OB_SUCCESS;
  ObAggrInfo &aggr_info = agg_ctx.locate_aggr_info(agg_col_id);
  if (T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE == aggr_info.get_expr_type()) {
    ObDatumMeta &param_meta = aggr_info.param_exprs_.at(0)->datum_meta_;
    VecValueTypeClass in_tc =
      get_vec_value_tc(param_meta.type_, param_meta.scale_, param_meta.precision_);
    if (in_tc != VEC_TC_STRING) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid input type", K(in_tc), K(param_meta));
    } else {
      ret = init_agg_func<ApproxCountDistinct<T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS_MERGE,
                                              VEC_TC_STRING, VEC_TC_STRING>>(
        agg_ctx, agg_col_id, aggr_info.has_distinct_, allocator, agg);
    }
  }
  return ret;
}
}
} // end aggregate
} // end share
} // end oceanbase
