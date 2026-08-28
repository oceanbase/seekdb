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

#include "common/mysqlclient/ob_mysql_proxy.h"

namespace oceanbase
{
namespace common
{
namespace
{

int mysql_proxy_factory_call_count = 0;
bool mysql_proxy_factory_is_ddl = false;
int32_t mysql_proxy_factory_group_id = 0;

int test_connection_factory(
    const bool is_ddl,
    const int32_t group_id,
    sqlclient::ObISQLConnectionGuard &conn)
{
  ++mysql_proxy_factory_call_count;
  mysql_proxy_factory_is_ddl = is_ddl;
  mysql_proxy_factory_group_id = group_id;
  conn.reset();
  return OB_EAGAIN;
}

TEST(TestMysqlProxy, explicit_connection_factory)
{
  ObCommonSqlProxy proxy;
  sqlclient::ObISQLConnectionGuard conn;
  mysql_proxy_factory_call_count = 0;
  mysql_proxy_factory_is_ddl = false;
  mysql_proxy_factory_group_id = 0;

  ASSERT_EQ(OB_SUCCESS, proxy.init(true, &test_connection_factory));
  EXPECT_EQ(OB_EAGAIN, proxy.acquire_connection(conn, 17));
  EXPECT_EQ(1, mysql_proxy_factory_call_count);
  EXPECT_TRUE(mysql_proxy_factory_is_ddl);
  EXPECT_EQ(17, mysql_proxy_factory_group_id);
  EXPECT_FALSE(conn.is_valid());
}

} // namespace
} // namespace common
} // namespace oceanbase
