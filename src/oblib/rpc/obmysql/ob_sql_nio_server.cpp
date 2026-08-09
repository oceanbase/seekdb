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
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "lib/ob_running_mode.h"
#include <stdio.h>
#include <unistd.h>

namespace oceanbase
{
using namespace common;
namespace obmysql
{

// ---- Bridge: the Rust seekdb_nio reactor's callbacks -> ObSqlSockHandler ----
namespace {
int nio_on_connect(void* ctx, void* sess, int fd, int is_unix,
                   nio_greeting_info* greeting) {
  int ret = static_cast<ObSqlSockHandler*>(ctx)->on_connect(sess, fd, is_unix != 0);
  if (0 == ret && NULL != greeting) {
    // Greeting inputs travel forward through the vtable (this replaced the
    // out-of-header reverse-FFI symbol sm_conn_greeting_info at ABI 22). The
    // session's sessid + scramble exist as soon as on_connect constructed it.
    observer::ObSMConnection &conn = static_cast<ObSqlSockSession*>(sess)->conn_;
    greeting->sessid = conn.sessid_;
    MEMCPY(greeting->scramble, conn.scramble_buf_, sizeof(greeting->scramble));
    // The short MySQL version (was DEFAULT_MYSQL_VERSION_CSTR in the deleted
    // ompk_handshake.h), not the long @@version_comment string.
    static const char VERSION[] = "5.7.25";
    MEMCPY(greeting->version, VERSION, sizeof(VERSION) - 1);
    greeting->version_len = sizeof(VERSION) - 1;
    ObServerStatusFlags status_flags;
    status_flags.status_flags_.OB_SERVER_STATUS_AUTOCOMMIT =
        0 != conn.autocommit_snapshot_;
    greeting->status_flags = status_flags.flags_;
  }
  return ret;
}
int nio_on_readable(void *ctx, void *sess, char *body, int64_t body_len,
                    uint64_t wire_bytes, int packet_kind,
                    const nio_mysql_command_view *command_view,
                    uint64_t generation) {
  return static_cast<ObSqlSockHandler *>(ctx)->on_readable(
      sess, body, body_len, wire_bytes, packet_kind, command_view, generation);
}
void nio_on_disconnect(void* ctx, void* sess) {
  UNUSED(ctx);
  static_cast<ObSqlSockSession*>(sess)->on_disconnect();
}
void nio_on_close(void* ctx, void* sess, int err) {
  static_cast<ObSqlSockHandler*>(ctx)->on_close(sess, err);
}
} // anonymous namespace

// Free function declared in rpc/ob_request.h. The accepted descriptor is
// immutable, so request diagnostics do not need a Rust registry lookup.
int get_fd_from_sess(void* sess)
{
  return NULL == sess ? -1 : static_cast<ObSqlSockSession *>(sess)->fd_;
}

int ObSqlNioServer::start(int port, rpc::frame::ObReqDeliver* deliver,
                          int n_thread, bool disable_tcp, bool use_tls)
{
  static_assert(alignof(ObSqlSockSession) <= 16,
                "Rust embedded session storage must satisfy C++ alignment");
  int ret = OB_SUCCESS;
  lib::ObMutexGuard guard(reactor_lock_);
  if (OB_FAIL(io_handler_.init(deliver))) {
  } else {
    nio_callbacks cb = {};
    cb.ctx = &io_handler_;
    cb.on_connect = nio_on_connect;
    cb.on_readable = nio_on_readable;
    cb.on_disconnect = nio_on_disconnect;
    cb.on_close = nio_on_close;
    char addr[64];
    // Match the old engine's family selection: an IPv6 deployment must bind
    // the v6 wildcard or the MySQL port is unreachable. The Rust bind path
    // sets IPV6_V6ONLY for a v6 address, mirroring the deleted C++ listener.
    if (oceanbase::lib::use_ipv6()) {
      snprintf(addr, sizeof(addr), "[::]:%d", port);
    } else {
      snprintf(addr, sizeof(addr), "0.0.0.0:%d", port);
    }
    const size_t thread_count = static_cast<size_t>(n_thread <= 0 ? 1 : n_thread);
    // TLS is startup-only, like the thread count (set_thread_count already
    // returns OB_NOT_SUPPORTED): toggling ssl_client_authentication at
    // runtime requires an observer restart to take effect.
    nio_tls_config tls_cfg = { OB_SSL_CA_FILE, OB_SSL_CERT_FILE, OB_SSL_KEY_FILE };
    const nio_tls_config *tls = use_tls ? &tls_cfg : NULL;
    int32_t start_err = NIO_START_OK;
    reactor_ = nio_start(addr, NIO_ABI_VERSION, &cb, sizeof(cb),
                         sizeof(ObSqlSockSession), thread_count,
                         tls, use_tls ? sizeof(tls_cfg) : 0, &start_err,
                         disable_tcp ? 1 : 0);
    if (NULL == reactor_) {
      ret = OB_ERR_UNEXPECTED;
      // start_err makes an ABI drift distinguishable from a busy port; ETLS
      // means the wallet cert/key/ca failed to load — startup fails rather
      // than serving cleartext on a port configured for TLS.
      LOG_WARN("nio_start failed", K(ret), K(port), K(start_err),
               K(disable_tcp), K(use_tls));
    } else {
      n_thread_ = (n_thread <= 0 ? 1 : n_thread);
      LOG_INFO("seekdb_nio (rust) started", K(port), K(n_thread));
      // A local-endpoint failure is non-fatal when TCP is enabled, matching
      // the old engine. Surface that degraded startup instead of hiding it.
      const char *local_endpoint =
#ifdef _WIN32
          "run/sql.pipe";
#else
          "run/sql.sock";
#endif
      if (0 != access(local_endpoint, F_OK)) {
        LOG_WARN("local SQL endpoint missing", K(errno), K(disable_tcp));
      }
    }
  }
  return ret;
}

int ObSqlNioServer::set_thread_count(const int thread_num) 
{
  int ret = OB_SUCCESS;
  if (thread_num != n_thread_) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("changing Rust SQL-NIO thread count requires observer restart",
             K(ret), K(thread_num), K(n_thread_));
  }
  return ret;
}

void ObSqlNioServer::stop()
{
  lib::ObMutexGuard guard(reactor_lock_);
  if (NULL != reactor_) {
    nio_stop(reactor_);
  }
}

void ObSqlNioServer::wait()
{
  // The Rust io thread is joined in destroy(); nothing to wait on here.
}

void ObSqlNioServer::destroy()
{
  nio_reactor *reactor = NULL;
  {
    lib::ObMutexGuard guard(reactor_lock_);
    reactor = reactor_;
    reactor_ = NULL;
  }
  if (NULL != reactor) {
    nio_wait_destroy(reactor);
  }
}

void ObSqlNioServer::update_tcp_keepalive_params(int keepalive_enabled, uint32_t tcp_keepidle, uint32_t tcp_keepintvl, uint32_t tcp_keepcnt)
{
  lib::ObMutexGuard guard(reactor_lock_);
  if (NULL != reactor_) {
    const int rc = nio_update_tcp_keepalive_params(
        reactor_, keepalive_enabled, tcp_keepidle, tcp_keepintvl, tcp_keepcnt);
    if (0 != rc) {
      LOG_WARN_RET(OB_ERR_UNEXPECTED,
                   "update Rust SQL-NIO TCP keepalive failed", K(rc),
                   K(keepalive_enabled), K(tcp_keepidle), K(tcp_keepintvl),
                   K(tcp_keepcnt));
    }
  }
}

ObSqlNioServer* global_sql_nio_server = NULL;
}; // end namespace obmysql
}; // end namespace oceanbase
