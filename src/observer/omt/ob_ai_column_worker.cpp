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

#include "observer/omt/ob_ai_column_worker.h"

#include <stdio.h>
#include <string.h>

#include "common/mysqlclient/ob_mysql_result.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/allocator/page_arena.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/string/ob_string_buffer.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/string/ob_sql_string.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/utility.h"
#include "share/ob_server_struct.h"
#include "query/api/query/ai/ob_ai_endpoint_resolver.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/rc/ob_server_runtime.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"
#include "sql/resolver/ddl/ob_ai_column_def.h"

namespace oceanbase
{
namespace omt
{

using namespace common;
using namespace share;
using namespace share::schema;

namespace
{

constexpr int64_t AI_COLUMN_WORKER_INTERVAL_US = 100LL * 1000LL;       // 100ms
constexpr int64_t AI_COLUMN_TASK_BATCH_SIZE = 32;
constexpr int64_t AI_COLUMN_RUNNING_TIMEOUT_SEC = 60;
constexpr int64_t AI_COLUMN_RETRY_DELAY_SEC = 1;

enum AiTaskState
{
  AI_TASK_PENDING = 1,
  AI_TASK_RUNNING = 2,
  AI_TASK_RETRY_WAIT = 3
};

const char *ai_state_pending() { return "PENDING"; }
const char *ai_state_ready() { return "READY"; }
const char *ai_state_failed() { return "FAILED"; }

const char *ai_type_hint(const int64_t result_type)
{
  const ObObjType type = static_cast<ObObjType>(result_type);
  const char *hint = "";
  if (ob_is_json(type)) {
    hint = "Return exactly one valid JSON value";
  } else if (ob_is_integer_type(type)) {
    hint = "Return exactly one base-10 integer";
  }
  return hint;
}

int append_literal(const ObString &value, ObSqlString &sql)
{
  return sql_append_hex_escape_str(value, sql);
}

// Appends the model result as a plain quoted SQL string literal.
// NOTE: a hex literal X'...' is a BINARY string: a json column would store it
// as a json string scalar (not an object) and an integer column would fail to
// cast it. Only the result value needs this; identifiers/keys keep hex escape.
int append_result_literal(const ObString &value, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append("'"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < value.length(); ++i) {
      char c = value.ptr()[i];
      if (c == '\'' || c == '\\') {
        if (OB_FAIL(sql.append("\\", 1))) {
        }
      }
      if (OB_SUCC(ret)) {
        ret = sql.append(&c, 1);
      }
    }
  }
  if (OB_SUCC(ret)) {
    ret = sql.append("'");
  }
  return ret;
}

int make_hidden_name(ObIAllocator &alloc,
                     const ObString &column_name,
                     const char *suffix,
                     ObString &out)
{
  int ret = OB_SUCCESS;
  ObSqlString buf;
  if (OB_FAIL(buf.append_fmt("__ai_%.*s%s", column_name.length(), column_name.ptr(), suffix))) {
  } else if (OB_FAIL(ob_write_string(alloc, buf.string(), out))) {
  }
  return ret;
}

// Calls the registered completion model with an explicit system instruction.
int call_completion_model(ObIAllocator &alloc,
                          const ObString &model_name,
                          const ObString &system_prompt,
                          const ObString &content,
                          ObString &result)
{
  int ret = OB_SUCCESS;
  share::ObAiModelEndpointInfo endpoint_info;
  query::ObIAiEndpointResolver *endpoint_resolver =
      share::server_service<query::ObIAiEndpointResolver>();
  ObAIFuncExprInfo *info = nullptr;
  if (model_name.empty() || content.empty()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(alloc, model_name, info))) {
  } else if (OB_ISNULL(info) || OB_ISNULL(endpoint_resolver)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(endpoint_resolver->resolve_by_model_name(model_name, alloc, endpoint_info))) {
  } else {
    ObString system_str = system_prompt;
    ObString content_str = content;
    ObAIFuncModel model(alloc, *info, endpoint_info);
    if (OB_FAIL(model.call_completion_with_system(system_str, content_str, nullptr, result))) {
    }
  }
  return ret;
}

struct AiColumnCtx
{
  AiColumnCtx()
    : task_(nullptr), def_(), state_name_(), request_name_(), result_text_(),
      retry_count_(0), failed_(false), err_code_(OB_SUCCESS)
  {}
  const void *task_;
  ObAiColumnDef def_;
  ObString state_name_;
  ObString request_name_;
  ObString result_text_;
  int64_t retry_count_;
  bool failed_;
  int err_code_;
};

} // anonymous namespace

