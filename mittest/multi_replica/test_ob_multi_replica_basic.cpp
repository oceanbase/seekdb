// owner: weixiaoxian.wxx
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

#include <gtest/gtest.h>
#define USING_LOG_PREFIX SERVER
#define protected public
#define private public

#include "env/ob_multi_replica_util.h"

#define CUR_TEST_CASE_NAME ObSimpleMultiReplicaExampleTest

DEFINE_MULTI_ZONE_TEST_CASE_CLASS

APPEND_RESTART_TEST_CASE_CLASS(1, 1)

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
  int time_sec_ = 0;
};

TestRunCtx RunCtx;

TEST_F(ObSimpleMultiReplicaExampleTest_ZONE1, observer_start)
{
  SERVER_LOG(INFO, "observer_start succ");
}
// Creating a tenant is not lightweight, consider necessity of use based on the scenario
TEST_F(ObSimpleMultiReplicaExampleTest_ZONE1, add_tenant)
{
  // Create normal tenant tt1
  ASSERT_EQ(OB_SUCCESS, create_tenant());
  // Get the tenant_id of tenant tt1
  ASSERT_EQ(OB_SUCCESS, get_tenant_id(RunCtx.tenant_id_));
  ASSERT_NE(0, RunCtx.tenant_id_);
  // Initialize the sql proxy for normal tenant tt1
  ASSERT_EQ(OB_SUCCESS, get_curr_simple_server().init_sql_proxy2());
}

TEST_F(ObSimpleMultiReplicaExampleTest_ZONE1, create_table)
{
  int ret = OB_SUCCESS;
  // Use normal tenant tt1
  common::ObMySQLProxy &sql_proxy = get_curr_simple_server().get_sql_proxy2();
  // Create table
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
    //check row count
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

TEST_F(ObSimpleMultiReplicaExampleTest_ZONE1, restart_zone1)
{
  // ASSERT_EQ(OB_SUCCESS, delete_tenant());
  ASSERT_EQ(OB_SUCCESS, ObMultiReplicaTestBase::restart_zone(1, 1));
}

TEST_F(ObSimpleMultiReplicaExampleTest_ZONE1, end)
{
  RunCtx.time_sec_ = 0;
  if (RunCtx.time_sec_ > 0) {
    ::sleep(RunCtx.time_sec_);
  }
}

TEST_F(ObSimpleMultiReplicaExampleTest_ZONE2, end)
{
  RunCtx.time_sec_ = 0;
  if (RunCtx.time_sec_ > 0) {
    ::sleep(RunCtx.time_sec_);
  }
}

TEST_F(ObSimpleMultiReplicaExampleTest_ZONE3, end)
{
  RunCtx.time_sec_ = 0;
  if (RunCtx.time_sec_ > 0) {
    ::sleep(RunCtx.time_sec_);
  }
}

TEST_F(GET_RESTART_ZONE_TEST_CLASS_NAME(1, 1), restart_zone1)
{
}

} // end unittest
} // end oceanbase

MULTI_REPLICA_TEST_MAIN_FUNCTION(test_multi_replica_basic_);
