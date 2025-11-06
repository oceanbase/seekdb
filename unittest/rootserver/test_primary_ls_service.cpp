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

#define USING_LOG_PREFIX RS
#include <gmock/gmock.h>
#define  private public
#include "rootserver/ob_primary_ls_service.h"
namespace oceanbase {
using namespace common;
using namespace share;
using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace rootserver{
//multiclustermanage
class TestPrimaryLSService : public testing::Test
{
public:
  TestPrimaryLSService() {}
  virtual ~TestPrimaryLSService(){}
  virtual void SetUp() {};
  virtual void TearDown() {}
  virtual void TestBody() {}
}
;
TEST_F(TestPrimaryLSService, LS_FLAG)
{
  int ret = OB_SUCCESS;
  ObLSFlag flag;
  ObLSFlagStr str;
  ObLSFlagStr empty_str;
  ObLSFlagStr str0("DUPLICATE");
  ObLSFlagStr str1("DUPLICATE ");
  ObLSFlagStr str2(" DUPLICATE ");
  ObLSFlagStr str3("BLOCK_TABLET_IN");
  ObLSFlagStr str4("BLOCK_TABLET_IN ");
  ObLSFlagStr str5("BLOCK_TABLET_IN|DUPLICATE");
  ObLSFlagStr str6("DUPLICATE|BLOCK_TABLET_IN");
  ObLSFlagStr str7("BLOCK_TABLET_IN  | DUPLICATE");
  ObLSFlagStr str8("BLOCK_TABLET_IN,DUPLICATE");
  ObLSFlagStr str9("BLOCK_TABLET_IN DUPLICATE");

  ret = flag.flag_to_str(str);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(empty_str, str);
  ASSERT_EQ(0, flag.flag_);
  LOG_INFO("test", K(flag), K(str));

  flag.set_block_tablet_in();
  ASSERT_EQ(2, flag.flag_);
  ret = flag.flag_to_str(str);
  ASSERT_EQ(str3, str);
  LOG_INFO("test", K(flag), K(str));

  flag.clear_block_tablet_in();
  ASSERT_EQ(0, flag.flag_);
  ret = flag.flag_to_str(str);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(empty_str, str);
  LOG_INFO("test", K(flag), K(str));

  flag.set_duplicate();
  ASSERT_EQ(1, flag.flag_);
  ret = flag.flag_to_str(str);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(str0, str);
  LOG_INFO("test", K(flag), K(str));

  flag.set_block_tablet_in();
  ASSERT_EQ(3, flag.flag_);
  ret = flag.flag_to_str(str);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(str6, str);
  LOG_INFO("test", K(flag), K(str));

  flag.clear_block_tablet_in();
  ASSERT_EQ(1, flag.flag_);
  ret = flag.flag_to_str(str);
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(str0, str);
  LOG_INFO("test", K(flag), K(str));

  ret = flag.str_to_flag(empty_str.str());
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(0, flag.flag_);
  LOG_INFO("test", K(flag));

  ret = flag.str_to_flag(str0.str());
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(1, flag.flag_);
  LOG_INFO("test", K(flag));

  ret = flag.str_to_flag(str1.str());
  ASSERT_EQ(OB_ERR_UNEXPECTED, ret);

  ret = flag.str_to_flag(str2.str());
  ASSERT_EQ(OB_ERR_UNEXPECTED, ret);

  ret = flag.str_to_flag(str3.str());
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(2, flag.flag_);
  LOG_INFO("test", K(flag));

  ret = flag.str_to_flag(str4.str());
  ASSERT_EQ(OB_ERR_UNEXPECTED, ret);

  ret = flag.str_to_flag(str5.str());
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(3, flag.flag_);
  LOG_INFO("test", K(flag));

  ret = flag.str_to_flag(str6.str());
  ASSERT_EQ(ret, OB_SUCCESS);
  ASSERT_EQ(3, flag.flag_);

  LOG_INFO("test", K(flag));
  ret = flag.str_to_flag(str7.str());
  ASSERT_EQ(OB_ERR_UNEXPECTED, ret);

  LOG_INFO("test", K(flag));

  ret = flag.str_to_flag(str8.str());
  ASSERT_EQ(OB_ERR_UNEXPECTED, ret);

  ret = flag.str_to_flag(str9.str());
  ASSERT_EQ(OB_ERR_UNEXPECTED, ret);


}

}
}
int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
#undef private
