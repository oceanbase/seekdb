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

#include "str_prefix_max.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{

#define INIT_STR_PREFIX_AGG_CASE(vec_tc)                                                  \
  case (vec_tc): {                                                                        \
    ret = init_agg_func<StrPrefixMax<vec_tc>>(                                            \
      agg_ctx, agg_col_id, aggr_info.has_distinct_, allocator, agg);                      \
  } break

int init_string_prefix_max_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                                    ObIAllocator &allocator, IAggregate *&agg)
{
  int ret = OB_SUCCESS;
  agg = nullptr;
  ObAggrInfo &aggr_info = agg_ctx.locate_aggr_info(agg_col_id);
  if (OB_ISNULL(aggr_info.expr_)) {
    ret = OB_ERR_UNEXPECTED;
    SQL_LOG(WARN, "invalid null expr", K(ret));
  } else {
    VecValueTypeClass res_vec =
      get_vec_value_tc(aggr_info.expr_->datum_meta_.type_, aggr_info.expr_->datum_meta_.scale_,
                      aggr_info.expr_->datum_meta_.precision_);
    switch (res_vec) {
      LST_DO_CODE(INIT_STR_PREFIX_AGG_CASE, VEC_TC_STRING, VEC_TC_LOB);
    default: {
      ret = OB_ERR_UNEXPECTED;
      SQL_LOG(WARN, "unexpected result type of min/max aggregate", K(ret), K(res_vec));
    }
    }
  }
  return ret;
}

#undef INIT_STR_PREFIX_AGG_CASE

}
}
}
}
