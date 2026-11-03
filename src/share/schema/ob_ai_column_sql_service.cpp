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
#include "share/schema/ob_ai_column_spec.h"
#include "share/ob_dml_sql_splicer.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "lib/allocator/page_arena.h"

namespace oceanbase
{
using namespace common;
namespace share
{
namespace schema
{

namespace
{
// The metadata table is always written through the sys tenant inner sql,
// the plain sql proxy has no default database, so the name is fully
// qualified.
const char *const AI_COLUMN_TABLE = "oceanbase.__all_ai_column";
// Build the name of a hidden companion column of an AI column.  The name is
// written into the caller owned buffer `buf`, so the returned ObString stays
// valid after the call.
int build_hidden_col_name(const ObString &col_name, const char *prefix,
                          char *buf, const int64_t buf_len, ObString &name)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(databuff_printf(buf, buf_len, "%s%.*s", prefix,
                              static_cast<int32_t>(col_name.length()), col_name.ptr()))) {
  } else {
    name.assign_ptr(buf, static_cast<int32_t>(STRLEN(buf)));
  }
  return ret;
}
} // namespace

int ObAiColumnSqlService::delete_ai_column_meta(ObISQLClient &sql_client,
                                                 const uint64_t table_id)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer sql;
  ObSqlString buffer;
  int64_t affected_rows = 0;
  if (OB_FAIL(sql.add_pk_column("table_id", static_cast<int64_t>(table_id)))) {
  } else if (OB_FAIL(sql.splice_delete_sql(AI_COLUMN_TABLE, buffer))) {
  } else if (OB_FAIL(sql_client.write(buffer.ptr(), affected_rows))) {
    LOG_WARN("fail to delete ai column meta", K(ret));
  }
  return ret;
}

int ObAiColumnSqlService::sync_ai_column_meta(ObISQLClient &sql_client,
                                               const ObTableSchema &table_schema,
                                               const ObTableSchema *origin_table_schema)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::OB_MODULE_PAGE_ALLOCATOR, OB_MALLOC_NORMAL_BLOCK_SIZE);
  // drop the rows of the columns that are not AI columns anymore.  The AI
  // columns are identified by their spec attribute: the ai result column
  // flag is not reliable on a retrieved schema.
  if (NULL != origin_table_schema) {
    for (ObTableSchema::const_column_iterator it = origin_table_schema->column_begin();
         OB_SUCC(ret) && it != origin_table_schema->column_end(); ++it) {
      const ObColumnSchemaV2 *col = *it;
      if (OB_ISNULL(col) || col->get_ai_spec_text().empty()) {
        continue;
      }
      ObDMLSqlSplicer sql;
      ObSqlString buffer;
      int64_t affected_rows = 0;
      if (OB_FAIL(sql.add_pk_column("table_id", static_cast<int64_t>(table_schema.get_table_id())))
          || OB_FAIL(sql.add_pk_column("column_id", static_cast<int64_t>(col->get_column_id())))
          || OB_FAIL(sql.splice_delete_sql(AI_COLUMN_TABLE, buffer))) {
      } else if (OB_FAIL(sql_client.write(buffer.ptr(), affected_rows))) {
        LOG_WARN("fail to delete ai column meta", K(ret));
      }
    }
  }
  // insert the rows of the current AI columns, identified by their spec
  // attribute (see the comment above)
  for (ObTableSchema::const_column_iterator it = table_schema.column_begin();
       OB_SUCC(ret) && it != table_schema.column_end(); ++it) {
    const ObColumnSchemaV2 *col = *it;
    if (OB_ISNULL(col)) {
      continue;
    }
    LOG_INFO("ai meta sync col", "name", col->get_column_name_str(),
             "spec_len", col->get_ai_spec_text().length());
    if (col->get_ai_spec_text().empty()) {
      continue;
    }
    if (OB_FAIL(write_ai_column_meta(sql_client, table_schema, *col))) {
      LOG_WARN("fail to write ai column meta", K(ret), K(col->get_column_name_str()));
    }
  }
  return ret;
}

