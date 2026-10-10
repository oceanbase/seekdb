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

#include <atomic>
#include <limits>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "lib/resource/ob_memory_quota.h"

using oceanbase::common::MemoryQuota;
using oceanbase::common::MemoryReservation;

TEST(TestMemoryQuota, reserve_reconcile_rollback)
{
  MemoryQuota quota(100);
  ASSERT_TRUE(quota.reserve(60));
  ASSERT_EQ(60, quota.reserved());
  ASSERT_TRUE(quota.reconcile(60, 40));
  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(40, quota.committed());

  ASSERT_TRUE(quota.reserve(50));
  ASSERT_FALSE(quota.reconcile(50, 70));
  ASSERT_EQ(50, quota.reserved());
  ASSERT_EQ(40, quota.committed());
  quota.rollback(50);
  ASSERT_EQ(0, quota.reserved());

  ASSERT_FALSE(quota.reserve(61));
  ASSERT_EQ(2, quota.reject_count());
  quota.release(40);
  ASSERT_EQ(0, quota.committed());
}

TEST(TestMemoryQuota, reservation_guard_rolls_back)
{
  MemoryQuota quota(64);
  {
    MemoryReservation reservation(&quota, 64);
    ASSERT_TRUE(reservation.valid());
    ASSERT_EQ(64, quota.reserved());
  }
  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(0, quota.committed());
}

TEST(TestMemoryQuota, actual_charge_can_grow_within_limit)
{
  MemoryQuota quota(100);
  ASSERT_TRUE(quota.reserve(40));
  ASSERT_TRUE(quota.reconcile(40, 60));
  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(60, quota.committed());
  quota.release(60);
  ASSERT_EQ(0, quota.committed());
}

TEST(TestMemoryQuota, admission_is_overflow_safe)
{
  const int64_t max = std::numeric_limits<int64_t>::max();
  MemoryQuota quota(max);
  ASSERT_TRUE(quota.reserve(max - 1));
  ASSERT_TRUE(quota.reconcile(max - 1, max - 1));
  ASSERT_FALSE(quota.reserve(2));
  ASSERT_EQ(1, quota.reject_count());
  quota.release(max - 1);
  ASSERT_EQ(0, quota.committed());
  ASSERT_EQ(0, quota.reserved());
}

