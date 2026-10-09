/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0.
 */

#include "gtest/gtest.h"
#include "sql/engine/basic/ob_workarea_memory_limit.h"
#include "sql/engine/ob_sql_memory_manager.h"

using namespace oceanbase::sql;

TEST(TestWorkareaMemoryQuota, zero_inherits_eighty_percent)
{
  EXPECT_EQ(80, calculate_workarea_memory_limit(0, 100));
  EXPECT_EQ(81, calculate_workarea_memory_limit(0, 102));
  EXPECT_EQ(0, calculate_workarea_memory_limit(0, 0));
}

TEST(TestWorkareaMemoryQuota, explicit_limit_is_preserved)
{
  EXPECT_EQ(123, calculate_workarea_memory_limit(123, 1000));
  EXPECT_EQ(UNLIMITED_WORKAREA_MEMORY,
            calculate_workarea_memory_limit(UNLIMITED_WORKAREA_MEMORY, 1000));
}

TEST(TestWorkareaMemoryQuota, successful_spill_write_records_one_reclaim)
{
  oceanbase::common::MemoryQuota quota(1024);
  ObSqlMemoryTracker tracker;
  tracker.set_memory_quota(&quota);

  tracker.dumped(256);
  EXPECT_EQ(1, quota.reclaim_count());
  EXPECT_EQ(256, tracker.get_total_dump_size());

  tracker.dumped(-256);
  EXPECT_EQ(1, quota.reclaim_count());
  EXPECT_EQ(0, tracker.get_total_dump_size());
}
