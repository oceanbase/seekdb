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

#include "hybrid_hist.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{

int init_hybrid_hist_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                               ObIAllocator &allocator, IAggregate *&agg)
{
  int ret = OB_SUCCESS;
  ObAggrInfo &aggr_info = agg_ctx.locate_aggr_info(agg_col_id);
  ObDatumMeta &param_meta = aggr_info.param_exprs_.at(0)->datum_meta_;
  bool has_distinct = aggr_info.has_distinct_;
  if (OB_FAIL(init_agg_func<HybridHist>(agg_ctx, 
                                        agg_col_id, 
                                        has_distinct, 
                                        allocator, 
                                        agg, 
                                        true))) {
    SQL_LOG(WARN, "failed init agg func", K(ret));
  }
  return ret;
}

}
}
}
}
