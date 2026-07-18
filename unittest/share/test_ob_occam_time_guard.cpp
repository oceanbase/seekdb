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

#define UNITTEST_DEBUG
#include "share/ob_occam_time_guard.h"
#include <gtest/gtest.h>
#include <thread>

namespace oceanbase {
namespace unittest {

using namespace common;
using namespace std;

class TestObOccamTimeGuard: public ::testing::Test
{
public:
  TestObOccamTimeGuard() {};
  virtual ~TestObOccamTimeGuard() {};
  virtual void SetUp() { OB_LOG(DEBUG, "set up", K(ObClockGenerator::getRealClock())); };
  virtual void TearDown() { OB_LOG(DEBUG, "TearDown", K(ObClockGenerator::getRealClock())); };
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestObOccamTimeGuard);
};

void test1() {
  TIMEGUARD_INIT(OCCAM, 10_ms);
  this_thread::sleep_for(chrono::seconds(2));
  // OB_LOG(INFO, "", KTIMERANGE(ObClockGenerator::getRealClock(), HOUR, DAY));// compile error
}

void test0() {
  TIMEGUARD_INIT(OCCAM, 10_ms);
  CLICK();
  test1();
  this_thread::sleep_for(chrono::seconds(2));
}

void test2() {
  TIMEGUARD_INIT(10_ms, 1_s);
  this_thread::sleep_for(chrono::seconds(2));
}

TEST_F(TestObOccamTimeGuard, double_threshold_guard_fallback) {
  auto just_sleep = []() {
    TIMEGUARD_INIT(OCCAM, 10_ms);
    this_thread::sleep_for(chrono::seconds(2));
  };
  thread th(just_sleep);
  th.join();
}

TEST_F(TestObOccamTimeGuard, normal) {
  std::thread t1(test0);
  std::thread t2(test2);
  t1.join();
  t2.join();
}

TEST_F(TestObOccamTimeGuard, normal2) {
  TIMEGUARD_INIT(OCCAM, 1_s);
  this_thread::sleep_for(chrono::seconds(2));
}

}
}

int main(int argc, char **argv)
{
  system("rm -rf test_ob_occam_time_guard.log");
  oceanbase::common::ObLogger &logger = oceanbase::common::ObLogger::get_logger();
  oceanbase::common::ObTscTimestamp::get_instance().init();
  logger.set_file_name("test_ob_occam_time_guard.log", false);
  logger.set_log_level(OB_LOG_LEVEL_DEBUG);
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
