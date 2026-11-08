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

#include <new>
#include <stdio.h>
#include <string.h>

#include "common/mysqlclient/ob_mysql_proxy.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "common/mysqlclient/ob_mysql_transaction.h"
#include "lib/allocator/page_arena.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/string/ob_sql_string.h"
#include "lib/time/ob_time_utility.h"
#include "lib/utility/utility.h"
#include "share/inner_table/ob_inner_table_schema.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/schema/ob_table_schema.h"

namespace oceanbase
{
namespace omt
{

using namespace common;
using namespace common::sqlclient;
using namespace share;
using namespace share::schema;

namespace
{
constexpr int64_t AI_COLUMN_WORKER_INTERVAL_US = 100LL * 1000LL;
constexpr int64_t AI_COLUMN_TASK_BATCH_SIZE = 32;
constexpr int64_t AI_TASK_PENDING = 1;
constexpr int64_t AI_TASK_RUNNING = 2;
constexpr int64_t AI_TASK_RETRY_WAIT = 3;
constexpr int64_t AI_TASK_READY = 0;
constexpr int64_t AI_TASK_FAILED = 4;
constexpr int64_t AI_COLUMN_RETRY_DELAY_SEC = 1;

int append_literal(const ObString &value, ObSqlString &sql)
{
  return sql_append_hex_escape_str(value, sql);
}

// A model result must be inserted as a plain quoted string: a hex literal is a
// binary string and would break json/vector/int casts.
int append_result_literal(const ObString &value, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sql.append("'"))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < value.length(); ++i) {
      const char c = value.ptr()[i];
      if ('\'' == c || '\\' == c) {
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

int get_column_name(const ObTableSchema &table_schema, const uint64_t column_id, ObString &name)
{
  int ret = OB_SUCCESS;
  const ObColumnSchemaV2 *column = table_schema.get_column_schema(column_id);
  if (NULL == column) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    name = column->get_column_name_str();
  }
  return ret;
}

struct AiColumnRuntime
{
  AiColumnRuntime()
    : state_name_(), request_name_(), state_column_id_(OB_INVALID_ID),
      request_column_id_(OB_INVALID_ID), max_retries_(0), normalized_expr_()
  {}
  ObString state_name_;
  ObString request_name_;
  uint64_t state_column_id_;
  uint64_t request_column_id_;
  int64_t max_retries_;
  ObString normalized_expr_;
};

int load_column_runtime(ObMySQLProxy *sql_proxy,
                        ObIAllocator &alloc,
                        const ObTableSchema &table_schema,
                        const uint64_t table_id,
                        const uint64_t column_id,
                        AiColumnRuntime &rt)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  if (OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(sql.assign_fmt(
               "SELECT state_column_id, request_column_id, max_retries, normalized_expr "
               "FROM oceanbase.%s WHERE table_id = %lu AND column_id = %lu",
               OB_ALL_AI_COLUMN_TNAME, table_id, column_id))) {
  } else {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = NULL;
      if (OB_FAIL(sql_proxy->read(res, sql.ptr()))) {
      } else if (NULL == (result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(result->next())) {
      } else {
        int64_t state_id = 0;
        int64_t request_id = 0;
        ObString normalized;
        if (OB_FAIL(result->get_int("state_column_id", state_id))
            || OB_FAIL(result->get_int("request_column_id", request_id))
            || OB_FAIL(result->get_int("max_retries", rt.max_retries_))
            || OB_FAIL(result->get_varchar("normalized_expr", normalized))) {
        } else if (OB_FAIL(get_column_name(table_schema, static_cast<uint64_t>(state_id),
                                           rt.state_name_))
                   || OB_FAIL(get_column_name(table_schema, static_cast<uint64_t>(request_id),
                                              rt.request_name_))) {
        } else if (OB_FAIL(ob_write_string(alloc, normalized, rt.normalized_expr_))) {
        } else {
          rt.state_column_id_ = static_cast<uint64_t>(state_id);
          rt.request_column_id_ = static_cast<uint64_t>(request_id);
        }
      }
    }
  }
  return ret;
}
} // anonymous namespace

ObAiColumnWorker::ObAiColumnWorker()
  : timer_(), sql_proxy_(NULL), is_inited_(false), is_running_(false), round_(0)
{
  disable_timeout_check();
}

ObAiColumnWorker::~ObAiColumnWorker()
{
  destroy();
}

int ObAiColumnWorker::init(ObMySQLProxy &sql_proxy)
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    ret = OB_INIT_TWICE;
  } else if (OB_FAIL(timer_.init("AiColumnWorker", ObMemAttr("AiColWorker")))) {
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
  ++round_;
  if (OB_FAIL(claim_tasks(alloc, tasks))) {
  } else if (tasks.empty()) {
  } else if (OB_FAIL(process_all(alloc, tasks))) {
  }
  return ret;
}

