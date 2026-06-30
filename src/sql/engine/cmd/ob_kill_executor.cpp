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
  } else {
    // In seekdb single-node mode, always treat as direct mode
    bool direct_mode = true;
    // Direct connection scenario kill session or kill query
    if (direct_mode) {
      if (OB_FAIL(kill_session(arg, session_mgr))) {
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
    } else {
      // Proxy connection scenario kill session or kill query.
      if (arg.is_query_ == true) {
        // kill query proxy cs id scene
        if (OB_FAIL(kill_query_cs_id(arg, session_mgr, ctx))) {
          LOG_WARN("Fail to kill query cs id", K(ret), K(arg));
        }
      } else {
        if (OB_FAIL(kill_client_session(arg, session_mgr, ctx))) {
          if (ret == OB_ERR_KILL_CLIENT_SESSION) {
            LOG_DEBUG("Succ to Kill Client Session", K(ret), K(arg));
          } else {
            LOG_WARN("Fail to kill client session", K(ret), K(arg));
          }
        }
      }
    }
  }

  if (OB_UNKNOWN_CONNECTION == ret) {
    LOG_USER_ERROR(OB_UNKNOWN_CONNECTION, static_cast<uint64_t>(arg.sess_id_));
  } else if (OB_ERR_KILL_DENIED == ret) {
    LOG_USER_ERROR(OB_ERR_KILL_DENIED, static_cast<uint64_t>(arg.sess_id_));
  }
  return ret;
}

int ObKillExecutor::kill_query_cs_id(const ObKillSessionArg &arg, ObSQLSessionMgr &sess_mgr,
                                       ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *sess_info = NULL;
  ObSQLSessionInfo *curr_sess_info = NULL;
  ObAddr addr;
  uint32_t client_sess_id = arg.sess_id_;
  uint32_t server_sess_id = INVALID_SESSID;
  // Proxy connection scenario kill session
  common::ObZone zone;
  obcall::ObKillQueryClientSessionArg cs_arg;
  bool is_kill_succ = true;
  LOG_DEBUG("Begin to send kill query rpc", K(arg.sess_id_));
  if (OB_ISNULL(curr_sess_info = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret), K(ctx));
  } else if (OB_ISNULL(GCTX.root_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("fail to get root_service", K(ret), K(GCTX.root_service_));
  } else if (FALSE_IT(cs_arg.set_client_sess_id(client_sess_id))) {
  } else {
    // seekdb: direct call to session_mgr, replicating ObKillQueryClientSessionP::process().
    uint32_t kill_server_sess_id = INVALID_SESSID;
    ObSQLSessionInfo *kill_session = nullptr;
    int kill_ret = OB_SUCCESS;
    if (OB_ISNULL(GCTX.session_mgr_)) {
      kill_ret = OB_ERR_UNEXPECTED;
    } else if (OB_FAIL(GCTX.session_mgr_->get_client_sess_map().get_refactored(
            cs_arg.get_client_sess_id(), kill_server_sess_id))) {
      if (ret == OB_HASH_NOT_EXIST) {
        ret = OB_SUCCESS;
      }
    } else if (OB_FAIL(GCTX.session_mgr_->get_session(kill_server_sess_id, kill_session))) {
      ret = OB_SUCCESS;
    } else if (OB_ISNULL(kill_session)) {
      kill_ret = OB_ERR_UNEXPECTED;
    } else {
      kill_ret = GCTX.session_mgr_->kill_query(*kill_session);
    }
    if (kill_session != nullptr) {
      GCTX.session_mgr_->revert_session(kill_session);
    }
    if (OB_SUCCESS != kill_ret) {
      is_kill_succ = false;
      if (kill_ret != OB_TENANT_NOT_IN_SERVER) {
        ret = kill_ret;
      } else {
        ret = OB_SUCCESS;
      }
    }
  }
  if (OB_FAIL(ret)) {
    LOG_WARN("Fail to Kill Query Client Session", K(ret), K(client_sess_id));
  } else {
    LOG_INFO("Succ to Kill Query Client Session", K(ret), K(client_sess_id));
  }

  return ret;
}

