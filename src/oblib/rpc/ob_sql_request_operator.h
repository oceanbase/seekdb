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

#ifndef OCEANBASE_RPC_OB_SQL_REQUEST_OPERATOR_H_
#define OCEANBASE_RPC_OB_SQL_REQUEST_OPERATOR_H_

#include <stdint.h>
#include "lib/net/ob_addr.h"
#include "lib/net/ob_sql_tls_info.h"

struct nio_connection_handle;

namespace oceanbase
{
namespace obmysql
{
class ObICSMemPool;
class ObSqlSockSession;
} // namespace obmysql
namespace observer
{
class ObSMConnection;
} // namespace observer

namespace rpc
{
class ObRequest;
class ObPacket;

struct ObSqlSockDesc
{
  ObSqlSockDesc() : sock_desc_(NULL) {}
  ~ObSqlSockDesc() = default;
  void reset() { sock_desc_ = NULL; }
  void set(obmysql::ObSqlSockSession *desc) { sock_desc_ = desc; }
  void clear_sql_session_info();
  obmysql::ObSqlSockSession *sock_desc_;
};

// Rust SQL-NIO is now the only MySQL transport. Keep one concrete operator
// instead of the former Easy/POC virtual-dispatch hierarchy.
class ObSqlRequestOperator
{
public:
  ObSqlRequestOperator() = default;
  ~ObSqlRequestOperator() = default;

  observer::ObSMConnection *get_sql_session(ObRequest *req);
  nio_connection_handle *get_nio_connection_handle(const ObRequest *req);
  int get_sql_tls_info(const ObRequest *req, common::ObSqlTlsInfo &info);
  common::ObAddr get_peer(const ObRequest *req);
  int disconnect_sql_conn(ObRequest *req, uint64_t generation);
  int finish_sql_request(ObRequest *req, uint64_t generation);
  // Mid-request reads are addressed by (request, generation): the former
  // opaque read handle carried no information beyond the generation.
  int wait_packet(ObRequest *req, obmysql::ObICSMemPool &mem_pool,
                  uint64_t generation, int64_t timeout_us, ObPacket *&pkt,
                  uint64_t &packet_lease);
  int release_read_packet(ObRequest *req, uint64_t generation,
                          uint64_t packet_lease);
  void get_sock_desc(ObRequest *req, ObSqlSockDesc &desc);
  void disconnect_by_sql_sock_desc(ObSqlSockDesc &desc);
  void interrupt_read_by_sql_sock_desc(ObSqlSockDesc &desc);
  void bind_sql_session(ObRequest *req);
};

extern ObSqlRequestOperator global_sql_req_operator;
#define SQL_REQ_OP (oceanbase::rpc::global_sql_req_operator)
} // namespace rpc
} // namespace oceanbase

#endif /* OCEANBASE_RPC_OB_SQL_REQUEST_OPERATOR_H_ */
