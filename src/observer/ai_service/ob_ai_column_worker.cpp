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

#define USING_LOG_PREFIX SERVER

#include "observer/ai_service/ob_ai_column_worker.h"

#include <string>
#include <unistd.h>

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/string/ob_sql_string.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_server_struct.h"

using namespace oceanbase::common;

namespace oceanbase
{
namespace observer
{

namespace
{

const int64_t AI_COLUMN_WORKER_BATCH = 64;
const int64_t AI_COLUMN_WORKER_IDLE_US = 100 * 1000;
// After a task was written the worker polls at this short interval, so a row
// that is still invisible because its transaction has not committed yet is
// claimed right after the commit instead of one idle tick later.
const int64_t AI_COLUMN_WORKER_FAST_US = 10 * 1000;
const int32_t AI_COLUMN_WORKER_FAST_ROUNDS = 40;
const int64_t AI_COLUMN_RETRY_DELAY_US = 500 * 1000;

struct AiTaskRow
{
  int64_t task_id_;
  int64_t table_id_;
  int64_t column_id_;
  int64_t request_id_;
  int64_t retry_count_;
  int64_t state_;
  std::string predicate_;
};

int append_quoted(ObSqlString &sql, const std::string &value)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append("'"))) {
  } else {
    for (size_t i = 0; OB_SUCC(ret) && i < value.size(); ++i) {
      if ('\'' == value[i] || '\\' == value[i]) {
        if (OB_FAIL(sql.append("\\"))) {
        }
      }
      if (OB_SUCC(ret) && OB_FAIL(sql.append_fmt("%c", value[i]))) {
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(sql.append("'"))) {
    }
  }
  return ret;
}

int update_user_row(ObMySQLTransaction &trans,
                    const std::string &database,
                    const std::string &table,
                    const std::string &result_column,
                    const std::string &state_column,
                    const std::string *value,
                    const std::string &predicate)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_FAIL(sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = ",
                             static_cast<int>(database.size()), database.c_str(),
                             static_cast<int>(table.size()), table.c_str(),
                             static_cast<int>(result_column.size()),
                             result_column.c_str()))) {
  } else if (OB_NOT_NULL(value)) {
    if (OB_FAIL(append_quoted(sql, *value))) {
    }
  } else if (OB_FAIL(sql.append("NULL"))) {
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(sql.append_fmt(", `%.*s` = %s WHERE ",
                               static_cast<int>(state_column.size()),
                               state_column.c_str(),
                               OB_NOT_NULL(value) ? "1" : "2"))) {
    } else if (OB_FAIL(sql.append(predicate.c_str()))) {
    } else {
      int64_t affected_rows = 0;
      if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
        LOG_WARN("failed to publish ai column result", K(ret), K(sql));
      }
    }
  }
  return ret;
}

int delete_task(ObMySQLTransaction &trans, int64_t task_id)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_FAIL(sql.append_fmt("DELETE FROM oceanbase.%s WHERE task_id = %ld",
                             OB_ALL_AI_COLUMN_TASK_TNAME, task_id))) {
  } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
  }
  return ret;
}

// A task only owns the row if it is still the newest task for that
// (table, column, row). A later DML deletes older tasks and creates a new one,
// so a missing task (or a different request id) means this result is stale.
int check_task_alive(ObMySQLTransaction &trans,
                     int64_t task_id,
                     int64_t request_id,
                     bool &alive)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  alive = false;
  if (OB_FAIL(sql.append_fmt("SELECT request_id FROM oceanbase.%s WHERE task_id = %ld",
                             OB_ALL_AI_COLUMN_TASK_TNAME, task_id))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      if (OB_FAIL(trans.read(res, sql.ptr(), 0))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
      } else {
        int64_t current_request_id = 0;
        EXTRACT_INT_FIELD_MYSQL(*result, "request_id", current_request_id, int64_t);
        alive = current_request_id == request_id;
      }
    }
  }
  return ret;
}

struct AiTaskOutcome
{
  AiTaskRow row_;
  bool claimed_;
  bool success_;
  bool has_row_;
  int64_t max_retries_;
  std::string database_;
  std::string table_;
  std::string result_column_;
  std::string state_column_;
  std::string result_;
};

