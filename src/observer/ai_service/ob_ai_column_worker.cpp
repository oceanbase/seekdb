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

#define USING_LOG_PREFIX SERVER

#include "observer/ai_service/ob_ai_column_worker.h"

#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/string/ob_sql_string.h"
#include "query/ai/ob_ai_endpoint_resolver.h"
#include "share/ob_server_struct.h"
#include "share/rc/ob_server_runtime.h"
#include "share/schema/ob_column_schema.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_schema_struct.h"
#include "share/schema/ob_table_schema.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_client.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"

using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;

namespace oceanbase
{
namespace observer
{

namespace
{

// The task table that stores the pending AI column tasks.
const char *const AI_TASK_TABLE = "oceanbase.__all_ai_column_task";
// The backoff of one retry of a failed task group.
const char *const RETRY_INTERVAL = "INTERVAL 1 SECOND";

int append_escaped_literal(ObSqlString &buf, const ObString &str)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(buf.append("'"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < str.length(); ++i) {
      const char c = str.ptr()[i];
      if ('\'' == c || '\\' == c) {
        char esc[2] = {'\\', c};
        ret = buf.append(esc, 2);
      } else {
        ret = buf.append(&c, 1);
      }
    }
    if (OB_SUCC(ret)) {
      ret = buf.append("'");
    }
  }
  return ret;
}

int build_hidden_name(ObIAllocator &alloc, const ObString &col_name,
                      const char *prefix, ObString &name)
{
  int ret = OB_SUCCESS;
  char buf[OB_MAX_COLUMN_NAME_LENGTH + 32];
  const int64_t len = snprintf(buf, sizeof(buf), "%s%.*s", prefix,
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
      name.assign_ptr(copy, static_cast<int32_t>(len));
    }
  }
  return ret;
}

// The result of the model must be convertible to the type of the AI result
// column, otherwise the generation of the column failed.
int convert_result_to_column_type(const ObColumnSchemaV2 &column,
                                  const ObString &raw_result,
                                  ObString &result)
{
  int ret = OB_SUCCESS;
  const ObObjType col_type = column.get_data_type();
  if (ob_is_integer_type(col_type)) {
    // the model must answer with one base-10 integer
    char buf[64];
    const int64_t len = MIN(raw_result.length(), static_cast<int64_t>(sizeof(buf)) - 1);
    MEMCPY(buf, raw_result.ptr(), len);
    buf[len] = '\0';
    char *end = NULL;
    (void)strtoll(buf, &end, 10);
    if (buf == end) {
      ret = OB_INVALID_DATA;
      LOG_WARN("the model result is not a valid integer", K(raw_result));
    } else {
      result = raw_result;
    }
  } else {
    // json, varchar, text and vector results are stored as they are, a
    // malformed value fails when the result is written back
    result = raw_result;
  }
  return ret;
}

} // anonymous namespace

ObAiColumnWorkerTask::ObAiColumnWorkerTask()
    : sql_proxy_(NULL),
      is_inited_(false)
{
}

ObAiColumnWorkerTask::~ObAiColumnWorkerTask()
{
}

int ObAiColumnWorkerTask::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else {
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
  }
  return ret;
}

void ObAiColumnWorkerTask::runTimerTask()
{
  ObCurTraceId::init(GCTX.self_addr());
  process_tasks();
}

