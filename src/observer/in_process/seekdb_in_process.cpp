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

#include "seekdb_in_process.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <dirent.h>
#include <cstring>
#include <limits.h>
#include <mutex>
#include <pthread.h>
#include <string>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>
#include <vector>
#include <curl/curl.h>
#include "lib/file/file_directory_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/thread/protected_stack_allocator.h"
#include "lib/worker.h"
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::observer;

namespace {
__attribute__((used)) const char artifact_metadata[] =
    "SEEKDB_IN_PROCESS_ARTIFACT_BUILD_ID=" SEEKDB_IN_PROCESS_BUILD_ID;

/** Engine states for the single-run-per-process in-process host. */
enum seekdb_in_process_state {
  SEEKDB_IN_PROCESS_IDLE = 0,
  SEEKDB_IN_PROCESS_STARTING = 1,
  SEEKDB_IN_PROCESS_RUNNING = 2,
  SEEKDB_IN_PROCESS_FAILED = 3
};
std::atomic<seekdb_in_process_state> runtime_state{SEEKDB_IN_PROCESS_IDLE};

/** Own one engine thread and the arguments retained for its entire lifetime. */
struct Engine {
  std::mutex mutex;
  bool started = false;
  std::string directory;
  std::string socket_path;
  std::vector<std::string> startup_storage;
  std::vector<std::string> initial_storage;
  std::vector<const char *> startup;
  std::vector<const char *> initial;
  std::atomic<int> result{OB_SUCCESS};
};
Engine engine;
thread_local std::string start_error;

int add_parameters(ObServerOptions &options, const char *const *parameters)
{
  int ret = OB_SUCCESS;
  for (size_t i = 0; OB_SUCC(ret) && parameters != nullptr && parameters[i] != nullptr; i += 2) {
    if (parameters[i + 1] == nullptr) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      ret = options.parameters_.push_back(std::make_pair(ObString(parameters[i]), ObString(parameters[i + 1])));
    }
  }
  return ret;
}

/** Create engine-owned directories and pass the caller's server parameters. */
int prepare_server(const char *directory, ObServerOptions &options,
                   const char *const *startup_parameters, const char *const *initial_parameters)
{
  int ret = options.base_dir_.assign(directory);
  if (OB_SUCC(ret)) {
    ret = FileDirectoryUtils::create_full_path(directory);
  }
  if (OB_SUCC(ret) && chdir(directory) != 0) {
    ret = OB_IO_ERROR;
  }
  for (const char *path : {"run", "etc", "log"}) {
    if (OB_SUCC(ret)) {
      ret = FileDirectoryUtils::create_full_path(path);
    }
  }
  options.in_process_ = true;
  options.nodaemon_ = true;
  if (OB_SUCC(ret)) {
    ret = add_parameters(options, startup_parameters);
  }
  // Seed caller configuration only for an uninitialized data directory.
  bool first_init = true;
  DIR *sstable = opendir("store/sstable");
  if (sstable != nullptr) {
    while (dirent *entry = readdir(sstable)) {
      if (std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0) {
        first_init = false;
        break;
      }
    }
    closedir(sstable);
  }
  if (OB_SUCC(ret) && first_init) {
    ret = add_parameters(options, initial_parameters);
  }
  return ret;
}

/** Start the singleton without taking ownership of process signals. */
int run_server(ObServerOptions &options)
{
  int ret = OB_SUCCESS;
  lib::ObStackHeaderGuard stack_header_guard;
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  OB_LOGGER.set_log_level(DEFAULT_LOG_LEVEL);
  OB_LOGGER.set_file_name("log/seekdb.log", true, false);
  ObServer &server = ObServer::get_instance();
  if (OB_FAIL(server.init(options))) {
  } else if (OB_FAIL(server.start())) {
  } else {
    runtime_state.store(SEEKDB_IN_PROCESS_RUNNING);
    while (true) {
      pause();
    }
  }
  lib::Worker::set_worker_to_thread_local(nullptr);
  return ret;
}

int run_engine(const char *absolute_directory, const char *const *startup_parameters,
               const char *const *initial_parameters)
{
  runtime_state.store(SEEKDB_IN_PROCESS_STARTING);
  int ret = OB_SUCCESS;
  ObServerOptions options;
  if (OB_FAIL(prepare_server(absolute_directory, options, startup_parameters, initial_parameters))) {
  } else if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = run_server(options);
  }
  return ret;
}

/** Run the in-process engine; it only returns when startup fails. */
void *engine_thread(void *)
{
  engine.result.store(run_engine(engine.directory.c_str(), engine.startup.data(), engine.initial.data()));
  runtime_state.store(SEEKDB_IN_PROCESS_FAILED);
  return nullptr;
}