int ObKillExecutor::kill_client_session(const ObKillSessionArg &arg, ObSQLSessionMgr &sess_mgr,
                                       ObExecContext &ctx)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  ObSQLSessionInfo *sess_info = NULL;
  ObSQLSessionInfo *curr_sess_info = NULL;
  ObAddr addr;
  uint32_t client_sess_id = arg.sess_id_;
  uint32_t server_sess_id = INVALID_SESSID;
  int64_t local_session_create_time = 0;
  // Proxy connection scenario kill session
  if (OB_ISNULL(curr_sess_info = ctx.get_my_session())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is NULL", K(ret), K(ctx));
  } else if (OB_FAIL(sess_mgr.get_client_sess_map().get_refactored(client_sess_id, server_sess_id))) {
    //The current machine does not have this id and needs to be broadcast to other machines.
    // 1. In order to obtain the create time of the killed client ID for storing the map,
    //    if it cannot be found on the current machine, you need to search globally.
    // 2. If no one can be found, the kill will fail directly. It should be an illegal ID.
    // 3. If found, the first address that can be obtained is recorded. If the address is
    //    specified, the time will be sent back when sending remotely.
    // If all machines are unsuccessful, it should be that this ID does not exist or
    // there is a network problem. If some are successful and some fail, it is a network problem.
    LOG_WARN("fail to get client session in this server", K(ret), K(client_sess_id));
    ret = OB_SUCCESS;
  } else if (OB_FAIL(sess_mgr.get_session(server_sess_id, sess_info))) {
    ret = OB_SUCCESS;
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session info is NULL", K(ret), K(client_sess_id));
  } else if (OB_FAIL(arg.check_auth_for_kill(1UL, sess_info->get_user_id()))) {
    ret = OB_ERR_KILL_DENIED;
    LOG_WARN("no permissions for kill", K(ret), K(arg.sess_id_));
  } else {
    // sess_info is the session need to be killed.
    // 1. If the current session is the session currently executing the kill command
    // it can directly return the error code (OB_ERR_KILL_CLIENT_SESSION) to the proxy.
    // 2. If not, return OB_SUCCESS.
    local_session_create_time = sess_info->get_client_create_time();
    if (OB_FAIL(sess_mgr.kill_session(*sess_info))) {
        LOG_WARN("fail to kill session", K(ret), K(arg));
    } else {
      if (client_sess_id == curr_sess_info->get_client_sid()) {
        ret = OB_ERR_KILL_CLIENT_SESSION;
      } else {
        ret = OB_SUCCESS;
      }
    }
    LOG_INFO("current server conclude kill client session", K(arg.sess_id_));
  }
  if (OB_SUCC(ret)) {
    ObAddr cs_addr;
    int64_t create_time = local_session_create_time;
    // current server not have cs_id, find it in remote.
    // If there is no link between proxy and server,
    // unknown client session id will be reported.
    if (OB_FAIL(get_remote_session_location(arg, ctx, cs_addr, true))) {
      LOG_WARN("fail to get client session location, unknown client sessid",
              K(ret), K(arg), K(ctx), K(cs_addr));
      // Obtain the client establishment time for map maintenance.
    } else if (cs_addr != GCTX.self_addr() &&
               OB_FAIL(get_client_session_create_time_and_auth(
                   arg, ctx, cs_addr, create_time))) {
      LOG_WARN("fail to get client session create time or no auth", K(ret),
               K(arg), K(ctx), K(cs_addr), K(ret));
      // If the time cannot be obtained, return kill failure.
      if (ret == OB_ENTRY_NOT_EXIST) {
        ret = OB_ERR_KILL_CLIENT_SESSION_FAILED;
      }
    } else if (cs_addr.is_valid()) {
      obcall::ObKillClientSessionArg cs_arg;
      obcall::ObKillClientSessionRes cs_result;
      common::ObZone zone;
      bool is_kill_succ = true;
      // Determine the broadcast range based on whether it is a system tenant
      // Currently, there is no interface for querying node addresses at tenant granularity,
      // which can be optimized later.
      LOG_DEBUG("Begin to send kill session rpc", K(arg.sess_id_),K(create_time));
      if (OB_ISNULL(GCTX.root_service_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("fail to get root_service", K(ret), K(GCTX.root_service_));
      } else if (FALSE_IT(cs_arg.set_create_time(create_time))) {
      } else if (FALSE_IT(cs_arg.set_client_sess_id(client_sess_id))) {
      } else {
        // seekdb: local kill already happened above. Record in kill map (mirroring processor).
        uint32_t kill_server_sess_id = INVALID_SESSID;
        if (OB_NOT_NULL(GCTX.session_mgr_) &&
            OB_SUCCESS == GCTX.session_mgr_->get_client_sess_map().get_refactored(
                cs_arg.get_client_sess_id(), kill_server_sess_id)) {
          int flag = 1;
          GCTX.session_mgr_->get_kill_client_sess_map().set_refactored(
              cs_arg.get_client_sess_id(), cs_arg.get_create_time(), flag);
        }
      }
      if (OB_FAIL(ret)) {
        // do nothing.
      } else if (is_kill_succ == false) {
        ret = OB_ERR_KILL_CLIENT_SESSION_FAILED;
        LOG_WARN("Fail to Kill Client Session", K(ret), K(client_sess_id));
      } else {
        LOG_INFO("Succ to Kill Client Session", K(ret), K(client_sess_id));
      }

      // In the end, if everything succeeds here, the current map will be recorded.
      // If it is not completely successful, there is no need to record it, in order
      // to ensure that the recording time is valid.
      if (OB_FAIL(ret)) {
        LOG_WARN("kill client session not all successful", K(ret), K(cs_arg));
      } else {
        if (NULL != sess_info) {
          // The mark maintained here is used to trigger a link
          // break when the next request hits the current session.
          sess_info->set_mark_killed(true);
        }
        // The reason for maintaining the kill session id map is that proxy A's kill
        // request kills proxy B's client link. The next time a new connection is requested,
        // the map will be used to determine whether kill is needed.
        int flag = 1;
        sess_mgr.get_kill_client_sess_map().set_refactored(client_sess_id, create_time, flag);
      }
    }
  }
  if (NULL != sess_info) {
    sess_mgr.revert_session(sess_info);
  }

  return ret;
}

