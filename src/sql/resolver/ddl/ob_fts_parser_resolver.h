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

#ifndef OCEANBASE_FTS_PARSER_RESOLVER_
#define OCEANBASE_FTS_PARSER_RESOLVER_

#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "storage/fts/ob_fts_parser_property.h"
#include "storage/fts/ob_fts_plugin_helper.h"
#include "share/schema/ob_schema_getter_guard.h"

namespace oceanbase
{
namespace sql
{
class ObSchemaChecker;
class ObFTParserResolverHelper final
{
public:
  ObFTParserResolverHelper() = default;
  ~ObFTParserResolverHelper() = default;

  static int resolve_parser_properties(
      const common::ObString &index_database_name,
      const ParseNode &parse_tree,
      const uint64_t tenant_id,
      common::ObIAllocator &allocator,
      ObSchemaChecker *schema_checker,
      common::ObString &parser_property);

  static int resolve_dict_table_name_and_id(
      const common::ObString &index_database_name,
      const common::ObString &table_name,
      const uint64_t tenant_id,
      share::schema::ObSchemaGetterGuard &schema_guard,
      common::ObIAllocator &allocator,
      uint64_t &table_id,
      common::ObString &full_table_name);

private:
  static int resolve_fts_index_parser_properties(
      const common::ObString &index_database_name,
      const ParseNode *node,
      const uint64_t tenant_id,
      storage::ObFTParserJsonProps &property,
      common::ObIAllocator &allocator,
      ObSchemaChecker &schema_checker);
  static int resolve_table_config(
      const common::ObString &index_database_name,
      const ParseNode *node,
      const uint64_t tenant_id,
      storage::ObFTParserJsonProps &property,
      common::ObIAllocator &allocator,
      ObSchemaChecker &schema_checker,
      int (storage::ObFTParserJsonProps::*set_name)(const common::ObString &),
      int (storage::ObFTParserJsonProps::*set_id)(const uint64_t));
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_FTS_PARSER_RESOLVER_ */