int start_failed(int code, const char *message)
{
  start_error = message;
  return code;
}

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

std::vector<std::string> copy_parameters(const char *const *parameters)
{
  std::vector<std::string> values;
  for (size_t i = 0; parameters != nullptr && parameters[i] != nullptr; ++i) {
    values.emplace_back(parameters[i]);
  }
  return values;
}

std::vector<const char *> parameter_pointers(const std::vector<std::string> &values)
{
  std::vector<const char *> pointers;
  for (const auto &value : values) {
    pointers.push_back(value.c_str());
  }
  pointers.push_back(nullptr);
  return pointers;
}

/** Confirm that the running engine's Unix socket answers with a MySQL protocol greeting. */
bool socket_ready()
{
  int client = socket(AF_UNIX, SOCK_STREAM, 0);
  if (client < 0) {
    return false;
  }
  timeval timeout{1, 0};
  setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
  sockaddr_un address{};
  address.sun_family = AF_UNIX;
  std::strncpy(address.sun_path, "run/sql.sock", sizeof(address.sun_path) - 1);
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
    FILE *log = std::fopen((engine.directory + "/log/in_process.log").c_str(), "a");
    if (log != nullptr) {
      std::fprintf(log, "socket readiness attempt %u: %zu bytes, %s\n", failed_attempts, received, std::strerror(error));
      std::fclose(log);
    }
  }
  return ready;
}
} // namespace

int seekdb_in_process_start(const char *absolute_directory, const char *const *startup_parameters,
                            const char *const *initial_parameters)
{
  start_error.clear();
  if (absolute_directory == nullptr || absolute_directory[0] != '/') {
    return start_failed(OB_INVALID_ARGUMENT, "the directory must be an absolute path");
  }
  std::lock_guard<std::mutex> guard(engine.mutex);
  std::string canonical = canonical_directory(absolute_directory);
  if (canonical.empty()) {
    return start_failed(OB_INVALID_ARGUMENT, "cannot resolve the directory");
  }
  if (engine.started && engine.directory != canonical) {
    return start_failed(OB_STATE_NOT_MATCH, "another directory is already open in this process");
  }
  if (engine.started && runtime_state.load() != SEEKDB_IN_PROCESS_RUNNING) {
    return start_failed(OB_STATE_NOT_MATCH,
                        "the engine is not running in this process; start a new process to open a database again");
  }
  if (!engine.started) {
    engine.directory = canonical;
    engine.socket_path = canonical + "/run/sql.sock";
    engine.startup_storage = copy_parameters(startup_parameters);
    engine.initial_storage = copy_parameters(initial_parameters);
    engine.startup = parameter_pointers(engine.startup_storage);
    engine.initial = parameter_pointers(engine.initial_storage);
    pthread_t thread;
    pthread_attr_t attributes;
    int thread_error = pthread_attr_init(&attributes);
    if (thread_error == 0) {
      thread_error = pthread_attr_setstacksize(&attributes, 8 * 1024 * 1024);
      if (thread_error == 0) {
        thread_error = pthread_attr_setdetachstate(&attributes, PTHREAD_CREATE_DETACHED);
      }
      if (thread_error == 0) {
        thread_error = pthread_create(&thread, &attributes, engine_thread, nullptr);
      }
      pthread_attr_destroy(&attributes);
    }
    if (thread_error != 0) {
      return start_failed(OB_ERR_SYS, "cannot start the engine thread");
    }
    engine.started = true;
    bool ready = false;
    bool failed = false;
    for (unsigned int attempt = 0; attempt < 1500; ++attempt) {
      auto state = runtime_state.load();
      if (state == SEEKDB_IN_PROCESS_FAILED) {
        failed = true;
        break;
      }
      if (state == SEEKDB_IN_PROCESS_RUNNING && socket_ready()) {
        ready = true;
        break;
      }
      usleep(200000);
    }
    if (failed) {
      const int ret = engine.result.load();
      return start_failed(OB_SUCCESS == ret ? OB_ERR_UNEXPECTED : ret,
                          "the engine stopped during startup; see the log directory under the database directory");
    }
    if (!ready) {
      return start_failed(OB_TIMEOUT, "the engine did not accept SQL connections within 300 seconds; "
                                      "see the log directory under the database directory");
    }
  }
  return OB_SUCCESS;
}

const char *seekdb_in_process_get_socket_path(void)
{
  std::lock_guard<std::mutex> guard(engine.mutex);
  return runtime_state.load() == SEEKDB_IN_PROCESS_RUNNING ? engine.socket_path.c_str() : nullptr;
}

const char *seekdb_in_process_get_start_error(void)
{
  return start_error.c_str();
}
