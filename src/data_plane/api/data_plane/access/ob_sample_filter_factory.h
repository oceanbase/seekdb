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

#ifndef OCEANBASE_DATA_PLANE_ACCESS_OB_SAMPLE_FILTER_FACTORY_H_
#define OCEANBASE_DATA_PLANE_ACCESS_OB_SAMPLE_FILTER_FACTORY_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace sql
{
class ObPushdownFilterExecutor;
class ObPushdownFilterNode;
class ObPushdownOperator;
}
namespace data_plane
{

// Storage supplies the concrete sample-filter implementations.  Query only
// binds them into its generic pushdown factory through this public seam.
int alloc_sample_filter_node(common::ObIAllocator &allocator,
                             uint32_t child_count,
                             sql::ObPushdownFilterNode *&filter_node);
int alloc_hybrid_sample_filter_executor(
    common::ObIAllocator &allocator,
    uint32_t child_count,
    sql::ObPushdownFilterNode &filter_node,
    sql::ObPushdownFilterExecutor *&filter_executor,
    sql::ObPushdownOperator &op);
int alloc_trival_sample_filter_executor(
    common::ObIAllocator &allocator,
    uint32_t child_count,
    sql::ObPushdownFilterNode &filter_node,
    sql::ObPushdownFilterExecutor *&filter_executor,
    sql::ObPushdownOperator &op);

} // namespace data_plane
} // namespace oceanbase

#endif // OCEANBASE_DATA_PLANE_ACCESS_OB_SAMPLE_FILTER_FACTORY_H_
