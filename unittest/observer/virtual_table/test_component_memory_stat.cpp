/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#include <atomic>
#include <thread>
#include <vector>

#include "gtest/gtest.h"
#include "lib/resource/ob_memory_quota.h"
#include "observer/virtual_table/ob_all_virtual_component_memory_stat.h"

using oceanbase::common::MemoryQuota;
using oceanbase::common::MemoryQuotaSample;
using oceanbase::common::MemoryReservation;
using oceanbase::observer::ComponentMemoryStat;

TEST(TestComponentMemoryStat, stable_component_keys)
{
  ASSERT_EQ(4, ComponentMemoryStat::component_count());
  EXPECT_STREQ("KV_CACHE", ComponentMemoryStat::component_name(0));
  EXPECT_STREQ("SQL_WORKAREA", ComponentMemoryStat::component_name(1));
  EXPECT_STREQ("VECTOR", ComponentMemoryStat::component_name(2));
  EXPECT_STREQ("META_OBJECT", ComponentMemoryStat::component_name(3));
  EXPECT_EQ(nullptr, ComponentMemoryStat::component_name(-1));
  EXPECT_EQ(nullptr, ComponentMemoryStat::component_name(4));
}

TEST(TestComponentMemoryStat, approximate_sample_is_safe_during_writes)
{
  static const int WRITER_COUNT = 8;
  static const int ITERATIONS = 20000;
  MemoryQuota quota(WRITER_COUNT);
  std::atomic<bool> start(false);
  std::atomic<bool> failed(false);
  std::vector<std::thread> writers;
  writers.reserve(WRITER_COUNT);

  for (int i = 0; i < WRITER_COUNT; ++i) {
    writers.emplace_back([&]() {
      while (!start.load(std::memory_order_acquire)) {
        std::this_thread::yield();
      }
      for (int j = 0; j < ITERATIONS; ++j) {
        MemoryReservation reservation(&quota, 1);
        if (reservation.valid() && reservation.reconcile(1)) {
          quota.release(1);
        }
      }
    });
  }

  start.store(true, std::memory_order_release);
  int64_t previous_rejects = 0;
  int64_t previous_reclaims = 0;
  for (int i = 0; i < ITERATIONS; ++i) {
    const MemoryQuotaSample sample = quota.sample();
    if (sample.limit_bytes_ < 0 || sample.committed_bytes_ < 0
        || sample.reserved_bytes_ < 0
        || sample.reject_count_ < previous_rejects
        || sample.reclaim_count_ < previous_reclaims) {
      failed.store(true, std::memory_order_release);
    }
    previous_rejects = sample.reject_count_;
    previous_reclaims = sample.reclaim_count_;
  }

  for (std::thread &writer : writers) {
    writer.join();
  }
  EXPECT_FALSE(failed.load(std::memory_order_acquire));
  const MemoryQuotaSample quiescent = quota.sample();
  EXPECT_EQ(0, quiescent.reserved_bytes_);
  EXPECT_EQ(0, quiescent.committed_bytes_);
}
