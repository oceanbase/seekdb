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
#include "query/protocol/ob_mysql_packet_sender.h"
#include "nio.h"
#include "observer/mysql/ob_mysql_result_set.h"
#include "query/protocol/ob_mysql_rust_row.h"
#include "observer/mysql/obmp_utils.h"
#include "rpc/ob_sql_request_operator.h"
#include "rpc/obmysql/packet/ompk_auth_switch.h"
#include "share/rc/ob_server_runtime.h"
#include "rpc/obmysql/packet/ompk_error.h"
#include "rpc/obmysql/packet/ompk_eof.h"
#include "rpc/obmysql/packet/ompk_error.h"
#include "rpc/obmysql/packet/ompk_field.h"
#include "rpc/obmysql/packet/ompk_local_infile.h"
#include "rpc/obmysql/packet/ompk_ok.h"
#include "rpc/obmysql/packet/ompk_prepare.h"
#include "rpc/obmysql/packet/ompk_resheader.h"
#include "rpc/obmysql/packet/ompk_row.h"
#include "rpc/obmysql/packet/ompk_string.h"
#ifdef _WIN32
#include <windows.h>
#endif
#include <cstddef>
#include <ctime>
#include <type_traits>

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
namespace
{

static_assert(sizeof(nio_byte_view) == 16, "nio_byte_view ABI mismatch");
static_assert(sizeof(nio_mysql_kv_view) == 32, "nio_mysql_kv_view ABI mismatch");
static_assert(std::is_standard_layout<nio_mysql_kv_view>::value,
              "nio_mysql_kv_view must have standard layout");
static_assert(std::is_trivially_copyable<nio_mysql_kv_view>::value,
              "nio_mysql_kv_view must be trivially copyable");
static_assert(offsetof(nio_mysql_kv_view, key) == 0,
              "nio_mysql_kv_view.key ABI mismatch");
static_assert(offsetof(nio_mysql_kv_view, value) == 16,
              "nio_mysql_kv_view.value ABI mismatch");
static_assert(sizeof(nio_mysql_ok_view) == 96, "nio_mysql_ok_view ABI mismatch");
static_assert(offsetof(nio_mysql_ok_view, message) == 32,
              "nio_mysql_ok_view.message ABI mismatch");
static_assert(offsetof(nio_mysql_ok_view, system_vars) == 64,
              "nio_mysql_ok_view.system_vars ABI mismatch");
static_assert(sizeof(nio_mysql_field_view) == 136,
              "nio_mysql_field_view ABI mismatch");
static_assert(std::is_standard_layout<nio_mysql_field_view>::value,
              "nio_mysql_field_view must have standard layout");
static_assert(std::is_trivially_copyable<nio_mysql_field_view>::value,
              "nio_mysql_field_view must be trivially copyable");
static_assert(offsetof(nio_mysql_field_view, column_length) == 112,
              "nio_mysql_field_view.column_length ABI mismatch");
static_assert(offsetof(nio_mysql_field_view, field_type) == 120,
              "nio_mysql_field_view.field_type ABI mismatch");
static_assert(offsetof(nio_mysql_field_view, decimals) == 128,
              "nio_mysql_field_view.decimals ABI mismatch");
static_assert(static_cast<uint32_t>(MYSQL_TYPE_COMPLEX) == 160,
              "Rust Field encoder assumes the MySQL complex-type sentinel");
static_assert(static_cast<uint32_t>(MYSQL_TYPE_NOT_DEFINED) == 65535,
              "Rust Field encoder assumes the undefined default-type sentinel");

nio_byte_view make_byte_view(const ObString &value) {
  nio_byte_view view = {};
  view.data = value.ptr();
  view.len = value.length();
  return view;
}

nio_mysql_field_view make_field_view(const ObMySQLField &field) {
  nio_mysql_field_view view = {};
  view.schema = make_byte_view(field.dname_);
  view.table = make_byte_view(field.tname_);
  view.org_table = make_byte_view(field.org_tname_);
  view.name = make_byte_view(field.cname_);
  view.org_name = make_byte_view(field.org_cname_);
  view.type_owner = make_byte_view(field.type_owner_);
  view.type_name = make_byte_view(field.type_name_);
  view.column_length = static_cast<uint32_t>(field.length_);
  view.charset = field.charsetnr_;
  view.flags = field.flags_;
  view.field_type = static_cast<uint32_t>(field.type_);
  view.default_type = static_cast<uint32_t>(field.default_value_);
  view.decimals = static_cast<uint8_t>(field.accuracy_.get_scale());
  return view;
}

OB_NOINLINE int encode_field_packet(const OMPKField &field_packet,
                                    nio_connection_handle *connection_handle,
                                    const uint64_t generation,
                                    int64_t *framed_len) {
  const nio_mysql_field_view view = make_field_view(field_packet.get_field());
  return nio_response_append_field(connection_handle, generation, &view,
                                   framed_len);
}

OB_NOINLINE int encode_row_packet(const OMPKRow &row_packet,
                                  nio_connection_handle *connection_handle,
                                  const uint64_t generation,
                                  int64_t *framed_len, int &encode_ret) {
  int ret = OB_SUCCESS;
  int frame_ret = NIO_FRAME_ERROR;
  const ObMySQLRow &row = row_packet.get_row();
  if (row.is_packed()) {
    const char *blob = NULL;
    int64_t blob_len = 0;
    if (OB_FAIL(row.get_packed_row_blob(blob, blob_len))) {
      encode_ret = ret;
      LOG_WARN("failed to get Rust packed-row blob", K(ret));
    } else {
      frame_ret = nio_response_append_packed_row_blob(
          connection_handle, generation, blob, blob_len, framed_len);
    }
  } else {
    const int64_t cell_count = row.get_cells_count();
    if (OB_UNLIKELY(cell_count <= 0)) {
      encode_ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid MySQL Row cell count", K(cell_count));
    } else {
      ObArenaAllocator scratch_allocator(ObMemAttr("MySQLRowEncode"));
      ObSEArray<ObMySQLCellValue, 16> values;
      ObSEArray<nio_mysql_cell_view, 16> views;
      if (OB_FAIL(values.prepare_allocate(cell_count))) {
        encode_ret = ret;
        LOG_WARN("failed to allocate MySQL Row semantic values", K(ret),
                 K(cell_count));
      } else if (OB_FAIL(views.prepare_allocate(cell_count))) {
        encode_ret = ret;
        LOG_WARN("failed to allocate Rust MySQL Row views", K(ret),
                 K(cell_count));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < cell_count; ++i) {
          if (OB_FAIL(
                  row.build_cell_value(i, scratch_allocator, values.at(i)))) {
            LOG_WARN("failed to build MySQL Row semantic cell", K(ret), K(i));
          } else if (OB_FAIL(
                         query::build_nio_mysql_cell_view(values.at(i), views.at(i)))) {
            LOG_WARN("failed to build Rust MySQL Row cell view", K(ret), K(i));
          }
        }
        if (OB_FAIL(ret)) {
          encode_ret = ret;
        } else {
          nio_mysql_row_view view = {};
          view.cells = &views.at(0);
          view.cell_count = views.count();
          if (OB_FAIL(query::get_nio_mysql_row_protocol(row.get_protocol_type(),
                                                        view.protocol))) {
            encode_ret = ret;
            LOG_WARN("invalid MySQL Row protocol", K(ret), "protocol",
                     row.get_protocol_type());
          } else {
            frame_ret = nio_response_append_row(connection_handle, generation,
                                                &view, framed_len);
          }
        }
      }
    }
  }
  return frame_ret;
}

