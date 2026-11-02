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
#include "rb_agg.h"

namespace oceanbase
{
namespace share
{
namespace aggregate
{
namespace helper
{
int init_rb_build_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                         ObIAllocator &allocator, IAggregate *&agg)
{
  return init_rb_aggregate<T_FUN_SYS_RB_BUILD_AGG>(agg_ctx, agg_col_id, allocator, agg);
}

int init_rb_and_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                          ObIAllocator &allocator, IAggregate *&agg)
{
  return init_rb_aggregate<T_FUN_SYS_RB_AND_AGG>(agg_ctx, agg_col_id, allocator, agg);
}

int init_rb_or_aggregate(RuntimeContext &agg_ctx, const int64_t agg_col_id,
                         ObIAllocator &allocator, IAggregate *&agg)
{
  return init_rb_aggregate<T_FUN_SYS_RB_OR_AGG>(agg_ctx, agg_col_id, allocator, agg);
}

} // end namespace helper
} // end namespace aggregate
} // end namespace share
} // end namespace oceanbase
