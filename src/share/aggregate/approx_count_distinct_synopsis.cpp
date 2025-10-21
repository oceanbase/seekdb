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

#include "approx_count_distinct_synopsis.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{
int init_approx_count_distinct_synopsis_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                                                  ObIAllocator &allocator, IAggregate *&agg)
{
#define INIT_APP_CNT_DIST_SYN_CASE(vec_tc)                                                         \
  case (vec_tc): {                                                                                 \
    ret = init_agg_func<                                                                           \
      ApproxCountDistinct<T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS, vec_tc, VEC_TC_STRING>>(           \
      agg_ctx, agg_col_id, has_distinct, allocator, agg);                                          \
  } break

  int ret = OB_SUCCESS;
  ObAggrInfo &aggr_info = agg_ctx.locate_aggr_info(agg_col_id);
  bool has_distinct = aggr_info.has_distinct_;
  if (T_FUN_APPROX_COUNT_DISTINCT_SYNOPSIS == aggr_info.get_expr_type()) {
    VecValueTypeClass vec_tc =
      get_vec_value_tc(aggr_info.get_first_child_type(), aggr_info.get_first_child_datum_scale(),
                       aggr_info.get_first_child_datum_precision());
    switch (vec_tc) {
      LST_DO_CODE(INIT_APP_CNT_DIST_SYN_CASE, AGG_VEC_TC_LIST);
      default: {
        ret = OB_ERR_UNEXPECTED;
        SQL_LOG(WARN, "invalid param format", K(ret), K(vec_tc));
      }
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid function type", K(ret), K(aggr_info.get_expr_type()), K(aggr_info));
  }
  return ret;
}
} // end helper
} // end aggregate
} // end share
} // end oceanbase
