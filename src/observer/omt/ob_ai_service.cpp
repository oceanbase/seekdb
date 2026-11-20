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

#define USING_LOG_PREFIX SERVER_OMT
#include "ob_ai_service.h"
#include "observer/ai_service/ob_ai_service_executor.h"
#include "sql/privilege_check/ob_ai_model_priv_util.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/expr/ob_expr_uuid.h"
#include "observer/ob_inner_sql_connection.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_latest_schema_guard.h"
#include "share/schema/ob_table_schema.h"
#include "share/schema/ob_schema_utils.h"
#include "share/schema/ob_ai_column_schema.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "rust/config.h"
#include "lib/string/ob_sql_string.h"
#include <algorithm>

using namespace oceanbase::share;
using namespace oceanbase::common;
namespace oceanbase
{
namespace omt
{

namespace
{

static const int64_t AI_WORKER_RECOVERY_SCAN_US = 60 * 1000 * 1000;
static const int64_t AI_WORKER_BUSY_US = 10 * 1000;
static const int64_t AI_WORKER_LEASE_SECONDS = 300;
static const int64_t AI_WORKER_MIN_THREAD_COUNT = 1;
static const int64_t AI_WORKER_MAX_THREAD_COUNT = 64;
static const int64_t AI_WORKER_MAX_RESULT_HEX_BYTES = 32 * 1024 * 1024;
static const int64_t AI_WORKER_METADATA_QUERY_TIMEOUT_US = 1000 * 1000;

static const char *AI_TASK_TABLE_ID = "table_id";
static const char *AI_TASK_COLUMN_ID = "column_id";
static const char *AI_TASK_REQUEST_ID = "request_id";
static const char *AI_TASK_ROWKEY_PREDICATE = "rowkey_predicate";
static const char *AI_TASK_SPEC_VERSION = "spec_version";
static const char *AI_TASK_STATE = "state";
static const char *AI_TASK_RETRY_COUNT = "retry_count";
static const char *AI_TASK_ELIGIBLE_TS = "eligible_ts";
static const char *AI_TASK_LEASE_TOKEN = "lease_token";
static const char *AI_TASK_LAST_ERROR_CODE = "last_error_code";
static const char *AI_TASK_LAST_ERROR_MESSAGE = "last_error_message";
static const char *AI_TASK_GMT_MODIFIED = "gmt_modified";

struct ObAIWorkerTaskResult
{
  ObAIWorkerTaskResult()
      : column_id_(OB_INVALID_ID), stale_(false), schema_(NULL), retry_count_(0), eval_ret_(OB_SUCCESS), result_hex_()
  {}
  uint64_t column_id_;
  bool stale_;
  const schema::ObAIColumnSchema *schema_;
  int64_t retry_count_;
  int eval_ret_;
  ObString result_hex_;
  TO_STRING_KV(KP_(schema), K_(retry_count), K_(eval_ret),
               "result_hex_length", result_hex_.length());
};

int append_ai_identifier(ObSqlString &sql, const ObString &identifier)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append("`"))) {
    LOG_WARN("failed to append identifier prefix", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < identifier.length(); ++i) {
    if ('`' == identifier.ptr()[i]) {
      if (OB_FAIL(sql.append("``"))) {
        LOG_WARN("failed to escape identifier", K(ret), K(identifier));
      }
    } else if (OB_FAIL(sql.append(identifier.ptr() + i, 1))) {
      LOG_WARN("failed to append identifier", K(ret), K(identifier));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(sql.append("`"))) {
    LOG_WARN("failed to append identifier suffix", K(ret));
  }
  return ret;
}

int append_ai_qualified_table(ObSqlString &sql,
                              const ObString &database_name,
                              const ObString &table_name)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(append_ai_identifier(sql, database_name))) {
    LOG_WARN("failed to append database identifier", K(ret), K(database_name));
  } else if (OB_FAIL(sql.append("."))) {
    LOG_WARN("failed to append qualified table delimiter", K(ret));
  } else if (OB_FAIL(append_ai_identifier(sql, table_name))) {
    LOG_WARN("failed to append table identifier", K(ret), K(table_name));
  }
  return ret;
}

int append_ai_task_table(ObSqlString &sql)
{
  return append_ai_qualified_table(
      sql, ObString::make_string(OB_SYS_DATABASE_NAME),
      ObString::make_string(OB_ALL_AI_COLUMN_TASK_TNAME));
}

bool is_ai_hex_string(const ObString &value, const int64_t expected_len)
{
  bool valid = value.length() == expected_len;
  for (int64_t i = 0; valid && i < value.length(); ++i) {
    const char c = value.ptr()[i];
    valid = ('0' <= c && c <= '9')
        || ('a' <= c && c <= 'f')
        || ('A' <= c && c <= 'F');
  }
  return valid;
}

bool is_ai_hex_value(const ObString &value)
{
  bool valid = 0 == value.length() % 2;
  for (int64_t i = 0; valid && i < value.length(); ++i) {
    const char c = value.ptr()[i];
    valid = ('0' <= c && c <= '9')
        || ('a' <= c && c <= 'f')
        || ('A' <= c && c <= 'F');
  }
  return valid;
}

int generate_ai_lease_id(char (&lease_hex)[33])
{
  int ret = OB_SUCCESS;
  char uuid[sql::UuidCommon::LENGTH_UUID + 1] = {'\0'};
  int64_t pos = 0;
  if (OB_FAIL(sql::ObExprUuid::gen_server_uuid(
          uuid, sql::UuidCommon::LENGTH_UUID))) {
    LOG_WARN("failed to generate AI worker lease UUID", K(ret));
  } else {
    for (int64_t i = 0; i < sql::UuidCommon::LENGTH_UUID; ++i) {
      if ('-' != uuid[i]) {
        if (pos >= 32) {
          ret = OB_ERR_UNEXPECTED;
          break;
        }
        lease_hex[pos++] = uuid[i];
      }
    }
    if (OB_SUCC(ret) && 32 != pos) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI worker lease UUID has unexpected length", K(ret), K(pos));
    } else if (OB_SUCC(ret)) {
      lease_hex[pos] = '\0';
    }
  }
  return ret;
}

int append_ai_task_group_predicate(ObSqlString &sql,
                                   const uint64_t table_id,
                                   const ObString &request_hex)
{
  int ret = OB_SUCCESS;
  if (!is_ai_hex_string(request_hex, 32)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid AI task request id", K(ret), K(request_hex));
  } else if (OB_FAIL(sql.append_fmt("`%s` = %lu AND `%s` = X'%.*s'",
                                    AI_TASK_TABLE_ID, table_id,
                                    AI_TASK_REQUEST_ID,
                                    request_hex.length(),
                                    request_hex.ptr()))) {
    LOG_WARN("failed to append AI task group predicate", K(ret));
  }
  return ret;
}

