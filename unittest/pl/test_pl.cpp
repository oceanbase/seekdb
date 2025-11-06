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

#define USING_LOG_PREFIX PL
#include <gtest/gtest.h>
#include "observer/ob_server.h"
#include "test_pl_utils.h"
#include "sql/ob_sql_init.h"

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::share;
using namespace observer;
using namespace test;

class ObPLTest : public TestPLUtils, public ::testing::Test
{
public:
  ObPLTest() {}
  virtual ~ObPLTest() {}
  virtual void SetUp() { init(); }
  virtual void TearDown() {}
  virtual void init() { TestPLUtils::init(); }

public:
  // data members

};

TEST_F(ObPLTest, test_resolve)
{
  const char* test_file = "./test_pl.sql";
  const char* result_file = "./test_resolve.result";
  const char* tmp_file = "./test_resolve.tmp";
  ASSERT_NO_FATAL_FAILURE(resolve_test(test_file, result_file, tmp_file));
}

TEST_F(ObPLTest, test_compile)
{
  const char* test_file = "./test_pl.sql";
  const char* result_file = "./test_compile.result";
  const char* tmp_file = "./test_compile.tmp";
  ASSERT_NO_FATAL_FAILURE(compile_test(test_file, result_file, tmp_file));
}

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("WARN");
  init_sql_factories();
  ::testing::InitGoogleTest(&argc,argv);
  ObScannerPool::build_instance();
  ObIntermResultPool::build_instance();
  ObServerOptions opts;
  opts.cluster_id_ = 1;
  opts.rs_list_ = "127.0.0.1:1";
  opts.zone_ = "test1";
  opts.data_dir_ = "/tmp";
  opts.mysql_port_ = 23000 + getpid() % 1000;
  opts.rpc_port_ = 24000 + getpid() % 1000;
  system("mkdir -p /tmp/sstable /tmp/clog /tmp/slog");
  GCONF.datafile_size = 41943040;
//  OBSERVER.init(opts);
  int ret = 0;//RUN_ALL_TESTS();
  OBSERVER.destroy();
  return ret;
}

