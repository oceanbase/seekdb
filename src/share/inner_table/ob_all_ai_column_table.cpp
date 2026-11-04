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

#define USING_LOG_PREFIX SHARE
#include "ob_all_ai_column_table.h"
#include "share/schema/ob_schema_builder.h"
#include "lib/ob_name_def.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace share
{

ObAllAiColumnTableSchema::ObAllAiColumnTableSchema()
{
}

ObAllAiColumnTableSchema::~ObAllAiColumnTableSchema()
{
}

int ObAllAiColumnTableSchema::get_table_schema(ObIAllocator& allocator,
                                               ObTableSchema& table_schema) const
{
  int ret = OB_SUCCESS;
  UNUSED(allocator);
  UNUSED(table_schema);

  // Placeholder: Table schema generation would be implemented here
  // In production, would use ObSchemaBuilder to construct the table schema
  LOG_INFO("Getting AI column table schema (placeholder)");

  return ret;
}

} // namespace share
} // namespace oceanbase
