/*
 * Copyright (c) 2026 OceanBase.
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

#pragma once
#include <cstdint>

namespace oceanbase {
namespace nio {
struct NioGreetingInfo;
struct NioMysqlCommandView;
}
namespace obmysql {
class ObSqlSockHandler;
class ObSqlSockSession;
int32_t ob_sql_sock_handler_on_connect(ObSqlSockHandler *handler,
    ObSqlSockSession *session, int32_t fd, int32_t is_unix,
    nio::NioGreetingInfo *greeting);
int32_t ob_sql_sock_handler_on_readable(ObSqlSockHandler *handler,
    ObSqlSockSession *session, char *body, int64_t body_len, uint64_t wire_bytes,
    int32_t packet_kind, const nio::NioMysqlCommandView *command, uint64_t generation);
void ob_sql_sock_handler_on_disconnect(ObSqlSockHandler *handler, ObSqlSockSession *session);
void ob_sql_sock_handler_on_close(ObSqlSockHandler *handler, ObSqlSockSession *session, int32_t error);
void ob_sql_nio_log_accept_state(int32_t kind, int32_t raw_error, uint64_t drain_id,
    uint64_t accepted, uint32_t transient_errors, uint32_t consecutive_errors,
    int32_t outcome);
void ob_sql_nio_log_reactor_state(int32_t poll_result, uint64_t poll_id,
    uint64_t event_count, uint64_t listener_events, uint64_t accept_calls,
    uint64_t accepted, uint64_t cxx_connect_attempts, uint64_t cxx_connect_returns,
    uint64_t cxx_connect_rejects, uint64_t greeting_sent,
    uint64_t greeting_deferred, uint64_t greeting_failed,
    uint64_t greeting_flushed, uint64_t admission_register_failed,
    uint64_t dispatch_handoff, uint64_t dispatch_direct,
    uint64_t dispatch_queue_full, uint64_t greeting_close_before_flush,
    uint32_t listener_event_flags, uint32_t listener_os_probe_state,
    uint32_t listener_os_probe_revents, int32_t listener_os_probe_raw_error);
} // namespace obmysql
} // namespace oceanbase