ObAiColumnWorker::ObAiColumnWorker()
  : timer_(),
    sql_proxy_(nullptr),
    is_inited_(false),
    is_running_(false),
    round_(0)
{
  disable_timeout_check();
}

ObAiColumnWorker::~ObAiColumnWorker()
{
  destroy();
}

int ObAiColumnWorker::init(common::ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(timer_.init("AiColumnWorker", common::ObMemAttr("AiColWorker")))) {
  } else {
    sql_proxy_ = &sql_proxy;
    is_inited_ = true;
  }
  return ret;
}

int ObAiColumnWorker::start()
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(timer_.schedule(*this, AI_COLUMN_WORKER_INTERVAL_US,
                                     true /* repeat */, true /* immediate */))) {
    LOG_WARN("failed to schedule ai column worker", K(ret));
  }
  return ret;
}

void ObAiColumnWorker::stop()
{
  if (timer_.inited()) {
    timer_.stop();
  }
}

void ObAiColumnWorker::wait()
{
  if (timer_.inited()) {
    timer_.wait();
  }
}

void ObAiColumnWorker::destroy()
{
  if (timer_.inited()) {
    timer_.destroy();
  }
  is_inited_ = false;
}

void ObAiColumnWorker::runTimerTask()
{
  if (!is_inited_ || is_running_) {
    // the previous round is still running, skip this tick
  } else {
    is_running_ = true;
    ObCurTraceId::init(GCTX.self_addr());
    int ret = OB_SUCCESS;
    if (OB_FAIL(do_work())) {
      LOG_WARN("ai column worker round failed", K(ret));
    }
    is_running_ = false;
  }
}

int ObAiColumnWorker::do_work()
{
  int ret = OB_SUCCESS;
  ObArenaAllocator alloc("AiColWorker");
  ObSEArray<TaskRow, AI_COLUMN_TASK_BATCH_SIZE> tasks;
  int tmp_ret = OB_SUCCESS;
  // recover tasks left in RUNNING by a crash, once every 5 seconds
  if (0 == round_ % 50 && OB_TMP_FAIL(recover_stale_tasks())) {
    LOG_WARN("failed to recover stale ai column tasks", K(tmp_ret));
  }
  ++round_;
  if (OB_FAIL(claim_tasks(alloc, tasks))) {
    LOG_WARN("failed to claim ai column tasks", K(ret));
  } else if (tasks.empty()) {
    // nothing to do
  } else if (OB_FAIL(process_all(alloc, tasks))) {
    LOG_WARN("failed to process ai column tasks", K(ret));
  }
  return ret;
}

int ObAiColumnWorker::recover_stale_tasks()
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObMySQLTransaction trans;
  int64_t affected_rows = 0;
  bool started = false;
  if (OB_FAIL(sql.append_fmt(
        "UPDATE %s SET state = %d WHERE state = %d AND gmt_modified < "
        "date_sub(now(6), interval %ld second)",
        OB_ALL_AI_COLUMN_TASK_TNAME, AI_TASK_PENDING, AI_TASK_RUNNING,
        AI_COLUMN_RUNNING_TIMEOUT_SEC))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (FALSE_IT(started = true)) {
  } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
  } else if (OB_FAIL(trans.end(true))) {
  }
  if (started && OB_FAIL(ret)) {
    int tmp_ret = trans.end(false);
    LOG_WARN("failed to rollback stale task recovery", K(tmp_ret));
  }
  return ret;
}

