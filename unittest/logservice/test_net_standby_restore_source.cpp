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

#include "src/share/backup/ob_log_restore_struct.h"
#include <gtest/gtest.h>

namespace oceanbase
{
using namespace common;
using namespace palf;

namespace unittest
{

TEST(TestRestoreSource, test_net_standby_restore_source)
{
  ObSqlString source_str;
  // Simplified: only IP_LIST is supported now
  char original_str[1000] = "127.0.0.1:1001";
  source_str.assign(original_str);

  ObSqlString ip_list;

  share::ObRestoreSourceServiceAttr attr, tmp_attr;
  (void)attr.parse_service_attr_from_str(source_str);

  EXPECT_TRUE(attr.is_valid());
  ASSERT_EQ(OB_SUCCESS, tmp_attr.assign(attr));
  EXPECT_TRUE(tmp_attr.is_valid());

  // check addr
  common::ObAddr addr1(common::ObAddr::VER::IPV4, "127.0.0.1", 1001);
  EXPECT_TRUE(attr.addr_.at(0) == addr1);

  (void)attr.get_ip_list_str_(ip_list);
  EXPECT_EQ(0, STRCMP(ip_list.ptr(),"127.0.0.1:1001"));

  EXPECT_TRUE(attr.is_valid());
  EXPECT_TRUE(attr == tmp_attr);

  // check invalid attr
  share::ObRestoreSourceServiceAttr attr1;
  (void)attr1.assign(attr);
  attr1.addr_.reset();
  EXPECT_FALSE(attr1.is_valid());
}

} // end of unittest
} // end of oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_file_name("test_net_standby_restore_source.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
