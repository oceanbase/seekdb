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

#include "share/config/ob_server_config.h"

#define private public
#define protected public
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#undef protected
#undef private

namespace oceanbase
{
namespace concurrency_control
{
namespace unittest
{

class TestMultiVersionGarbageCollector : public ::testing::Test
{
public:
  void SetUp() override
  {
    original_mvcc_gc_enabled_ = GCONF._mvcc_gc_using_min_txn_snapshot;
    GCONF._mvcc_gc_using_min_txn_snapshot = true;
    ASSERT_EQ(OB_SUCCESS, collector_.init());
  }

  void TearDown() override
  {
    GCONF._mvcc_gc_using_min_txn_snapshot = original_mvcc_gc_enabled_;
  }

  share::SCN make_tx_scn(const int64_t value)
  {
    share::SCN scn;
    EXPECT_EQ(OB_SUCCESS, scn.convert_for_tx(value));
    return scn;
  }

protected:
  ObMultiVersionGarbageCollector collector_;
  bool original_mvcc_gc_enabled_;
};

TEST_F(TestMultiVersionGarbageCollector, first_sample_failure_keeps_min_scn)
{
  collector_.update_study_status_(OB_EAGAIN, 100);

  EXPECT_TRUE(collector_.has_error_when_study_);
  EXPECT_EQ(0, collector_.last_study_timestamp_);
  EXPECT_FALSE(collector_.is_gc_disabled());
  EXPECT_EQ(share::SCN::min_scn(), collector_.local_reserved_snapshot_.atomic_load());
  EXPECT_EQ(share::SCN::min_scn(), collector_.get_reserved_snapshot_for_active_txn());
}

TEST_F(TestMultiVersionGarbageCollector, failure_after_success_keeps_last_watermark)
{
  const share::SCN first_watermark = make_tx_scn(100);
  collector_.local_reserved_snapshot_.atomic_set(first_watermark);
  collector_.update_study_status_(OB_SUCCESS, 1000);

  collector_.update_study_status_(OB_EAGAIN, 2000);

  EXPECT_TRUE(collector_.has_error_when_study_);
  EXPECT_EQ(1000, collector_.last_study_timestamp_);
  EXPECT_FALSE(collector_.is_gc_disabled());
  EXPECT_EQ(first_watermark, collector_.local_reserved_snapshot_.atomic_load());
  EXPECT_EQ(first_watermark, collector_.get_reserved_snapshot_for_active_txn());
}

TEST_F(TestMultiVersionGarbageCollector, disk_pressure_falls_back_without_dropping_cache)
{
  const share::SCN cached_watermark = make_tx_scn(100);
  collector_.local_reserved_snapshot_.atomic_set(cached_watermark);
  collector_.update_study_status_(OB_SUCCESS, 1000);

  collector_.update_disk_pressure_status_(true);

  EXPECT_TRUE(collector_.is_gc_disabled());
  EXPECT_EQ(share::SCN::max_scn(), collector_.get_reserved_snapshot_for_active_txn());
  EXPECT_EQ(cached_watermark, collector_.local_reserved_snapshot_.atomic_load());
}

TEST_F(TestMultiVersionGarbageCollector, successful_recovery_publishes_new_watermark)
{
  const share::SCN first_watermark = make_tx_scn(100);
  const share::SCN recovered_watermark = make_tx_scn(200);
  collector_.local_reserved_snapshot_.atomic_set(first_watermark);
  collector_.update_study_status_(OB_SUCCESS, 1000);
  collector_.update_study_status_(OB_EAGAIN, 2000);
  collector_.update_disk_pressure_status_(true);

  // study() publishes a complete sample before repeat_study() records success.
  collector_.local_reserved_snapshot_.atomic_set(recovered_watermark);
  collector_.update_study_status_(OB_SUCCESS, 3000);
  collector_.update_disk_pressure_status_(false);

  EXPECT_FALSE(collector_.has_error_when_study_);
  EXPECT_FALSE(collector_.is_gc_disabled());
  EXPECT_EQ(3000, collector_.last_study_timestamp_);
  EXPECT_EQ(recovered_watermark, collector_.get_reserved_snapshot_for_active_txn());
}

} // namespace unittest
} // namespace concurrency_control
} // namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
