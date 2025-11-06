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

#pragma once

#include "lib/lock/ob_mutex.h"
#include "lib/lock/ob_thread_cond.h"
#include "lib/profile/ob_trace_id.h"
#include "lib/queue/ob_lighty_queue.h"
#include "share/ob_thread_pool.h"
#include "lib/allocator/page_arena.h"
#include "lib/oblog/ob_warning_buffer.h"

namespace oceanbase
{
namespace observer
{
class ObTableLoadTask;

class ObITableLoadTaskScheduler
{
public:
  ObITableLoadTaskScheduler() = default;
  virtual ~ObITableLoadTaskScheduler() = default;
  virtual int init() = 0;
  virtual int start() = 0;
  virtual void stop() = 0;
  virtual void wait() = 0;
  virtual int add_task(int64_t thread_idx, ObTableLoadTask *task) = 0;
  virtual int64_t get_thread_count() const = 0;
  virtual bool is_stopped() const = 0;
private:
  DISALLOW_COPY_AND_ASSIGN(ObITableLoadTaskScheduler);
};

class ObTableLoadTaskThreadPoolScheduler final : public ObITableLoadTaskScheduler
{
  static const int64_t DEFAULT_TIMEOUT_US = 10LL * 1000 * 1000; // 10s
  // Running status
  static const int STATE_ZERO = 0;
  static const int STATE_STARTING = 1;
  static const int STATE_RUNNING = 2;
  static const int STATE_STOPPING = 3;
  static const int STATE_STOPPED = 4;
  static const int STATE_STOPPED_NO_WAIT = 5;
public:
  ObTableLoadTaskThreadPoolScheduler(int64_t thread_count,
                                     uint64_t table_id,
                                     const char *label,
                                     sql::ObSQLSessionInfo *session_info,
                                     int64_t session_queue_size = 64);
  virtual ~ObTableLoadTaskThreadPoolScheduler();
  int init() override;
  int start() override;
  void stop() override;
  void wait() override;
  int add_task(int64_t thread_idx, ObTableLoadTask *task) override;
  int64_t get_thread_count() const override { return thread_count_; }
  bool is_stopped() const override
  {
    return state_ == STATE_STOPPED || state_ == STATE_STOPPED_NO_WAIT;
  }
private:
  void run(uint64_t thread_idx);
  int init_worker_ctx_array();
  OB_INLINE bool is_running() const
  {
    return state_ == STATE_RUNNING;
  }
  // Will be called only if startup is successful
  void before_running();
  // Startup failure may also call
  void after_running();
  void clear_all_task();
private:
  class MyThreadPool : public share::ObThreadPool
  {
  public:
    MyThreadPool(ObTableLoadTaskThreadPoolScheduler *scheduler)
      : scheduler_(scheduler), running_thread_count_(0) {}
    int init();
    virtual ~MyThreadPool() = default;
    void run1() override;
  private:
    ObTableLoadTaskThreadPoolScheduler * const scheduler_;
    int64_t running_thread_count_ CACHE_ALIGNED;
    common::ObThreadCond pool_cond_;
  };
  struct WorkerContext
  {
    WorkerContext() : need_signal_(false) {}
    int64_t worker_id_;
    common::ObThreadCond cond_;
    bool need_signal_;
    common::LightyQueue task_queue_; // thread-safe
  };
  int execute_worker_tasks(WorkerContext &worker_ctx);
private:
  common::ObArenaAllocator allocator_;
  const int64_t thread_count_;
  const int64_t session_queue_size_;
  sql::ObSQLSessionInfo *session_info_;
  char name_[OB_THREAD_NAME_BUF_LEN];
  common::ObCurTraceId::TraceId trace_id_;
  int64_t timeout_ts_;
  MyThreadPool thread_pool_;
  WorkerContext *worker_ctx_array_;
  volatile int state_;
  bool is_inited_;
  lib::ObMutex state_mutex_;
  common::ObWarningBuffer warning_buffer_;
  lib::ObMutex wb_mutex_;
};

}  // namespace observer
}  // namespace oceanbase