void ObAiColumnWorkerTask::process_tasks()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else {
    ObArenaAllocator alloc("AiColWorker");
    ObSEArray<ObAiColumnTaskRow, 16> task_rows;
    if (OB_FAIL(fetch_due_tasks(alloc, task_rows))) {
      LOG_WARN("fail to fetch the due ai column tasks", K(ret));
    } else {
      if (task_rows.count() > 0) {
        LOG_INFO("ai column tasks are fetched", "rows", task_rows.count());
      }
      // the rows are ordered by (table_id, request_id), the rows of one
      // request group are consecutive
      int64_t group_start = 0;
      while (OB_SUCC(ret) && group_start < task_rows.count()) {
        const uint64_t group_table_id = task_rows.at(group_start).table_id_;
        const ObString &group_request_id = task_rows.at(group_start).request_id_;
        int64_t group_end = group_start + 1;
        while (group_end < task_rows.count()
               && task_rows.at(group_end).table_id_ == group_table_id
               && 0 == task_rows.at(group_end).request_id_.case_compare(group_request_id)) {
          ++group_end;
        }
        ObSEArray<ObAiColumnTaskRow, 16> group;
        for (int64_t i = group_start; OB_SUCC(ret) && i < group_end; ++i) {
          if (OB_FAIL(group.push_back(task_rows.at(i)))) {
            LOG_WARN("fail to build the task group", K(ret));
          }
        }
        if (OB_SUCC(ret) && OB_FAIL(process_group(alloc, group))) {
          LOG_WARN("fail to process the ai column task group", K(ret),
                   K(group_table_id), K(group_request_id));
          // one broken group must not stop the other groups of this round
          ret = OB_SUCCESS;
        }
        group_start = group_end;
      }
    }
  }
}

int ObAiColumnWorkerTask::fetch_due_tasks(ObIAllocator &alloc,
                                          ObIArray<ObAiColumnTaskRow> &task_rows)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    if (OB_ISNULL(sql_proxy_)) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(sql.append_fmt(
        "SELECT table_id, request_id, column_id, rowkey_predicate, "
        "coalesce(payload, ''), retry_count, state "
        "FROM %s WHERE state IN (%ld, %ld) AND eligible_ts <= now(6) "
        "ORDER BY table_id, request_id LIMIT 256",
        AI_TASK_TABLE,
        ObAiColumnTaskState::STATE_PENDING,
        ObAiColumnTaskState::STATE_RETRY_WAIT))) {
      LOG_WARN("fail to build the fetch sql", K(ret));
    } else if (OB_FAIL(sql_proxy_->read(res, sql.ptr()))) {
      LOG_WARN("fail to fetch the due ai column tasks", K(ret), K(sql));
    } else if (OB_ISNULL(res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the fetch of the ai column tasks has no result", K(ret));
    } else {
      sqlclient::ObMySQLResult *result = res.get_result();
      int tmp_ret = OB_SUCCESS;
      while (OB_SUCCESS == (tmp_ret = result->next())) {
        ObAiColumnTaskRow row;
        ObString request_id;
        ObString rowkey_predicate;
        ObString payload;
        int64_t table_id = 0;
        int64_t column_id = 0;
        // the numeric columns are signed int columns, they must be read as
        // an int64 rather than an uint64
        if (OB_FAIL(result->get_int(static_cast<int64_t>(0), table_id))
            || OB_FAIL(result->get_varchar(1, request_id))
            || OB_FAIL(result->get_int(2, column_id))
            || OB_FAIL(result->get_varchar(3, rowkey_predicate))
            || OB_FAIL(result->get_varchar(4, payload))
            || OB_FAIL(result->get_int(5, row.retry_count_))
            || OB_FAIL(result->get_int(6, row.state_))) {
          LOG_WARN("fail to read one ai column task row", K(ret));
          break;
        } else {
          row.table_id_ = static_cast<uint64_t>(table_id);
          row.column_id_ = static_cast<uint64_t>(column_id);
          // the strings of the result live until the next call of next(),
          // the task rows of this round are kept in the arena of the round
          char *req_copy = static_cast<char *>(alloc.alloc(request_id.length() + 1));
          char *pred_copy = static_cast<char *>(alloc.alloc(rowkey_predicate.length() + 1));
          char *payload_copy = static_cast<char *>(alloc.alloc(payload.length() + 1));
          if (OB_ISNULL(req_copy) || OB_ISNULL(pred_copy) || OB_ISNULL(payload_copy)) {
            ret = OB_ALLOCATE_MEMORY_FAILED;
            break;
          } else {
            MEMCPY(req_copy, request_id.ptr(), request_id.length());
            req_copy[request_id.length()] = '\0';
            MEMCPY(pred_copy, rowkey_predicate.ptr(), rowkey_predicate.length());
            pred_copy[rowkey_predicate.length()] = '\0';
            MEMCPY(payload_copy, payload.ptr(), payload.length());
            payload_copy[payload.length()] = '\0';
            row.request_id_.assign_ptr(req_copy, static_cast<int32_t>(request_id.length()));
            row.rowkey_predicate_.assign_ptr(pred_copy, static_cast<int32_t>(rowkey_predicate.length()));
            row.payload_.assign_ptr(payload_copy, static_cast<int32_t>(payload.length()));
            if (OB_FAIL(task_rows.push_back(row))) {
              break;
            }
          }
        }
      }
      if (OB_ITER_END == tmp_ret) {
        ret = OB_SUCCESS;
      }
      const int close_ret = result->close();
      if (OB_SUCCESS != close_ret) {
        LOG_WARN("fail to close the fetch result", K(close_ret));
      }
    }
  }
  return ret;
}

