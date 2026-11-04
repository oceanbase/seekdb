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
  // Check if table has any AI columns
  for (int64_t i = 0; i < table_schema.get_column_count(); i++) {
    const ObColumnSchemaV2* column = table_schema.get_column(i);
    if (NULL != column && column->is_ai_column()) {
      return true;
    }
  }
  return false;
}

int ObAiColumnTrigger::on_insert(ObExecContext& ctx,
                                 const share::schema::ObTableSchema& table_schema,
                                 const ObNewRow& new_row)
{
  int ret = OB_SUCCESS;

  if (!has_ai_columns(table_schema)) {
    return ret; // No AI columns, nothing to do
  }

  // Get row ID (primary key)
  uint64_t row_id = OB_INVALID_ID;
  if (new_row.row_key_cell_cnt_ > 0 && new_row.cells_[0].is_int()) {
    row_id = new_row.cells_[0].get_int();
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Cannot extract row ID from new row", K(ret));
  }

  // Process each AI column
  for (int64_t i = 0; i < table_schema.get_column_count() && OB_SUCC(ret); i++) {
    const ObColumnSchemaV2* column = table_schema.get_column(i);
    if (NULL == column || !column->is_ai_column()) {
      continue;
    }

    uint64_t column_id = column->get_column_id();
    int64_t cell_idx = column->get_cell_index();

    if (cell_idx < 0 || cell_idx >= new_row.count_) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Invalid cell index for AI column", K(ret), K(column_id), K(cell_idx));
      continue;
    }

    const ObObj& source_value = new_row.cells_[cell_idx];
    if (source_value.is_null()) {
      continue; // Skip NULL values
    }

    // Trigger AI processing
    if (OB_FAIL(process_ai_column(ctx, table_schema, column_id, source_value, row_id))) {
      LOG_WARN("Failed to process AI column", K(ret), K(column_id));
    }
  }

  return ret;
}

int ObAiColumnTrigger::on_update(ObExecContext& ctx,
                                 const share::schema::ObTableSchema& table_schema,
                                 const ObNewRow& old_row,
                                 const ObNewRow& new_row)
{
  int ret = OB_SUCCESS;

  if (!has_ai_columns(table_schema)) {
    return ret; // No AI columns, nothing to do
  }

  // Get row ID
  uint64_t row_id = OB_INVALID_ID;
  if (new_row.row_key_cell_cnt_ > 0 && new_row.cells_[0].is_int()) {
    row_id = new_row.cells_[0].get_int();
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("Cannot extract row ID from updated row", K(ret));
  }

  // Check which AI columns changed
  for (int64_t i = 0; i < table_schema.get_column_count() && OB_SUCC(ret); i++) {
    const ObColumnSchemaV2* column = table_schema.get_column(i);
    if (NULL == column || !column->is_ai_column()) {
      continue;
    }

    uint64_t column_id = column->get_column_id();
    int64_t cell_idx = column->get_cell_index();

    if (cell_idx < 0 || cell_idx >= new_row.count_ || cell_idx >= old_row.count_) {
      continue;
    }

    const ObObj& old_value = old_row.cells_[cell_idx];
    const ObObj& new_value = new_row.cells_[cell_idx];

    // Only trigger if value changed
    if (old_value == new_value) {
      continue;
    }

    if (new_value.is_null()) {
      continue; // Skip NULL values
    }

    // Trigger AI processing for updated value
    if (OB_FAIL(process_ai_column(ctx, table_schema, column_id, new_value, row_id))) {
      LOG_WARN("Failed to process updated AI column", K(ret), K(column_id));
    }
  }

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
