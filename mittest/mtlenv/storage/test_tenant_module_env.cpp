// owner: lana.lgx
// owner group: transaction

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

#include "../mock_tenant_module_env.h"

namespace oceanbase
{
using namespace storage;

class TestTenantModuleEnv : public ::testing::Test
{
public:
  static void SetUpTestCase()
  {
    EXPECT_EQ(OB_SUCCESS, MockTenantModuleEnv::get_instance().init());
  }
  static void TearDownTestCase()
  {
    MockTenantModuleEnv::get_instance().destroy();
  }
  void SetUp()
  {
    ASSERT_TRUE(MockTenantModuleEnv::get_instance().is_inited());
  }
};

TEST_F(TestTenantModuleEnv, basic)
{
  EXPECT_EQ(OB_SYS_TENANT_ID, MTL_ID());
  //
  // MTL(ObLSSerivce*)
  // do ...
  //
}

TEST_F(TestTenantModuleEnv, basic2)
{
  EXPECT_EQ(OB_SYS_TENANT_ID, MTL_ID());
  //
  // MTL(ObLSSerivce*)
  // do ...
  //
}

} // end oceanbase

int main(int argc, char **argv)
{
  system("rm -rf test_tenant_module_env.log");
  OB_LOGGER.set_file_name("test_tenant_module_env.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
