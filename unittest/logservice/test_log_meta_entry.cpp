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

#include "logservice/palf/log_meta.h"
#include "logservice/palf/log_meta_entry.h"

namespace oceanbase
{
namespace unittest
{
using namespace palf;

TEST(TestLogMetaEntry, serialize_and_check_integrity)
{
  constexpr int64_t BUF_SIZE = 4096;
  char meta_buf[BUF_SIZE];
  char entry_buf[BUF_SIZE];

  PalfBaseInfo base_info;
  base_info.generate_by_default();
  LogMeta log_meta;
  EXPECT_EQ(OB_SUCCESS, log_meta.generate_by_palf_base_info(base_info, AccessMode::APPEND));
  EXPECT_TRUE(log_meta.is_valid());

  int64_t meta_pos = 0;
  EXPECT_EQ(OB_SUCCESS, log_meta.serialize(meta_buf, BUF_SIZE, meta_pos));
  EXPECT_EQ(log_meta.get_serialize_size(), meta_pos);

  LogMetaEntryHeader header;
  EXPECT_EQ(OB_SUCCESS, header.generate(meta_buf, meta_pos));
  EXPECT_TRUE(header.check_integrity(meta_buf, meta_pos));

  LogMetaEntry entry;
  EXPECT_FALSE(entry.is_valid());
  EXPECT_EQ(OB_INVALID_ARGUMENT, entry.generate(LogMetaEntryHeader(), meta_buf));
  EXPECT_EQ(OB_INVALID_ARGUMENT, entry.generate(header, nullptr));
  EXPECT_EQ(OB_SUCCESS, entry.generate(header, meta_buf));
  EXPECT_TRUE(entry.is_valid());
  EXPECT_TRUE(entry.check_integrity());

  int64_t entry_pos = 0;
  EXPECT_EQ(OB_SUCCESS, entry.serialize(entry_buf, BUF_SIZE, entry_pos));
  EXPECT_EQ(entry.get_serialize_size(), entry_pos);

  LogMetaEntry restored;
  int64_t restored_pos = 0;
  EXPECT_EQ(OB_SUCCESS, restored.deserialize(entry_buf, entry_pos, restored_pos));
  EXPECT_EQ(entry_pos, restored_pos);
  EXPECT_TRUE(restored.is_valid());
  EXPECT_TRUE(restored.check_integrity());
  EXPECT_EQ(meta_pos, restored.get_data_len());
  EXPECT_EQ(0, MEMCMP(meta_buf, restored.get_buf(), meta_pos));

  LogMetaEntry copied;
  EXPECT_EQ(OB_SUCCESS, copied.shallow_copy(restored));
  EXPECT_EQ(restored.get_buf(), copied.get_buf());
  EXPECT_TRUE(copied.check_integrity());
}

} // namespace unittest
} // namespace oceanbase
