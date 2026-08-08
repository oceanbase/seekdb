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
#include "lib/lock/ob_thread_cond.h"
#include "lib/thread/threads.h"

namespace oceanbase
{
namespace common
{
class TestThreadCondStress: public lib::ThreadPool
{
public:
  TestThreadCondStress(ObThreadCond &cond, const bool is_wait);
  virtual ~TestThreadCondStress();
  void run1() final;
private:
  ObThreadCond &cond_;
  bool is_wait_;
};

TestThreadCondStress::TestThreadCondStress(ObThreadCond &cond, const bool is_wait)
 : cond_(cond),
   is_wait_(is_wait)
{
}

TestThreadCondStress::~TestThreadCondStress()
{
}

void TestThreadCondStress::run1()
{
  int ret = OB_SUCCESS;

  if (is_wait_) {
    while(!has_set_stop()) {
      ret = cond_.lock();
      ASSERT_EQ(OB_SUCCESS, ret);
      ret = cond_.wait();
      ASSERT_EQ(OB_SUCCESS, ret);
      ret = cond_.unlock();
      ASSERT_EQ(OB_SUCCESS, ret);
    }
  } else {
    while(!has_set_stop()) {
      ret = cond_.lock();
      ASSERT_EQ(OB_SUCCESS, ret);
      ret = cond_.signal();
      ASSERT_EQ(OB_SUCCESS, ret);
      ret = cond_.unlock();
      ASSERT_EQ(OB_SUCCESS, ret);
    }
  }
}

TEST(ObThreadCond, normal)
{
  int ret = OB_SUCCESS;
  ObThreadCond cond;

  //destroy when not init
  cond.destroy();

  //repeatedly init
  ret = cond.init(ObWaitEventIds::DEFAULT_COND_WAIT);
  ASSERT_EQ(OB_SUCCESS, ret);

  //empty signal
  ret = cond.lock();
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = cond.signal();
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = cond.broadcast();
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = cond.unlock();
  ASSERT_EQ(OB_SUCCESS, ret);

  //wait timeout
  ret = cond.lock();
  ASSERT_EQ(OB_SUCCESS, ret);
  ret = cond.wait(1);
  ASSERT_EQ(OB_TIMEOUT, ret);
  ret = cond.unlock();
  ASSERT_EQ(OB_SUCCESS, ret);

  //repeatly destroy
  cond.destroy();
  cond.destroy();
}


}
}
