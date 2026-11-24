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

#ifndef OCEANBASE_SQL_RESOLVER_DDL_OB_AI_COLUMN_RESOLVER_H_
#define OCEANBASE_SQL_RESOLVER_DDL_OB_AI_COLUMN_RESOLVER_H_

#include "lib/container/ob_iarray.h"
#include "share/schema/ob_ai_column_schema.h"
#include "share/schema/ob_column_schema.h"
#include "sql/parser/parse_node.h"

namespace oceanbase
{
namespace sql
{

class ObBasicSessionInfo;
class ObSchemaChecker;

class ObAIColumnResolver final
{
public:
  ObAIColumnResolver(ObBasicSessionInfo &session_info,
                     ObSchemaChecker &schema_checker);
  ~ObAIColumnResolver() = default;

  static bool is_ai_column_definition(const ParseNode *column_definition);

  // Resolve the self-contained definition first. This split keeps parse-tree
  // validation independently testable and prevents any model invocation from
  // being introduced into DDL resolution.
  static int resolve_definition(
      const ParseNode &column_definition,
      const uint64_t table_id,
      const share::schema::ObColumnSchemaV2 &column,
      const common::ObIArray<const share::schema::ObColumnSchemaV2 *> &all_columns,
      share::schema::ObAIColumnSchema &ai_column_schema,
      common::ObString &model_name);

  int resolve(
      const ParseNode &column_definition,
      const uint64_t table_id,
      const share::schema::ObColumnSchemaV2 &column,
      const common::ObIArray<const share::schema::ObColumnSchemaV2 *> &all_columns,
      share::schema::ObAIColumnSchema &ai_column_schema) const;

private:
  ObBasicSessionInfo &session_info_;
  ObSchemaChecker &schema_checker_;
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_RESOLVER_DDL_OB_AI_COLUMN_RESOLVER_H_
