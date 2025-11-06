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

#include "lib/thread/ob_work_queue.h"
#include <gtest/gtest.h>
using namespace oceanbase::common;
class TestWorkQueue: public ::testing::Test
{
public:
  TestWorkQueue();
  virtual ~TestWorkQueue();
  virtual void SetUp();
  virtual void TearDown();
  static void SetUpTestCase();
  static void TearDownTestCase();
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(TestWorkQueue);
protected:
  // function members
protected:
  // data members
};

TestWorkQueue::TestWorkQueue()
{
}

TestWorkQueue::~TestWorkQueue()
{
}

void TestWorkQueue::SetUp()
{
}

void TestWorkQueue::TearDown()
{
}

void TestWorkQueue::SetUpTestCase()
{
  ASSERT_EQ(OB_SUCCESS, ObTimerService::get_instance().start());
}

void TestWorkQueue::TearDownTestCase()
{
  ObTimerService::get_instance().stop();
  ObTimerService::get_instance().wait();
  ObTimerService::get_instance().destroy();
}

TEST_F(TestWorkQueue, init)
{
  ObWorkQueue wqueue;
  ASSERT_EQ(OB_INVALID_ARGUMENT, wqueue.init(4, 1000));
  ASSERT_EQ(OB_SUCCESS, wqueue.init(4, 1024));
  ASSERT_EQ(OB_INIT_TWICE, wqueue.init(4, 1024));
  ASSERT_EQ(OB_SUCCESS, wqueue.start());
  ASSERT_EQ(OB_SUCCESS, wqueue.stop());
  ASSERT_EQ(OB_SUCCESS, wqueue.wait());
}

class ATimerTask: public ObAsyncTimerTask
{
public:
  ATimerTask(ObWorkQueue &queue, bool fail_it = false)
      :ObAsyncTimerTask(queue),
       fail_it_(fail_it)
  {
    set_retry_interval(0);
    set_retry_times(1);
  }
  virtual ~ATimerTask()
  {}
  virtual int process() override
  {
    int ret = OB_SUCCESS;
    ATOMIC_INC(&process_count_);
    if (fail_it_) {
      OB_LOG(INFO, "test fail and retry", K_(process_count));
      ret = OB_INVALID_ARGUMENT;
      fail_it_ = false;
    } else {
      OB_LOG(INFO, "process", K_(process_count));
    }
    return ret;
  }
  virtual int64_t get_deep_copy_size() const override
  {
    return sizeof(*this);
  }
  virtual ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const override
  {
    int ret = 0;
    ObAsyncTask *task = NULL;
    if (buf == NULL || buf_size < sizeof(*this)) {
      OB_LOG(ERROR, "invalid argument");
    } else {
      task = new(buf) ATimerTask(work_queue_, fail_it_);
    }
    return task;
  }
  static int64_t get_process_count() { return process_count_; }
  static void clear_process_count() { process_count_ = 0; }
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ATimerTask);
  // function members
private:
  // data members
  static int64_t process_count_;
  bool fail_it_;
};
int64_t ATimerTask::process_count_ = 0;

TEST_F(TestWorkQueue, async_task)
{
  ObWorkQueue wqueue;
  ASSERT_EQ(OB_INVALID_ARGUMENT, wqueue.init(4, 1000));
  ASSERT_EQ(OB_SUCCESS, wqueue.init(4, 1024));
  ASSERT_EQ(OB_INIT_TWICE, wqueue.init(4, 1024));
  ASSERT_EQ(OB_SUCCESS, wqueue.start());
  ATimerTask task1(wqueue);
  ATimerTask::clear_process_count();
  for (int64_t i = 0; i < 16; ++i)
  {
    ASSERT_EQ(OB_SUCCESS, wqueue.add_async_task(task1));
  }
  sleep(3);
  ASSERT_EQ(16, task1.get_process_count());

  ASSERT_EQ(OB_SUCCESS, wqueue.stop());
  ASSERT_EQ(OB_SUCCESS, wqueue.wait());
}

TEST_F(TestWorkQueue, on_shoot_timer_task)
{
  ObWorkQueue wqueue;
  ASSERT_EQ(OB_INVALID_ARGUMENT, wqueue.init(4, 1000));
  ASSERT_EQ(OB_SUCCESS, wqueue.init(4, 1024));
  ASSERT_EQ(OB_INIT_TWICE, wqueue.init(4, 1024));
  ASSERT_EQ(OB_SUCCESS, wqueue.start());
  ATimerTask task1(wqueue);
  ATimerTask::clear_process_count();
  for (int64_t i = 0; i < 16; ++i)
  {
    ASSERT_EQ(OB_SUCCESS, wqueue.add_timer_task(task1, 2*1000*1000, false));
  }
  OB_LOG(INFO, "before sleep 1");
  sleep(1);
  OB_LOG(INFO, "after sleep 1");
  ASSERT_EQ(0, task1.get_process_count());
  OB_LOG(INFO, "before sleep 2");
  sleep(2);
  OB_LOG(INFO, "after sleep 2");
  ASSERT_EQ(16, task1.get_process_count());

  ASSERT_EQ(OB_SUCCESS, wqueue.stop());
  ASSERT_EQ(OB_SUCCESS, wqueue.wait());
}

