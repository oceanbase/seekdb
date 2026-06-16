// owner: wangzhennan.wzn 
// owner group: rs

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

#include "env/ob_simple_cluster_test_base.h"

namespace oceanbase
{
namespace share
{
using namespace schema;
using namespace common;

static const int64_t table_count = 6;
static const int64_t int_default_value = 1;
static const char *varchar_default_value = "aaa";
static const float float_default_value = 1.01;
static const double double_default_value = 2.111345;
static const char *timestamp_default_value = "2022-10-12 11:56:00.0";
static const char *datetime_default_value = "2022-10-12";

class TestProxyDefaultValue : public unittest::ObSimpleClusterTestBase
{
public:
  TestProxyDefaultValue() : unittest::ObSimpleClusterTestBase("test_proxy_default_value") {}
private:
};

TEST_F(TestProxyDefaultValue, test_mysql_common_data_types)
{
  int ret = OB_SUCCESS;
  common::ObMySQLProxy &sql_proxy = get_curr_simple_server().get_sql_proxy();
  ObSqlString sql;
  int64_t affected_rows = 0;
  ASSERT_EQ(OB_SUCCESS, sql.assign_fmt("use oceanbase;"));
  ASSERT_EQ(OB_SUCCESS, sql_proxy.write(sql.ptr(), affected_rows));

  // create table
  sql.reset();
  ret = sql.assign_fmt("create table t1(c1 int default %ld not null, c2 varchar(20) default '%s', "
      "c3 float default %f not null, c4 double default %lf not null, c5 timestamp default '%s', "
      "c6 datetime default '%s') partition by key(c1,c2,c3,c4,c5,c6)",
      int_default_value, varchar_default_value, float_default_value,
      double_default_value, timestamp_default_value, datetime_default_value);
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(OB_SUCCESS, sql_proxy.write(sql.ptr(), affected_rows));
  OB_LOG(INFO, "create_table succ");

  sql.reset();
  ret = sql.assign_fmt("select part_key_default_value from __all_virtual_proxy_partition_info "
      "where tenant_name = 'sys' and table_id = (select table_id from __all_virtual_table where table_name='t1')");
  ASSERT_EQ(OB_SUCCESS, ret);
  SMART_VAR(ObMySQLProxy::MySQLResult, result) {
  ASSERT_EQ(OB_SUCCESS, sql_proxy.read(result, OB_SYS_TENANT_ID, sql.ptr()));
    ASSERT_TRUE(OB_NOT_NULL(result.get_result()));
    sqlclient::ObMySQLResult &res = *result.get_result();
    int64_t index = 0;

    while (OB_SUCC(ret)) {
      if (OB_SUCC(res.next())) {
        ObString tmp_str;
        ObObj row;
        int64_t pos = 0;
        ret = res.get_varchar("part_key_default_value", tmp_str);
        ASSERT_EQ(OB_SUCCESS, ret);
        ret = row.deserialize(tmp_str.ptr(), tmp_str.length(), pos);
        ASSERT_EQ(OB_SUCCESS, ret);
        LOG_INFO("default value", K(index), K(row));
        switch(index) {
          case 0: {
            bool equal = int_default_value == row.get_int();
            ASSERT_TRUE(equal);
            break;
          }
          case 1: {
            bool equal = (0 == row.get_string().compare(varchar_default_value));
            ASSERT_TRUE(equal);
            break;
          }
          case 2: {
            bool equal = row.get_float() == float_default_value;
            ASSERT_TRUE(equal);
            break;
          }
          case 3: {
            bool equal = row.get_double() == double_default_value;
            ASSERT_TRUE(equal);
            break;
          }
          case 4: {
            int64_t timestamp = 0;
            ObTimeZoneInfo tz_info;
            char buf[50] = {0};
            ObString str;
            ObTimeConvertCtx cvrt_ctx(&tz_info, true);
            strcpy(buf, "+8:00");
            str.assign(buf, static_cast<int32_t>(strlen(buf)));
            tz_info.set_timezone(str);
            ret = ObTimeConverter::str_to_datetime(ObString(timestamp_default_value), cvrt_ctx, timestamp);
            ASSERT_EQ(OB_SUCCESS, ret);
            bool equal = row.get_timestamp() == timestamp;
            LOG_INFO("timestamp default value", K(row.get_timestamp()), K(timestamp));
            ASSERT_TRUE(equal);
            break;
          }
          case 5: {
            int64_t datetime = 0;
            ObTimeZoneInfo tz_info;
            char buf[50] = {0};
            ObString str;
            ObTimeConvertCtx cvrt_ctx(&tz_info, true);
            cvrt_ctx.oracle_nls_format_ = ObTimeConverter::COMPAT_OLD_NLS_TIMESTAMP_TZ_FORMAT;
            strcpy(buf, "+00:00");
            str.assign(buf, static_cast<int32_t>(strlen(buf)));
            tz_info.set_timezone(str);
            ret = ObTimeConverter::str_to_datetime(ObString(datetime_default_value), cvrt_ctx, datetime);
            ASSERT_EQ(OB_SUCCESS, ret);
            bool equal = row.get_datetime() == datetime;
            LOG_INFO("datetime default value", K(row.get_datetime()), K(datetime));
            ASSERT_TRUE(equal);
            break;
          }
          default: FAIL();
        }
        ++index;
      }
    } // end while
  }
}

TEST_F(TestProxyDefaultValue, test_default_value_is_null)
{
  int ret = OB_SUCCESS;
  // sys tenant sql_proxy
  common::ObMySQLProxy &sql_proxy = get_curr_simple_server().get_sql_proxy();

  // create table
  ObSqlString sql;
  int64_t affected_rows = 0;
  ret = sql.assign_fmt("create table t2 (c1 int default 1, c2 int, c3 int generated always as (c1 + 1) virtual) partition by key(c2, c3);");
  ASSERT_EQ(OB_SUCCESS, ret);
  ASSERT_EQ(OB_SUCCESS, sql_proxy.write(sql.ptr(), affected_rows));

  // query in sys tenant
  sql.reset();
  ret = sql.assign_fmt("select part_key_default_value from __all_virtual_proxy_partition_info "
      "where tenant_name = 'sys' and table_id = (select table_id from __all_virtual_table where table_name='t2')");
  ASSERT_EQ(OB_SUCCESS, ret);
  SMART_VAR(ObMySQLProxy::MySQLResult, result) {
  ASSERT_EQ(OB_SUCCESS, sql_proxy.read(result, OB_SYS_TENANT_ID, sql.ptr()));
    ASSERT_TRUE(OB_NOT_NULL(result.get_result()));
    sqlclient::ObMySQLResult &res = *result.get_result();
    int64_t index = 0;
    while (OB_SUCC(ret)) {
      if (OB_SUCC(res.next())) {
        ObString tmp_str;
        ret = res.get_varchar("part_key_default_value", tmp_str);
        ASSERT_EQ(OB_ERR_NULL_VALUE, ret);
      }
    }
  }
}

} // namespace share
} // namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::unittest::init_log_and_gtest(argc, argv);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