int load_task_metadata(const AiTaskRow &task, AiTaskOutcome &outcome)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_FAIL(sql.append_fmt(
          "SELECT a.column_name, IFNULL(a.max_retries, 0) AS max_retries,"
          " t.table_name, d.database_name FROM oceanbase.%s a"
          " JOIN oceanbase.%s t ON t.table_id = a.table_id"
          " JOIN oceanbase.%s d ON d.database_id = t.database_id"
          " WHERE a.table_id = %ld AND a.column_id = %ld",
          OB_ALL_AI_COLUMN_TNAME, OB_ALL_TABLE_TNAME, OB_ALL_DATABASE_TNAME,
          task.table_id_, task.column_id_))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      ObString column_name;
      ObString table_name;
      ObString database_name;
      int64_t max_retries = 0;
      if (OB_ISNULL(GCTX.sql_proxy_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          ret = OB_ENTRY_NOT_EXIST;
        }
      } else {
        EXTRACT_VARCHAR_FIELD_MYSQL(*result, "column_name", column_name);
        EXTRACT_INT_FIELD_MYSQL(*result, "max_retries", max_retries, int64_t);
        EXTRACT_VARCHAR_FIELD_MYSQL(*result, "table_name", table_name);
        EXTRACT_VARCHAR_FIELD_MYSQL(*result, "database_name", database_name);
        if (OB_SUCC(ret)) {
          outcome.result_column_.assign(column_name.ptr(), column_name.length());
          outcome.table_.assign(table_name.ptr(), table_name.length());
          outcome.database_.assign(database_name.ptr(), database_name.length());
          outcome.state_column_ = "__ai_state_" + outcome.result_column_;
          outcome.max_retries_ = max_retries;
        }
      }
    }
  }
  return ret;
}

int claim_task(int64_t task_id)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  int64_t affected_rows = 0;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(sql.append_fmt(
                 "UPDATE %s SET state = 1 WHERE task_id = %ld AND state IN (0, 3)",
                 OB_ALL_AI_COLUMN_TASK_TNAME, task_id))) {
  } else if (OB_FAIL(GCTX.sql_proxy_->write(sql.ptr(), 0, affected_rows))) {
  } else if (1 != affected_rows) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int evaluate_task(const AiTaskRow &task, AiTaskOutcome &outcome)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  std::string normalized_expr;
  ObString expr_value;
  if (OB_FAIL(sql.append_fmt(
          "SELECT a.normalized_expr AS ai_expr FROM oceanbase.%s a"
          " WHERE a.table_id = %ld AND a.column_id = %ld",
          OB_ALL_AI_COLUMN_TNAME, task.table_id_, task.column_id_))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      if (OB_ISNULL(GCTX.sql_proxy_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result->next())) {
        if (OB_ITER_END == ret) {
          ret = OB_ENTRY_NOT_EXIST;
        }
      } else {
        EXTRACT_VARCHAR_FIELD_MYSQL(*result, "ai_expr", expr_value);
        if (OB_SUCC(ret)) {
          normalized_expr.assign(expr_value.ptr(), expr_value.length());
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    ObSqlString select_sql;
    if (OB_FAIL(select_sql.append_fmt("SELECT %s AS ai_result FROM `%.*s`.`%.*s` WHERE %s",
            normalized_expr.c_str(),
            static_cast<int>(outcome.database_.size()), outcome.database_.c_str(),
            static_cast<int>(outcome.table_.size()), outcome.table_.c_str(),
            task.predicate_.c_str()))) {
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res2) {
        ObMySQLResult *result2 = nullptr;
        if (OB_FAIL(GCTX.sql_proxy_->read(res2, select_sql.ptr()))) {
        } else if (OB_ISNULL(result2 = res2.get_result())) {
          ret = OB_ERR_UNEXPECTED;
        } else if (OB_FAIL(result2->next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            outcome.has_row_ = false;
          }
        } else {
          ObString value;
          EXTRACT_VARCHAR_FIELD_MYSQL(*result2, "ai_result", value);
          if (OB_SUCC(ret)) {
            outcome.has_row_ = true;
            outcome.result_.assign(value.ptr(), value.length());
          }
        }
      }
    }
  }
  return ret;
}

