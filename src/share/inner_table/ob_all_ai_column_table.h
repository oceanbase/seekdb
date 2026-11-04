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

#ifndef OCEANBASE_SHARE_INNER_TABLE_OB_ALL_AI_COLUMN_TABLE_H_
#define OCEANBASE_SHARE_INNER_TABLE_OB_ALL_AI_COLUMN_TABLE_H_

#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace share
{

// __all_ai_column system table schema
// Stores metadata for AI columns including:
// - column_id: the AI column ID
// - table_id: parent table ID
// - ai_model_name: referenced AI model name
// - ai_type: AI_COMPLETE, AI_EMBED, AI_RERANK
// - prompt_expr: prompt template expression
// - state_column_id: hidden state column
// - request_column_id: hidden request column
// - result_spec_column_id: hidden result spec column
// - sync_mode: 'sync' or 'async'
// - max_retries: maximum retry count
// - retry_delay: delay between retries in seconds
// - status: current processing status
// - last_error: last error message if failed

class ObAllAiColumnTableSchema
{
public:
  ObAllAiColumnTableSchema();
  virtual ~ObAllAiColumnTableSchema();

  int get_table_schema(ObIAllocator& allocator,
                       ObTableSchema& table_schema) const;

private:
  DISALLOW_COPY_AND_ASSIGN(ObAllAiColumnTableSchema);
};

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_INNER_TABLE_OB_ALL_AI_COLUMN_TABLE_H_
