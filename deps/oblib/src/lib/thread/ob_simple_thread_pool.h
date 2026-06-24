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

#ifndef OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_
#define OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_

#include "lib/queue/ob_lighty_queue.h"
#include "lib/queue/ob_priority_queue.h"
#include "lib/thread/thread_pool.h"
#include "lib/thread/ob_dynamic_thread_pool.h"
#include "lib/thread/ob_adaptive_worker_pool.h"
#include "lib/list/ob_dlist.h"
#include "lib/lock/ob_mutex.h"
#include "lib/thread/threads.h"
#include "common/ob_queue_thread.h"

namespace oceanbase
{
namespace common
{
template <class T = ObLightyQueue>
struct QueueTypeMap {
  using TaskType = void;
  using QElemType = void;
};

template <>
struct QueueTypeMap<ObTLinkQueue16> {
  using TaskType = LinkTask;
  using QElemType = ObLink;
};

template <class T = ObLightyQueue>
class ObSimpleThreadPoolBase
    : public ObSimpleDynamicThreadPool
    , public lib::ObAdaptiveWorkerPool<ObSimpleThreadPoolBase<T>>
{
  using TaskType = typename QueueTypeMap<T>::TaskType;
  using QElemType = typename QueueTypeMap<T>::QElemType;
  static const int64_t QUEUE_WAIT_TIME = 1000 * 1000;
  static const int64_t SHRINK_TIMEOUT_US = 1 * 1000 * 1000;

  struct Worker;
  using WorkerNode = common::ObDLinkNode<Worker *>;
  using WorkerList = common::ObDList<WorkerNode>;

  struct Worker : public lib::Threads {
    ObSimpleThreadPoolBase *pool_;
    int64_t idx_;
    WorkerNode worker_node_;
    Worker(ObSimpleThreadPoolBase *pool, int64_t idx)
        : lib::Threads(1), pool_(pool), idx_(idx) {
      worker_node_.get_data() = this;
    }
    void run1() override;
  };
  friend struct Worker;

public:
  ObSimpleThreadPoolBase();
  virtual ~ObSimpleThreadPoolBase();

  int init(const int64_t thread_num, const int64_t task_num_limit,
           const char *name = "unknown");
  void destroy();
  int push(TaskType *task);
  virtual int64_t get_queue_num() const override { return queue_.size(); }
  virtual void notify_stop() override { queue_.wake_all(); }
  int64_t worker_count() const override {
    return lib::ObAdaptiveWorkerPool<ObSimpleThreadPoolBase<T>>::worker_count();
  }

  // Pool-level control
  void stop();
  void wait();
  bool has_set_stop() const { return stop_; }

  // RunWrapper
  void set_run_wrapper(lib::IRunWrapper *rw) { run_wrapper_ = rw; }
  lib::IRunWrapper *get_run_wrapper() const { return run_wrapper_; }

  // Thread count queries (TG handler compatibility)
  int64_t get_thread_count() const { return worker_count(); }
  uint64_t get_thread_idx() const { return cur_worker_idx_; }

  // Thread count configuration
  int set_thread_count(int64_t n_threads);
  int set_adaptive_thread(int64_t min_thread_num, int64_t max_thread_num);

  // CRTP for lib::ObAdaptiveWorkerPool
  bool do_add_worker();
  int64_t queue_size() const { return queue_.size(); }

  // Reap stopped workers (called by Mgr background thread only)
  void reap_workers() override;

protected:
  virtual void handle(TaskType *task) = 0;
  virtual void handle_drop(TaskType *task) {
    handle(task);
  }
  virtual void run1();

private:
  bool is_inited_;
  bool stop_;
  T queue_;
  lib::IRunWrapper *run_wrapper_;
  WorkerList workers_;
  lib::ObMutex workers_lock_;
  uint64_t cur_worker_idx_;
};

using ObSimpleThreadPool = ObSimpleThreadPoolBase<ObLightyQueue>;
using ObLinkQueueThreadPool = ObSimpleThreadPoolBase<ObTLinkQueue16>;

} // namespace common
} // namespace oceanbase

#include "ob_simple_thread_pool.ipp"

#endif // OCEANBASE_COMMON_OB_SIMPLE_THREAD_POOL_H_
