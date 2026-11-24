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

#define USING_LOG_PREFIX SHARE_SCHEMA

#include "share/schema/ob_ai_column_sql_service.h"
#include "common/mysqlclient/ob_isql_client.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/schema/ob_ai_column_schema.h"
#include "share/schema/ob_schema_utils.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace share
{
namespace schema
{

int ObAIColumnSqlService::add_ai_column_fields(
    const ObAIColumnSchema &schema,
    ObDMLSqlSplicer &dml)
{
  int ret = OB_SUCCESS;
  ObSqlString dependency_ids;
  for (int64_t i = 0; OB_SUCC(ret)
          && i < schema.get_dependency_column_ids().count(); ++i) {
    if (OB_FAIL(dependency_ids.append_fmt("%s%lu", 0 == i ? "" : ",",
                                         schema.get_dependency_column_ids().at(i)))) {
      LOG_WARN("failed to serialize AI column dependencies", K(ret), K(i));
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(dml.add_column("column_name", ObHexEscapeSqlStr(schema.get_column_name())))) {
    LOG_WARN("failed to add AI column name", K(ret));
  } else if (OB_FAIL(dml.add_column("expr_text", ObHexEscapeSqlStr(schema.get_expr_text())))) {
    LOG_WARN("failed to add AI expression", K(ret));
  } else if (OB_FAIL(dml.add_column("normalized_expr", ObHexEscapeSqlStr(schema.get_normalized_expr())))) {
    LOG_WARN("failed to add normalized AI expression", K(ret));
  } else if (OB_FAIL(dml.add_column("dependency_column_ids",
                                    ObHexEscapeSqlStr(dependency_ids.string())))) {
    LOG_WARN("failed to add AI column dependencies", K(ret));
  } else if (OB_FAIL(dml.add_column("sql_mode", schema.get_sql_mode()))) {
    LOG_WARN("failed to add AI column sql mode", K(ret));
  } else if (OB_FAIL(dml.add_column("time_zone", ObHexEscapeSqlStr(schema.get_time_zone())))) {
    LOG_WARN("failed to add AI column time zone", K(ret));
  } else if (OB_FAIL(dml.add_column("spec_version", schema.get_spec_version()))) {
    LOG_WARN("failed to add AI column spec version", K(ret));
  } else if (OB_FAIL(dml.add_column("max_retries", schema.get_max_retries()))) {
    LOG_WARN("failed to add AI column retries", K(ret));
  } else if (OB_FAIL(dml.add_column("state_column_id",
                                    static_cast<int64_t>(schema.get_state_column_id())))) {
    LOG_WARN("failed to add AI state column id", K(ret));
  } else if (OB_FAIL(dml.add_column("request_column_id",
                                    static_cast<int64_t>(schema.get_request_column_id())))) {
    LOG_WARN("failed to add AI request column id", K(ret));
  } else if (OB_FAIL(dml.add_column("result_spec_column_id",
                                    static_cast<int64_t>(schema.get_result_spec_column_id())))) {
    LOG_WARN("failed to add AI result spec column id", K(ret));
  }
  return ret;
}

int ObAIColumnSqlService::upsert_ai_column(
    const ObAIColumnSchema &schema,
    common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObDMLSqlSplicer dml;
  ObSqlString sql;
  const uint64_t table_id = ObSchemaUtils::get_extract_schema_id(schema.get_table_id());
  if (!schema.is_valid_for_persistence()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid AI column schema for persistence", K(ret), K(schema));
  } else if (OB_FAIL(dml.add_pk_column("table_id", table_id))) {
    LOG_WARN("failed to add AI table id", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("column_id", schema.get_column_id()))) {
    LOG_WARN("failed to add AI column id", K(ret));
  } else if (OB_FAIL(add_ai_column_fields(schema, dml))) {
    LOG_WARN("failed to add AI column fields", K(ret));
  } else if (OB_FAIL(dml.add_column("schema_version", schema.get_schema_version()))) {
    LOG_WARN("failed to add AI column schema version", K(ret));
  } else if (OB_FAIL(dml.splice_insert_update_sql(OB_ALL_AI_COLUMN_TNAME, sql))) {
    LOG_WARN("failed to build AI column upsert", K(ret));
  } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
    LOG_WARN("failed to upsert AI column", K(ret), K(sql));
  } else if (affected_rows < 1 || affected_rows > 2) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected AI column upsert row count", K(ret), K(affected_rows));
  }

  if (OB_SUCC(ret)) {
    dml.reset();
    sql.reuse();
    if (OB_FAIL(dml.add_pk_column("table_id", table_id))) {
      LOG_WARN("failed to add AI history table id", K(ret));
    } else if (OB_FAIL(dml.add_pk_column("column_id", schema.get_column_id()))) {
      LOG_WARN("failed to add AI history column id", K(ret));
    } else if (OB_FAIL(dml.add_pk_column("schema_version", schema.get_schema_version()))) {
      LOG_WARN("failed to add AI history schema version", K(ret));
    } else if (OB_FAIL(dml.add_column("is_deleted", 0))) {
      LOG_WARN("failed to add AI history delete flag", K(ret));
    } else if (OB_FAIL(add_ai_column_fields(schema, dml))) {
      LOG_WARN("failed to add AI history fields", K(ret));
    } else if (OB_FAIL(dml.splice_insert_sql(OB_ALL_AI_COLUMN_HISTORY_TNAME, sql))) {
      LOG_WARN("failed to build AI column history insert", K(ret));
    } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
      LOG_WARN("failed to insert AI column history", K(ret), K(sql));
    } else if (!is_single_row(affected_rows)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected AI column history row count", K(ret), K(affected_rows));
    }
  }
  return ret;
}