int ObAiColumnWorker::claim_tasks(common::ObIAllocator &alloc, common::ObIArray<TaskRow> &tasks)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  ObMySQLTransaction trans;
  bool started = false;
  if (OB_FAIL(select_sql.append_fmt(
        "SELECT table_id, column_id, request_id, rowkey_predicate, state, retry_count "
        "FROM %s WHERE state IN (%d, %d) AND eligible_ts <= now(6) "
        "ORDER BY table_id, column_id, request_id LIMIT %ld",
        OB_ALL_AI_COLUMN_TASK_TNAME, AI_TASK_PENDING, AI_TASK_RETRY_WAIT, AI_COLUMN_TASK_BATCH_SIZE))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (FALSE_IT(started = true)) {
  } else {
    ObISQLClient::ReadResult read_result;
    if (OB_FAIL(trans.read(read_result, select_sql.ptr()))) {
    } else if (OB_ISNULL(read_result.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      common::sqlclient::ObMySQLResult *result = read_result.get_result();
      while (OB_SUCC(ret)) {
        TaskRow row;
        int64_t table_id = 0;
        int64_t column_id = 0;
        if (OB_FAIL(result->next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_FAIL(result->get_int("table_id", table_id))
                   || OB_FAIL(result->get_int("column_id", column_id))
                   || OB_FAIL(result->get_varchar("request_id", row.request_id_))
                   || OB_FAIL(result->get_varchar("rowkey_predicate", row.rowkey_predicate_))
                   || OB_FAIL(result->get_int("retry_count", row.retry_count_))
                   || OB_FAIL(result->get_int("state", row.state_))) {
        } else {
          row.table_id_ = static_cast<uint64_t>(table_id);
          row.column_id_ = static_cast<uint64_t>(column_id);
          ObString request_copy = row.request_id_;
          ObString predicate_copy = row.rowkey_predicate_;
          if (OB_FAIL(ob_write_string(alloc, request_copy, row.request_id_))
              || OB_FAIL(ob_write_string(alloc, predicate_copy, row.rowkey_predicate_))) {
          } else if (OB_FAIL(tasks.push_back(row))) {
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && started) {
    for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); ++i) {
      ObSqlString update_sql;
      int64_t affected_rows = 0;
      const TaskRow &row = tasks.at(i);
      if (OB_FAIL(update_sql.append_fmt(
            "UPDATE %s SET state = %d WHERE table_id = %lu AND column_id = %lu AND request_id = ",
            OB_ALL_AI_COLUMN_TASK_TNAME, AI_TASK_RUNNING,
            row.table_id_, row.column_id_))) {
      } else if (OB_FAIL(append_literal(row.request_id_, update_sql))) {
      } else if (OB_FAIL(trans.write(update_sql.ptr(), affected_rows))) {
      }
    }
  }
  if (started) {
    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true))) {
        LOG_WARN("failed to commit claimed tasks", K(ret));
      }
    } else {
      int tmp_ret = trans.end(false);
      LOG_WARN("failed to rollback claimed tasks", K(tmp_ret));
    }
  }
  return ret;
}

