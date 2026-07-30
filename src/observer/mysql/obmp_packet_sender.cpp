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
#include "obmp_packet_sender.h"
#include "rpc/obmysql/packet/ompk_error.h"
#include "rpc/obmysql/packet/ompk_eof.h"
#include "rpc/obmysql/packet/ompk_ok.h"
#include "observer/mysql/obmp_utils.h"
#include "observer/mysql/ob_mysql_result_set.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <ctime>

namespace oceanbase
{
using namespace share;
using namespace rpc;
using namespace sql;
using namespace obmysql;
using namespace common;
using namespace transaction;
using namespace share::schema;

namespace observer
{
void ObOKPParam::reset()
{
  affected_rows_ = 0;
  message_ = NULL;
  is_on_connect_ = false;
  is_on_change_user_ = false;
  has_more_result_ = false;
  take_trace_id_to_client_ = false;
  warnings_count_ = 0;
  lii_ = 0;
}

int64_t ObOKPParam::to_string(char *buf, const int64_t buf_len) const
{
  int64_t pos = 0;
  J_KV(KT_(affected_rows),
       K_(is_on_connect),
       K_(take_trace_id_to_client),
       K_(has_more_result),
       K_(lii));
  return pos;
}

ObMPPacketSender::ObMPPacketSender()
    : req_(NULL),
      seq_(0),
      read_handle_(NULL),
      comp_context_(),
      ez_buf_(NULL),
      conn_valid_(true),
      sessid_(0),
      req_has_wokenup_(true),
      query_receive_ts_(0),
      conn_(NULL)
{
}

ObMPPacketSender::~ObMPPacketSender()
{
  reset();
}

void ObMPPacketSender::reset()
{
  (void)release_read_handle();

  req_ = NULL;
  seq_ = 0;
  comp_context_.reset();
  ez_buf_ = NULL;
  conn_valid_ = true;
  sessid_ = 0;
  req_has_wokenup_ = true;
  query_receive_ts_ = 0;
  conn_ = NULL;
}

int ObMPPacketSender::init(rpc::ObRequest *req)
{
  int ret = OB_SUCCESS;
  if (NULL == req) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    uint8_t pkt_seq = reinterpret_cast<const ObMySQLRawPacket &>(req->get_packet()).get_seq();
    bool is_conn_valid = true;
    bool req_has_wokenup = false;
    int64_t receive_ts = req->get_receive_timestamp();
    ret = do_init(req, pkt_seq + 1, pkt_seq + 1, is_conn_valid, req_has_wokenup, receive_ts);
  }
  return ret;
}

int ObMPPacketSender::clone_from(ObMPPacketSender& that, int64_t com_offset)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(do_init(that.req_, that.seq_, that.comp_context_.seq_ + com_offset,
                      that.conn_valid_, that.req_has_wokenup_, that.query_receive_ts_))) {
    SERVER_LOG(ERROR, "clone packet sender fail", K(ret));
  }
  return ret;
}

int ObMPPacketSender::do_init(rpc::ObRequest *req,
                           uint8_t packet_seq,
                           uint8_t comp_seq,
                           bool conn_status,
                           bool req_has_wokenup,
                           int64_t query_receive_ts)
{
  int ret = OB_SUCCESS;
  req_ = req;
  ObSMConnection *conn = get_conn();
  if (OB_ISNULL(req) || OB_ISNULL(conn)) {
    ret = OB_ERR_NULL_VALUE;
    SERVER_LOG(WARN, "invalid argument", K(ret),
               K(req), K(conn_status), K(packet_seq), K(conn));
  } else {
    // memory for ok eof error packet
    seq_ = packet_seq;
    conn_valid_ = conn_status;
    req_has_wokenup_ = req_has_wokenup;
    query_receive_ts_ = query_receive_ts;
    conn_ = conn;

    sessid_ = conn->sessid_;
    // init comp_context
    comp_context_.reset();
    comp_context_.type_ = conn->get_compress_type();
    comp_context_.seq_ = comp_seq;
    comp_context_.sessid_ = sessid_;
    comp_context_.conn_ = conn;

  }
  return ret;
}

