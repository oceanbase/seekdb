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

#ifndef OCEANBASE_QUERY_SPLIT_OB_SPLIT_RANGE_SAMPLER_H_
#define OCEANBASE_QUERY_SPLIT_OB_SPLIT_RANGE_SAMPLER_H_

#include "common/ob_range.h"
#include "common/ob_tablet_id.h"
#include "lib/allocator/page_arena.h"
#include "lib/container/ob_array.h"
#include "lib/container/ob_iarray.h"
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace query
{

int sample_partition_split_ranges(
    const common::ObString &database_name,
    const share::schema::ObTableSchema &table_schema,
    const common::ObTabletID &tablet_id,
    int64_t range_count,
    int64_t used_disk_space,
    common::ObArenaAllocator &range_allocator,
    common::ObArray<common::ObNewRange> &ranges);

int sample_column_split_ranges(
    const common::ObString &database_name,
    const share::schema::ObTableSchema &data_table_schema,
    const common::ObIArray<common::ObString> &column_names,
    const common::ObIArray<common::ObNewRange> &column_ranges,
    int64_t range_count,
    int64_t used_disk_space,
    common::ObArenaAllocator &range_allocator,
    common::ObArray<common::ObNewRange> &ranges);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_SPLIT_OB_SPLIT_RANGE_SAMPLER_H_
