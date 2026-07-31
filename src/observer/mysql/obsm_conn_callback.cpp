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

#define USING_LOG_PREFIX RPC_OBMYSQL
#include "observer/mysql/obsm_conn_callback.h"
#include "share/rc/ob_server_runtime.h"
#include "rpc/obmysql/ob_sql_sock_session.h"
#include "lib/random/ob_mysql_random.h"
#include "observer/omt/ob_server_runtime.h"
#include "observer/ob_srv_task.h"
#include "share/schema/ob_schema_utils.h"

namespace oceanbase
{
using namespace common;
using namespace observer;
namespace obmysql
{

static int create_scramble_string(char *scramble_buf, const int64_t buf_len, common::ObMysqlRandom &thread_rand)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!thread_rand.is_inited())) {
    if (OB_UNLIKELY(!GCTX.scramble_rand_->is_inited())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("global_rand has not inited, it should not happened", K(ret));
    } else {
      // Concurrent access by multiple threads maybe happened here, but we do not care
      const uint64_t tmp_seed = GCTX.scramble_rand_->get_uint64();
      thread_rand.init(tmp_seed + reinterpret_cast<uint64_t>(&thread_rand),
                       tmp_seed + static_cast<uint64_t>(ObTimeUtility::current_time()));
      LOG_INFO("init thread_rand succ", K(ret));
    }
  }

  if (FAILEDx(thread_rand.create_random_string(scramble_buf, buf_len))) {
    LOG_ERROR("fail to create_random_string", K(scramble_buf), K(buf_len), K(ret));
  }
  return ret;
}

static int sm_conn_init(ObSMConnection& conn)
{
  int ret = OB_SUCCESS;
  int crt_id_ret = OB_SUCCESS;
  uint32_t sessid = 0;
  crt_id_ret = ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->create_sessid(sessid);
  if (OB_UNLIKELY(OB_SUCCESS != crt_id_ret && OB_ERR_CON_COUNT_ERROR != crt_id_ret)) {
    ret = crt_id_ret;
    LOG_WARN("fail to create sessid", K(crt_id_ret), K(sessid));
  } else {
    conn.sessid_ = sessid;
    conn.ret_ = crt_id_ret;
  }
  return ret;
}

int ObSMConnectionCallback::init(ObSqlSockSession& sess, ObSMConnection& conn)
{
  int ret = OB_SUCCESS;
  // The HandshakeV10 greeting itself is built and sent by the Rust reactor
  // right after this callback returns; here we only create what it needs —
  // the session id and the scramble the later auth check verifies against.
  RLOCAL(common::ObMysqlRandom, thread_scramble_rand);
  int64_t autocommit = 0;
  if (OB_FAIL(sm_conn_init(conn))) {
    LOG_WARN("init conn fail", K(ret));
  } else if (OB_FAIL(share::schema::ObSchemaUtils::get_runtime_int_variable(
                 *GCTX.schema_service_, share::SYS_VAR_AUTOCOMMIT, autocommit))) {
    LOG_WARN("get global autocommit failed", K(ret));
  } else if (OB_UNLIKELY(0 != autocommit && 1 != autocommit)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected global autocommit", K(ret), K(autocommit));
  } else if (OB_FAIL(create_scramble_string(conn.scramble_buf_, sizeof(conn.scramble_buf_), thread_scramble_rand))) {
    LOG_WARN("create scramble string failed", K(ret));
  } else {
    conn.autocommit_snapshot_ = autocommit;
    sess.sql_session_id_ = conn.sessid_;
    LOG_INFO("sm conn init succ", K(conn.sessid_), K(sess.client_addr_),
             K(autocommit));
  }
  return ret;
}

static void sm_conn_unlock_runtime(ObSMConnection& conn)
{
  if (NULL != conn.runtime_ && conn.is_runtime_locked_) {
    conn.runtime_->unlock();
    conn.is_runtime_locked_ = false;
    conn.runtime_ = NULL;
    LOG_INFO("unlock session of runtime", K(conn.sessid_));
  }
}

