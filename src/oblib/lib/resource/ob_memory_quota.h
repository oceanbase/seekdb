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

#ifndef OCEANBASE_LIB_RESOURCE_OB_MEMORY_QUOTA_H_
#define OCEANBASE_LIB_RESOURCE_OB_MEMORY_QUOTA_H_

#include <atomic>
#include <cstdint>
#include <limits>

#include "lib/ob_define.h"
#include "lib/thread_local/ob_tsi_utils.h"

namespace oceanbase
{
namespace common
{

// A row returned by the component memory virtual table.  The fields are
// sampled independently and therefore do not form a transactional snapshot.
struct MemoryQuotaSample
{
  MemoryQuotaSample()
      : limit_bytes_(0), committed_bytes_(0), reserved_bytes_(0),
        reject_count_(0), reclaim_count_(0)
  {}
  MemoryQuotaSample(const int64_t limit_bytes,
                    const int64_t committed_bytes,
                    const int64_t reserved_bytes,
                    const int64_t reject_count,
                    const int64_t reclaim_count)
      : limit_bytes_(limit_bytes), committed_bytes_(committed_bytes),
        reserved_bytes_(reserved_bytes), reject_count_(reject_count),
        reclaim_count_(reclaim_count)
  {}

  int64_t limit_bytes_;
  int64_t committed_bytes_;
  int64_t reserved_bytes_;
  int64_t reject_count_;
  int64_t reclaim_count_;
};

// Admission-only primitive shared by components.  Product policy (wash,
// spill, cleanup or GC) remains with the owning component manager.
class MemoryQuota
{
public:
  explicit MemoryQuota(const int64_t limit = std::numeric_limits<int64_t>::max())
      : limit_(normalize_limit(limit)), available_(normalize_limit(limit)),
        committed_overflow_(0), reject_count_(0), reclaim_count_(0)
  {
    reset_committed_slots();
  }

  MemoryQuota(const MemoryQuota &) = delete;
  MemoryQuota &operator=(const MemoryQuota &) = delete;

  void set_limit(const int64_t limit)
  {
    // Limit updates are rare and serialized so their deltas cannot be applied
    // out of order.  reserve() linearizes on available_: one that overlaps
    // this update is ordered before or after the delta, and a shrink can make
    // available_ negative until the owner reclaims existing committed bytes.
    while (limit_update_lock_.test_and_set(std::memory_order_acquire)) {}
    const int64_t normalized_limit = normalize_limit(limit);
    const int64_t old_limit = limit_.load(std::memory_order_relaxed);
    limit_.store(normalized_limit, std::memory_order_release);
    available_.fetch_add(normalized_limit - old_limit, std::memory_order_acq_rel);
    limit_update_lock_.clear(std::memory_order_release);
  }

  // Only for an owning component's quiescent init/destroy boundary.
  void reset(const int64_t limit)
  {
    const int64_t normalized_limit = normalize_limit(limit);
    limit_.store(normalized_limit, std::memory_order_relaxed);
    available_.store(normalized_limit, std::memory_order_relaxed);
    reset_committed_slots();
    reject_count_.store(0, std::memory_order_relaxed);
    reclaim_count_.store(0, std::memory_order_relaxed);
    limit_update_lock_.clear(std::memory_order_relaxed);
  }

  int64_t limit() const { return limit_.load(std::memory_order_acquire); }
  int64_t committed() const
  {
    uint64_t total = committed_overflow_.load(std::memory_order_acquire);
    const int64_t used_slot_count = get_max_used_itid();
    const int64_t slot_count = used_slot_count < COMMITTED_SLOT_COUNT
        ? used_slot_count : COMMITTED_SLOT_COUNT;
    for (int64_t i = 0; i < slot_count; ++i) {
      total += committed_slots_[i].value_.load(std::memory_order_acquire);
    }
    // Concurrent sampling may combine slot values from different instants.
    // The strict admission state is available_, so clamp only the approximate
    // observation.  At a quiescent boundary the modulo sum is exact and is at
    // most INT64_MAX by construction.
    return total <= static_cast<uint64_t>(std::numeric_limits<int64_t>::max())
        ? static_cast<int64_t>(total)
        : std::numeric_limits<int64_t>::max();
  }
  int64_t reserved() const
  {
    return reserved_from_committed(committed());
  }
  int64_t reject_count() const { return reject_count_.load(std::memory_order_acquire); }
  int64_t reclaim_count() const { return reclaim_count_.load(std::memory_order_acquire); }

