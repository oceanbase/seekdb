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

#include "seekdb.h"
#include "seekdb_in_process.h"
#include "seekdb_platform.h"
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include "lib/ob_errno.h"

namespace {
struct SeekdbHandleImpl {
  char *sock_path;
  char *socket_alias_dir;
};

thread_local std::string last_open_error;

int open_failed(int code, const char *message)
{
  last_open_error = message;
  return code;
}

/** Validate desktop key/value syntax and the in-process socket-only restriction. */
const char *invalid_parameters(const char **parameters)
{
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    if (parameters[i][0] == '\0' || parameters[i + 1] == nullptr) {
      return "parameters must be key/value pairs with non-empty keys";
    }
    if (std::strcmp(parameters[i], "port") == 0) {
      char *end = nullptr;
      errno = 0;
      long port = std::strtol(parameters[i + 1], &end, 10);
      if (errno != 0 || parameters[i + 1][0] == '\0' || *end != '\0' || port != 0) {
        return "TCP is not available for the in-process engine; port must be 0";
      }
    }
    if (std::strcmp(parameters[i], "mysql_port_mode") == 0
        && std::strcmp(parameters[i + 1], "disabled") != 0) {
      return "TCP is not available for the in-process engine; mysql_port_mode must be disabled";
    }
  }
  return nullptr;
}

void append_parameters(std::vector<std::string> &values, const char *const *parameters)
{
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; ++i) {
    values.emplace_back(parameters[i]);
  }
}

bool has_key(const std::vector<std::string> &parameters, const char *key)
{
  bool found = false;
  for (size_t i = 0; i < parameters.size(); i += 2) {
    found = found || parameters[i] == key;
  }
  return found;
}

std::vector<std::string> startup_parameters()
{
  std::vector<std::string> result = {"mysql_port_mode", "disabled"};
  append_parameters(result, seekdb_platform::extra_startup_parameters());
  return result;
}

std::vector<std::string> initial_parameters(const char **parameters, const std::vector<std::string> &startup)
{
  std::vector<std::string> result = {"memory_budget", "1G", "log_disk_size", "2G"};
  append_parameters(result, seekdb_platform::extra_initial_defaults());
  const size_t defaults = result.size();
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    bool overridden = false;
    for (size_t j = 0; j < defaults; j += 2) {
      if (result[j] == parameters[i]) {
        result[j + 1] = parameters[i + 1];
        overridden = true;
      }
    }
    if (!overridden && std::strcmp(parameters[i], "port") != 0 && !has_key(startup, parameters[i])) {
      result.emplace_back(parameters[i]);
      result.emplace_back(parameters[i + 1]);
    }
  }
  return result;
}

std::vector<const char *> parameter_pointers(const std::vector<std::string> &values)
{
  std::vector<const char *> result;
  for (const auto &value : values) {
    result.push_back(value.c_str());
  }
  result.push_back(nullptr);
  return result;
}

/** Release a sandbox socket alias and the opaque handle's owned strings. */
void destroy_handle(SeekdbHandleImpl *handle)
{
  if (handle != nullptr) {
    if (handle->socket_alias_dir != nullptr) {
      std::string link = std::string(handle->socket_alias_dir) + "/r";
      unlink(link.c_str());
      rmdir(handle->socket_alias_dir);
    }
    std::free(handle->socket_alias_dir);
    std::free(handle->sock_path);
    std::free(handle);
  }
}

bool prepare_socket(SeekdbHandleImpl *handle, const char *engine_socket)
{
  if (engine_socket == nullptr) {
    return false;
  }
  std::string socket = engine_socket;
  if (socket.size() >= sizeof(sockaddr_un::sun_path)) {
    std::string link;
    for (const std::string &pattern : seekdb_platform::socket_alias_patterns()) {
      if (pattern.size() + sizeof("/r/sql.sock") > sizeof(sockaddr_un::sun_path)) {
        continue;
      }
      std::vector<char> buffer(pattern.begin(), pattern.end());
      buffer.push_back('\0');
      if (mkdtemp(buffer.data()) != nullptr) {
        handle->socket_alias_dir = strdup(buffer.data());
        if (handle->socket_alias_dir == nullptr) {
          rmdir(buffer.data());
          return false;
        }
        link = std::string(buffer.data()) + "/r";
        break;
      }
    }
    if (link.empty() || symlink(socket.substr(0, socket.rfind('/')).c_str(), link.c_str()) != 0) {
      return false;
    }
    socket = link + "/sql.sock";
  }
  handle->sock_path = strdup(socket.c_str());
  return handle->sock_path != nullptr;
}
} // namespace

extern "C" {
/** Open the singleton on a background thread, blocking until its socket accepts SQL clients. */
int seekdb_open(const char *directory, const char **parameters, SeekdbHandle *out_handle)
{
  last_open_error.clear();
  if (out_handle == nullptr) {
    return open_failed(SEEKDB_INVALID_ARGUMENT, "out_handle must not be NULL");
  }
  *out_handle = nullptr;
  if (const char *message = invalid_parameters(parameters)) {
    return open_failed(SEEKDB_INVALID_ARGUMENT, message);
  }
  auto *handle = static_cast<SeekdbHandleImpl *>(std::calloc(1, sizeof(SeekdbHandleImpl)));
  if (handle == nullptr) {
    return open_failed(SEEKDB_INTERNAL_ERROR, "out of memory");
  }
  std::vector<std::string> startup = startup_parameters();
  std::vector<std::string> initial = initial_parameters(parameters, startup);
  std::vector<const char *> startup_pointers = parameter_pointers(startup);
  std::vector<const char *> initial_pointers = parameter_pointers(initial);
  int ret = seekdb_in_process_start(directory, startup_pointers.data(), initial_pointers.data());
  if (ret != oceanbase::common::OB_SUCCESS) {
    destroy_handle(handle);
    return open_failed(ret == oceanbase::common::OB_INVALID_ARGUMENT ? SEEKDB_INVALID_ARGUMENT : SEEKDB_INTERNAL_ERROR,
                       seekdb_in_process_get_start_error());
  }
  if (!prepare_socket(handle, seekdb_in_process_get_socket_path())) {
    destroy_handle(handle);
    return open_failed(SEEKDB_INTERNAL_ERROR, "cannot prepare the Unix socket path");
  }
  *out_handle = handle;
  return SEEKDB_SUCCESS;
}

int seekdb_connection_options(SeekdbHandle opaque, SeekdbConnectionOptions *out_options)
{
  if (opaque == nullptr || out_options == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  const auto *handle = static_cast<const SeekdbHandleImpl *>(opaque);
  *out_options = {};
  if (handle->sock_path == nullptr) {
    return SEEKDB_INTERNAL_ERROR;
  }
  out_options->transport = SEEKDB_CONNECTION_TRANSPORT_UNIX_SOCKET;
  out_options->unix_socket = handle->sock_path;
  out_options->user = "root";
  return SEEKDB_SUCCESS;
}

const char *seekdb_last_open_error(void)
{
  return last_open_error.c_str();
}

/** Release this handle; the engine keeps running until the process exits. */
int seekdb_close(SeekdbHandle opaque)
{
  if (opaque == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  destroy_handle(static_cast<SeekdbHandleImpl *>(opaque));
  return SEEKDB_SUCCESS;
}
}