int ObMPPacketSender::alloc_ezbuf()
{
  int ret = OB_SUCCESS;
  char *buf = nullptr;
  if (OB_ISNULL(ez_buf_)) {
    if (OB_ISNULL(req_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("request or data buffer is null", KP(req_), K(ret));
    } else if (OB_ISNULL(buf = (char*)SQL_REQ_OP.alloc_sql_response_buffer(req_, OB_MULTI_RESPONSE_BUF_SIZE + sizeof(easy_buf_t)))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("fail to easy_alloc",
                "size", OB_MULTI_RESPONSE_BUF_SIZE + sizeof(easy_buf_t), K(ret));
    } else {
      ez_buf_ = reinterpret_cast<easy_buf_t*>(buf);
      init_easy_buf(ez_buf_, reinterpret_cast<char*>(ez_buf_ + 1),
                    nullptr, OB_MULTI_RESPONSE_BUF_SIZE);
    }
  }
  return ret;
}

int ObMPPacketSender::read_packet(obmysql::ObICSMemPool &mem_pool, obmysql::ObMySQLPacket *&mysql_pkt)
{
  int ret = OB_SUCCESS;
  mysql_pkt = NULL;

  if (OB_ISNULL(read_handle_)) {
    ret = init_read_handle();
  }

  if (OB_SUCC(ret)) {
    rpc::ObPacket *pkt = NULL;
    ret = SQL_REQ_OP.read_packet(req_, mem_pool, read_handle_, pkt);
    if (pkt != NULL) {
      mysql_pkt = static_cast<obmysql::ObMySQLPacket *>(pkt);
      const uint8_t seq = mysql_pkt->get_seq();
      seq_ = seq + 1;

      if (comp_context_.use_compress()) {
        comp_context_.seq_ = comp_context_.conn_->compressed_pkt_context_.last_pkt_seq_ + 1;
      }
    }
  }
  return ret;
}

int ObMPPacketSender::release_packet(obmysql::ObMySQLPacket* pkt)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(read_handle_)) {
    ret = OB_NOT_INIT;
  } else {
    ret = SQL_REQ_OP.release_packet(req_, read_handle_, static_cast<rpc::ObPacket*>(pkt));
  }
  return ret;
}

int ObMPPacketSender::response_packet(obmysql::ObMySQLPacket &pkt, sql::ObSQLSessionInfo* session)
{
  LOG_DEBUG("response-packet", K(lbt()));
  int ret = OB_SUCCESS;
  if (!conn_valid_) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret));
  }

  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(alloc_ezbuf())) {
    LOG_ERROR("easy buffer alloc failed", K(ret));
  } else {
    int64_t seri_size = 0;
    pkt.set_seq(seq_);
    if (OB_FAIL(try_encode_with(pkt,
                                ez_buf_->end - ez_buf_->pos,
                                seri_size,
                                0))) {
      LOG_WARN("failed to encode packet", K(ret));
    } else {
      LOG_DEBUG("succ encode packet", K(pkt), K(seri_size));
      seq_ = pkt.get_seq(); // here will point next avail seq
    }
  }

  if (OB_ISNULL(session)) {
    // do nothing
  } else {
    // reset
    session->get_extra_info_alloc().reset();
  }
  return ret;
}

