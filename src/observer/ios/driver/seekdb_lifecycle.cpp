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
#include "../seekdb_ios.h"
#include <atomic>
#include <cerrno>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <limits.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <TargetConditionals.h>
#include <jemalloc/jemalloc.h>

/** Complete allocator bootstrap on the loading thread before engine workers can initialize it. */
__attribute__((constructor)) static void initialize_framework_allocator()
{
  // Refuse a partially initialized allocator before a short-lived loader thread can retain bootstrap TSD.
  void *allocation = je_malloc(1);
  if (allocation == nullptr) {
    std::abort();
  }
  je_free(allocation);
}

namespace {
struct SeekdbHandleImpl {
  char *db_dir;
  char *sock_path;
  char *socket_alias_dir;
};

/** Own one engine thread and the arguments retained for its entire lifetime. */
struct Engine {
  std::mutex mutex;
  pthread_t thread{};
  bool started = false;
  size_t handles = 0;
  std::string directory;
  std::vector<std::string> parameters;
  std::vector<const char *> arguments;
  std::atomic<int> result{0};
};
Engine engine;

/** Resolve existing parents so fresh and existing paths use the same singleton identity. */
std::string canonical_directory(const char *directory)
{
  std::string parent = directory;
  std::vector<std::string> suffix;
  char resolved[PATH_MAX];
  while (realpath(parent.c_str(), resolved) == nullptr) {
    size_t slash = parent.find_last_of('/');
    if (slash == std::string::npos || parent == "/") {
      return {};
    }
    suffix.push_back(parent.substr(slash + 1));
    parent = slash == 0 ? "/" : parent.substr(0, slash);
  }
  std::string result = resolved;
  for (auto part = suffix.rbegin(); part != suffix.rend(); ++part) {
    if (part->empty() || *part == ".") {
      continue;
    }
    if (*part == "..") {
      size_t slash = result.find_last_of('/');
      result = slash == 0 ? "/" : result.substr(0, slash);
    } else {
      result += (result == "/" ? "" : "/") + *part;
    }
  }
  return result;
}

/** Run the in-process engine; the caller joins before releasing its arguments. */
void *run_engine(void *)
{
  engine.result.store(seekdb_ios_run_with_parameters(engine.directory.c_str(), engine.arguments.data()));
  return nullptr;
}

/** Validate desktop key/value syntax and the iOS socket-only restriction. */
int validate_parameters(const char **parameters)
{
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; i += 2) {
    if (parameters[i][0] == '\0' || parameters[i + 1] == nullptr) {
      return SEEKDB_INVALID_ARGUMENT;
    }
    if (std::strcmp(parameters[i], "port") == 0) {
      char *end = nullptr;
      errno = 0;
      long port = std::strtol(parameters[i + 1], &end, 10);
      if (errno != 0 || parameters[i + 1][0] == '\0' || *end != '\0' || port != 0) {
        return SEEKDB_INVALID_ARGUMENT;
      }
    }
    if (std::strcmp(parameters[i], "mysql_port_mode") == 0
        && std::strcmp(parameters[i + 1], "disabled") != 0) {
      return SEEKDB_INVALID_ARGUMENT;
    }
  }
  return SEEKDB_SUCCESS;
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
    std::free(handle->db_dir);
    std::free(handle);
  }
}

/** Shorten long socket names inside the App home; simulator may use host /tmp. */
bool prepare_socket(SeekdbHandleImpl *handle)
{
  std::string socket = engine.directory + "/run/sql.sock";
  if (socket.size() >= sizeof(sockaddr_un::sun_path)) {
    const char *home = std::getenv("HOME");
    if (home == nullptr || home[0] != '/') {
      return false;
    }
    std::string writable_home = home;
    // /var and /private/var name the same container; the short spelling leaves room for its writable tmp directory.
    if (writable_home.compare(0, 13, "/private/var/") == 0) {
      writable_home.erase(0, 8);
    }
    std::string pattern = writable_home + "/tmp/.XXXXXX";
#if TARGET_OS_SIMULATOR
    if (pattern.size() + sizeof("/r/sql.sock") > sizeof(sockaddr_un::sun_path)) {
      pattern = "/tmp/seekdb-XXXXXX";
    }
#endif
    if (pattern.size() + sizeof("/r/sql.sock") > sizeof(sockaddr_un::sun_path)) {
      return false;
    }
    std::vector<char> buffer(pattern.begin(), pattern.end());
    buffer.push_back('\0');
    if (mkdtemp(buffer.data()) == nullptr) {
      return false;
    }
    handle->socket_alias_dir = strdup(buffer.data());
    if (handle->socket_alias_dir == nullptr) {
      rmdir(buffer.data());
      return false;
    }
    std::string link = std::string(buffer.data()) + "/r";
    if (symlink((engine.directory + "/run").c_str(), link.c_str()) != 0) {
      return false;
    }
    socket = link + "/sql.sock";
  }
  handle->sock_path = strdup(socket.c_str());
  return handle->sock_path != nullptr;
}

