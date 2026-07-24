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
#include "storage/compaction/ob_snapshot_gc_scn_renewal_state.h"
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
  static const int64_t SECOND_US = 1000L * 1000L;
  static const int64_t HISTORY_SCN = 100L * SECOND_NS;

  EXPECT_EQ(120L * SECOND_US,
      ObSnapshotGcScnRenewer::calc_next_renew_ts_(
          HISTORY_SCN, 20));
  EXPECT_EQ(100L * SECOND_US,
      ObSnapshotGcScnRenewer::calc_next_renew_ts_(
          HISTORY_SCN, 0));
  EXPECT_EQ(100L * SECOND_US,
      ObSnapshotGcScnRenewer::calc_next_renew_ts_(
          HISTORY_SCN + 999, 0));
}

TEST(TestSnapshotGcScnRenewer, later_history_does_not_change_next_renew_time)
{
  static const int64_t SECOND_NS = 1000L * 1000L * 1000L;
  static const int64_t SECOND_US = 1000L * 1000L;
  static const int64_t FIRST_HISTORY_SCN = 100L * SECOND_NS;
  static const int64_t LATEST_HISTORY_SCN = 110L * SECOND_NS;
  ObSnapshotGcScnRenewer renewer;

  EXPECT_EQ(120L * SECOND_US,
      renewer.latch_next_renew_ts_(FIRST_HISTORY_SCN, 20));
  EXPECT_EQ(120L * SECOND_US,
      renewer.latch_next_renew_ts_(LATEST_HISTORY_SCN, 20));
  EXPECT_EQ(120L * SECOND_US,
      renewer.latch_next_renew_ts_(LATEST_HISTORY_SCN, 60));
}

TEST(TestSnapshotGcScnRenewer, standby_start_and_restart_only_reload)
{
  ObSnapshotGcScnRenewer renewer;
  renewer.is_inited_ = true;
  renewer.is_primary_service_ = false;

  renewer.resume(); // RAW_WRITE service activation on initial startup.
  EXPECT_FALSE(renewer.is_primary_active_);
  EXPECT_FALSE(renewer.need_primary_catchup_);
  EXPECT_FALSE(renewer.need_renew_(1));
  EXPECT_EQ(OB_SUCCESS, renewer.try_renew());
  EXPECT_TRUE(ObMajorMergeInfoDetector::need_reload_freeze_info_(false));
  EXPECT_EQ(10L * 1000L * 1000L, renewer.get_renew_interval());

  renewer.pause();
  renewer.resume(); // RAW_WRITE service activation after restart/LS online.
  EXPECT_FALSE(renewer.is_primary_active_);
  EXPECT_FALSE(renewer.need_renew_(1));
  EXPECT_EQ(OB_SUCCESS, renewer.try_renew());
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
  EXPECT_TRUE(renewer.need_renew_(1));
  EXPECT_FALSE(ObMajorMergeInfoDetector::need_reload_freeze_info_(true));
}

TEST(TestSnapshotGcScnRenewer, demotion_stops_renew_and_reactivation_catches_up)
{
  static const int64_t NEXT_RENEW_TS = 123456789;
  static const int64_t REFRESHED_SCN = 987654321;
  ObSnapshotGcScnRenewer renewer;
  renewer.is_inited_ = true;
  renewer.is_primary_service_ = true;
  renewer.resume();
  ASSERT_EQ(OB_SUCCESS, renewer.on_become_primary());
  renewer.next_renew_ts_ = NEXT_RENEW_TS;
  renewer.refreshed_scn_ = REFRESHED_SCN;

  renewer.pause(); // APPEND service deactivation before RAW_WRITE takes over.
  EXPECT_TRUE(renewer.is_paused());
  EXPECT_FALSE(renewer.is_primary_active_);
  EXPECT_FALSE(renewer.need_renew_(1));
  EXPECT_EQ(NEXT_RENEW_TS, renewer.next_renew_ts_);
  EXPECT_EQ(REFRESHED_SCN, renewer.refreshed_scn_);

  renewer.resume();
  ASSERT_EQ(OB_SUCCESS, renewer.on_become_primary());
  EXPECT_TRUE(renewer.is_primary_active_);
  EXPECT_TRUE(renewer.need_primary_catchup_);
  EXPECT_TRUE(renewer.need_renew_(1));
}

TEST(TestSnapshotGcScnRenewer, refreshed_scn_is_consumer_progress)
{
  storage::ObSnapshotGcScnRenewalState renewal_state;
  ObSnapshotGcScnRenewer renewer;

  renewal_state.update_target_scn(100);
  renewer.refreshed_scn_ = 100;
  EXPECT_EQ(renewal_state.get_target_scn(), renewer.refreshed_scn_);

  renewal_state.update_target_scn(200);
  EXPECT_GT(renewal_state.get_target_scn(), renewer.refreshed_scn_);
}

TEST(TestSnapshotGcScnRenewer, renew_failure_retries_on_fixed_interval)
{
  static const int64_t START_TS = 100L * 1000L * 1000L;
  ObSnapshotGcScnRenewer renewer;
  renewer.is_primary_service_ = true;
  renewer.is_primary_active_ = true;
  renewer.need_primary_catchup_ = true;
  renewer.next_renew_ts_ =
      START_TS + renewer.RENEW_INTERVAL_US;

  EXPECT_FALSE(renewer.need_renew_(
      START_TS + renewer.RENEW_INTERVAL_US - 1));
  EXPECT_TRUE(renewer.need_renew_(
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
  EXPECT_FALSE(restore_renewer.need_renew_(1));

  // A later explicit mode transition to APPEND activates the primary service.
  restore_renewer.pause();
  primary_renewer.resume();
  ASSERT_EQ(OB_SUCCESS, primary_renewer.on_become_primary());
  EXPECT_TRUE(primary_renewer.need_renew_(1));
}

} // namespace unittest
} // namespace rootserver
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