int append_ok_kv_views(const ObIArray<ObStringKV> &source,
                       ObIArray<nio_mysql_kv_view> &views) {
  int ret = OB_SUCCESS;
  for (int64_t i = 0; OB_SUCC(ret) && i < source.count(); ++i) {
    const ObStringKV &kv = source.at(i);
    nio_mysql_kv_view view = {};
    view.key = make_byte_view(kv.key_);
    view.value = make_byte_view(kv.value_);
    if (OB_FAIL(views.push_back(view))) {
      LOG_WARN("failed to build MySQL OK variable view", K(ret), K(i));
    }
  }
  return ret;
}

void fill_ok_view(const OMPKOK &ok, nio_mysql_ok_view &view) {
  view.affected_rows = ok.get_affected_rows();
  view.last_insert_id = ok.get_last_insert_id();
  view.capability_flags = ok.get_capability().capability_;
  if (ok.use_standard_serialize()) {
    view.behavior_flags |= NIO_MYSQL_OK_USE_STANDARD_SERIALIZE;
  }
  if (ok.is_schema_changed()) {
    view.behavior_flags |= NIO_MYSQL_OK_SCHEMA_CHANGED;
  }
  if (ok.is_state_changed()) {
    view.behavior_flags |= NIO_MYSQL_OK_STATE_CHANGED;
  }
  view.status_flags = ok.get_server_status().flags_;
  view.warnings = ok.get_warnings();
  view.message = make_byte_view(ok.get_message());
  view.changed_schema = make_byte_view(ok.get_changed_schema());
}