void process_group(const std::vector<AiTaskRow> &group)
{
  int ret = OB_SUCCESS;
  std::vector<AiTaskOutcome> outcomes;
  outcomes.reserve(group.size());
  for (size_t i = 0; i < group.size() && OB_SUCC(ret); ++i) {
    AiTaskOutcome outcome;
    outcome.row_ = group[i];
    outcome.claimed_ = false;
    outcome.success_ = false;
    outcome.has_row_ = false;
    outcome.max_retries_ = 0;
    if (OB_FAIL(load_task_metadata(group[i], outcome))) {
      LOG_WARN("failed to load ai column metadata", K(ret), K(group[i].task_id_));
      if (OB_ENTRY_NOT_EXIST == ret) {
        // The column, or the table that owns it, is gone: the task can never
        // run. Drop it, otherwise the worker keeps re-reading it every tick.
        ObMySQLTransaction trans;
        if (OB_SUCC(trans.start(GCTX.sql_proxy_))) {
          int del_ret = delete_task(trans, group[i].task_id_);
          trans.end(OB_SUCC(del_ret));
        }
      }
      ret = OB_SUCCESS;
    } else if (OB_FAIL(claim_task(group[i].task_id_))) {
      if (OB_ENTRY_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
      }
    } else {
      outcome.claimed_ = true;
      int eval_ret = evaluate_task(group[i], outcome);
      // A failed model call is a normal outcome: record it and let the
      // publish transaction persist the retry/failed state.
      outcome.success_ = (OB_SUCCESS == eval_ret);
      outcomes.push_back(outcome);
      ret = OB_SUCCESS;
    }
  }

  if (OB_SUCC(ret) && !outcomes.empty()) {
    ObMySQLTransaction trans;
    if (OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(trans.start(GCTX.sql_proxy_))) {
      LOG_WARN("failed to start ai column publish transaction", K(ret));
    } else {
      for (size_t i = 0; OB_SUCC(ret) && i < outcomes.size(); ++i) {
        AiTaskOutcome &outcome = outcomes[i];
        if (!outcome.claimed_) {
          continue;
        }
        if (outcome.success_) {
          if (!outcome.has_row_) {
            if (OB_FAIL(delete_task(trans, outcome.row_.task_id_))) {
            }
          } else {
            bool alive = false;
            if (OB_FAIL(check_task_alive(trans, outcome.row_.task_id_,
                                         outcome.row_.request_id_, alive))) {
            } else if (!alive) {
              // A newer request owns the row; drop this stale result.
            } else if (OB_FAIL(update_user_row(trans, outcome.database_, outcome.table_,
                                               outcome.result_column_, outcome.state_column_,
                                               &outcome.result_, outcome.row_.predicate_))) {
            } else if (OB_FAIL(delete_task(trans, outcome.row_.task_id_))) {
            }
          }
        } else if (outcome.row_.retry_count_ < outcome.max_retries_) {
          bool alive = false;
          if (OB_FAIL(check_task_alive(trans, outcome.row_.task_id_,
                                       outcome.row_.request_id_, alive))) {
          } else if (!alive) {
            // A newer change owns the row already; the stale task is dropped
            // instead of being retried.
            if (OB_FAIL(delete_task(trans, outcome.row_.task_id_))) {
            }
          } else {
            ObSqlString sql;
            int64_t affected_rows = 0;
            if (OB_FAIL(sql.append_fmt(
                    "UPDATE %s SET state = 3, retry_count = IFNULL(retry_count, 0) + 1,"
                    " eligible_ts = DATE_ADD(now(6), INTERVAL %ld MICROSECOND)"
                    " WHERE task_id = %ld",
                    OB_ALL_AI_COLUMN_TASK_TNAME, AI_COLUMN_RETRY_DELAY_US,
                    outcome.row_.task_id_))) {
            } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
            }
          }
        } else {
          bool alive = false;
          if (OB_FAIL(check_task_alive(trans, outcome.row_.task_id_,
                                       outcome.row_.request_id_, alive))) {
          } else if (alive
                     && OB_FAIL(update_user_row(trans, outcome.database_, outcome.table_,
                                                outcome.result_column_, outcome.state_column_,
                                                nullptr, outcome.row_.predicate_))) {
          } else if (OB_FAIL(delete_task(trans, outcome.row_.task_id_))) {
          }
        }
      }
      int end_ret = trans.end(OB_SUCC(ret));
      if (OB_SUCCESS != end_ret && OB_SUCC(ret)) {
        ret = end_ret;
      }
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("failed to process ai column task group", K(ret));
  }
}

} // anonymous namespace