int ObMPPacketSender::send_error_packet(int err,
                                        const char* errmsg,
                                        void *extra_err_info /* = NULL */)
{
  int ret = OB_SUCCESS;
  sql::ObSQLSessionInfo *session = NULL;
  BACKTRACE(ERROR, (OB_SUCCESS == err), "BUG send error packet but err code is 0");
  {
    int client_error = common::ob_mysql_errno(err);
    // OB error codes that are not compatible with mysql will be displayed using
    // OB error codes
    client_error = client_error == -1 ? err : client_error;
    LOG_INFO("sending error packet", "ob_error", err, "client error", client_error,
            K(extra_err_info), K(lbt()));
  }
  OMPKError epacket;
  ObSqlString fin_msg;
  if (OB_SUCCESS == err) {
    // @BUG work around
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("error code is incorrect", K(err));
  } else if (!conn_valid_ || OB_ISNULL(conn_)) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret), KP(conn_));
  } else if (OB_FAIL(alloc_ezbuf())) {
    LOG_WARN("ez_buf_ alloc failed", K_(ez_buf), K(ret));
  } else {
    // error message
    ObString message;
    const int32_t MAX_MSG_BUF_SIZE = 512;
    char msg_buf[MAX_MSG_BUF_SIZE];
    const ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer();
    if (NULL != wb
        && (wb->get_err_code() == err
            || (err >= OB_MIN_RAISE_APPLICATION_ERROR
                && err <= OB_MAX_RAISE_APPLICATION_ERROR))) {
      message = wb->get_err_msg();
    }
    if (message.length() <= 0) {
      if (OB_LIKELY(NULL != errmsg) && 0 < strlen(errmsg)) {
        message = ObString::make_string(errmsg);
      } else if (err >= OB_MIN_RAISE_APPLICATION_ERROR
                 && err <= OB_MAX_RAISE_APPLICATION_ERROR) {
        // do nothing ...
      } else {
        snprintf(msg_buf, MAX_MSG_BUF_SIZE, "%s", ob_errpkt_strerror(err));
        message = ObString::make_string(msg_buf); // default error message
      }
    }
    if (OB_SUCC(ret)) {
      ObArenaAllocator allocator(ObMemAttr("WARN_MSG"));
      ObString new_message = message;
      ObCollationType client_cs_type = CS_TYPE_UTF8MB4_BIN;

      if (OB_UNLIKELY(OB_SUCCESS != get_session(session))) {
        session = NULL;
      } else {
        client_cs_type = session->get_local_collation_connection();
        if (OB_UNLIKELY(OB_SUCCESS != ObCharset::charset_convert(allocator,
                                                                 message,
                                                                 CS_TYPE_UTF8MB4_BIN,
                                                                 client_cs_type,
                                                                 new_message,
                                                                 ObCharset::REPLACE_UNKNOWN_CHARACTER))) {
        } else {
          int64_t length = MIN(new_message.length(), MAX_MSG_BUF_SIZE);
          MEMCPY(msg_buf, new_message.ptr(), length);
          message.assign_ptr(msg_buf, length);
        }
      }
    }

    if (ObServerConfig::get_instance().enable_rich_error_msg) {
      // During the test process, if accessing the OceanBase cluster through a proxy,
      // Often it is unknown which observer the sql was sent to,
      // Need to query the proxy logs first, carefully compare the SQL text and error codes to confirm, which is very inefficient.
      // After adding this feature, it can be achieved through
      //  1. ip:port directly locates to observer
      //  2. Then time-stamp to the log file
      //  3. Finally locate the log directly through the trace id

      int32_t msg_buf_size = 0;

      struct timeval tv;
      struct tm tm;
#ifdef _WIN32
      {
        FILETIME ft;
        GetSystemTimePreciseAsFileTime(&ft);
        ULARGE_INTEGER uli;
        uli.LowPart = ft.dwLowDateTime;
        uli.HighPart = ft.dwHighDateTime;
        uint64_t us = (uli.QuadPart - 116444736000000000ULL) / 10;
        tv.tv_sec = static_cast<long>(us / 1000000);
        tv.tv_usec = static_cast<long>(us % 1000000);
      }
      {
        time_t sec = tv.tv_sec;
        localtime_s(&tm, &sec);
      }
#else
      (void)gettimeofday(&tv, NULL);
      ::localtime_r((const time_t *)&tv.tv_sec, &tm);
#endif

      char tmp_msg_buf[MAX_MSG_BUF_SIZE];
      strncpy(tmp_msg_buf, message.ptr(), message.length()); // msg_buf is overwriten
      char trace_id_buf[OB_MAX_TRACE_ID_BUFFER_SIZE] = {'\0'};
      msg_buf_size = snprintf(msg_buf, MAX_MSG_BUF_SIZE,
                           "%.*s\n"
                           "[%04d-%02d-%02d %02d:%02d:%02d.%06ld] "
                           "[%s]",
                           message.length(), tmp_msg_buf,
                           tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                           tm.tm_hour, tm.tm_min, tm.tm_sec, tv.tv_usec,
                           ObCurTraceId::get_trace_id_str(trace_id_buf, sizeof(trace_id_buf)));
      (void) msg_buf_size; // make compiler happy
      message = ObString::make_string(msg_buf); // default error message
    }

    // TODO Negotiate a err for rerouting sql
    if (OB_SP_RAISE_APPLICATION_ERROR == err) {
      epacket.set_errcode(static_cast<uint16_t>(wb->get_err_code()));
      if (strlen(wb->get_sql_state()) == 0) {
        if (OB_FAIL(epacket.set_sqlstate(ob_sqlstate(err)))) {
          LOG_WARN("set sql_state failed", K(ret));
        }  
      } else if (OB_FAIL(epacket.set_sqlstate(wb->get_sql_state()))) {
        LOG_WARN("set sql_state failed", K(ret));
      }
    } else {
      epacket.set_errcode(static_cast<uint16_t>(ob_errpkt_errno(err)));
      if (OB_FAIL(epacket.set_sqlstate(ob_sqlstate(err)))) {
        LOG_WARN("set sql_state failed", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else {
      if (OB_FAIL(fin_msg.append(message))) {
        LOG_WARN("append pl exact err msg fail", K(ret), K(message));
      } else if (has_pl()) {
        if (NULL == session && OB_FAIL(get_session(session))) {
          LOG_WARN("fail to get session", K(ret));
        } else if (OB_ISNULL(session)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("sql session info is null", K(ret));
        } else {
          ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
        }
      } else { /* do nothing */ }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(epacket.set_message(fin_msg.string()))) {
        LOG_WARN("failed to set error message", K(ret));
      } else {
        // do nothing
      }
    }
    if (OB_FAIL(ret)) {
      ret = OB_ERR_UNEXPECTED;
      RPC_LOG(WARN, "failed to set error info", K(err), K(errmsg), K(ret));
    }
  }

  // rollback autocommit transaction which is active
  if (OB_SUCC(ret) && conn_valid_ && conn_->is_sess_alloc_ && !conn_->is_sess_free_) {
    transaction::ObTransID trans_id;
    if (OB_ISNULL(session) && OB_FAIL(get_session(session))) {
      LOG_WARN("get session failed", K(ret));
    } else {
      ObSQLSessionInfo::LockGuard lock_guard(session->get_query_lock());
      if (session->has_active_autocommit_trans(trans_id)) {
        bool need_disconnect = false;
        if (OB_FAIL(ObSqlTransControl::rollback_trans(session, need_disconnect))) {
          LOG_WARN("rollback autocommit trans failed", K(ret), K(need_disconnect));
        } else {
          LOG_INFO("rollback autocommit trans succeed", K(trans_id));
        }
      }
    }
  }
  // TODO: Should separate the logic below from send_error_packet because connect fails,
  // Also need to call send_error_packet, but there is no session at this time
  if (conn_valid_) {
    if (OB_FAIL(ret)) {
      disconnect();
    } else {
      if (OB_FAIL(response_packet(epacket, session))) {
        RPC_LOG(WARN, "failed to send error packet", K(epacket), K(ret));
        disconnect();
      } else if (has_pl() && NULL != session && 0 < session->get_pl_exact_err_msg().length()) {
        session->get_pl_exact_err_msg().reset();
      }
    }
  }
  if (OB_LIKELY(NULL != session)) {
    revert_session(session);
  }
  return ret;
}

int ObMPPacketSender::revert_session(ObSQLSessionInfo *sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.session_mgr_) || OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("session mgr or session info is null", K_(GCTX.session_mgr), K(sess_info), K(ret));
  } else {
    GCTX.session_mgr_->revert_session(sess_info);
  }
  return ret;
}