int ObAiColumnWorkerTask::process_group(ObArenaAllocator &alloc,
                                        const ObIArray<ObAiColumnTaskRow> &group)
{
  int ret = OB_SUCCESS;
  if (group.empty()) {
    // nothing to do
  } else {
    const uint64_t table_id = group.at(0).table_id_;
    ObSchemaGetterGuard guard;
    const ObTableSchema *table_schema = NULL;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_ISNULL(GCTX.schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(guard))) {
      LOG_WARN("fail to get the schema guard", K(ret));
    } else if (OB_FAIL(guard.get_table_schema(table_id, table_schema))) {
      LOG_WARN("fail to get the table schema", K(ret), K(table_id));
    } else if (OB_ISNULL(table_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the table schema of the ai column task is missing", K(ret), K(table_id));
    } else if (OB_FAIL(guard.get_database_schema(table_schema->get_database_id(), db_schema))) {
      LOG_WARN("fail to get the database schema", K(ret));
    } else if (OB_ISNULL(db_schema)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("the database schema of the ai column task is missing", K(ret));
    } else {
      ObSEArray<ColumnResult, 4> results;
      int64_t min_max_retries = INT64_MAX;
      int64_t max_retry_count = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
        const ObAiColumnTaskRow &row = group.at(i);
        const ObColumnSchemaV2 *column = table_schema->get_column_schema(row.column_id_);
        ObAiColumnSpec spec;
        ColumnResult col_result;
        col_result.column_id_ = row.column_id_;
        if (OB_ISNULL(column)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("the ai column of the task is missing", K(ret), K(row.column_id_));
        } else if (OB_FAIL(spec.parse(column->get_ai_spec_text(), alloc))) {
          LOG_WARN("fail to parse the ai column spec", K(ret), K(row.column_id_));
        } else if (OB_FAIL(build_hidden_name(alloc, column->get_column_name_str(),
                                             "__ai_state_", col_result.state_name_))
                   || OB_FAIL(build_hidden_name(alloc, column->get_column_name_str(),
                                                "__ai_request_", col_result.request_name_))) {
          LOG_WARN("fail to build the hidden column names", K(ret));
        } else {
          col_result.result_name_ = column->get_column_name_str();
          min_max_retries = MIN(min_max_retries, spec.max_retries_);
          max_retry_count = MAX(max_retry_count, row.retry_count_);
          if (OB_FAIL(generate_column(alloc, spec, *column, row.payload_, col_result.value_))) {
            col_result.success_ = false;
            col_result.err_ = ret;
            LOG_WARN("fail to generate the ai column result", K(ret), K(row.column_id_));
            // the generation of one column failed, the whole group fails
            ret = OB_SUCCESS;
          } else {
            col_result.success_ = true;
          }
        }
        if (OB_FAIL(results.push_back(col_result))) {
          LOG_WARN("fail to push the column result", K(ret));
        }
      }
      if (OB_SUCC(ret)) {
        bool all_success = true;
        for (int64_t i = 0; i < results.count(); ++i) {
          all_success = all_success && results.at(i).success_;
        }
        const int64_t next_retry_count = max_retry_count + 1;
        if (all_success) {
          if (OB_FAIL(publish_group(alloc, group, results,
                                    db_schema->get_database_name_str(),
                                    table_schema->get_table_name_str()))) {
            LOG_WARN("fail to publish the ai column group", K(ret));
          }
        } else if (min_max_retries == INT64_MAX || next_retry_count > min_max_retries) {
          // the retry budget of the group is exhausted, the row is marked as
          // failed and the task rows are removed
          if (OB_FAIL(fail_group(alloc, group, results,
                                 db_schema->get_database_name_str(),
                                 table_schema->get_table_name_str()))) {
            LOG_WARN("fail to mark the ai column group as failed", K(ret));
          }
        } else {
          if (OB_FAIL(retry_group(alloc, group, next_retry_count, ObString("generate failed")))) {
            LOG_WARN("fail to retry the ai column group", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObAiColumnWorkerTask::generate_column(ObArenaAllocator &alloc,
                                          const ObAiColumnSpec &spec,
                                          const ObColumnSchemaV2 &column,
                                          const ObString &payload,
                                          ObString &result)
{
  int ret = OB_SUCCESS;
  if (payload.empty()) {
    ret = OB_INVALID_DATA;
    LOG_WARN("the payload of the ai column task is empty", K(ret));
  } else if (spec.is_embed()) {
    // the embedding of one text, the typed system prompt is not used
    ObAIFuncExprInfo *info = NULL;
    ObAiModelEndpointInfo endpoint;
    query::ObIAiEndpointResolver *endpoint_resolver =
        share::server_service<query::ObIAiEndpointResolver>();
    if (OB_ISNULL(endpoint_resolver)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(alloc, spec.model_name_, info))) {
      LOG_WARN("fail to resolve the ai model", K(ret), K(spec.model_name_));
    } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(spec.model_name_, alloc, endpoint))) {
      LOG_WARN("fail to resolve the ai endpoint", K(ret), K(spec.model_name_));
    } else {
      ObAIFuncModel model(alloc, *info, endpoint);
      ObJsonObject *config = NULL;
      ObString cfg_text = spec.config_;
      if (!cfg_text.empty()
          && OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(alloc, cfg_text, config))) {
        LOG_WARN("fail to parse the ai column config", K(ret));
      } else if (OB_FAIL(model.call_dense_embedding(const_cast<ObString &>(payload), config, result))) {
        LOG_WARN("fail to call the embedding model", K(ret));
      }
    }
  } else {
    // the completion of one text, the typed system prompt of the result
    // column type is sent as the system message
    ObString sys_prompt;
    if (OB_FAIL(ObAiColumnSpec::compute_system_prompt(column, alloc, sys_prompt))) {
      LOG_WARN("fail to compute the system prompt", K(ret));
    } else {
      ObAIFuncExprInfo *info = NULL;
      ObAiModelEndpointInfo endpoint;
      query::ObIAiEndpointResolver *endpoint_resolver =
          share::server_service<query::ObIAiEndpointResolver>();
      ObAIFuncIComplete *provider = NULL;
      ObArray<ObString> headers;
      ObJsonObject *body = NULL;
      ObJsonObject *response = NULL;
      ObIJsonBase *result_base = NULL;
      ObAIFuncClient client;
      ObString unencrypted_access_key;
      ObString request_model_name;
      ObJsonObject *config = NULL;
      ObString cfg_text = spec.config_;
      ObString raw_result;
      if (OB_ISNULL(endpoint_resolver)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(alloc, spec.model_name_, info))) {
        LOG_WARN("fail to resolve the ai model", K(ret), K(spec.model_name_));
      } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(spec.model_name_, alloc, endpoint))) {
        LOG_WARN("fail to resolve the ai endpoint", K(ret), K(spec.model_name_));
      } else if (OB_FAIL(ObAIFuncUtils::get_complete_provider(alloc, endpoint.get_provider(), provider))) {
        LOG_WARN("fail to get the completion provider", K(ret));
      } else if (OB_FAIL(endpoint.get_unencrypted_access_key(alloc, unencrypted_access_key))) {
        LOG_WARN("fail to get the access key", K(ret));
      } else if (OB_FAIL(provider->get_header(alloc, unencrypted_access_key, headers))) {
        LOG_WARN("fail to build the request header", K(ret));
      } else if (!spec.config_.empty()
                 && OB_FAIL(ObAIFuncJsonUtils::get_json_object_form_str(alloc, cfg_text, config))) {
        LOG_WARN("fail to parse the ai column config", K(ret));
      } else {
        request_model_name = info->model_;
        if (!endpoint.get_request_model_name().empty()) {
          request_model_name = endpoint.get_request_model_name();
        }
        if (OB_FAIL(provider->get_body(alloc, request_model_name, sys_prompt,
                                       const_cast<ObString &>(payload), config, body))) {
          LOG_WARN("fail to build the request body", K(ret));
        } else if (OB_FAIL(client.send_post(alloc, endpoint.get_url(), headers, body, response))) {
          LOG_WARN("fail to call the completion model", K(ret));
        } else if (OB_FAIL(provider->parse_output(alloc, response, result_base))) {
          LOG_WARN("fail to parse the completion response", K(ret));
        } else if (OB_FAIL(ObAIFuncJsonUtils::print_json_to_str(alloc, result_base, raw_result))) {
          LOG_WARN("fail to print the completion result", K(ret));
        } else if (OB_FAIL(convert_result_to_column_type(column, raw_result, result))) {
          LOG_WARN("the model result does not fit the ai column type", K(ret));
        }
      }
    }
  }
  return ret;
}