ObAiColumnWorker &ObAiColumnWorker::get_instance()
{
  static ObAiColumnWorker instance;
  return instance;
}

ObAiColumnWorker::ObAiColumnWorker()
  : started_(false), stopped_(false), thread_()
{
}

int ObAiColumnWorker::start()
{
  int ret = OB_SUCCESS;
  bool expected = false;
  if (started_.compare_exchange_strong(expected, true)) {
    stopped_ = false;
    thread_ = std::thread([this] { this->run(); });
    thread_.detach();
  }
  return ret;
}

void ObAiColumnWorker::stop()
{
  stopped_ = true;
}

void ObAiColumnWorker::notify_task_created()
{
  ObAiColumnWorker &worker = get_instance();
  {
    std::lock_guard<std::mutex> guard(worker.mutex_);
    worker.fast_rounds_.store(AI_COLUMN_WORKER_FAST_ROUNDS);
  }
  worker.cv_.notify_one();
}

void ObAiColumnWorker::run()
{
  while (!stopped_) {
    process_tasks();
    std::unique_lock<std::mutex> lock(mutex_);
    if (fast_rounds_.load() > 0) {
      fast_rounds_.fetch_sub(1);
      cv_.wait_for(lock, std::chrono::microseconds(AI_COLUMN_WORKER_FAST_US));
    } else {
      cv_.wait_for(lock, std::chrono::microseconds(AI_COLUMN_WORKER_IDLE_US));
    }
  }
}

void ObAiColumnWorker::process_tasks()
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  std::vector<AiTaskRow> tasks;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(sql.append_fmt(
                 "SELECT task_id, table_id, column_id, request_id, rowkey_predicate,"
                 " IFNULL(retry_count, 0) AS retry_count, IFNULL(state, 0) AS state"
                 " FROM oceanbase.%s WHERE state IN (0, 3) AND eligible_ts <= now(6)"
                 " ORDER BY task_id LIMIT %ld",
                 OB_ALL_AI_COLUMN_TASK_TNAME, AI_COLUMN_WORKER_BATCH))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      if (OB_FAIL(GCTX.sql_proxy_->read(res, sql.ptr()))) {
        LOG_WARN("failed to read ai column tasks", K(ret));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        while (OB_SUCC(result->next())) {
          AiTaskRow row;
          ObString predicate;
          EXTRACT_INT_FIELD_MYSQL(*result, "task_id", row.task_id_, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "table_id", row.table_id_, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "column_id", row.column_id_, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "request_id", row.request_id_, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "retry_count", row.retry_count_, int64_t);
          EXTRACT_INT_FIELD_MYSQL(*result, "state", row.state_, int64_t);
          EXTRACT_VARCHAR_FIELD_MYSQL(*result, "rowkey_predicate", predicate);
          if (OB_SUCC(ret)) {
            row.predicate_.assign(predicate.ptr(), predicate.length());
            tasks.push_back(row);
          }
        }
        if (OB_ITER_END == ret) {
          ret = OB_SUCCESS;
        }
      }
    }
  }
  if (OB_SUCC(ret) && !tasks.empty()) {
    std::vector<bool> handled(tasks.size(), false);
    for (size_t i = 0; i < tasks.size(); ++i) {
      if (handled[i]) {
        continue;
      }
      std::vector<AiTaskRow> group;
      group.push_back(tasks[i]);
      handled[i] = true;
      for (size_t j = i + 1; j < tasks.size(); ++j) {
        if (!handled[j] && tasks[j].request_id_ == tasks[i].request_id_) {
          handled[j] = true;
          group.push_back(tasks[j]);
        }
      }
      process_group(group);
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("failed to fetch ai column tasks", K(ret));
  }
}

} // namespace observer
} // namespace oceanbase
