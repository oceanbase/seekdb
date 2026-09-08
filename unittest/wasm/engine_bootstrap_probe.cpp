// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Development probe: executes the production lifecycle against fresh MEMFS.
// Success is not proof of browser SQL or durable storage.
#include "observer/ob_server.h"
#include "observer/ob_server_options.h"
#include "lib/resource/achunk_mgr.h"
#include "lib/worker.h"
#include "lib/guard/ob_light_shared_gaurd.h"
#include "lib/container/ob_ext_ring_buffer.h"
#include "sql/session/ob_system_variable.h"
#include "sql/engine/expr/ob_expr_uuid.h"
#include "storage/blocksstable/ob_sstable.h"
#include "storage/meta_mem/ob_storage_meta_cache.h"
#include "storage/tablet/ob_table_store_util.h"
#include "bootstrap_sql_checks.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>
#include <emscripten/emscripten.h>

using namespace oceanbase;
using namespace oceanbase::common;

// Test control only: the application Worker lifecycle will own this handshake.
static std::atomic<int> client_probe_state{0};
extern "C" EMSCRIPTEN_KEEPALIVE int seekdb_probe_state()
{
  return client_probe_state.load(std::memory_order_acquire);
}
extern "C" EMSCRIPTEN_KEEPALIVE void seekdb_probe_finish(int success)
{
  int expected = 1;
  client_probe_state.compare_exchange_strong(expected, success ? 2 : 3);
}
extern "C" EMSCRIPTEN_KEEPALIVE nio_memory_client *seekdb_probe_connect(size_t capacity)
{
  if (client_probe_state.load(std::memory_order_acquire) != 1) return nullptr;
  return obmysql::global_sql_nio_server->connect_memory(capacity);
}

namespace {
extern "C" int ob_pthread_create(void **, void *(*)(void *), void *);
extern "C" int ob_pthread_tryjoin_np(void *);
extern "C" void ob_pthread_join(void *);

bool check_try_join()
{
  std::atomic<bool> release{false};
  void *thread = nullptr;
  const auto run = +[](void *arg) -> void * {
    auto &release = *static_cast<std::atomic<bool> *>(arg);
    while (!release.load(std::memory_order_acquire)) usleep(1000);
    return nullptr;
  };
  if (ob_pthread_create(&thread, run, &release) != OB_SUCCESS) return false;
  // The thread cannot exit until release, so a successful join is impossible.
  const int before_release = ob_pthread_tryjoin_np(thread);
  release.store(true, std::memory_order_release);
  ob_pthread_join(thread);
  return before_release == OB_EAGAIN;
}

int shared_payload_destructs = 0;
struct SharedPayload {
  int64_t counter = 0;
  ~SharedPayload() { ++shared_payload_destructs; }
  int64_t to_string(char *, const int64_t) const { return 0; }
};

bool check_shared_payload()
{
  ObArenaAllocator allocator;
  shared_payload_destructs = 0;
  ObLightSharedPtr<SharedPayload> first;
  if (first.construct(allocator) != OB_SUCCESS
      || reinterpret_cast<uintptr_t>(first.ptr()) % alignof(SharedPayload) != 0) return false;
  ObLightSharedPtr<SharedPayload> second(first);
  int64_t refs = 0;
  if (second.get_ref_cnt(refs) != OB_SUCCESS || refs != 2) return false;
  first.reset();
  if (shared_payload_destructs != 0 || ATOMIC_AAF(&second->counter, 1) != 1) return false;
  second.reset();
  return shared_payload_destructs == 1;
}

bool check_pointer_ring()
{
  ObExtendibleRingBuffer<SharedPayload> ring;
  SharedPayload payload;
  if (ring.init(0, 128) != OB_SUCCESS) return false;
  bool valid = true;
  for (int64_t i = 0; valid && i < 128; ++i) {
    SharedPayload *actual = nullptr;
    valid = ring.set(i, &payload) == OB_SUCCESS
        && ring.get(i, actual) == OB_SUCCESS && actual == &payload;
  }
  valid = valid && ring.begin_sn() == 0 && ring.end_sn() == 128;
  // Exercise real segment allocation/growth/free, including allocator tail guards.
  return ring.destroy() == OB_SUCCESS && valid;
}

bool check_sstable_copies()
{
  using namespace storage;
  using namespace blocksstable;
  ObArenaAllocator allocator;
  ObSSTable source;
  source.set_table_type(ObITable::MDS_MINOR_SSTABLE);
  share::ObScnRange range;
  range.start_scn_ = share::SCN::min_scn();
  range.end_scn_ = share::SCN::max_scn();
  source.set_scn_range(range);
  auto aligned_and_equal = [&](ObSSTable *table) {
    return table && reinterpret_cast<uintptr_t>(table) % alignof(ObSSTable) == 0
        && table->get_start_scn() == range.start_scn_
        && table->get_end_scn() == range.end_scn_;
  };
  // Both metadata wrappers and odd-length pointer arrays precede table bytes.
  for (int offset : {0, 4}) {
    ObStorageMetaValue value(ObStorageMetaValue::SSTABLE, &source);
    const int64_t size = value.size();
    char *buf = static_cast<char *>(allocator.alloc(size + 8));
    if (!buf) return false;
    std::memset(buf, 0x5a, size + 8);
    ObIKVCacheValue *copy = nullptr;
    if (value.deep_copy(buf + offset, size, copy) != OB_SUCCESS || !copy) return false;
    auto *metadata = static_cast<ObStorageMetaValue *>(copy);
    ObSSTable *table = nullptr;
    const bool valid = metadata->get_sstable(table) == OB_SUCCESS && aligned_and_equal(table)
        && buf[offset + size] == 0x5a;
    metadata->reset();
    metadata->~ObStorageMetaValue();
    if (!valid) return false;
    for (int count : {1, 2, 3}) {
      ObSEArray<ObITable *, 3> tables;
      for (int i = 0; i < count; ++i) if (tables.push_back(&source) != OB_SUCCESS) return false;
      ObSSTableArray array;
      if (array.init(allocator, tables) != OB_SUCCESS) return false;
      const int64_t array_size = array.get_deep_copy_size();
      char *array_buf = static_cast<char *>(allocator.alloc(array_size + 8));
      if (!array_buf) return false;
      std::memset(array_buf, 0x5a, array_size + 8);
      int64_t pos = offset;
      ObSSTableArray copied;
      if (array.deep_copy(array_buf, offset + array_size, pos, copied) != OB_SUCCESS
          || copied.count() != count || pos > offset + array_size
          || array_buf[offset + array_size] != 0x5a) return false;
      for (int i = 0; i < count; ++i) if (!aligned_and_equal(copied.at(i))) return false;
    }
  }
  return true;
}
}