int ObAiColumnWorker::claim_tasks(ObIAllocator &alloc, ObIArray<TaskRow> &tasks)
{
  int ret = OB_SUCCESS;
  ObSqlString select_sql;
  ObMySQLTransaction trans;
  bool started = false;
  if (OB_ISNULL(sql_proxy_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(select_sql.append_fmt(
               "SELECT table_id, column_id, request_id, rowkey_predicate, retry_count "
               "FROM %s WHERE state IN (%ld, %ld) AND eligible_ts <= now(6) "
               "ORDER BY table_id, column_id, request_id LIMIT %ld",
               OB_ALL_AI_COLUMN_TASK_TNAME, AI_TASK_PENDING, AI_TASK_RETRY_WAIT,
               AI_COLUMN_TASK_BATCH_SIZE))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (FALSE_IT(started = true)) {
  } else {
    ObISQLClient::ReadResult read_result;
    if (OB_FAIL(trans.read(read_result, select_sql.ptr()))) {
    } else if (OB_ISNULL(read_result.get_result())) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      ObMySQLResult *result = read_result.get_result();
      while (OB_SUCC(ret)) {
        TaskRow row;
        int64_t table_id = 0;
        int64_t column_id = 0;
        ObString request_id;
        ObString predicate;
        if (OB_FAIL(result->next())) {
          if (OB_ITER_END == ret) {
            ret = OB_SUCCESS;
            break;
          }
        } else if (OB_FAIL(result->get_int("table_id", table_id))
                   || OB_FAIL(result->get_int("column_id", column_id))
                   || OB_FAIL(result->get_varchar("request_id", request_id))
                   || OB_FAIL(result->get_varchar("rowkey_predicate", predicate))
                   || OB_FAIL(result->get_int("retry_count", row.retry_count_))) {
        } else if (OB_FAIL(ob_write_string(alloc, request_id, row.request_id_))
                   || OB_FAIL(ob_write_string(alloc, predicate, row.rowkey_predicate_))) {
        } else {
          row.table_id_ = static_cast<uint64_t>(table_id);
          row.column_id_ = static_cast<uint64_t>(column_id);
          if (OB_FAIL(tasks.push_back(row))) {
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
                    "UPDATE %s SET state = %ld WHERE table_id = %lu AND column_id = %lu "
                    "AND request_id = ",
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
      }
    } else {
      int tmp_ret = trans.end(false);
      LOG_WARN("failed to rollback claim tasks", K(tmp_ret));
    }
  }
  return ret;
}

int ObAiColumnWorker::process_all(ObIAllocator &alloc, ObIArray<TaskRow> &tasks)
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
      if (!handled[k] && candidate.table_id_ == first.table_id_
          && candidate.request_id_ == first.request_id_) {
        if (OB_FAIL(group.push_back(candidate))) {
        } else {
          handled[k] = true;
        }
      }
    }
    if (OB_SUCC(ret) && OB_FAIL(process_group(alloc, group))) {
    }
  }
  return ret;
}

int ObAiColumnWorker::delete_group_tasks(const uint64_t table_id, const ObString &request_id)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  ObSqlString sql;
  int64_t affected_rows = 0;
  bool started = false;
  if (OB_FAIL(sql.append_fmt("DELETE FROM %s WHERE table_id = %lu AND request_id = ",
                             OB_ALL_AI_COLUMN_TASK_TNAME, table_id))) {
  } else if (OB_FAIL(append_literal(request_id, sql))) {
  } else if (OB_FAIL(trans.start(sql_proxy_))) {
  } else if (FALSE_IT(started = true)) {
  } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
  }
  if (started) {
    if (OB_SUCC(ret)) {
      if (OB_FAIL(trans.end(true))) {
      }
    } else {
      int tmp_ret = trans.end(false);
      LOG_WARN("failed to rollback task cleanup", K(tmp_ret));
    }
  }
  return ret;
}

