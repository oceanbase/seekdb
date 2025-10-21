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
#include "db_initializer.h"

namespace oceanbase
{
namespace share
{
namespace schema
{
using namespace common;

TEST(TestDBInitializer, init)
{
  DBInitializer initer;
  int ret = initer.init();
  ASSERT_EQ(OB_SUCCESS, ret);
}

// TEST(TestDBInitializer, create_system_table)
// {
//   {
//     DBInitializer initer;
//     int ret = initer.init();
//     ASSERT_EQ(OB_SUCCESS, ret);

//     const bool only_core_tables = true;
//     ret = initer.create_system_table(only_core_tables);
//     ASSERT_EQ(OB_SUCCESS, ret);
//   }

//   {
//     DBInitializer initer;
//     int ret = initer.init();
//     ASSERT_EQ(OB_SUCCESS, ret);

//     const bool only_core_tables = false;
//     ret = initer.create_system_table(only_core_tables);
//     ASSERT_EQ(OB_SUCCESS, ret);

//     ret = initer.fill_sys_stat_table();
//     ASSERT_EQ(OB_SUCCESS, ret);

//     ret = initer.create_tenant_space(2);
//     ASSERT_EQ(OB_SUCCESS, ret);
//   }
// }

TEST(TestDBInitializer, inactive_sql_client)
{
  const uint64_t tenant_id = OB_SYS_TENANT_ID;
  DBInitializer initer;
  int ret = initer.init();
  ASSERT_EQ(OB_SUCCESS, ret);

  ObMySQLProxy &proxy = initer.get_sql_proxy();
  proxy.set_inactive();

  ObSqlString sql;
  sql.assign("select 1");
  int64_t ar = false;
  ret = proxy.write(sql.ptr(), ar);
  ASSERT_EQ(OB_INACTIVE_SQL_CLIENT, ret);

  {
    ObISQLClient::ReadResult rs;
    ret = proxy.read(rs, sql.ptr());
    ASSERT_EQ(OB_INACTIVE_SQL_CLIENT, ret);
  }

  {
    ObMySQLTransaction trans;
    ret = trans.start(&proxy, tenant_id);
    ASSERT_EQ(OB_INACTIVE_SQL_CLIENT, ret);
  }

  {
    proxy.set_active();
    ObMySQLTransaction trans;
    ret = trans.start(&proxy, tenant_id);
    ASSERT_EQ(OB_SUCCESS, ret);
    proxy.set_inactive();
    ret = proxy.write(sql.ptr(), ar);
    ASSERT_EQ(OB_INACTIVE_SQL_CLIENT, ret);
  }
}

} // end namespace schema
} // end namespace share
} // end namespace oceanbase

int main(int argc, char **argv)
{
  testing::InitGoogleTest(&argc, argv);
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  return RUN_ALL_TESTS();
}

