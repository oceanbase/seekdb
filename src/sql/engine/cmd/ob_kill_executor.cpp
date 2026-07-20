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
#include "rootserver/ob_root_service.h"
#include "sql/resolver/cmd/ob_kill_stmt.h"
#include "sql/engine/cmd/ob_kill_session_arg.h"
#include "sql/engine/cmd/ob_kill_executor.h"
#include "observer/ob_server.h"
#include "share/ob_ex_rpc.h"
namespace oceanbase
{
using namespace common;
using namespace obcall;
namespace sql
{

int ObKillExecutor::execute(ObExecContext &ctx, ObKillStmt &stmt)
{
  int ret = OB_SUCCESS;
  ObKillSessionArg arg;
  ObAddr addr;
  ObSQLSessionMgr &session_mgr = OBSERVER.get_sql_session_mgr();
  if (OB_FAIL(arg.init(ctx, stmt))) {
    LOG_WARN("fail to init kill_session arg", K(ret), K(arg), K(ctx), K(stmt));
  } else if (OB_FAIL(kill_session(arg, session_mgr))) {
    if (OB_ENTRY_NOT_EXIST == ret) {//doesn't find sessid in current server
      if (OB_FAIL(get_remote_session_location(arg, ctx, addr))) {
        LOG_WARN("fail to get remote session location", K(ret), K(arg), K(ctx), K(addr));
      } else if (OB_FAIL(kill_remote_session(ctx, addr, arg))) {
        LOG_WARN("fail to kill remote session", K(ret), K(ctx), K(addr), K(arg));
      } else { /*do nothing*/}
    } else {
      LOG_WARN("fail to kill session", K(ret), K(arg));
    }
  }

  if (OB_UNKNOWN_CONNECTION == ret) {
    LOG_USER_ERROR(OB_UNKNOWN_CONNECTION, static_cast<uint64_t>(arg.sess_id_));
  } else if (OB_ERR_KILL_DENIED == ret) {
    LOG_USER_ERROR(OB_ERR_KILL_DENIED, static_cast<uint64_t>(arg.sess_id_));
  }
  return ret;
}

//If you are in system tenant, you can kill all threads and statements in any tenant.
//If you have the SUPER privilege, you can kill all threads and statements at your Tenant.
//Otherwise, you can kill only your own threads and statements.
int ObKillSession::kill_session(const ObKillSessionArg &arg, ObSQLSessionMgr &sess_mgr)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *sess_info = NULL;
  ObAddr addr;
  uint32_t sess_id = arg.sess_id_;
  ObSessionGetterGuard guard(sess_mgr, sess_id);
  if (OB_FAIL(guard.get_session(sess_info))) {
    LOG_WARN("fail to get session", K(ret), K(sess_id));
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is NULL", K(ret), K(arg));
  } else if (sess_info->is_real_inner_session()) {
    ret = OB_ERR_KILL_DENIED;
    LOG_WARN("It is not allowed to close the inner session", K(ret), K(arg));
  } else if (OB_FAIL(arg.check_auth_for_kill(1UL, sess_info->get_user_id()))) {
    ret = OB_ERR_KILL_DENIED;
    LOG_WARN("no permissions for kill", K(ret), K(arg.sess_id_));
  } else {
    if (arg.is_query_) {
      if (OB_FAIL(sess_mgr.kill_query(*sess_info))) {
        LOG_WARN("fail to kill query", K(ret), K(arg));
      }
    } else {
      if (OB_FAIL(sess_mgr.kill_session(*sess_info))) {
        LOG_WARN("fail to kill session", K(ret), K(arg));
      }
    }
  }
  return ret;
}

int ObKillExecutor::get_remote_session_location(const ObKillSessionArg &arg,
                  ObExecContext &ctx, ObAddr &addr)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObMySQLProxy::MySQLResult, res) {
    ObMySQLProxy *sql_proxy = ctx.get_sql_proxy();
    sqlclient::ObMySQLResult *result_set = NULL;
    ObSQLSessionInfo *cur_sess = ctx.get_my_session();
    ObSqlString read_sql;
    char svr_ip[OB_IP_STR_BUFF] = "";
    int64_t svr_port = 0;
    int64_t tmp_real_str_len = 0;

    //execute sql
    if (OB_ISNULL(sql_proxy) || OB_ISNULL(cur_sess)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sql proxy or session from exec context is NULL", K(ret), K(sql_proxy), K(cur_sess));
    } else if (OB_FAIL(generate_read_sql(arg.sess_id_, read_sql))) {
      LOG_WARN("fail to generate sql", K(ret), K(read_sql), K(*cur_sess), K(arg));
    } else if (OB_FAIL(sql_proxy->read(res, read_sql.ptr()))) {
      LOG_WARN("fail to read by sql proxy", K(ret), K(read_sql));
    } else if (OB_ISNULL(result_set = res.get_result())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("result set is NULL", K(ret), K(read_sql));
    } else {/*do nothing*/}
  
    //read result_set
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(result_set->next())) {
      if (OB_LIKELY(OB_ITER_END == ret)) {
        read_sql.reuse();
        if (OB_FAIL(generate_read_sql_from_session_info(arg.sess_id_, read_sql))) {
          LOG_WARN("fail to generate sql", K(ret), K(read_sql));
        } else if (OB_FAIL(sql_proxy->read(res, read_sql.ptr()))) {
          LOG_WARN("fail to read by sql proxy", K(ret), K(read_sql));
        } else if (OB_ISNULL(result_set = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("result set is NULL", K(ret), K(read_sql));
        } else if (OB_FAIL(result_set->next())) {
          if (OB_LIKELY(OB_ITER_END == ret)) {
            ret = OB_UNKNOWN_CONNECTION;
            LOG_WARN("fail to get next row", K(ret), K(result_set));
          }
        }
      }
    }
  
    if (OB_FAIL(ret)) {
    } else {
      UNUSED(tmp_real_str_len);
      EXTRACT_STRBUF_FIELD_MYSQL(*result_set, "svr_ip", svr_ip, OB_IP_STR_BUFF, tmp_real_str_len);
      EXTRACT_INT_FIELD_MYSQL(*result_set, "svr_port", svr_port, int64_t);
    }

    //set addr
    if (OB_FAIL(ret)) {
    } else if (OB_UNLIKELY(OB_ITER_END != result_set->next())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("more than one sessid record", K(ret), K(arg), K(read_sql));
    } else if (OB_UNLIKELY(!addr.set_ip_addr(svr_ip, static_cast<int32_t>(svr_port)))) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("fail to set ip_addr", K(ret), K(svr_ip), K(svr_port));
    } else {/*do nothing*/}
  
  }
  return ret;
}

