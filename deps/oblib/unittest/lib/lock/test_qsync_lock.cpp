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
#include <thread>

#include "lib/lock/ob_qsync_lock.h"

namespace oceanbase
{
namespace share
{
using namespace common;

TEST(TestQSyncLock, basic)
{
  lib::ObMemAttr mem_attr(OB_SERVER_TENANT_ID, "TestQSyncLock");
  ObQSyncLock lock;
  lock.init(mem_attr);
  for (int i=0;i<100;i++) {
    ObQSyncLockReadGuard read_guard(lock);
  }
  ObQSyncLockWriteGuard write_guard(lock);
}

TEST(TestQSyncLock, concurrent)
{
  lib::ObMemAttr mem_attr(OB_SERVER_TENANT_ID, "TestQSyncLock");
  ObQSyncLock lock;
  lock.init(mem_attr);
  std::vector<std::thread> ths;
  bool g_stop = false;
  for (int i=0;i<10;i++) {
    std::thread th([&](){
      while (!g_stop) {
        ObQSyncLockReadGuard read_guard(lock);
      }
    });
    ths.push_back(std::move(th));
  }
  ::sleep(1);
  {
    auto start = ObTimeUtility::current_time();
    ObQSyncLockWriteGuard write_guard(lock);
    auto end = ObTimeUtility::current_time();
    std::cout << end-start << std::endl;
    ASSERT_TRUE(end - start < 1000000);
  }
  g_stop = true;
  for (auto &it : ths) {
    it.join();
  }
}


} // end share
} // oceanbase

int main(int argc, char **argv)
{
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

