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

namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace palf;

namespace
{
constexpr int64_t BUF_SIZE = 4096;
}

TEST(TestLogMetaInfos, log_mode_meta)
{
  char buf[BUF_SIZE];
  LogModeMeta meta;
  share::SCN invalid_scn;
  EXPECT_FALSE(meta.is_valid());
  EXPECT_EQ(OB_INVALID_ARGUMENT,
            meta.generate(AccessMode::INVALID_ACCESS_MODE, share::SCN::min_scn()));
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.generate(AccessMode::APPEND, invalid_scn));
  ASSERT_EQ(OB_SUCCESS, meta.generate(AccessMode::APPEND, share::SCN::min_scn()));
  ASSERT_TRUE(meta.is_valid());

  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(buf, BUF_SIZE, pos));
  EXPECT_EQ(meta.get_serialize_size(), pos);
  const int64_t serialized_size = pos;
  LogModeMeta restored;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(buf, serialized_size, pos));
  ASSERT_TRUE(restored.is_valid());
  EXPECT_EQ(serialized_size, pos);
  EXPECT_EQ(meta.access_mode_, restored.access_mode_);
  EXPECT_EQ(meta.ref_scn_, restored.ref_scn_);

  restored.reset();
  EXPECT_FALSE(restored.is_valid());
}

TEST(TestLogMetaInfos, log_snapshot_meta)
{
  char buf[BUF_SIZE];
  LogSnapshotMeta meta;
  LogInfo prev_log_info;
  prev_log_info.generate_by_default();
  const LSN prev_tail_lsn(PALF_BLOCK_SIZE);
  const LSN base_lsn(2 * PALF_BLOCK_SIZE);

  EXPECT_FALSE(meta.is_valid());
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.generate(LSN(), prev_log_info, prev_tail_lsn));
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.generate(base_lsn, LogInfo(), prev_tail_lsn));
  EXPECT_EQ(OB_INVALID_ARGUMENT, meta.generate(base_lsn, prev_log_info, LSN()));
  ASSERT_EQ(OB_SUCCESS, meta.generate(base_lsn, prev_log_info, prev_tail_lsn));
  ASSERT_TRUE(meta.is_valid());

  LogInfo restored_log_info;
  LSN restored_tail_lsn;
  EXPECT_EQ(OB_SUCCESS,
            meta.get_prev_log_info(prev_tail_lsn, restored_log_info, restored_tail_lsn));
  EXPECT_EQ(prev_log_info, restored_log_info);
  EXPECT_EQ(prev_tail_lsn, restored_tail_lsn);
  EXPECT_EQ(OB_ENTRY_NOT_EXIST,
            meta.get_prev_log_info(base_lsn, restored_log_info, restored_tail_lsn));

  int64_t pos = 0;
  ASSERT_EQ(OB_SUCCESS, meta.serialize(buf, BUF_SIZE, pos));
  EXPECT_EQ(meta.get_serialize_size(), pos);
  const int64_t serialized_size = pos;
  LogSnapshotMeta restored;
  pos = 0;
  ASSERT_EQ(OB_SUCCESS, restored.deserialize(buf, serialized_size, pos));
  ASSERT_TRUE(restored.is_valid());
  EXPECT_EQ(serialized_size, pos);
  EXPECT_EQ(meta.version_, restored.version_);
  EXPECT_EQ(meta.base_lsn_, restored.base_lsn_);
  EXPECT_EQ(meta.prev_log_info_, restored.prev_log_info_);
  EXPECT_EQ(meta.prev_log_tail_lsn_, restored.prev_log_tail_lsn_);

  restored.reset();
  EXPECT_FALSE(restored.is_valid());
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_meta_infos.log", true);
  OB_LOGGER.set_log_level("TRACE");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