int finish_ai_worker_transaction(observer::ObInnerSQLConnection &conn,
                                 const int work_ret)
{
  int ret = work_ret;
  if (OB_SUCCESS == work_ret) {
    if (OB_FAIL(conn.commit())) {
      LOG_WARN("failed to commit AI worker transaction", K(ret));
    }
  } else {
    const int rollback_ret = conn.rollback();
    if (OB_SUCCESS != rollback_ret) {
      LOG_WARN("failed to rollback AI worker transaction", K(rollback_ret),
               K(work_ret));
    }
  }
  return ret;
}

int find_ai_task_candidate(observer::ObInnerSQLConnection &conn,
                           ObIAllocator &allocator,
                           bool &found,
                           uint64_t &table_id,
                           ObSqlString &rowkey_predicate,
                           ObString &request_hex,
                           int64_t &next_wait_us)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  found = false;
  table_id = OB_INVALID_ID;
  rowkey_predicate.reset();
  request_hex.reset();
  next_wait_us = AI_WORKER_RECOVERY_SCAN_US;
  if (OB_FAIL(sql.append_fmt(
          "SELECT `%s` AS __ai_table_id, HEX(`%s`) AS __ai_request, "
          "`%s` AS __ai_rowkey, "
          "TIMESTAMPDIFF(MICROSECOND, NOW(6), `%s`) AS __ai_wait_us FROM ",
          AI_TASK_TABLE_ID, AI_TASK_REQUEST_ID, AI_TASK_ROWKEY_PREDICATE,
          AI_TASK_ELIGIBLE_TS))) {
    LOG_WARN("failed to start AI task candidate SQL", K(ret));
  } else if (OB_FAIL(append_ai_task_table(sql))) {
    LOG_WARN("failed to append centralized AI task table", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(sql.append_fmt(
                    " WHERE `%s` IN (%d, %d, %d)"
                    " ORDER BY `%s` LIMIT 1",
                    AI_TASK_STATE,
                    static_cast<int>(schema::AI_COLUMN_TASK_STATE_PENDING),
                    static_cast<int>(schema::AI_COLUMN_TASK_STATE_RETRY),
                    static_cast<int>(schema::AI_COLUMN_TASK_STATE_RUNNING),
                    AI_TASK_ELIGIBLE_TS))) {
    LOG_WARN("failed to finish AI task candidate SQL", K(ret));
  }

  if (OB_SUCC(ret)) {
    ObISQLClient::ReadResult read_result;
    sqlclient::ObMySQLResult *result = NULL;
    conn.get_session().set_table_name_hidden(false);
    if (OB_FAIL(conn.execute_read(sql.string(), read_result))) {
      LOG_WARN("failed to query AI task candidate", K(ret), K(sql));
    } else if (OB_ISNULL(result = read_result.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI task candidate result is null", K(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        LOG_DEBUG("AI worker scan found no centralized task", K(sql));
      } else {
        LOG_WARN("failed to read AI task candidate", K(ret));
      }
    } else {
      ObString request_value;
      ObString rowkey_value;
      int64_t table_id_value = OB_INVALID_ID;
      int64_t wait_us = 0;
      if (OB_FAIL(result->get_int("__ai_table_id", table_id_value))) {
        LOG_WARN("failed to read AI task table id", K(ret));
      } else if (table_id_value <= 0) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("AI task table id is invalid", K(ret), K(table_id_value));
      } else if (FALSE_IT(table_id = static_cast<uint64_t>(table_id_value))) {
      } else if (OB_FAIL(result->get_varchar("__ai_request", request_value))) {
        LOG_WARN("failed to read AI task request id", K(ret));
      } else if (!is_ai_hex_string(request_value, 32)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("AI task request id is not valid hex", K(ret),
                 K(request_value));
      } else if (OB_FAIL(result->get_varchar("__ai_rowkey", rowkey_value))) {
        LOG_WARN("failed to read AI task rowkey predicate", K(ret));
      } else if (OB_FAIL(result->get_int("__ai_wait_us", wait_us))) {
        LOG_WARN("failed to read AI task eligible delay", K(ret));
      } else if (OB_FAIL(ob_write_string(allocator, request_value,
                                         request_hex))) {
        LOG_WARN("failed to copy AI request id", K(ret));
      } else if (OB_FAIL(rowkey_predicate.append(rowkey_value))) {
        LOG_WARN("failed to copy AI rowkey predicate", K(ret));
      } else {
        next_wait_us = std::min<int64_t>(
            AI_WORKER_RECOVERY_SCAN_US, std::max<int64_t>(0, wait_us));
        found = wait_us <= 0;
        LOG_DEBUG("AI worker inspected task candidate", K(table_id),
                  K(request_hex), K(wait_us), K(found), K(next_wait_us));
        if (found) {
          LOG_INFO("AI worker found task candidate", K(table_id), K(request_hex));
        }
      }
    }
  }
  return ret;
}