OB_NOINLINE int
encode_ok_packet_with_vars(const OMPKOK &ok, nio_mysql_ok_view &view,
                           nio_connection_handle *connection_handle,
                           const uint64_t generation,
                           int64_t *framed_len, int &encode_ret) {
  int ret = OB_SUCCESS;
  int frame_ret = NIO_FRAME_ERROR;
  const int64_t system_var_count = ok.get_system_vars().count();
  ObSEArray<nio_mysql_kv_view, 24> variable_views;
  if (OB_FAIL(variable_views.reserve(system_var_count))) {
    encode_ret = ret;
    LOG_WARN("failed to reserve MySQL OK variable views", K(ret),
             K(system_var_count));
  } else if (OB_FAIL(
                 append_ok_kv_views(ok.get_system_vars(), variable_views))) {
    encode_ret = ret;
    LOG_WARN("failed to build system variable views for MySQL OK", K(ret));
  } else {
    view.system_vars = 0 == system_var_count ? NULL : &variable_views.at(0);
    view.system_var_count = system_var_count;
    frame_ret = nio_response_append_ok(connection_handle, generation, &view,
                                       framed_len);
  }
  return frame_ret;
}

OB_NOINLINE int encode_ok_packet(const OMPKOK &ok,
                                 nio_connection_handle *connection_handle,
                                 const uint64_t generation, int64_t *framed_len,
                                 int &encode_ret) {
  nio_mysql_ok_view view = {};
  fill_ok_view(ok, view);
  const bool has_system_vars = !ok.get_system_vars().empty();
  return has_system_vars
             ? encode_ok_packet_with_vars(ok, view, connection_handle,
                                          generation, framed_len, encode_ret)
             : nio_response_append_ok(connection_handle, generation, &view,
                                      framed_len);
}

} // namespace

void ObOKPParam::reset() {
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
    : req_(NULL), request_generation_(0), nio_connection_handle_(NULL),
      mid_request_read_started_(false), read_packet_lease_(0),
      request_ownership_(RequestOwnership::RELEASED), response_started_(false),
      response_disabled_(true), query_receive_ts_(0), conn_(NULL) {}

ObMPPacketSender::~ObMPPacketSender()
{
  reset();
}

void ObMPPacketSender::reset()
{
  if (RequestOwnership::OWNED == request_ownership_) {
    const int ret = finish_sql_request();
    if (OB_SUCCESS != ret) {
      LOG_ERROR("failed to finish owned mysql request during sender reset",
                K(ret));
    }
  } else {
    (void)release_read_packet_lease();
  }
  clear_request_identity(RequestOwnership::RELEASED);
}

void ObMPPacketSender::clear_request_identity(RequestOwnership next_state) {
  req_ = NULL;
  request_generation_ = 0;
  nio_connection_handle_ = NULL;
  mid_request_read_started_ = false;
  read_packet_lease_ = 0;
  request_ownership_ = next_state;
  response_started_ = false;
  response_disabled_ = true;
  query_receive_ts_ = 0;
  conn_ = NULL;
}

int ObMPPacketSender::init(rpc::ObRequest *req)
{
  int ret = OB_SUCCESS;
  if (RequestOwnership::RELEASED != request_ownership_ &&
      RequestOwnership::DETACHED_FOR_RETRY != request_ownership_) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "packet sender already retains a request identity",
               K(ret));
  } else if (NULL == req) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    bool owns_request = true;
    int64_t receive_ts = req->get_receive_timestamp();
    ret = do_init(req, owns_request, receive_ts);
  }
  return ret;
}

