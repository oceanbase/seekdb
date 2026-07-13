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
#include "logservice/palf/log_meta_entry.h"
#include "logservice/palf/log_meta.h"
#include "share/ob_cluster_version.h"

namespace oceanbase
{
namespace unittest
{
using namespace palf;

TEST(TestLogMetaEntry, serialize_single_log_meta_entry)
{
  PalfBaseInfo base_info;
  base_info.prev_log_info_.log_id_ = 1;
  base_info.prev_log_info_.lsn_ = LSN(10000);
  base_info.prev_log_info_.scn_.convert_for_logservice(10);
  base_info.prev_log_info_.accum_checksum_ = 10;
  base_info.curr_lsn_ = LSN(20000);

  LogMeta meta;
  ASSERT_EQ(OB_SUCCESS, meta.generate_by_palf_base_info(base_info, AccessMode::APPEND));
  char meta_buf[4096];
  int64_t meta_pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(meta_buf, sizeof(meta_buf), meta_pos));

  LogMetaEntryHeader header;
  ASSERT_EQ(OB_SUCCESS, header.generate(meta_buf, meta_pos));
  ASSERT_TRUE(header.check_integrity(meta_buf, meta_pos));

  LogMetaEntry entry;
  ASSERT_EQ(OB_SUCCESS, entry.generate(header, meta_buf));
  char entry_buf[8192];
  int64_t entry_pos = 0;
  ASSERT_EQ(OB_SUCCESS, entry.serialize(entry_buf, sizeof(entry_buf), entry_pos));

  LogMetaEntry restored;
  entry_pos = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(entry_buf, sizeof(entry_buf), entry_pos));
  EXPECT_TRUE(restored.check_integrity());
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_meta_entry.log", true);
  ::testing::InitGoogleTest(&argc, argv);
  oceanbase::ObClusterVersion::get_instance().update_data_version(DATA_CURRENT_VERSION);
  return RUN_ALL_TESTS();
}
