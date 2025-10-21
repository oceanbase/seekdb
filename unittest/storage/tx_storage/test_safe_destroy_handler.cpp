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

#define USING_LOG_PREFIX STORAGETEST

#define protected public
#define private public

#include "storage/tx_storage/ob_safe_destroy_handler.h"

namespace oceanbase
{
using namespace common;
namespace storage
{
class TestSafeDestroyHandler : public ::testing::Test
{
public:
  TestSafeDestroyHandler() = default;
  virtual ~TestSafeDestroyHandler() = default;
};


TEST_F(TestSafeDestroyHandler, destroy_before_init)
{
  int ret = OB_SUCCESS;
  ObSafeDestroyHandler handler;
  ret = handler.stop();
  ASSERT_EQ(OB_NOT_INIT, ret);
  handler.wait();
  handler.destroy();
}


} // namespace storage
} // namespace oceanbase

int main(int argc, char **argv)
{
  system("rm -f ./test_safe_destroy_handler.log*");

  OB_LOGGER.set_file_name("test_safe_destroy_handler.log", true);
  OB_LOGGER.set_log_level("INFO");
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
