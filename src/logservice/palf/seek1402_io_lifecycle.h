/*
 * Copyright (c) 2025 OceanBase.
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

#pragma once
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
#include <cstddef>
#include <new>
#if defined(__linux__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/mman.h>
#endif
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

namespace oceanbase { namespace palf {
struct Seek1402IOPhase {
  bool entered = false;
  bool returned = false;
  int result = 0;
  int64_t begin_us = 0;
  int64_t end_us = 0;
};
inline int64_t seek1402_io_now_us() {
  return std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
}
template <typename Call>
int seek1402_observe_io_phase(Seek1402IOPhase *phase, Call call) {
  if (phase == nullptr) return call();
  phase->entered = true;
  phase->begin_us = seek1402_io_now_us();
  const int result = call();
  phase->end_us = seek1402_io_now_us();
  phase->result = result;
  phase->returned = true;
  return result;
}
inline bool seek1402_write_io_phase(FILE *file, const Seek1402IOPhase &phase) {
  if (!phase.entered) return std::fprintf(file, "null") >= 0;
  if (!phase.returned) return std::fprintf(file, "{\"returned\":false,\"result\":null}") >= 0;
  return std::fprintf(file, "{\"returned\":true,\"result\":%d,\"begin_us\":%lld,\"end_us\":%lld}",
      phase.result, static_cast<long long>(phase.begin_us), static_cast<long long>(phase.end_us)) >= 0;
}
// Diagnostic storage outlives tasks. No slot is recycled within a process.
struct Seek1402IOSlot {
  int64_t epoch = 0;
  uint64_t lsn = 0;
  int64_t length = 0;
  uint64_t batch_group = 0;
  int64_t batch_member = -1;
  int64_t batch_size = 0;
  uint64_t reuse_end_lsn = 0;
  Seek1402IOPhase append;
  Seek1402IOPhase reuse;
  Seek1402IOPhase push;
  Seek1402IOPhase callback_handler;
  Seek1402IOPhase after_flush;
  Seek1402IOPhase max_flushed;
  Seek1402IOPhase committed;
  bool callback_epoch_present = false;
  int64_t callback_epoch = 0;
  bool active_observed = false;
  bool active = false;
  bool observe_max_store = false;
  bool observe_committed_cas = false;
  bool max_store = false;
  uint64_t max_store_to = 0;
  uint64_t committed_cas_count = 0;
  uint64_t committed_cas_from = 0;
  uint64_t committed_cas_to = 0;
  bool submission_failure_present = false;
  int submission_failure_ret = 0;
  int producer_ret = 0;
  int callback_ret = 0;
  bool callback_skipped = false;
  std::atomic<bool> producer_done{false};
  std::atomic<bool> consumer_seen{false};
  std::atomic<bool> callback_done{false};
  std::atomic<bool> freed{false};
  std::atomic<bool> dropped_before_push{false};
  void produced(int ret) {
    producer_ret = ret;
    producer_done.store(true, std::memory_order_release);
  }
  void consumed(int ret, bool skipped) {
    callback_ret = ret;
    callback_skipped = skipped;
    callback_handler.result = ret;
    callback_handler.end_us = seek1402_io_now_us();
    callback_handler.returned = true;
    callback_done.store(true, std::memory_order_release);
  }
  void released() {
    if (!consumer_seen.load(std::memory_order_acquire) &&
        !producer_done.load(std::memory_order_acquire)) {
      dropped_before_push.store(true, std::memory_order_release);
    }
    freed.store(true, std::memory_order_release);
  }
  bool terminal() const {
    const bool produced = producer_done.load(std::memory_order_acquire);
    const bool consumed = consumer_seen.load(std::memory_order_acquire);
    const bool dropped = dropped_before_push.load(std::memory_order_acquire);
    return freed.load(std::memory_order_acquire) &&
      !(submission_failure_present && consumed) &&
      ((produced && ((producer_ret == 0 && consumed) ||
                     (producer_ret != 0 && !consumed))) || (dropped && !consumed)) &&
      (!consumed || callback_done.load(std::memory_order_acquire));
  }
};
inline Seek1402IOSlot *&seek1402_callback_slot() {
  static thread_local Seek1402IOSlot *slot = nullptr;
  return slot;
}
struct Seek1402CallbackScope {
  Seek1402IOSlot *previous;
  explicit Seek1402CallbackScope(Seek1402IOSlot *slot) : previous(seek1402_callback_slot()) {
    seek1402_callback_slot() = slot;
  }
  ~Seek1402CallbackScope() { seek1402_callback_slot() = previous; }
};
template <typename Call>
int seek1402_observe_callback_epoch(int64_t &epoch, Call call) {
  const int result = call();
  Seek1402IOSlot *slot = seek1402_callback_slot();
  if (slot != nullptr && result == 0) {
    slot->callback_epoch = epoch;
    slot->callback_epoch_present = true;
  }
  return result;
}

struct Seek1402IOLifecycle {
  static constexpr uint64_t limit = 8192;
  static constexpr uint64_t closed = uint64_t{1} << 63;
  static constexpr uint64_t overflow = uint64_t{1} << 62;
  static constexpr uint64_t count_mask = overflow - 1;
  Seek1402IOSlot slots[limit];
  std::atomic<uint64_t> gate{0};
  std::atomic<bool> exported{false};
  std::atomic<uintptr_t> bound_env{0};
  std::atomic<uint64_t> init_calls{0};
  std::atomic<uint64_t> batch_sequence{0};
  std::atomic<bool> multiple_envs{false};
  const char *path;
  char run_id[129] = {};
  bool identity_ready = false;
  Seek1402IOLifecycle() : path(std::getenv("SEEK1402_IO_LIFECYCLE_FILE")) {
    const char *identity = std::getenv("SEEK1402_IO_CAPTURE_RUN_ID");
    if (identity != nullptr) {
      size_t count = 0;
      for (; count < sizeof(run_id) - 1 && identity[count] != '\0'; ++count) {
        const char c = identity[count];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.')) return;
        run_id[count] = c;
      }
      identity_ready = count > 0 && identity[count] == '\0';
    }
  }
  bool enabled() const { return path != nullptr && *path != '\0' && identity_ready; }
  void bind_env(const void *environment) {
    if (path == nullptr || *path == '\0') return;
    if (init_calls.fetch_add(1) != 0) multiple_envs.store(true, std::memory_order_release);
    uintptr_t expected = 0;
    const uintptr_t address = reinterpret_cast<uintptr_t>(environment);
    if (!bound_env.compare_exchange_strong(expected, address) && expected != address) {
      multiple_envs.store(true, std::memory_order_release);
    }
  }
  Seek1402IOSlot *reserve(int64_t epoch, uint64_t lsn, int64_t length) {
    if (!enabled()) return nullptr;
    uint64_t state = gate.load(std::memory_order_acquire);
    for (;;) {
      if (state & (closed | overflow)) return nullptr;
      if ((state & count_mask) == limit) {
        if (gate.compare_exchange_weak(state, state | overflow)) return nullptr;
      } else if (gate.compare_exchange_weak(state, state + 1)) {
        Seek1402IOSlot *slot = &slots[state];
        slot->epoch = epoch;
        slot->lsn = lsn;
        slot->length = length;
        return slot;
      }
    }
  }
  // Only call after the PALF producer/consumer threads have joined.
  bool export_after_join() {
    if (path == nullptr || *path == '\0') return true;
    if (exported.exchange(true)) return false;
    const uint64_t state = gate.fetch_or(closed, std::memory_order_acq_rel);
    const uint64_t count = state & count_mask;
    uint64_t unfinished = 0;
    for (uint64_t i = 0; i < count; ++i) if (!slots[i].terminal()) ++unfinished;
    FILE *file = std::fopen(path, "wx");
    if (file == nullptr) return false;
#ifdef _WIN32
    const int writer_pid = _getpid();
#else
    const int writer_pid = getpid();
#endif
    bool ok = std::fprintf(file,
      "{\"schema\":1,\"writer_pid\":%d,\"run_id\":\"%s\",\"identity_valid\":%s,\"scope\":\"flush_task_append_reuse_and_lifecycle\",\"clock\":\"realtime_us\",\"slots\":%llu,\"unfinished\":%llu,\"overflow\":%s,\"multiple_envs\":%s,\"progress_phases_present\":false,\"io_phases_present\":true}\n",
      writer_pid, identity_ready ? run_id : "", identity_ready ? "true" : "false",
      static_cast<unsigned long long>(count), static_cast<unsigned long long>(unfinished),
      state & overflow ? "true" : "false", multiple_envs.load() ? "true" : "false") >= 0;
    for (uint64_t i = 0; i < count && ok; ++i) {
      const auto &slot = slots[i];
      if (!slot.terminal()) {
        ok = std::fprintf(file, "{\"slot\":%llu,\"incomplete\":true}\n",
            static_cast<unsigned long long>(i)) >= 0;
        continue;
      }
      const bool producer = slot.producer_done.load(std::memory_order_acquire);
      const bool callback = slot.callback_done.load(std::memory_order_acquire);
      char submission_result[24] = "null";
      if (slot.submission_failure_present) std::snprintf(submission_result, sizeof(submission_result), "%d", slot.submission_failure_ret);
      char producer_result[24] = "null";
      char callback_result[24] = "null";
      if (producer) std::snprintf(producer_result, sizeof(producer_result), "%d", slot.producer_ret);
      if (callback) std::snprintf(callback_result, sizeof(callback_result), "%d", slot.callback_ret);
      ok = std::fprintf(file,
        "{\"slot\":%llu,\"epoch\":%lld,\"lsn\":%llu,\"length\":%lld,\"submission_failure_ret\":%s,\"producer_done\":%s,\"producer_ret\":%s,\"consumer_seen\":%s,\"callback_done\":%s,\"callback_ret\":%s,\"callback_skipped\":%s,\"freed\":%s,\"dropped_before_push\":%s,\"batch_group\":%llu,\"batch_member\":%lld,\"batch_size\":%lld,\"reuse_end_lsn\":%llu,\"append\":",
        static_cast<unsigned long long>(i), static_cast<long long>(slot.epoch),
        static_cast<unsigned long long>(slot.lsn), static_cast<long long>(slot.length),
        submission_result, producer ? "true" : "false", producer_result,
        slot.consumer_seen.load(std::memory_order_acquire) ? "true" : "false",
        callback ? "true" : "false", callback_result,
        callback && slot.callback_skipped ? "true" : "false",
        slot.freed.load(std::memory_order_acquire) ? "true" : "false",
        slot.dropped_before_push.load(std::memory_order_acquire) ? "true" : "false",
        static_cast<unsigned long long>(slot.batch_group), static_cast<long long>(slot.batch_member),
        static_cast<long long>(slot.batch_size), static_cast<unsigned long long>(slot.reuse_end_lsn)) >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.append);
      ok = ok && std::fprintf(file, ",\"reuse\":") >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.reuse);
      ok = ok && std::fprintf(file, ",\"push\":") >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.push);
      ok = ok && std::fprintf(file, ",\"callback_handler\":") >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.callback_handler);
      ok = ok && std::fprintf(file, ",\"after_flush\":") >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.after_flush);
      ok = ok && std::fprintf(file, ",\"max_flushed\":") >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.max_flushed);
      ok = ok && std::fprintf(file, ",\"committed\":") >= 0;
      ok = ok && seek1402_write_io_phase(file, slot.committed);
      char callback_epoch[32] = "null";
      char max_store_to[32] = "null";
      char committed_from[32] = "null";
      char committed_to[32] = "null";
      if (slot.callback_epoch_present) std::snprintf(callback_epoch, sizeof(callback_epoch), "%lld", static_cast<long long>(slot.callback_epoch));
      if (slot.max_store) std::snprintf(max_store_to, sizeof(max_store_to), "%llu", static_cast<unsigned long long>(slot.max_store_to));
      if (slot.committed_cas_count != 0) {
        std::snprintf(committed_from, sizeof(committed_from), "%llu", static_cast<unsigned long long>(slot.committed_cas_from));
        std::snprintf(committed_to, sizeof(committed_to), "%llu", static_cast<unsigned long long>(slot.committed_cas_to));
      }
      ok = ok && std::fprintf(file,
          ",\"callback_epoch\":%s,\"active\":%s,\"max_store_to\":%s,\"committed_cas_count\":%llu,\"committed_cas_from\":%s,\"committed_cas_to\":%s",
          callback_epoch, slot.active_observed ? (slot.active ? "true" : "false") : "null", max_store_to,
          static_cast<unsigned long long>(slot.committed_cas_count),
          committed_from, committed_to) >= 0;
      ok = ok && std::fprintf(file, "}\n") >= 0;
    }
    ok = !std::ferror(file) && ok;
    ok = std::fclose(file) == 0 && ok;
    return ok && identity_ready && unfinished == 0 && !(state & overflow) && !multiple_envs.load();
  }
};
static_assert(sizeof(Seek1402IOLifecycle) <= 4 * 1024 * 1024,
              "diagnostic lifecycle storage exceeds 4MiB");
static_assert(std::is_trivially_destructible<Seek1402IOLifecycle>::value,
              "diagnostic storage must not register a teardown destructor");
// Raw mapping is decoded only after the controller observes writer exit.
inline Seek1402IOLifecycle *seek1402_create_mapped_capture(const char *raw_path) {
#if defined(__linux__) || defined(__APPLE__)
  static_assert(std::atomic<uint64_t>::is_always_lock_free && std::atomic<bool>::is_always_lock_free,
                "mapped diagnostic counters require lock-free atomics");
  static_assert(std::is_standard_layout<Seek1402IOLifecycle>::value &&
                std::is_standard_layout<Seek1402IOSlot>::value,
                "raw descriptor requires standard layout");
  constexpr size_t header_bytes = 4096;
  constexpr size_t file_bytes = header_bytes + sizeof(Seek1402IOLifecycle);
  static_assert(file_bytes <= 4 * 1024 * 1024, "mapped capture exceeds fixed budget");
  const int fd = ::open(raw_path, O_RDWR | O_CREAT | O_EXCL, 0600);
  if (fd < 0) return nullptr;
  if (::ftruncate(fd, file_bytes) != 0) { ::close(fd); return nullptr; }
  void *mapping = ::mmap(nullptr, file_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  ::close(fd);
  if (mapping == MAP_FAILED) return nullptr;
  auto *capture = new(static_cast<char *>(mapping) + header_bytes) Seek1402IOLifecycle();
  char *header = static_cast<char *>(mapping);
  size_t used = 0;
  bool ok = true;
  auto append = [&](const char *format, auto... values) {
    if (!ok) return;
    const int count = std::snprintf(header + used, header_bytes - used, format, values...);
    if (count < 0 || static_cast<size_t>(count) >= header_bytes - used) ok = false;
    else used += static_cast<size_t>(count);
  };
  const uint16_t endian = 1;
  append("{\"schema\":1,\"format\":\"seek1402-native-layout\",\"initialization_complete\":true,"
         "\"writer_pid\":%lld,\"run_id\":\"%s\",\"little_endian\":%s,\"pointer_bytes\":%zu,"
         "\"file_bytes\":%zu,\"capture_offset\":%zu,\"slot_bytes\":%zu,\"slots_limit\":8192,"
         "\"atomic_u64_bytes\":%zu,\"atomic_bool_bytes\":%zu,\"threads_joined\":false,"
         "\"progress_phases_present\":false,\"capture_offsets\":{",
         static_cast<long long>(::getpid()), capture->identity_ready ? capture->run_id : "",
         *reinterpret_cast<const unsigned char *>(&endian) == 1 ? "true" : "false", sizeof(void *),
         file_bytes, header_bytes, sizeof(Seek1402IOSlot), sizeof(std::atomic<uint64_t>),
         sizeof(std::atomic<bool>));
#define SEEK1402_CAPTURE_OFFSET(field) append("\"" #field "\":%zu,", offsetof(Seek1402IOLifecycle, field))
  SEEK1402_CAPTURE_OFFSET(gate);
  SEEK1402_CAPTURE_OFFSET(init_calls);
  SEEK1402_CAPTURE_OFFSET(multiple_envs);
#undef SEEK1402_CAPTURE_OFFSET
  append("\"slots\":%zu},\"slot_offsets\":{", offsetof(Seek1402IOLifecycle, slots));
#define SEEK1402_SLOT_OFFSET(field) append("\"" #field "\":%zu,", offsetof(Seek1402IOSlot, field))
  SEEK1402_SLOT_OFFSET(epoch); SEEK1402_SLOT_OFFSET(lsn); SEEK1402_SLOT_OFFSET(length);
  SEEK1402_SLOT_OFFSET(batch_group); SEEK1402_SLOT_OFFSET(batch_member); SEEK1402_SLOT_OFFSET(batch_size);
  SEEK1402_SLOT_OFFSET(reuse_end_lsn); SEEK1402_SLOT_OFFSET(append); SEEK1402_SLOT_OFFSET(reuse);
  SEEK1402_SLOT_OFFSET(push); SEEK1402_SLOT_OFFSET(callback_handler); SEEK1402_SLOT_OFFSET(after_flush);
  SEEK1402_SLOT_OFFSET(max_flushed); SEEK1402_SLOT_OFFSET(committed);
  SEEK1402_SLOT_OFFSET(callback_epoch_present); SEEK1402_SLOT_OFFSET(callback_epoch);
  SEEK1402_SLOT_OFFSET(active_observed); SEEK1402_SLOT_OFFSET(active);
  SEEK1402_SLOT_OFFSET(max_store); SEEK1402_SLOT_OFFSET(max_store_to);
  SEEK1402_SLOT_OFFSET(committed_cas_count); SEEK1402_SLOT_OFFSET(committed_cas_from); SEEK1402_SLOT_OFFSET(committed_cas_to);
  SEEK1402_SLOT_OFFSET(submission_failure_present); SEEK1402_SLOT_OFFSET(submission_failure_ret);
  SEEK1402_SLOT_OFFSET(producer_ret); SEEK1402_SLOT_OFFSET(callback_ret); SEEK1402_SLOT_OFFSET(callback_skipped);
  SEEK1402_SLOT_OFFSET(producer_done); SEEK1402_SLOT_OFFSET(consumer_seen); SEEK1402_SLOT_OFFSET(callback_done);
  SEEK1402_SLOT_OFFSET(freed);
#undef SEEK1402_SLOT_OFFSET
  append("\"dropped_before_push\":%zu},\"phase_offsets\":{", offsetof(Seek1402IOSlot, dropped_before_push));
  append("\"entered\":%zu,\"returned\":%zu,\"result\":%zu,\"begin_us\":%zu,\"end_us\":%zu},\"phase_bytes\":%zu}\n",
         offsetof(Seek1402IOPhase, entered), offsetof(Seek1402IOPhase, returned), offsetof(Seek1402IOPhase, result),
         offsetof(Seek1402IOPhase, begin_us), offsetof(Seek1402IOPhase, end_us), sizeof(Seek1402IOPhase));
  if (!ok) { ::munmap(mapping, file_bytes); return nullptr; }
  // Process lifetime mapping; _Exit does not need a destructor or a synchronous flush.
  return capture;
#else
  (void)raw_path;
  return nullptr; // Unsupported platforms must fail raw-file admission, not claim capture.
#endif
}
inline Seek1402IOLifecycle &seek1402_io_lifecycle() {
  static Seek1402IOLifecycle *capture = [] {
    const char *raw_path = std::getenv("SEEK1402_IO_RAW_FILE");
    if (raw_path != nullptr && *raw_path != '\0') {
      if (auto *mapped = seek1402_create_mapped_capture(raw_path)) return mapped;
    }
    return new Seek1402IOLifecycle();
  }();
  return *capture;
}
} }