/** Confirm that the running engine's Unix socket answers with a MySQL protocol greeting. */
bool socket_ready(const SeekdbHandleImpl *handle)
{
  int client = socket(AF_UNIX, SOCK_STREAM, 0);
  if (client < 0) {
    return false;
  }
  timeval timeout{1, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, handle->sock_path, sizeof(address.sun_path) - 1);
  unsigned char greeting[5] = {};
  size_t received = 0;
  bool ready = connect(client, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == 0;
  while (ready && received < sizeof(greeting)) {
    ssize_t count = recv(client, greeting + received, sizeof(greeting) - received, 0);
    ready = count > 0;
    if (ready) {
      received += static_cast<size_t>(count);
    }
  }
  int error = errno;
  ready = ready && greeting[3] == 0 && greeting[4] == 10;
  close(client);
  static unsigned int failed_attempts = 0;
  if (!ready && ++failed_attempts % 50 == 1) {
    std::fprintf(stderr, "SeekDB socket readiness: %zu bytes, %s\n", received, std::strerror(error));
    FILE *log = std::fopen((engine.directory + "/log/ios-driver.log").c_str(), "a");
    if (log != nullptr) {
      std::fprintf(log, "socket readiness attempt %u: %zu bytes, %s\n", failed_attempts, received, std::strerror(error));
      std::fclose(log);
    }
  }
  return ready;
}

/** Stop and join the consumed engine thread before returning to the caller. */
int stop_engine()
{
  seekdb_ios_request_stop();
  if (pthread_join(engine.thread, nullptr) != 0) {
    return SEEKDB_INTERNAL_ERROR;
  }
  return engine.result.load() == 0 ? SEEKDB_SUCCESS : SEEKDB_INTERNAL_ERROR;
}
} // namespace

extern "C" {
/** Open the singleton on a background thread, blocking until its socket accepts SQL clients. */
int seekdb_open(const char *directory, const char **parameters, SeekdbHandle *out_handle)
{
  if (out_handle == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  *out_handle = nullptr;
  if (directory == nullptr || directory[0] != '/' || validate_parameters(parameters) != SEEKDB_SUCCESS) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> guard(engine.mutex);
  std::string canonical = canonical_directory(directory);
  if (canonical.empty()) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  if (engine.started && (engine.handles == 0 || engine.directory != canonical
      || seekdb_ios_get_state() != SEEKDB_IOS_RUNNING)) {
    return SEEKDB_INTERNAL_ERROR;
  }
  auto *handle = static_cast<SeekdbHandleImpl *>(std::calloc(1, sizeof(SeekdbHandleImpl)));
  if (handle == nullptr) {
    return SEEKDB_INTERNAL_ERROR;
  }
  if (!engine.started) {
    engine.directory = canonical;
  }
  handle->db_dir = strdup(canonical.c_str());
  if (handle->db_dir == nullptr || !prepare_socket(handle)) {
    destroy_handle(handle);
    return SEEKDB_INTERNAL_ERROR;
  }
  if (!engine.started) {
    for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; ++i) {
      engine.parameters.emplace_back(parameters[i]);
    }
    for (const auto &parameter : engine.parameters) {
      engine.arguments.push_back(parameter.c_str());
    }
    engine.arguments.push_back(nullptr);
    pthread_attr_t attributes;
    int thread_error = pthread_attr_init(&attributes);
    if (thread_error == 0) {
      thread_error = pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
      if (thread_error == 0) {
        thread_error = pthread_create(&engine.thread, &attributes, run_engine, nullptr);
      }
      pthread_attr_destroy(&attributes);
    }
    if (thread_error != 0) {
      engine.parameters.clear();
      engine.arguments.clear();
      destroy_handle(handle);
      return SEEKDB_INTERNAL_ERROR;
    }
    engine.started = true;
    bool ready = false;
    for (unsigned int attempt = 0; attempt < 1500; ++attempt) {
      auto state = seekdb_ios_get_state();
      if (state == SEEKDB_IOS_FAILED || state == SEEKDB_IOS_STOPPED) {
        break;
      }
      if (state == SEEKDB_IOS_RUNNING && socket_ready(handle)) {
        ready = true;
        break;
      }
      usleep(200000);
    }
    if (!ready) {
      stop_engine();
      destroy_handle(handle);
      return SEEKDB_INTERNAL_ERROR;
    }
  }
  ++engine.handles;
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
  out_options->endpoint = handle->sock_path;
  out_options->user = "root";
  return SEEKDB_SUCCESS;
}

/** Release this handle; the last handle stops and joins the one-shot engine. */
int seekdb_close(SeekdbHandle opaque)
{
  if (opaque == nullptr) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  std::lock_guard<std::mutex> guard(engine.mutex);
  if (engine.handles == 0) {
    return SEEKDB_INVALID_ARGUMENT;
  }
  --engine.handles;
  int result = engine.handles == 0 ? stop_engine() : SEEKDB_SUCCESS;
  destroy_handle(static_cast<SeekdbHandleImpl *>(opaque));
  return result;
}
}
