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
#define USING_LOG_PREFIX SERVER
#define protected public
#define private public

#include "env/ob_simple_cluster_test_base.h"

namespace oceanbase
{
namespace unittest
{

using namespace oceanbase::transaction;
using namespace oceanbase::storage;


class TestRunCtx
{
public:
  uint64_t tenant_id_ = 0;
  int64_t time_sec_ = 0;
};

TestRunCtx RunCtx;

class ObSimpleClusterExampleTest : public ObSimpleClusterTestBase
{
public:
  // Specify the case run directory prefix test_ob_simple_cluster_
  ObSimpleClusterExampleTest() : ObSimpleClusterTestBase("test_ob_simple_cluster_") {}
};

TEST_F(ObSimpleClusterExampleTest, observer_start)
{
  SERVER_LOG(INFO, "observer_start succ");
}
// Creating a tenant is not lightweight, consider necessity of use based on the scenario
TEST_F(ObSimpleClusterExampleTest, add_tenant)
{
  // Create normal tenant tt1
  ASSERT_EQ(OB_SUCCESS, create_tenant());
  // Get the tenant_id of tenant tt1
  ASSERT_EQ(OB_SUCCESS, get_tenant_id(RunCtx.tenant_id_));
  ASSERT_NE(0, RunCtx.tenant_id_);
  // Initialize the SQL proxy for the normal tenant tt1
  ASSERT_EQ(OB_SUCCESS, get_curr_simple_server().init_sql_proxy2());
}

/*
TEST_F(ObSimpleClusterExampleTest, create_table)
{
  int ret = OB_SUCCESS;
  // use normal tenant tt1
  common::ObMySQLProxy &sql_proxy = get_curr_simple_server().get_sql_proxy2();
  // create table
  {
    OB_LOG(INFO, "create_table start");
    ObSqlString sql;
    sql.assign_fmt(
      "create table school (sid int,sname varchar(100), primary key(sid)) "
      "partition by range(sid) (partition p0 values less than (100), partition p1 values less than (200), partition p2 values less than MAXVALUE)");
    int64_t affected_rows = 0;
    ASSERT_EQ(OB_SUCCESS, sql_proxy.write(sql.ptr(), affected_rows));
    OB_LOG(INFO, "create_table succ");
  }

  {
    OB_LOG(INFO, "insert data start");
    for (int i = 1;i <= 1000; i++) {
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.assign_fmt("insert into school values(%d, '%s')", i, "ob"));
      int64_t affected_rows = 0;
      ASSERT_EQ(OB_SUCCESS, sql_proxy.write(sql.ptr(), affected_rows));
    }
    // check row count
    OB_LOG(INFO, "check row count");
    {
      int64_t row_cnt = 0;
      ObSqlString sql;
      ASSERT_EQ(OB_SUCCESS, sql.assign_fmt("select count(*) row_cnt from school"));
      SMART_VAR(ObMySQLProxy::MySQLResult, res) {
        ASSERT_EQ(OB_SUCCESS, sql_proxy.read(res, sql.ptr()));
        sqlclient::ObMySQLResult *result = res.get_result();
        ASSERT_NE(nullptr, result);
        ASSERT_EQ(OB_SUCCESS, result->next());
        ASSERT_EQ(OB_SUCCESS, result->get_int("row_cnt", row_cnt));
      }
      ASSERT_EQ(row_cnt, 1000);
    }

  }
}
*/
/*
TEST_F(ObSimpleClusterExampleTest, delete_tenant)
{
  ASSERT_EQ(OB_SUCCESS, delete_tenant());
}
*/

TEST_F(ObSimpleClusterExampleTest, end)
{
  if (RunCtx.time_sec_ > 0) {
    ::sleep(RunCtx.time_sec_);
  }
}

} // end unittest
} // end oceanbase


int main(int argc, char **argv)
{
  int64_t c = 0;
  int64_t time_sec = 0;
  char *log_level = (char*)"INFO";
  while(EOF != (c = getopt(argc,argv,"t:l:"))) {
    switch(c) {
    case 't':
      time_sec = atoi(optarg);
      break;
    case 'l':
     log_level = optarg;
     oceanbase::unittest::ObSimpleClusterTestBase::enable_env_warn_log_ = false;
     break;
    default:
      break;
    }
  }
  oceanbase::unittest::init_log_and_gtest(argc, argv);
  OB_LOGGER.set_log_level(log_level);

  LOG_INFO("main>>>");
  oceanbase::unittest::RunCtx.time_sec_ = time_sec;
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
