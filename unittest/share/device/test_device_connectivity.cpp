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
#include "share/object_storage/ob_device_connectivity.h"
#include "share/object_storage/ob_object_storage_struct.h"

#define private public
#undef private

namespace oceanbase {
namespace unittest {

using namespace oceanbase::common;
using namespace oceanbase::share;

class TestDeviceConnectivity: public ::testing::Test
{
public:
  TestDeviceConnectivity() {}
  virtual ~TestDeviceConnectivity() {}
  virtual void SetUp()
  {
  }
  virtual void TearDown() {}
  static void SetUpTestCase() {}
  static void TearDownTestCase() {}
private:
  DISALLOW_COPY_AND_ASSIGN(TestDeviceConnectivity);
};

TEST_F(TestDeviceConnectivity, DISABLED_test_device_connectivity)
{
  const char *path = "oss://cloudstorageb1/test";
  const char *endpoint = "host=oss-cn-hangzhou.aliyuncs.com";
  const char *encrypt_authorization = "access_id=xxx&access_key=xxx";
  const char *extension = "";
  ObBackupDest storage_dest;
  ASSERT_EQ(OB_SUCCESS, storage_dest.set(path, endpoint, encrypt_authorization, extension));
  ObDeviceConnectivityCheckManager conn_check_mgr;
  ASSERT_EQ(OB_SUCCESS, conn_check_mgr.check_device_connectivity(storage_dest));
}

} // namespace unittest
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f test_device_connectivity.log");
  OB_LOGGER.set_file_name("test_device_connectivity.log");
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
