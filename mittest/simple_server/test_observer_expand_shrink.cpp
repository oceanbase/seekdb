// owner: zjf225077
// owner group: clog

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
#include "observer/ob_server_utils.h"
#include "env/ob_simple_server_restart_helper.h"
#undef protected
#undef private

const char *TEST_FILE_NAME = "test_observer_expand_shrink";
const char *BORN_CASE_NAME= "ObserverExpandShink";
const char *RESTART_CASE_NAME = "ObserverExpandShinkRestart";
namespace oceanbase
{
using namespace logservice;
namespace unittest
{

class TestRunCtx
{
public:
  uint64_t tenant_id_ = 0;
  int time_sec_ = 0;
};

TestRunCtx RunCtx;
class ObserverExpandShink : public ObSimpleClusterTestBase
{
public:
  // Specify the case run directory prefix test_ob_simple_cluster_
  ObserverExpandShink() : ObSimpleClusterTestBase(TEST_FILE_NAME) {}
};

TEST_F(ObserverExpandShink, observer_start)
{
  SERVER_LOG(INFO, "start observer success");
}

TEST_F(ObserverExpandShink, basic_func)
{
  omt::ObTenantNodeBalancer::get_instance().refresh_interval_ = 1 * 1000 * 1000;
  int64_t origin_server_in_use_size, origin_server_log_total_size;
  EXPECT_EQ(OB_SUCCESS, GCTX.log_block_mgr_->get_disk_usage(origin_server_in_use_size, origin_server_log_total_size));
  GCONF.log_disk_size = GCTX.log_block_mgr_->lower_align_(2 * origin_server_log_total_size);
  sleep(11);
  int64_t new_server_in_use_size, new_server_log_total_size;
  EXPECT_EQ(OB_SUCCESS, GCTX.log_block_mgr_->get_disk_usage(new_server_in_use_size, new_server_log_total_size));
  EXPECT_EQ(new_server_log_total_size, 2 * origin_server_log_total_size);
  LOG_INFO("first resize success");
  GCONF.log_disk_size = 0;
  sleep(3);
  EXPECT_EQ(OB_SUCCESS, GCTX.log_block_mgr_->get_disk_usage(new_server_in_use_size, new_server_log_total_size));
  EXPECT_NE(new_server_log_total_size, 0);
  LOG_INFO("second resize success");

  int64_t affected_rows = 0;
  std::string succ_sql_str = "ALTER RESOURCE UNIT sys_unit_config LOG_DISK_SIZE='3G'";
  EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(succ_sql_str.c_str(), affected_rows));
  // tenant_node_balancer runs once every 1 s
  sleep(3);

  std::string succ_sql_str1 = "ALTER SYSTEM SET log_disk_utilization_limit_threshold = 81";
  EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(succ_sql_str1.c_str(), affected_rows));
  // Validate tenant specification modification failure, error less than clog disk lower limit
  std::string fail_sql_str = "ALTER RESOURCE UNIT sys_unit_config LOG_DISK_SIZE='1G'";
  EXPECT_EQ(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, exec_write_sql_sys(fail_sql_str.c_str(), affected_rows));
  std::string succ_sql_str2 = "ALTER SYSTEM SET log_disk_size = '4G'";
  EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(succ_sql_str2.c_str(), affected_rows));
  sleep(6);
  EXPECT_EQ(GCONF.log_disk_size, 4 * 1024 * 1024 * 1024ul);
  // Verify that modifying tenant specification failed, clog disk space insufficient
  std::string fail_sql_str1 = "ALTER RESOURCE UNIT sys_unit_config LOG_DISK_SIZE='100G'";
  EXPECT_EQ(OB_MACHINE_RESOURCE_NOT_ENOUGH, exec_write_sql_sys(fail_sql_str1.c_str(), affected_rows));
  // Validate tenant creation failure, clog disk space insufficient
  std::string succ_sql_str3 = "ALTER SYSTEM SET log_disk_size = '3G'";
  EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(succ_sql_str3.c_str(), affected_rows));
  sleep(6);
  EXPECT_EQ(GCONF.log_disk_size, 3 * 1024 * 1024 * 1024ul);
  EXPECT_EQ(OB_ZONE_RESOURCE_NOT_ENOUGH, create_tenant("runlin"));
  CLOG_LOG_RET(ERROR, OB_SUCCESS, "create runlin finished");
  GCONF.log_disk_size = 1024 * 1024 * 1024ul * 1024 * 100ul;
  int64_t log_disk_size = 0;
  int64_t log_disk_percentage = 0;
  int64_t total_space = 0;
  EXPECT_EQ(OB_SUCCESS, observer::ObServerUtils::get_log_disk_info_in_config(
                                                                log_disk_size,
                                                                log_disk_percentage,
                                                                total_space));
  EXPECT_EQ(true, total_space < log_disk_size);
}

