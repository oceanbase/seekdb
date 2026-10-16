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
    const char *host;
    const char *unix_socket;
    const char *named_pipe;
    const char *user;
} SeekdbConnectionOptions;

/* Open a seekdb instance rooted at db_dir.
 *
 * parameters is an optional NULL-terminated array of key/value pairs:
 *   {"memory_budget", "10G", "syslog_max_file", "1000", NULL}
 *
 * Driver-reserved keys (not forwarded as server parameters):
 *   port — omit or "0"; the in-process engine has no TCP listener.
 *   mysql_port_mode — omit or "disabled".
 *
 * All other keys are seekdb server parameters, passed on first init only. On
 * first init the driver always seeds memory_budget=1G and log_disk_size=2G
 * unless the caller overrides them; a platform may seed more defaults and fix
 * more parameters on every start. On restart, persisted values are kept (issue #26). */
int seekdb_open(const char *db_dir, const char **parameters, SeekdbHandle *out_handle);
/* Description of the most recent seekdb_open failure on the calling thread.
 * Empty before the first open and after a successful open. The borrowed string
 * remains valid until the next seekdb_open on this thread (or thread exit).
 * No handle is required. Copy it before another open if it must be retained.
 * Startup diagnostics describe the driver stage; server-internal failures may
 * require inspecting the instance's log directory. */
const char *seekdb_last_open_error(void);
int seekdb_close(SeekdbHandle handle);

/* Return the MySQL-protocol connection options for an open handle.
 *
 * transport is "tcp", "unix_socket", or "named_pipe". TCP returns host and
 * port; local transports return their corresponding named field. Unused
 * transport fields are NULL and port is zero for local transports. user is
 * always "root". The in-process engine always uses "unix_socket"; a long socket
 * path is replaced by a per-handle short alias in a platform directory (on iOS
 * the App's tmp directory, or /tmp on the Simulator).
 * Returned strings are borrowed and remain valid until seekdb_close(handle). */
int seekdb_connection_options(SeekdbHandle handle, SeekdbConnectionOptions *out_options);

#ifdef __cplusplus
}
#endif
