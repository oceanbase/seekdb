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

  // 解析全文索引属性，并在属性写入 schema 前校验其中引用的词典表。
  static int resolve_parser_properties(
      const ParseNode &parse_tree,
      common::ObIAllocator &allocator,
      ObSchemaChecker &schema_checker,
      const common::ObString &current_database_name,
      common::ObString &parser_property);

private:
  static int resolve_fts_index_parser_properties(const ParseNode *node,
                                                 storage::ObFTParserJsonProps &property,
                                                 ObSchemaChecker &schema_checker,
                                                 const common::ObString &current_database_name);
  // 解析 db.table 或 table，并确认目标是合法的全文词典表。
  static int resolve_and_validate_dict_table_(ObSchemaChecker &schema_checker,
                                              const common::ObString &current_database_name,
                                              const common::ObString &raw_table_name,
                                              uint64_t &dict_table_id);
};

} // end namespace sql
} // end namespace oceanbase

#endif /* OCEANBASE_FTS_PARSER_RESOLVER_ */