int ObMPPacketSender::get_session(ObSQLSessionInfo *&sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn_) || OB_ISNULL(GCTX.session_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn or sessoin mgr is NULL", K(ret), KP(conn_), K(GCTX.session_mgr_));
  } else if (OB_FAIL(GCTX.session_mgr_->get_session(conn_->sessid_, sess_info))) {
    LOG_WARN("get session fail", K(ret), "sessid", conn_->sessid_);
  } else {
    NG_TRACE_EXT(session, OB_ID(sid), sess_info->get_server_sid());
  }
  return ret;
}

int ObMPPacketSender::send_ok_packet(ObSQLSessionInfo &session, ObOKPParam &ok_param, obmysql::ObMySQLPacket* pkt)
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("send-ok-packet", K(lbt()));
  ObSQLSessionInfo::LockGuard lock_guard(session.get_query_lock());
  OMPKOK okp;
  if (!conn_valid_ || OB_ISNULL(conn_)) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret), KP(conn_));
  } else {
    okp.set_affected_rows(ok_param.affected_rows_);
    if (OB_UNLIKELY(NULL != ok_param.message_)) {
      ObString message_str;
      message_str.assign_ptr(ok_param.message_, static_cast<int32_t>(strlen(ok_param.message_)));
      okp.set_message(message_str);
    }
    okp.set_capability(session.get_capability());
    okp.set_last_insert_id(ok_param.lii_);
    okp.set_warnings(ok_param.warnings_count_);

    if (OB_SUCC(ret)) {
      if (!ok_param.take_trace_id_to_client_) {
        const int64_t elapsed_time = ObClockGenerator::getClock() - query_receive_ts_;
        bool is_slow = (elapsed_time > GCONF.trace_log_slow_query_watermark);
        ok_param.take_trace_id_to_client_ = is_slow;
      }
      if (ok_param.take_trace_id_to_client_) {
        ObCurTraceId::TraceId *trace_id = ObCurTraceId::get_trace_id();
        if (NULL != trace_id) {
          session.set_last_trace_id(trace_id);
        }
      }
    }
  }

  bool need_track_session_info = false;
  if (OB_SUCC(ret)) {
    if (session.is_track_session_info()) {
      // in hand shake response's ok packet, return all session variable
      if (ok_param.is_on_connect_ || ok_param.is_on_change_user_) {
        need_track_session_info = true;
        okp.set_use_standard_serialize(true);
      } else {
        if (!ok_param.has_more_result_) {
          need_track_session_info = true;
          if (OB_SUCC(ret)) {
            okp.set_use_standard_serialize(true);
            if (OB_FAIL(ObMPUtils::add_changed_session_info(okp, session))) {
              SERVER_LOG(WARN, "fail to add changed session info", K(ret));
            }
          }
        }
      }
    }
  }


  if (OB_SUCC(ret)) {
    bool ac = true;
    bool is_no_backslash_escapes = false;
    IS_NO_BACKSLASH_ESCAPES(session.get_sql_mode(), is_no_backslash_escapes);
    if (OB_FAIL(session.get_autocommit(ac))) {
      LOG_WARN("fail to get autocommit", K(ret));
    } else {
      ObServerStatusFlags flags = okp.get_server_status();
      flags.status_flags_.OB_SERVER_STATUS_IN_TRANS
        = (session.is_server_status_in_transaction() ? 1 : 0);
      flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = (ac ? 1 : 0);
      flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = ok_param.has_more_result_;
      flags.status_flags_.OB_SERVER_STATUS_NO_BACKSLASH_ESCAPES = is_no_backslash_escapes;
      flags.status_flags_.OB_SERVER_STATUS_CURSOR_EXISTS = ok_param.cursor_exist_ ? 1 : 0;
      flags.status_flags_.OB_SERVER_STATUS_LAST_ROW_SENT = ok_param.send_last_row_ ? 1 : 0;
      flags.status_flags_.OB_SERVER_PS_OUT_PARAMS = ok_param.has_pl_out_ ? 1 : 0;
      if (ok_param.is_on_connect_) {
        flags.status_flags_.OB_SERVER_STATUS_RESERVED = 0;
      }
      // todo: set OB_SERVER_STATUS_IN_TRANS_READONLY flag if need?
      okp.set_server_status(flags);
    }
  }

  if (OB_SUCC(ret) && OB_FAIL(alloc_ezbuf())) {
    LOG_WARN("ez_buf_ alloc failed", K(ret));
  }

  if (OB_SUCC(ret)) {
    // for ok packet which has no status packet
    if (NULL == pkt) {
      if (OB_FAIL(response_packet(okp, &session))) {
        LOG_WARN("response ok packet fail", K(ret));
      }
    } else {
      if (OB_FAIL(response_packet(*pkt, &session))) {
        LOG_WARN("response ok packet fail", K(pkt), K(ret));
      }
    }
  }
  if (need_track_session_info) {
    session.reset_session_changed_info();
  }
  // if somethig wrong during send_ok_paket, disconnect
  if (OB_FAIL(ret)) {
    disconnect();
  }

  return ret;
}

