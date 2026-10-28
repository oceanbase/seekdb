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

#define SEEKDB_CONNECTION_TRANSPORT_TCP "tcp"
#define SEEKDB_CONNECTION_TRANSPORT_UNIX_SOCKET "unix_socket"
#define SEEKDB_CONNECTION_TRANSPORT_NAMED_PIPE "named_pipe"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
  SEEKDB_SUCCESS = 0,
  SEEKDB_INTERNAL_ERROR = -1,
  SEEKDB_INVALID_ARGUMENT = -2,
  SEEKDB_NO_MORE_ROWS = -3,
} SeekdbReturnCode;

typedef void *SeekdbHandle;

typedef struct {
  const char *transport;
  unsigned int port;
  const char *host;
  const char *unix_socket;
  const char *named_pipe;
  const char *user;
} SeekdbConnectionOptions;

int seekdb_open(const char *db_dir, const char **parameters, SeekdbHandle *out_handle);

int seekdb_connection_options(SeekdbHandle handle, SeekdbConnectionOptions *out_options);

const char *seekdb_last_open_error(void);

int seekdb_close(SeekdbHandle handle);

#ifdef __cplusplus
}
#endif
