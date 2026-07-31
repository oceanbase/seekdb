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

#define USING_LOG_PREFIX SQL

#include "ob_sql_session_mgr.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "sql/engine/dml/ob_trigger_handler.h"

using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share;
using namespace oceanbase::observer;

ObSQLSessionMgr::ObSQLSessionMgr()
  : sessinfo_map_(),
    next_sessid_(1)
{}

ObSQLSessionMgr::~ObSQLSessionMgr()
{}

ObSQLSessionInfo *ObSQLSessionMgr::ValueAlloc::alloc_value()
{
  int ret = OB_SUCCESS;
  ObSQLSessionInfo *session = op_instance_alloc_args(&session_allocator_,
                                                     ObSQLSessionInfo);
  int64_t alloc_total_count = 0;
  if (OB_ISNULL(session)) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("fail to alloc session", K(ret));
  } else {
    ATOMIC_FAA(&active_count_, 1);
    session->set_valid(true);
    session->set_shadow(true);
  }
  alloc_total_count = ATOMIC_FAA(&alloc_total_count_, 1);
  if (alloc_total_count > 0 && alloc_total_count % 10000 == 0) {
    LOG_INFO("alloc_session_count", K(alloc_total_count));
  }
  return session;
}

void ObSQLSessionMgr::ValueAlloc::free_value(ObSQLSessionInfo *session)
{
  if (OB_NOT_NULL(session)) {
    int64_t free_total_count = 0;
    op_free(session);
    ATOMIC_FAA(&active_count_, -1);
    free_total_count = ATOMIC_FAA(&free_total_count_, 1);
    if (free_total_count > 0 && free_total_count % 10000 == 0) {
      LOG_INFO("free_session_count", K(free_total_count));
    }
  }
}

int ObSQLSessionMgr::init()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(sessinfo_map_.init())) {
    LOG_WARN("fail to init session map", K(ret));
  }
  // Start from 1 so first allocated sessid is 2, avoiding collision with
  // INNER_SQL_SESS_ID (== 1) which is reserved for non-managed inner sessions.
  next_sessid_ = 1;
  return ret;
}

void ObSQLSessionMgr::destroy()
{
  sessinfo_map_.destroy();
}

int ObSQLSessionMgr::inc_session_ref(const ObSQLSessionInfo *my_session)
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(my_session)) {
    ObSQLSessionInfo *tmp_session = NULL;
    uint32_t sessid = my_session->get_server_sid();
    if (OB_FAIL(get_session(sessid, tmp_session))) {
      LOG_WARN("fail to get session", K(sessid), K(ret));
    }
    UNUSED(tmp_session);
  }

  return ret;
}

int ObSQLSessionMgr::create_sessid(uint32_t &sessid)
{
  int ret = OB_SUCCESS;
  uint32_t candidate = 0;
  bool found = false;

  while (OB_SUCC(ret) && !found) {
    // Take next candidate, skip 0 and INNER_SQL_SESS_ID (1)
    do {
      candidate = ATOMIC_AAF(&next_sessid_, 1);
    } while (OB_UNLIKELY(candidate <= 1));

    // Probe sessinfo_map_ to check if sessid is already in use
    int probe_ret = sessinfo_map_.contains_key(Key(candidate));
    if (OB_ENTRY_NOT_EXIST == probe_ret) {
      sessid = candidate;
      found = true;
    } else if (OB_HASH_EXIST == probe_ret) {
      // sessid in use, try next
    } else {
      ret = probe_ret;
      LOG_WARN("probe sessid failed", K(ret), K(candidate));
    }
  }
  return ret;
}
// ret == OB_SUCCESS when, ensure sess_info != NULL, need to perform revert_session outside
// ret != OB_SUCCESS when, ensure sess_info == NULL, no need to revert_session outside
int ObSQLSessionMgr::create_session(ObSMConnection *conn, ObSQLSessionInfo *&sess_info)
{
  int ret = OB_SUCCESS;
  sess_info = NULL;
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn is NULL", K(ret));
  } else if (OB_FAIL(create_session(conn->sessid_, sess_info))) {
    LOG_WARN("create session failed", K(ret));
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sess_info is null", K(ret));
  } else {
    sess_info->inc_in_bytes(conn->connect_in_bytes_);
  }
  return ret;
}

