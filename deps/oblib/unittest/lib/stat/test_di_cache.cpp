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
#include "lib/stat/ob_diagnose_info.h"
#include "lib/stat/ob_diagnostic_info_guard.h"

using namespace oceanbase;
using namespace oceanbase::common;

TEST(ObDIRuntimeCache, concurrent_stat_updates)
{
  static const int64_t THREAD_COUNT = 4;
  static const int64_t UPDATE_COUNT = 10000;
  ObDIGlobalRuntimeCache::get_instance().reset();

  std::thread workers[THREAD_COUNT];
  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    workers[i] = std::thread([]() {
      for (int64_t j = 0; j < UPDATE_COUNT; ++j) {
        EVENT_INC(RPC_PACKET_IN);
      }
    });
  }
  for (int64_t i = 0; i < THREAD_COUNT; ++i) {
    workers[i].join();
  }

  ObDiagnoseRuntimeInfo runtime_info;
  ASSERT_EQ(OB_SUCCESS,
      ObDIGlobalRuntimeCache::get_instance().get_runtime_info(runtime_info));
  ASSERT_EQ(THREAD_COUNT * UPDATE_COUNT,
      runtime_info.get_add_stat_stats()
          .get(ObStatEventIds::RPC_PACKET_IN)->get_stat_value());
}

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
