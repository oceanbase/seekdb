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

#include "observer/ai_column/ob_ai_column_worker.h"

#include "common/mysqlclient/ob_mysql_transaction.h"
#include "common/mysqlclient/ob_mysql_result.h"
#include "lib/string/ob_sql_string.h"
#include "observer/ai_service/ob_ai_service_proxy.h"
#include "share/ob_share_util.h"
#include "share/inner_table/ob_inner_table_schema_constants.h"
#include "share/ob_server_struct.h"
#include "share/schema/ob_schema_service.h"
#include "share/schema/ob_multi_version_schema_service.h"
#include "share/schema/ob_schema_getter_guard.h"
#include "share/ai_service/ob_ai_service_struct.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_client.h"
#include "sql/engine/expr/ob_expr_ai/ob_ai_func_utils.h"
#include "observer/omt/ob_ai_service.h"
#include "observer/ob_server.h"
#include "observer/ob_inner_sql_connection.h"
#include "sql/executor/ob_worker_session_guard.h"
#include "sql/session/ob_sql_session_mgr.h"
#include "query/session/ob_free_session_ctx.h"
#include "share/rc/ob_server_runtime.h"

#define USING_LOG_PREFIX SHARE

namespace oceanbase
{
namespace share
{

using namespace common;

static const int64_t WORKER_SQL_TIMEOUT_US = 60L * 1000L * 1000L;

// run write statements on a dedicated job session, replicating the
// dbms_scheduler background execution pattern (fresh session +
// ObWorkerSessionGuard + SPI connection). plain proxy writes from a timer
// thread fail with -4016 on user tables / do not persist in transactions.
static int exec_sqls_in_job_session(ObIArray<ObString> &sqls)
{
  int ret = OB_SUCCESS;
  ObFreeSessionCtx free_session_ctx;
  sql::ObSQLSessionInfo *session_info = nullptr;
  sqlclient::ObISQLConnectionGuard conn_guard;
  schema::ObSchemaGetterGuard schema_guard;
  const schema::ObUserInfo *user_info = nullptr;
  const schema::ObDatabaseSchema *db_schema = nullptr;
  ObInnerSQLConnection *conn = nullptr;
  int64_t affected_rows = 0;
  THIS_WORKER.set_timeout_ts(INT64_MAX);
  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
  } else if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(
                 schema_guard))) {
  } else if (OB_FAIL(schema_guard.get_database_schema(OB_SYS_DATABASE_NAME, db_schema))
             || OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ai worker cannot resolve sys database schema", K(ret));
  }
  // pick a privileged user: root first, fall back to whoever exists
  if (OB_SUCC(ret)) {
    ObSEArray<const schema::ObUserInfo *, 2> users;
    if (OB_FAIL(schema_guard.get_user_info(ObString("root"), users)) || users.empty()) {
      users.reset();
      if (OB_FAIL(schema_guard.get_user_info(ObString("admin"), users))) {
        LOG_WARN("ai worker cannot resolve any job user", K(ret));
      }
    }
    if (OB_SUCC(ret)) {
      if (users.empty()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ai worker found no usable job user", K(ret));
      } else {
        user_info = users.at(0);
      }
    }
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(OBSERVER.get_sql_session_mgr().create_sessid(free_session_ctx.sessid_))) {
  } else if (OB_FAIL(OBSERVER.get_sql_session_mgr().create_session(
                 free_session_ctx.sessid_, session_info))) {
    session_info = nullptr;
  } else if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    sql::ObWorkerSessionGuard worker_guard(session_info);
    ObPCMemPctConf pc_mem_conf;
    ObPrivSet db_priv_set = OB_PRIV_SET_EMPTY;
    session_info->set_inner_session();
    if (OB_FAIL(session_info->load_default_sys_variable(true, true))) {
    } else if (OB_FAIL(session_info->update_max_packet_size())) {
    } else if (OB_FAIL(session_info->init_runtime(OB_SERVER_RUNTIME_NAME))) {
    } else if (OB_FAIL(session_info->load_all_sys_vars(schema_guard))) {
    } else if (OB_FAIL(session_info->set_default_database(OB_SYS_DATABASE_NAME))) {
    } else if (OB_FAIL(session_info->get_pc_mem_conf(pc_mem_conf))) {
    } else {
      session_info->set_database_id(db_schema->get_database_id());
      if (OB_FAIL(session_info->set_user(user_info->get_user_name(),
                                         user_info->get_host_name_str(),
                                         user_info->get_user_id()))) {
      } else {
        session_info->set_priv_user_id(user_info->get_user_id());
        session_info->set_user_priv_set(user_info->get_priv_set());
        if (OB_FAIL(schema_guard.get_db_priv_set(user_info->get_user_id(),
                                                 OB_SYS_DATABASE_NAME, db_priv_set))) {
        } else {
          session_info->set_db_priv_set(db_priv_set);
          session_info->get_enable_role_array().reuse();
        }
      }
    }
    if (OB_FAIL(ret)) {
      LOG_WARN("ai worker fail to init job session", K(ret));
    } else if (OB_FAIL(ObInnerSQLConnection::create_spi_connection_with_external_session(
                   session_info, conn_guard))) {
      LOG_WARN("ai worker fail to create spi connection", K(ret));
    } else if (OB_ISNULL(conn = static_cast<ObInnerSQLConnection *>(conn_guard.get_ptr()))) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < sqls.count(); ++i) {
        // ObString is length-delimited and is not necessarily NUL-terminated.
        // The SQL executor accepts a C string, so materialize a terminated copy.
        ObSqlString terminated_sql;
        if (OB_FAIL(terminated_sql.assign(sqls.at(i)))) {
        } else if (OB_FAIL(conn->execute_write(terminated_sql.ptr(), affected_rows))) {
          LOG_WARN("ai worker job session write failed", K(ret), K(sqls.at(i)));
        }
      }
    }
  }
  conn = nullptr;
  conn_guard.reset();
  if (nullptr != session_info) {
    session_info->set_session_sleep();
    ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->revert_session(
        session_info);
    ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->free_session(
        free_session_ctx);
    session_info = nullptr;
  }
  return ret;
}

