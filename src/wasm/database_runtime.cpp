// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// One ObServer lifetime per module. The owning JS Worker calls only the C ABI;
// main runs on an Emscripten pthread so filesystem proxies can make progress.
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"
#include "common/ob_timeout_ctx.h"
#include "lib/oblog/ob_warning_buffer.h"
#include "lib/resource/achunk_mgr.h"
#include "lib/worker.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include <atomic>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <emscripten/emscripten.h>

using namespace oceanbase;
using namespace oceanbase::common;

#if defined(SEEKDB_WASMFS)
bool seekdb_mount_storage(bool persistent);
#endif

namespace {
enum State { STARTING = 0, READY = 1, CLOSING = 2, CLOSED = 3, FAILED = 4 };
std::atomic<int> state{STARTING};
std::atomic<bool> closing{false};

bool directory(const char *path)
{
  if (mkdir(path, 0755) == 0) return true;
  struct stat info;
  return errno == EEXIST && stat(path, &info) == 0 && S_ISDIR(info.st_mode);
}

bool positive_integer(const char *input, unsigned &value)
{
  if (*input < '0' || *input > '9') return false;
  char *end = nullptr;
  errno = 0;
  const unsigned long parsed = std::strtoul(input, &end, 10);
  if (errno || *end || parsed == 0 || parsed > 4096) return false;
  value = static_cast<unsigned>(parsed);
  return true;
}
}

extern "C" EMSCRIPTEN_KEEPALIVE int seekdb_runtime_state()
{
  return state.load(std::memory_order_acquire);
}

// Called only by the owning Worker, after it closes all memory connections.
// Safe during initialization as well: main completes initialization/cleanup.
extern "C" EMSCRIPTEN_KEEPALIVE void seekdb_runtime_close()
{
  int expected = READY;
  state.compare_exchange_strong(expected, CLOSING);
  closing.store(true, std::memory_order_release);
}

extern "C" EMSCRIPTEN_KEEPALIVE nio_memory_client *seekdb_runtime_connect(size_t capacity)
{
  if (state.load(std::memory_order_acquire) != READY || closing.load(std::memory_order_acquire)) return nullptr;
  return obmysql::global_sql_nio_server->connect_memory(capacity);
}

extern "C" EMSCRIPTEN_KEEPALIVE int seekdb_runtime_set_external_tcp_port(unsigned port)
{
  if (port > UINT16_MAX || state.load(std::memory_order_acquire) != READY
      || closing.load(std::memory_order_acquire) || obmysql::global_sql_nio_server == nullptr) {
    return -1;
  }
  return obmysql::global_sql_nio_server->set_external_tcp_port(static_cast<uint16_t>(port)) ? 0 : -1;
}

int main(int argc, char **argv)
{
  // Positional integer arguments and the storage word are constructed/validated
  // by runtime-host.mjs. Keep their backing strings alive until server destruction.
  unsigned budget[] = {1024, 1536, 64, 192, 64, 2, 32, 2048};
  if (argc != 10) return 1;
  for (int i = 0; i < 8; ++i) if (!positive_integer(argv[i + 1], budget[i])) return 1;
  if (budget[0] > budget[1] || budget[1] > 1536 || budget[5] > 4
      || budget[2] + budget[3] + budget[4] >= budget[0]) return 1;
  const bool persistent = std::strcmp(argv[9], "opfs") == 0;
  if (!persistent && std::strcmp(argv[9], "memory") != 0) return 1;
#if defined(SEEKDB_WASMFS)
  if (!seekdb_mount_storage(persistent)) return 1;
#else
  if (persistent) return 1;
#endif
  lib::AChunkMgr::instance().set_limit(static_cast<int64_t>(budget[1]) * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(static_cast<int64_t>(budget[1]) * 1024 * 1024);
  if (!directory("/seekdb") || chdir("/seekdb") != 0) return 1;
  for (const char *path : {"run", "log", "etc"}) if (!directory(path)) return 1;
  observer::ObServerOptions options;
  options.embedded_ = true;
  options.nodaemon_ = true;
  options.log_level_ = OB_LOG_LEVEL_WARN;
  if (options.base_dir_.assign("/seekdb") != OB_SUCCESS) return 1;
  const char *names[] = {"memory_budget", "allocator_limit", "kvcache_memory_limit",
    "memstore_memory_limit", "vector_memory_limit", "cpu_count", "datafile_size", "log_disk_size"};
  char values[8][16];
  for (int i = 0; i < 8; ++i) {
    if (i == 1) continue; // Allocator limit is applied above, not a server option.
    std::snprintf(values[i], sizeof(values[i]), i == 5 ? "%u" : "%uM", budget[i]);
    if (options.parameters_.push_back({ObString::make_string(names[i]), ObString::make_string(values[i])}) != OB_SUCCESS) return 1;
  }
  const char *settings[][2] = {{"net_thread_count", "1"}, {"sql_net_thread_count", "1"},
    {"mysql_port_mode", "disabled"}, {"stack_size", "1M"}};
  for (const auto &setting : settings) {
    if (options.parameters_.push_back({ObString::make_string(setting[0]), ObString::make_string(setting[1])}) != OB_SUCCESS) return 1;
  }
  OB_LOGGER.set_log_level(OB_LOG_LEVEL_WARN);
  OB_LOGGER.set_file_name("log/observer.log", true);
  if (!persistent) OB_LOGGER.set_max_file_size(8 * 1024 * 1024);
  ObWarningBuffer::set_warn_log_on(true);
  ObPLogWriterCfg log_config;
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  auto &server = observer::ObServer::get_instance();
  int ret = OB_SUCCESS;
  {
    ObTimeoutCtx startup_timeout;
    if (!startup_timeout.is_timeout_set()) {
      ret = startup_timeout.set_timeout(5LL * 60 * 1000000);
    }
    if (ret == OB_SUCCESS) ret = server.init(options, log_config);
    if (ret == OB_SUCCESS) ret = server.start();
  }
  const bool started = ret == OB_SUCCESS;
  if (started) {
    state.store(closing.load(std::memory_order_acquire) ? CLOSING : READY, std::memory_order_release);
    while (!closing.load(std::memory_order_acquire)) usleep(1000);
  }
  state.store(CLOSING, std::memory_order_release);
  server.set_stop();
  if (started) {
    const int wait_ret = server.wait();
    if (ret == OB_SUCCESS) ret = wait_ret;
  }
  server.destroy();
  // Descriptors left open here would be torn down by WasmFS while still open;
  // on the OPFS backend that trips an assertion and keeps the files locked.
  for (int fd = 3; fd < 65536; ++fd) close(fd);
  state.store(ret == OB_SUCCESS ? CLOSED : FAILED, std::memory_order_release);
  std::fprintf(stderr, "seekdb-runtime: destroyed, status %d\n", ret);
  return ret == OB_SUCCESS ? 0 : 1;
}
