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

// q external_table get_external_file_location pure forwarding declaration(impl lives in ob_external_table_utils.cpp)
#ifndef OCEANBASE_SHARE_EXTERNAL_TABLE_OB_EXTERNAL_FILE_LOCATION_BASIC_H_
#define OCEANBASE_SHARE_EXTERNAL_TABLE_OB_EXTERNAL_FILE_LOCATION_BASIC_H_

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
class ObSchemaGetterGuard;
}

int ob_get_external_file_location(const schema::ObTableSchema &table_schema,
                                  schema::ObSchemaGetterGuard &schema_guard,
                                  common::ObIAllocator &allocator,
                                  common::ObString &file_location);

} // namespace share
} // namespace oceanbase
#endif