// one task row from the scan (strings are deep-copied into the round arena)
struct ObAiWorkerTask
{
  ObAiWorkerTask()
      : table_id_(OB_INVALID_ID),
        column_id_(OB_INVALID_ID),
        state_(0),
        retry_count_(0),
        max_retries_(0),
        meta_idx_(-1),
        request_ok_(false)
  {}
  ObString request_id_;
  uint64_t table_id_;
  ObString rowkey_pred_;
  uint64_t column_id_;
  ObString column_name_;
  int64_t state_;
  int64_t retry_count_;
  int64_t max_retries_;
  int64_t meta_idx_;
  bool request_ok_;
  ObString result_value_;
};

// per-table info used inside one processing round
struct ObAiWorkerTableInfo
{
  ObAiWorkerTableInfo() : table_id_(OB_INVALID_ID) {}
  uint64_t table_id_;
  ObString db_name_;
  ObString table_name_;
  ObArenaAllocator allocator_;
  ObSEArray<ObAiColumnMeta, 2> metas_;
};


// writes from a timer thread through the plain sql_proxy_ are not
// persisted in this build; the platform's designated channel for
// background-thread inner writes is the ddl proxy (as in rootserver tasks)
static common::ObMySQLProxy *worker_proxy()
{
  return OB_NOT_NULL(GCTX.ddl_sql_proxy_) ? GCTX.ddl_sql_proxy_ : GCTX.sql_proxy_;
}
static int append_sql_escaped(ObStringBuffer &buf, const ObString &src)
{
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < src.length(); ++i) {
    char c = src.ptr()[i];
    if ('\'' == c) {
      ret = buf.append("''");
    } else {
      char one[2] = {c, '\0'};
      ret = buf.append(one);
    }
  }
  return ret;
}

static int sql_quote_escaped(ObIAllocator &allocator, const ObString &src, ObString &dst)
{
  int ret = OB_SUCCESS;
  ObStringBuffer buf(&allocator);
  if (OB_FAIL(buf.append("'"))) {
  } else if (OB_FAIL(append_sql_escaped(buf, src))) {
  } else if (OB_FAIL(buf.append("'"))) {
  } else {
    dst = buf.string();
  }
  return ret;
}

ObAiColumnWorker &ObAiColumnWorker::get_instance()
{
  static ObAiColumnWorker instance;
  return instance;
}

int ObAiColumnWorker::ensure_started()
{
  int ret = OB_SUCCESS;
  if (is_started_) {
    // already running
  } else if (OB_FAIL(timer_.init("AiColWorker", ObMemAttr("AiColWorker")))) {
    LOG_WARN("fail to init ai column worker timer", K(ret));
  } else if (OB_FAIL(timer_.start())) {
    LOG_WARN("fail to start ai column worker timer", K(ret));
  } else if (OB_FAIL(timer_.schedule(*this, SCAN_PERIOD_US, true))) {
    LOG_WARN("fail to schedule ai column worker", K(ret));
  } else {
    is_started_ = true;
    LOG_INFO("ai column worker started");
  }
  return ret;
}