TEST_F(TestWorkQueue, repeat_timer_task)
{
  ObWorkQueue wqueue;
  ASSERT_EQ(OB_INVALID_ARGUMENT, wqueue.init(4, 1000));
  ASSERT_EQ(OB_SUCCESS, wqueue.init(4, 1024));
  ASSERT_EQ(OB_INIT_TWICE, wqueue.init(4, 1024));
  ASSERT_EQ(OB_SUCCESS, wqueue.start());
  ATimerTask task1(wqueue);
  ATimerTask::clear_process_count();
  for (int64_t i = 0; i < 16; ++i)
  {
    ASSERT_EQ(OB_SUCCESS, wqueue.add_timer_task(task1, 2*1000*1000, true));
  }
  OB_LOG(INFO, "before sleep 1");
  sleep(1);
  OB_LOG(INFO, "after sleep 1");
  ASSERT_EQ(0, task1.get_process_count());
  OB_LOG(INFO, "before sleep 2");
  sleep(2);
  OB_LOG(INFO, "after sleep 2");
  ASSERT_EQ(16, task1.get_process_count());
  sleep(2);
  OB_LOG(INFO, "sleep 2");
  ASSERT_EQ(32, task1.get_process_count());

  ASSERT_EQ(OB_SUCCESS, wqueue.stop());
  ASSERT_EQ(OB_SUCCESS, wqueue.wait());
}

TEST_F(TestWorkQueue, retry_task)
{
  ObWorkQueue wqueue;
  ASSERT_EQ(OB_INVALID_ARGUMENT, wqueue.init(4, 1000));
  ASSERT_EQ(OB_SUCCESS, wqueue.init(4, 1024));
  ASSERT_EQ(OB_INIT_TWICE, wqueue.init(4, 1024));
  ASSERT_EQ(OB_SUCCESS, wqueue.start());
  ATimerTask task1(wqueue, true);
  ATimerTask::clear_process_count();
  for (int64_t i = 0; i < 16; ++i)
  {
    ASSERT_EQ(OB_SUCCESS, wqueue.add_timer_task(task1, 2*1000*1000, false));
  }
  OB_LOG(INFO, "before sleep 1");
  sleep(1);
  OB_LOG(INFO, "after sleep 1");
  ASSERT_EQ(0, task1.get_process_count());
  OB_LOG(INFO, "before sleep 2");
  sleep(2);
  OB_LOG(INFO, "after sleep 2");
  ASSERT_EQ(32, task1.get_process_count());

  ASSERT_EQ(OB_SUCCESS, wqueue.stop());
  ASSERT_EQ(OB_SUCCESS, wqueue.wait());
}

TEST_F(TestWorkQueue, immediate_task)
{
  ObWorkQueue wqueue;
  ASSERT_EQ(OB_INVALID_ARGUMENT, wqueue.init(4, 1000));
  ASSERT_EQ(OB_SUCCESS, wqueue.init(4, 1024));
  ASSERT_EQ(OB_INIT_TWICE, wqueue.init(4, 1024));
  ASSERT_EQ(OB_SUCCESS, wqueue.start());
  ATimerTask task1(wqueue);
  ATimerTask::clear_process_count();
  for (int64_t i = 0; i < 16; ++i)
  {
    ASSERT_EQ(OB_SUCCESS, wqueue.add_repeat_timer_task_schedule_immediately(task1, 2*1000*1000));
  }
  OB_LOG(INFO, "before sleep 1");
  sleep(1);
  OB_LOG(INFO, "after sleep 1");
  ASSERT_EQ(16, task1.get_process_count());
  OB_LOG(INFO, "before sleep 2");
  sleep(2);
  OB_LOG(INFO, "after sleep 2");
  ASSERT_EQ(32, task1.get_process_count());

  ASSERT_EQ(OB_SUCCESS, wqueue.stop());
  ASSERT_EQ(OB_SUCCESS, wqueue.wait());
}

int main(int argc, char **argv)
{
  system("rm -rf test_work_queue.log");
  OB_LOGGER.set_log_level("INFO");
  OB_LOGGER.set_file_name("test_work_queue.log", true);
  ::testing::InitGoogleTest(&argc,argv);
  return RUN_ALL_TESTS();
}