int ObAiColumnWorker::process_group(ObIAllocator &alloc, ObIArray<TaskRow> &group)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard guard;
  const ObTableSchema *table_schema = NULL;
  const ObDatabaseSchema *db_schema = NULL;
  ObSEArray<AiColumnRuntime *, 4> runtimes;
  ObSEArray<ObString, 4> results;
  if (group.empty()) {
  } else if (OB_ISNULL(sql_proxy_)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(guard))) {
  } else if (OB_FAIL(guard.get_table_schema(group.at(0).table_id_, table_schema))) {
  } else if (OB_ISNULL(table_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(guard.get_database_schema(table_schema->get_database_id(), db_schema))) {
  } else if (OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < group.count(); ++i) {
      void *mem = alloc.alloc(sizeof(AiColumnRuntime));
      AiColumnRuntime *rt = (NULL == mem) ? NULL : new (mem) AiColumnRuntime();
      if (OB_ISNULL(rt)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_FAIL(load_column_runtime(sql_proxy_, alloc, *table_schema,
                                             group.at(i).table_id_,
                                             group.at(i).column_id_, *rt))) {
      } else if (OB_FAIL(runtimes.push_back(rt))) {
      }
    }
  }

  // 1. stale check: the request id stored on the row must still match
  bool stale = false;
  if (OB_SUCC(ret)) {
    const ObString &db_name = db_schema->get_database_name_str();
    const ObString &table_name = table_schema->get_table_name_str();
    const ObString &predicate = group.at(0).rowkey_predicate_;
    for (int64_t i = 0; OB_SUCC(ret) && !stale && i < runtimes.count(); ++i) {
      ObSqlString sql;
      if (OB_FAIL(sql.append_fmt("SELECT `%.*s` FROM `%.*s`.`%.*s` WHERE %.*s",
                                 runtimes.at(i)->request_name_.length(),
                                 runtimes.at(i)->request_name_.ptr(),
                                 db_name.length(), db_name.ptr(),
                                 table_name.length(), table_name.ptr(),
                                 predicate.length(), predicate.ptr()))) {
      } else {
        SMART_VAR(ObMySQLProxy::MySQLResult, res) {
          ObMySQLResult *result = NULL;
          if (OB_FAIL(sql_proxy_->read(res, sql.ptr()))) {
          } else if (NULL == (result = res.get_result())) {
            ret = OB_ERR_UNEXPECTED;
          } else if (OB_ITER_END == (ret = result->next())) {
            ret = OB_SUCCESS;
            stale = true;
          } else if (OB_FAIL(ret)) {
          } else {
            ObString current_request;
            if (OB_FAIL(result->get_varchar(static_cast<int64_t>(0), current_request))) {
            } else if (!current_request.empty()
                       && current_request != group.at(i).request_id_) {
              stale = true;
            }
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && stale) {
    ret = delete_group_tasks(group.at(0).table_id_, group.at(0).request_id_);
  }

  // 2. call the model for every column of the group
  bool failed = false;
  if (OB_SUCC(ret) && !stale) {
    const ObString &db_name = db_schema->get_database_name_str();
    const ObString &table_name = table_schema->get_table_name_str();
    const ObString &predicate = group.at(0).rowkey_predicate_;
    for (int64_t i = 0; OB_SUCC(ret) && !failed && i < runtimes.count(); ++i) {
      ObSqlString sql;
      if (OB_FAIL(sql.append_fmt("SELECT %.*s FROM `%.*s`.`%.*s` WHERE %.*s",
                                 runtimes.at(i)->normalized_expr_.length(),
                                 runtimes.at(i)->normalized_expr_.ptr(),
                                 db_name.length(), db_name.ptr(),
                                 table_name.length(), table_name.ptr(),
                                 predicate.length(), predicate.ptr()))) {
      } else {
        SMART_VAR(ObMySQLProxy::MySQLResult, res) {
          ObMySQLResult *result = NULL;
          if (OB_FAIL(sql_proxy_->read(res, sql.ptr()))) {
            failed = true;
            ret = OB_SUCCESS;
          } else if (NULL == (result = res.get_result())) {
            failed = true;
            ret = OB_SUCCESS;
          } else if (OB_FAIL(result->next())) {
            failed = true;
            ret = OB_SUCCESS;
          } else {
            ObString value;
            if (OB_FAIL(result->get_varchar(static_cast<int64_t>(0), value)) || value.empty()) {
              failed = true;
              ret = OB_SUCCESS;
            } else {
              ObString copy;
              if (OB_FAIL(ob_write_string(alloc, value, copy))) {
              } else if (OB_FAIL(results.push_back(copy))) {
              }
            }
          }
        }
      }
    }
  }

  // 3. retry or publish
  if (OB_SUCC(ret) && !stale && failed) {
    bool can_retry = false;
    for (int64_t i = 0; !can_retry && i < runtimes.count() && i < group.count(); ++i) {
      if (group.at(i).retry_count_ < runtimes.at(i)->max_retries_) {
        can_retry = true;
      }
    }
    if (can_retry) {
      ObMySQLTransaction trans;
      ObSqlString sql;
      int64_t affected_rows = 0;
      bool started = false;
      if (OB_FAIL(sql.append_fmt(
                    "UPDATE %s SET state = %ld, retry_count = retry_count + 1, "
                    "eligible_ts = date_add(now(6), interval %ld second) "
                    "WHERE table_id = %lu AND request_id = ",
                    OB_ALL_AI_COLUMN_TASK_TNAME, AI_TASK_RETRY_WAIT,
                    AI_COLUMN_RETRY_DELAY_SEC, group.at(0).table_id_))) {
      } else if (OB_FAIL(append_literal(group.at(0).request_id_, sql))) {
      } else if (OB_FAIL(trans.start(sql_proxy_))) {
      } else if (FALSE_IT(started = true)) {
      } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
      }
      if (started) {
        if (OB_SUCC(ret)) {
          if (OB_FAIL(trans.end(true))) {
          }
        } else {
          int tmp_ret = trans.end(false);
          LOG_WARN("failed to rollback retry task", K(tmp_ret));
        }
      }
    } else {
      // terminal failure: mark the AI columns FAILED and drop the task rows.
      ObMySQLTransaction trans;
      bool started = false;
      const ObString &db_name = db_schema->get_database_name_str();
      const ObString &table_name = table_schema->get_table_name_str();
      const ObString &predicate = group.at(0).rowkey_predicate_;
      if (OB_FAIL(trans.start(sql_proxy_))) {
      } else if (FALSE_IT(started = true)) {
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < runtimes.count(); ++i) {
          ObSqlString sql;
          int64_t affected_rows = 0;
          const AiColumnRuntime *rt = runtimes.at(i);
          if (OB_FAIL(sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = %ld WHERE %.*s AND `%.*s` = ",
                                     db_name.length(), db_name.ptr(),
                                     table_name.length(), table_name.ptr(),
                                     rt->state_name_.length(), rt->state_name_.ptr(),
                                     AI_TASK_FAILED,
                                     predicate.length(), predicate.ptr(),
                                     rt->request_name_.length(), rt->request_name_.ptr()))) {
          } else if (OB_FAIL(append_literal(group.at(i).request_id_, sql))) {
          } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
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
          }
        } else {
          int tmp_ret = trans.end(false);
          LOG_WARN("failed to rollback failed ai column group", K(tmp_ret));
        }
      }
    }
  } else if (OB_SUCC(ret) && !stale && !failed) {
    ObMySQLTransaction trans;
    bool started = false;
    const ObString &db_name = db_schema->get_database_name_str();
    const ObString &table_name = table_schema->get_table_name_str();
    const ObString &predicate = group.at(0).rowkey_predicate_;
    if (results.count() != runtimes.count()) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(trans.start(sql_proxy_))) {
    } else if (FALSE_IT(started = true)) {
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < runtimes.count(); ++i) {
        ObSqlString sql;
        int64_t affected_rows = 0;
        const AiColumnRuntime *rt = runtimes.at(i);
        if (OB_FAIL(sql.append_fmt("UPDATE `%.*s`.`%.*s` SET `%.*s` = ",
                                   db_name.length(), db_name.ptr(),
                                   table_name.length(), table_name.ptr(),
                                   table_schema->get_column_schema(group.at(i).column_id_)->
                                     get_column_name_str().length(),
                                   table_schema->get_column_schema(group.at(i).column_id_)->
                                     get_column_name_str().ptr()))) {
        } else if (OB_FAIL(append_result_literal(results.at(i), sql))) {
        } else if (OB_FAIL(sql.append_fmt(", `%.*s` = %ld WHERE %.*s AND `%.*s` = ",
                                          rt->state_name_.length(), rt->state_name_.ptr(),
                                          AI_TASK_READY,
                                          predicate.length(), predicate.ptr(),
                                          rt->request_name_.length(), rt->request_name_.ptr()))) {
        } else if (OB_FAIL(append_literal(group.at(i).request_id_, sql))) {
        } else if (OB_FAIL(trans.write(sql.ptr(), affected_rows))) {
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
        }
      } else {
        int tmp_ret = trans.end(false);
        LOG_WARN("failed to rollback publish tasks", K(tmp_ret));
      }
    }
  }
  return ret;
}

} // namespace omt
} // namespace oceanbase
