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
#include "seek1402_io_notify.h"
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

inline uint64_t &seek1402_space_current_call() {
  static thread_local uint64_t id = 0;
  return id;
}
struct Seek1402SpaceCall {
  uint64_t first_point = UINT64_MAX;
  int64_t result = 0;
  uint64_t allocator_upper = 0;
  int64_t allocator_id_upper = 0;
  int64_t allocator_result = 0;
  uint64_t allocated_lsn = 0;
  int64_t allocated_log_id = 0;
  int64_t rejected_log_id = 0;
  uint64_t rejected_max_lsn = 0;
  bool allocator_entered = false;
  bool allocator_returned = false;
  bool allocator_output_present = false;
  bool allocator_reject_present = false;
  bool abort_scope = false;
  std::atomic<bool> ready{false};
};
struct Seek1402SpacePoint {
  uint64_t call_id = 0;
  uint64_t group_buffer = 0;
  uint64_t value[8] = {};
  bool abort_scope = false;
  std::atomic<bool> ready{false};
};

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
  Seek1402IONotify notify;
  Seek1402SpacePoint space_points[limit];
  std::atomic<uint64_t> space_gate{0};
  Seek1402SpaceCall space_calls[limit];
  std::atomic<uint64_t> space_call_gate{0};
  std::atomic<bool> space_orphan{false};
  bool space_enabled = false;
  uint64_t space_headroom_threshold = 0;
  bool space_config_error = false;
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
      const char *space_option = std::getenv("SEEK1402_SPACE_CAPTURE");
      space_enabled = identity_ready && space_option != nullptr && std::strcmp(space_option, "1") == 0;
      const char *threshold = std::getenv("SEEK1402_SPACE_HEADROOM_BYTES");
      if (threshold != nullptr && *threshold != '\0') {
        const size_t length = std::strlen(threshold);
        bool valid = space_enabled && length <= 7;
        for (size_t i = 0; valid && i < length; ++i) valid = threshold[i] >= '0' && threshold[i] <= '9';
        if (valid) {
          space_headroom_threshold = std::strtoull(threshold, nullptr, 10);
          valid = space_headroom_threshold > 0 && space_headroom_threshold <= 4194304;
        }
        space_config_error = !valid;
      }
    }
  }
  bool enabled() const { return path != nullptr && *path != '\0' && identity_ready; }
  void bind_env(const void *environment) {
    if (path == nullptr || *path == '\0') return;
    if (init_calls.fetch_add(1) != 0) multiple_envs.store(true, std::memory_order_release);
    else if (enabled()) {
      notify.initialize();
      if (space_config_error) notify.lost.fetch_add(1, std::memory_order_relaxed);
    }
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
  void record_space_comparison(uintptr_t buffer, uint64_t size, uint64_t lsn,
                               uint64_t committed, uint64_t reuse, uint64_t capacity,
                               uint64_t base, uint64_t upper, bool passed, bool abort_scope) {
    if (!space_enabled || !enabled()) return;
    uint64_t state = space_gate.load(std::memory_order_acquire);
    for (;;) {
      if (state & (closed | overflow)) return;
      if ((state & count_mask) == limit) {
        if (space_gate.compare_exchange_weak(state, state | overflow)) return;
      } else if (space_gate.compare_exchange_weak(state, state + 1)) {
        auto &point = space_points[state];
        point.call_id = seek1402_space_current_call();
        if (point.call_id == 0 || point.call_id > limit) {
          space_orphan.store(true, std::memory_order_relaxed);
        } else {
          auto &call = space_calls[point.call_id - 1];
          if (call.first_point == UINT64_MAX) call.first_point = state;
        }
        point.group_buffer = buffer;
        const uint64_t values[8] = {size, lsn, committed, reuse, capacity, base, upper, passed ? 1UL : 0UL};
        for (int i = 0; i < 8; ++i) point.value[i] = values[i];
        point.abort_scope = abort_scope;
        point.ready.store(true, std::memory_order_release);
        // Only a historical comparison hint; later reuse/allocation may change the available range.
        if (!space_config_error && space_headroom_threshold > 0 && !abort_scope &&
            point.call_id > 0 && (upper < lsn || upper - lsn < space_headroom_threshold)) {
          notify.space_compared(state, point.call_id);
        }
        return;
      }
    }
  }
  void record_allocator_rejection(int64_t new_log_id, uint64_t new_max_lsn) {
    const uint64_t id = seek1402_space_current_call();
    if (!space_enabled || id == 0 || id > limit) return;
    auto &call = space_calls[id - 1];
    call.rejected_log_id = new_log_id;
    call.rejected_max_lsn = new_max_lsn;
    call.allocator_reject_present = true;
  }
  void notify_append_entered(Seek1402IOSlot *slot) {
    if (!notify.requested || slot == nullptr || space_headroom_threshold > 0 || space_config_error) return;
    const uintptr_t address = reinterpret_cast<uintptr_t>(slot);
    const uintptr_t begin = reinterpret_cast<uintptr_t>(slots);
    if (address < begin || address >= begin + sizeof(slots) ||
        (address - begin) % sizeof(Seek1402IOSlot) != 0 ||
        !slot->append.entered || slot->append.returned) {
      notify.lost.fetch_add(1, std::memory_order_relaxed);
      return;
    }
    notify.append_entered((address - begin) / sizeof(Seek1402IOSlot), slot->epoch);
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
      "{\"schema\":1,\"writer_pid\":%d,\"run_id\":\"%s\",\"identity_valid\":%s,\"scope\":\"flush_task_append_reuse_and_lifecycle\",\"clock\":\"realtime_us\",\"slots\":%llu,\"unfinished\":%llu,\"overflow\":%s,\"multiple_envs\":%s,\"progress_phases_present\":false,\"io_phases_present\":true,\"notify_requested\":%s,\"notify_attempts\":%llu,\"notify_sent\":%llu,\"notify_lost\":%llu}\n",
      writer_pid, identity_ready ? run_id : "", identity_ready ? "true" : "false",
      static_cast<unsigned long long>(count), static_cast<unsigned long long>(unfinished),
      state & overflow ? "true" : "false", multiple_envs.load() ? "true" : "false",
      notify.requested ? "true" : "false", static_cast<unsigned long long>(notify.attempts.load()),
      static_cast<unsigned long long>(notify.sent.load()), static_cast<unsigned long long>(notify.lost.load())) >= 0;
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
    // Space points require raw decoding; this text exporter does not serialize them.
    return ok && !space_enabled && identity_ready && unfinished == 0 && !(state & overflow) &&
        !multiple_envs.load() && notify.lost.load() == 0;
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
  SEEK1402_CAPTURE_OFFSET(space_gate);
  SEEK1402_CAPTURE_OFFSET(space_call_gate);
  SEEK1402_CAPTURE_OFFSET(space_calls);
  SEEK1402_CAPTURE_OFFSET(space_orphan);
  SEEK1402_CAPTURE_OFFSET(space_enabled);
  SEEK1402_CAPTURE_OFFSET(space_headroom_threshold);
  SEEK1402_CAPTURE_OFFSET(space_config_error);
  SEEK1402_CAPTURE_OFFSET(space_points);
  append("\"notify_requested\":%zu,\"notify_attempts\":%zu,\"notify_sent\":%zu,\"notify_lost\":%zu,",
         offsetof(Seek1402IOLifecycle, notify) + offsetof(Seek1402IONotify, requested),
         offsetof(Seek1402IOLifecycle, notify) + offsetof(Seek1402IONotify, attempts),
         offsetof(Seek1402IOLifecycle, notify) + offsetof(Seek1402IONotify, sent),
         offsetof(Seek1402IOLifecycle, notify) + offsetof(Seek1402IONotify, lost));
#undef SEEK1402_CAPTURE_OFFSET
  append("\"slots\":%zu},\"space_call_bytes\":%zu,\"space_call_offsets\":{"
         "\"first_point\":%zu,\"result\":%zu,\"allocator_upper\":%zu,\"allocator_id_upper\":%zu,"
         "\"allocator_result\":%zu,\"allocated_lsn\":%zu,\"allocated_log_id\":%zu,"
         "\"rejected_log_id\":%zu,\"rejected_max_lsn\":%zu,\"allocator_entered\":%zu,"
         "\"allocator_returned\":%zu,\"allocator_output_present\":%zu,\"allocator_reject_present\":%zu,"
         "\"abort_scope\":%zu,\"ready\":%zu},\"space_point_bytes\":%zu,\"space_offsets\":{"
         "\"call_id\":%zu,\"group_buffer\":%zu,\"value\":%zu,\"abort_scope\":%zu,\"ready\":%zu},\"slot_offsets\":{",
         offsetof(Seek1402IOLifecycle, slots), sizeof(Seek1402SpaceCall),
         offsetof(Seek1402SpaceCall, first_point), offsetof(Seek1402SpaceCall, result),
         offsetof(Seek1402SpaceCall, allocator_upper), offsetof(Seek1402SpaceCall, allocator_id_upper),
         offsetof(Seek1402SpaceCall, allocator_result), offsetof(Seek1402SpaceCall, allocated_lsn),
         offsetof(Seek1402SpaceCall, allocated_log_id), offsetof(Seek1402SpaceCall, rejected_log_id),
         offsetof(Seek1402SpaceCall, rejected_max_lsn), offsetof(Seek1402SpaceCall, allocator_entered),
         offsetof(Seek1402SpaceCall, allocator_returned), offsetof(Seek1402SpaceCall, allocator_output_present),
         offsetof(Seek1402SpaceCall, allocator_reject_present), offsetof(Seek1402SpaceCall, abort_scope), offsetof(Seek1402SpaceCall, ready), sizeof(Seek1402SpacePoint),
         offsetof(Seek1402SpacePoint, call_id),
         offsetof(Seek1402SpacePoint, group_buffer), offsetof(Seek1402SpacePoint, value),
         offsetof(Seek1402SpacePoint, abort_scope), offsetof(Seek1402SpacePoint, ready));
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
class Seek1402SpaceCallScope {
public:
  Seek1402SpaceCallScope(int &result, bool abort_scope)
      : previous_(seek1402_space_current_call()), result_(result), record_(nullptr) {
    auto &capture = seek1402_io_lifecycle();
    seek1402_space_current_call() = 0;
    if (!capture.space_enabled || !capture.enabled()) return;
    uint64_t state = capture.space_call_gate.load(std::memory_order_acquire);
    for (;;) {
      if (state & (capture.closed | capture.overflow)) return;
      if ((state & capture.count_mask) == capture.limit) {
        if (capture.space_call_gate.compare_exchange_weak(state, state | capture.overflow)) return;
      } else if (capture.space_call_gate.compare_exchange_weak(state, state + 1)) {
        record_ = &capture.space_calls[state];
        record_->abort_scope = abort_scope;
        seek1402_space_current_call() = state + 1;
        return;
      }
    }
  }
  template <typename Call>
  int observe_allocator(uint64_t upper, int64_t id_upper, const uint64_t &allocated_lsn,
                        const int64_t &allocated_log_id, Call call) {
    if (record_ != nullptr) {
      record_->allocator_upper = upper;
      record_->allocator_id_upper = id_upper;
      record_->allocator_entered = true;
    }
    const int result = call();
    if (record_ != nullptr) {
      record_->allocator_result = result;
      record_->allocator_returned = true;
      if (result == 0) {
        record_->allocated_lsn = allocated_lsn;
        record_->allocated_log_id = allocated_log_id;
        record_->allocator_output_present = true;
      }
    }
    return result;
  }
  ~Seek1402SpaceCallScope() {
    if (record_ != nullptr) {
      record_->result = result_;
      record_->ready.store(true, std::memory_order_release);
    }
    seek1402_space_current_call() = previous_;
  }
  Seek1402SpaceCallScope(const Seek1402SpaceCallScope &) = delete;
  Seek1402SpaceCallScope &operator=(const Seek1402SpaceCallScope &) = delete;
private:
  uint64_t previous_;
  int &result_;
  Seek1402SpaceCall *record_;
};
} }
