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

#ifndef OCEANBASE_OBMYSQL_OB_SQL_SOCK_SESSION_H_
#define OCEANBASE_OBMYSQL_OB_SQL_SOCK_SESSION_H_
#include "rpc/obmysql/ob_i_cs_mem_pool.h"
#include "rpc/obmysql/obsm_struct.h"
#include "rpc/ob_sql_mem_pool.h"
#include "rpc/obmysql/ob_i_sm_conn_callback.h"
#include "rpc/ob_request.h"

struct nio_connection_handle;

namespace oceanbase
{
namespace obmysql
{

class ObSqlSessionMemPool: public ObICSMemPool
{
public:
  ObSqlSessionMemPool(): pool_() {}
  ~ObSqlSessionMemPool() override = default;
  void* alloc(int64_t sz) { return pool_.alloc(sz); }
  
  void reset() { pool_.destroy(); }
  void reuse() { pool_.reuse(); }
private:
  obmysql::ObSqlMemPool pool_;
};

class ObSqlSockSession
{
public:
  ObSqlSockSession(ObISMConnectionCallback &conn_cb);
  ~ObSqlSockSession() = default;
  int init();
  void destroy();
  int prepare_request_commit(uint64_t generation);
  void commit_request(uint64_t generation);
  int set_shutdown(uint64_t generation);
  void shutdown();
  int on_disconnect();
  int clear_sql_session_info();
  void bind_sql_session();
  nio_connection_handle *get_nio_connection_handle() const
  {
    return nio_connection_handle_;
  }
  ObISMConnectionCallback &sm_conn_cb_;
  rpc::ObRequest sql_req_;
  ObSqlSessionMemPool pool_;
  observer::ObSMConnection conn_;
  common::ObAddr client_addr_;
  int fd_; // immutable accepted descriptor, for request diagnostics
  uint32_t sql_session_id_; // debug only
  // One connection-scoped Rust owner acquired during admission. The Rust
  // request gate keeps it live until on_close, where destroy() releases it.
  nio_connection_handle *nio_connection_handle_;
};

}; // end namespace obmysql
}; // end namespace oceanbase

#endif /* OCEANBASE_OBMYSQL_OB_SQL_SOCK_SESSION_H_ */
