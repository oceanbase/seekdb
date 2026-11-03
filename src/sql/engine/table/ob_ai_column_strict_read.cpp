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

// OB-P:a4L9E_IDIyEM2Ol_ZvDXqw

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/table/ob_ai_column_strict_read.h"

#include "lib/string/ob_sql_string.h"
#include "observer/ob_inner_sql_connection.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "share/ob_errno.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_table_schema.h"
#include "share/system_variable/ob_sys_var_class_type.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"

using namespace oceanbase::common;
using namespace oceanbase::share;

namespace oceanbase
{
namespace sql
{

namespace
{

// Runs one read statement in a nested session, so that it sees the rows of
// the current transaction.
int execute_nested_read(ObSQLSessionInfo &session,
                        const char *sql,
                        ObMySQLProxy::MySQLResult &res)
{
  int ret = OB_SUCCESS;
  ObIInnerSQLConnection *inner_conn = NULL;
  ObSQLSessionInfo::StmtSavedValue saved_session;
  ObIInnerSQLConnection::SavedValue saved_conn;
  common::sqlclient::ObISQLConnectionGuard conn_guard;
  if (OB_FAIL(query::ObInnerSQLConnectionAccess::create_connection_with_external_session(
          &session, conn_guard))) {
    LOG_WARN("fail to create the inner connection", K(ret));
  } else if (OB_ISNULL(inner_conn = as_inner_sql_connection(conn_guard.get_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("the inner connection is invalid", K(ret));
  } else {
    session.set_inner_conn(conn_guard.get_ptr());
    if (OB_FAIL(inner_conn->begin_nested_session(saved_session, saved_conn, false))) {
      LOG_WARN("fail to begin the nested session", K(ret));
    } else if (OB_FAIL(inner_conn->execute_read(sql, res))) {
      LOG_WARN("fail to execute the strict read query", K(ret), K(sql));
    }
    const int end_ret = inner_conn->end_nested_session(saved_session, saved_conn);
    if (OB_SUCCESS != end_ret) {
      LOG_WARN("fail to end the nested session", K(end_ret));
      if (OB_SUCCESS == ret) {
        ret = end_ret;
      }
    }
  }
  return ret;
}

// True when any row of the table carries a state of the hidden column that is
// not READY.
int check_ai_column_state(ObSQLSessionInfo &session,
                          const ObTableSchema &table_schema,
                          const ObString &db_name,
                          const ObString &table_name,
                          const ObColumnSchemaV2 &column,
                          const ObString &state_column_name)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    if (OB_FAIL(sql.append_fmt("SELECT 1 FROM `%.*s`.`%.*s` WHERE `%.*s` IS NULL OR `%.*s` <> 'READY' LIMIT 1",
                               static_cast<int32_t>(db_name.length()), db_name.ptr(),
                               static_cast<int32_t>(table_name.length()), table_name.ptr(),
                               static_cast<int32_t>(state_column_name.length()), state_column_name.ptr(),
                               static_cast<int32_t>(state_column_name.length()), state_column_name.ptr()))) {
      LOG_WARN("fail to build the strict read query", K(ret));
    } else if (OB_FAIL(execute_nested_read(session, sql.ptr(), res))) {
      LOG_WARN("fail to run the strict read query", K(ret), K(sql));
    } else if (OB_ISNULL(res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the strict read query has no result", K(ret));
    } else if (OB_ITER_END != res.get_result()->next()) {
      // the column has rows that are not ready, the read of the column is
      // rejected with the schema ids of the ai column
      LOG_USER_ERROR(OB_ERR_AI_COLUMN_NOT_READY,
                     static_cast<uint64_t>(table_schema.get_table_id()),
                     static_cast<uint64_t>(column.get_column_id()));
      ret = OB_ERR_AI_COLUMN_NOT_READY;
      LOG_WARN("the ai column has rows that are not ready", K(ret),
               K(table_schema.get_table_id()), K(column.get_column_id()));
    }
    if (OB_NOT_NULL(res.get_result())) {
      const int close_ret = res.get_result()->close();
      if (OB_SUCCESS != close_ret) {
        LOG_WARN("fail to close the strict read result", K(close_ret));
      }
    }
  }
  return ret;
}

} // anonymous namespace

int ObAiColumnStrictRead::check_table_scan(ObExecContext &ctx,
                                           uint64_t table_id,
                                           const ObIArray<ObColDesc> &columns)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  bool strict_read = false;
  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(session->get_sys_variable(SYS_VAR_AI_COLUMN_STRICT_READ, strict_read))) {
    LOG_WARN("fail to read the ai column strict read variable", K(ret));
  } else if (!strict_read) {
    // the strict read is disabled
  } else if (AI_COL_MODE_NONE != session->get_ai_column_internal_mode()) {
    // the internal modes manage the ai columns themselves
  } else if (session->is_inner()) {
    // the sql of internal components is not a business read
  } else {
    ObSchemaGetterGuard *schema_guard = ctx.get_sql_ctx()->schema_guard_;
    const ObTableSchema *table_schema = NULL;
    if (OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema guard is null", K(ret));
    } else if (OB_FAIL(schema_guard->get_table_schema(table_id, table_schema))) {
      LOG_WARN("fail to get the table schema", K(ret), K(table_id));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("table schema is null", K(ret), K(table_id));
    } else {
      // collect the ai result columns that the scan accesses, every ai
      // column owns a hidden companion column that carries the state
      ObSEArray<const ObColumnSchemaV2 *, 4> ai_columns;
      ObSEArray<ObString, 4> state_names;
      ObArenaAllocator alloc("AiColStrict");
      for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
        const ObColDesc &col_desc = columns.at(i);
        const ObColumnSchemaV2 *column = table_schema->get_column_schema(col_desc.col_id_);
        if (OB_ISNULL(column) || !column->is_ai_result_column()) {
          continue;
        } else {
          char buf[OB_MAX_COLUMN_NAME_LENGTH + 32];
          const ObString &col_name = column->get_column_name_str();
          const int64_t len = snprintf(buf, sizeof(buf), "__ai_state_%.*s",
                                       static_cast<int32_t>(col_name.length()), col_name.ptr());
          if (OB_UNLIKELY(len <= 0 || len >= static_cast<int64_t>(sizeof(buf)))) {
            ret = OB_ERR_UNEXPECTED;
          } else {
            char *copy = static_cast<char *>(alloc.alloc(len + 1));
            if (OB_ISNULL(copy)) {
              ret = OB_ALLOCATE_MEMORY_FAILED;
            } else {
              MEMCPY(copy, buf, len);
              copy[len] = '\0';
              ObString state_name(static_cast<int32_t>(len), copy);
              if (OB_FAIL(ai_columns.push_back(column))
                  || OB_FAIL(state_names.push_back(state_name))) {
                LOG_WARN("fail to record the ai column", K(ret));
              }
            }
          }
        }
      }
      if (OB_SUCC(ret) && !ai_columns.empty()) {
        const ObDatabaseSchema *db_schema = NULL;
        if (OB_FAIL(schema_guard->get_database_schema(table_schema->get_database_id(), db_schema))) {
          LOG_WARN("fail to get the database schema", K(ret));
        } else if (OB_ISNULL(db_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("database schema is null", K(ret));
        }
        for (int64_t i = 0; OB_SUCC(ret) && i < ai_columns.count(); ++i) {
          if (OB_FAIL(check_ai_column_state(*session, *table_schema,
                                            db_schema->get_database_name_str(),
                                            table_schema->get_table_name_str(),
                                            *ai_columns.at(i), state_names.at(i)))) {
            LOG_WARN("the strict read of the ai column failed", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

} // namespace sql
} // namespace oceanbase
