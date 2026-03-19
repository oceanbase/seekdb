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
#ifndef OCEANBASE_LOG_FETCHER_TIMER_H__
#define OCEANBASE_LOG_FETCHER_TIMER_H__

#include "lib/queue/ob_fixed_queue.h"           // ObFixedQueue
#include "common/ob_queue_thread.h"             // ObCond
#include "lib/allocator/ob_small_allocator.h"   // ObSmallAllocator
#include "ob_log_utils.h"                       // _SEC_

namespace oceanbase
{
namespace logfetcher
{
class ObLogTimerTask
{
public:
  virtual ~ObLogTimerTask() {}

public:
  virtual void process_timer_task() = 0;
};

class IObLogErrHandler;
class ObLogFetcherConfig;

// Timer class
class ObLogFixedTimer
{
  static const int64_t STAT_INTERVAL = 30 * _SEC_;
  static const int64_t COND_WAIT_TIME = 1 * _SEC_;

public:
  // Timer task waiting time
  static int64_t g_wait_time;

public:
  ObLogFixedTimer();
  virtual ~ObLogFixedTimer();

public:
  int init(IObLogErrHandler &err_handler, const int64_t max_task_count);
  void destroy();

  int start();
  void stop();
  void mark_stop_flag();

public:
  int schedule(ObLogTimerTask *task);

public:
  static void configure(const ObLogFetcherConfig &config);

public:
  void run();

private:
  static void *thread_func_(void *args);
  struct QTask
  {
    int64_t         out_timestamp_;   // timestamp of out
    ObLogTimerTask  &task_;           // Actual timer tasks

    explicit QTask(ObLogTimerTask &task);
  };

  typedef common::ObFixedQueue<QTask> TaskQueue;

private:
  void destroy_all_tasks_();
  QTask *alloc_queue_task_(ObLogTimerTask &timer_task);
  int push_queue_task_(QTask &task);
  void free_queue_task_(QTask *task);
  int next_queue_task_(QTask *&task);

private:
  bool                      inited_;
  pthread_t                 tid_;           // Timer thread ID
  IObLogErrHandler          *err_handler_;  // err handler
  TaskQueue                 task_queue_;    // task queue
  common::ObCond            task_cond_;
  common::ObSmallAllocator  allocator_;

  volatile bool stop_flag_ CACHE_ALIGNED;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLogFixedTimer);
};

}
}

#endif
