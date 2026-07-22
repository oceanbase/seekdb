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

#define private public
#include "rootserver/freeze/ob_freeze_info_detector.h"
#undef private

namespace oceanbase
{
namespace rootserver
{
namespace unittest
{

TEST(TestFreezeInfoDetector, snapshot_gc_history_waits_for_undo_retention)
{
  static const int64_t SECOND_NS = 1000L * 1000L * 1000L;
  static const int64_t HISTORY_SCN = 100L * SECOND_NS;

  EXPECT_FALSE(ObMajorMergeInfoDetector::is_snapshot_gc_history_due_(
      119L * SECOND_NS, HISTORY_SCN, 20));
  EXPECT_TRUE(ObMajorMergeInfoDetector::is_snapshot_gc_history_due_(
      120L * SECOND_NS, HISTORY_SCN, 20));
  EXPECT_TRUE(ObMajorMergeInfoDetector::is_snapshot_gc_history_due_(
      120L * SECOND_NS, HISTORY_SCN, 0));
}

TEST(TestFreezeInfoDetector, later_history_does_not_change_first_deadline)
{
  static const int64_t SECOND_NS = 1000L * 1000L * 1000L;
  static const int64_t FIRST_HISTORY_SCN = 100L * SECOND_NS;
  static const int64_t LATEST_HISTORY_SCN = 110L * SECOND_NS;
  static const int64_t CURRENT_TIME_NS = 120L * SECOND_NS;
  ObMajorMergeInfoDetector detector;

  EXPECT_EQ(FIRST_HISTORY_SCN,
      detector.latch_first_pending_snapshot_gc_history_scn_(FIRST_HISTORY_SCN));
  EXPECT_EQ(FIRST_HISTORY_SCN,
      detector.latch_first_pending_snapshot_gc_history_scn_(LATEST_HISTORY_SCN));
  EXPECT_TRUE(ObMajorMergeInfoDetector::is_snapshot_gc_history_due_(
      CURRENT_TIME_NS, FIRST_HISTORY_SCN, 20));
  EXPECT_FALSE(ObMajorMergeInfoDetector::is_snapshot_gc_history_due_(
      CURRENT_TIME_NS, LATEST_HISTORY_SCN, 20));
}

TEST(TestFreezeInfoDetector, standby_start_and_restart_only_reload)
{
  ObMajorMergeInfoDetector detector;
  detector.is_inited_ = true;
  detector.is_primary_service_ = false;

  detector.resume(); // RAW_WRITE service activation on initial startup.
  EXPECT_FALSE(detector.is_primary_active_);
  EXPECT_FALSE(detector.need_primary_catchup_);
  EXPECT_FALSE(detector.need_renew_snapshot_gc_scn_(1));
  EXPECT_TRUE(ObMajorMergeInfoDetector::need_reload_freeze_info_(false));
  EXPECT_EQ(10L * 1000L * 1000L, detector.get_schedule_interval());

  detector.pause();
  detector.resume(); // RAW_WRITE service activation after restart/LS online.
  EXPECT_FALSE(detector.is_primary_active_);
  EXPECT_FALSE(detector.need_renew_snapshot_gc_scn_(1));
  EXPECT_EQ(OB_NOT_SUPPORTED, detector.on_become_primary());

  // Avoid invoking timer destruction on this white-box object.
  detector.is_inited_ = false;
}

TEST(TestFreezeInfoDetector, append_activation_immediately_requests_catchup)
{
  ObMajorMergeInfoDetector detector;
  detector.is_inited_ = true;
  detector.is_primary_service_ = true;
  detector.resume();

  // Both switchover and failover reach the same APPEND service activation.
  ASSERT_EQ(OB_SUCCESS, detector.on_become_primary());
  EXPECT_TRUE(detector.is_primary_active_);
  EXPECT_TRUE(detector.need_primary_catchup_);
  EXPECT_TRUE(detector.need_renew_snapshot_gc_scn_(1));
  EXPECT_FALSE(ObMajorMergeInfoDetector::need_reload_freeze_info_(true));

  detector.is_inited_ = false;
}

TEST(TestFreezeInfoDetector, demotion_stops_renew_and_reactivation_catches_up)
{
  static const int64_t PENDING_HISTORY_SCN = 123456789;
  ObMajorMergeInfoDetector detector;
  detector.is_inited_ = true;
  detector.is_primary_service_ = true;
  detector.resume();
  ASSERT_EQ(OB_SUCCESS, detector.on_become_primary());
  detector.first_pending_snapshot_gc_history_scn_ = PENDING_HISTORY_SCN;

  detector.pause(); // APPEND service deactivation before RAW_WRITE takes over.
  EXPECT_TRUE(detector.is_paused());
  EXPECT_FALSE(detector.is_primary_active_);
  EXPECT_FALSE(detector.need_renew_snapshot_gc_scn_(1));
  EXPECT_EQ(PENDING_HISTORY_SCN,
      detector.first_pending_snapshot_gc_history_scn_);

  detector.resume();
  ASSERT_EQ(OB_SUCCESS, detector.on_become_primary());
  EXPECT_TRUE(detector.is_primary_active_);
  EXPECT_TRUE(detector.need_primary_catchup_);
  EXPECT_TRUE(detector.need_renew_snapshot_gc_scn_(1));

  detector.is_inited_ = false;
}

TEST(TestFreezeInfoDetector, renew_failure_retries_on_fixed_interval)
{
  static const int64_t START_TS = 100L * 1000L * 1000L;
  ObMajorMergeInfoDetector detector;
  detector.is_primary_service_ = true;
  detector.is_primary_active_ = true;
  detector.need_primary_catchup_ = true;
  detector.last_gc_renew_attempt_ts_ = START_TS;

  EXPECT_FALSE(detector.need_renew_snapshot_gc_scn_(
      START_TS + detector.UPDATER_INTERVAL_US - 1));
  EXPECT_TRUE(detector.need_renew_snapshot_gc_scn_(
      START_TS + detector.UPDATER_INTERVAL_US));
}

TEST(TestFreezeInfoDetector, role_restore_before_ls_activation_is_safe)
{
  ObMajorMergeInfoDetector primary_detector;
  ObMajorMergeInfoDetector restore_detector;
  primary_detector.is_inited_ = true;
  primary_detector.is_primary_service_ = true;
  restore_detector.is_inited_ = true;
  restore_detector.is_primary_service_ = false;

  // multi_tenant/LS may start before the persisted tenant role is loaded. RAW_WRITE
  // activates only the restore service, so local-LS online cannot arm renewal.
  restore_detector.resume();
  EXPECT_FALSE(restore_detector.is_primary_active_);
  EXPECT_FALSE(restore_detector.need_renew_snapshot_gc_scn_(1));

  // A later explicit mode transition to APPEND activates the primary service.
  restore_detector.pause();
  primary_detector.resume();
  ASSERT_EQ(OB_SUCCESS, primary_detector.on_become_primary());
  EXPECT_TRUE(primary_detector.need_renew_snapshot_gc_scn_(1));

  primary_detector.is_inited_ = false;
  restore_detector.is_inited_ = false;
}

} // namespace unittest
} // namespace rootserver
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
