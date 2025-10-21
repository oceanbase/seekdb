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

#define private public
#include "ob_log_miner_file_manager.h"
#undef private
#include "share/io/ob_io_manager.h"
#include "share/ob_device_manager.h"
#include "gtest/gtest.h"

namespace oceanbase
{
namespace oblogminer
{

TEST(test_ob_log_miner_file_manager, GenFileHeader)
{
  ObLogMinerFileManager manager;
  const int64_t path_size = 100;
  char cwd_buf[path_size];
  ObArenaAllocator alloc;
  char *header = nullptr;
  int64_t data_len = 0;
  EXPECT_NE(nullptr, getcwd(cwd_buf, path_size));
  char path[OB_MAX_URI_LENGTH];
  char rm_dir_command[OB_MAX_URI_LENGTH];
  EXPECT_EQ(OB_SUCCESS, databuff_printf(path, sizeof(path), "file://%s/logminer_output", cwd_buf));
  EXPECT_EQ(OB_SUCCESS, databuff_printf(rm_dir_command, sizeof(rm_dir_command), "rm -rf %s/logminer_output", cwd_buf));
  EXPECT_EQ(OB_SUCCESS,
      manager.init(path, RecordFileFormat::CSV, ObLogMinerFileManager::FileMgrMode::ANALYZE));
  EXPECT_EQ(OB_SUCCESS, manager.generate_data_file_header_(alloc, header, data_len));
  EXPECT_STREQ(header, "TENANT_ID,TRANS_ID,PRIMARY_KEY,TENANT_NAME,DATABASE_NAME,"
  "TABLE_NAME,OPERATION,OPERATION_CODE,COMMIT_SCN,COMMIT_TIMESTAMP,SQL_REDO,SQL_UNDO,ORG_CLUSTER_ID\n");

  system(rm_dir_command);
}

}
}

int main(int argc, char **argv)
{
  // testing::FLAGS_gtest_filter = "DO_NOT_RUN";
  system("rm -f test_ob_log_miner_file_manager.log");
  oceanbase::ObLogger &logger = oceanbase::ObLogger::get_logger();
  logger.set_file_name("test_ob_log_miner_file_manager.log", true, false);
  logger.set_log_level("DEBUG");
  logger.set_enable_async_log(false);
  EXPECT_EQ(OB_SUCCESS, ObDeviceManager::get_instance().init_devices_env());
  EXPECT_EQ(OB_SUCCESS, ObIOManager::get_instance().init(1000000000));
  EXPECT_EQ(OB_SUCCESS, ObIOManager::get_instance().start());
//  ObTenantIOConfig tenant_io_config = ObTenantIOConfig::default_instance();
//  oceanbase::share::ObTenantBase *tenant_base = MTL_NEW(oceanbase::share::ObTenantBase, "unittest", OB_SERVER_TENANT_ID);
//  ObTenantIOManager *tio_manager = NULL;
//  ObTenantIOManager::mtl_new(tio_manager);
//  ObTenantIOManager::mtl_init(tio_manager);
  testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