int ObAiColumnWorkerTask::publish_group(ObArenaAllocator &alloc,
                                        const ObIArray<ObAiColumnTaskRow> &group,
                                        const ObIArray<ColumnResult> &results,
                                        const ObString &db_name,
                                        const ObString &table_name)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = group.at(0).table_id_;
  ObMySQLTransaction trans;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
    LOG_WARN("fail to start the publish transaction", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
      const ObAiColumnTaskRow &row = group.at(i);
      const ColumnResult *col_result = NULL;
      for (int64_t j = 0; OB_SUCC(ret) && NULL == col_result && j < results.count(); ++j) {
        if (results.at(j).column_id_ == row.column_id_) {
          col_result = &results.at(j);
        }
      }
      if (OB_ISNULL(col_result)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the result of the ai column is missing", K(ret), K(row.column_id_));
      } else {
        ObSqlString sql;
        int64_t affected_rows = 0;
        // the publish is guarded by the request id of the row: when a newer
        // request invalidated the row, the old result is not written back
        if (OB_FAIL(sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = ",
                                   static_cast<int32_t>(db_name.length()), db_name.ptr(),
                                   static_cast<int32_t>(table_name.length()), table_name.ptr(),
                                   static_cast<int32_t>(col_result->result_name_.length()),
                                   col_result->result_name_.ptr()))) {
          LOG_WARN("fail to build the publish sql", K(ret));
        } else if (OB_FAIL(append_escaped_literal(sql, col_result->value_))) {
          LOG_WARN("fail to append the publish value", K(ret));
        } else if (OB_FAIL(sql.append_fmt(", `%.*s` = 'READY', `%.*s` = ",
                           static_cast<int32_t>(col_result->state_name_.length()),
                           col_result->state_name_.ptr(),
                           static_cast<int32_t>(col_result->request_name_.length()),
                           col_result->request_name_.ptr()))) {
          LOG_WARN("fail to append the state update", K(ret));
        } else if (OB_FAIL(append_escaped_literal(sql, row.request_id_))) {
          LOG_WARN("fail to append the request id", K(ret));
        } else if (OB_FAIL(sql.append_fmt(" WHERE %.*s AND `%.*s` = ",
                                          static_cast<int32_t>(row.rowkey_predicate_.length()),
                                          row.rowkey_predicate_.ptr(),
                                          static_cast<int32_t>(col_result->request_name_.length()),
                                          col_result->request_name_.ptr()))) {
          LOG_WARN("fail to append the publish predicate", K(ret));
        } else if (OB_FAIL(append_escaped_literal(sql, row.request_id_))) {
          LOG_WARN("fail to append the publish request guard", K(ret));
        } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
          LOG_WARN("fail to publish the ai column result", K(ret), K(sql));
        }
      }
    }
    if (OB_SUCC(ret)) {
      // the whole group is published, its tasks are removed in the same
      // transaction so that the results become visible atomically
      ObSqlString sql;
      int64_t affected_rows = 0;
      const ObString &request_id = group.at(0).request_id_;
      if (OB_FAIL(sql.append_fmt("DELETE FROM %s WHERE table_id = %lu AND request_id = ",
                                 AI_TASK_TABLE, table_id))) {
        LOG_WARN("fail to build the task removal sql", K(ret));
      } else if (OB_FAIL(append_escaped_literal(sql, request_id))) {
        LOG_WARN("fail to append the removal request id", K(ret));
      } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
        LOG_WARN("fail to remove the published ai column tasks", K(ret), K(sql));
      }
    }
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_WARN("fail to commit the publish transaction", KR(ret), K(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  UNUSED(alloc);
  return ret;
}

int ObAiColumnWorkerTask::retry_group(ObArenaAllocator &alloc,
                                      const ObIArray<ObAiColumnTaskRow> &group,
                                      int64_t next_retry_count,
                                      const ObString &last_error)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = group.at(0).table_id_;
  const ObString &request_id = group.at(0).request_id_;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(sql.append_fmt("UPDATE %s SET state = %ld, retry_count = %ld, "
                                    "eligible_ts = date_add(now(6), %s), last_error = ",
                                    AI_TASK_TABLE,
                                    ObAiColumnTaskState::STATE_RETRY_WAIT,
                                    next_retry_count,
                                    RETRY_INTERVAL))) {
    LOG_WARN("fail to build the retry sql", K(ret));
  } else if (OB_FAIL(append_escaped_literal(sql, last_error))) {
    LOG_WARN("fail to append the last error", K(ret));
  } else if (OB_FAIL(sql.append_fmt(" WHERE table_id = %lu AND request_id = ", table_id))) {
    LOG_WARN("fail to build the retry predicate", K(ret));
  } else if (OB_FAIL(append_escaped_literal(sql, request_id))) {
    LOG_WARN("fail to append the request id", K(ret));
  } else if (OB_FAIL(sql_proxy_->write(sql.ptr(), affected_rows))) {
    LOG_WARN("fail to retry the ai column tasks", K(ret), K(sql));
  }
  UNUSED(alloc);
  return ret;
}

