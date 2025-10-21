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

#pragma once

#include <gtest/gtest.h>

#include "ob_simple_server.h"

namespace oceanbase
{
namespace unittest
{

int set_trace_id(char *buf);
void init_log_and_gtest(int argc, char **argv);
void init_gtest_output(std::string &gtest_log_name);

class ObSimpleClusterTestBase : public testing::Test
{
public:
  static const int64_t TRANS_TIMEOUT = 5 * 1000 * 1000;
  // set_bootstrap_and_create_tenant_warn_log default bootstrap and create tenant use WARN log, accelerate startup
  ObSimpleClusterTestBase(const std::string &env_prefix = "run_",
                          const char *log_disk_size = "10G",
                          const char *memory_limit = "16G",
                          const char *datafile_size = "10G");
  virtual ~ObSimpleClusterTestBase();

  int start();
  static int close();
  observer::ObServer& get_curr_observer() { return cluster_->get_observer(); }
  observer::ObSimpleServer& get_curr_simple_server() { return *cluster_; }

  int create_tenant(const char *tenant_name = "tt1",
                    const char *memory_size = "4G",
                    const char *log_disk_size = "4G",
                    const bool oracle_mode = false,
                    int64_t tenant_cpu = 2);
  int delete_tenant(const char *tenant_name = "tt1");
  int get_tenant_id(uint64_t &tenant_id, const char *tenant_name = "tt1");
  int exec_write_sql_sys(const char *sql_str, int64_t &affected_rows);
  int check_tenant_exist(bool &bool_ret, const char *tenant_name = "tt1");
  int batch_create_table(const uint64_t tenant_id,
                         ObMySQLProxy &sql_proxy,
                         const int64_t TOTAL_NUM,
                         ObIArray<ObTabletLSPair> &tablet_ls_pairs);
  int batch_drop_table(const uint64_t tenant_id,
                       ObMySQLProxy &sql_proxy,
                       const int64_t TOTAL_NUM);

protected:
  virtual void SetUp();
  virtual void TearDown();
  static void TearDownTestCase();

protected:
  // Because the usage of ObServer in ob_server.h now only allows starting a single server
  static std::shared_ptr<observer::ObSimpleServer> cluster_;
  static bool is_started_;
  static std::thread th_;
  static std::string env_prefix_;
  static std::string curr_dir_;
  static bool enable_env_warn_log_;
  static const char *UNIT_BASE;
  static const char *POOL_BASE;
};

} // end unittest
} // end oceanbase
