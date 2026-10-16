/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "observer/include/seekdb_engine.h"
#include "observer/ob_command_line_parser.h"
#include "observer/ob_server.h"
#include "observer/ob_server_utils.h"
#include "share/ob_errno.h"
#include "lib/file/file_directory_utils.h"
#include "lib/thread/protected_stack_allocator.h"
#include "lib/worker.h"
#include <curl/curl.h>
#include <climits>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>
#include <fcntl.h>
#include <sys/stat.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::observer;

struct SeekdbEngineImpl {
  std::mutex mutex;
  std::condition_variable condition;
  std::thread thread;
  std::vector<std::string> arguments;
  bool ready = false;
  bool closing = false;
  int result = OB_SUCCESS;
  char error_message[2048] = {};
};

namespace {
std::mutex engine_mutex;
SeekdbEngineHandle active_engine = nullptr;
bool core_init_attempted = false;
// The failed engine is deleted before open returns. Keep its diagnostic on the
// calling thread, independent of other callers and without allocating on failure.
thread_local char last_open_error[2048] = {};

void set_error(char (&message)[2048], int code, const char *format, ...)
{
  if (message[0] != '\0') { return; } // Preserve the first failure during cleanup.
  va_list args;
  va_start(args, format);
  vsnprintf(message, sizeof(message), format, args);
  va_end(args);
  const size_t used = strlen(message);
  snprintf(message + used, sizeof(message) - used, "; code=%d (%s)", code, ob_strerror(code));
}

void set_system_error(char (&message)[2048], int code, const char *operation,
                      const char *path, int saved_errno)
{
  set_error(message, code, "%s failed%s%s%s: errno=%d (%s)", operation,
            path == nullptr ? "" : " for '", path == nullptr ? "" : path,
            path == nullptr ? "" : "'", saved_errno, strerror(saved_errno));
}

// Unlike start_daemon(), retain the descriptor so close releases the PID lock.
int acquire_pid_file(int &fd, char (&message)[2048])
{
  fd = ::open("run/seekdb.pid", O_RDWR | O_CREAT, 0600);
  if (fd < 0) {
    set_system_error(message, OB_IO_ERROR, "open PID file", "run/seekdb.pid", errno);
    return OB_IO_ERROR;
  }
#ifdef _WIN32
  if (_locking(fd, _LK_NBLCK, 1) != 0) {
#else
  if (lockf(fd, F_TLOCK, 0) != 0) {
#endif
    const int saved_errno = errno;
    set_system_error(message, OB_INIT_TWICE, "lock PID file", "run/seekdb.pid", saved_errno);
    ::close(fd);
    fd = -1;
    return OB_INIT_TWICE;
  }
#ifdef _WIN32
  if (_chsize(fd, 0) != 0) {
#else
  if (ftruncate(fd, 0) != 0) {
#endif
    set_system_error(message, OB_IO_ERROR, "truncate PID file", "run/seekdb.pid", errno);
    return OB_IO_ERROR;
  }
  char pid[32];
  const int length = snprintf(pid, sizeof(pid), "%d", static_cast<int>(getpid()));
  const auto written = ::write(fd, pid, length);
  if (written < 0) {
    set_system_error(message, OB_IO_ERROR, "write PID file", "run/seekdb.pid", errno);
    return OB_IO_ERROR;
  } else if (written != length) {
    set_error(message, OB_IO_ERROR, "Short write to PID file 'run/seekdb.pid': wrote %lld of %d bytes",
              static_cast<long long>(written), length);
    return OB_IO_ERROR;
  }
  return OB_SUCCESS;
}

// The parser only borrows this pointer array during the call. The strings
// remain owned by the engine for options that retain references to arguments.
int parse_engine_arguments(const std::vector<std::string> &arguments, ObServerOptions &options)
{
  try {
    std::vector<const char *> argv;
    argv.reserve(arguments.size());
    for (const auto &argument : arguments) { argv.push_back(argument.c_str()); }
    ObCommandLineParser parser;
    return parser.parse_args(static_cast<int>(argv.size()), argv.data(), options, false);
  } catch (const std::bad_alloc &) {
    return OB_ALLOCATE_MEMORY_FAILED;
  }
}

void run_engine(SeekdbEngineHandle engine)
{
  lib::ObStackHeaderGuard stack_guard;
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  int ret = OB_SUCCESS;
  int pid_fd = -1;
  bool entered_core = false;
  bool curl_initialized = false;
  bool changed_directory = false;
  char previous_directory[4096];
  ObServerOptions options;
  options.log_level_ = DEFAULT_LOG_LEVEL;
  options.in_process_ = true;
  options.nodaemon_ = true;

  if (nullptr == getcwd(previous_directory, sizeof(previous_directory))) {
    const int saved_errno = errno;
    ret = OB_IO_ERROR;
    set_system_error(engine->error_message, ret, "getcwd", nullptr, saved_errno);
  } else if (OB_FAIL(parse_engine_arguments(engine->arguments, options))) {
    set_error(engine->error_message, ret, "Failed to parse engine arguments");
  } else if (OB_FAIL(ObServerUtils::prepare_directories(options))) {
    set_error(engine->error_message, ret, "Failed to prepare directories: base='%s', data='%s', redo='%s'",
              options.base_dir_.empty() ? "" : options.base_dir_.ptr(),
              options.data_dir_.empty() ? "" : options.data_dir_.ptr(),
              options.redo_dir_.empty() ? "" : options.redo_dir_.ptr());
  } else if (::chdir(options.base_dir_.ptr()) != 0) {
    const int saved_errno = errno;
    ret = OB_IO_ERROR;
    set_system_error(engine->error_message, ret, "chdir", options.base_dir_.ptr(), saved_errno);
  } else {
    changed_directory = true;
#ifndef _WIN32
    struct stat info;
    if (::stat("etc", &info) == 0 && info.st_uid != getuid()) {
      ret = OB_FILE_OR_DIRECTORY_PERMISSION_DENIED;
      set_error(engine->error_message, ret, "Directory 'etc' in '%s' belongs to uid=%llu, current uid=%llu",
                options.base_dir_.ptr(), static_cast<unsigned long long>(info.st_uid),
                static_cast<unsigned long long>(getuid()));
    }
#endif
    for (const char *directory : {"run", "etc", "log"}) {
      if (OB_SUCC(ret) && OB_FAIL(FileDirectoryUtils::create_full_path(directory))) {
        set_error(engine->error_message, ret, "Failed to create directory '%s' in '%s'",
                  directory, options.base_dir_.ptr());
      }
    }
    if (OB_SUCC(ret)) { ret = acquire_pid_file(pid_fd, engine->error_message); }
    if (OB_SUCC(ret)) {
      const CURLcode curl_ret = curl_global_init(CURL_GLOBAL_ALL);
      if (curl_ret != CURLE_OK) {
        ret = OB_ERR_UNEXPECTED;
        set_error(engine->error_message, ret, "curl_global_init failed: curl_code=%d (%s)",
                  static_cast<int>(curl_ret), curl_easy_strerror(curl_ret));
      } else {
        curl_initialized = true;
        OB_LOGGER.set_log_level(options.log_level_);
        OB_LOGGER.set_file_name("log/seekdb.log", true /* no_redirect */, false);
        ObPLogWriterCfg log_config;
        core_init_attempted = true;
        entered_core = true;
        ObServer &server = ObServer::get_instance();
        if (OB_FAIL(server.init(options, log_config))) {
          set_error(engine->error_message, ret, "Failed to initialize seekdb server; see '%slog/seekdb.log'",
                    options.base_dir_.ptr());
        } else if (OB_FAIL(server.start())) {
          set_error(engine->error_message, ret, "Failed to start seekdb server; see '%slog/seekdb.log'",
                    options.base_dir_.ptr());
        }
      }
    }
  }
  if (OB_SUCC(ret)) {
    std::unique_lock<std::mutex> guard(engine->mutex);
    engine->ready = true;
    engine->condition.notify_all();
    engine->condition.wait(guard, [engine] { return engine->closing; });
  }
  if (entered_core) {
    ObServer &server = ObServer::get_instance();
    server.set_stop();
    const int stop_ret = server.wait();
    server.destroy();
    if (OB_SUCC(ret)) {
      ret = stop_ret;
      if (OB_FAIL(ret)) { set_error(engine->error_message, ret, "Failed to wait for seekdb server shutdown"); }
    }
  }
  if (curl_initialized) { curl_global_cleanup(); }
  if (pid_fd >= 0) {
    // Remove our pathname while the lock is still held.
    if (::unlink("run/seekdb.pid") != 0 && OB_SUCC(ret)) {
      const int saved_errno = errno;
      ret = OB_IO_ERROR;
      set_system_error(engine->error_message, ret, "unlink PID file", "run/seekdb.pid", saved_errno);
    }
    if (::close(pid_fd) != 0 && OB_SUCC(ret)) {
      const int saved_errno = errno;
      ret = OB_IO_ERROR;
      set_system_error(engine->error_message, ret, "close PID file", "run/seekdb.pid", saved_errno);
    }
  }
  if (changed_directory && ::chdir(previous_directory) != 0 && OB_SUCC(ret)) {
    const int saved_errno = errno;
    ret = OB_IO_ERROR;
    set_system_error(engine->error_message, ret, "restore cwd", previous_directory, saved_errno);
  }
  lib::Worker::set_worker_to_thread_local(nullptr);
  std::lock_guard<std::mutex> guard(engine->mutex);
  engine->result = ret;
  engine->ready = true;
  engine->condition.notify_all();
}
} // namespace

SeekdbEngineHandle seekdb_engine_open(int argc, const char *const argv[], int *error_code,
                                     const char **error_message)
{
  std::lock_guard<std::mutex> lifecycle_guard(engine_mutex);
  int ret = OB_SUCCESS;
  SeekdbEngineHandle engine = nullptr;
  last_open_error[0] = '\0';
  if (argc < 0 || argc == INT_MAX || (argc > 0 && argv == nullptr)) {
    ret = OB_INVALID_ARGUMENT;
    set_error(last_open_error, ret, "Invalid argc/argv: argc=%d, argv is %s", argc, argv == nullptr ? "NULL" : "non-NULL");
  } else if (active_engine != nullptr) {
    ret = OB_INIT_TWICE;
    set_error(last_open_error, ret, "A seekdb engine is already open");
  } else if (core_init_attempted) {
    // Do not pretend the current singleton graph can be initialized twice.
    ret = OB_NOT_SUPPORTED;
    set_error(last_open_error, ret, "Cannot reopen seekdb after core initialization has already been attempted");
  } else {
    for (int i = 0; i < argc; ++i) {
      if (argv[i] == nullptr) {
        ret = OB_INVALID_ARGUMENT;
        set_error(last_open_error, ret, "Invalid argument: argv[%d] is NULL", i);
        break;
      }
    }
    if (OB_SUCC(ret)) {
      try {
        engine = new SeekdbEngineImpl;
        for (int i = 0; i < argc; ++i) { engine->arguments.emplace_back(argv[i]); }
        engine->thread = std::thread(run_engine, engine);
        std::unique_lock<std::mutex> guard(engine->mutex);
        engine->condition.wait(guard, [engine] { return engine->ready; });
        ret = engine->result;
        if (OB_FAIL(ret)) { snprintf(last_open_error, sizeof(last_open_error), "%s", engine->error_message); }
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        set_error(last_open_error, ret, "Failed to allocate engine startup state or argument copies");
      } catch (const std::system_error &error) {
        ret = OB_ERR_UNEXPECTED;
        set_error(last_open_error, ret, "Engine startup thread/synchronization failed: system_code=%d, %s",
                  error.code().value(), error.what());
      }
      if (OB_FAIL(ret)) {
        if (engine != nullptr && engine->thread.joinable()) {
          {
            std::lock_guard<std::mutex> guard(engine->mutex);
            engine->closing = true;
            engine->condition.notify_all();
          }
          engine->thread.join();
        }
        delete engine;
        engine = nullptr;
      } else {
        active_engine = engine;
      }
    }
  }
  if (error_code != nullptr) { *error_code = ret; }
  if (OB_FAIL(ret) && last_open_error[0] == '\0') {
    set_error(last_open_error, ret, "Failed to open seekdb engine");
  }
  if (error_message != nullptr) { *error_message = OB_SUCCESS == ret ? nullptr : last_open_error; }
  return engine;
}

int seekdb_engine_close(SeekdbEngineHandle engine)
{
  std::lock_guard<std::mutex> lifecycle_guard(engine_mutex);
  if (engine == nullptr) { return OB_SUCCESS; }
  if (engine != active_engine) { return OB_INVALID_ARGUMENT; }
  {
    std::lock_guard<std::mutex> guard(engine->mutex);
    engine->closing = true;
    engine->condition.notify_all();
  }
  engine->thread.join();
  const int ret = engine->result;
  active_engine = nullptr;
  delete engine;
  return ret;
}
