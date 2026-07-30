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

#ifndef OCEANBASE_SHARE_OB_ASYNC_TASK_QUEUE_H_
#define OCEANBASE_SHARE_OB_ASYNC_TASK_QUEUE_H_

#include "lib/queue/ob_lighty_queue.h"
#include "lib/allocator/ob_concurrent_fifo_allocator.h"
#include "lib/thread/ob_reentrant_thread.h"

namespace oceanbase
{
namespace share
{
class ObAsyncTask
{
public:
  ObAsyncTask() : retry_interval_(RETRY_INTERVAL), retry_times_(INFINITE_RETRY_TIMES),
      last_execute_time_(0) { }

  virtual ~ObAsyncTask() { }
  // if process fail, will push back to the queue and retry %retry_times_ times.
  virtual int process() = 0;
  virtual int64_t get_deep_copy_size() const = 0;
  virtual ObAsyncTask *deep_copy(char *buf, const int64_t buf_size) const = 0;
  inline int64_t get_retry_interval() const;
  virtual bool need_process(const int64_t switch_epoch) const { UNUSED(switch_epoch); return true; }
  virtual void set_is_retry(const bool is_retry) { UNUSED(is_retry); }
  inline int64_t get_retry_times() const;
  inline void set_retry_interval(const int64_t retry_interval);
  inline void set_retry_times(const int64_t retry_times);
  inline int64_t get_last_execute_time() const;
  inline void set_last_execute_time(const int64_t execute_time);
private:
  static const int64_t RETRY_INTERVAL = 1000 * 1000L;    // 1s
  static const int64_t INFINITE_RETRY_TIMES = INT64_MAX;
  int64_t retry_interval_;                               // us
  int64_t retry_times_;
  int64_t last_execute_time_;

  DISALLOW_COPY_AND_ASSIGN(ObAsyncTask);
};

inline int64_t ObAsyncTask::get_retry_interval() const
{
  return retry_interval_;
}

inline int64_t ObAsyncTask::get_retry_times() const
{
  return retry_times_;
}

inline void ObAsyncTask::set_retry_interval(const int64_t retry_interval)
{
  if (retry_interval < 0) {
    retry_interval_ = RETRY_INTERVAL;
  } else {
    retry_interval_ = retry_interval;
  }
}

inline void ObAsyncTask::set_retry_times(const int64_t retry_times)
{
  if (retry_times < 0) {
    retry_times_ = INFINITE_RETRY_TIMES;
  } else {
    retry_times_ = retry_times;
  }
}

inline int64_t ObAsyncTask::get_last_execute_time() const
{
  return last_execute_time_;
}

inline void ObAsyncTask::set_last_execute_time(const int64_t execute_time)
{
  last_execute_time_ = execute_time;
}

class ObAsyncTaskQueue : public ObReentrantThread
{
public:
  // if thread_cnt > 1, be sure the task can be processed in different order
  // with push order
  ObAsyncTaskQueue();
  virtual ~ObAsyncTaskQueue();
  //attention queue_size should be 2^n
  int init(const int64_t thread_cnt, const int64_t queue_size,
           const char *thread_name = nullptr, const int64_t page_size = ALLOC_PAGE_SIZE);
  int init_without_thread(
      const int64_t queue_size,
      const int64_t page_size = ALLOC_PAGE_SIZE);
  int start();
  void stop();
  void wait();
  int destroy();

  int push(const ObAsyncTask &task);
  // Process at most one task with an external worker. Business failures are
  // handled with the queue's existing retry policy and are not returned.
  int process_one_task(
      bool &processed,
      int64_t &next_ready_ts,
      bool &has_more_ready);
protected:
  static const int64_t TOTAL_LIMIT = 1024L * 1024L * 1024L;
  static const int64_t HOLD_LIMIT = 512L * 1024L * 1024L;
  static const int64_t ALLOC_PAGE_SIZE = common::OB_MALLOC_MIDDLE_BLOCK_SIZE;
  static const int64_t SLEEP_INTERVAL = 10000; //10ms
  virtual void run2();
  virtual int blocking_run() { BLOCKING_RUN_IMPLEMENT(); }
  int pop(ObAsyncTask *&task);
  int try_pop(ObAsyncTask *&task);
  virtual int64_t get_external_task_ready_ts_(
      const ObAsyncTask &task,
      const int64_t now) const;
  virtual bool can_retry_external_task_(const ObAsyncTask &task) const;
  virtual void on_external_task_processed_(
      ObAsyncTask &task,
      const int process_ret);
protected:
  bool is_inited_;
  bool use_external_driver_;
  common::ObLightyQueue queue_;
  common::ObConcurrentFIFOAllocator allocator_;
  ObAsyncTask *external_pending_task_;
private:
  int init_(
      const int64_t thread_cnt,
      const int64_t queue_size,
      const char *thread_name,
      const int64_t page_size,
      const bool create_thread);
  void clear_external_tasks_();
  DISALLOW_COPY_AND_ASSIGN(ObAsyncTaskQueue);
};
}//end namespace share
}//end namespace oceanbase
#endif
