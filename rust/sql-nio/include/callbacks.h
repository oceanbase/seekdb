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
} // namespace obmysql
} // namespace oceanbase
