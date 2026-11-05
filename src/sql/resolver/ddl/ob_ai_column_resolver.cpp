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
#include "lib/ob_name_def.h"

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
  UNUSED(table_schema);
  UNUSED(ai_column_name);

  // Placeholder: Hidden column creation would be implemented here
  // In production, would create three hidden columns:
  // 1. __ai_state_<name>: stores processing state
  // 2. __ai_request_<name>: stores request payload
  // 3. __ai_result_<name>: stores AI result

  state_column_id = OB_INVALID_ID;
  request_column_id = OB_INVALID_ID;
  result_spec_column_id = OB_INVALID_ID;

  LOG_INFO("Creating hidden columns for AI column (placeholder)", K(ai_column_name));

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