int ObMPPacketSender::snapshot_from(ObMPPacketSender &that) {
  int ret = OB_SUCCESS;
  if (RequestOwnership::RELEASED != request_ownership_ || NULL != req_ ||
      !that.is_conn_valid()) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "invalid packet sender state while taking snapshot", K(ret));
  } else {
    // Capture a non-owning immutable lease. It becomes the callback's finish
    // lease only after async submission succeeds under the session query lock.
    req_ = that.req_;
    request_generation_ = that.request_generation_;
    nio_connection_handle_ = that.nio_connection_handle_;
    mid_request_read_started_ = false;
    read_packet_lease_ = 0;
    request_ownership_ = RequestOwnership::SNAPSHOT;
    response_started_ = that.response_started_;
    response_disabled_ = true;
    query_receive_ts_ = that.query_receive_ts_;
    conn_ = that.conn_;
  }
  return ret;
}

int ObMPPacketSender::handoff_request_to(ObMPPacketSender &target) {
  int ret = OB_SUCCESS;
  if (!is_conn_valid() ||
      RequestOwnership::SNAPSHOT != target.request_ownership_ ||
      target.req_ != req_ || target.conn_ != conn_ ||
      target.nio_connection_handle_ != nio_connection_handle_ ||
      target.request_generation_ != request_generation_) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(ERROR, "invalid packet sender request handoff", K(ret),
               K(request_generation_), K(target.request_generation_));
  } else {
    (void)release_read_packet_lease();
    // Response bytes and sequence state remain generation-scoped in Rust; only
    // C++ request ownership changes hands here.
    target.request_ownership_ = RequestOwnership::OWNED;
    target.response_started_ = response_started_;
    target.response_disabled_ = false;
    clear_request_identity(RequestOwnership::RELEASED);
  }
  return ret;
}

int ObMPPacketSender::do_init(rpc::ObRequest *req, bool owns_request,
                              int64_t query_receive_ts) {
  int ret = OB_SUCCESS;
  ObSMConnection *conn = NULL;
  nio_connection_handle *connection_handle = NULL;
  if (RequestOwnership::RELEASED != request_ownership_ &&
      RequestOwnership::DETACHED_FOR_RETRY != request_ownership_) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "packet sender already retains a request identity",
               K(ret));
  } else if (OB_ISNULL(req)) {
    ret = OB_ERR_NULL_VALUE;
    SERVER_LOG(WARN, "invalid argument", K(ret), K(req));
  } else if (OB_ISNULL(conn = SQL_REQ_OP.get_sql_session(req))) {
    ret = OB_ERR_NULL_VALUE;
    SERVER_LOG(WARN, "invalid argument", K(ret), K(req), K(conn));
  } else if (OB_ISNULL(
                 connection_handle = SQL_REQ_OP.get_nio_connection_handle(req))) {
    ret = OB_ERR_NULL_VALUE;
    SERVER_LOG(WARN, "request has no Rust SQL-NIO connection handle", K(ret),
               K(req));
  } else {
    req_ = req;
    request_generation_ = req->get_nio_request_generation();
    nio_connection_handle_ = connection_handle;
    request_ownership_ =
        owns_request ? RequestOwnership::OWNED : RequestOwnership::SNAPSHOT;
    response_started_ = false;
    response_disabled_ = !owns_request;
    query_receive_ts_ = query_receive_ts;
    conn_ = conn;

    // The sender always produces plain MySQL packets — the Rust reactor adds
    // any negotiated compression framing and owns its sequence cursor.
  }
  return ret;
}