TEST(TestMemoryQuota, concurrent_failed_admission_cannot_wrap_accounting)
{
  static const int THREAD_COUNT = 32;
  static const int ITERATIONS = 20000;
  const int64_t max = std::numeric_limits<int64_t>::max();
  MemoryQuota quota(max);
  ASSERT_TRUE(quota.reserve(max));
  ASSERT_TRUE(quota.reconcile(max, max));

  std::atomic<bool> start(false);
  std::atomic<int64_t> unexpected_successes(0);
  std::vector<std::thread> workers;
  workers.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    workers.emplace_back([&, i]() {
      const int64_t request = 1 == i % 4 ? max : 1;
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int j = 0; j < ITERATIONS; ++j) {
        MemoryReservation reservation(&quota, request);
        if (reservation.valid()) {
          unexpected_successes.fetch_add(1, std::memory_order_relaxed);
        }
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (std::thread &worker : workers) {
    worker.join();
  }

  ASSERT_EQ(0, unexpected_successes.load(std::memory_order_acquire));
  ASSERT_EQ(max, quota.committed());
  ASSERT_EQ(0, quota.reserved());
  quota.release(max);
  ASSERT_EQ(0, quota.committed());
  ASSERT_EQ(0, quota.reserved());
}

TEST(TestMemoryQuota, dynamic_shrink_blocks_growth_until_reclaimed)
{
  MemoryQuota quota(100);
  ASSERT_TRUE(quota.reserve(80));
  ASSERT_TRUE(quota.reconcile(80, 80));
  quota.set_limit(40);
  ASSERT_FALSE(quota.reserve(1));
  quota.release(50);
  ASSERT_TRUE(quota.reserve(10));
  ASSERT_TRUE(quota.reconcile(10, 10));
  quota.release(40);
  ASSERT_EQ(0, quota.committed());
  ASSERT_EQ(0, quota.reserved());
}

TEST(TestMemoryQuota, cross_thread_release_preserves_sharded_committed_total)
{
  MemoryQuota quota(100);
  std::atomic<bool> committed(false);
  std::atomic<bool> released(false);
  std::thread allocator([&]() {
    ASSERT_TRUE(quota.reserve(60));
    ASSERT_TRUE(quota.reconcile(60, 60));
    committed.store(true, std::memory_order_release);
    while (!released.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
  });
  std::thread releaser([&]() {
    while (!committed.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }
    quota.release(60);
    released.store(true, std::memory_order_release);
  });
  allocator.join();
  releaser.join();

  ASSERT_EQ(0, quota.committed());
  ASSERT_EQ(0, quota.reserved());
}

TEST(TestMemoryQuota, concurrent_admission_never_exceeds_limit)
{
  static const int64_t LIMIT = 8;
  static const int THREAD_COUNT = 32;
  static const int ITERATIONS = 2000;
  MemoryQuota quota(LIMIT);
  std::atomic<bool> start(false);
  std::atomic<bool> failed(false);
  std::atomic<int64_t> active_reservations(0);
  std::vector<std::thread> workers;
  workers.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    workers.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int j = 0; j < ITERATIONS; ++j) {
        MemoryReservation reservation(&quota, 1);
        if (reservation.valid()) {
          const int64_t active = active_reservations.fetch_add(
              1, std::memory_order_acq_rel) + 1;
          const bool reconciled = reservation.reconcile(1);
          if (active > LIMIT || !reconciled) {
            failed.store(true, std::memory_order_release);
          }
          // Drop the test-side ownership count before making the quota
          // available again.  Reversing these operations leaves a window
          // where another thread may reserve legitimately while this
          // already-released reservation is still counted as active.
          active_reservations.fetch_sub(1, std::memory_order_acq_rel);
          if (reconciled) {
            quota.release(1);
          }
        }
      }
    });
  }
  start.store(true, std::memory_order_release);
  for (std::thread &worker : workers) {
    worker.join();
  }
  ASSERT_FALSE(failed.load(std::memory_order_acquire));
  ASSERT_EQ(0, active_reservations.load(std::memory_order_acquire));
  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(0, quota.committed());
}

TEST(TestMemoryQuota, concurrent_limit_changes_leave_no_inflight_charge)
{
  static const int THREAD_COUNT = 8;
  const int64_t max = std::numeric_limits<int64_t>::max();
  MemoryQuota quota(max);
  std::atomic<bool> start(false);
  std::atomic<bool> stop(false);
  std::vector<std::thread> workers;
  workers.reserve(THREAD_COUNT);
  for (int i = 0; i < THREAD_COUNT; ++i) {
    workers.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      while (!stop.load(std::memory_order_acquire)) {
        MemoryReservation reservation(&quota, 1);
        if (reservation.valid() && reservation.reconcile(1)) {
          quota.release(1);
        }
      }
    });
  }

  start.store(true, std::memory_order_release);
  for (int i = 0; i < 20000; ++i) {
    // Exercise the far-from-limit fetch_sub path together with the largest
    // possible shrink.  Stale fast-path deductions must remain bounded and
    // must not wrap available_ back into a successful admission.
    quota.set_limit(0 == i % 2 ? 0 : max);
  }
  quota.set_limit(0);
  stop.store(true, std::memory_order_release);
  for (std::thread &worker : workers) {
    worker.join();
  }

  ASSERT_EQ(0, quota.reserved());
  ASSERT_EQ(0, quota.committed());
  ASSERT_FALSE(quota.reserve(1));
}

TEST(TestMemoryQuota, reclaim_counts_successful_batches_only)
{
  MemoryQuota quota(100);
  quota.record_reclaim(0);
  quota.record_reclaim(-1);
  ASSERT_EQ(0, quota.reclaim_count());
  quota.record_reclaim(1);
  quota.record_reclaim(1024);
  ASSERT_EQ(2, quota.reclaim_count());
}