ObSMConnection* ObMPPacketSender::get_conn() const
{
  ObSMConnection *conn = NULL;
  if (OB_ISNULL(req_)) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "request is null");
  } else if (conn_valid_) {
    conn = reinterpret_cast<ObSMConnection *>(SQL_REQ_OP.get_sql_session(req_));
  } else {
    // do nothing
  }
  return conn;
}

void ObMPPacketSender::force_disconnect()
{
  LOG_WARN_RET(OB_ERROR, "force disconnect", K(lbt()));
  ObMPPacketSender::disconnect();
}

void ObMPPacketSender::disconnect()
{
  if (conn_valid_) {
    if (OB_ISNULL(req_)) {// do nothing
    } else {
      ObSMConnection *conn = reinterpret_cast<ObSMConnection *>(SQL_REQ_OP.get_sql_session(req_));
      if (conn != NULL) {
        LOG_WARN_RET(OB_SUCCESS, "server close connection",
                 "sessid", conn->sessid_,
                 "stack", lbt());
        sql::ObSQLSessionInfo *session = NULL;
        get_session(session);
        if (OB_ISNULL(session)) {
          LOG_WARN_RET(OB_ERR_UNEXPECTED, "session is null");
        } else {
          // set SERVER_FORCE_DISCONNECT state.
          session->set_disconnect_state(SERVER_FORCE_DISCONNECT);
          revert_session(session);
        }
      } else {
        LOG_WARN_RET(OB_ERR_UNEXPECTED, "connection is null");
      }

      SQL_REQ_OP.disconnect_sql_conn(req_);
    }
  }
}

