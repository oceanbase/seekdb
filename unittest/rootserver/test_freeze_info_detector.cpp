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
#include "rootserver/freeze/ob_snapshot_gc_scn_renewer.h"
#undef private

namespace oceanbase
{
namespace rootserver
{
namespace unittest
{

TEST(TestSnapshotGcScnRenewer, snapshot_gc_history_waits_for_undo_retention)
{
  static const int64_t SECOND_NS = 1000L * 1000L * 1000L;
  static const int64_t HISTORY_SCN = 100L * SECOND_NS;

  EXPECT_FALSE(ObSnapshotGcScnRenewer::is_snapshot_gc_history_due_(
      119L * SECOND_NS, HISTORY_SCN, 20));
  EXPECT_TRUE(ObSnapshotGcScnRenewer::is_snapshot_gc_history_due_(
      120L * SECOND_NS, HISTORY_SCN, 20));
  EXPECT_TRUE(ObSnapshotGcScnRenewer::is_snapshot_gc_history_due_(
      120L * SECOND_NS, HISTORY_SCN, 0));
}

TEST(TestSnapshotGcScnRenewer, later_history_does_not_change_first_deadline)
{
  static const int64_t SECOND_NS = 1000L * 1000L * 1000L;
  static const int64_t FIRST_HISTORY_SCN = 100L * SECOND_NS;
  static const int64_t LATEST_HISTORY_SCN = 110L * SECOND_NS;
  static const int64_t CURRENT_TIME_NS = 120L * SECOND_NS;
  ObSnapshotGcScnRenewer renewer;

  EXPECT_EQ(FIRST_HISTORY_SCN,
      renewer.latch_first_pending_snapshot_gc_history_scn_(FIRST_HISTORY_SCN));
  EXPECT_EQ(FIRST_HISTORY_SCN,
      renewer.latch_first_pending_snapshot_gc_history_scn_(LATEST_HISTORY_SCN));
  EXPECT_TRUE(ObSnapshotGcScnRenewer::is_snapshot_gc_history_due_(
      CURRENT_TIME_NS, FIRST_HISTORY_SCN, 20));
  EXPECT_FALSE(ObSnapshotGcScnRenewer::is_snapshot_gc_history_due_(
      CURRENT_TIME_NS, LATEST_HISTORY_SCN, 20));
}

TEST(TestSnapshotGcScnRenewer, standby_start_and_restart_only_reload)
{
  ObSnapshotGcScnRenewer renewer;
  renewer.is_inited_ = true;
  renewer.is_primary_service_ = false;

  renewer.resume(); // RAW_WRITE service activation on initial startup.
  EXPECT_FALSE(renewer.is_primary_active_);
  EXPECT_FALSE(renewer.need_primary_catchup_);
  EXPECT_FALSE(renewer.need_renew(1));
  EXPECT_TRUE(ObMajorMergeInfoDetector::need_reload_freeze_info_(false));
  EXPECT_EQ(10L * 1000L * 1000L, renewer.get_renew_interval());

  renewer.pause();
  renewer.resume(); // RAW_WRITE service activation after restart/LS online.
  EXPECT_FALSE(renewer.is_primary_active_);
  EXPECT_FALSE(renewer.need_renew(1));
  EXPECT_EQ(OB_NOT_SUPPORTED, renewer.on_become_primary());
}

TEST(TestSnapshotGcScnRenewer, append_activation_immediately_requests_catchup)
{
  ObSnapshotGcScnRenewer renewer;
  renewer.is_inited_ = true;
  renewer.is_primary_service_ = true;
  renewer.resume();

  // Both switchover and failover reach the same APPEND service activation.
  ASSERT_EQ(OB_SUCCESS, renewer.on_become_primary());
  EXPECT_TRUE(renewer.is_primary_active_);
  EXPECT_TRUE(renewer.need_primary_catchup_);
  EXPECT_TRUE(renewer.need_renew(1));
  EXPECT_FALSE(ObMajorMergeInfoDetector::need_reload_freeze_info_(true));
}

TEST(TestSnapshotGcScnRenewer, demotion_stops_renew_and_reactivation_catches_up)
{
  static const int64_t PENDING_HISTORY_SCN = 123456789;
  ObSnapshotGcScnRenewer renewer;
  renewer.is_inited_ = true;
  renewer.is_primary_service_ = true;
  renewer.resume();
  ASSERT_EQ(OB_SUCCESS, renewer.on_become_primary());
  renewer.first_pending_snapshot_gc_history_scn_ = PENDING_HISTORY_SCN;

  renewer.pause(); // APPEND service deactivation before RAW_WRITE takes over.
  EXPECT_TRUE(renewer.is_paused());
  EXPECT_FALSE(renewer.is_primary_active_);
  EXPECT_FALSE(renewer.need_renew(1));
  EXPECT_EQ(PENDING_HISTORY_SCN,
      renewer.first_pending_snapshot_gc_history_scn_);

  renewer.resume();
  ASSERT_EQ(OB_SUCCESS, renewer.on_become_primary());
  EXPECT_TRUE(renewer.is_primary_active_);
  EXPECT_TRUE(renewer.need_primary_catchup_);
  EXPECT_TRUE(renewer.need_renew(1));
}

TEST(TestSnapshotGcScnRenewer, renew_failure_retries_on_fixed_interval)
{
  static const int64_t START_TS = 100L * 1000L * 1000L;
  ObSnapshotGcScnRenewer renewer;
  renewer.is_primary_service_ = true;
  renewer.is_primary_active_ = true;
  renewer.need_primary_catchup_ = true;
  renewer.last_gc_renew_attempt_ts_ = START_TS;

  EXPECT_FALSE(renewer.need_renew(
      START_TS + renewer.RENEW_INTERVAL_US - 1));
  EXPECT_TRUE(renewer.need_renew(
      START_TS + renewer.RENEW_INTERVAL_US));
}

TEST(TestSnapshotGcScnRenewer, role_restore_before_ls_activation_is_safe)
{
  ObSnapshotGcScnRenewer primary_renewer;
  ObSnapshotGcScnRenewer restore_renewer;
  primary_renewer.is_inited_ = true;
  primary_renewer.is_primary_service_ = true;
  restore_renewer.is_inited_ = true;
  restore_renewer.is_primary_service_ = false;

  // multi_tenant/LS may start before the persisted tenant role is loaded. RAW_WRITE
  // activates only the restore service, so local-LS online cannot arm renewal.
  restore_renewer.resume();
  EXPECT_FALSE(restore_renewer.is_primary_active_);
  EXPECT_FALSE(restore_renewer.need_renew(1));

  // A later explicit mode transition to APPEND activates the primary service.
  restore_renewer.pause();
  primary_renewer.resume();
  ASSERT_EQ(OB_SUCCESS, primary_renewer.on_become_primary());
  EXPECT_TRUE(primary_renewer.need_renew(1));
}

} // namespace unittest
} // namespace rootserver
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