int ObSQLSessionMgr::create_session(const uint32_t sessid,
                                    ObSQLSessionInfo *&session_info)
{
  int ret = OB_SUCCESS;
  int err = OB_SUCCESS;
  session_info = NULL;
  ObSQLSessionInfo *tmp_sess = NULL;
  if (OB_FAIL(sessinfo_map_.create(Key(sessid), tmp_sess))) {
    LOG_WARN("fail to create session", K(ret), K(sessid));
    if (OB_ENTRY_EXIST == ret) {
      ret = OB_SESSION_ENTRY_EXIST;
    }
  } else if (OB_ISNULL(tmp_sess)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("fail to alloc session info", K(ret), K(sessid));
  }

  if (OB_FAIL(ret)) {
    if (NULL != tmp_sess) {
      if (FALSE_IT(revert_session(tmp_sess))) {
      } else if (OB_SUCCESS != (err = sessinfo_map_.del(Key(sessid)))) {
        LOG_ERROR("fail to free session", K(err), K(sessid));
      } else {
        LOG_DEBUG("free session successfully in create session", K(err), K(sessid));
      }
    }
  } else if (OB_FAIL(tmp_sess->init(sessid, NULL, NULL))) {
    LOG_WARN("fail to init session", K(ret), K(tmp_sess), K(sessid));
    if (FALSE_IT(revert_session(tmp_sess))) {
      LOG_ERROR("fail to free session", K(err), K(sessid));
    } else if (OB_SUCCESS != (err = sessinfo_map_.del(Key(sessid)))) {
      LOG_ERROR("fail to free session", K(err), K(sessid));
    } else {
      LOG_DEBUG("free session successfully in create session", K(err), K(sessid));
    }
  } else {
    tmp_sess->update_last_active_time();
    session_info = tmp_sess;
  }
  return ret;
}

int ObSQLSessionMgr::free_session(const ObFreeSessionCtx &ctx)
{
  int ret = OB_SUCCESS;
  uint32_t sessid = ctx.sessid_;
  
  bool has_inc = ctx.has_inc_active_num_;
  ObSQLSessionInfo *sess_info = NULL;
  sessinfo_map_.get(Key(sessid), sess_info);
  if (NULL != sess_info) {
    if (OB_UNLIKELY(OB_SUCCESS != sess_info->on_user_disconnect())) {
      LOG_WARN("user disconnect failed", K(ret), K(sess_info->get_user_id()));
    }
    sessinfo_map_.revert(sess_info);
  }
  if (OB_FAIL(sessinfo_map_.del(Key(sessid)))) {
    LOG_WARN("fail to remove session from session map", K(ret), K(sessid));
  } else if (sessid != 0 && has_inc) {
  }
  return ret;
}

void ObSQLSessionMgr::try_check_session()
{
  int ret = OB_SUCCESS;
  CheckSessionFunctor check_timeout(this);
  if (OB_FAIL(for_each_session(check_timeout))) {
    LOG_WARN("fail to check time out", K(ret));
  }
}

int ObSQLSessionMgr::get_min_active_snapshot_version(share::SCN &snapshot_version)
{
  int ret = OB_SUCCESS;

  concurrency_control::GetMinActiveSnapshotVersionFunctor min_active_txn_version_getter;

  if (OB_FAIL(for_each_session(min_active_txn_version_getter))) {
    LOG_WARN("fail to get min active snapshot version", K(ret));
  } else {
    snapshot_version = min_active_txn_version_getter.get_min_active_snapshot_version();
  }

  return ret;
}

void ObSQLSessionMgr::runTimerTask()
{
  try_check_session();
}

// just a wrapper
int ObSQLSessionMgr::kill_query(ObSQLSessionInfo &session)
{
  return kill_query(session, ObSQLSessionState::QUERY_KILLED);
}