int claim_ai_task_group(observer::ObInnerSQLConnection &conn,
                        const uint64_t table_id,
                        const ObString &request_hex,
                        const char *lease_hex,
                        bool &claimed)
{
  int ret = OB_SUCCESS;
  int work_ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString sql;
  claimed = false;
  if (OB_ISNULL(lease_hex)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(sql.append("UPDATE "))) {
    LOG_WARN("failed to start AI task claim SQL", K(ret));
  } else if (OB_FAIL(append_ai_task_table(sql))) {
    LOG_WARN("failed to append centralized AI task table", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
                 " SET `%s` = %d, `%s` = X'%s',"
                 " `%s` = DATE_ADD(NOW(6), INTERVAL %ld SECOND),"
                 " `%s` = NOW(6) WHERE ",
                 AI_TASK_STATE,
                 static_cast<int>(schema::AI_COLUMN_TASK_STATE_RUNNING),
                 AI_TASK_LEASE_TOKEN, lease_hex,
                 AI_TASK_ELIGIBLE_TS, AI_WORKER_LEASE_SECONDS,
                 AI_TASK_GMT_MODIFIED))) {
    LOG_WARN("failed to append AI task claim values", K(ret));
  } else if (OB_FAIL(append_ai_task_group_predicate(
                 sql, table_id, request_hex))) {
    LOG_WARN("failed to append AI task claim key", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
                 " AND `%s` <= NOW(6) AND `%s` IN (%d, %d, %d)",
                 AI_TASK_ELIGIBLE_TS, AI_TASK_STATE,
                 static_cast<int>(schema::AI_COLUMN_TASK_STATE_PENDING),
                 static_cast<int>(schema::AI_COLUMN_TASK_STATE_RETRY),
                 static_cast<int>(schema::AI_COLUMN_TASK_STATE_RUNNING)))) {
    LOG_WARN("failed to finish AI task claim SQL", K(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(conn.start_transaction())) {
    LOG_WARN("failed to start AI task claim transaction", K(ret));
  } else if (OB_SUCC(ret)) {
    conn.get_session().set_table_name_hidden(false);
    if (OB_FAIL(conn.execute_write(sql.string(), affected_rows))) {
      LOG_WARN("failed to claim AI task group", K(ret), K(sql));
    } else {
      claimed = affected_rows > 0;
    }
    work_ret = ret;
    ret = finish_ai_worker_transaction(conn, work_ret);
    if (OB_FAIL(ret)) {
      claimed = false;
    }
  }
  return ret;
}

int fetch_claimed_ai_tasks(observer::ObInnerSQLConnection &conn,
                           const schema::ObTableSchema &base_schema,
                           const uint64_t table_id,
                           const ObString &request_hex,
                           const char *lease_hex,
                           ObIArray<ObAIWorkerTaskResult> &tasks,
                           bool &stale)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  tasks.reset();
  stale = false;
  if (OB_FAIL(sql.append_fmt(
          "SELECT `%s` AS __ai_column_id, `%s` AS __ai_spec_version,"
          " `%s` AS __ai_retry_count FROM ",
          AI_TASK_COLUMN_ID, AI_TASK_SPEC_VERSION, AI_TASK_RETRY_COUNT))) {
    LOG_WARN("failed to start claimed AI task SQL", K(ret));
  } else if (OB_FAIL(append_ai_task_table(sql))) {
    LOG_WARN("failed to append centralized claimed AI task table", K(ret));
  } else if (OB_FAIL(sql.append(" WHERE "))) {
    LOG_WARN("failed to append claimed AI task WHERE", K(ret));
  } else if (OB_FAIL(append_ai_task_group_predicate(
                 sql, table_id, request_hex))) {
    LOG_WARN("failed to append claimed AI task key", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
                 " AND `%s` = %d AND `%s` = X'%s' ORDER BY `%s`",
                 AI_TASK_STATE,
                 static_cast<int>(schema::AI_COLUMN_TASK_STATE_RUNNING),
                 AI_TASK_LEASE_TOKEN, lease_hex, AI_TASK_COLUMN_ID))) {
    LOG_WARN("failed to finish claimed AI task SQL", K(ret));
  }
  if (OB_SUCC(ret)) {
    ObISQLClient::ReadResult read_result;
    sqlclient::ObMySQLResult *result = NULL;
    conn.get_session().set_table_name_hidden(false);
    if (OB_FAIL(conn.execute_read(sql.string(), read_result))) {
      LOG_WARN("failed to fetch claimed AI tasks", K(ret), K(sql));
    } else if (OB_ISNULL(result = read_result.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("claimed AI task result is null", K(ret));
    } else {
      int next_ret = OB_SUCCESS;
      while (OB_SUCC(ret) && OB_SUCCESS == (next_ret = result->next())) {
        int64_t column_id = OB_INVALID_ID;
        int64_t spec_version = 0;
        ObAIWorkerTaskResult task;
        if (tasks.count() >= OB_MAX_COLUMN_NUMBER) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("claimed AI task group exceeds column limit", K(ret),
                   K(tasks.count()), K(OB_MAX_COLUMN_NUMBER));
        } else if (OB_FAIL(result->get_int(
                       "__ai_column_id", column_id))) {
          LOG_WARN("failed to read claimed AI column id", K(ret));
        } else if (OB_FAIL(result->get_int(
                       "__ai_spec_version", spec_version))) {
          LOG_WARN("failed to read claimed AI spec version", K(ret));
        } else if (column_id <= 0 || spec_version < 0) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("claimed AI task metadata is invalid", K(ret),
                   K(column_id), K(spec_version));
        } else if (OB_FAIL(result->get_int(
                       "__ai_retry_count", task.retry_count_))) {
          LOG_WARN("failed to read claimed AI retry count", K(ret));
        } else {
          task.column_id_ = static_cast<uint64_t>(column_id);
          task.schema_ = base_schema.get_ai_column_schema(task.column_id_);
          task.stale_ = OB_ISNULL(task.schema_) || task.schema_->get_spec_version() != spec_version;
          if (OB_FAIL(tasks.push_back(task))) {
            LOG_WARN("failed to append claimed AI task", K(ret));
          }
        }
      }
      if (OB_SUCC(ret) && OB_ITER_END != next_ret) {
        ret = next_ret;
        LOG_WARN("failed while iterating claimed AI tasks", K(ret));
      } else if (OB_SUCC(ret) && tasks.empty()) {
        stale = true;
      }
    }
  }
  return ret;
}