int ObMPPacketSender::send_eof_packet(const ObSQLSessionInfo &session,
                                      const ObMySQLResultSet &result,
                                      ObOKPParam *ok_param)
{
  int ret = OB_SUCCESS;
  OMPKEOF eofp;
  const ObWarningBuffer *warnings_buf = common::ob_get_tsi_warning_buffer();
  uint16_t warning_count = 0;
  bool ac = true;
  if (OB_ISNULL(warnings_buf)) {
    // ignore ret
    LOG_WARN("can not get thread warnings buffer");
  } else {
    warning_count = static_cast<uint16_t>(warnings_buf->get_readable_warning_count());
  }
  eofp.set_warning_count(warning_count);
  ObServerStatusFlags flags = eofp.get_server_status();
  if (session.is_server_status_in_transaction()) {
    flags.status_flags_.OB_SERVER_STATUS_IN_TRANS = 1;
  }
  if (ac) {
    flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT = 1;
  }
  flags.status_flags_.OB_SERVER_MORE_RESULTS_EXISTS = result.has_more_result();
  eofp.set_server_status(flags);
  if (OB_ISNULL(ok_param)) {
    if (OB_FAIL(response_packet(eofp, const_cast<ObSQLSessionInfo*>(&session)))) {
      LOG_WARN("response packet fail", K(ret));
    }
  } else {
    if (OB_FAIL(send_ok_packet(*const_cast<ObSQLSessionInfo*>(&session), *ok_param, &eofp))) {
      LOG_WARN("failed to response ok packet and eof packet", K(ret), K(*ok_param));
    }
  }
  return ret;
}

