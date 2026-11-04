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

#define USING_LOG_PREFIX SQL_RESOLVE
#include "ob_ai_column_resolver.h"
#include "share/schema/ob_schema_builder.h"
#include "lib/ob_name_def.h"
#include "common/ob_obj_type.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace sql
{

ObAiColumnResolver::ObAiColumnResolver()
{
}

ObAiColumnResolver::~ObAiColumnResolver()
{
}

int ObAiColumnResolver::validate_ai_config(const ObString& ai_type,
                                           const ObString& model_name,
                                           const ObString& prompt_expr)
{
  int ret = OB_SUCCESS;

  // Validate AI type
  if (ai_type.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("AI type cannot be empty", K(ret));
  } else if (ai_type.case_compare("AI_COMPLETE") != 0 &&
             ai_type.case_compare("AI_EMBED") != 0 &&
             ai_type.case_compare("AI_RERANK") != 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "ai_type",
                   "must be one of: AI_COMPLETE, AI_EMBED, AI_RERANK");
  } else if (model_name.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, "model_name", "cannot be empty");
  }

  // Validate prompt expression based on AI type
  if (OB_SUCC(ret)) {
    if (ai_type.case_compare("AI_COMPLETE") == 0 ||
        ai_type.case_compare("AI_EMBED") == 0) {
      if (prompt_expr.empty()) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "prompt_expr",
                       "is required for AI_COMPLETE and AI_EMBED");
      }
    }
  }

  return ret;
}

int ObAiColumnResolver::create_hidden_columns(ObTableSchema& table_schema,
                                              const ObString& ai_column_name,
                                              uint64_t& state_column_id,
                                              uint64_t& request_column_id,
                                              uint64_t& result_spec_column_id)
{
  int ret = OB_SUCCESS;
  ObSchemaBuilder schema_builder(table_schema.get_allocator());

  // Create state column: stores processing state (PENDING/PROCESSING/COMPLETED/FAILED)
  ObString state_col_name;
  char state_buf[OB_MAX_COLUMN_NAME_LENGTH];
  snprintf(state_buf, sizeof(state_buf), "__ai_state_%.*s",
           static_cast<int>(ai_column_name.length()), ai_column_name.ptr());
  state_col_name.assign_ptr(state_buf, static_cast<int32_t>(strlen(state_buf)));

  if (OB_FAIL(schema_builder.add_varchar_column(state_col_name, 32, NOT_NULL))) {
    LOG_WARN("Failed to add state column", K(ret), K(state_col_name));
  } else if (OB_FAIL(schema_builder.set_column_hidden(true))) {
    LOG_WARN("Failed to set column as hidden", K(ret));
  } else {
    state_column_id = schema_builder.get_current_column_id();
  }

  // Create request column: stores the request payload
  ObString request_col_name;
  char request_buf[OB_MAX_COLUMN_NAME_LENGTH];
  snprintf(request_buf, sizeof(request_buf), "__ai_request_%.*s",
           static_cast<int>(ai_column_name.length()), ai_column_name.ptr());
  request_col_name.assign_ptr(request_buf, static_cast<int32_t>(strlen(request_buf)));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_builder.add_text_column(request_col_name, CAN_NULL))) {
      LOG_WARN("Failed to add request column", K(ret), K(request_col_name));
    } else if (OB_FAIL(schema_builder.set_column_hidden(true))) {
      LOG_WARN("Failed to set column as hidden", K(ret));
    } else {
      request_column_id = schema_builder.get_current_column_id();
    }
  }

  // Create result_spec column: stores result specification
  ObString result_col_name;
  char result_buf[OB_MAX_COLUMN_NAME_LENGTH];
  snprintf(result_buf, sizeof(result_buf), "__ai_result_%.*s",
           static_cast<int>(ai_column_name.length()), ai_column_name.ptr());
  result_col_name.assign_ptr(result_buf, static_cast<int32_t>(strlen(result_buf)));

  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_builder.add_text_column(result_col_name, CAN_NULL))) {
      LOG_WARN("Failed to add result column", K(ret), K(result_col_name));
    } else if (OB_FAIL(schema_builder.set_column_hidden(true))) {
      LOG_WARN("Failed to set column as hidden", K(ret));
    } else {
      result_spec_column_id = schema_builder.get_current_column_id();
    }
  }

  // Add columns to table schema
  if (OB_SUCC(ret)) {
    ObTableSchema& mutable_schema = table_schema;
    if (OB_FAIL(mutable_schema.add_column(schema_builder.get_column_schema()))) {
      LOG_WARN("Failed to add hidden columns to table", K(ret));
    }
  }

  return ret;
}

int ObAiColumnResolver::resolve_ai_column(ObTableSchema& table_schema,
                                          const ObString& column_name,
                                          const ObString& ai_type,
                                          const ObString& model_name,
                                          const ObString& prompt_expr,
                                          const ObString& sync_mode,
                                          int64_t max_retries)
{
  int ret = OB_SUCCESS;

  // Validate configuration
  if (OB_FAIL(validate_ai_config(ai_type, model_name, prompt_expr))) {
    LOG_WARN("Invalid AI column configuration", K(ret));
  } else {
    // Create hidden columns
    uint64_t state_column_id = OB_INVALID_ID;
    uint64_t request_column_id = OB_INVALID_ID;
    uint64_t result_spec_column_id = OB_INVALID_ID;

    if (OB_FAIL(create_hidden_columns(table_schema, column_name,
                                      state_column_id,
                                      request_column_id,
                                      result_spec_column_id))) {
      LOG_WARN("Failed to create hidden columns", K(ret));
    } else {
      // Store AI column metadata in __all_ai_column system table
      // This will be done by inserting into the system table during DDL execution
      LOG_INFO("AI column resolved successfully",
               K(column_name), K(ai_type), K(model_name),
               K(state_column_id), K(request_column_id), K(result_spec_column_id),
               K(sync_mode), K(max_retries));
    }
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
