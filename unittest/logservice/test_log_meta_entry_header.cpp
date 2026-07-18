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

#include "logservice/palf/log_meta_entry_header.h"   // LogMetaEntryHeader
#include <gtest/gtest.h>

namespace oceanbase
{
namespace unittest
{
using namespace common;
using namespace palf;

TEST(TestLogMetaEntryHeader, test_log_meta_entry_header)
{
  const int64_t BUFSIZE = 1 << 21;
  LogMetaEntryHeader log_meta_entry_header1;
  char buf[BUFSIZE] = "hello world";
  int64_t buf_len = strlen(buf);
  char buf_ser[BUFSIZE];
  int64_t pos = 0;
  // Test invalid argument
  EXPECT_FALSE(log_meta_entry_header1.is_valid());
  EXPECT_EQ(OB_SUCCESS, log_meta_entry_header1.generate(buf, buf_len));
  EXPECT_TRUE(log_meta_entry_header1.is_valid());

  // Test integrity
  EXPECT_TRUE(log_meta_entry_header1.check_integrity(buf, buf_len));

  // Test serialize and deserialize
  EXPECT_EQ(OB_SUCCESS, log_meta_entry_header1.serialize(buf_ser, BUFSIZE, pos));
  EXPECT_EQ(pos, log_meta_entry_header1.get_serialize_size());
  pos = 0;
  LogMetaEntryHeader log_meta_entry_header2;
  EXPECT_EQ(OB_SUCCESS, log_meta_entry_header2.deserialize(buf_ser, BUFSIZE, pos));
  EXPECT_EQ(log_meta_entry_header1, log_meta_entry_header2);
}

} // end of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_log_meta_entry_header.log", true);
  OB_LOGGER.set_log_level("INFO");
  PALF_LOG(INFO, "begin unittest::test_log_meta_entry_header");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
