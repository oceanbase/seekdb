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

#ifndef _OB_WORK_QUEUE_H
#define _OB_WORK_QUEUE_H 1
#include "lib/thread/ob_async_task_queue.h"
#include "lib/task/ob_timer.h"

namespace oceanbase
{
namespace common
{
class ObWorkQueue;
// AsyncTimerTask do not execute in the timer thread, instead it submit an async task to do the real work.
// You should consider the following design choices:
// 1. whether the timer task is repeat
// 2. whether the async task need retry when failed
class ObAsyncTimerTask: public share::ObAsyncTask, public common::ObTimerTask
{
public:
  ObAsyncTimerTask(ObWorkQueue &work_queue)
      :work_queue_(work_queue)
  {
    set_retry_times(0);  // don't retry when process failed by default
  }
  virtual ~ObAsyncTimerTask() {}
  // interface of TimerTask
  virtual void runTimerTask() override;
  // interface of AsynTask
  virtual int process() override = 0;
  virtual int64_t get_deep_copy_size() const override = 0;
  virtual ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const override = 0;
private:
  // types and constants
private:
  // disallow copy
  DISALLOW_COPY_AND_ASSIGN(ObAsyncTimerTask);
  // function members
protected:
  // data members
  ObWorkQueue &work_queue_;
};

class ObWorkQueue
{
public:
  ObWorkQueue();
  virtual ~ObWorkQueue();

  // @note queue_size should be 2^n
  int init(const int64_t thread_count, const int64_t queue_size,
           const char *thread_name = nullptr);
  void destroy();

  // @note timer use task by reference
  int add_timer_task(ObAsyncTimerTask &task, const int64_t delay, bool did_repeat);
  int add_repeat_timer_task_schedule_immediately(ObAsyncTimerTask &task, const int64_t delay);
  bool exist_timer_task(const ObAsyncTimerTask &task);
  int cancel_timer_task(const ObAsyncTimerTask &task);

  int add_async_task(const share::ObAsyncTask &task);

  int start();
  int stop();
  int wait();
private:
  bool inited_;
  common::ObTimer timer_;
  share::ObAsyncTaskQueue task_queue_;
  DISALLOW_COPY_AND_ASSIGN(ObWorkQueue);
};

} // end namespace rootserver
} // end namespace oceanbase

#endif /* _OB_WORK_QUEUE_H */