int ObAiColumnWorker::process_all(common::ObIAllocator &alloc, common::ObIArray<TaskRow> &tasks)
{
  int ret = OB_SUCCESS;
  bool handled[AI_COLUMN_TASK_BATCH_SIZE];
  MEMSET(handled, 0, sizeof(handled));
  for (int64_t i = 0; OB_SUCC(ret) && i < tasks.count(); ++i) {
    if (handled[i]) {
      continue;
    }
    ObSEArray<TaskRow, 4> group;
    const TaskRow &first = tasks.at(i);
    for (int64_t k = i; OB_SUCC(ret) && k < tasks.count(); ++k) {
      const TaskRow &candidate = tasks.at(k);
      if (!handled[k]
          && candidate.table_id_ == first.table_id_
          && candidate.request_id_ == first.request_id_) {
        if (OB_FAIL(group.push_back(candidate))) {
        } else {
          handled[k] = true;
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(process_group(alloc, group))) {
      LOG_WARN("failed to process ai column group", K(ret), K(first.table_id_));
    }
  }
  return ret;
}

int ObAiColumnWorker::delete_group_tasks(const common::ObIArray<TaskRow> &group)
{
  int ret = OB_SUCCESS;
  ObSqlString delete_sql;
  ObMySQLTransaction trans;
  int64_t affected_rows = 0;
  bool started = false;
  if (group.empty()) {
    // nothing to do
  } else if (OB_FAIL(delete_sql.append_fmt("DELETE FROM %s WHERE table_id = %lu AND request_id = ",
                                           OB_ALL_AI_COLUMN_TASK_TNAME, group.at(0).table_id_))) {
  } else if (OB_FAIL(append_literal(group.at(0).request_id_, delete_sql))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (FALSE_IT(started = true)) {
  } else if (OB_FAIL(trans.write(delete_sql.ptr(), affected_rows))) {
  }
  if (started) {
    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true))) {
      }
    } else {
      int tmp_ret = trans.end(false);
      LOG_WARN("failed to rollback ai column task cleanup", K(tmp_ret));
    }
  }
  return ret;
}