int ObAIColumnSqlService::create_ai_columns(
    const ObTableSchema &table_schema,
    common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret)
          && i < table_schema.get_ai_column_schema_count(); ++i) {
    const ObAIColumnSchema *schema = table_schema.get_ai_column_schema_by_idx(i);
    if (OB_ISNULL(schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI column schema is null", K(ret), K(i));
    } else if (OB_FAIL(upsert_ai_column(*schema, sql_client))) {
      LOG_WARN("failed to create AI column schema", K(ret), K(i));
    }
  }
  return ret;
}

int ObAIColumnSqlService::delete_ai_column(
    const uint64_t table_id,
    const uint64_t column_id,
    const int64_t schema_version,
    common::ObISQLClient &sql_client)
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  const uint64_t extract_table_id = ObSchemaUtils::get_extract_schema_id(table_id);
  ObDMLSqlSplicer dml;
  ObSqlString sql;
  if (OB_INVALID_ID == table_id || OB_INVALID_ID == column_id
      || schema_version <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid AI column delete argument", K(ret), K(table_id),
             K(column_id), K(schema_version));
  } else if (OB_FAIL(dml.add_pk_column("table_id", extract_table_id))) {
    LOG_WARN("failed to add AI table id", K(ret));
  } else if (OB_FAIL(dml.add_pk_column("column_id", column_id))) {
    LOG_WARN("failed to add AI column id", K(ret));
  } else if (OB_FAIL(dml.splice_delete_sql(OB_ALL_AI_COLUMN_TNAME, sql))) {
    LOG_WARN("failed to build AI column delete", K(ret));
  } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
    LOG_WARN("failed to delete AI column", K(ret), K(sql));
  } else if (affected_rows > 1) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected AI column delete row count", K(ret), K(affected_rows));
  }

  if (OB_SUCC(ret)) {
    dml.reset();
    sql.reuse();
    if (OB_FAIL(dml.add_pk_column("table_id", extract_table_id))) {
      LOG_WARN("failed to add AI history table id", K(ret));
    } else if (OB_FAIL(dml.add_pk_column("column_id", column_id))) {
      LOG_WARN("failed to add AI history column id", K(ret));
    } else if (OB_FAIL(dml.add_pk_column("schema_version", schema_version))) {
      LOG_WARN("failed to add AI history schema version", K(ret));
    } else if (OB_FAIL(dml.add_column("is_deleted", 1))) {
      LOG_WARN("failed to add AI history delete flag", K(ret));
    } else if (OB_FAIL(dml.splice_insert_sql(OB_ALL_AI_COLUMN_HISTORY_TNAME, sql))) {
      LOG_WARN("failed to build AI column history tombstone", K(ret));
    } else if (OB_FAIL(sql_client.write(sql.ptr(), affected_rows))) {
      LOG_WARN("failed to insert AI column history tombstone", K(ret), K(sql));
    } else if (!is_single_row(affected_rows)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected AI history tombstone row count", K(ret), K(affected_rows));
    }
  }
  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
