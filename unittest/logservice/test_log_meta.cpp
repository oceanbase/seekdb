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
#include "share/ob_cluster_version.h"

namespace oceanbase
{
namespace unittest
{
using namespace palf;

PalfBaseInfo build_base_info()
{
  PalfBaseInfo base_info;
  base_info.prev_log_info_.log_id_ = 1;
  base_info.prev_log_info_.lsn_ = LSN(10000);
  base_info.prev_log_info_.scn_.convert_for_logservice(10);
  base_info.prev_log_info_.accum_checksum_ = 10;
  base_info.curr_lsn_ = LSN(20000);
  return base_info;
}

TEST(TestLogMeta, generate_single_log_meta)
{
  const PalfBaseInfo base_info = build_base_info();
  LogMeta meta;
  ASSERT_EQ(OB_SUCCESS, meta.generate_by_palf_base_info(base_info, AccessMode::APPEND));
  EXPECT_TRUE(meta.is_valid());
  EXPECT_EQ(base_info.curr_lsn_, meta.log_snapshot_meta_.base_lsn_);
}

TEST(TestLogMeta, serialize_single_log_meta)
{
  LogMeta meta;
  ASSERT_EQ(OB_SUCCESS, meta.generate_by_palf_base_info(build_base_info(), AccessMode::APPEND));
  char buf[4096];
  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(buf, sizeof(buf), pos));
  EXPECT_EQ(meta.get_serialize_size(), pos);

  LogMeta restored;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(buf, sizeof(buf), pos));
  EXPECT_TRUE(restored.is_valid());
  EXPECT_EQ(meta.log_mode_meta_.access_mode_, restored.log_mode_meta_.access_mode_);
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_meta.log", true);
  ::testing::InitGoogleTest(&argc, argv);
  oceanbase::ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
  return RUN_ALL_TESTS();
}
