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

#ifndef OCEANBASE_MYSQL_OBMP_PACKET_SENDER_H_
#define OCEANBASE_MYSQL_OBMP_PACKET_SENDER_H_
#include "lib/container/ob_iarray.h"
#include "observer/ob_server_struct.h"
#include "rpc/obmysql/ob_i_cs_mem_pool.h"
#include "rpc/obmysql/ob_mysql_field.h"
#include "rpc/obmysql/obsm_struct.h"

struct nio_connection_handle;

namespace oceanbase
{
namespace sql
{
class ObSQLSessionInfo;
}
namespace observer
{
class ObMySQLResultSet;
struct ObSMConnection;

struct ObOKPParam
{
  ObOKPParam()
    : affected_rows_(0),
      message_(NULL),
      is_on_connect_(false),
      is_on_change_user_(false),
      has_more_result_(false),
      take_trace_id_to_client_(false),
      warnings_count_(0),
      cursor_exist_(false),
      send_last_row_(false),
      has_pl_out_(false),
      lii_(0)
  { }
  void reset();
  int64_t to_string(char *buf, const int64_t buf_len) const;
  uint64_t affected_rows_;
  char *message_;
  bool is_on_connect_;
  bool is_on_change_user_;
  bool has_more_result_;
  // current, when return error packet or query fly time >= trace_log_slow_query_watermark(100ms default),
  // will take trace id to client for easy debugging;
  bool take_trace_id_to_client_;
  uint16_t warnings_count_;
  bool cursor_exist_;
  bool send_last_row_;
  bool has_pl_out_;
  // According to mysql behavior, when last_insert_id was changed by insert,
  // update or replace, no matter auto generated or manual specified, mysql
  // should send last_insert_id through ok packet to mysql client.
  // And if last_insert_id was changed by set @@last_insert_id=xxx, mysql
  // will not send changed last_insert_id to client.
  //
  // if last insert id has changed, send lii to client;
  // Attention, in this case, lii is in ok packet's last insert id field
  // (encode by int<lenenc>), not in session track field;
  uint64_t lii_;
};

class ObIMPPacketSender
{
public:
  virtual ~ObIMPPacketSender() { }
  virtual void disconnect() = 0;
  virtual void force_disconnect() = 0;

  virtual int wait_packet(obmysql::ObICSMemPool& mem_pool,
                          int64_t timeout_us,
                          obmysql::ObMySQLPacket *&pkt) = 0;
  virtual int release_packet(obmysql::ObMySQLPacket* pkt) = 0;
  virtual int response_packet(obmysql::ObMySQLPacket &pkt) = 0;
  virtual int response_resultset_metadata(
      const common::ObIArray<obmysql::ObMySQLField> &fields,
      bool include_result_header, uint8_t eof_field_count, uint16_t warnings,
      uint16_t status_flags) = 0;
  virtual int send_error_packet(int err,
                                const char* errmsg,
                                void *extra_err_info = NULL) = 0;
  virtual int send_ok_packet(sql::ObSQLSessionInfo &session, ObOKPParam &ok_param, obmysql::ObMySQLPacket* pkt=NULL) = 0;
  virtual int send_eof_packet(const sql::ObSQLSessionInfo &session,
                              const ObMySQLResultSet &result,
                              ObOKPParam *ok_param = NULL) = 0;
  virtual int flush_buffer(const bool is_last) = 0;
  virtual ObSMConnection* get_conn() const = 0;
};

class ObMPPacketSender final : public ObIMPPacketSender
{
public:
  ObMPPacketSender();
  virtual ~ObMPPacketSender() override;

