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
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#define private public
#include "lib/thread/ob_reentrant_thread.h"

namespace oceanbase
{
using namespace common;
namespace share
{

class TestReentrantThread : public ObReentrantThread
{
public:
  TestReentrantThread() : sleeped_cnt_(0) {}

  volatile int64_t sleeped_cnt_;
  void run()
  {
    while (!stop_) {
      usleep(10000);
      sleeped_cnt_++;
    }
  }
  int blocking_run() { return ObReentrantThread::blocking_run(); }
};

TEST(TestReentrantThread, all)
{
  TestReentrantThread thread;
  ASSERT_NE(OB_SUCCESS, thread.create(0));

  const int64_t thread_cnt = ObReentrantThread::TID_ARRAY_DEF_SIZE * 2;
  ASSERT_EQ(OB_SUCCESS, thread.create(thread_cnt));
  ASSERT_EQ(OB_SUCCESS, thread.destroy());

  ASSERT_EQ(OB_SUCCESS, thread.create(thread_cnt));
  ASSERT_EQ(0, thread.running_cnt_);
  ASSERT_EQ(OB_SUCCESS, thread.start());
  usleep(10000);
  ASSERT_EQ(thread_cnt, thread.running_cnt_);
  ASSERT_EQ(OB_SUCCESS, thread.stop());
  ASSERT_EQ(OB_SUCCESS, thread.wait());
  ASSERT_EQ(0, thread.running_cnt_);

  // start again
  ASSERT_EQ(OB_SUCCESS, thread.start());
  usleep(10000);
  ASSERT_EQ(thread_cnt, thread.running_cnt_);

  ASSERT_EQ(OB_SUCCESS, thread.destroy());
  ASSERT_EQ(0, thread.running_cnt_);
}

} // end namespace share
} // end namespace oceanbase

int main(int argc, char *argv[])
{
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
