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

#define USING_LOG_PREFIX SHARE
#include "ob_all_ai_column_table.h"
#include "share/schema/ob_schema_builder.h"
#include "lib/ob_name_def.h"

using namespace oceanbase::common;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace share
{

ObAllAiColumnTableSchema::ObAllAiColumnTableSchema()
    : ObInnerTableSchema(OB_ALL_AI_COLUMN_TNAME, OB_SYS_TENANT_ID)
{
}

ObAllAiColumnTableSchema::~ObAllAiColumnTableSchema()
{
}

int ObAllAiColumnTableSchema::get_table_schema(ObIAllocator& allocator,
                                               ObTableSchema& table_schema) const
{
  int ret = OB_SUCCESS;
  ObSchemaBuilder schema_builder(allocator);

  // Basic table properties
  if (OB_FAIL(schema_builder.set_table_type(USER_TABLE))) {
    LOG_WARN("Failed to set table type", K(ret));
  } else if (OB_FAIL(schema_builder.set_tablegroup_name(""))) {
    LOG_WARN("Failed to set tablegroup name", K(ret));
  } else if (OB_FAIL(schema_builder.set_description("AI column metadata table"))) {
    LOG_WARN("Failed to set description", K(ret));
  }

  // Primary key columns
  if (OB_SUCC(ret)) {
    // column_id
    if (OB_FAIL(schema_builder.add_varchar_column("column_id",
                                                  OB_MAX_COLUMN_NAME_LENGTH,
                                                  NOT_NULL))) {
      LOG_WARN("Failed to add column_id", K(ret));
    // table_id
    } else if (OB_FAIL(schema_builder.add_int_column("table_id", NOT_NULL))) {
      LOG_WARN("Failed to add table_id", K(ret));
    // database_id (denormalized for query performance)
    } else if (OB_FAIL(schema_builder.add_int_column("database_id", NOT_NULL))) {
      LOG_WARN("Failed to add database_id", K(ret));
    }
  }

  // AI configuration columns
  if (OB_SUCC(ret)) {
    // ai_model_name: referenced AI model
    if (OB_FAIL(schema_builder.add_varchar_column("ai_model_name",
                                                  OB_MAX_AI_MODEL_NAME_LENGTH,
                                                  NOT_NULL))) {
      LOG_WARN("Failed to add ai_model_name", K(ret));
    // ai_type: AI_COMPLETE, AI_EMBED, AI_RERANK
    } else if (OB_FAIL(schema_builder.add_tinyint_column("ai_type", NOT_NULL))) {
      LOG_WARN("Failed to add ai_type", K(ret));
    // prompt_expr: prompt template
    } else if (OB_FAIL(schema_builder.add_text_column("prompt_expr", CAN_NULL))) {
      LOG_WARN("Failed to add prompt_expr", K(ret));
    // normalized_expr: normalized prompt for caching
    } else if (OB_FAIL(schema_builder.add_text_column("normalized_expr", CAN_NULL))) {
      LOG_WARN("Failed to add normalized_expr", K(ret));
    }
  }

  // Hidden column references
  if (OB_SUCC(ret)) {
    // state_column_id: tracks processing state
    if (OB_FAIL(schema_builder.add_int_column("state_column_id", NOT_NULL))) {
      LOG_WARN("Failed to add state_column_id", K(ret));
    // request_column_id: stores request payload
    } else if (OB_FAIL(schema_builder.add_int_column("request_column_id", NOT_NULL))) {
      LOG_WARN("Failed to add request_column_id", K(ret));
    // result_spec_column_id: stores result specification
    } else if (OB_FAIL(schema_builder.add_int_column("result_spec_column_id", NOT_NULL))) {
      LOG_WARN("Failed to add result_spec_column_id", K(ret));
    }
  }

  // Execution configuration
  if (OB_SUCC(ret)) {
    // sync_mode: 'sync' or 'async'
    if (OB_FAIL(schema_builder.add_varchar_column("sync_mode",
                                                  16,
                                                  NOT_NULL))) {
      LOG_WARN("Failed to add sync_mode", K(ret));
    // max_retries: maximum retry attempts
    } else if (OB_FAIL(schema_builder.add_int_column("max_retries", NOT_NULL))) {
      LOG_WARN("Failed to add max_retries", K(ret));
    // retry_delay: delay between retries (seconds)
    } else if (OB_FAIL(schema_builder.add_int_column("retry_delay", NOT_NULL))) {
      LOG_WARN("Failed to add retry_delay", K(ret));
    }
  }

  // Status tracking
  if (OB_SUCC(ret)) {
    // status: PENDING, PROCESSING, COMPLETED, FAILED
    if (OB_FAIL(schema_builder.add_varchar_column("status",
                                                  32,
                                                  NOT_NULL))) {
      LOG_WARN("Failed to add status", K(ret));
    // last_error: error message if failed
    } else if (OB_FAIL(schema_builder.add_text_column("last_error", CAN_NULL))) {
      LOG_WARN("Failed to add last_error", K(ret));
    // retry_count: current retry count
    } else if (OB_FAIL(schema_builder.add_int_column("retry_count", NOT_NULL))) {
      LOG_WARN("Failed to add retry_count", K(ret));
    }
  }

  // Timestamps
  if (OB_SUCC(ret)) {
    // gmt_create: creation time
    if (OB_FAIL(schema_builder.add_timestamp_column("gmt_create", NOT_NULL))) {
      LOG_WARN("Failed to add gmt_create", K(ret));
    // gmt_modified: last modification time
    } else if (OB_FAIL(schema_builder.add_timestamp_column("gmt_modified", NOT_NULL))) {
      LOG_WARN("Failed to add gmt_modified", K(ret));
    }
  }

  // Set rowkey
  if (OB_SUCC(ret)) {
    ObRowkeyInfo rowkey_info;
    if (OB_FAIL(rowkey_info.add_column("column_id")) ||
        OB_FAIL(rowkey_info.add_column("table_id")) ||
        OB_FAIL(rowkey_info.add_column("database_id"))) {
      LOG_WARN("Failed to build rowkey", K(ret));
    } else if (OB_FAIL(schema_builder.set_rowkey_info(rowkey_info))) {
      LOG_WARN("Failed to set rowkey info", K(ret));
    }
  }

  // Build final schema
  if (OB_SUCC(ret)) {
    if (OB_FAIL(schema_builder.generate(table_schema))) {
      LOG_WARN("Failed to generate table schema", K(ret));
    }
  }

  return ret;
}

} // namespace share
} // namespace oceanbase