int ObAiColumnSqlService::write_ai_column_meta(ObISQLClient &sql_client,
                                               const ObTableSchema &table_schema,
                                               const ObColumnSchemaV2 &col)
{
  int ret = OB_SUCCESS;
  ObArenaAllocator allocator(ObModIds::OB_MODULE_PAGE_ALLOCATOR, OB_MALLOC_NORMAL_BLOCK_SIZE);
  const ObString &spec_text = col.get_ai_spec_text();
  LOG_INFO("write ai col enter", "name", col.get_column_name_str(),
           "spec_len", spec_text.length());
  // the spec of an AI column is kept in the ai_spec attribute of the column
  // schema, the result column keeps an empty default value
  if (spec_text.empty()) {
    LOG_WARN("ai column has no spec", K(ret), K(col.get_column_name_str()));
    ret = OB_SUCCESS;
  } else {
    ObAiColumnSpec spec;
    if (OB_FAIL(spec.parse(spec_text, allocator))) {
      LOG_WARN("fail to parse ai column spec", K(ret), K(col.get_column_name_str()));
      ret = OB_SUCCESS;
    } else {
      LOG_INFO("ai column spec parsed", "dep_cnt", spec.dep_column_cnt_);
      // the hidden column names are built into caller owned buffers: a stack
      // buffer inside build_hidden_col_name would dangle after the call
      char state_buf[OB_MAX_COLUMN_NAME_LENGTH + 32];
      char request_buf[OB_MAX_COLUMN_NAME_LENGTH + 32];
      char spec_buf[OB_MAX_COLUMN_NAME_LENGTH + 32];
      ObString state_name;
      ObString request_name;
      ObString spec_name;
      if (OB_FAIL(build_hidden_col_name(col.get_column_name_str(), "__ai_state_",
                                        state_buf, sizeof(state_buf), state_name))) {
        LOG_WARN("fail to build state column name", K(ret));
      } else if (OB_FAIL(build_hidden_col_name(col.get_column_name_str(), "__ai_request_",
                                               request_buf, sizeof(request_buf), request_name))) {
        LOG_WARN("fail to build request column name", K(ret));
      } else if (OB_FAIL(build_hidden_col_name(col.get_column_name_str(), "__ai_spec_",
                                               spec_buf, sizeof(spec_buf), spec_name))) {
        LOG_WARN("fail to build spec column name", K(ret));
      } else if (OB_ISNULL(table_schema.get_column_schema(state_name))
                 || OB_ISNULL(table_schema.get_column_schema(request_name))
                 || OB_ISNULL(table_schema.get_column_schema(spec_name))) {
        LOG_WARN("ai column hidden columns are missing", K(ret), K(col.get_column_name_str()),
                 K(state_name), K(request_name), K(spec_name));
        ret = OB_SUCCESS;
      } else {
        ObDMLSqlSplicer sql;
        ObSqlString buffer;
        int64_t affected_rows = 0;
        ObSqlString dep_ids;
        for (int64_t i = 0; OB_SUCC(ret) && i < spec.dep_column_cnt_; ++i) {
          if (i > 0) {
            ret = dep_ids.append(",");
          }
          if (OB_SUCC(ret)) {
            ret = dep_ids.append_fmt("%lu", spec.dep_column_ids_[i]);
          }
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_pk_column("table_id", static_cast<int64_t>(table_schema.get_table_id()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_pk_column("column_id", static_cast<int64_t>(col.get_column_id()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("column_name", ObHexEscapeSqlStr(col.get_column_name_str()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("sync_mode", ObHexEscapeSqlStr(ObString::make_string(ObAiColumnSpec::SYNC_ASYNC)));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("max_retries", spec.max_retries_);
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("func_type", ObHexEscapeSqlStr(spec.func_type_));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("model_name", ObHexEscapeSqlStr(spec.model_name_));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("normalized_expr", ObHexEscapeSqlStr(spec.normalized_));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("prompt_expr", ObHexEscapeSqlStr(spec.prompt_expr_));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("config", ObHexEscapeSqlStr(spec.config_));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("dependency_column_ids", ObHexEscapeSqlStr(dep_ids.string()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("state_column_id",
                               static_cast<int64_t>(table_schema.get_column_schema(state_name)->get_column_id()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("request_column_id",
                               static_cast<int64_t>(table_schema.get_column_schema(request_name)->get_column_id()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("result_spec_column_id",
                               static_cast<int64_t>(table_schema.get_column_schema(spec_name)->get_column_id()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.add_column("def_version", static_cast<int64_t>(table_schema.get_schema_version()));
        }
        if (OB_SUCC(ret)) {
          ret = sql.splice_insert_sql(AI_COLUMN_TABLE, buffer);
        }
        if (OB_SUCC(ret)) {
          LOG_INFO("ai column meta insert", K(buffer));
          ret = sql_client.write(buffer.ptr(), affected_rows);
          LOG_INFO("ai column meta insert done", K(ret), K(affected_rows));
        }
        if (OB_SUCC(ret) && affected_rows != 1) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected affected rows", K(ret), K(affected_rows));
        }
      }
    }
  }
  return ret;
}

int ObAiColumnSqlService::remove_ai_column_meta(ObISQLClient &sql_client,
                                                 const uint64_t table_id,
                                                 const uint64_t column_id)
{
  int ret = OB_SUCCESS;
  ObDMLSqlSplicer sql;
  ObSqlString buffer;
  int64_t affected_rows = 0;
  if (OB_FAIL(sql.add_pk_column("table_id", static_cast<int64_t>(table_id)))
      || OB_FAIL(sql.add_pk_column("column_id", static_cast<int64_t>(column_id)))
      || OB_FAIL(sql.splice_delete_sql(AI_COLUMN_TABLE, buffer))) {
  } else if (OB_FAIL(sql_client.write(buffer.ptr(), affected_rows))) {
    LOG_WARN("fail to delete ai column meta", K(ret));
  }
  return ret;
}

} // namespace schema
} // namespace share
} // namespace oceanbase