int ObAiColumnWorkerTask::fail_group(ObArenaAllocator &alloc,
                                     const ObIArray<ObAiColumnTaskRow> &group,
                                     const ObIArray<ColumnResult> &results,
                                     const ObString &db_name,
                                     const ObString &table_name)
{
  int ret = OB_SUCCESS;
  const uint64_t table_id = group.at(0).table_id_;
  ObMySQLTransaction trans;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
    LOG_WARN("fail to start the fail transaction", K(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
      const ObAiColumnTaskRow &row = group.at(i);
      const ColumnResult *col_result = NULL;
      for (int64_t j = 0; OB_SUCC(ret) && NULL == col_result && j < results.count(); ++j) {
        if (results.at(j).column_id_ == row.column_id_) {
          col_result = &results.at(j);
        }
      }
      if (OB_ISNULL(col_result)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("the failed ai column of the task is missing", K(ret), K(row.column_id_));
      } else {
        ObSqlString sql;
        int64_t affected_rows = 0;
        // the terminal failure is guarded by the request id as well: the row
        // of a newer request keeps its own generation state
        if (OB_FAIL(sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = NULL, `%.*s` = 'FAILED'",
                                   static_cast<int32_t>(db_name.length()), db_name.ptr(),
                                   static_cast<int32_t>(table_name.length()), table_name.ptr(),
                                   static_cast<int32_t>(col_result->result_name_.length()),
                                   col_result->result_name_.ptr(),
                                   static_cast<int32_t>(col_result->state_name_.length()),
                                   col_result->state_name_.ptr()))) {
          LOG_WARN("fail to build the fail sql", K(ret));
        } else if (OB_FAIL(sql.append_fmt(" WHERE %.*s AND `%.*s` = ",
                                          static_cast<int32_t>(row.rowkey_predicate_.length()),
                                          row.rowkey_predicate_.ptr(),
                                          static_cast<int32_t>(col_result->request_name_.length()),
                                          col_result->request_name_.ptr()))) {
          LOG_WARN("fail to append the fail predicate", K(ret));
        } else if (OB_FAIL(append_escaped_literal(sql, row.request_id_))) {
          LOG_WARN("fail to append the fail request guard", K(ret));
        } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
          LOG_WARN("fail to mark the ai column as failed", K(ret), K(sql));
        }
      }
    }
    if (OB_SUCC(ret)) {
      // the task rows of the terminal failure are removed, the state of the
      // row keeps the failure
      ObSqlString sql;
      int64_t affected_rows = 0;
      const ObString &request_id = group.at(0).request_id_;
      if (OB_FAIL(sql.append_fmt("DELETE FROM %s WHERE table_id = %lu AND request_id = ",
                                 AI_TASK_TABLE, table_id))) {
        LOG_WARN("fail to build the task removal sql", K(ret));
      } else if (OB_FAIL(append_escaped_literal(sql, request_id))) {
        LOG_WARN("fail to append the removal request id", K(ret));
      } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
        LOG_WARN("fail to remove the failed ai column tasks", K(ret), K(sql));
      }
    }
  }
  if (trans.is_started()) {
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = trans.end(OB_SUCC(ret)))) {
      LOG_WARN("fail to commit the fail transaction", KR(ret), K(tmp_ret));
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  UNUSED(alloc);
  return ret;
}