int evaluate_ai_task(observer::ObInnerSQLConnection &conn,
                     ObIAllocator &allocator,
                     const schema::ObDatabaseSchema &database_schema,
                     const schema::ObTableSchema &base_schema,
                     const ObString &rowkey_predicate,
                     const ObString &request_hex,
                     int64_t &total_result_hex_bytes,
                     ObAIWorkerTaskResult &task,
                     bool &stale)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  stale = false;
  if (OB_ISNULL(task.schema_)) {
    ret = OB_INVALID_ARGUMENT;
  }
  const schema::ObColumnSchemaV2 *request_column = OB_SUCC(ret)
      ? base_schema.get_column_schema(task.schema_->get_request_column_id())
      : NULL;
  const schema::ObColumnSchemaV2 *spec_column = OB_SUCC(ret)
      ? base_schema.get_column_schema(task.schema_->get_result_spec_column_id())
      : NULL;
  const schema::ObColumnSchemaV2 *state_column = OB_SUCC(ret)
      ? base_schema.get_column_schema(task.schema_->get_state_column_id())
      : NULL;
  if (OB_SUCC(ret) && (OB_ISNULL(request_column)
                       || OB_ISNULL(spec_column)
                       || OB_ISNULL(state_column)
                       || task.schema_->get_normalized_expr().empty())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI column evaluation metadata is incomplete", K(ret),
             KPC(task.schema_), KP(request_column), KP(spec_column),
             KP(state_column));
  } else if (OB_SUCC(ret)
             && OB_FAIL(sql.append_fmt(
                    "SELECT HEX((%.*s)) AS __ai_result FROM ",
                    task.schema_->get_normalized_expr().length(),
                    task.schema_->get_normalized_expr().ptr()))) {
    LOG_WARN("failed to append AI expression", K(ret), KPC(task.schema_));
  } else if (OB_SUCC(ret)
             && OB_FAIL(append_ai_qualified_table(
                    sql, database_schema.get_database_name_str(),
                    base_schema.get_table_name_str()))) {
    LOG_WARN("failed to append AI base table", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(sql.append_fmt(" WHERE %.*s AND ",
                                      rowkey_predicate.length(),
                                      rowkey_predicate.ptr()))) {
    LOG_WARN("failed to append AI evaluation rowkey", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(append_ai_identifier(
                    sql, request_column->get_column_name_str()))) {
    LOG_WARN("failed to append AI request guard", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(sql.append_fmt(" = X'%.*s' AND ",
                                      request_hex.length(),
                                      request_hex.ptr()))) {
    LOG_WARN("failed to append AI request value", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(append_ai_identifier(
                    sql, spec_column->get_column_name_str()))) {
    LOG_WARN("failed to append AI spec guard", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(sql.append_fmt(" = %ld AND ",
                                      task.schema_->get_spec_version()))) {
    LOG_WARN("failed to append AI spec value", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(append_ai_identifier(
                    sql, state_column->get_column_name_str()))) {
    LOG_WARN("failed to append AI state guard", K(ret));
  } else if (OB_SUCC(ret)
             && OB_FAIL(sql.append_fmt(
                    " = %d LIMIT 1",
                    static_cast<int>(schema::AI_COLUMN_VALUE_STATE_PENDING)))) {
    LOG_WARN("failed to finish AI evaluation SQL", K(ret));
  }

  if (OB_SUCC(ret)) {
    ObISQLClient::ReadResult read_result;
    sqlclient::ObMySQLResult *result = NULL;
    sql::ObSQLSessionInfo &session = conn.get_session();
    session.set_table_name_hidden(false);
    session.set_sql_mode(task.schema_->get_sql_mode());
    if (!task.schema_->get_time_zone().empty()
        && OB_FAIL(conn.set_session_variable(
               ObString::make_string("time_zone"),
               task.schema_->get_time_zone()))) {
      LOG_WARN("failed to restore AI expression time zone", K(ret),
               K(task.schema_->get_time_zone()));
    } else if (OB_FAIL(conn.execute_read(sql.string(), read_result))) {
      LOG_WARN("failed to evaluate AI column task", K(ret),
               K(task.schema_->get_table_id()),
               K(task.schema_->get_column_id()));
    } else if (OB_ISNULL(result = read_result.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI evaluation result is null", K(ret));
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        ret = OB_SUCCESS;
        stale = true;
      } else {
        LOG_WARN("failed to read AI evaluation result", K(ret));
      }
    } else {
      ObString result_hex;
      if (OB_FAIL(result->get_varchar("__ai_result", result_hex))) {
        LOG_WARN("failed to read AI result", K(ret));
      } else if (!is_ai_hex_value(result_hex)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("AI result is not valid hex", K(ret), K(result_hex.length()));
      } else if (result_hex.length() > AI_WORKER_MAX_RESULT_HEX_BYTES
                 || total_result_hex_bytes
                        > AI_WORKER_MAX_RESULT_HEX_BYTES
                              - result_hex.length()) {
        ret = OB_SIZE_OVERFLOW;
        LOG_WARN("AI row result exceeds bounded worker memory", K(ret),
                 K(result_hex.length()), K(total_result_hex_bytes),
                 K(AI_WORKER_MAX_RESULT_HEX_BYTES));
      } else if (OB_FAIL(ob_write_string(
                     allocator, result_hex, task.result_hex_))) {
        LOG_WARN("failed to copy AI result", K(ret), K(result_hex.length()));
      } else {
        total_result_hex_bytes += result_hex.length();
      }
    }
  }
  return ret;
}

int change_ai_task_group_state(observer::ObInnerSQLConnection &conn,
                               const uint64_t table_id,
                               const ObString &request_hex,
                               const char *lease_hex,
                               const int error_code,
                               const int64_t retry_count,
                               const bool remove_tasks)
{
  int ret = OB_SUCCESS;
  int work_ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString sql;
  if (remove_tasks) {
    if (OB_FAIL(sql.append("DELETE FROM "))) {
      LOG_WARN("failed to start stale AI task delete", K(ret));
    } else if (OB_FAIL(append_ai_task_table(sql))) {
      LOG_WARN("failed to append centralized stale AI task table", K(ret));
    } else if (OB_FAIL(sql.append(" WHERE "))) {
      LOG_WARN("failed to append stale AI task WHERE", K(ret));
    } else if (OB_FAIL(append_ai_task_group_predicate(
                   sql, table_id, request_hex))) {
      LOG_WARN("failed to append stale AI task key", K(ret));
    } else if (OB_FAIL(sql.append_fmt(" AND `%s` = X'%s'",
                                     AI_TASK_LEASE_TOKEN, lease_hex))) {
      LOG_WARN("failed to append stale AI task lease", K(ret));
    }
  } else {
    const int64_t shift = std::min<int64_t>(retry_count, 6);
    const int64_t delay_seconds = std::min<int64_t>(1LL << shift, 60);
    if (OB_FAIL(sql.append("UPDATE "))) {
      LOG_WARN("failed to start AI task retry", K(ret));
    } else if (OB_FAIL(append_ai_task_table(sql))) {
      LOG_WARN("failed to append centralized AI retry task table", K(ret));
    } else if (OB_FAIL(sql.append_fmt(
                   " SET `%s` = %d, `%s` = `%s` + 1,"
                   " `%s` = DATE_ADD(NOW(6), INTERVAL %ld SECOND),"
                   " `%s` = NULL, `%s` = %d,"
                   " `%s` = 'AI expression evaluation failed',"
                   " `%s` = NOW(6) WHERE ",
                   AI_TASK_STATE,
                   static_cast<int>(schema::AI_COLUMN_TASK_STATE_RETRY),
                   AI_TASK_RETRY_COUNT, AI_TASK_RETRY_COUNT,
                   AI_TASK_ELIGIBLE_TS, delay_seconds,
                   AI_TASK_LEASE_TOKEN, AI_TASK_LAST_ERROR_CODE, error_code,
                   AI_TASK_LAST_ERROR_MESSAGE, AI_TASK_GMT_MODIFIED))) {
      LOG_WARN("failed to append AI task retry values", K(ret));
    } else if (OB_FAIL(append_ai_task_group_predicate(
                   sql, table_id, request_hex))) {
      LOG_WARN("failed to append AI retry task key", K(ret));
    } else if (OB_FAIL(sql.append_fmt(" AND `%s` = X'%s'",
                                     AI_TASK_LEASE_TOKEN, lease_hex))) {
      LOG_WARN("failed to append AI retry task lease", K(ret));
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(conn.start_transaction())) {
    LOG_WARN("failed to start AI task state transaction", K(ret),
             K(remove_tasks));
  } else if (OB_SUCC(ret)) {
    conn.get_session().set_table_name_hidden(false);
    if (OB_FAIL(conn.execute_write(sql.string(), affected_rows))) {
      LOG_WARN("failed to change AI task group state", K(ret), K(sql),
               K(remove_tasks));
    }
    work_ret = ret;
    ret = finish_ai_worker_transaction(conn, work_ret);
  }
  return ret;
}

int append_ai_writeback_guard(ObSqlString &sql, const schema::ObTableSchema &base_schema,
                              const schema::ObAIColumnSchema &ai_schema, const ObString &request_hex)
{
  int ret = OB_SUCCESS;
  const auto *request_column = base_schema.get_column_schema(ai_schema.get_request_column_id());
  const auto *spec_column = base_schema.get_column_schema(ai_schema.get_result_spec_column_id());
  const auto *state_column = base_schema.get_column_schema(ai_schema.get_state_column_id());
  if (OB_ISNULL(request_column) || OB_ISNULL(spec_column)
      || OB_ISNULL(state_column)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI writeback guard metadata is missing", K(ret), K(ai_schema.get_column_id()));
  } else if (OB_FAIL(append_ai_identifier(
                 sql, request_column->get_column_name_str()))) {
    LOG_WARN("failed to append AI writeback request guard", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
                 " = X'%.*s' AND ", request_hex.length(),
                 request_hex.ptr()))) {
    LOG_WARN("failed to append AI writeback request id", K(ret));
  } else if (OB_FAIL(append_ai_identifier(
                 sql, spec_column->get_column_name_str()))) {
    LOG_WARN("failed to append AI writeback spec guard", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
                 " = %ld AND ", ai_schema.get_spec_version()))) {
    LOG_WARN("failed to append AI writeback spec version", K(ret));
  } else if (OB_FAIL(append_ai_identifier(
                 sql, state_column->get_column_name_str()))) {
    LOG_WARN("failed to append AI writeback state guard", K(ret));
  } else if (OB_FAIL(sql.append_fmt(
                 " = %d",
                 static_cast<int>(schema::AI_COLUMN_VALUE_STATE_PENDING)))) {
    LOG_WARN("failed to append AI writeback pending state", K(ret));
  }
  return ret;
}

// Lock the base row and select only derivations that still belong to this
// request. This keeps a single UPDATE for triggers, constraints and indexes.
int select_current_ai_tasks(observer::ObInnerSQLConnection &conn,
                            const schema::ObDatabaseSchema &database_schema,
                            const schema::ObTableSchema &base_schema,
                            const ObString &rowkey_predicate,
                            const ObString &request_hex,
                            ObIArray<const ObAIWorkerTaskResult *> &current)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  OZ(sql.append("SELECT "));
  for (int64_t i = 0; OB_SUCC(ret) && i < current.count(); ++i) {
    OZ(sql.append(i > 0 ? ", IFNULL(" : "IFNULL("));
    OZ(append_ai_writeback_guard(sql, base_schema, *current.at(i)->schema_, request_hex));
    OZ(sql.append(", 0)"));
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(sql.append(" FROM "))) {
  } else if (OB_FAIL(append_ai_qualified_table(sql, database_schema.get_database_name_str(),
                                               base_schema.get_table_name_str()))) {
  } else if (OB_FAIL(sql.append_fmt(" WHERE %.*s FOR UPDATE", rowkey_predicate.length(), rowkey_predicate.ptr()))) {
  } else {
    ObISQLClient::ReadResult rows;
    sqlclient::ObMySQLResult *result = nullptr;
    if (OB_FAIL(conn.execute_read(sql.string(), rows))) {
    } else if (OB_ISNULL(result = rows.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(result->next())) {
      if (OB_ITER_END == ret) {
        current.reset();
        ret = OB_SUCCESS;
      }
    } else {
      int64_t kept = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < current.count(); ++i) {
        int64_t matches = 0;
        if (OB_FAIL(result->get_int(i, matches))) {
        } else if (matches != 0) {
          current.at(kept++) = current.at(i);
        }
      }
      while (current.count() > kept) {
        current.pop_back();
      }
    }
  }
  return ret;
}

int update_ai_results(observer::ObInnerSQLConnection &conn,
                      const schema::ObDatabaseSchema &database_schema,
                      const schema::ObTableSchema &base_schema,
                      const ObString &rowkey_predicate,
                      const ObIArray<const ObAIWorkerTaskResult *> &tasks)
{
  int ret = OB_SUCCESS;
  int64_t base_affected_rows = 0;
  ObSqlString update_sql;
  if (tasks.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(update_sql.append("UPDATE "))) {
    LOG_WARN("failed to start AI writeback SQL", K(ret));
  } else if (OB_FAIL(append_ai_qualified_table(
                 update_sql, database_schema.get_database_name_str(),
                 base_schema.get_table_name_str()))) {
    LOG_WARN("failed to append AI writeback base table", K(ret));
  } else if (OB_FAIL(update_sql.append(" SET "))) {
    LOG_WARN("failed to append AI writeback SET", K(ret));
  }
  for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); ++i) {
    const ObAIWorkerTaskResult &task = *tasks.at(i);
    const schema::ObColumnSchemaV2 *result_column = OB_ISNULL(task.schema_)
        ? NULL : base_schema.get_column_schema(task.schema_->get_column_id());
    const schema::ObColumnSchemaV2 *state_column = OB_ISNULL(task.schema_)
        ? NULL : base_schema.get_column_schema(task.schema_->get_state_column_id());
    if (OB_ISNULL(result_column) || OB_ISNULL(state_column)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("AI writeback column metadata is missing", K(ret), K(i));
    } else if (i > 0 && OB_FAIL(update_sql.append(", "))) {
      LOG_WARN("failed to append AI writeback delimiter", K(ret));
    } else if (OB_FAIL(append_ai_identifier(
                   update_sql, result_column->get_column_name_str()))) {
      LOG_WARN("failed to append AI result column", K(ret));
    } else if (OB_SUCCESS == task.eval_ret_
               && OB_FAIL(update_sql.append_fmt(
                      " = CONVERT(X'%.*s' USING utf8mb4), ",
                      task.result_hex_.length(), task.result_hex_.ptr()))) {
      LOG_WARN("failed to append AI result value", K(ret));
    } else if (OB_SUCCESS != task.eval_ret_
               && OB_FAIL(update_sql.append(" = NULL, "))) {
      LOG_WARN("failed to append terminal AI null result", K(ret));
    } else if (OB_FAIL(append_ai_identifier(
                   update_sql, state_column->get_column_name_str()))) {
      LOG_WARN("failed to append AI value state column", K(ret));
    } else {
      const schema::ObAIColumnValueState value_state =
          OB_SUCCESS == task.eval_ret_ ? schema::AI_COLUMN_VALUE_STATE_READY
                                       : schema::AI_COLUMN_VALUE_STATE_FAILED;
      if (OB_FAIL(update_sql.append_fmt(
              " = %d", static_cast<int>(value_state)))) {
        LOG_WARN("failed to append AI value state", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)
      && OB_FAIL(update_sql.append_fmt(" WHERE %.*s",
                                      rowkey_predicate.length(),
                                      rowkey_predicate.ptr()))) {
    LOG_WARN("failed to append AI writeback rowkey", K(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(conn.execute_write(update_sql.string(), base_affected_rows))) {
    LOG_WARN("failed to publish AI results", K(ret), K(update_sql));
  } else if (OB_SUCC(ret) && base_affected_rows != 1) {
    ret = OB_EAGAIN;
  }
  return ret;
}

// Lock the persisted definition in the writeback transaction. A concurrent
// redefinition updates this same row, so its commit orders publication.
int lock_ai_definition(observer::ObInnerSQLConnection &conn,
                       const schema::ObAIColumnSchema &definition, bool &current)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObISQLClient::ReadResult rows;
  sqlclient::ObMySQLResult *result = nullptr;
  int64_t version = 0;
  current = false;
  if (OB_FAIL(sql.assign_fmt("SELECT spec_version FROM oceanbase.%s WHERE table_id = %lu"
                             " AND column_id = %lu FOR UPDATE", OB_ALL_AI_COLUMN_TNAME,
                             schema::ObSchemaUtils::get_extract_schema_id(definition.get_table_id()),
                             definition.get_column_id()))) {
  } else if (OB_FAIL(conn.execute_read(sql.string(), rows))) {
  } else if (OB_ISNULL(result = rows.get_result())) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(result->next())) {
    if (OB_ITER_END == ret) {
      ret = OB_SUCCESS;
    }
  } else if (OB_FAIL(result->get_int("spec_version", version))) {
  } else {
    current = version == definition.get_spec_version();
  }
  return ret;
}

int writeback_ai_task_group(observer::ObInnerSQLConnection &conn,
                            const schema::ObDatabaseSchema &database_schema,
                            const schema::ObTableSchema &base_schema,
                            const uint64_t table_id,
                            const ObString &rowkey_predicate,
                            const ObString &request_hex,
                            const char *lease_hex,
                            const ObIArray<ObAIWorkerTaskResult> &tasks)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(conn.start_transaction())) {
    LOG_WARN("failed to start AI writeback transaction", K(ret));
  } else {
    ObSEArray<const ObAIWorkerTaskResult *, 4> current;
    for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); ++i) {
      const ObAIWorkerTaskResult &task = tasks.at(i);
      bool active = false;
      if (!task.stale_ && OB_FAIL(lock_ai_definition(conn, *task.schema_, active))) {
        LOG_WARN("failed to lock AI definition", K(ret), K(task.column_id_));
      } else if (active && OB_FAIL(current.push_back(&task))) {
      }
    }
    conn.get_session().set_table_name_hidden(false);
    if (OB_SUCC(ret) && !current.empty()
        && OB_FAIL(select_current_ai_tasks(conn, database_schema, base_schema, rowkey_predicate,
                                           request_hex, current))) {
      LOG_WARN("failed to verify AI request on the base row", K(ret), K(table_id));
    } else if (OB_SUCC(ret) && !current.empty()
               && OB_FAIL(update_ai_results(conn, database_schema, base_schema, rowkey_predicate, current))) {
    }
    int64_t published_idx = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); ++i) {
      const ObAIWorkerTaskResult &task = tasks.at(i);
      const bool published = published_idx < current.count() && current.at(published_idx) == &task;
      int64_t deleted = 0;
      ObSqlString cleanup;
      if (OB_FAIL(cleanup.append("DELETE FROM "))) {
      } else if (OB_FAIL(append_ai_task_table(cleanup))) {
      } else if (OB_FAIL(cleanup.append(" WHERE "))) {
      } else if (OB_FAIL(append_ai_task_group_predicate(cleanup, table_id, request_hex))) {
      } else if (OB_FAIL(cleanup.append_fmt(" AND column_id = %lu AND lease_token = X'%s'",
                                            task.column_id_, lease_hex))) {
      } else if (OB_FAIL(conn.execute_write(cleanup.string(), deleted))) {
        LOG_WARN("failed to remove leased AI task", K(ret), K(task.column_id_));
      } else if (published) {
        ++published_idx;
        if (deleted != 1) {
          ret = OB_EAGAIN;
        }
      }
    }
    ret = finish_ai_worker_transaction(conn, ret);
  }
  return ret;
}