int ObSQLSessionMgr::set_query_deadlocked(ObSQLSessionInfo &session)
{
  return kill_query(session, ObSQLSessionState::QUERY_DEADLOCKED);
}

int ObSQLSessionMgr::kill_query(ObSQLSessionInfo &session,
    const ObSQLSessionState status)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  // If start_stmt/end_stmt gets stuck, at this point, we need to wake up the sql thread first, then set the flag, otherwise kill session will not work
  if (OB_SUCCESS != (tmp_ret = ObSqlTransControl::kill_query_session(session, status))) {
    LOG_WARN("fail to kill query or session", "ret", tmp_ret, K(session));
  }

  if (ObSQLSessionState::QUERY_KILLED == status) {
    ret = session.kill_query();
  } else if (ObSQLSessionState::QUERY_DEADLOCKED == status) {
    ret = session.set_query_deadlocked();
  } else {
    LOG_WARN("unexpected status", K(status));
    ret = OB_ERR_UNEXPECTED;
  }

  return ret;
}

// kill idle timeout transaction on this session
int ObSQLSessionMgr::kill_idle_timeout_tx(ObSQLSessionInfo *session)
{
  return ObSqlTransControl::kill_idle_timeout_tx(session);
}

// kill deadlock transaction on this session
int ObSQLSessionMgr::kill_deadlock_tx(ObSQLSessionInfo *session)
{
  return ObSqlTransControl::kill_deadlock_tx(session);
}

int ObSQLSessionMgr::kill_session(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  // If start_stmt/end_stmt gets stuck, at this point, we need to wake up the sql thread first, then set the flag, otherwise kill session will not work
  if (OB_SUCCESS != (tmp_ret = ObSqlTransControl::kill_query_session(
        session, ObSQLSessionState::SESSION_KILLED))) {
    LOG_WARN("fail to kill query or session", "ret", tmp_ret, K(session));
  }
  session.set_session_state(SESSION_KILLED);
  // NOTE: The order of the following two guards cannot be changed, otherwise there is a chance of forming a deadlock
  ObSQLSessionInfo::LockGuard query_lock_guard(session.get_query_lock());
  ObSQLSessionInfo::LockGuard data_lock_guard(session.get_thread_data_lock());
  bool need_disconnect = false;
  session.set_query_start_time(ObTimeUtility::current_time());
  session.set_mark_killed(true);
  if (session.is_in_transaction()) {
    if (OB_SUCCESS != (tmp_ret = ObSqlTransControl::kill_tx_on_session_killed(&session))) {
      LOG_WARN("fail to rollback transaction", K(session.get_server_sid()),
               K(tmp_ret), KPC(session.get_tx_desc()),
               "query_str", session.get_current_query_string(),
               K(need_disconnect));
    }
  }

  session.update_last_active_time();
  session.set_disconnect_state(NORMAL_KILL_SESSION);
  rpc::ObSqlSockDesc &sock_desc = session.get_sock_desc();
  if (OB_LIKELY(NULL != sock_desc.sock_desc_)) {
    SQL_REQ_OP.disconnect_by_sql_sock_desc(sock_desc);
    // this function will trigger on_close(), and then free the session
    LOG_INFO("kill session successfully",
             "peer", session.get_peer_addr(),
             "real_client_ip", session.get_client_ip(),
             "server_sid", session.get_server_sid(),
             "query_str", session.get_current_query_string());
  } else {
    LOG_WARN("get conn from session info is null", K(session.get_server_sid()),
        K(session.get_magic_num()));
  }

  return ret;
}