  bool reserve(const int64_t bytes)
  {
    bool succeeded = 0 == bytes;
    if (bytes > 0) {
      succeeded = grow_accounted(bytes);
    }
    if (!succeeded) {
      reject_count_.fetch_add(1, std::memory_order_relaxed);
    }
    return succeeded;
  }

  // Converts a successful reservation to committed backing bytes.  A larger
  // actual allocation is admitted atomically before it can be published.
  bool reconcile(const int64_t reservation, const int64_t actual)
  {
    bool succeeded = reservation >= 0 && actual >= 0;
    const int64_t extra = succeeded && actual > reservation
        ? actual - reservation : 0;
    if (succeeded && extra > 0) {
      succeeded = grow_accounted(extra);
      if (!succeeded) {
        reject_count_.fetch_add(1, std::memory_order_relaxed);
      }
    }
    if (succeeded) {
      add_committed(actual);
      if (actual < reservation) {
        available_.fetch_add(reservation - actual, std::memory_order_release);
      }
    }
    return succeeded;
  }

  void rollback(const int64_t reservation)
  {
    if (reservation > 0) {
      available_.fetch_add(reservation, std::memory_order_release);
    }
  }

  void release(const int64_t committed)
  {
    if (committed > 0) {
      add_committed(-committed);
      available_.fetch_add(committed, std::memory_order_release);
    }
  }

  // One count represents one successfully completed component reclaim batch.
  // Failed attempts are logged by the owner and are not mixed into this value.
  void record_reclaim(const int64_t reclaimed_bytes)
  {
    if (reclaimed_bytes > 0) {
      reclaim_count_.fetch_add(1, std::memory_order_relaxed);
    }
  }

  // Count a component admission rejection that occurs before reserve(), for
  // example when a cache's logical block limit cannot accept the request.
  void record_reject()
  {
    reject_count_.fetch_add(1, std::memory_order_relaxed);
  }

  MemoryQuotaSample sample() const
  {
    MemoryQuotaSample result;
    result.limit_bytes_ = limit();
    result.committed_bytes_ = committed();
    result.reserved_bytes_ = reserved_from_committed(result.committed_bytes_);
    result.reject_count_ = reject_count();
    result.reclaim_count_ = reclaim_count();
    return result;
  }

private:
  static constexpr int64_t COMMITTED_SLOT_COUNT = OB_MAX_THREAD_NUM;

  struct alignas(64) CommittedSlot
  {
    std::atomic<uint64_t> value_;
  };

  static int64_t normalize_limit(const int64_t limit)
  {
    return limit >= 0 ? limit : std::numeric_limits<int64_t>::max();
  }

  int64_t reserved_from_committed(const int64_t committed) const
  {
    // available_ is limit minus the admitted charge and committed is the
    // durable part of that charge.  These values are sampled independently
    // while writers are active, like the component SQL view; at an owning
    // component's quiescent boundary the result is exact.
    const int64_t current_limit = limit_.load(std::memory_order_acquire);
    const int64_t available = available_.load(std::memory_order_acquire);
    int64_t accounted = 0;
    if (available >= 0) {
      accounted = current_limit > available ? current_limit - available : 0;
    } else {
      const uint64_t unavailable = static_cast<uint64_t>(-(available + 1)) + 1;
      const uint64_t unsigned_limit = static_cast<uint64_t>(current_limit);
      accounted = unavailable > static_cast<uint64_t>(
          std::numeric_limits<int64_t>::max()) - unsigned_limit
          ? std::numeric_limits<int64_t>::max()
          : static_cast<int64_t>(unsigned_limit + unavailable);
    }
    return accounted > committed ? accounted - committed : 0;
  }

  void reset_committed_slots()
  {
    for (int64_t i = 0; i < COMMITTED_SLOT_COUNT; ++i) {
      committed_slots_[i].value_.store(0, std::memory_order_relaxed);
    }
    committed_overflow_.store(0, std::memory_order_relaxed);
  }