int process_one_ai_task(bool &did_work,
                        int64_t &next_wait_us)
{
  int ret = OB_SUCCESS;
  schema::ObSchemaGetterGuard schema_guard;
  ObArenaAllocator allocator("AIColumnWorker");
  sqlclient::ObISQLConnectionGuard conn_guard;
  observer::ObInnerSQLConnection *conn = NULL;
  const schema::ObTableSchema *base_schema = NULL;
  const schema::ObDatabaseSchema *database_schema = NULL;
  ObSqlString rowkey_predicate;
  ObString request_hex;
  uint64_t table_id = OB_INVALID_ID;
  bool found = false;
  bool claimed = false;
  bool stale = false;
  char lease_hex[33] = {'\0'};
  ObSEArray<ObAIWorkerTaskResult, 4> tasks;
  did_work = false;
  next_wait_us = AI_WORKER_RECOVERY_SCAN_US;
  if (OB_FAIL(observer::ObInnerSQLConnection::
          create_connection_with_owned_session(false, 0, conn_guard))) {
    LOG_WARN("failed to create AI worker SQL connection", K(ret));
  } else if (OB_ISNULL(conn = static_cast<observer::ObInnerSQLConnection *>(
                                  conn_guard.get_ptr()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("AI worker SQL connection is null", K(ret));
  } else {
    conn->get_session().set_ai_column_internal_dml(true);
    // A temporarily unreadable task tablet must not monopolize a worker.
    conn->set_user_timeout(AI_WORKER_METADATA_QUERY_TIMEOUT_US);
  }

  if (OB_SUCC(ret)
      && OB_FAIL(find_ai_task_candidate(
             *conn, allocator, found, table_id, rowkey_predicate,
             request_hex, next_wait_us))) {
    LOG_WARN("failed to find centralized AI task candidate", K(ret));
  } else if (OB_SUCC(ret)) {
    // Model calls and transactional writeback use the configured internal-SQL
    // timeout; only the non-blocking task-table probe is kept short.
    conn->set_user_timeout(0);
  }

  if (OB_SUCC(ret) && found
      && OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
    LOG_WARN("failed to get schema for AI task candidate", K(ret));
  } else if (OB_SUCC(ret) && found
      && OB_FAIL(schema_guard.get_table_schema(table_id, base_schema))) {
    LOG_WARN("failed to load AI task base table", K(ret), K(table_id));
  } else if (OB_SUCC(ret) && found && OB_ISNULL(base_schema)) {
    stale = true;
  } else if (OB_SUCC(ret) && found
             && (!base_schema->is_user_table()
                 || base_schema->is_index_table()
                 || base_schema->get_ai_column_schema_count() <= 0)) {
    stale = true;
  } else if (OB_SUCC(ret) && found
             && OB_FAIL(schema_guard.get_database_schema(
                    base_schema->get_database_id(), database_schema))) {
    LOG_WARN("failed to load AI task database", K(ret), K(table_id));
  } else if (OB_SUCC(ret) && found && OB_ISNULL(database_schema)) {
    stale = true;
  } else if (OB_SUCC(ret) && found
             && OB_FAIL(conn->get_session().set_default_database(
                    database_schema->get_database_name_str()))) {
    LOG_WARN("failed to set AI worker database", K(ret), K(table_id));
  }

  if (OB_SUCC(ret) && found
             && OB_FAIL(generate_ai_lease_id(lease_hex))) {
    LOG_WARN("failed to generate AI task lease", K(ret));
  } else if (OB_SUCC(ret) && found
             && OB_FAIL(claim_ai_task_group(
                    *conn, table_id, request_hex, lease_hex, claimed))) {
    LOG_WARN("failed to claim AI task group", K(ret), K(table_id));
  }
  if (OB_SUCC(ret) && claimed && !stale) {
    did_work = true;
    LOG_INFO("AI worker claimed task group",
             K(table_id), K(request_hex), K(lease_hex));
    if (OB_FAIL(fetch_claimed_ai_tasks(
            *conn, *base_schema, table_id, request_hex, lease_hex,
            tasks, stale))) {
      LOG_WARN("failed to fetch claimed AI task group", K(ret));
    }
  } else if (OB_SUCC(ret) && claimed) {
    did_work = true;
  }

  if (OB_SUCC(ret) && claimed && stale) {
    if (OB_FAIL(change_ai_task_group_state(
            *conn, table_id, request_hex, lease_hex,
            OB_SCHEMA_ERROR, 0, true))) {
      LOG_WARN("failed to remove stale AI task group", K(ret));
    }
  } else if (OB_SUCC(ret) && claimed) {
    int64_t total_result_hex_bytes = 0;
    for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); ++i) {
      if (tasks.at(i).stale_) {
        continue;
      }
      const int eval_ret = evaluate_ai_task(
          *conn, allocator, *database_schema, *base_schema,
          rowkey_predicate.string(), request_hex,
          total_result_hex_bytes, tasks.at(i), tasks.at(i).stale_);
      tasks.at(i).eval_ret_ = eval_ret;
      if (OB_SUCCESS != eval_ret) {
        LOG_WARN("AI task evaluation failed", K(eval_ret),
                 K(tasks.at(i).schema_->get_table_id()),
                 K(tasks.at(i).schema_->get_column_id()),
                 K(tasks.at(i).retry_count_));
      }
    }
    if (OB_SUCC(ret)) {
      bool need_retry = false;
      int first_error = OB_SUCCESS;
      int64_t max_retry_count = 0;
      for (int64_t i = 0; i < tasks.count(); ++i) {
        max_retry_count = std::max(max_retry_count, tasks.at(i).retry_count_);
        if (OB_SUCCESS != tasks.at(i).eval_ret_) {
          if (OB_SUCCESS == first_error) {
            first_error = tasks.at(i).eval_ret_;
          }
          if (tasks.at(i).retry_count_
              < tasks.at(i).schema_->get_max_retries()) {
            need_retry = true;
          }
        }
      }
      if (need_retry) {
        if (OB_FAIL(change_ai_task_group_state(
                *conn, table_id, request_hex, lease_hex,
                first_error, max_retry_count, false))) {
          LOG_WARN("failed to reschedule AI task group", K(ret));
        }
      } else {
        const int writeback_ret = writeback_ai_task_group(
            *conn, *database_schema, *base_schema, table_id,
            rowkey_predicate.string(), request_hex, lease_hex, tasks);
        if (OB_SUCCESS == writeback_ret) {
          // Complete: all affected AI values became visible in one transaction.
        } else if (OB_EAGAIN == writeback_ret) {
          LOG_INFO("AI task group lease changed during writeback",
                   K(writeback_ret), K(table_id));
        } else {
          bool retry_writeback = false;
          for (int64_t i = 0; i < tasks.count(); ++i) {
            retry_writeback = retry_writeback || (!tasks.at(i).stale_
                && tasks.at(i).retry_count_ < tasks.at(i).schema_->get_max_retries());
          }
          if (retry_writeback) {
            if (OB_FAIL(change_ai_task_group_state(
                    *conn, table_id, request_hex, lease_hex,
                    writeback_ret, max_retry_count, false))) {
              LOG_WARN("failed to reschedule AI writeback", K(ret),
                       K(writeback_ret));
            }
          } else {
            for (int64_t i = 0; i < tasks.count(); ++i) {
              tasks.at(i).eval_ret_ = writeback_ret;
              tasks.at(i).result_hex_.reset();
            }
            if (OB_FAIL(writeback_ai_task_group(
                    *conn, *database_schema, *base_schema, table_id,
                    rowkey_predicate.string(), request_hex,
                    lease_hex, tasks))) {
              LOG_WARN("failed to persist terminal AI writeback failure",
                       K(ret), K(writeback_ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

} // namespace

ObAiService::ObAiService()
  : share::ObThreadPool(),
    is_inited_(false),
    worker_cond_(),
    wakeup_pending_(false)
{
}

ObAiService::~ObAiService()
{
  destroy();
}

int ObAiService::server_module_init(ObAiService* &ai_service)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ai_service)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ai_service->init())) {
  }
  return ret;
}

int ObAiService::init()
{
  int ret = OB_SUCCESS;
  const int64_t worker_thread_count = config::ai_column_worker_thread_count();
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObAiService already initialized", K(ret));
  } else if (OB_UNLIKELY(worker_thread_count < AI_WORKER_MIN_THREAD_COUNT
                         || worker_thread_count > AI_WORKER_MAX_THREAD_COUNT)) {
    ret = OB_INVALID_CONFIG;
    LOG_WARN("invalid AI column worker thread count",
             K(ret), K(worker_thread_count));
  } else if (FALSE_IT(ObThreadPool::set_run_wrapper(
                 share::server_runtime()))) {
  } else if (OB_FAIL(ObThreadPool::set_thread_count(worker_thread_count))) {
    LOG_WARN("failed to configure AI worker thread count",
             K(ret), K(worker_thread_count));
  } else if (OB_FAIL(worker_cond_.init(ObWaitEventIds::DEFAULT_COND_WAIT))) {
    LOG_WARN("failed to initialize AI worker condition", K(ret));
  } else if (OB_FAIL(ObThreadPool::init())) {
    LOG_WARN("failed to initialize AI worker thread", K(ret));
    worker_cond_.destroy();
  } else {
    is_inited_ = true;
    LOG_INFO("AI service initialized", K(worker_thread_count));
  }
  return ret;
}

int ObAiService::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("AI service is not initialized", K(ret));
  } else if (OB_FAIL(ObThreadPool::start())) {
    LOG_WARN("failed to start AI worker", K(ret));
  }
  return ret;
}

void ObAiService::stop()
{
  ObThreadPool::stop();
  wakeup();
}

void ObAiService::wait()
{
  ObThreadPool::wait();
}

void ObAiService::destroy()
{
  if (is_inited_) {
    stop();
    wait();
    worker_cond_.destroy();
    is_inited_ = false;
  }
}

void ObAiService::run1()
{
  while (!has_set_stop()) {
    bool did_work = false;
    int64_t next_wait_us = AI_WORKER_RECOVERY_SCAN_US;
    const int ret = process_one_round_(did_work, next_wait_us);
    if (OB_SUCCESS != ret) {
      LOG_WARN("AI worker round failed", K(ret));
    }
    if (did_work) {
      ob_usleep(AI_WORKER_BUSY_US);
    } else {
      wait_for_work_(std::min(next_wait_us, AI_WORKER_RECOVERY_SCAN_US));
    }
  }
}

void ObAiService::wakeup()
{
  ObThreadCondGuard guard(worker_cond_);
  wakeup_pending_ = true;
  worker_cond_.broadcast();
}

void ObAiService::wait_for_work_(const int64_t wait_us)
{
  ObThreadCondGuard guard(worker_cond_);
  if (!wakeup_pending_ && !has_set_stop()) {
    worker_cond_.wait_us(std::max<int64_t>(1, wait_us));
  }
  wakeup_pending_ = false;
}

int ObAiService::process_one_round_(bool &did_work, int64_t &next_wait_us)
{
  int ret = OB_SUCCESS;
  did_work = false;
  next_wait_us = AI_WORKER_RECOVERY_SCAN_US;
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("schema service is null for AI worker", K(ret));
  } else if (OB_FAIL(process_one_ai_task(did_work, next_wait_us))) {
    LOG_WARN("failed to process centralized AI task", K(ret));
  }
  return ret;
}