int ObMPPacketSender::try_encode_with(ObMySQLPacket &pkt,
                                      int64_t current_size,
                                      int64_t &seri_size,
                                      int64_t try_steps,
                                      bool is_composed_ok_pkt)
{
  int ret = OB_SUCCESS;
  seri_size = 0;
  if (OB_ISNULL(ez_buf_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("easy buffer is null", K(ret));
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_UNLIKELY(!conn_valid_)) {
    // force core dump in debug
    ret = OB_CONNECT_ERROR;
    LOG_ERROR("connection already disconnected", K(ret));
  } else if (OB_UNLIKELY(req_has_wokenup_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("req_has_wokenup, resource maybe has destroy", K(ret));
  } else if (OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("invalid input", K(ret), KP(req_));
  } else {
    ObEasyBuffer easy_buffer(*ez_buf_);
    int encode_ret = pkt.encode(easy_buffer.last(), easy_buffer.write_avail_size(), seri_size);
    if (OB_SUCC(encode_ret)) {
      easy_buffer.write(seri_size);
    } else {
      // buffer too small - try flush or resize
      int last_ret = encode_ret;
      seri_size = 0;
      if (need_flush_buffer() && !is_composed_ok_pkt) {
        // try again with same buf size
        if (OB_FAIL(flush_buffer(false))) {
          LOG_WARN("failed to flush_buffer", K(ret), K(last_ret));
        } else {
          ret = try_encode_with(pkt, current_size, seri_size, try_steps, is_composed_ok_pkt);
        }
      } else {
        if (try_steps >= MAX_TRY_STEPS) {
          ret = last_ret;
          LOG_ERROR("encode MySQL packet fail", "buffer size", ez_buf_->end - ez_buf_->last, K(ret));
        } else {
          // try again with larger buf size
          const int64_t new_alloc_size = TRY_EZ_BUF_SIZES[try_steps++];
          if (OB_SIZE_OVERFLOW != last_ret && OB_BUF_NOT_ENOUGH != last_ret) {
            ret = last_ret;
            LOG_WARN("last_ret is not size overflow, need check code", K(last_ret));
          } else {
            if (OB_FAIL(resize_ezbuf(new_alloc_size))) {
              LOG_ERROR("fail to resize_ezbuf", K(ret), K(last_ret));
            } else {
              ret = try_encode_with(pkt, new_alloc_size, seri_size, try_steps, is_composed_ok_pkt);
            }
          }
        }
      }
    }
  }
  return ret;
}

bool ObMPPacketSender::need_flush_buffer() const
{
  bool bret = false;
  //there is data in buf
  if (NULL != ez_buf_ && ez_buf_->pos != ez_buf_->last) {
    bret = true;
  }
  return bret;
}

int ObMPPacketSender::flush_buffer(const bool is_last)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!conn_valid_)) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection in error, maybe has disconnected", K(ret));
  } else if (req_has_wokenup_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("req_has_wokenup, resource maybe has destroy", K(ret));
  } else if (OB_ISNULL(req_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("req_ is null", KP_(ez_buf), KP_(req), K(ret));
  } else if (OB_FAIL(alloc_ezbuf())) {
    LOG_ERROR("easy buffer alloc failed", KP_(ez_buf), K(ret));
  } else {
    //int64_t buf_sz = ez_buf_->last - ez_buf_->pos;
    if (OB_SUCCESS != ret) {
    } else if (comp_context_.use_compress()) {
      ObEasyBuffer orig_send_buf(*ez_buf_);
      if (OB_FAIL(ObMySQLRequestUtils::flush_compressed_buffer(is_last, comp_context_, orig_send_buf, *req_))) {
        LOG_WARN("failed to flush buffer for compressed sql nio", K(ret));
      }
    } else {
      if (is_last) {
        if (OB_FAIL(SQL_REQ_OP.async_write_response(req_, ez_buf_->pos, ez_buf_->last - ez_buf_->pos))) {
          LOG_WARN("write response fail", K(ret));
        }
      } else {
        if (OB_FAIL(SQL_REQ_OP.write_response(req_, ez_buf_->pos, ez_buf_->last - ez_buf_->pos))) {
          LOG_WARN("write response fail", K(ret));
        } else {
          init_easy_buf(ez_buf_, (char*)(ez_buf_ + 1), NULL, ez_buf_->end - ez_buf_->pos);
        }
      }
    }

    if (OB_SUCC(ret)) {
      /*
      ObSQLSessionInfo *sess = nullptr;
      if (OB_FAIL(get_session(sess))) {
        LOG_WARN("fail to get session info", K(ret));
      } else if (OB_ISNULL(sess)) {
        // do nothing
      } else {
        sess->inc_out_bytes(buf_sz);
      }
      if (OB_NOT_NULL(sess)) {
        GCTX.session_mgr_->revert_session(sess);
      }*/
    }
  }
  if (is_last) {
    finish_sql_request();
  }
  return ret;
}

