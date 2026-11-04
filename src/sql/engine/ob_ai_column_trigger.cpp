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

#define USING_LOG_PREFIX SQL_ENG
#include "ob_ai_column_trigger.h"
#include "share/schema/ob_schema_service.h"
#include "lib/string/ob_string_util.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace sql
{

ObAiColumnTrigger::ObAiColumnTrigger()
{
}

ObAiColumnTrigger::~ObAiColumnTrigger()
{
}

bool ObAiColumnTrigger::has_ai_columns(const share::schema::ObTableSchema& table_schema)
{
  // TODO: Check if table has any AI columns
  // For now, return false as placeholder until DDL parser integration is complete
  UNUSED(table_schema);
  return false;
}

int ObAiColumnTrigger::on_insert(ObExecContext& ctx,
                                 const share::schema::ObTableSchema& table_schema,
                                 const ObNewRow& new_row)
{
  int ret = OB_SUCCESS;

  // TODO: Integrate with DDL parser to identify AI columns
  // For now, this is a placeholder that returns success without processing
  UNUSED(ctx);
  UNUSED(table_schema);
  UNUSED(new_row);

  return ret;
}

int ObAiColumnTrigger::on_update(ObExecContext& ctx,
                                 const share::schema::ObTableSchema& table_schema,
                                 const ObNewRow& old_row,
                                 const ObNewRow& new_row)
{
  int ret = OB_SUCCESS;

  // TODO: Integrate with DDL parser to identify AI columns
  // For now, this is a placeholder that returns success without processing
  UNUSED(ctx);
  UNUSED(table_schema);
  UNUSED(old_row);
  UNUSED(new_row);

  return ret;
}

int ObAiColumnTrigger::process_ai_column(ObExecContext& ctx,
                                         const share::schema::ObTableSchema& table_schema,
                                         uint64_t column_id,
                                         const ObObj& source_value,
                                         uint64_t row_id)
{
  int ret = OB_SUCCESS;

  // Get AI column metadata
  // In production, would query __all_ai_column system table
  ObAiColumnTask task;
  task.tenant_id_ = ctx.get_my_session()->get_effective_tenant_id();
  task.table_id_ = table_schema.get_table_id();
  task.column_id_ = column_id;
  task.row_id_ = row_id;
  task.ai_type_ = ObString("AI_COMPLETE"); // Would come from metadata
  task.model_name_ = ObString("default_model"); // Would come from metadata
  task.prompt_expr_ = source_value.get_varchar(); // Use source value as prompt
  task.sync_mode_ = ObString("async"); // Would come from metadata
  task.max_retries_ = 3;
  task.retry_delay_ = 1;

  // Submit task to scheduler
  if (NULL == g_ai_column_scheduler) {
    ret = OB_NOT_INIT;
    LOG_WARN("AI column scheduler not initialized", K(ret));
  } else if (OB_FAIL(g_ai_column_scheduler->submit_task(task))) {
    LOG_WARN("Failed to submit AI column task", K(ret), K(task));
  } else {
    LOG_INFO("AI column task submitted successfully",
             K(column_id), K(row_id), K(task.ai_type_));
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
