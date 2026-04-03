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

#define USING_LOG_PREFIX SHARE
#include "share/ob_cluster_version.h"
#include <gtest/gtest.h>


namespace oceanbase
{
namespace share
{
using namespace common;
using namespace oceanbase::lib;
class TestClusterVersion: public ::testing::Test
{
public:
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestClusterVersion, init)
{
  uint64_t version = 0;

  version = cal_version(1, 0, 0, 0);
  ObClusterVersion ver_1;
  ASSERT_EQ(ver_1.init(version), OB_SUCCESS);
}

TEST_F(TestClusterVersion, refresh_cluster_version)
{
  ObClusterVersion ver_24;
  ASSERT_EQ(ver_24.refresh_cluster_version("1.0.0.0"), OB_SUCCESS);
}

TEST_F(TestClusterVersion, encode)
{
  uint64_t version = 0;
  version = cal_version(1, 0, 0, 0);
  ObClusterVersion ver_1;
  ASSERT_EQ(ver_1.init(version), OB_SUCCESS);
  ObClusterVersion ver_2;
  ASSERT_EQ(ver_2.refresh_cluster_version("1.0.0.0"), OB_SUCCESS);
  ASSERT_EQ(ver_1.get_cluster_version(), ver_2.get_cluster_version());
}

TEST_F(TestClusterVersion, is_valid)
{
  ASSERT_EQ(ObClusterVersion::is_valid("1.0.0.0"), OB_SUCCESS);
}

TEST_F(TestClusterVersion, get_version)
{
  uint64_t version = 0;
  uint64_t res_version = 0;
  version = cal_version(1, 0, 0, 0);
  ASSERT_EQ(ObClusterVersion::get_version("1.0.0.0", res_version), OB_SUCCESS);
  ASSERT_EQ(version, res_version);
}

TEST_F(TestClusterVersion, print_version_str)
{
  char version_str[OB_CLUSTER_VERSION_LENGTH] = {0};
  uint64_t version = 0;
  int64_t pos = 0;

  version = cal_version(1, 0, 0, 0);
  ASSERT_NE(OB_INVALID_INDEX, ObClusterVersion::print_version_str(version_str, OB_CLUSTER_VERSION_LENGTH, version));
  ASSERT_EQ(0, STRNCMP(version_str, "1.0.0.0", OB_CLUSTER_VERSION_LENGTH));
}

} // end share
} // end oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