static void logs()
{
  MAIN_THREAD_EM_ASM({
    try {
      for (const name of FS.readdir('/seekdb/log')) {
        if (name === '.' || name === '..') continue;
        const path = '/seekdb/log/' + name;
        if (FS.isFile(FS.stat(path).mode)) {
          out('=== ' + path + ' (tail) ===');
          out(FS.readFile(path, {encoding:'utf8'}).slice(-65536));
        }
      }
    } catch (error) { err('log export: ' + error); }
  });
}

int main(int argc, char **argv)
{
  std::fprintf(stderr, "bootstrap: entered main\n");
  std::fprintf(stderr, "bootstrap: system timezone %s\n", sql::ObSpecialSysVarValues::system_time_zone_str_);
  if (argc == 3 && std::strcmp(argv[1], "--static-init-only") == 0) {
    const char *uuid = sql::ObSpecialSysVarValues::server_uuid_;
    char binary[16] = {};
    bool valid = false;
    const int parse = sql::UuidCommon::uuid2bin(binary, valid, uuid, std::strlen(uuid));
    const bool log_timestamps = bootstrap_sql_checks::check_log_timestamps();
    const bool slog_file_ids = bootstrap_sql_checks::check_slog_file_ids();
    const bool integer_sql = bootstrap_sql_checks::check_integer_literals();
    const bool metadata_sql = bootstrap_sql_checks::check_metadata_updates();
    const bool schema_sql = bootstrap_sql_checks::check_schema_rows();
    const bool stats_sql = bootstrap_sql_checks::check_runtime_stats();
    const bool optimizer_sql = bootstrap_sql_checks::check_optimizer_defaults();
    const bool try_join = check_try_join();
    std::fprintf(stderr, "bootstrap: pending thread join %s\n", try_join ? "passed" : "failed");
    std::fprintf(stderr, "bootstrap: integer literals %s\n", integer_sql ? "passed" : "failed");
    std::fprintf(stderr, "bootstrap: log timestamps %s\n", log_timestamps ? "passed" : "failed");
    std::fprintf(stderr, "bootstrap: slog file IDs %s\n", slog_file_ids ? "passed" : "failed");
    std::fprintf(stderr, "bootstrap: metadata SQL %s, schema SQL %s, runtime stats SQL %s, optimizer SQL %s\n",
        metadata_sql ? "passed" : "failed", schema_sql ? "passed" : "failed",
        stats_sql ? "passed" : "failed", optimizer_sql ? "passed" : "failed");
    const bool matches = try_join && slog_file_ids && log_timestamps && integer_sql && metadata_sql && schema_sql && stats_sql && optimizer_sql
        && check_shared_payload() && check_pointer_ring() && check_sstable_copies()
        && std::strcmp(argv[2], sql::ObSpecialSysVarValues::system_time_zone_str_) == 0
        && parse == OB_SUCCESS && valid && (static_cast<unsigned char>(binary[6]) >> 4) == 1
        && (static_cast<unsigned char>(binary[8]) & 0xc0) == 0x80 && (binary[10] & 1) != 0;
    std::fprintf(stderr, "bootstrap: static initialization %s\n", matches ? "passed" : "failed");
    return matches ? 0 : 1;
  }
  lib::AChunkMgr::instance().set_limit(INT64_C(1536) * 1024 * 1024);
  lib::AChunkMgr::instance().set_hard_limit(INT64_C(1536) * 1024 * 1024);
  if (mkdir("/seekdb", 0755) != 0 || chdir("/seekdb") != 0) return 1;
  for (const char *path : {"run", "log", "etc"}) if (mkdir(path, 0755) != 0) return 1;
  observer::ObServerOptions options;
  options.embedded_ = true;
  options.nodaemon_ = true;
  options.log_level_ = OB_LOG_LEVEL_WARN;
  if (options.base_dir_.assign("/seekdb") != OB_SUCCESS) return 1;
  const char *settings[][2] = {
    {"memory_budget", "1G"}, {"kvcache_memory_limit", "64M"},
    // The production freezer reserves 100 MiB for replay before user writes.
    {"memstore_memory_limit", "192M"}, {"vector_memory_limit", "64M"},
    {"cpu_count", "2"}, {"net_thread_count", "1"}, {"sql_net_thread_count", "1"},
    {"mysql_port_mode", "disabled"}, {"datafile_size", "32M"},
    {"log_disk_size", argc == 2 && std::strcmp(argv[1], "--invalid-log-budget") == 0 ? "256M" : "2G"},
    {"stack_size", "1M"},
  };
  for (const auto &setting : settings) {
    if (options.parameters_.push_back({ObString::make_string(setting[0]),
                                      ObString::make_string(setting[1])}) != OB_SUCCESS) return 1;
  }
  OB_LOGGER.set_log_level(OB_LOG_LEVEL_WARN);
  OB_LOGGER.set_file_name("log/observer.log", true);
  ObPLogWriterCfg log_config;
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  std::fprintf(stderr, "bootstrap: constructing server\n");
  auto &server = observer::ObServer::get_instance();
  std::fprintf(stderr, "bootstrap: initializing\n");
  int ret = server.init(options, log_config);
  std::fprintf(stderr, "bootstrap: init returned %d\n", ret);
  if (ret == OB_SUCCESS) {
    auto *schema_service = server.get_schema_service().get_schema_service();
    const bool matches = schema_service != nullptr
        && bootstrap_sql_checks::check_schema_refresh_sql(*schema_service);
    std::fprintf(stderr, "bootstrap: schema refresh SQL %s\n", matches ? "passed" : "failed");
    if (!matches) ret = OB_ERR_UNEXPECTED;
  }
  if (ret == OB_SUCCESS) {
    ret = server.start();
    std::fprintf(stderr, "bootstrap: start returned %d\n", ret);
  }
  const bool started = ret == OB_SUCCESS;
  if (started && argc == 2 && std::strcmp(argv[1], "--client-service") == 0) {
    client_probe_state.store(1, std::memory_order_release);
    std::fprintf(stderr, "bootstrap: client service ready\n");
    while (client_probe_state.load(std::memory_order_acquire) == 1) usleep(1000);
    if (client_probe_state.load(std::memory_order_acquire) != 2) ret = OB_ERR_UNEXPECTED;
  }
  logs();
  server.set_stop();
  if (started) {
    const int wait_ret = server.wait();
    std::fprintf(stderr, "bootstrap: wait returned %d\n", wait_ret);
    if (ret == OB_SUCCESS) ret = wait_ret;
  }
  std::fprintf(stderr, "bootstrap: destroying\n");
  server.destroy();
  std::fprintf(stderr, "bootstrap: destroy returned\n");
  logs();
  return ret == OB_SUCCESS ? 0 : 1;
}
