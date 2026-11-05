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

#include "sql/resolver/ddl/ob_ddl_resolver.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace sql
{

class ObAiColumnResolver
{
public:
  ObAiColumnResolver();
  virtual ~ObAiColumnResolver();

  // Resolve AI COLUMN clause in CREATE TABLE
  static int resolve_ai_column(share::schema::ObTableSchema& table_schema,
                               const common::ObString& column_name,
                               const common::ObString& ai_type,
                               const common::ObString& model_name,
                               const common::ObString& prompt_expr,
                               const common::ObString& sync_mode,
                               int64_t max_retries);

  // Create hidden columns for AI column
  static int create_hidden_columns(share::schema::ObTableSchema& table_schema,
                                   const common::ObString& ai_column_name,
                                   uint64_t& state_column_id,
                                   uint64_t& request_column_id,
                                   uint64_t& result_spec_column_id);

  // Validate AI column configuration
  static int validate_ai_config(const common::ObString& ai_type,
                                const common::ObString& model_name,
                                const common::ObString& prompt_expr);

private:
  DISALLOW_COPY_AND_ASSIGN(ObAiColumnResolver);
};

} // namespace sql
} // namespace oceanbase

#endif // OCEANBASE_SQL_RESOLVER_DDL_OB_AI_COLUMN_RESOLVER_H_