int ObAiService::get_ai_service_guard(ObAiServiceGuard &ai_service_guard)
{
  // TODO: implement after cache support
  int ret = OB_SUCCESS;
  return ret;
}

ObAiServiceGuard::ObAiServiceGuard()
  : local_allocator_(ObMemAttr("AiServiceGuard", ObCtxIds::DEFAULT_CTX_ID))
{
}

ObAiServiceGuard::~ObAiServiceGuard()
{
}

int ObAiServiceGuard::check_access_privilege()
{
  int ret = OB_SUCCESS;
  bool has_priv = false;

  sql::ObSQLSessionInfo *session = nullptr;
  if (OB_ISNULL(session = THIS_WORKER.get_session())) {
    // system background task thread do not have session, skip check
  } else {
    ObArenaAllocator tmp_allocator;
    share::schema::ObSchemaGetterGuard *schema_guard = session->get_cur_exec_ctx()->get_sql_ctx()->schema_guard_;
    if (OB_ISNULL(schema_guard)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      sql::ObAIServiceEndpointPrivUtil priv_util(*schema_guard);
      share::schema::ObSessionPrivInfo session_priv;
      uint64_t user_id = session->get_priv_user_id();
      if (user_id == OB_INVALID_ID) {
        user_id = OB_SYS_USER_ID;
      }
      if (OB_FAIL(schema_guard->get_session_priv_info(
                                                    user_id,
                                                    session->get_database_name(),
                                                    session_priv))) {
      } else if (OB_FAIL(priv_util.check_access_ai_model_priv(tmp_allocator, session_priv, has_priv))) {
      } else if (!has_priv) {
        ret = OB_ERR_NO_PRIVILEGE;
        LOG_WARN("no privilege for ai model access", K(ret));
        LOG_USER_ERROR(OB_ERR_NO_PRIVILEGE, "access ai model");
      }
    }
  }

  return ret;
}