int ObKillExecutor::generate_read_sql(uint32_t sess_id, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  const char *sql_str = "select svr_ip, svr_port from oceanbase.__all_virtual_processlist, oceanbase.__all_virtual_server_stat \
              where id = %u";
  if (OB_FAIL(sql.append_fmt(sql_str, sess_id))) {
    LOG_WARN("fail to append sql", K(ret), K(sess_id));
  }
  return ret;
}

int ObKillExecutor::generate_read_sql_from_session_info(uint32_t sess_id, ObSqlString &sql)
{
  int ret = OB_SUCCESS;
  const char *sql_str = "select svr_ip, svr_port from oceanbase.__all_virtual_session_info, oceanbase.__all_virtual_server_stat  \
              where id = %u";
  if (OB_FAIL(sql.append_fmt(sql_str, sess_id))) {
    LOG_WARN("fail to append sql", K(ret), K(sess_id));
  }
  return ret;
}

int ObKillExecutor::kill_remote_session(ObExecContext &ctx, const ObAddr &addr, const ObKillSessionArg &arg)
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = ctx.get_my_session();
  ObPhysicalPlanCtx *plan_ctx = GET_PHY_PLAN_CTX(ctx);
  if (OB_ISNULL(session) || OB_ISNULL(plan_ctx)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("some params are NULL", K(ret), K(session), K(plan_ctx));
  } else {
    int64_t timeout = plan_ctx->get_timeout_timestamp() - ObTimeUtility::current_time();
    
    if (OB_UNLIKELY(timeout <= 0)) {
      ret = OB_TIMEOUT;
      LOG_WARN("task_execute timeout before rpc", K(ret), K(addr), K(timeout), K(arg),
               K(1UL), "timeout_ts", plan_ctx->get_timeout_timestamp());
    } else if (OB_FAIL(ex_rpc::sync_call([&]() -> int {
      int ret = OB_SUCCESS;
      if (OB_ISNULL(GCTX.session_mgr_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(kill_session(arg, *GCTX.session_mgr_))) {
        ret = (OB_ENTRY_NOT_EXIST == ret) ? OB_UNKNOWN_CONNECTION : ret;
      }
      return ret;
    }))) {
      LOG_WARN("fail to kill remote session", K(ret), K(addr), K(timeout), K(arg));
    } else {/*do nothing*/}
  }
  return ret;
}
}// sql
}// oceanbase