  void add_committed(const int64_t bytes)
  {
    const int64_t thread_id = get_itid();
    const uint64_t delta = static_cast<uint64_t>(bytes);
    if (thread_id >= 0 && thread_id < COMMITTED_SLOT_COUNT) {
      // get_itid() assigns this slot to one live writer.  Atomic load/store
      // keeps concurrent SQL sampling data-race-free without a locked RMW.
      std::atomic<uint64_t> &slot = committed_slots_[thread_id].value_;
      slot.store(slot.load(std::memory_order_relaxed) + delta,
                 std::memory_order_release);
    } else {
      // Threads beyond the normal SeekDB worker range share a safe fallback.
      committed_overflow_.fetch_add(delta, std::memory_order_release);
    }
  }

  bool grow_accounted(const int64_t bytes)
  {
    bool succeeded = 0 == bytes;
    if (bytes > 0) {
      int64_t available = available_.load(std::memory_order_relaxed);
      const int64_t current_limit = limit_.load(std::memory_order_relaxed);
      // Far from the limit, fetch_sub has the same successful-path shape as
      // the removed ObMemoryMgr accounting and avoids a more expensive CAS.
      // Only enter it from an observed empty quota.  Together with a margin
      // covering the complete get_itid() namespace, this bounds every stale
      // deduction even if a concurrent limit shrink consumes all available
      // bytes.  Non-empty, large or near-limit requests stay on the fully
      // general CAS path.
      static constexpr int64_t MAX_CONCURRENT_RESERVES = 1024L * 64L;
      if (bytes <= std::numeric_limits<int64_t>::max()
                       / MAX_CONCURRENT_RESERVES
          && available == current_limit
          && available >= bytes * MAX_CONCURRENT_RESERVES) {
        const int64_t old_available = available_.fetch_sub(
            bytes, std::memory_order_relaxed);
        if (old_available >= bytes) {
          succeeded = true;
        } else {
          // This is only reachable if a concurrent shrink consumed the
          // margin.  Restore before reporting the failed reservation.
          available_.fetch_add(bytes, std::memory_order_relaxed);
        }
      } else {
        while (available >= bytes) {
          if (available_.compare_exchange_weak(
                  available, available - bytes,
                  std::memory_order_relaxed,
                  std::memory_order_relaxed)) {
            succeeded = true;
            break;
          }
        }
      }
    }
    return succeeded;
  }

  std::atomic<int64_t> limit_;
  // available_ is the single admission word: limit - committed - in-flight.
  // Keep admission and durable accounting on separate cache lines.  The four
  // process-lifetime component quotas make the small padding cost negligible,
  // while reserve/reconcile/release otherwise bounce one line for every RMW.
  alignas(64) std::atomic<int64_t> available_;
  CommittedSlot committed_slots_[COMMITTED_SLOT_COUNT];
  alignas(64) std::atomic<uint64_t> committed_overflow_;
  std::atomic<int64_t> reject_count_;
  std::atomic<int64_t> reclaim_count_;
  std::atomic_flag limit_update_lock_ = ATOMIC_FLAG_INIT;
};

class MemoryReservation
{
public:
  MemoryReservation() : quota_(nullptr), bytes_(0), active_(false) {}
  MemoryReservation(MemoryQuota *quota, const int64_t bytes)
      : quota_(quota), bytes_(bytes),
        active_(nullptr == quota || quota->reserve(bytes))
  {}
  ~MemoryReservation()
  {
    if (active_ && nullptr != quota_) {
      quota_->rollback(bytes_);
    }
  }

  MemoryReservation(const MemoryReservation &) = delete;
  MemoryReservation &operator=(const MemoryReservation &) = delete;

  bool valid() const { return active_; }
  bool reconcile(const int64_t actual)
  {
    bool succeeded = active_;
    if (succeeded && nullptr != quota_) {
      succeeded = quota_->reconcile(bytes_, actual);
    }
    if (succeeded) {
      active_ = false;
    }
    return succeeded;
  }
  void rollback()
  {
    if (active_ && nullptr != quota_) {
      quota_->rollback(bytes_);
    }
    active_ = false;
  }

private:
  MemoryQuota *quota_;
  int64_t bytes_;
  bool active_;
};

} // namespace common
} // namespace oceanbase

#endif // OCEANBASE_LIB_RESOURCE_OB_MEMORY_QUOTA_H_
