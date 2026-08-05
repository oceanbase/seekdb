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

#ifndef OCEANBASE_QUERY_API_RESOLVER_OB_SCHEMA_LOOKUP_H_
#define OCEANBASE_QUERY_API_RESOLVER_OB_SCHEMA_LOOKUP_H_

#include <stdint.h>
#include "lib/string/ob_string.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
class ObDatabaseSchema;
class ObSchemaGetterGuard;
class ObTableSchema;
}
}
namespace query
{

// Narrow, stateful schema lookup capability for non-query command handlers.
// Resolver policy and the concrete ObSchemaChecker stay inside SQL.
class ObSchemaLookup
{
public:
  ObSchemaLookup();
  ~ObSchemaLookup();

  int init(share::schema::ObSchemaGetterGuard &schema_guard,
           uint64_t session_id);

  int get_table_schema(
      const common::ObString &database_name,
      const common::ObString &table_name,
      bool is_index_table,
      const share::schema::ObTableSchema *&table_schema,
      bool with_hidden_flag = false,
      bool is_built_in_index = false);

  int get_table_schema(
      uint64_t table_id,
      const share::schema::ObTableSchema *&table_schema);

  int get_database_schema(
      uint64_t database_id,
      const share::schema::ObDatabaseSchema *&database_schema);

private:
  ObSchemaLookup(const ObSchemaLookup &) = delete;
  ObSchemaLookup &operator=(const ObSchemaLookup &) = delete;

private:
  void *impl_;
};

} // namespace query
} // namespace oceanbase

#endif // OCEANBASE_QUERY_API_RESOLVER_OB_SCHEMA_LOOKUP_H_
