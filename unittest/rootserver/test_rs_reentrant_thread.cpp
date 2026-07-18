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

#define USING_LOG_PREFIX RS

#include "gmock/gmock.h"
#define private public
#include "share/ob_cluster_version.h"
#include "rootserver/ob_rs_reentrant_thread.h"

using ::testing::_;
using ::testing::Invoke;
using ::testing::Return;

namespace oceanbase
{
namespace rootserver
{
using namespace oceanbase::common;
using namespace oceanbase::share;

class ObTestThread : public ObRsReentrantThread
{
public: 
  ObTestThread() : ObRsReentrantThread(true) {}
  virtual int blocking_run() override { BLOCKING_RUN_IMPLEMENT(); }

  virtual void run3() override
  {
    while (!has_set_stop()) {
      ::usleep(10000);
    }
  }
};

class TestRsReentrantThread : public ::testing::Test
{
public:
  TestRsReentrantThread() {}
  virtual ~TestRsReentrantThread() {}
  virtual void SetUp() {}
  virtual void TearDown() {}

  int start_thread(int64_t thread_num)
  {
    int ret = OB_SUCCESS;
    threads_.reset();

    for (int64_t i = 0; (i < thread_num) && OB_SUCC(ret); ++i) {
      ObTestThread *thread = new ObTestThread();
      if (OB_FAIL(thread->create(1, "test"))) {
        LOG_WARN("fail to create thread", KR(ret));
      } else if (OB_FAIL(thread->start())) {
        LOG_WARN("fail to start thread", KR(ret));
      } else if (OB_FAIL(threads_.push_back(thread))) {
        LOG_WARN("fail to push back", KR(ret));
      } 
    }

    return ret;
  }

  int stop_thread()
  {
    int ret = OB_SUCCESS;
    for (int64_t i = 0; (i < threads_.count()) && OB_SUCC(ret); ++i) {
      threads_[i]->stop();
      threads_[i]->wait();
      if (OB_FAIL(threads_[i]->destroy())) {
        LOG_WARN("fail to destroy");
      }
      delete threads_[i];
    }
    threads_.reset();
    return ret;
  }
  
  ObSEArray<ObTestThread *, 17> threads_;
};

} // namespace rootserver
} // namespace oceanbase

int main(int argc, char **argv)
{
  oceanbase::common::ObLogger::get_logger().set_log_level("INFO");
  OB_LOGGER.set_log_level("INFO");
  testing::InitGoogleTest(&argc, argv);
  oceanbase::common::ObClusterVersion::get_instance().init(CLUSTER_VERSION_1_0_0_0);
  return RUN_ALL_TESTS();
}