void ObSMConnectionCallback::destroy(ObSMConnection& conn)
{
  int ret = OB_SUCCESS;
  sql::ObDisconnectState disconnect_state = sql::ObDisconnectState::DIS_INIT;
  ObCurTraceId::TraceId trace_id;
  if (conn.is_sess_alloc_.load(std::memory_order_acquire)) {
    if (!conn.is_sess_free_.load(std::memory_order_acquire)) {
      {
        int tmp_ret = OB_SUCCESS;
        sql::ObSQLSessionInfo *sess_info = NULL;
        sql::ObSessionGetterGuard guard(*::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>(), conn.sessid_);
        if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = guard.get_session(sess_info)))) {
          LOG_WARN_RET(tmp_ret, "fail to get session", K(tmp_ret), K(conn.sessid_));
        } else if (OB_ISNULL(sess_info)) {
          tmp_ret = OB_ERR_UNEXPECTED;
          LOG_WARN_RET(tmp_ret, "session info is NULL", K(tmp_ret), K(conn.sessid_));
        } else {
          disconnect_state = sess_info->get_disconnect_state();
          trace_id = sess_info->get_current_trace_id();
        }
      }
      sql::ObFreeSessionCtx ctx;
      
      ctx.sessid_ = conn.sessid_;
      ctx.has_inc_active_num_ = conn.has_inc_active_num_;

      //free session in task
      ObSrvTask *task = OB_NEW(ObDisconnectTask,
                                ObModIds::OB_SQL_REQUEST,
                                ctx);
      if (OB_UNLIKELY(NULL == task)) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } else if (OB_UNLIKELY(NULL == conn.runtime_)) {
        ret = OB_RUNTIME_SCHEMA_NOT_READY;
      } else if (OB_FAIL(conn.runtime_->recv_request(*task))) {
        LOG_WARN("push disconnect task fail", K(conn.sessid_), K(ret));
        ob_delete(task);
      }
      // free session locally
      if (OB_FAIL(ret)) {
        ObMPDisconnect disconnect_processor(ctx);
        rpc::frame::ObReqProcessor *processor = static_cast<rpc::frame::ObReqProcessor *>(&disconnect_processor);
        if (OB_FAIL(processor->run())) {
          LOG_WARN("free session fail and related session id can not be reused", K(ret), K(ctx));
        }
      }
   }
  } else {
    // sessid no longer needs to be recycled in seekdb
  }

  sm_conn_unlock_runtime(conn);
  share::ObTaskController::get().allow_next_syslog();
  LOG_INFO("connection close",
           "sessid", conn.sessid_,
           "c/s protocol", get_cs_protocol_type_name(conn.get_cs_protocol_type()),
           "is_sess_alloc_", conn.is_sess_alloc_.load(std::memory_order_acquire),
           K(ret),
           K(trace_id),
           K(disconnect_state));
  conn.~ObSMConnection();
}

int ObSMConnectionCallback::on_disconnect(observer::ObSMConnection& conn)
{
  int ret = OB_SUCCESS;
  if (conn.is_sess_alloc_.load(std::memory_order_acquire)
      && !conn.is_sess_free_.load(std::memory_order_acquire)
      && ObSMConnection::INITIAL_SESSID != conn.sessid_) {
    sql::ObSQLSessionInfo *sess_info = NULL;
    sql::ObSessionGetterGuard guard(*::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>(), conn.sessid_);
    if (OB_FAIL(guard.get_session(sess_info))) {
      LOG_WARN("fail to get session", K(conn.sessid_));
    } else if (OB_ISNULL(sess_info)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session info is NULL", K(conn.sessid_));
    } else {
      sess_info->set_session_state(sql::SESSION_KILLED);
      sess_info->set_mark_killed(true);
    }
  }
  LOG_INFO("kill and revert session", K(conn.sessid_), K(ret));
  return ret;
}

ObSMConnectionCallback global_sm_conn_callback;
}; // end namespace mysql
}; // end namespace oceanbase
