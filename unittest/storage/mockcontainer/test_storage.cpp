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

#include "mock_ob_server.h"
#include <gtest/gtest.h>

using namespace oceanbase;
using namespace oceanbase::common;
using namespace oceanbase::unittest;

class TestObStorage : public ::testing::Test
{
public :
  virtual void SetUp() {}
  virtual void TearDown() {}
};

TEST_F(TestObStorage, test1)
{
}

int main(int argc, char **argv)
{
  int ret = EXIT_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  static ObServerOptions server_options =
    {8021, 8011, NULL, NULL, false, NULL, NULL, "127.0.0.1", "storage", "./tmp/data"};
  MockObServer ob_server(server_options);
  const char *schema_file = "./test.schema";

  ObLogger &logger = ObLogger::get_logger();
  logger.set_file_name("test_storage.log", true);
  logger.set_log_level(OB_LOG_LEVEL_INFO);

    STORAGE_LOG(WARN, "init memory pool error", "ret", tmp_ret);
  } else if (OB_SUCCESS != (tmp_ret = ob_server.init(schema_file))) {
    STORAGE_LOG(WARN, "init ob server error", "ret", tmp_ret, K(schema_file));
  } else {
    STORAGE_LOG(INFO, "init ob server success", K(schema_file));
  }
  if (OB_SUCCESS != tmp_ret) {
    ret = EXIT_FAILURE;
  } else {
    testing::InitGoogleTest(&argc, argv);
    ret = RUN_ALL_TESTS();
  }

  return ret;
}
