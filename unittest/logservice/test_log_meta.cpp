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

#define private public
#include "logservice/palf/log_meta.h"
#undef private

#include <gtest/gtest.h>

namespace oceanbase
{
namespace unittest
{
using namespace palf;

PalfBaseInfo build_base_info()
{
  PalfBaseInfo base_info;
  base_info.generate_by_default();
  base_info.prev_log_info_.log_id_ = 1;
  base_info.prev_log_info_.lsn_ = LSN(10000);
  base_info.prev_log_info_.scn_.convert_for_logservice(10);
  base_info.prev_log_info_.accum_checksum_ = 10;
  base_info.curr_lsn_ = LSN(20000);
  return base_info;
}

TEST(TestLogMeta, generate_update_and_serialize)
{
  const PalfBaseInfo base_info = build_base_info();

  LogMeta meta;
  EXPECT_FALSE(meta.is_valid());
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            meta.generate_by_palf_base_info(base_info, AccessMode::INVALID_ACCESS_MODE));
  ASSERT_EQ(OB_SUCCESS, meta.generate_by_palf_base_info(base_info, AccessMode::APPEND));
  ASSERT_TRUE(meta.is_valid());

  const LogModeMeta generated_mode_meta = meta.get_log_mode_meta();
  EXPECT_EQ(AccessMode::APPEND, generated_mode_meta.access_mode_);
  EXPECT_EQ(base_info.prev_log_info_.scn_, generated_mode_meta.ref_scn_);

  const LogSnapshotMeta generated_snapshot_meta = meta.get_log_snapshot_meta();
  EXPECT_EQ(base_info.curr_lsn_, generated_snapshot_meta.base_lsn_);
  EXPECT_EQ(base_info.prev_log_info_, generated_snapshot_meta.prev_log_info_);
  EXPECT_EQ(base_info.curr_lsn_, generated_snapshot_meta.prev_log_tail_lsn_);

  LogInfo updated_prev_log_info = base_info.prev_log_info_;
  updated_prev_log_info.log_id_ = 2;
  updated_prev_log_info.lsn_ = base_info.curr_lsn_;
  updated_prev_log_info.scn_.convert_for_logservice(20);
  updated_prev_log_info.accum_checksum_ = 20;
  LogSnapshotMeta updated_snapshot_meta;
  ASSERT_EQ(OB_SUCCESS,
            updated_snapshot_meta.generate(LSN(30000), updated_prev_log_info, LSN(30000)));
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.update_log_snapshot_meta(LogSnapshotMeta()));
  ASSERT_EQ(OB_SUCCESS, meta.update_log_snapshot_meta(updated_snapshot_meta));

  constexpr int64_t BUF_SIZE = 4096;
  char buf[BUF_SIZE];
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(buf, BUF_SIZE, pos));
  EXPECT_EQ(meta.get_serialize_size(), pos);

  LogMeta restored;
  ASSERT_EQ(OB_SUCCESS, restored.load(buf, pos));
  ASSERT_TRUE(restored.is_valid());
  const LogModeMeta restored_mode_meta = restored.get_log_mode_meta();
  EXPECT_EQ(generated_mode_meta.access_mode_, restored_mode_meta.access_mode_);
  EXPECT_EQ(generated_mode_meta.ref_scn_, restored_mode_meta.ref_scn_);
  const LogSnapshotMeta restored_snapshot_meta = restored.get_log_snapshot_meta();
  EXPECT_EQ(updated_snapshot_meta.base_lsn_, restored_snapshot_meta.base_lsn_);
  EXPECT_EQ(updated_snapshot_meta.prev_log_info_, restored_snapshot_meta.prev_log_info_);
  EXPECT_EQ(updated_snapshot_meta.prev_log_tail_lsn_, restored_snapshot_meta.prev_log_tail_lsn_);

  LogMeta copied(restored);
  EXPECT_TRUE(copied.is_valid());
  EXPECT_EQ(restored.get_log_snapshot_meta().base_lsn_, copied.get_log_snapshot_meta().base_lsn_);
  copied.reset();
  EXPECT_FALSE(copied.is_valid());
}

TEST(TestLogMeta, reject_invalid_base_info)
{
  PalfBaseInfo base_info = build_base_info();
  base_info.curr_lsn_ = LSN(1);

  LogMeta meta;
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            meta.generate_by_palf_base_info(base_info, AccessMode::APPEND));
  EXPECT_FALSE(meta.is_valid());
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_meta.log", true);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
