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

#ifndef OB_SHARE_AGG_APX_CNT_DISTINCT_SYN_H_
#define OB_SHARE_AGG_APX_CNT_DISTINCT_SYN_H_

#include "approx_count_distinct.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{
int init_approx_count_distinct_synopsis_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                                                  ObIAllocator &allocator, IAggregate *&agg);
} // end helper
} // end aggregate
} // end share
} // end oceanbase
#endif // OB_SHARE_AGG_APX_CNT_DISTINCT_SYN_H_