void ObAiColumnWorker::destroy()
{
  if (timer_.inited()) {
    timer_.stop();
  }
}

void ObAiColumnWorker::runTimerTask()
{
  if (is_started_ && !ATOMIC_LOAD(&busy_)) {
    ATOMIC_STORE(&busy_, true);
    int ret = process_round();
    if (OB_FAIL(ret)) {
      LOG_WARN("ai column worker round failed", K(ret));
    }
    ATOMIC_STORE(&busy_, false);
  }
}

static int load_table_info(uint64_t table_id, ObAiWorkerTableInfo &info)
{
  int ret = OB_SUCCESS;
  schema::ObSchemaGetterGuard guard;
  const schema::ObTableSchema *table_schema = nullptr;
  const schema::ObDatabaseSchema *db_schema = nullptr;
  info.table_id_ = table_id;
  if (OB_FAIL(ObMultiVersionSchemaService::get_instance().get_runtime_schema_guard(guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(guard.get_table_schema(table_id, table_schema)) || OB_ISNULL(table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("ai column business table not found", K(ret), K(table_id));
  } else if (OB_FAIL(ob_write_string(info.allocator_, table_schema->get_table_name(),
                                     info.table_name_))) {
  } else if (OB_FAIL(guard.get_database_schema(table_schema->get_database_id(), db_schema))
             || OB_ISNULL(db_schema)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ai column business database not found", K(ret));
  } else if (OB_FAIL(ob_write_string(info.allocator_, db_schema->get_database_name(),
                                     info.db_name_))) {
  } else if (OB_FAIL(ObAiColumnUtil::select_metas_by_table(table_id, info.allocator_, info.metas_))) {
    LOG_WARN("fail to load ai metas in worker", K(ret), K(table_id));
  }
  return ret;
}

// read the dependency columns of one task's row and render the prompt
static int eval_task_prompt(ObAiWorkerTableInfo &info, const ObAiColumnMeta &meta,
                            const ObAiWorkerTask &task,
                            ObString &user_text, bool &row_exists)
{
  int ret = OB_SUCCESS;
  ObSqlString sql;
  ObTimeoutCtx timeout_ctx;
  row_exists = false;
  if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(timeout_ctx, WORKER_SQL_TIMEOUT_US))) {
  } else if (meta.expr_.arg_columns_.empty()) {
    user_text = meta.expr_.user_template_;
    row_exists = true;
  } else if (OB_FAIL(sql.assign_fmt("SELECT "))) {
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < meta.expr_.arg_columns_.count(); ++i) {
      if (OB_FAIL(sql.append_fmt("%s`%.*s` AS c%ld", i == 0 ? "" : ",",
                                 static_cast<int>(meta.expr_.arg_columns_.at(i).length()),
                                 meta.expr_.arg_columns_.at(i).ptr(), i))) {
      }
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(sql.append_fmt(" FROM `%.*s`.`%.*s` WHERE %.*s",
                  static_cast<int>(info.db_name_.length()), info.db_name_.ptr(),
                  static_cast<int>(info.table_name_.length()), info.table_name_.ptr(),
                  static_cast<int>(task.rowkey_pred_.length()), task.rowkey_pred_.ptr()))) {
      }
    }
  }
  if (OB_SUCC(ret)) {
    SMART_VAR(ObMySQLProxy::MySQLResult, res) {
      ObMySQLResult *result = nullptr;
      if (OB_FAIL(worker_proxy()->read(res, sql.ptr()))) {
        LOG_WARN("worker fail to read business row", K(ret), K(sql));
      } else if (OB_ISNULL(result = res.get_result())) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_ITER_END == result->next()) {
        // row gone: task is stale
        row_exists = false;
        ret = OB_SUCCESS;
      } else if (OB_FAIL(ret)) {
      } else {
        row_exists = true;
        ObStringBuffer buf(&(info.allocator_));
        const ObString &tmpl = meta.expr_.user_template_;
        for (int64_t i = 0; OB_SUCC(ret) && i < tmpl.length(); ++i) {
          char c = tmpl.ptr()[i];
          // "{N}" placeholder: { + digit + }
          if ('{' == c && i + 2 < tmpl.length() && tmpl.ptr()[i + 1] >= '0'
              && tmpl.ptr()[i + 1] <= '9'
              && '}' == tmpl.ptr()[i + 2]) {
            int64_t idx = tmpl.ptr()[i + 1] - '0';
            if (idx >= meta.expr_.arg_columns_.count()) {
              ret = OB_ERR_UNEXPECTED;
            } else {
              char col_alias[16];
              snprintf(col_alias, sizeof(col_alias), "c%ld", idx);
              ObString raw;
              if (OB_FAIL(result->get_varchar(col_alias, raw))) {
                LOG_WARN("fail to get dep column value", K(ret), K(col_alias));
              } else if (OB_FAIL(buf.append(raw))) {
              }
              i += 2;  // skip the digit and the closing brace
            }
          } else {
            char one[2] = {c, '\0'};
            if (OB_FAIL(buf.append(one))) {
            }
          }
        }
        if (OB_SUCC(ret)) {
          user_text = buf.string();
        }
      }
    }
  }
  return ret;
}