  void reset();
  virtual void disconnect() override;
  virtual void force_disconnect() override;
  virtual int wait_packet(obmysql::ObICSMemPool& mem_pool,
                          int64_t timeout_us,
                          obmysql::ObMySQLPacket *&pkt) override;
  virtual int release_packet(obmysql::ObMySQLPacket* pkt) override;
  virtual int response_packet(obmysql::ObMySQLPacket &pkt) override;
  virtual int response_resultset_metadata(
      const common::ObIArray<obmysql::ObMySQLField> &fields,
      bool include_result_header, uint8_t eof_field_count, uint16_t warnings,
      uint16_t status_flags) override;
  virtual int send_error_packet(int err,
                                const char* errmsg,
                                void *extra_err_info = NULL) override;
  virtual int send_ok_packet(sql::ObSQLSessionInfo &session, ObOKPParam &ok_param, obmysql::ObMySQLPacket* pkt=NULL) override;
  virtual int send_eof_packet(const sql::ObSQLSessionInfo &session,
                              const ObMySQLResultSet &result,
                              ObOKPParam *ok_param = NULL) override;
  virtual int flush_buffer(const bool is_last) override;
  int snapshot_from(ObMPPacketSender &that);
  int handoff_request_to(ObMPPacketSender &target);
  int init(rpc::ObRequest *req);
  int do_init(rpc::ObRequest *req, bool owns_request, int64_t query_receive_ts);
  bool is_conn_valid() const {
    return RequestOwnership::OWNED == request_ownership_ && NULL != conn_ &&
           NULL != nio_connection_handle_;
  }
  int finish_sql_request();
  // Packet retry keeps the Rust request generation active and transfers the
  // same ObRequest to a later processor. Make that exceptional non-finish
  // transition explicit so ordinary reset/destruction can fail closed.
  int detach_for_retry();
  int get_conn_id(uint32_t &sessid) const;
  ObSMConnection *get_conn() const override;
  int get_session(sql::ObSQLSessionInfo *&sess_info);
  int revert_session(sql::ObSQLSessionInfo *sess_info);
  // Suppress any further response bytes while retaining responsibility for
  // finishing the request (for example, a connect-time auth-switch failure).
  void disable_response() { response_disabled_ = true; }
  bool is_disable_response() const {
    return response_disabled_ || RequestOwnership::OWNED != request_ownership_;
  }
  bool has_pl();

private:
  enum class RequestOwnership : uint8_t {
    RELEASED,
    SNAPSHOT,
    OWNED,
    DETACHED_FOR_RETRY,
  };

  int release_read_packet_lease();
  void clear_request_identity(RequestOwnership next_state);

private:
  int append_response(obmysql::ObMySQLPacket &pkt, int64_t &seri_size);

protected:
  rpc::ObRequest *req_;
  // Immutable copy made before the embedded ObRequest can be placement-newed
  // for a later request on this connection.
  uint64_t request_generation_;
  // Non-owning connection-scoped Rust response identity. The session owns the
  // handle. Only an OWNED sender may dereference it; a SNAPSHOT merely carries
  // the identity until a successful handoff transfers REQUEST_BUSY ownership.
  nio_connection_handle *nio_connection_handle_;
  // Set once this sender has performed a mid-request read on the connection.
  // The stream cursor has then moved even after the packet lease is released,
  // so packet retry must stay closed. Replaces the removed read_handle_, whose
  // non-null state carried exactly this fact.
  bool mid_request_read_started_;
  // At most one synchronous Rust packet body may be exposed to C++ at once.
  uint64_t read_packet_lease_;
  RequestOwnership request_ownership_;
  // Packet retry is legal only before the first response append attempt.
  // A snapshot copies this bit, and handoff refreshes it because the original
  // sender may append after the snapshot was created.
  bool response_started_;
  bool response_disabled_;
  int64_t query_receive_ts_;
  ObSMConnection *conn_;
private:
  DISALLOW_COPY_AND_ASSIGN(ObMPPacketSender);
};

}; // end namespace observer
}; // end namespace oceanbase

#endif /* OCEANBASE_MYSQL_OBMP_PACKET_SENDER_H_ */