int ObAiColumnWorker::process_group(common::ObIAllocator &alloc, common::ObIArray<TaskRow> &group)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  const ObTableSchema *table_schema = NULL;
  const ObDatabaseSchema *db_schema = NULL;
  ObSEArray<AiColumnCtx *, 4> cols;
  if (group.empty()) {
    // nothing to do
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(guard))) {
  } else if (OB_FAIL(guard.get_table_schema(group.at(0).table_id_, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(guard.get_database_schema(table_schema->get_database_id(), db_schema))) {
  } else if (OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    // 1. resolve the definition of every column of this group
    for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
      const TaskRow &task = group.at(i);
      void *mem = alloc.alloc(sizeof(AiColumnCtx));
      AiColumnCtx *ctx = (NULL == mem) ? NULL : new (mem) AiColumnCtx();
      const ObColumnSchemaV2 *result_column = table_schema->get_column_schema(task.column_id_);
      const ObColumnSchemaV2 *state_column = NULL;
      if (OB_ISNULL(ctx)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_ISNULL(result_column)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai column result column is missing", K(ret), K(task.column_id_));
      } else if (FALSE_IT(ctx->task_ = &task)) {
      } else if (FALSE_IT(ctx->retry_count_ = task.retry_count_)) {
      } else if (OB_FAIL(make_hidden_name(alloc, result_column->get_column_name_str(),
                                          AI_COLUMN_STATE_SUFFIX, ctx->state_name_))
                 || OB_FAIL(make_hidden_name(alloc, result_column->get_column_name_str(),
                                             AI_COLUMN_REQUEST_SUFFIX, ctx->request_name_))) {
      } else if (OB_ISNULL(state_column = table_schema->get_column_schema(ctx->state_name_))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai column state column is missing", K(ret), K(ctx->state_name_));
      } else if (OB_FAIL(decode_ai_column_def(state_column->get_comment(), alloc, ctx->def_))) {
      } else if (OB_FAIL(cols.push_back(ctx))) {
      }
    }
  }

  // 2. read the current request id and the model input from the user table
  ObSEArray<ObString, 8> inputs;
  bool stale = false;
  if (OB_SUCC(ret)) {
    ObSqlString select_sql;
    const ObString &db_name = db_schema->get_database_name_str();
    const ObString &table_name = table_schema->get_table_name_str();
    const ObString &predicate = group.at(0).rowkey_predicate_;
    if (OB_FAIL(select_sql.append_fmt("SELECT "))) {
    }
    for (int64_t i = 0; OB_SUCC(ret) && i < cols.count(); ++i) {
      const AiColumnCtx *ctx = cols.at(i);
      if (i > 0 && OB_FAIL(select_sql.append(", "))) {
      } else if (OB_FAIL(select_sql.append_fmt("`%.*s` AS r%ld, IFNULL(CAST((%.*s) AS CHAR), '') AS v%ld",
                                               ctx->request_name_.length(), ctx->request_name_.ptr(), i,
                                               ctx->def_.input_expr_.length(), ctx->def_.input_expr_.ptr(), i))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(select_sql.append_fmt(" FROM `%.*s`.`%.*s` WHERE %.*s",
                                                      db_name.length(), db_name.ptr(),
                                                      table_name.length(), table_name.ptr(),
                                                      predicate.length(), predicate.ptr()))) {
    }
    if (OB_SUCC(ret)) {
      ObMySQLTransaction trans;
      bool started = false;
      ObISQLClient::ReadResult read_result;
      if (OB_FAIL(trans.start(sql_proxy_))) {
      } else if (FALSE_IT(started = true)) {
      } else if (OB_FAIL(trans.read(read_result, select_sql.ptr()))) {
      } else if (OB_ISNULL(read_result.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        common::sqlclient::ObMySQLResult *result = read_result.get_result();
        int next_ret = result->next();
        if (OB_ITER_END == next_ret) {
          // the row is gone: drop the tasks, never resurrect a deleted row
          stale = true;
        } else if (OB_FAIL(next_ret)) {
          ret = next_ret;
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && !stale && i < cols.count(); ++i) {
            const AiColumnCtx *ctx = cols.at(i);
            ObString request_value;
            ObString input_value;
            char req_name[32];
            char val_name[32];
            snprintf(req_name, sizeof(req_name), "r%ld", i);
            snprintf(val_name, sizeof(val_name), "v%ld", i);
            if (OB_FAIL(result->get_varchar(req_name, request_value))) {
            } else if (request_value != group.at(i).request_id_) {
              // superseded by a newer change of this row
              stale = true;
            } else if (OB_FAIL(result->get_varchar(val_name, input_value))) {
            } else if (OB_FAIL(ob_write_string(alloc, input_value, input_value))) {
            } else if (OB_FAIL(inputs.push_back(input_value))) {
            }
          }
        }
      }
      if (started) {
        int tmp_ret = trans.end(false);
        if (OB_SUCCESS != tmp_ret) {
          LOG_WARN("failed to end read transaction", K(tmp_ret));
        }
      }
    }
  }

  if (OB_SUCC(ret) && stale) {
    // the row is gone or a newer request won: drop the tasks and publish nothing
    ret = delete_group_tasks(group);
  }

  // 3. call the models (no lock is held here)
  bool need_retry = false;
  for (int64_t i = 0; OB_SUCC(ret) && !stale && i < cols.count(); ++i) {
    AiColumnCtx *ctx = cols.at(i);
    ObString result_text;
    const char *hint = ai_type_hint(ctx->def_.result_type_);
    const ObString system_prompt(static_cast<int32_t>(strlen(hint)), hint);
    int tmp_ret = call_completion_model(alloc, ctx->def_.model_name_, system_prompt,
                                        inputs.at(i), result_text);
    if (OB_SUCCESS == tmp_ret) {
      ctx->failed_ = false;
      ctx->result_text_ = result_text;
    } else {
      ctx->failed_ = true;
      ctx->err_code_ = tmp_ret;
      if (ctx->retry_count_ < ctx->def_.max_retries_) {
        need_retry = true;
      }
      LOG_WARN("ai column model call failed", K(tmp_ret), K(ctx->def_.model_name_),
               K(ctx->retry_count_), K(ctx->def_.max_retries_));
    }
  }

  // 4. publish the group in one transaction
  if (OB_SUCC(ret) && !stale) {
    ObMySQLTransaction trans;
    bool started = false;
    if (OB_FAIL(trans.start(sql_proxy_))) {
    } else if (FALSE_IT(started = true)) {
    } else if (need_retry) {
      ObSqlString retry_sql;
      int64_t affected_rows = 0;
      if (OB_FAIL(retry_sql.append_fmt(
            "UPDATE %s SET state = %d, retry_count = retry_count + 1, "
            "eligible_ts = date_add(now(6), interval %ld second) "
            "WHERE table_id = %lu AND request_id = ",
            OB_ALL_AI_COLUMN_TASK_TNAME, AI_TASK_RETRY_WAIT, AI_COLUMN_RETRY_DELAY_SEC,
            group.at(0).table_id_))) {
      } else if (OB_FAIL(append_literal(group.at(0).request_id_, retry_sql))) {
      } else if (OB_FAIL(trans.write(retry_sql.ptr(), affected_rows))) {
      }
    } else {
      const ObString &db_name = db_schema->get_database_name_str();
      const ObString &table_name = table_schema->get_table_name_str();
      const ObString &predicate = group.at(0).rowkey_predicate_;
      for (int64_t i = 0; OB_SUCC(ret) && i < cols.count(); ++i) {
        const AiColumnCtx *ctx = cols.at(i);
        ObSqlString publish_sql;
        int64_t affected_rows = 0;
        if (ctx->failed_) {
          if (OB_FAIL(publish_sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = '%s' WHERE %.*s AND `%.*s` = ",
                                             db_name.length(), db_name.ptr(),
                                             table_name.length(), table_name.ptr(),
                                             ctx->state_name_.length(), ctx->state_name_.ptr(),
                                             ai_state_failed(),
                                             predicate.length(), predicate.ptr(),
                                             ctx->request_name_.length(), ctx->request_name_.ptr()))) {
          }
        } else if (OB_FAIL(publish_sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = ",
                                                  db_name.length(), db_name.ptr(),
                                                  table_name.length(), table_name.ptr(),
                                                  ctx->def_.column_name_.length(),
                                                  ctx->def_.column_name_.ptr()))) {
        } else if (OB_FAIL(append_result_literal(ctx->result_text_, publish_sql))) {
        } else if (OB_FAIL(publish_sql.append_fmt(", `%.*s` = '%s' WHERE %.*s AND `%.*s` = ",
                                                  ctx->state_name_.length(), ctx->state_name_.ptr(),
                                                  ai_state_ready(),
                                                  predicate.length(), predicate.ptr(),
                                                  ctx->request_name_.length(), ctx->request_name_.ptr()))) {
        }
        if (OB_FAIL(ret)) {
        } else if (OB_FAIL(append_literal(group.at(0).request_id_, publish_sql))) {
        } else if (OB_FAIL(trans.write(publish_sql.ptr(), affected_rows))) {
        }
      }
      if (OB_SUCC(ret)) {
        ObSqlString delete_sql;
        int64_t affected_rows = 0;
        if (OB_FAIL(delete_sql.append_fmt("DELETE FROM %s WHERE table_id = %lu AND request_id = ",
                                          OB_ALL_AI_COLUMN_TASK_TNAME, group.at(0).table_id_))) {
        } else if (OB_FAIL(append_literal(group.at(0).request_id_, delete_sql))) {
        } else if (OB_FAIL(trans.write(delete_sql.ptr(), affected_rows))) {
        }
      }
    }
    if (started) {
      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("failed to publish ai column group", K(ret));
        }
      } else {
        int tmp_ret = trans.end(false);
        LOG_WARN("failed to rollback ai column group", K(tmp_ret));
      }
    }
  }
  return ret;
}

} // namespace omt
} // namespace oceanbase
