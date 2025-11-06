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

#include <cstdlib>

#include "gtest/gtest.h"

#define private public

#include "ob_inc_backup_producer.h"

using namespace oceanbase;
using namespace common;
using namespace tools;

namespace oceanbase
{
namespace unittest
{

class TestObIncBackUpProducer: public ::testing::Test
{
public :
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST(TestObIncBackUpProducer, confirm_tb_white_list)
{
  ObIncBackUpProducer producer;
  const int64_t tb_white_list_len = 1024;
  char tb_white_list[tb_white_list_len];

  const char *tenant_white_list0 = "*";
  EXPECT_EQ(OB_SUCCESS, producer.confirm_tb_white_list_(tenant_white_list0, tb_white_list, tb_white_list_len));
  EXPECT_STREQ("*.*.*", tb_white_list);

  const char *tenant_white_list1 = "tt1";
  EXPECT_EQ(OB_SUCCESS, producer.confirm_tb_white_list_(tenant_white_list1, tb_white_list, tb_white_list_len));
  EXPECT_STREQ("tt1.*.*", tb_white_list);

  const char *tenant_white_list2 = "tt1*";
  EXPECT_EQ(OB_SUCCESS, producer.confirm_tb_white_list_(tenant_white_list2, tb_white_list, tb_white_list_len));
  EXPECT_STREQ("tt1*.*.*", tb_white_list);

  const char *tenant_white_list3 = "tt1*|tt2*|tt3*";
  EXPECT_EQ(OB_SUCCESS, producer.confirm_tb_white_list_(tenant_white_list3, tb_white_list, tb_white_list_len));
  EXPECT_STREQ("tt1*.*.*|tt2*.*.*|tt3*.*.*", tb_white_list);

  const char *tenant_white_list4 = "tt1*|tt2|tt3|tt4*";
  EXPECT_EQ(OB_SUCCESS, producer.confirm_tb_white_list_(tenant_white_list4, tb_white_list, tb_white_list_len));
  EXPECT_STREQ("tt1*.*.*|tt2.*.*|tt3.*.*|tt4*.*.*", tb_white_list);

  const char *tenant_white_list5 = "tt1|tt2|tt3|tt4*";
  EXPECT_EQ(OB_SUCCESS, producer.confirm_tb_white_list_(tenant_white_list5, tb_white_list, tb_white_list_len));
  EXPECT_STREQ("tt1.*.*|tt2.*.*|tt3.*.*|tt4*.*.*", tb_white_list);
}

}//end of unittest
}//end of oceanbase

int main(int argc, char **argv)
{
  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_ob_inc_backup_producer.log", true);
  logger.set_log_level(OB_LOG_LEVEL_INFO);

  //oceanbase::common::ObLogger::get_logger().set_log_level("debug");
  testing::InitGoogleTest(&argc,argv);
  // testing::FLAGS_gtest_filter = "DO_NOT_RUN";
  return RUN_ALL_TESTS();
}
