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
#include <cstdint>
#include <cstdio>
#include <cstdlib>

namespace oceanbase { namespace palf {
// Diagnostic storage outlives tasks. No slot is recycled within a process.
struct Seek1402IOSlot {
  int64_t epoch = 0;
  uint64_t lsn = 0;
  int64_t length = 0;
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
      ((produced && ((producer_ret == 0 && consumed) ||
                     (producer_ret != 0 && !consumed))) || (dropped && !consumed)) &&
      (!consumed || callback_done.load(std::memory_order_acquire));
  }
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
  std::atomic<bool> multiple_envs{false};
  const char *path;
  Seek1402IOLifecycle() : path(std::getenv("SEEK1402_IO_LIFECYCLE_FILE")) {}
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
    if (path == nullptr || *path == '\0') return nullptr;
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
    bool ok = std::fprintf(file,
      "{\"scope\":\"flush_task_lifecycle_only\",\"slots\":%llu,\"unfinished\":%llu,\"overflow\":%s,\"multiple_envs\":%s,\"progress_phases_present\":false}\n",
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
      char producer_result[24] = "null";
      char callback_result[24] = "null";
      if (producer) std::snprintf(producer_result, sizeof(producer_result), "%d", slot.producer_ret);
      if (callback) std::snprintf(callback_result, sizeof(callback_result), "%d", slot.callback_ret);
      ok = std::fprintf(file,
        "{\"slot\":%llu,\"epoch\":%lld,\"lsn\":%llu,\"length\":%lld,\"producer_done\":%s,\"producer_ret\":%s,\"consumer_seen\":%s,\"callback_done\":%s,\"callback_ret\":%s,\"callback_skipped\":%s,\"freed\":%s,\"dropped_before_push\":%s}\n",
        static_cast<unsigned long long>(i), static_cast<long long>(slot.epoch),
        static_cast<unsigned long long>(slot.lsn), static_cast<long long>(slot.length),
        producer ? "true" : "false", producer_result,
        slot.consumer_seen.load(std::memory_order_acquire) ? "true" : "false",
        callback ? "true" : "false", callback_result,
        callback && slot.callback_skipped ? "true" : "false",
        slot.freed.load(std::memory_order_acquire) ? "true" : "false",
        slot.dropped_before_push.load(std::memory_order_acquire) ? "true" : "false") >= 0;
    }
    ok = !std::ferror(file) && ok;
    ok = std::fclose(file) == 0 && ok;
    return ok && unfinished == 0 && !(state & overflow) && !multiple_envs.load();
  }
};
static_assert(sizeof(Seek1402IOLifecycle) <= 4 * 1024 * 1024,
              "diagnostic lifecycle storage exceeds 4MiB");
inline Seek1402IOLifecycle &seek1402_io_lifecycle() {
  static Seek1402IOLifecycle capture;
  return capture;
}
} }
