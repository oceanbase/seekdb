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

#ifndef OCEANBASE_QUERY_DDL_OB_DYNAMIC_PARTITION_POLICY_H_
#define OCEANBASE_QUERY_DDL_OB_DYNAMIC_PARTITION_POLICY_H_

#include <cstdint>

namespace oceanbase
{
namespace common
{
class ObIAllocator;
class ObString;
}
namespace share
{
namespace schema
{
class ObTableSchema;
}
}
namespace query
{

int format_dynamic_partition_policy(common::ObIAllocator &allocator,
                                    const common::ObString &original,
                                    common::ObString &formatted);
int fill_dynamic_partition_policy_defaults(common::ObIAllocator &allocator,
                                           const common::ObString &original,
                                           common::ObString &completed);
int print_dynamic_partition_policy(const share::schema::ObTableSchema &table_schema,
                                   char *buf,
                                   int64_t buf_len,
                                   int64_t &pos);

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_DDL_OB_DYNAMIC_PARTITION_POLICY_H_