int ObMPPacketSender::get_conn_id(uint32_t &sessid) const
{
  int ret = OB_SUCCESS;
  ObSMConnection *conn = get_conn();
  if (OB_ISNULL(conn)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get connection fail");
  } else {
    sessid = conn->sessid_;
  }
  return ret;
}

int ObMPPacketSender::resize_ezbuf(const int64_t size)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(req_)
      || OB_ISNULL(ez_buf_)
      || OB_UNLIKELY(size < static_cast<int64_t>(sizeof(easy_buf_t)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid input size", K(size), KP_(req), KP_(ez_buf), K(ret));
  } else {
    const int64_t remain_data_size = ez_buf_->last - ez_buf_->pos;
    char *buf = NULL;
    if (size - sizeof(easy_buf_t) < remain_data_size) {
      ret = OB_SUCCESS;
      LOG_WARN("new size is too little, need alloc bigger", K(remain_data_size), K(size));
    } else if (OB_ISNULL(buf = (char*)SQL_REQ_OP.alloc_sql_response_buffer(req_, size))) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
      LOG_ERROR("allocate memory failed", K(size), K(ret));
      disconnect();
    } else {
      easy_buf_t *tmp = reinterpret_cast<easy_buf_t *>(buf);
      init_easy_buf(tmp, reinterpret_cast<char *>(tmp + 1),
                    nullptr, size - sizeof(easy_buf_t));
      if (remain_data_size > 0) {
        //if ezbuf has leave data, we need move it
        MEMCPY(tmp->last, ez_buf_->pos, remain_data_size);
        tmp->last = tmp->pos + remain_data_size;
        ez_buf_->last = ez_buf_->pos;
      }
      ez_buf_ = tmp;
    }
  }
  return ret;
}

void ObMPPacketSender::finish_sql_request()
{
  if (conn_valid_ && !req_has_wokenup_) {
    (void)release_read_handle();
    SQL_REQ_OP.finish_sql_request(req_);
    req_has_wokenup_ = true;
    ez_buf_ = NULL;
  }
}

int ObMPPacketSender::clean_buffer()
{
  int ret = OB_SUCCESS;
  CK (OB_NOT_NULL(ez_buf_));
  if (OB_SUCC(ret)) {
    ez_buf_->last = reinterpret_cast<char*>(ez_buf_ + 1);
    ez_buf_->pos = reinterpret_cast<char*>(ez_buf_ + 1);
    seq_ = 1;
  }
  return ret;
}

bool ObMPPacketSender::has_pl()
{
  bool has_pl = false;
  const obmysql::ObMySQLRawPacket &pkt = reinterpret_cast<const obmysql::ObMySQLRawPacket&>(req_->get_packet());
  if (obmysql::COM_STMT_PREPARE == pkt.get_cmd()
        || obmysql::COM_STMT_EXECUTE == pkt.get_cmd()
        || obmysql::COM_QUERY == pkt.get_cmd()
        || obmysql::COM_STMT_FETCH == pkt.get_cmd()) {
    has_pl = true;
  }
  return has_pl;
}

// current max_allow_packet_size <= 1G, TODO haozheng
int64_t ObMPPacketSender::TRY_EZ_BUF_SIZES[] = {64*1024, 128*1024, 2*1024*1024 - 1024, 4*1024*1024 - 1024,
                                                64*1024*1024 - 1024, 128*1024*1024, 512*1024*1024, 1024*1024*1024};

int ObMPPacketSender::init_read_handle()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(read_handle_)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ret = SQL_REQ_OP.create_read_handle(req_, read_handle_);
    LOG_DEBUG("create read handle", KP(req_), KP_(read_handle));
  }
  return ret;
}

int ObMPPacketSender::release_read_handle()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(read_handle_)) {
    LOG_DEBUG("release read handle", KP(req_), KP_(read_handle));
    ret = SQL_REQ_OP.release_read_handle(req_, read_handle_);
    read_handle_ = NULL;
  }
  return ret;
}

}; // end namespace observer
}; // end namespace oceanbase
