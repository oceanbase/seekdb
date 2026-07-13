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
#include "logservice/palf/log_meta_info.h"
#undef private
#include <gtest/gtest.h>
#include "share/ob_cluster_version.h"

namespace oceanbase
{
using namespace common;
using namespace palf;
namespace unittest
{

TEST(TestLogMetaInfos, test_log_mode_meta)
{
  static const int64_t BUFSIZE = 1 << 21;
  char buf[BUFSIZE];
  LogModeMeta log_mode_meta1;
  LSN lsn; lsn.val_ = 1;
  ObAddr addr(ObAddr::IPV4, "127.0.0.1", 4096);

  share::SCN invalid_scn;
  // Test invalid argument
  EXPECT_FALSE(log_mode_meta1.is_valid());
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_mode_meta1.generate(AccessMode::INVALID_ACCESS_MODE, share::SCN::min_scn()));
  EXPECT_EQ(OB_INVALID_ARGUMENT, log_mode_meta1.generate(AccessMode::APPEND, invalid_scn));
  EXPECT_EQ(OB_SUCCESS, log_mode_meta1.generate(AccessMode::APPEND, share::SCN::min_scn()));
  EXPECT_TRUE(log_mode_meta1.is_valid());

  // Test serialize and deserialize
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, log_mode_meta1.serialize(buf, BUFSIZE, pos));
  EXPECT_EQ(pos, log_mode_meta1.get_serialize_size());
  pos = 0;
  LogModeMeta log_mode_meta2;
  EXPECT_EQ(OB_SUCCESS, log_mode_meta2.deserialize(buf, BUFSIZE, pos));
  const bool equal = (log_mode_meta1.access_mode_ == log_mode_meta2.access_mode_ &&
                      log_mode_meta1.ref_scn_ == log_mode_meta2.ref_scn_);
  EXPECT_TRUE(equal);
}

TEST(TestLogMetaInfos, test_log_snapshot_meta)
{
  static const int64_t BUFSIZE = 1 << 21;
  char buf[BUFSIZE];
  LogSnapshotMeta log_snapshot_meta1;
  LSN lsn; lsn.val_ = 1;
  ObAddr addr(ObAddr::IPV4, "127.0.0.1", 4096);

  LogInfo prev_log_info; prev_log_info.generate_by_default();
  // Test invalid argument
  EXPECT_FALSE(log_snapshot_meta1.is_valid());
  LSN base_lsn(2*PALF_BLOCK_SIZE), prev_tail_lsn(PALF_BLOCK_SIZE);
  EXPECT_EQ(OB_SUCCESS, log_snapshot_meta1.generate(base_lsn, prev_log_info, prev_tail_lsn));
  EXPECT_EQ(true, log_snapshot_meta1.prev_log_info_.is_valid());
  EXPECT_EQ(true, log_snapshot_meta1.prev_log_tail_lsn_.is_valid());
  EXPECT_EQ(LogSnapshotMeta::LOG_SNAPSHOT_META_VERSION, log_snapshot_meta1.version_);
  LogInfo result_log_info;
  LSN input_curr_lsn = prev_tail_lsn;
  LSN output_prev_tail_lsn;
  EXPECT_EQ(OB_SUCCESS, log_snapshot_meta1.get_prev_log_info(input_curr_lsn, result_log_info, output_prev_tail_lsn));
  EXPECT_EQ(result_log_info, prev_log_info);
  EXPECT_EQ(output_prev_tail_lsn.is_valid(), true);
  // return OB_ENTRY_NOT_EXIST when base_lsn is not same as prev_tail_lsn of prev_log_info
  EXPECT_EQ(OB_ENTRY_NOT_EXIST, log_snapshot_meta1.get_prev_log_info(base_lsn, result_log_info, output_prev_tail_lsn));
  EXPECT_TRUE(log_snapshot_meta1.is_valid());
  // Test serialize and deserialize
  int64_t pos = 0;
  EXPECT_EQ(OB_SUCCESS, log_snapshot_meta1.serialize(buf, BUFSIZE, pos));
  EXPECT_EQ(pos, log_snapshot_meta1.get_serialize_size());
  pos = 0;
  LogSnapshotMeta log_snapshot_meta2;
  EXPECT_EQ(OB_SUCCESS, log_snapshot_meta2.deserialize(buf, BUFSIZE, pos));
  EXPECT_EQ(log_snapshot_meta1.base_lsn_,
            log_snapshot_meta2.base_lsn_);
}

} // end of unittest
} // end of oceanbase

int main(int args, char **argv)
{
  OB_LOGGER.set_file_name("test_log_meta_infos.log", true);
  OB_LOGGER.set_log_level("TRACE");
  PALF_LOG(INFO, "begin unittest::test_log_meta_infos");
  ::testing::InitGoogleTest(&args, argv);
  oceanbase::ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
  oceanbase::ObClusterVersion::get_instance().update_cluster_version(CLUSTER_CURRENT_VERSION);
  return RUN_ALL_TESTS();
}
