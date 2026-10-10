// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void *SeekdbHandle;

typedef enum {
    SEEKDB_SUCCESS = 0,
    SEEKDB_INTERNAL_ERROR = -1,
    SEEKDB_INVALID_ARGUMENT = -2,
    SEEKDB_NO_MORE_ROWS = -3,
} SeekdbReturnCode;

#define SEEKDB_CONNECTION_TRANSPORT_TCP "tcp"
#define SEEKDB_CONNECTION_TRANSPORT_UNIX_SOCKET "unix_socket"
#define SEEKDB_CONNECTION_TRANSPORT_NAMED_PIPE "named_pipe"

typedef struct {
    const char *transport;
    unsigned int port;
    const char *endpoint;
    const char *user;
} SeekdbConnectionOptions;

/* Open a seekdb instance rooted at db_dir.
 *
 * parameters is an optional NULL-terminated array of key/value pairs:
 *   {"port", "3306", "memory_budget", "10G", "syslog_max_file", "1000", NULL}
 *
 * Driver-reserved keys (consumed by libseekdb, not forwarded to the server):
 *   port — TCP port for connect; omit or "0" for local transport (UDS/pipe).
 *
 * All other keys are seekdb server parameters, passed as --parameter on first
 * init only. On first init the driver always seeds memory_budget=1G and
 * log_disk_size=2G unless the caller overrides them; additional server keys
 * may also be supplied. On restart, persisted values are kept (issue #26). */
int seekdb_open(const char *db_dir, const char **parameters, SeekdbHandle *out_handle);
int seekdb_close(SeekdbHandle handle);

/* Return the MySQL-protocol connection options for an open handle.
 *
 * transport is "tcp", "unix_socket", or "named_pipe". TCP exposes only port;
 * clients use their default local host. Local transports expose endpoint as a
 * Unix socket path or full Windows named-pipe path. user is always "root".
 * On POSIX, the Unix socket endpoint is a per-handle alias under /tmp and is
 * usable only while the handle remains open.
 * transport, endpoint, and user are borrowed and remain valid until
 * seekdb_close(handle). */
int seekdb_connection_options(SeekdbHandle handle, SeekdbConnectionOptions *out_options);

#ifdef __cplusplus
}
#endif