// ---------------------------------- ObAiColumnWorkerScheduler -----------------------------------------//

ObAiColumnWorkerScheduler::ObAiColumnWorkerScheduler()
    : is_inited_(false),
      timer_(),
      worker_task_()
{
}

int ObAiColumnWorkerScheduler::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (IS_INIT) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(timer_.init("AiColWorker", common::ObMemAttr("AiColWorker")))) {
    LOG_WARN("fail to init the ai column worker timer", K(ret));
  } else if (OB_FAIL(worker_task_.init(sql_proxy))) {
    LOG_WARN("fail to init the ai column worker task", K(ret));
  } else {
    is_inited_ = true;
    LOG_INFO("the ai column worker is inited");
  }
  return ret;
}

int ObAiColumnWorkerScheduler::start()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(timer_.start())) {
    LOG_WARN("fail to start the ai column worker timer", K(ret));
  } else if (OB_FAIL(timer_.schedule(worker_task_, AI_COLUMN_WORKER_PERIOD_US, true))) {
    LOG_WARN("fail to schedule the ai column worker", K(ret));
  } else {
    LOG_INFO("the ai column worker is started");
  }
  return ret;
}

void ObAiColumnWorkerScheduler::stop()
{
  if (timer_.inited()) {
    timer_.stop();
  }
}

void ObAiColumnWorkerScheduler::wait()
{
  if (timer_.inited()) {
    timer_.wait();
  }
}

void ObAiColumnWorkerScheduler::destroy()
{
  timer_.destroy();
}

} // namespace observer
} // namespace oceanbase
