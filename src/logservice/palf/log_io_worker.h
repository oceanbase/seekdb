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

#ifndef OCEANBASE_LOGSERVIVE_LOG_IO_WORKER_
#define OCEANBASE_LOGSERVIVE_LOG_IO_WORKER_

#include <stdint.h>
#include "lib/lock/ob_spin_lock.h"
#include "lib/queue/ob_priority_queue.h"            // ObTLinkQueue16
#include "lib/utility/ob_macro_utils.h"             // DISALLOW_COPY_AND_ASSIGN
#include "lib/utility/ob_print_utils.h"             // TO_STRING_KV
#include "lib/container/ob_fixed_array.h"           // ObSEArrayy
#include "lib/hash/ob_array_hash_map.h"             // ObArrayHashMap
#include "lib/atomic/ob_atomic.h"                   // ATOMIC_LOAD
#include "share/ob_thread_pool.h"                   // ObThreadPool
#include "lib/time/ob_clock_generator.h"              // ObClockGenerator
#include "log_io_task.h"                            // LogBatchIOFlushLogTask
#include "share/log/palf/log_define.h"                             // PALF_SLIDING_WINDOW_SIZE
#include "share/log/palf/palf_options.h"                           // PalfThrottleOptions
#include "log_throttle.h"                           // LogWritingThrottle
namespace oceanbase
{
namespace common
{
class ObIAllocator;
}
namespace palf
{
class LogIOTask;
class LogIOTaskCbThreadPool;
class IPalfEnvImpl;

struct LogIOWorkerConfig
{
  LogIOWorkerConfig()
  {
    reset();
  }
  ~LogIOWorkerConfig()
  {
    reset();
  }
  bool is_valid() const
  {
    return 0 < io_queue_capcity_ && 0 < batch_depth_;
  }
  void reset()
  {
    io_queue_capcity_ = 0;
    batch_depth_ = 0;
  }
  int64_t io_queue_capcity_;
  int64_t batch_depth_;
  TO_STRING_KV(K_(io_queue_capcity), K_(batch_depth));
};

class LogIOWorker : public share::ObThreadPool
{
public:
  LogIOWorker();
  ~LogIOWorker();
  int init(const LogIOWorkerConfig &config,
           LogIOTaskCbThreadPool *cb_thread_pool,
           ObIAllocator *allocaotr,
           LogWritingThrottle *throttle,
           IPalfEnvImpl *palf_env_impl);
  void destroy();
  void stop() override;

  void run1() override final;
  int submit_io_task(LogIOTask *io_task);
  int64_t get_last_working_time() const { return ATOMIC_LOAD(&last_working_time_); }

  static constexpr int64_t MAX_THREAD_NUM = 1;
  TO_STRING_KV(KP_(cb_thread_pool));
private:
  bool is_stopping_() const;
  bool wait_before_retry_(int64_t &delay_us) const;
  bool need_reduce_(LogIOTask *task);
  int reduce_io_task_(ObLink *task);
  int handle_io_task_(LogIOTask *io_task);
  int handle_io_task_with_throttling_(LogIOTask *io_task);
  int update_throttling_options_();
  int run_loop_();
private:
  static constexpr int64_t QUEUE_WAIT_TIME = 100 * 1000;
private:

  class BatchLogIOFlushLogTaskMgr {
  public:
    BatchLogIOFlushLogTaskMgr();
    ~BatchLogIOFlushLogTaskMgr();
    int init(int64_t batch_depth, ObIAllocator *allocator, ObMiniStat::ObStatItem *wait_cost_stat);
    void destroy();
    int insert(LogIOFlushLogTask *io_task);
    int handle(LogIOTaskCbThreadPool *cb_thread_pool, IPalfEnvImpl *palf_env_impl, const LogIOWorker &worker);
    bool empty();
    TO_STRING_KV(K_(batch_io_task));
  private:
    BatchLogIOFlushLogTask batch_io_task_;
    int64_t handle_count_;
    ObMiniStat::ObStatItem *wait_cost_stat_;
  };

  // TODO: io_task_queue used to store all LogIOTask objects, and the LogIOWorker
  //       will consume it, at nowdays, the io_task_queue is single consumer and mutil
  //       producers model.

  common::ObSpinLock admission_lock_;
  bool accepting_tasks_;
  LogIOTaskCbThreadPool *cb_thread_pool_;
  IPalfEnvImpl *palf_env_impl_;
  ObTLinkQueue16 queue_;
  BatchLogIOFlushLogTaskMgr batch_io_task_mgr_;
  int64_t do_task_used_ts_;
  int64_t do_task_count_;
  int64_t print_log_interval_;
  int64_t last_working_time_;
  LogWritingThrottle *throttle_;
  ObMiniStat::ObStatItem log_io_worker_queue_size_stat_;
  ObMiniStat::ObStatItem wait_cost_stat_;
  bool is_inited_;
};
} // end namespace palf
} // end namespace oceanbase

#endif
