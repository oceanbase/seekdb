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

#include "ob_mysql_end_trans_cb.h"
#include "obmp_stmt_send_piece_data.h"
using namespace oceanbase::common;
using namespace oceanbase::obmysql;
namespace oceanbase
{
namespace observer
{

ObSqlEndTransCb::ObSqlEndTransCb()
{
  reset();
}

ObSqlEndTransCb::~ObSqlEndTransCb()
{
  destroy();
}

int ObSqlEndTransCb::set_packet_param(const sql::ObEndTransCbPacketParam &pkt_param)
{
  int ret = OB_SUCCESS;
  if (!pkt_param.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "invalid copy", K(ret));
  } else {
    pkt_param_ = pkt_param; //! Copy semantics
  }
  return ret;
}

int ObSqlEndTransCb::init(ObMPPacketSender& packet_sender, 
                          sql::ObSQLSessionInfo *sess_info, 
                          int32_t stmt_id,
                          uint64_t params_num)
{
  int ret = OB_SUCCESS;
  if (IDLE != state_) {
    ret = OB_STATE_NOT_MATCH;
    LOG_ERROR("async mysql callback is still active", K(ret), K(state_));
  } else if (OB_ISNULL(sess_info)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("session info is NULL", K(ret));
  } else {
    packet_sender_.reset();
    sess_info_ = sess_info;
    stmt_id_ = stmt_id;
    params_num_ = params_num;
    if (OB_FAIL(packet_sender_.snapshot_from(packet_sender))) {
      LOG_WARN("failed to snapshot mysql request identity", K(ret));
      reset_callback_state();
    } else {
      state_ = ARMED;
    }
  }
  return ret;
}

int ObSqlEndTransCb::take_request_ownership(ObMPPacketSender& packet_sender)
{
  int ret = OB_SUCCESS;
  CallbackState next_state = IDLE;
  if (ARMED == state_) {
    next_state = OWNED_BLOCKED;
  } else if (CALLBACK_PENDING == state_) {
    next_state = OWNED_CALLBACK_PENDING;
  } else {
    ret = OB_STATE_NOT_MATCH;
    LOG_ERROR("invalid async mysql callback state during request handoff", K(ret), K(state_));
  }
  if (OB_SUCC(ret)) {
    if (OB_FAIL(packet_sender.handoff_request_to(packet_sender_))) {
      LOG_ERROR("failed to hand off async mysql request", K(ret), K(state_));
    } else {
      state_ = next_state;
    }
  }
  return ret;
}

int ObSqlEndTransCb::abort_request_handoff()
{
  int ret = OB_SUCCESS;
  if (ARMED == state_) {
    state_ = ABORTED_BLOCKED;
  } else if (CALLBACK_PENDING == state_) {
    state_ = ABORTED_CALLBACK_PENDING;
  } else {
    ret = OB_STATE_NOT_MATCH;
    LOG_ERROR("invalid async mysql callback state while aborting request handoff",
              K(ret), K(state_));
  }
  return ret;
}

int ObSqlEndTransCb::cancel_unsubmitted(bool &needs_cleanup)
{
  int ret = OB_SUCCESS;
  needs_cleanup = false;
  sql::ObSQLSessionInfo *session_info = sess_info_;
  if (OB_ISNULL(session_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("active async mysql callback has no session", K(ret), K(state_));
  } else {
    sql::ObSQLSessionInfo::LockGuard lock_guard(session_info->get_query_lock());
    if (ARMED == state_) {
      reset_callback_state();
    } else if (CALLBACK_PENDING == state_) {
      // This contradicts the driver's submission flag, but the callback owns
      // a session lease already. Defer its non-transport cleanup until the
      // processor's final cleanup hook instead of leaking or reverting early.
      ret = OB_STATE_NOT_MATCH;
      state_ = ABORTED_CALLBACK_PENDING;
      needs_cleanup = true;
      LOG_ERROR("end-trans callback completed but response was not marked submitted",
                K(ret), K(state_));
    } else {
      ret = OB_STATE_NOT_MATCH;
      LOG_ERROR("cannot cancel async mysql callback in current state", K(ret), K(state_));
    }
  }
  return ret;
}

void ObSqlEndTransCb::allow_request_completion()
{
  int ret = OB_SUCCESS;
  uint32_t sessid = 0;
  bool need_revert_session = false;
  sql::ObSQLSessionInfo *session_info = sess_info_;
  if (OB_ISNULL(session_info)) {
    ret = OB_ERR_NULL_VALUE;
    SERVER_LOG(ERROR, "session info is NULL while enabling async callback", K(ret), K(state_));
  } else {
    sql::ObSQLSessionInfo::LockGuard lock_guard(session_info->get_query_lock());
    if (OWNED_BLOCKED == state_) {
      state_ = OWNED_READY;
    } else if (OWNED_CALLBACK_PENDING == state_) {
      const int cb_param = pending_cb_param_;
      complete_callback_locked(cb_param, session_info, ret, sessid);
      need_revert_session = true;
    } else if (ABORTED_BLOCKED == state_) {
      state_ = ABORTED_READY;
    } else if (ABORTED_CALLBACK_PENDING == state_) {
      const int cb_param = pending_cb_param_;
      complete_aborted_callback_locked(cb_param, session_info, sessid);
      need_revert_session = true;
    } else {
      ret = OB_STATE_NOT_MATCH;
      LOG_ERROR("invalid async mysql callback state while enabling completion", K(ret), K(state_));
    }
  }
  ob_setup_tsi_warning_buffer(NULL);
  if (need_revert_session) {
    MEM_BARRIER();
    const int sret = packet_sender_.revert_session(session_info);
    if (OB_SUCCESS != sret) {
      SERVER_LOG_RET(ERROR, sret, "revert session fail", K(sessid), K(sret), K(ret), K(lbt()));
    }
  }
}

//cb_param : the error code from SQL engine
void ObSqlEndTransCb::callback(int cb_param)
{
  int ret = OB_SUCCESS;
  uint32_t sessid = 0;
  bool need_revert_session = false;
  bool clear_tsi_warning_buffer = true;
  sql::ObSQLSessionInfo *session_info = sess_info_;
  if (OB_ISNULL(session_info)) {
    ret = OB_ERR_NULL_VALUE;
    SERVER_LOG(ERROR, "session info is NULL", "ret", ret, K(session_info));
  } else {
    sql::ObSQLSessionInfo::LockGuard lock_guard(session_info->get_query_lock());
    if (ARMED == state_) {
      // ARMED is only observable by a callback synchronously re-entering the
      // worker's recursive query lock. Preserve that worker's TSI state until
      // the processor cleanup barrier.
      clear_tsi_warning_buffer = false;
      pending_cb_param_ = cb_param;
      state_ = CALLBACK_PENDING;
    } else if (OWNED_BLOCKED == state_) {
      pending_cb_param_ = cb_param;
      state_ = OWNED_CALLBACK_PENDING;
    } else if (OWNED_READY == state_) {
      complete_callback_locked(cb_param, session_info, ret, sessid);
      need_revert_session = true;
    } else if (ABORTED_BLOCKED == state_) {
      pending_cb_param_ = cb_param;
      state_ = ABORTED_CALLBACK_PENDING;
    } else if (ABORTED_READY == state_) {
      complete_aborted_callback_locked(cb_param, session_info, sessid);
      need_revert_session = true;
    } else {
      ret = OB_STATE_NOT_MATCH;
      LOG_ERROR("unexpected or duplicate async mysql callback", K(ret), K(state_), K(cb_param));
    }
  } /* end query_lock protection */

  // An asynchronous callback may return before another thread performs the
  // deferred session cleanup, so do not leave its TSI warning buffer attached.
  if (clear_tsi_warning_buffer) {
    ob_setup_tsi_warning_buffer(NULL);
  }

  if (need_revert_session) {
    MEM_BARRIER();
    const int sret = packet_sender_.revert_session(session_info);
    if (OB_SUCCESS != sret) {
      SERVER_LOG_RET(ERROR, sret, "revert session fail", K(sessid), K(sret), K(ret), K(lbt()));
    }
  }
}

void ObSqlEndTransCb::complete_callback_locked(int cb_param,
                                                sql::ObSQLSessionInfo *session_info,
                                                int &ret,
                                                uint32_t &sessid)
{
  sessid = session_info->get_server_sid();
  const bool reuse_tx = OB_SUCCESS == cb_param
      || OB_TRANS_COMMITED == cb_param
      || OB_TRANS_ROLLBACKED == cb_param;
  sql::ObSqlTransControl::reset_session_tx_state(session_info, reuse_tx);

  // Check these variables within the critical section to prevent adverse effects caused by concurrent callbacks
  if (OB_UNLIKELY(!pkt_param_.is_valid())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(ERROR, "pkt_param_ is invalid", K(ret), K(pkt_param_));
  } else if (FALSE_IT(ObCurTraceId::set(pkt_param_.get_trace_id()))) { // set trace_id as early as possible
    //do nothing
  } else if (!packet_sender_.is_conn_valid()) {
    //network problem, callback will still be called
    ret = OB_CONNECT_ERROR;
    SERVER_LOG(INFO, "connection is invalid", "ret", ret);
  } else {
    session_info->set_show_warnings_buf(cb_param);
    if (OB_SUCCESS == cb_param) {
      //ok pakcet
      ObOKPParam ok_param;
      ok_param.message_ = const_cast<char*>(pkt_param_.get_message());
      ok_param.affected_rows_ = pkt_param_.get_affected_rows();
      ok_param.lii_ = pkt_param_.get_last_insert_id_to_client();
      ok_param.warnings_count_ = static_cast<uint16_t>(
          session_info->get_warnings_buffer().get_readable_warning_count());
      if (OB_SUCCESS != (ret = packet_sender_.send_ok_packet(*session_info, ok_param))) {
        SERVER_LOG(WARN, "encode ok packet fail", K(ok_param), "ret", ret);
      }
    } else {
      //error + possible ok packet
      const char *error_msg = session_info->get_warnings_buffer().get_err_msg();
      if (OB_SUCCESS !=
          (ret = packet_sender_.send_error_packet(cb_param, error_msg))) {
        SERVER_LOG(WARN, "encode error packet fail", "ret", ret);
      }
    }
    //succ or not reset warning buffer
    session_info->reset_warnings_buf();
  }

  cleanup_session_locked(session_info);
  if (OB_SUCCESS == ret) {
    if (need_disconnect_) {
      packet_sender_.force_disconnect();
    }
    const bool is_last = true;
    const int flush_ret = packet_sender_.flush_buffer(is_last);
    if (OB_SUCCESS != flush_ret) {
      SERVER_LOG(WARN, "failed to flush async mysql response", K(flush_ret));
      ret = flush_ret;
    }
  } else {
    packet_sender_.force_disconnect();
    packet_sender_.finish_sql_request();
  }

  reset_callback_state();
  destroy();
}

void ObSqlEndTransCb::complete_aborted_callback_locked(
    int cb_param,
    sql::ObSQLSessionInfo *session_info,
    uint32_t &sessid)
{
  sessid = session_info->get_server_sid();
  const bool reuse_tx = OB_SUCCESS == cb_param
      || OB_TRANS_COMMITED == cb_param
      || OB_TRANS_ROLLBACKED == cb_param;
  sql::ObSqlTransControl::reset_session_tx_state(session_info, reuse_tx);
  session_info->set_show_warnings_buf(cb_param);
  session_info->reset_warnings_buf();
  cleanup_session_locked(session_info);

  // The original sender retained ownership when handoff failed. response()
  // has already finished that request, so this path only releases callback
  // state and its session lease.
  reset_callback_state();
  destroy();
}

void ObSqlEndTransCb::cleanup_session_locked(sql::ObSQLSessionInfo *session_info)
{
  ObPieceCache *piece_cache = session_info->get_piece_cache();
  if (OB_ISNULL(piece_cache)) {
    // do nothing
    // piece_cache not be null in piece data protocol
  } else {
    int piece_ret = OB_SUCCESS;
    for (uint64_t i = 0; OB_SUCCESS == piece_ret && i < params_num_; i++) {
      piece_ret = piece_cache->remove_piece(
                          piece_cache->get_piece_key(stmt_id_, i),
                          *session_info);
      if (OB_SUCCESS != piece_ret) {
        if (OB_HASH_NOT_EXIST == piece_ret) {
          piece_ret = OB_SUCCESS;
        } else {
          LOG_WARN_RET(piece_ret, "remove piece fail", K(stmt_id_), K(i), K(piece_ret));
        }
      }
    }
  }

  session_info->reset_cur_sql_id();
  session_info->reset_current_plan_hash();
  session_info->reset_current_plan_id();
  session_info->set_session_sleep();
}

void ObSqlEndTransCb::destroy()
{
}

void ObSqlEndTransCb::reset()
{
  reset_callback_state();
}

void ObSqlEndTransCb::reset_callback_state()
{
  packet_sender_.reset();
  sess_info_ = NULL;
  pkt_param_.reset();
  need_disconnect_ = false;
  stmt_id_ = 0;
  params_num_ = 0;
  state_ = IDLE;
  pending_cb_param_ = OB_SUCCESS;
}

} // end of namespace obmysql
} // end of namespace oceanbase