template<typename ... Args>
std::string string_format( const std::string& format, Args ... args )
{
    int size_s = std::snprintf( nullptr, 0, format.c_str(), args ... ) + 1; // Extra space for '\0'
    if( size_s <= 0 ){ throw std::runtime_error( "Error during formatting." ); }
    auto size = static_cast<size_t>( size_s );
    std::unique_ptr<char[]> buf( new char[ size ] );
    std::snprintf( buf.get(), size, format.c_str(), args ... );
    return std::string( buf.get(), buf.get() + size - 1 ); // We don't want the '\0' inside
}

TEST_F(ObserverExpandShink, resize_tenant_log_disk)
{
  omt::ObTenantNodeBalancer::get_instance().refresh_interval_ = 1 * 1000 * 1000;
  sleep(10);
  GCONF.log_disk_size = 20 * 1024 * 1024 * 1024ul;
  bool tenant_exist = true;
  int ret = OB_SUCCESS;
  delete_tenant("runlin");
  while (true == tenant_exist && OB_SUCC(ret)) {
    if (OB_FAIL(check_tenant_exist(tenant_exist, "runlin"))) {
      SERVER_LOG(WARN, "check_tenant_exist failed", K(ret));
    } 
  }
  // Ensure log_disk_size change to 10G takes effect
  sleep(3);
  int64_t log_disk_origin_assigned = GCTX.log_block_mgr_->min_log_disk_size_for_all_tenants_;
  bool bool_ret = true;
  EXPECT_EQ(bool_ret, true);
  LOG_INFO("runlin trace, before create one default tenant", KPC(GCTX.log_block_mgr_), K(log_disk_origin_assigned));
  // The size of each tenant's log disk is 2G (default value)
  EXPECT_EQ(OB_SUCCESS, create_tenant("runlin1"));
  LOG_INFO("runlin trace, after create one default tenant", KPC(GCTX.log_block_mgr_), K(log_disk_origin_assigned));
  EXPECT_EQ(OB_SUCCESS, create_tenant("runlin2"));
  EXPECT_EQ(GCTX.log_block_mgr_->min_log_disk_size_for_all_tenants_,
            log_disk_origin_assigned + 4*1024*1024*1024ul);
  LOG_INFO("runlin trace, after create two default tenant", KPC(GCTX.log_block_mgr_), K(log_disk_origin_assigned));
  // Modify tenant specification
  int64_t affected_rows = 0;
  std::string sql_str = "ALTER RESOURCE UNIT %s%s LOG_DISK_SIZE='%s'";
  {
    std::string alter_resource_failed_sql = string_format(sql_str, UNIT_BASE, "runlin1", "100G");
    EXPECT_EQ(OB_MACHINE_RESOURCE_NOT_ENOUGH, exec_write_sql_sys(alter_resource_failed_sql.c_str(), affected_rows));
    LOG_INFO("runlin trace, alter resource failed", KPC(GCTX.log_block_mgr_));
  }
  {
    std::string alter_resource_failed_sql = string_format(sql_str, UNIT_BASE, "runlin1", "1G");
    EXPECT_EQ(OB_RESOURCE_UNIT_VALUE_BELOW_LIMIT, exec_write_sql_sys(alter_resource_failed_sql.c_str(), affected_rows));
    LOG_INFO("runlin trace, alter resource below limit", KPC(GCTX.log_block_mgr_));
  }
  {
    // Expansion validation
    std::string alter_resource_runlin1 = string_format(sql_str, UNIT_BASE, "runlin1", "6G");
    std::string alter_resource_runlin2 = string_format(sql_str, UNIT_BASE, "runlin2", "6G");
    EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(alter_resource_runlin1.c_str(), affected_rows));
    EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(alter_resource_runlin2.c_str(), affected_rows));
    sleep(3);
    // Expansion operation executed successfully directly
    EXPECT_EQ(GCTX.log_block_mgr_->min_log_disk_size_for_all_tenants_,
              log_disk_origin_assigned + 12*1024*1024*1024ul);
    LOG_INFO("runlin trace, alter resource to 6G success", KPC(GCTX.log_block_mgr_));
  }
  {
    // Shrink validation
    // Pause the operation of TenantNodeBalancer
    omt::ObTenantNodeBalancer::get_instance().refresh_interval_ = 10 * 1000 * 1000;
    int64_t start_ts = ObTimeUtility::current_time();
    sleep(2);
    {
      std::string alter_resource_runlin1 = string_format(sql_str, UNIT_BASE, "runlin1", "2G");
      std::string alter_resource_runlin2 = string_format(sql_str, UNIT_BASE, "runlin2", "2G");
      EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(alter_resource_runlin1.c_str(), affected_rows));
      EXPECT_EQ(OB_SUCCESS, exec_write_sql_sys(alter_resource_runlin2.c_str(), affected_rows));
    }
    const int64_t cost_ts = ObTimeUtility::current_time() - start_ts;
    // Assume the sleep + execution of sql time is less than 4s
    EXPECT_LE(cost_ts, 4*1000*1000);
    // At this time, the TenantNodeBalancer thread has not started running, ObServerLogBlockMgr's min_log_disk_size_for_all_tenants_ will not change
    EXPECT_EQ(GCTX.log_block_mgr_->min_log_disk_size_for_all_tenants_,
              log_disk_origin_assigned + 12*1024*1024*1024ul);
    omt::ObTenantNodeBalancer::get_instance().refresh_interval_ = 1 * 1000 * 1000;
    sleep(11);
    LOG_INFO("runlin trace, alter resource to 2G success", KPC(GCTX.log_block_mgr_));
    EXPECT_EQ(GCTX.log_block_mgr_->min_log_disk_size_for_all_tenants_,
              log_disk_origin_assigned + 4*1024*1024*1024ul);
  }
  EXPECT_EQ(OB_SUCCESS, delete_tenant("runlin1"));
  EXPECT_EQ(OB_SUCCESS, delete_tenant("runlin2"));
}

//class ObserverExpandShinkRestart: public ObSimpleClusterTestBase
//{
//public:
//  ObserverExpandShinkRestart() : ObSimpleClusterTestBase(TEST_FILE_NAME) {}
//};
//
//TEST_F(ObserverExpandShinkRestart, create_tenant_after_restart) 
//{
//  EXPECT_NE(0, GCTX.log_block_mgr_->min_log_disk_size_for_all_tenants_);
//  SERVER_LOG(INFO, "create_tenant_after_restart trace", KPC(GCTX.log_block_mgr_));
//}
} // end unittest
} // end oceanbase

int main(int argc, char **argv)
{
  int c = 0;
  int time_sec = 0;
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
  ObSimpleServerRestartHelper restart_helper(argc, argv, TEST_FILE_NAME, BORN_CASE_NAME,
                                             RESTART_CASE_NAME);
  restart_helper.set_sleep_sec(time_sec);
  restart_helper.run();
  return RUN_ALL_TESTS();
}