int ObMPPacketSender::wait_packet(obmysql::ObICSMemPool &mem_pool,
                                  int64_t timeout_us,
                                  obmysql::ObMySQLPacket *&mysql_pkt)
{
  int ret = OB_SUCCESS;
  mysql_pkt = NULL;

  // Same ownership gate as append_response: a RELEASED sender has req_ == NULL
  // (the operator would dereference it) and a SNAPSHOT sender must not
  // become a second mid-request reader of the same connection. This also
  // absorbs the gate that the removed init_read_handle used to apply.
  if (OB_UNLIKELY(RequestOwnership::OWNED != request_ownership_) || OB_ISNULL(req_)) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "mid-request read without request ownership",
               K(ret), KP_(req));
  } else if (OB_UNLIKELY(0 != read_packet_lease_)) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "previous read packet is still leased",
               K(ret), K_(read_packet_lease));
  } else if (OB_UNLIKELY(0 == request_generation_)) {
    // 0 is never a live generation, so it fails exactly where a null read
    // handle used to.
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "mid-request read without a live request generation",
               K(ret), KP_(req));
  }

  if (OB_SUCC(ret)) {
    rpc::ObPacket *pkt = NULL;
    uint64_t packet_lease = 0;
    mid_request_read_started_ = true;
    LOG_DEBUG("mid-request read", KP(req_), K_(request_generation));
    ret = SQL_REQ_OP.wait_packet(req_, mem_pool, request_generation_,
                                 timeout_us, pkt, packet_lease);
    if (pkt != NULL) {
      mysql_pkt = static_cast<obmysql::ObMySQLPacket *>(pkt);
      read_packet_lease_ = packet_lease;
    }
  }
  return ret;
}

int ObMPPacketSender::release_packet(obmysql::ObMySQLPacket* pkt)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(RequestOwnership::OWNED != request_ownership_) || OB_ISNULL(req_)) {
    ret = OB_STATE_NOT_MATCH;
    SERVER_LOG(WARN, "mid-request release without request ownership",
               K(ret), KP_(req));
  } else if (!mid_request_read_started_) {
    ret = OB_NOT_INIT;
  } else if (0 == read_packet_lease_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (OB_FAIL(SQL_REQ_OP.release_read_packet(
                 req_, request_generation_, read_packet_lease_))) {
    SERVER_LOG(WARN, "release Rust read packet lease failed",
               K(ret), K_(read_packet_lease));
  } else {
    read_packet_lease_ = 0;
  }
  UNUSED(pkt);
  return ret;
}

int ObMPPacketSender::response_packet(obmysql::ObMySQLPacket &pkt) {
  LOG_DEBUG("response-packet", K(lbt()));
  int ret = OB_SUCCESS;
  if (!is_conn_valid()) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret));
  }

  if (OB_SUCC(ret)) {
    int64_t seri_size = 0;
    if (OB_FAIL(append_response(pkt, seri_size))) {
      LOG_WARN("failed to encode packet", K(ret));
    } else {
      LOG_DEBUG("succ encode packet", K(pkt), K(seri_size));
    }
  }

  return ret;
}

