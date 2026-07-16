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

#include <cstring>
#include <gtest/gtest.h>

#include "rpc/obmysql/packet/ompk_handshake.h"

using namespace oceanbase::common;
using namespace oceanbase::obmysql;

namespace
{

uint16_t encode_and_get_server_status(const OMPKHandshake &handshake)
{
  char buffer[256] = {};
  int64_t pos = 0;
  int64_t packet_count = 0;
  EXPECT_EQ(OB_SUCCESS, handshake.encode(buffer, sizeof(buffer), pos, packet_count));
  EXPECT_EQ(1, packet_count);

  const int64_t version_offset = OB_MYSQL_HEADER_LENGTH + 1; // packet header + protocol version
  EXPECT_GT(pos, version_offset);
  if (pos <= version_offset) {
    return 0;
  }
  const char *version_end = static_cast<const char *>(
      std::memchr(buffer + version_offset, '\0', pos - version_offset));
  EXPECT_NE(nullptr, version_end);
  if (nullptr == version_end) {
    return 0;
  }

  const int64_t status_offset = version_end - buffer + 1
      + 4  // thread id
      + 8  // auth plugin data part 1
      + 1  // filler
      + 2  // lower capability flags
      + 1; // character set
  EXPECT_LE(status_offset + 2, pos);
  if (status_offset + 2 > pos) {
    return 0;
  }

  return static_cast<uint16_t>(static_cast<uint8_t>(buffer[status_offset]))
      | static_cast<uint16_t>(static_cast<uint8_t>(buffer[status_offset + 1]) << 8);
}

TEST(TestMySQLHandshake, default_status_advertises_autocommit)
{
  const OMPKHandshake handshake;
  EXPECT_EQ(0x0002, encode_and_get_server_status(handshake));
}

TEST(TestMySQLHandshake, explicit_status_override_is_preserved)
{
  OMPKHandshake handshake;
  handshake.set_server_status(0);
  EXPECT_EQ(0, encode_and_get_server_status(handshake));
}

} // namespace

int main(int argc, char **argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
