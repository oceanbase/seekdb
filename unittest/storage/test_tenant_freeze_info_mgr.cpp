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

#include <gtest/gtest.h>
#include <thread>

#include "storage/compaction/ob_tenant_freeze_info_mgr.h"

namespace oceanbase
{
namespace storage
{
namespace unittest
{

TEST(TestTenantFreezeInfoMgr, snapshot_gc_history_scn_coalesces_by_max)
{
  ObTenantFreezeInfoMgr freeze_info_mgr;
  static const int64_t THREAD_COUNT = 8;
  std::thread threads[THREAD_COUNT];

  EXPECT_EQ(0, freeze_info_mgr.get_pending_snapshot_gc_history_scn());
  freeze_info_mgr.notify_snapshot_gc_history_created(100);
  freeze_info_mgr.notify_snapshot_gc_history_created(50);
  EXPECT_EQ(100, freeze_info_mgr.get_pending_snapshot_gc_history_scn());

  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    threads[i] = std::thread([&freeze_info_mgr, i]() {
      freeze_info_mgr.notify_snapshot_gc_history_created(200 + i);
    });
  }
  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    threads[i].join();
  }
  EXPECT_EQ(200 + THREAD_COUNT - 1,
      freeze_info_mgr.get_pending_snapshot_gc_history_scn());
}

TEST(TestTenantFreezeInfoMgr, snapshot_gc_history_scn_clear_uses_compare_and_swap)
{
  ObTenantFreezeInfoMgr freeze_info_mgr;
  freeze_info_mgr.notify_snapshot_gc_history_created(100);

  EXPECT_FALSE(freeze_info_mgr.try_clear_pending_snapshot_gc_history_scn(90));
  freeze_info_mgr.notify_snapshot_gc_history_created(200);
  EXPECT_FALSE(freeze_info_mgr.try_clear_pending_snapshot_gc_history_scn(100));
  EXPECT_EQ(200, freeze_info_mgr.get_pending_snapshot_gc_history_scn());
  EXPECT_TRUE(freeze_info_mgr.try_clear_pending_snapshot_gc_history_scn(200));
  EXPECT_EQ(0, freeze_info_mgr.get_pending_snapshot_gc_history_scn());
}

} // namespace unittest
} // namespace storage
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