int ObKillExecutor::get_client_session_create_time_and_auth(const ObKillSessionArg &arg, ObExecContext &ctx,
                          common::ObAddr &cs_addr, int64_t &create_time)
{
  int ret = OB_SUCCESS;
  obcall::ObClientSessionCreateTimeAndAuthArg cs_arg;
  obcall::ObClientSessionCreateTimeAndAuthRes cs_result;
  common::ObZone zone;

  if (FALSE_IT(cs_arg.set_client_sess_id(arg.sess_id_))) {
  } else if (FALSE_IT(cs_arg.set_has_user_super_privilege(arg.has_user_super_privilege_))) {
  } else if (FALSE_IT(cs_arg.set_user_id(arg.user_id_))) {
  } else if (OB_FAIL(ex_rpc::sync_call([&]() -> int {
      int ret = OB_SUCCESS;
      ObSQLSessionInfo *session = NULL;
      uint32_t server_sess_id = INVALID_SESSID;
      if (OB_ISNULL(GCTX.session_mgr_)) {
        ret = OB_ERR_UNEXPECTED;
      } else if (OB_FAIL(GCTX.session_mgr_->get_client_sess_map().get_refactored(cs_arg.get_client_sess_id(), server_sess_id))) {
      } else if (OB_FAIL(GCTX.session_mgr_->get_session(server_sess_id, session))) {
      } else if (OB_ISNULL(session)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        cs_result.set_client_create_time(session->get_client_create_time());
        cs_result.set_have_kill_auth(true);
      }
      if (NULL != session) { GCTX.session_mgr_->revert_session(session); }
      return ret;
    }))) {
      // rpc fail not kill client session.
      LOG_WARN("fail to rpc", K(ret));
  } else if (cs_result.is_have_kill_auth() == false) {
    ret = OB_ERR_KILL_DENIED;
    LOG_WARN("no permissions for kill", K(ret), K(arg.sess_id_));
  } else if (FALSE_IT(create_time = cs_result.get_client_create_time())) {
  } else if (create_time == 0) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to set client create time", K(ret));
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

// is_client_session = true, for finding kill client session
int ObKillExecutor::get_remote_session_location(const ObKillSessionArg &arg,
                  ObExecContext &ctx, ObAddr &addr, bool is_client_session)
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
    } else if (!is_client_session && OB_UNLIKELY(OB_ITER_END != result_set->next())) {
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
