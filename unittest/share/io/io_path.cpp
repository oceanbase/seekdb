/*
 * Copyright (c) 2026 OceanBase.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#include "share/ob_local_device.h"
#include "share/io/ob_io_manager.h"
#include "lib/time/ob_clock_generator.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <vector>
#include <sys/resource.h>

using namespace oceanbase::common;
using namespace oceanbase::share;
using Clock = std::chrono::steady_clock;
constexpr size_t depth = 32;
constexpr int64_t chunk = 256 * 1024;

struct Slot {
  ObIOHandle handle;
  std::vector<char> buffer;
  uint64_t offset = 0;
  uint64_t sequence = 0;
  bool active = false;
  Clock::time_point submitted;
};

static uint64_t pattern(uint64_t position) {
  uint64_t x = position + 0x9e3779b97f4a7c15ULL;
  x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
  x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
  return x ^ (x >> 31);
}
static double seconds(Clock::duration d) { return std::chrono::duration<double>(d).count(); }
static double cpu_seconds(const rusage &r) {
  return r.ru_utime.tv_sec + r.ru_stime.tv_sec + (r.ru_utime.tv_usec + r.ru_stime.tv_usec) / 1e6;
}
static void phase(const char *name) {
  const auto now = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
  std::printf("{\"phase\":\"%s\",\"monotonic_ns\":%ld}\n", name, now);
}
static bool ok(int status, const char *operation) {
  if (status == OB_SUCCESS) return true;
  std::fprintf(stderr, "%s failed status=%d\n", operation, status);
  return false;
}

int main(int argc, char **argv) {
  if (argc != 7) {
    std::fprintf(stderr, "usage: fixture PATH native|worker read|write FILE_MIB OPERATIONS validate|measure\n");
    return 2;
  }
  const bool native = std::strcmp(argv[2], "native") == 0;
  const bool write = std::strcmp(argv[3], "write") == 0;
  const bool measure = std::strcmp(argv[6], "measure") == 0;
  const uint64_t file_size = std::strtoull(argv[4], nullptr, 10) * 1024 * 1024;
  const uint64_t operations = std::strtoull(argv[5], nullptr, 10);
  const uint64_t size = write ? chunk : 16 * 1024;
  if ((!native && std::strcmp(argv[2], "worker")) || (!write && std::strcmp(argv[3], "read")) ||
      (!measure && std::strcmp(argv[6], "validate")) || file_size < chunk || file_size % chunk ||
      operations < depth || operations > 100000000 || file_size > 8ULL * 1024 * 1024 * 1024 ||
      (write && operations < file_size / size)) return 2;
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  OB_LOGGER.set_log_level(static_cast<int8_t>(OB_LOG_LEVEL_WARN));
  if (!ok(ObClockGenerator::init(), "clock init")) return 1;
  void *image_pointer = nullptr, *scratch_pointer = nullptr;
  if (posix_memalign(&image_pointer, 4096, file_size) || posix_memalign(&scratch_pointer, 4096, chunk)) return 1;
  std::unique_ptr<char, decltype(&std::free)> image(static_cast<char *>(image_pointer), &std::free);
  std::unique_ptr<char, decltype(&std::free)> scratch(static_cast<char *>(scratch_pointer), &std::free);
  auto words = reinterpret_cast<uint64_t *>(image.get());
  for (uint64_t i = 0; i < file_size / 8; ++i) words[i] = pattern(i);
  std::vector<Slot> slots(depth);
  for (auto &slot : slots) slot.buffer.resize(size);
  std::vector<uint64_t> latencies(operations);
  ObLocalDevice device;
  device.device_type_ = OB_STORAGE_LOCAL;
  LocalIOConfig config;
  config.backend = native ? LocalIOBackend::NATIVE_AIO : LocalIOBackend::WORKER;
  config.direct_io = true;
  ObIODOpts empty;
  ObIOFd fd;
  auto &manager = ObIOManager::get_instance();
  bool manager_initialized = false, opened = false, success = true;
  uint64_t accepted = 0, completed = 0, submit_errors = 0, completion_errors = 0, short_io = 0, data_errors = 0;
  uint64_t max_outstanding = 0, active = 0;
  double elapsed = 0, sync_elapsed = 0, cpu_elapsed = 0;
  rusage usage_begin{}, usage_end{};
  do {
    if (!ok(device.init(empty, config), "device init")) { success = false; break; }
    if (!ok(device.open(argv[1], O_CREAT | O_EXCL | O_RDWR | O_DIRECT, 0600, fd), "open")) { success = false; break; }
    opened = true;
    fd.device_handle_ = &device;
    if (!ok(device.fallocate(fd, 0, 0, file_size), "fallocate")) { success = false; break; }
    for (uint64_t offset = 0; offset < file_size; offset += chunk) {
      std::memcpy(scratch.get(), image.get() + offset, chunk);
      if (write) for (int64_t i = 0; i < chunk; ++i) scratch.get()[i] ^= 0x5a;
      int64_t bytes = 0;
      if (!ok(device.pwrite(fd, offset, chunk, scratch.get(), bytes), "initialize") || bytes != chunk) { success = false; break; }
    }
    if (!success || !ok(device.fdatasync(fd), "initialize sync")) { success = false; break; }
    if (!ok(manager.init(ObIORuntimeOptions(10LL << 30), 512LL << 20, 4096, 1), "manager init")) { success = false; break; }
    manager_initialized = true;
    if (!ok(manager.add_device_channel(&device, 1, 4, 512), "device channel") || !ok(manager.start(), "manager start")) { success = false; break; }
    auto finish = [&](Slot &slot) {
      if (!slot.active) return;
      const int status = slot.handle.wait(60000);
      if (status != OB_SUCCESS) {
        ++completion_errors;
        int fs_error = 0;
        slot.handle.get_fs_errno(fs_error);
        std::fprintf(stderr, "completion sequence=%lu status=%d fs_errno=%d\n", slot.sequence, status, fs_error);
        success = false;
      } else if (slot.handle.get_data_size() != static_cast<int64_t>(size)) {
        ++short_io; success = false;
      } else {
        ++completed;
        if (!write && std::memcmp(slot.buffer.data(), image.get() + slot.offset, size)) {
          ++data_errors;
          std::fprintf(stderr, "read bytes differ at sequence=%lu offset=%lu\n", slot.sequence, slot.offset);
          success = false;
        }
      }
      latencies[slot.sequence] = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - slot.submitted).count();
      slot.handle.reset();
      slot.active = false;
      --active;
    };
    phase("io_begin");
    getrusage(RUSAGE_SELF, &usage_begin);
    const auto begin = Clock::now();
    for (uint64_t sequence = 0; sequence < operations && success; ++sequence) {
      auto &slot = slots[sequence % depth];
      finish(slot);
      if (!success) break;
      slot.offset = (write ? sequence % (file_size / size) : pattern(sequence) % (file_size / size)) * size;
      slot.sequence = sequence;
      ObIOInfo info;
      info.fd_ = fd;
      info.offset_ = slot.offset;
      info.size_ = size;
      info.timeout_us_ = 60000000;
      info.flag_.set_wait_event(ObWaitEventIds::DB_FILE_DATA_READ);
      info.flag_.set_unlimited(true);
      if (write) {
        info.flag_.set_write(); info.flag_.set_sys_module_id(ObIOModule::CLOG_WRITE_IO);
        info.buf_ = image.get() + slot.offset;
      } else {
        info.flag_.set_read(); info.flag_.set_sys_module_id(ObIOModule::CLOG_READ_IO);
        info.user_data_buf_ = slot.buffer.data();
      }
      slot.submitted = Clock::now();
      const int status = write ? manager.aio_write(info, slot.handle) : manager.aio_read(info, slot.handle);
      if (status != OB_SUCCESS) {
        ++submit_errors; success = false;
        std::fprintf(stderr, "submission sequence=%lu status=%d\n", sequence, status);
      } else {
        ++accepted; ++active; slot.active = true;
        max_outstanding = std::max(max_outstanding, active);
      }
    }
    for (auto &slot : slots) finish(slot);
    elapsed = seconds(Clock::now() - begin);
    getrusage(RUSAGE_SELF, &usage_end);
    cpu_elapsed = cpu_seconds(usage_end) - cpu_seconds(usage_begin);
    phase("io_end");
    if (!success) break;
    if (!write) {
      for (const auto &slot : slots) {
        if (std::memcmp(slot.buffer.data(), image.get() + slot.offset, size)) { success = false; break; }
      }
    }
    const auto sync_begin = Clock::now();
    if (write && !ok(device.fdatasync(fd), "final write sync")) success = false;
    sync_elapsed = write ? seconds(Clock::now() - sync_begin) : 0;
    for (uint64_t offset = 0; success && offset < file_size; offset += chunk) {
      int64_t bytes = 0;
      if (!ok(device.pread(fd, offset, chunk, scratch.get(), bytes), "verify file") || bytes != chunk ||
          std::memcmp(scratch.get(), image.get() + offset, chunk)) success = false;
    }
  } while (false);
  for (auto &slot : slots) slot.handle.reset();
  if (manager_initialized) manager.destroy();
  if (opened && !ok(device.close(fd), "close")) success = false;
  device.destroy();
  ObClockGenerator::destroy();
  success = success && accepted == operations && completed == operations && max_outstanding == depth;
  std::printf("{\"success\":%s,\"mode\":\"%s\",\"backend\":\"%s\",\"operation\":\"%s\",\"file_bytes\":%lu,\"request_bytes\":%lu,\"operations\":%lu,\"accepted\":%lu,\"completed\":%lu,\"logical_completed_bytes\":%lu,\"submission_errors\":%lu,\"completion_errors\":%lu,\"short_io\":%lu,\"data_errors\":%lu,\"read_validation_in_timed_region\":true,\"maximum_unretired_handles\":%lu,\"configured_application_depth\":32,\"configured_device_depth\":512,\"workers\":4,\"native_channels\":1,\"unlimited\":true,\"direct_io\":true,\"full_file_verified_after\":%s",
      success ? "true" : "false", measure ? "measure" : "validate", argv[2], argv[3], file_size, size, operations,
      accepted, completed, completed * size, submit_errors, completion_errors, short_io, data_errors, max_outstanding, success ? "true" : "false");
  if (measure && success) {
    std::sort(latencies.begin(), latencies.end());
    std::printf(",\"completion_seconds_including_drain\":%.9f,\"final_fdatasync_seconds\":%.9f,\"cpu_seconds_including_drain\":%.9f,\"max_rss_kib\":%ld,\"observed_latency_p50_ns\":%lu,\"observed_latency_p99_ns\":%lu", elapsed, sync_elapsed, cpu_elapsed, usage_end.ru_maxrss, latencies[(operations-1)/2], latencies[(operations-1)*99/100]);
  }
  std::puts("}");
  return success ? 0 : 1;
}