int ObAiColumnWorker::process_round()
{
  int ret = OB_SUCCESS;
  int64_t now_us = ObTimeUtility::current_time();
  int64_t affected_rows = 0;
  ObArenaAllocator arena("AiColWorker");
  ObSEArray<ObAiWorkerTask, 16> rows;
  ObSEArray<int64_t, 8> group_heads;
  // stale-task deletes collected during the round, run via the job session
  ObSEArray<ObString, 8> stale_del_sqls;
  if (OB_ISNULL(worker_proxy())) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ObSqlString scan_sql;
    if (OB_FAIL(scan_sql.assign_fmt(
            "SELECT request_id, table_id, rowkey_predicate, column_id, column_name,"
            " state, retry_count FROM %s WHERE state IN (1, 3) AND eligible_ts <= %ld"
            " LIMIT %ld",
            OB_ALL_AI_COLUMN_TASK_TNAME, now_us,
            static_cast<int64_t>(MAX_TASKS_PER_ROUND)))) {
    } else {
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ObMySQLResult *result = nullptr;
        if (OB_FAIL(worker_proxy()->read(res, scan_sql.ptr()))) {
          LOG_WARN("worker scan failed", K(ret), K(scan_sql));
        } else if (OB_ISNULL(result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
        } else {
          int tmp_ret = OB_SUCCESS;
          while (OB_SUCCESS == (tmp_ret = result->next())) {
            ObAiWorkerTask row;
            ObString req, pred, cname;
            int64_t tid = 0, cid = 0, state = 0, retry = 0;
            EXTRACT_INT_FIELD_MYSQL(*result, "table_id", tid, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "column_id", cid, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "state", state, int64_t);
            EXTRACT_INT_FIELD_MYSQL(*result, "retry_count", retry, int64_t);
            EXTRACT_VARCHAR_FIELD_MYSQL(*result, "request_id", req);
            EXTRACT_VARCHAR_FIELD_MYSQL(*result, "rowkey_predicate", pred);
            EXTRACT_VARCHAR_FIELD_MYSQL(*result, "column_name", cname);
            if (OB_SUCC(ret)) {
              if (OB_FAIL(ob_write_string(arena, req, row.request_id_))
                  || OB_FAIL(ob_write_string(arena, pred, row.rowkey_pred_))
                  || OB_FAIL(ob_write_string(arena, cname, row.column_name_))) {
              } else {
                row.table_id_ = tid;
                row.column_id_ = cid;
                row.state_ = state;
                row.retry_count_ = retry;
                if (OB_FAIL(rows.push_back(row))) {
                }
              }
            }
          }
          if (OB_ITER_END != tmp_ret) {
            ret = tmp_ret;
          }
        }
      }
    }
  }

  // group rows by request_id; group_heads stores the index of the first member
  if (OB_SUCC(ret)) {
    for (int64_t i = 0; OB_SUCC(ret) && i < rows.count(); ++i) {
      bool found = false;
      for (int64_t h = 0; !found && h < group_heads.count(); ++h) {
        if (0 == rows.at(group_heads.at(h)).request_id_.compare(rows.at(i).request_id_)) {
          found = true;
        }
      }
      if (!found && OB_FAIL(group_heads.push_back(i))) {
      }
    }
  }

  for (int64_t g = 0; OB_SUCC(ret) && g < group_heads.count(); ++g) {
    int64_t head = group_heads.at(g);
    uint64_t table_id = rows.at(head).table_id_;
    // indices of the group members
    ObSEArray<int64_t, 4> members;
    for (int64_t i = head; OB_SUCC(ret) && i < rows.count(); ++i) {
      if (0 == rows.at(i).request_id_.compare(rows.at(head).request_id_)) {
        if (OB_FAIL(members.push_back(i))) {
        }
      }
    }
    if (OB_FAIL(ret) || members.empty()) {
      continue;
    }
    ObAiWorkerTableInfo info;
    if (OB_FAIL(load_table_info(table_id, info))) {
      // table gone: drop the whole group
      LOG_WARN("worker cannot load table info, drop group", K(ret), K(table_id));
      ret = OB_SUCCESS;
      for (int64_t i = 0; OB_SUCC(ret) && i < members.count(); ++i) {
        ObAiWorkerTask &tr = rows.at(members.at(i));
        ObSqlString del_sql;
        ObStringBuffer pb(&arena), qb(&arena);
        if (OB_FAIL(append_sql_escaped(pb, tr.rowkey_pred_))) {
        } else if (OB_FAIL(append_sql_escaped(qb, tr.request_id_))) {
        } else if (OB_FAIL(del_sql.assign_fmt(
                "DELETE FROM %s WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                " AND request_id = '%.*s'",
                OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                static_cast<int>(pb.length()), pb.ptr(),
                static_cast<int>(qb.length()), qb.ptr()))) {
        ObString del_copy;
        if (OB_FAIL(ob_write_string(arena, del_sql.string(), del_copy))) {
        } else if (OB_FAIL(stale_del_sqls.push_back(del_copy))) {
        }
        }
      }
      continue;
    }
    // resolve meta per task
    for (int64_t i = 0; OB_SUCC(ret) && i < members.count(); ++i) {
      ObAiWorkerTask &tr = rows.at(members.at(i));
      tr.meta_idx_ = -1;
      for (int64_t m = 0; m < info.metas_.count(); ++m) {
        if (info.metas_.at(m).column_id_ == tr.column_id_
            || (tr.column_name_.length() > 0
                && 0 == info.metas_.at(m).column_name_.case_compare(tr.column_name_))) {
          tr.meta_idx_ = m;
          tr.max_retries_ = info.metas_.at(m).max_retries_;
          break;
        }
      }
    }
    if (rows.at(head).meta_idx_ < 0) {
      // metas gone (column dropped): drop the group
      for (int64_t i = 0; OB_SUCC(ret) && i < members.count(); ++i) {
        ObAiWorkerTask &tr = rows.at(members.at(i));
        ObSqlString del_sql;
        ObStringBuffer pb(&arena), qb(&arena);
        if (OB_FAIL(append_sql_escaped(pb, tr.rowkey_pred_))) {
        } else if (OB_FAIL(append_sql_escaped(qb, tr.request_id_))) {
        } else if (OB_FAIL(del_sql.assign_fmt(
                "DELETE FROM %s WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                " AND request_id = '%.*s'",
                OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                static_cast<int>(pb.length()), pb.ptr(),
                static_cast<int>(qb.length()), qb.ptr()))) {
        ObString del_copy;
        if (OB_FAIL(ob_write_string(arena, del_sql.string(), del_copy))) {
        } else if (OB_FAIL(stale_del_sqls.push_back(del_copy))) {
        }
        }
      }
      continue;
    }
    // resolve endpoint once per group
    omt::ObAiService *ai_service =
        ::oceanbase::share::server_service<::oceanbase::omt::ObAiService>();
    const ObAiModelEndpointInfo *endpoint = nullptr;
    omt::ObAiServiceGuard ai_guard;
    ObString request_model_name;
    ObString access_key;
    ObString url;
    ObString provider;
    ObAIFuncExprInfo *ai_func_info = nullptr;
    const ObAiColumnMeta &head_meta = info.metas_.at(rows.at(head).meta_idx_);
    if (OB_ISNULL(ai_service)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(ai_service->get_ai_service_guard(ai_guard))) {
    } else if (OB_FAIL(ai_guard.get_ai_endpoint_by_ai_model_name(
                   head_meta.expr_.model_, endpoint, false /*need_check*/))) {
      LOG_WARN("worker fail to resolve endpoint", K(ret));
    } else if (OB_ISNULL(endpoint)) {
      ret = OB_AI_FUNC_ENDPOINT_NOT_FOUND;
      LOG_WARN("endpoint not found for ai column worker", K(ret));
    } else if (OB_FAIL(endpoint->get_unencrypted_access_key(arena, access_key))) {
    } else if (OB_FAIL(ob_write_string(arena, endpoint->get_url(), url, true))) {
    } else if (OB_FAIL(ob_write_string(arena, endpoint->get_provider(), provider))) {
    } else if (!endpoint->get_request_model_name().empty()) {
      request_model_name = endpoint->get_request_model_name();
    } else if (OB_FAIL(ObAIFuncUtils::get_ai_func_info(arena, head_meta.expr_.model_,
                                                       ai_func_info))
               || OB_ISNULL(ai_func_info)) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      request_model_name = ai_func_info->model_;
    }

    ObArray<ObString> headers;
    if (OB_SUCC(ret)) {
      ObAIFuncIComplete *complete_provider = nullptr;
      ObAIFuncIEmbed *embed_provider = nullptr;
      if (head_meta.expr_.is_embed_) {
        if (OB_FAIL(ObAIFuncUtils::get_embed_provider(arena, provider, embed_provider))) {
        } else if (OB_FAIL(embed_provider->get_header(arena, access_key, headers))) {
        }
      } else {
        if (OB_FAIL(ObAIFuncUtils::get_complete_provider(arena, provider, complete_provider))) {
        } else if (OB_FAIL(complete_provider->get_header(arena, access_key, headers))) {
        }
      }
    }

    if (OB_SUCC(ret)) {
      ObAIFuncClient client;
      ObSEArray<int64_t, 4> ok_tasks;
      ObSEArray<int64_t, 4> fail_tasks;
      for (int64_t i = 0; OB_SUCC(ret) && i < members.count(); ++i) {
        ObAiWorkerTask &tr = rows.at(members.at(i));
        if (tr.meta_idx_ < 0) {
          continue;
        }
        const ObAiColumnMeta &meta = info.metas_.at(tr.meta_idx_);
        ObString user_text;
        bool row_exists = false;
        if (OB_FAIL(eval_task_prompt(info, meta, tr, user_text, row_exists))) {
          LOG_WARN("worker fail to eval prompt", K(ret), K(tr.column_id_));
        }
        if (OB_SUCC(ret) && !row_exists) {
          // stale task (row deleted): drop it
          ObSqlString del_sql;
          ObStringBuffer pb(&arena), qb(&arena);
          if (OB_FAIL(append_sql_escaped(pb, tr.rowkey_pred_))) {
          } else if (OB_FAIL(append_sql_escaped(qb, tr.request_id_))) {
          } else if (OB_FAIL(del_sql.assign_fmt(
                  "DELETE FROM %s WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                  " AND column_id = %lu AND request_id = '%.*s'",
                  OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                  static_cast<int>(pb.length()), pb.ptr(),
                  tr.column_id_,
                  static_cast<int>(qb.length()), qb.ptr()))) {
          } else {
            ObString del_copy;
            if (OB_FAIL(ob_write_string(arena, del_sql.string(), del_copy))) {
            } else if (OB_FAIL(stale_del_sqls.push_back(del_copy))) {
            }
          }
        } else if (OB_SUCC(ret)) {
          ObJsonObject *body = nullptr;
          ObJsonObject *response = nullptr;
          ObString sys_prompt_nc = meta.expr_.system_prompt_;
          ObString user_text_nc = user_text;
          ObAIFuncIComplete *complete_provider = nullptr;
          ObAIFuncIEmbed *embed_provider = nullptr;
          ObIJsonBase *result_base = nullptr;
          bool ok = false;
          ObString content_str;
          int http_ret = OB_SUCCESS;
          if (meta.expr_.is_embed_) {
            if (OB_FAIL(ObAIFuncUtils::get_embed_provider(arena, provider, embed_provider))) {
            } else {
              ObArray<ObString> contents;
              if (OB_FAIL(contents.push_back(user_text))) {
              } else if (OB_FAIL(embed_provider->get_body(arena, request_model_name, contents,
                                                          nullptr, body))) {
              } else if (OB_FAIL(http_ret = client.send_post(arena, url, headers, body, response))) {
              } else if (OB_FAIL(embed_provider->parse_output(arena, response, result_base))) {
              } else if (OB_ISNULL(result_base)) {
                http_ret = OB_ERR_UNEXPECTED;
              } else {
                // parse_output wraps vectors in an outer array; the vector
                // literal to store is the single inner array
                ObJsonArray *outer = static_cast<ObJsonArray *>(result_base);
                ObIJsonBase *inner = outer->element_count() > 0 ? outer->get_value(0) : nullptr;
                if (OB_ISNULL(inner)) {
                  http_ret = OB_ERR_UNEXPECTED;
                } else {
                  ObStringBuffer rb(&arena);
                  if (OB_FAIL(inner->print(rb, 0))) {
                  } else {
                    content_str = rb.string();
                    ok = true;
                  }
                }
              }
            }
          } else {
            if (OB_FAIL(ObAIFuncUtils::get_complete_provider(arena, provider, complete_provider))) {
            } else if (OB_FAIL(complete_provider->get_body(arena, request_model_name,
                                                           sys_prompt_nc,
                                                           user_text_nc, nullptr, body))) {
            } else if (OB_FAIL(http_ret = client.send_post(arena, url, headers, body, response))) {
            } else if (OB_FAIL(complete_provider->parse_output(arena, response, result_base))) {
            } else if (OB_ISNULL(result_base)) {
              http_ret = OB_ERR_UNEXPECTED;
            } else {
              ObStringBuffer rb(&arena);
              if (OB_FAIL(result_base->print(rb, 0))) {
              } else {
                content_str = rb.string();
                ok = true;
              }
            }
          }
          if (OB_FAIL(ret)) {
            // local error: treat as request failure and continue with other tasks
            ret = OB_SUCCESS;
          }
          if (ok) {
            ObStringBuffer lb(&arena);
            if (OB_FAIL(lb.append("'"))) {
            } else if (OB_FAIL(append_sql_escaped(lb, content_str))) {
            } else if (OB_FAIL(lb.append("'"))) {
            } else if (OB_FAIL(ob_write_string(arena, lb.string(), tr.result_value_))) {
            } else {
              tr.request_ok_ = true;
              if (OB_FAIL(ok_tasks.push_back(members.at(i)))) {
              }
            }
          } else {
            tr.request_ok_ = false;
            if (OB_FAIL(fail_tasks.push_back(members.at(i)))) {
            }
          }
        }
      }

      // write results on a dedicated job session (dbms_scheduler background
      // pattern): staleness-check each task, then ONE merged UPDATE per
      // distinct rowkey (all columns of the row at once, atomically) followed
      // by the task DELETEs
      if (OB_SUCC(ret) && ok_tasks.count() > 0) {
        ObSEArray<ObString, 8> write_sqls;
        // pass A: staleness check on the plain proxy (reads work from timer threads)
        for (int64_t i = 0; OB_SUCC(ret) && i < ok_tasks.count(); ++i) {
          ObAiWorkerTask &tr = rows.at(ok_tasks.at(i));
          ObStringBuffer pb(&arena), qb(&arena);
          if (OB_FAIL(append_sql_escaped(pb, tr.rowkey_pred_))) {
          } else if (OB_FAIL(append_sql_escaped(qb, tr.request_id_))) {
          } else {
            ObSqlString chk_sql;
            if (OB_FAIL(chk_sql.assign_fmt(
                    "SELECT column_id FROM %s WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                    " AND column_id = %lu AND request_id = '%.*s'",
                    OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                    static_cast<int>(pb.length()), pb.ptr(),
                    tr.column_id_,
                    static_cast<int>(qb.length()), qb.ptr()))) {
            } else {
              SMART_VAR(ObMySQLProxy::MySQLResult, cres) {
                ObMySQLResult *cresult = nullptr;
                bool alive = false;
                if (OB_FAIL(worker_proxy()->read(cres, chk_sql.ptr()))) {
                  ret = OB_SUCCESS;
                } else if (OB_ISNULL(cresult = cres.get_result())) {
                  ret = OB_ERR_UNEXPECTED;
                } else {
                  int tmp = OB_SUCCESS;
                  while (OB_SUCCESS == (tmp = cresult->next())) {
                    alive = true;
                  }
                  if (OB_ITER_END != tmp && OB_SUCCESS != tmp) {
                    ret = tmp;
                  }
                }
                tr.request_ok_ = alive;
              }
            }
          }
        }
        // pass B: one DELETE per alive task, built now
        for (int64_t i = 0; OB_SUCC(ret) && i < ok_tasks.count(); ++i) {
          ObAiWorkerTask &tr = rows.at(ok_tasks.at(i));
          if (!tr.request_ok_) {
            continue;
          }
          ObStringBuffer pb(&arena), qb(&arena);
          ObSqlString del_sql;
          ObString del_copy;
          if (OB_FAIL(append_sql_escaped(pb, tr.rowkey_pred_))) {
          } else if (OB_FAIL(append_sql_escaped(qb, tr.request_id_))) {
          } else if (OB_FAIL(del_sql.assign_fmt(
                  "DELETE FROM %s WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                  " AND column_id = %lu AND request_id = '%.*s'",
                  OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                  static_cast<int>(pb.length()), pb.ptr(),
                  tr.column_id_,
                  static_cast<int>(qb.length()), qb.ptr()))) {
          } else if (OB_FAIL(ob_write_string(arena, del_sql.string(), del_copy))) {
          } else if (OB_FAIL(write_sqls.push_back(del_copy))) {
          }
        }
        // pass C: one merged UPDATE per distinct rowkey; all columns of the
        // row land in one SET clause so the group becomes visible atomically
        for (int64_t i = 0; OB_SUCC(ret) && i < ok_tasks.count(); ++i) {
          ObAiWorkerTask &tr = rows.at(ok_tasks.at(i));
          if (!tr.request_ok_) {
            continue;  // not alive or already consumed into an UPDATE
          }
          ObSqlString upd_sql;
          int64_t n = 0;
          if (OB_FAIL(upd_sql.assign_fmt(
                  "UPDATE `%.*s`.`%.*s` SET ",
                  static_cast<int>(info.db_name_.length()), info.db_name_.ptr(),
                  static_cast<int>(info.table_name_.length()), info.table_name_.ptr()))) {
          }
          for (int64_t j = 0; OB_SUCC(ret) && j < ok_tasks.count(); ++j) {
            ObAiWorkerTask &tr2 = rows.at(ok_tasks.at(j));
            if (!tr2.request_ok_
                || 0 != tr2.rowkey_pred_.compare(tr.rowkey_pred_)) {
              continue;
            }
            if (OB_FAIL(upd_sql.append_fmt("%s`%.*s` = %.*s",
                    0 == n ? "" : ",",
                    static_cast<int>(tr2.column_name_.length()), tr2.column_name_.ptr(),
                    static_cast<int>(tr2.result_value_.length()), tr2.result_value_.ptr()))) {
            } else {
              tr2.request_ok_ = false;  // consumed
              ++n;
            }
          }
          if (OB_SUCC(ret) && n > 0) {
            ObString upd_copy;
            if (OB_FAIL(upd_sql.append_fmt(" WHERE %.*s",
                    static_cast<int>(tr.rowkey_pred_.length()), tr.rowkey_pred_.ptr()))) {
            } else if (OB_FAIL(ob_write_string(arena, upd_sql.string(), upd_copy))) {
            } else if (OB_FAIL(write_sqls.push_back(upd_copy))) {
            }
          }
        }
        if (OB_SUCC(ret) && write_sqls.count() > 0) {
          if (OB_FAIL(exec_sqls_in_job_session(write_sqls))) {
            ret = OB_SUCCESS;  // fail-safe: a failed write-back is retried on the next scan
          }
        }
      }

      // failure bookkeeping (same job-session channel as the write-back:
      // timer-thread proxy writes do not persist)
      if (OB_SUCC(ret) && fail_tasks.count() > 0) {
        ObSEArray<ObString, 8> fail_sqls;
        for (int64_t i = 0; OB_SUCC(ret) && i < fail_tasks.count(); ++i) {
          ObAiWorkerTask &tr = rows.at(fail_tasks.at(i));
          ObSqlString sql;
          ObStringBuffer pb(&arena), qb(&arena);
          ObString sql_copy;
          if (OB_FAIL(append_sql_escaped(pb, tr.rowkey_pred_))) {
          } else if (OB_FAIL(append_sql_escaped(qb, tr.request_id_))) {
          }
          int64_t new_retry = tr.retry_count_ + 1;
          if (OB_SUCC(ret) && new_retry > tr.max_retries_) {
            // terminal failure: drop the task, result stays NULL
            if (OB_FAIL(sql.assign_fmt(
                    "DELETE FROM %s WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                    " AND column_id = %lu AND request_id = '%.*s'",
                    OB_ALL_AI_COLUMN_TASK_TNAME, table_id,
                    static_cast<int>(pb.length()), pb.ptr(),
                    tr.column_id_,
                    static_cast<int>(qb.length()), qb.ptr()))) {
            }
          } else if (OB_SUCC(ret)) {
            if (OB_FAIL(sql.assign_fmt(
                    "UPDATE %s SET state = 3, retry_count = retry_count + 1, eligible_ts = %ld"
                    " WHERE table_id = %lu AND rowkey_predicate = '%.*s'"
                    " AND column_id = %lu AND request_id = '%.*s'",
                    OB_ALL_AI_COLUMN_TASK_TNAME,
                    ObTimeUtility::current_time() + RETRY_BACKOFF_US, table_id,
                    static_cast<int>(pb.length()), pb.ptr(),
                    tr.column_id_,
                    static_cast<int>(qb.length()), qb.ptr()))) {
            }
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(ob_write_string(arena, sql.string(), sql_copy))) {
            } else if (OB_FAIL(fail_sqls.push_back(sql_copy))) {
            }
          }
        }
        if (OB_SUCC(ret) && fail_sqls.count() > 0) {
          if (OB_FAIL(exec_sqls_in_job_session(fail_sqls))) {
            ret = OB_SUCCESS;  // retried on the next scan
          }
        }
      }
    }
  }
  if (OB_SUCC(ret) && stale_del_sqls.count() > 0) {
    if (OB_FAIL(exec_sqls_in_job_session(stale_del_sqls))) {
      ret = OB_SUCCESS;
    }
  }
  return ret;
}

}  // end namespace share
}  // end namespace oceanbase