int ObAiServiceGuard::get_ai_endpoint(const common::ObString &name, const share::ObAiModelEndpointInfo *&endpoint_info)
{
  int ret = OB_SUCCESS;
  ObAiModelEndpointInfo *tmp_endpoint_info = nullptr;

  if (OB_FAIL(check_access_privilege())) {
  } else if (OB_ISNULL(tmp_endpoint_info = OB_NEWx(ObAiModelEndpointInfo, &local_allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ObAiServiceExecutor::read_ai_endpoint(local_allocator_, name, *tmp_endpoint_info))) {
  } else {
    endpoint_info = tmp_endpoint_info;
  }
  return ret;
}

int ObAiServiceGuard::get_ai_endpoint_by_ai_model_name(const common::ObString &ai_model_name, const share::ObAiModelEndpointInfo *&endpoint_info, bool need_check)
{
  int ret = OB_SUCCESS;
  ObAiModelEndpointInfo *tmp_endpoint_info = nullptr;

  if (need_check && OB_FAIL(check_access_privilege())) {
    LOG_WARN("failed to check access privilege", K(ret));
  } else if (OB_ISNULL(tmp_endpoint_info = OB_NEWx(ObAiModelEndpointInfo, &local_allocator_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(ObAiServiceExecutor::read_ai_endpoint_by_ai_model_name(local_allocator_, ai_model_name, *tmp_endpoint_info))) {
  } else {
    endpoint_info = tmp_endpoint_info;
  }
  return ret;
}

} // namespace omt
} // namespace oceanbase