int ObSQLSessionMgr::disconnect_session(ObSQLSessionInfo &session)
{
  int ret = OB_SUCCESS;
  // NOTE: The order of the following two guards cannot be changed, otherwise there is a chance of forming a deadlock
  ObSQLSessionInfo::LockGuard query_lock_guard(session.get_query_lock());
  ObSQLSessionInfo::LockGuard data_lock_guard(session.get_thread_data_lock());
  bool need_disconnect = false;
  session.set_query_start_time(ObTimeUtility::current_time());
  // Call this function before session.set_session_state(SESSION_KILLED) is called in ObSMHandler::on_disconnect,
  if (session.is_in_transaction()) {
    if (OB_FAIL(ObSqlTransControl::kill_tx_on_session_disconnect(&session))) {
      LOG_WARN("fail to rollback transaction", K(session.get_server_sid()), K(ret),
               "query_str", session.get_current_query_string(),
               K(need_disconnect));
    }
  }
  session.update_last_active_time();
  return ret;
}

int ObSQLSessionMgr::kill_all_sessions(bool force_kill)
{
  int ret = OB_SUCCESS;
  KillAllSessions kt_func(this, force_kill);
  OZ (for_each_session(kt_func));
  OX (ret = kt_func.get_ret_code());
  LOG_INFO("killed all sessions", K(force_kill));
  return ret;
}

void ObSQLSessionMgr::wait_sessions_drained()
{
  while (sessinfo_map_.get_alloc_handle().count() != 0) {
    LOG_WARN_RET(OB_NEED_RETRY, "session manager is waiting for sessions to drain",
                 "count", sessinfo_map_.get_alloc_handle().count());
    usleep(1000 * 1000);
  }
  LOG_INFO("all managed sessions have drained");
}

