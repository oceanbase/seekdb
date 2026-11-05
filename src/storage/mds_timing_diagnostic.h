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

#if defined(OB_MDS_TIMING_DIAGNOSTIC) && OB_MDS_TIMING_DIAGNOSTIC
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace oceanbase { namespace storage { namespace mds_timing {

enum Phase {
  CREATE_INCLUSIVE, PREPARE_THROTTLE, BUCKET_LOCK, CREATE_MSD,
  INITIALIZE, POINTER_PARAM, CAS_PUBLISH, ID_SET, REPORT,
  STATUS_INCLUSIVE, BEFORE_PREPARE_INCLUSIVE, TRY_NODE_LOCK, STATE_TRANSITION,
  REPORT_EVENT_INCLUSIVE, FILL_EVENT, INFO_EMIT, EVENT_CACHE, PHASE_COUNT
};

struct Metric {
  uint64_t count = 0;
  uint64_t sum_ns = 0;
  uint64_t max_ns = 0;
};

struct Snapshot {
  Metric metrics[PHASE_COUNT];
  uint64_t start_ns = 0;
  uint64_t end_ns = 0;
  uint64_t try_failures = 0;
  uint64_t nested_locks = 0;
  const void *owner = nullptr;
  bool observed() const {
    for (const auto &m : metrics) { if (m.count != 0) { return true; } }
    return false;
  }
  void format(char *buf, const size_t capacity) const {
    static const char *const names[] = {
      "create_inclusive", "prepare_throttle", "bucket_lock", "create_msd",
      "initialize", "pointer_param", "cas_publish", "id_set", "report",
      "status_inclusive", "before_prepare_inclusive", "try_node_lock", "state_transition",
      "report_event_inclusive", "fill_event", "info_emit", "event_cache"
    };
    size_t pos = 0;
    int n = std::snprintf(buf, capacity,
        "inclusive_parents_do_not_sum;unit=ns;owner=%p;start=%llu;end=%llu;try_failures=%llu;nested_locks=%llu",
        owner, (unsigned long long)start_ns, (unsigned long long)end_ns,
        (unsigned long long)try_failures, (unsigned long long)nested_locks);
    if (n < 0 || static_cast<size_t>(n) >= capacity) { return; }
    pos = static_cast<size_t>(n);
    for (int i = 0; i < PHASE_COUNT; ++i) {
      n = std::snprintf(buf + pos, capacity - pos, ";%s=%llu/%llu/%llu", names[i],
          (unsigned long long)metrics[i].count, (unsigned long long)metrics[i].sum_ns,
          (unsigned long long)metrics[i].max_ns);
      if (n < 0 || static_cast<size_t>(n) >= capacity - pos) { return; }
      pos += static_cast<size_t>(n);
    }
  }
};

struct Collector {
  Snapshot value;
  uint64_t depth = 0;
};

// One TLS instance across translation units; no heap allocation or new latches.
inline thread_local Collector collector;
inline uint64_t now_ns() {
  return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now().time_since_epoch()).count());
}
inline void begin(const void *owner) {
  if (collector.depth++ == 0) {
    collector.value = Snapshot{};
    collector.value.owner = owner;
    collector.value.start_ns = now_ns();
  } else {
    ++collector.value.nested_locks;
  }
}
inline Snapshot end() {
  Snapshot result;
  if (collector.depth != 0 && --collector.depth == 0) {
    collector.value.end_ns = now_ns();
    result = collector.value;
    collector.value = Snapshot{};
  }
  return result;
}
class Timer {
public:
  explicit Timer(const Phase phase) : phase_(phase), active_(collector.depth != 0),
      start_(active_ ? now_ns() : 0) {}
  ~Timer() { stop(); }
  void stop() {
    if (active_) {
      const uint64_t elapsed = now_ns() - start_;
      auto &m = collector.value.metrics[phase_];
      ++m.count;
      m.sum_ns += elapsed;
      if (elapsed > m.max_ns) { m.max_ns = elapsed; }
      active_ = false;
    }
  }
private:
  Phase phase_;
  bool active_;
  uint64_t start_;
};
inline void node_lock_failed() {
  if (collector.depth != 0) { ++collector.value.try_failures; }
}
}}}

#define MDS_DIAG_JOIN_(a, b) a##b
#define MDS_DIAG_JOIN(a, b) MDS_DIAG_JOIN_(a, b)
#define MDS_DIAG_SCOPE(phase) ::oceanbase::storage::mds_timing::Timer MDS_DIAG_JOIN(mds_diag_, __LINE__)(::oceanbase::storage::mds_timing::phase)
#define MDS_DIAG_CALL(phase, ...) ([&]() -> decltype(auto) { MDS_DIAG_SCOPE(phase); return (__VA_ARGS__); }())
#define MDS_DIAG_TIMER(name, phase) ::oceanbase::storage::mds_timing::Timer name(::oceanbase::storage::mds_timing::phase)
#define MDS_DIAG_LOCK_STOP(name) name.stop()
#define MDS_DIAG_LOCK_FAILED() ::oceanbase::storage::mds_timing::node_lock_failed()
#else
#define MDS_DIAG_SCOPE(phase)
#define MDS_DIAG_CALL(phase, ...) (__VA_ARGS__)
#define MDS_DIAG_TIMER(name, phase)
#define MDS_DIAG_LOCK_STOP(name)
#define MDS_DIAG_LOCK_FAILED()
#endif