int ObMPPacketSender::response_resultset_metadata(
    const common::ObIArray<obmysql::ObMySQLField> &fields,
    bool include_result_header, uint8_t eof_field_count, uint16_t warnings,
    uint16_t status_flags) {
  int ret = OB_SUCCESS;
  int64_t packet_count = 0;
  int64_t framed_len = 0;
  ObSEArray<nio_mysql_field_view, 16> views;
  if (!is_conn_valid()) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret));
  } else if (OB_UNLIKELY(RequestOwnership::OWNED != request_ownership_) ||
             OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("request ownership has been released", K(ret), KP_(req));
  } else {
    response_started_ = true;
    if (OB_FAIL(views.prepare_allocate(fields.count()))) {
      LOG_WARN("failed to allocate Rust result-set metadata views", K(ret),
               "field_count", fields.count());
    } else {
      for (int64_t i = 0; i < fields.count(); ++i) {
        views.at(i) = make_field_view(fields.at(i));
      }
      const nio_mysql_field_view *view_data =
          views.count() == 0 ? NULL : &views.at(0);
      const int append_ret = nio_response_append_resultset_metadata(
          nio_connection_handle_, request_generation_,
          include_result_header ? 1 : 0, view_data, views.count(),
          eof_field_count, warnings, status_flags, &packet_count, &framed_len);
      if (0 != append_ret) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("Rust result-set metadata append failed", K(ret),
                 K(append_ret), K(include_result_header), K(eof_field_count),
                 K(warnings), K(status_flags), "field_count", fields.count());
      } else {
      }
    }
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
  } else if (!is_conn_valid()) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection already disconnected", K(ret), KP(conn_));
  } else {
    // error message
    ObString message;
    const int32_t MAX_MSG_BUF_SIZE = 512;
    char msg_buf[MAX_MSG_BUF_SIZE];
    const ObWarningBuffer *wb = common::ob_get_tsi_warning_buffer();
    if (NULL != wb
        && (wb->get_err_code() == err
            || OB_ERR_SIGNAL_EXCEPTION == err)) {
      message = wb->get_err_msg();
    }
    if (message.length() <= 0) {
      if (OB_LIKELY(NULL != errmsg) && 0 < strlen(errmsg)) {
        message = ObString::make_string(errmsg);
      } else if (OB_ERR_SIGNAL_EXCEPTION == err) {
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
    if (OB_ERR_SIGNAL_EXCEPTION == err && OB_NOT_NULL(wb)) {
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
  if (OB_SUCC(ret) && is_conn_valid()
      && conn_->is_sess_alloc_.load(std::memory_order_acquire)
      && !conn_->is_sess_free_.load(std::memory_order_acquire)) {
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
  if (is_conn_valid()) {
    if (OB_FAIL(ret)) {
      disconnect();
    } else {
      if (OB_FAIL(response_packet(epacket))) {
        RPC_LOG(WARN, "failed to send error packet", K(epacket), K(ret));
        disconnect();
      } else if (has_pl() && NULL != session &&
                 0 < session->get_pl_exact_err_msg().length()) {
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
  if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()) || OB_ISNULL(sess_info)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR(
        "session mgr or session info is null",
        KP(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()),
        K(sess_info),
        K(ret));
  } else {
    ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->revert_session(sess_info);
  }
  return ret;
}

int ObMPPacketSender::get_session(ObSQLSessionInfo *&sess_info)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(conn_) || OB_ISNULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("conn or session mgr is NULL", K(ret), KP(conn_), K(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->get_session(conn_->sessid_, sess_info))) {
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
  if (!is_conn_valid()) {
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

  if (OB_SUCC(ret)) {
    // for ok packet which has no status packet
    if (NULL == pkt) {
      if (OB_FAIL(response_packet(okp))) {
        LOG_WARN("response ok packet fail", K(ret));
      }
    } else {
      if (OB_FAIL(response_packet(*pkt))) {
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
  if (!is_conn_valid()) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "packet sender does not own a request");
  }
  return is_conn_valid() ? conn_ : NULL;
}

void ObMPPacketSender::force_disconnect()
{
  LOG_WARN_RET(OB_ERROR, "force disconnect", K(lbt()));
  ObMPPacketSender::disconnect();
}

void ObMPPacketSender::disconnect()
{
  if (is_conn_valid()) {
    if (OB_ISNULL(req_)) {// do nothing
    } else if (OB_SUCCESS != SQL_REQ_OP.disconnect_sql_conn(req_, request_generation_)) {
      LOG_WARN_RET(OB_STATE_NOT_MATCH, "ignore stale sql request disconnect",
                   K(request_generation_));
    } else {
      ObSMConnection *conn = SQL_REQ_OP.get_sql_session(req_);
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
    if (OB_FAIL(response_packet(eofp))) {
      LOG_WARN("response packet fail", K(ret));
    }
  } else {
    if (OB_FAIL(send_ok_packet(*const_cast<ObSQLSessionInfo *>(&session),
                               *ok_param, &eofp))) {
      LOG_WARN("failed to response ok packet and eof packet", K(ret),
               K(*ok_param));
    }
  }
  return ret;
}

int ObMPPacketSender::append_response(ObMySQLPacket &pkt, int64_t &seri_size) {
  int ret = OB_SUCCESS;
  seri_size = 0;
  if (OB_UNLIKELY(!is_conn_valid())) {
    ret = OB_CONNECT_ERROR;
    LOG_ERROR("connection already disconnected", K(ret));
  } else if (OB_UNLIKELY(RequestOwnership::OWNED != request_ownership_) ||
             OB_ISNULL(req_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("request ownership has been released", K(ret), KP_(req));
  } else {
    // Once response production has been attempted, packet retry is no longer
    // safe: a failed encoder may already have flushed an earlier batch, moved
    // a sequence cursor, or marked the Rust connection unusable.
    response_started_ = true;
    nio_connection_handle *connection_handle = nio_connection_handle_;
    int encode_ret = OB_SUCCESS;
    int append_ret = -1;
    switch (pkt.get_mysql_packet_type()) {
      case ObMySQLPacketType::PKT_ERR: {
        const OMPKError &error = static_cast<const OMPKError &>(pkt);
        const ObString sql_state = error.get_sql_state();
        const ObString message = error.get_message();
        append_ret = nio_response_append_error(
            connection_handle, request_generation_, error.get_err_code(),
            sql_state.ptr(), sql_state.length(), message.ptr(),
            message.length(), &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_AUTH_SWITCH: {
        const OMPKAuthSwitch &auth_switch =
            static_cast<const OMPKAuthSwitch &>(pkt);
        const ObString &plugin_name = auth_switch.get_plugin_name();
        const ObString &scramble = auth_switch.get_scramble();
        append_ret = nio_response_append_auth_switch(
            connection_handle, request_generation_, plugin_name.ptr(),
            plugin_name.length(), scramble.ptr(), scramble.length(),
            &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_FILENAME: {
        const OMPKLocalInfile &local_infile =
            static_cast<const OMPKLocalInfile &>(pkt);
        const ObString &filename = local_infile.get_filename();
        append_ret = nio_response_append_local_infile(
            connection_handle, request_generation_, filename.ptr(),
            filename.length(), &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_STR: {
        const OMPKString &string = static_cast<const OMPKString &>(pkt);
        const ObString &value = string.get_string();
        append_ret =
            nio_response_append_string(connection_handle, request_generation_,
                                       value.ptr(), value.length(), &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_RESHEAD: {
        const OMPKResheader &resheader =
            static_cast<const OMPKResheader &>(pkt);
        append_ret = nio_response_append_result_header(
            connection_handle, request_generation_, resheader.get_field_count(),
            &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_EOF: {
        const OMPKEOF &eof = static_cast<const OMPKEOF &>(pkt);
        append_ret = nio_response_append_eof(
            connection_handle, request_generation_, eof.get_field_count(),
            eof.get_warning_count(), eof.get_server_status().flags_,
            &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_PREPARE: {
        const OMPKPrepare &prepare = static_cast<const OMPKPrepare &>(pkt);
        append_ret = nio_response_append_prepare_ok(
            connection_handle, request_generation_, prepare.get_statement_id(),
            prepare.get_column_num(), prepare.get_param_num(),
            prepare.get_warning_count(), &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_FIELD: {
        append_ret = encode_field_packet(static_cast<const OMPKField &>(pkt),
                                         connection_handle, request_generation_,
                                         &seri_size);
        break;
      }
      case ObMySQLPacketType::PKT_ROW: {
        append_ret = encode_row_packet(static_cast<const OMPKRow &>(pkt),
                                       connection_handle, request_generation_,
                                       &seri_size, encode_ret);
        break;
      }
      case ObMySQLPacketType::PKT_OKP: {
        append_ret = encode_ok_packet(static_cast<const OMPKOK &>(pkt),
                                      connection_handle, request_generation_,
                                      &seri_size, encode_ret);
        break;
      }
      default:
        encode_ret = OB_NOT_SUPPORTED;
        LOG_ERROR("MySQL response packet has no Rust encoder", K(encode_ret),
                  K(pkt.get_mysql_packet_type()));
        break;
    }
    if (OB_FAIL(encode_ret)) {
      ret = encode_ret;
    } else if (0 != append_ret) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("Rust MySQL response append failed", K(ret), K(append_ret),
               K(pkt.get_mysql_packet_type()));
    }
  }
  return ret;
}

int ObMPPacketSender::flush_buffer(const bool is_last) {
  int ret = OB_SUCCESS;
  nio_connection_handle *connection_handle = nio_connection_handle_;
  if (OB_UNLIKELY(!is_conn_valid())) {
    ret = OB_CONNECT_ERROR;
    LOG_WARN("connection in error, maybe has disconnected", K(ret));
  } else if (RequestOwnership::OWNED != request_ownership_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("request ownership has been released", K(ret));
  } else if (OB_ISNULL(req_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("req_ is null", KP_(req), K(ret));
  } else if (0 != nio_response_flush(connection_handle, request_generation_,
                                     is_last ? 1 : 0)) {
    ret = OB_IO_ERROR;
    LOG_WARN("flush Rust-owned MySQL response failed", K(ret), K(is_last),
             K(request_generation_));
  }
  if (is_last) {
    const int finish_ret = finish_sql_request();
    if (OB_SUCCESS == ret && OB_SUCCESS != finish_ret) {
      ret = finish_ret;
    }
  }
  return ret;
}

int ObMPPacketSender::get_conn_id(uint32_t &sessid) const {
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

int ObMPPacketSender::finish_sql_request() {
  int ret = OB_SUCCESS;
  if (RequestOwnership::OWNED == request_ownership_) {
    if (OB_ISNULL(req_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("owned mysql request has no request object", K(ret),
                  K(request_generation_));
    } else {
        (void)release_read_packet_lease();
        rpc::ObRequest *req = req_;
        const uint64_t generation = request_generation_;
        ret = SQL_REQ_OP.finish_sql_request(req, generation);
        if (OB_SUCCESS != ret) {
        // A true current owner is guaranteed a fail-closed prepare path by
        // Rust. Reaching this branch therefore means stale/corrupt local
        // ownership; request a generation-scoped shutdown before discarding
        // that unusable identity.
        const int shutdown_ret =
            SQL_REQ_OP.disconnect_sql_conn(req, generation);
        LOG_ERROR("failed to finish owned mysql request", K(ret),
                  K(shutdown_ret), K(generation));
        }
    }
    // Do not relinquish local ownership until prepare/commit has returned.
    // Repeated finish calls observe RELEASED and are strict no-ops.
    clear_request_identity(RequestOwnership::RELEASED);
  }
  return ret;
}

int ObMPPacketSender::detach_for_retry() {
  int ret = OB_SUCCESS;
  if (RequestOwnership::RELEASED == request_ownership_ ||
      RequestOwnership::DETACHED_FOR_RETRY == request_ownership_) {
    // A processor with no request ownership has nothing to transfer.
  } else if (RequestOwnership::OWNED != request_ownership_ || OB_ISNULL(req_)) {
    ret = OB_STATE_NOT_MATCH;
    LOG_ERROR("invalid mysql request ownership for packet retry", K(ret));
  } else if (response_started_ || mid_request_read_started_ ||
             0 != read_packet_lease_) {
    // Retrying after any wire progress would let the next processor inherit
    // bytes or a sequence cursor from a failed attempt. Leave ownership in
    // place so reset/destruction finishes and closes instead.
    ret = OB_STATE_NOT_MATCH;
    LOG_ERROR("cannot retry mysql request after wire progress", K(ret),
              K(response_started_), K_(mid_request_read_started),
              K_(read_packet_lease));
  } else {
    clear_request_identity(RequestOwnership::DETACHED_FOR_RETRY);
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

// Teardown counterpart of wait_packet: drop any packet body still leased to
// C++ before the request identity is discarded.
int ObMPPacketSender::release_read_packet_lease()
{
  int ret = OB_SUCCESS;
  if (0 != read_packet_lease_) {
    LOG_DEBUG("release read packet lease", KP(req_), K_(request_generation),
              K_(read_packet_lease));
    if (OB_FAIL(SQL_REQ_OP.release_read_packet(req_, request_generation_,
                                               read_packet_lease_))) {
      SERVER_LOG(WARN, "release Rust read packet lease failed", K(ret),
                 K_(read_packet_lease));
    }
    // A matching request commit is Rust's final cleanup fallback if this
    // explicit release lost a shutdown or generation race.
    read_packet_lease_ = 0;
  }
  mid_request_read_started_ = false;
  return ret;
}

}; // end namespace observer
}; // end namespace oceanbase