bool ObSQLSessionMgr::CheckSessionFunctor::operator()(sql::ObSQLSessionMgr::Key key,
                                                      ObSQLSessionInfo *sess_info)
{
  int ret = OB_SUCCESS;
  UNUSED(key);
  bool is_timeout = false;
  if (OB_ISNULL(sess_info)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session info is NULL");
  } else if (false == sess_info->is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("session info is not valid", K(ret));
  } else {
    int callback_retcode = OB_SUCCESS;
    transaction::ObITxCallback *commit_cb = NULL;
    // NOTE: The order of the following two guards cannot be changed, otherwise there is a chance of forming a deadlock
    if (OB_FAIL(sess_info->try_lock_query())) {
      if (OB_UNLIKELY(OB_EAGAIN != ret)) {
        LOG_WARN("fail to try lock query", K(ret));
      } else {
        ret = OB_SUCCESS;
      }
    } else {
      if (OB_FAIL(sess_info->try_lock_thread_data())) {
        if (OB_UNLIKELY(OB_EAGAIN != ret)) {
          LOG_WARN("fail to try lock thread data", K(ret));
        } else {
          ret = OB_SUCCESS;
        }
      } else {
        if (OB_ISNULL(sess_mgr_)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("session manager point is NULL");
        } else if (OB_FAIL(sess_info->is_timeout(is_timeout))) {
          LOG_WARN("fail to check is timeout", K(ret));
        } else if (true == is_timeout) {
          LOG_INFO("session is timeout, kill this session", K(key.sessid_));
          ret = sess_mgr_->kill_session(*sess_info);
        } else {
          //with the help ofsession traversaloffunctionality，tryrevert sessioncacheofschema guard，
          // Avoid holding guard for a long time, causing schema mgr slots to be unable to release
          sess_info->get_cached_schema_guard_info().try_revert_schema_guard();
          // Refresh cached runtime configuration periodically.
          sess_info->refresh_runtime_config();
          // send client commit result if txn commit timeout
          if (OB_FAIL(sess_info->is_trx_commit_timeout(commit_cb, callback_retcode))) {
            LOG_WARN("fail to check transaction commit timeout", K(ret));
          } else if (commit_cb) {
            LOG_INFO("transaction commit reach timeout", K(callback_retcode), K(key.sessid_));
          } else if (OB_FAIL(sess_info->is_trx_idle_timeout(is_timeout))) {
            // kill transaction which is idle more than configuration 'ob_trx_idle_timeout'
            LOG_WARN("fail to check transaction idle timeout", K(ret));
          } else if (true == is_timeout) {
            LOG_INFO("transaction is idle timeout, start to rollback", K(key.sessid_));
            int tmp_ret;
            if (OB_SUCCESS != (tmp_ret = sess_mgr_->kill_idle_timeout_tx(sess_info))) {
              LOG_WARN("fail to kill transaction", K(ret), K(key.sessid_));
            }
          }
        }
        (void)sess_info->unlock_thread_data();
      }
      (void)sess_info->unlock_query();
      // NOTE: must execute callback after release query_lock
      if (commit_cb) {
        commit_cb->callback(callback_retcode);
      }
    }
  }
  //detect sql hung
  int64_t tmp_ret = (OB_E(EventTable::EN_SQL_HUNG_DETECT) OB_SUCCESS);
  int64_t timeout_multiplier = OB_ERROR == tmp_ret ? 2 : std::max(static_cast<int64_t>(1), std::abs(tmp_ret));
  if (OB_FAIL(ret)) {
  } else if (OB_SUCCESS == tmp_ret) {
    //do nothing
  } else if (obmysql::COM_QUERY == sess_info->get_mysql_cmd() ||
            obmysql::COM_STMT_EXECUTE == sess_info->get_mysql_cmd() ||
            obmysql::COM_STMT_PREPARE == sess_info->get_mysql_cmd()) {
    int64_t cur_time = common::ObTimeUtility::current_time();
    int64_t query_timeout = 0;
    ObSQLSessionInfo::LockGuard lock_guard(sess_info->get_thread_data_lock());
    if ((sess_info->get_stmt_type() != stmt::T_SELECT &&
         sess_info->get_stmt_type() != stmt::T_UPDATE &&
         sess_info->get_stmt_type() != stmt::T_INSERT &&
         sess_info->get_stmt_type() != stmt::T_DELETE &&
         sess_info->get_stmt_type() != stmt::T_REPLACE &&
         sess_info->get_stmt_type() != stmt::T_EXPLAIN) || 
        sess_info->get_ddl_info().is_ddl() || 
        OB_NOT_NULL(sess_info->get_pl_context()) ||
        !sess_info->is_user_session() || 
        sess_info->get_current_trace_id().is_invalid()) {
      // DDL, PL and physical-restore statements are not subject to query-timeout control.
    } else if (OB_FAIL(sess_info->get_sys_variable(SYS_VAR_OB_QUERY_TIMEOUT, query_timeout))) {
      LOG_WARN("failed to get sesion variable", K(ret));
    } else if (sess_info->get_query_start_time() > 0 &&
               cur_time - sess_info->get_query_start_time() > timeout_multiplier * query_timeout + 1000000) {
      LOG_ERROR("detect sql hung!!!", K(sess_info->get_current_trace_id()), 
                                      K(sess_info->get_cur_sql_id()),
                                      K(sess_info->get_thread_id()),
                                      K(cur_time - sess_info->get_query_start_time()), 
                                      K(query_timeout), K(timeout_multiplier),
                                      "session_state", ObString::make_string(sess_info->get_session_state_str()),
                                      K(sess_info->get_current_query_string()));
    }
  }
  return OB_SUCCESS == ret;
}

bool ObSQLSessionMgr::KillAllSessions::operator() (
    sql::ObSQLSessionMgr::Key, ObSQLSessionInfo *sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("session mgr_ is NULL", K(mgr_));
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sess info is NULL", K(sess_info));
  } else {
    LOG_INFO("kill session", K(sess_info->get_server_sid()), K_(force_kill));
    ret = mgr_->kill_session(*sess_info);
  }
  return OB_SUCCESS == ret;
}

ObSessionGetterGuard::ObSessionGetterGuard(ObSQLSessionMgr &sess_mgr, uint32_t sessid)
  : mgr_(sess_mgr), session_(NULL)
{
  ret_ = mgr_.get_session(sessid, session_);
  if (OB_SUCCESS != ret_) {
    LOG_WARN_RET(ret_, "get session fail", K(ret_), K(sessid));
  } else {
    NG_TRACE_EXT(session, OB_ID(sid), session_->get_server_sid());
  }
}

ObSessionGetterGuard::~ObSessionGetterGuard()
{
  if (session_) {
    mgr_.revert_session(session_);
  }
}
