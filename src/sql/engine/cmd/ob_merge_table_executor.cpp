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

#define USING_LOG_PREFIX SQL_ENG

#include "sql/engine/cmd/ob_merge_table_executor.h"
#include "sql/resolver/cmd/ob_merge_table_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "lib/mysqlclient/ob_isql_client.h"
#include "observer/ob_inner_sql_connection.h"
#include "observer/ob_inner_sql_connection_pool.h"

namespace oceanbase
{
using namespace common;
using namespace observer;
namespace sql
{

int ObMergeTableExecutor::execute(ObExecContext &ctx, ObMergeTableStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  ObInnerSQLConnectionPool *pool = NULL;
  ObInnerSQLConnection *conn = NULL;
  uint64_t tenant_id = OB_INVALID_TENANT_ID;
  bool need_tx = false;
  int64_t total_affected_rows = 0;

  if (OB_ISNULL(session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", K(ret));
  } else if (OB_ISNULL(ctx.get_sql_proxy())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy is null", K(ret));
  } else if (OB_ISNULL(pool = static_cast<ObInnerSQLConnectionPool *>(
                            ctx.get_sql_proxy()->get_pool()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("connection pool is null", K(ret));
  } else if (OB_FAIL(pool->acquire_spi_conn(session, conn))) {
    LOG_WARN("failed to acquire inner sql connection", K(ret));
  } else {
    tenant_id = session->get_effective_tenant_id();
  }

  if (OB_SUCC(ret) && !session->is_in_transaction()) {
    if (OB_FAIL(conn->start_transaction(tenant_id))) {
      LOG_WARN("failed to start transaction", K(ret));
    } else {
      need_tx = true;
    }
  }

  // FAIL strategy: check for conflicts first (FOR UPDATE locks rows)
  int64_t conflict_cnt = 0;
  if (OB_SUCC(ret) && stmt.get_strategy() == MERGE_STRATEGY_FAIL
      && !stmt.get_conflict_check_sql().empty()) {
    {
      ObISQLClient::ReadResult res;
      if (OB_FAIL(conn->execute_read(tenant_id, stmt.get_conflict_check_sql(), res))) {
        LOG_WARN("failed to execute conflict check", K(ret));
      } else {
        common::sqlclient::ObMySQLResult *result = res.get_result();
        if (OB_ISNULL(result)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("result is null", K(ret));
        } else if (OB_FAIL(result->next())) {
          LOG_WARN("failed to get conflict check result", K(ret));
        } else if (OB_FAIL(result->get_int("conflict_cnt", conflict_cnt))) {
          LOG_WARN("failed to get conflict_cnt", K(ret));
        }
      }
    }

    if (OB_SUCC(ret) && conflict_cnt > 0) {
      char err_msg[256] = "";
      snprintf(err_msg, sizeof(err_msg),
          "MERGE TABLE: %ld conflict(s) detected. "
          "Use THEIRS or OURS strategy to resolve. FAIL strategy is",
          conflict_cnt);
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("merge conflict detected", K(ret), K(conflict_cnt));
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, err_msg);
    }
  }

  // Execute INSERT (incoming-only rows)
  if (OB_SUCC(ret) && !stmt.get_insert_sql().empty()) {
    int64_t affected_rows = 0;
    if (OB_FAIL(conn->execute_write(tenant_id, stmt.get_insert_sql(), affected_rows))) {
      LOG_WARN("failed to execute merge insert sql", K(ret), K(stmt.get_insert_sql()));
    } else {
      total_affected_rows += affected_rows;
      LOG_INFO("merge insert executed", K(affected_rows));
    }
  }

  // Execute UPDATE (conflict rows, THEIRS strategy only)
  if (OB_SUCC(ret) && !stmt.get_update_sql().empty()) {
    int64_t affected_rows = 0;
    if (OB_FAIL(conn->execute_write(tenant_id, stmt.get_update_sql(), affected_rows))) {
      LOG_WARN("failed to execute merge update sql", K(ret), K(stmt.get_update_sql()));
    } else {
      total_affected_rows += affected_rows;
      LOG_INFO("merge update executed", K(affected_rows));
    }
  }

  if (need_tx) {
    if (OB_SUCC(ret)) {
      int tmp_ret = conn->commit();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("failed to commit", K(tmp_ret));
        ret = tmp_ret;
      }
    } else {
      int tmp_ret = conn->rollback();
      if (OB_SUCCESS != tmp_ret) {
        LOG_WARN("failed to rollback", K(tmp_ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    ObPhysicalPlanCtx *plan_ctx = ctx.get_physical_plan_ctx();
    if (OB_NOT_NULL(plan_ctx)) {
      plan_ctx->set_affected_rows(total_affected_rows);
    }
  }

  if (OB_NOT_NULL(conn)) {
    ctx.get_sql_proxy()->close(conn, ret);
  }

  return ret;
}

} // namespace sql
} // namespace oceanbase
