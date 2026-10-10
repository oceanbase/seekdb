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

#include "seekdb_ios.h"
#include <atomic>
#include <cstdlib>
#include <fcntl.h>
#include <dirent.h>
#include <cstring>
#include <unistd.h>
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
    "SEEKDB_IOS_ARTIFACT_BUILD_ID=" SEEKDB_IOS_BUILD_ID
    ";SEEKDB_IOS_ARTIFACT_HOOK_MODE=" SEEKDB_IOS_HOOK_MODE;
std::atomic<seekdb_ios_state> runtime_state{SEEKDB_IOS_IDLE};
std::atomic<bool> stop_requested{false};
std::atomic<unsigned int> cleanup_status{SEEKDB_IOS_CLEANUP_NONE};
std::atomic<int> cleanup_error{OB_SUCCESS};

/** Preserve the first cleanup failure for diagnostics. */
void record_cleanup_error(int error)
{
  int expected = OB_SUCCESS;
  if (OB_SUCCESS != error) {
    cleanup_error.compare_exchange_strong(expected, error);
  }
}

/** Create engine-owned directories and configure a bounded, socket-only server. */
int prepare_runtime(const char *directory, ObServerOptions &options,
                    const char *const *caller_parameters)
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
  const char *parameters[][2] = {
      {"mysql_port_mode", "disabled"}, {"sql_net_thread_count", "2"},
      {"cpu_count", "2"}};
  for (const auto &parameter : parameters) {
    if (OB_SUCC(ret)) {
      ret = options.parameters_.push_back(std::make_pair(
          ObString(parameter[0]), ObString(parameter[1])));
    }
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
  const char *defaults[][2] = {
      {"memory_budget", "1G"}, {"vector_memory_limit", "128M"},
      {"log_disk_size", "2G"}, {"datafile_maxsize", "20G"}};
  if (first_init) {
    for (const auto &parameter : defaults) {
      const char *value = parameter[1];
      for (size_t i = 0; caller_parameters != nullptr && caller_parameters[i] != nullptr; i += 2) {
        if (std::strcmp(caller_parameters[i], parameter[0]) == 0) {
          value = caller_parameters[i + 1];
        }
      }
      if (OB_SUCC(ret)) {
        ret = options.parameters_.push_back(std::make_pair(ObString(parameter[0]), ObString(value)));
      }
    }
    for (size_t i = 0; caller_parameters != nullptr && caller_parameters[i] != nullptr; i += 2) {
      const char *key = caller_parameters[i];
      if (std::strcmp(key, "port") == 0 || std::strcmp(key, "memory_budget") == 0
          || std::strcmp(key, "vector_memory_limit") == 0 || std::strcmp(key, "log_disk_size") == 0
          || std::strcmp(key, "datafile_maxsize") == 0
          || std::strcmp(key, "mysql_port_mode") == 0 || std::strcmp(key, "cpu_count") == 0
          || std::strcmp(key, "sql_net_thread_count") == 0) {
        continue;
      }
      if (OB_SUCC(ret)) {
        ret = options.parameters_.push_back(std::make_pair(ObString(key), ObString(caller_parameters[i + 1])));
      }
    }
  }
  return ret;
}

/** Stop and destroy an in-process server while preserving its primary error. */
int cleanup_server(ObServer &server, int primary_ret, bool server_initialized)
{
  server.set_stop();
  const int cleanup_ret = server.wait();
  server.destroy();
  const bool cleanup_succeeded = OB_SUCCESS == cleanup_ret
      || (!server_initialized && OB_NOT_INIT == cleanup_ret);
  if (cleanup_succeeded) {
    cleanup_status.fetch_or(SEEKDB_IOS_CLEANUP_SERVER);
  } else {
    record_cleanup_error(cleanup_ret);
  }
  return primary_ret == OB_SUCCESS ? cleanup_ret : primary_ret;
}

/** Start and stop the singleton without taking ownership of process signals. */
int run_runtime(ObServerOptions &options)
{
  int ret = OB_SUCCESS;
  lib::ObStackHeaderGuard stack_header_guard;
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  OB_LOGGER.set_log_level(DEFAULT_LOG_LEVEL);
  OB_LOGGER.set_file_name("log/seekdb.log", true, false);
  ObPLogWriterCfg log_config;
  ObServer &server = ObServer::get_instance();
  bool server_initialized = false;
  if (OB_FAIL(server.init(options, log_config))) {
  } else {
    server_initialized = true;
    if (OB_FAIL(server.start())) {
    }
    if (OB_SUCC(ret)) {
      runtime_state.store(SEEKDB_IOS_RUNNING);
      while (!stop_requested.load()) {
        usleep(100000);
      }
      runtime_state.store(SEEKDB_IOS_STOPPING);
      server.prepare_stop();
    }
  }
  ret = cleanup_server(server, ret, server_initialized);
  lib::Worker::set_worker_to_thread_local(nullptr);
  return ret;
}
} // namespace

int seekdb_ios_run(const char *absolute_directory)
{
  return seekdb_ios_run_with_parameters(absolute_directory, nullptr);
}

int seekdb_ios_run_with_parameters(const char *absolute_directory, const char *const *parameters)
{
  if (absolute_directory == nullptr || absolute_directory[0] != '/') {
    return OB_INVALID_ARGUMENT;
  }
  seekdb_ios_state expected = SEEKDB_IOS_IDLE;
  if (!runtime_state.compare_exchange_strong(expected, SEEKDB_IOS_STARTING)) {
    return OB_INIT_TWICE;
  }
  int ret = OB_SUCCESS;
  bool curl_initialized = false;
  cleanup_status.store(SEEKDB_IOS_CLEANUP_NONE);
  cleanup_error.store(OB_SUCCESS);
  const int previous_directory = open(".", O_RDONLY);
  if (previous_directory < 0) {
    ret = OB_IO_ERROR;
  } else {
    ObServerOptions options;
    if (OB_FAIL(prepare_runtime(absolute_directory, options, parameters))) {
    } else if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK) {
      ret = OB_ERR_UNEXPECTED;
    } else {
      curl_initialized = true;
      ret = run_runtime(options);
    }
    if (curl_initialized) {
      curl_global_cleanup();
      cleanup_status.fetch_or(SEEKDB_IOS_CLEANUP_CURL);
    }
    if (fchdir(previous_directory) != 0) {
      record_cleanup_error(OB_IO_ERROR);
      if (OB_SUCC(ret)) {
        ret = OB_IO_ERROR;
      }
    } else {
      cleanup_status.fetch_or(SEEKDB_IOS_CLEANUP_WORKING_DIRECTORY);
    }
    if (close(previous_directory) != 0) {
      record_cleanup_error(OB_IO_ERROR);
      if (OB_SUCC(ret)) {
        ret = OB_IO_ERROR;
      }
    }
  }
  runtime_state.store(OB_SUCC(ret) ? SEEKDB_IOS_STOPPED : SEEKDB_IOS_FAILED);
  return ret;
}

void seekdb_ios_request_stop(void)
{
  stop_requested.store(true);
}

seekdb_ios_state seekdb_ios_get_state(void)
{
  return runtime_state.load();
}

unsigned int seekdb_ios_get_cleanup_status(void)
{
  return cleanup_status.load();
}

int seekdb_ios_get_cleanup_error(void)
{
  return cleanup_error.load();
}

const char *seekdb_ios_get_build_id(void)
{
  return SEEKDB_IOS_BUILD_ID;
}

const char *seekdb_ios_get_hook_mode(void)
{
  return SEEKDB_IOS_HOOK_MODE;
}
