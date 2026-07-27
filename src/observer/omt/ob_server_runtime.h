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

#ifndef OCEANBASE_OBSERVER_OMT_OB_SERVER_RUNTIME_H_
#define OCEANBASE_OBSERVER_OMT_OB_SERVER_RUNTIME_H_

#include <stdint.h>
#include <cmath>
#include "lib/time/ob_time_utility.h"
#include "lib/list/ob_dlist.h"
#include "lib/queue/ob_priority_queue.h"
#include "lib/queue/ob_fixed_queue.h"
#include "lib/lock/ob_spin_lock.h"
#include "lib/lock/ob_mutex.h"
#include "lib/atomic/ob_atomic.h"
#include "lib/thread/ob_thread_name.h"
#include "lib/rc/ob_rc.h"
#include "rpc/ob_request.h"
#include "share/system_variable/ob_sys_var_class_type.h"
#include "share/ob_thread_pool.h"
#include "share/rc/ob_server_runtime.h"
#include "share/rc/ob_context.h"
#include "observer/omt/ob_th_worker.h"
#include "ob_retry_queue.h"
#include "observer/omt/ob_server_runtime_meta.h"
#include "lib/thread/ob_adaptive_worker_pool.h"
#include "lib/lock/ob_tc_rwlock.h"      // TCRWLock

namespace oceanbase
{
namespace observer
{
class ObAllVirtualDumpInfo;
}
namespace omt
{
typedef common::ObPriorityQueue2<1, QQ_MAX_PRIO - 1, RQ_MAX_PRIO - QQ_MAX_PRIO> ReqQueue;
class ObPxPool
    : public share::ObThreadPool
{
  using RunFuncT = std::function<void (bool)>;
  void run(int64_t idx) final;
  void run1() final;
  static const int64_t QUEUE_WAIT_TIME = 100 * 1000;

public:
	class Task;
  ObPxPool() :
      group_id_(0),
      is_inited_(false),
      concurrency_(0),
      active_threads_(0)
  {}
  virtual void stop();

  void set_group_id(uint64_t group_id)
  {
    group_id_ = group_id;
  }
  int64_t get_pool_size() const { return get_thread_count(); }
  int submit(const RunFuncT &func);
  void set_px_thread_name();
  int64_t get_queue_size() const { return queue_.size(); }
private:
  void handle(common::ObLink *task);
  void try_recycle(int64_t idle_time);
  void disable_recycle()
  {
    recycle_lock_.lock();
  }
  void enable_recycle()
  {
    IGNORE_RETURN recycle_lock_.unlock();
  }
private:
  uint64_t group_id_;
	common::ObPriorityQueue2<0, 1> queue_;
  bool is_inited_;
  int64_t concurrency_;
  int64_t active_threads_;
  mutable common::ObSpinLock recycle_lock_;
};

class ObPxPool::Task : public common::ObLink
{
public:
  Task(const RunFuncT &func)
      : func_(func)
  {}
public:
  RunFuncT func_;
};

class ObPxPools
{
public:
  class StopPoolFunc
  {
  public:
    StopPoolFunc() {}
    virtual ~StopPoolFunc() = default;
    int operator()(common::hash::HashMapPair<int64_t, ObPxPool*> &kv);
  };
  class DeletePoolFunc
  {
  public:
    DeletePoolFunc() {}
    virtual ~DeletePoolFunc() = default;
    int operator()(common::hash::HashMapPair<int64_t, ObPxPool*> &kv);
  };
  class ThreadRecyclePoolFunc
  {
  public:
    ThreadRecyclePoolFunc() {}
    virtual ~ThreadRecyclePoolFunc() = default;
    int operator()(common::hash::HashMapPair<int64_t, ObPxPool*> &kv);
  };
public:
  static int server_module_init(ObPxPools *&pools)
  {
    int ret = common::OB_SUCCESS;

    if (OB_FAIL(pools->init())) {
    }
    return ret;
  }
  static void server_module_stop(ObPxPools *&pools);
  static void server_module_destroy(ObPxPools *&pools)
  {
    common::ob_delete(pools);
    pools = nullptr;
  }
public:
  ObPxPools() {}
  ~ObPxPools()
  {
    destroy();
  }
  int init();
  int get_or_create(int64_t group_id, ObPxPool *&pool);
  int thread_recycle();
private:
  void destroy();
  int create_pool(int64_t group_id, ObPxPool *&pool);
private:
  common::SpinRWLock lock_;
  common::hash::ObHashMap<int64_t, ObPxPool *> pool_map_;
};

// Forward declarations
class ObThWorker;

// Type aliases
typedef common::ObDLinkNode<ObThWorker*> WorkerNode;
typedef common::ObDList<WorkerNode> WorkerList;

//================================= ObServerRuntime ====================================//
// Except for get_new_request wakeup_paused_worker recv_request, all
// other functions aren't thread safe.
class ObServerRuntime : public share::ObServerRuntimeState,
                 public lib::ObAdaptiveWorkerPool<ObServerRuntime>
{
  friend class observer::ObAllVirtualDumpInfo;
  friend int create_worker(ObThWorker* &worker, ObServerRuntime *runtime);
  friend int destroy_worker(ObThWorker *worker);
  friend class ObThWorker;
  using WListNode = common::ObDLinkNode<lib::Worker*>;
  using WList = common::ObDList<WListNode>;

public:
  static constexpr int64_t KEEP_ALIVE_TIMEOUT = 10 * 1000 * 1000L;  // 10s

  ObServerRuntime();
  virtual ~ObServerRuntime();

  ObServerRuntime(const ObServerRuntime &) = delete;
  ObServerRuntime &operator=(const ObServerRuntime &) = delete;

  int init(const ObServerRuntimeMeta &meta);
  void stop() { ATOMIC_STORE(&stopped_, ObTimeUtility::current_time()); }
  void start() { ATOMIC_STORE(&stopped_, 0); }
  int try_wait();
  void destroy();
  bool has_stopped() const { return stopped_ != 0; }

  ObServerRuntimeMeta get_runtime_meta();
  bool is_hidden();
  void set_create_status(const storage::ObServerRuntimeCreateStatus status);

  int create_modules();

  share::ObServerRuntimeConfig get_runtime_config();
  storage::ObServerRuntimeSuperBlock get_super_block();
  void set_server_resources(const share::ObServerRuntimeConfig &runtime_config);
  void set_server_super_block(const storage::ObServerRuntimeSuperBlock &super_block);

  void set_max_cpu(double cpu);
  void set_min_cpu(double cpu);
  int64_t cpu_quota_concurrency() const;
  int64_t min_worker_cnt() const;
  int64_t max_worker_cnt() const;
  int64_t cur_ddl_thread_count() {return ATOMIC_LOAD(&total_ddl_thread_cnt_);}
  void inc_ddl_thread_count() { ATOMIC_INC(&total_ddl_thread_cnt_); };
  void dec_ddl_thread_count() { ATOMIC_DEC(&total_ddl_thread_cnt_); };
  bool check_ddl_thread_is_limit(const int64_t cpu_quota_concurrency) { return ATOMIC_LOAD(&total_ddl_thread_cnt_) >= static_cast<int64_t>(min_cpu() * cpu_quota_concurrency); }
  int rdlock();
  int wrlock();
  int try_rdlock();
  int try_wrlock();
  int unlock();
  virtual void on_schema_publish() override;

  // get request from request queue, waiting at most TIMEOUT us.
  // if IN_HIGH_PRIORITY is set, get request from hp queue.
  int get_new_request(int64_t timeout, rpc::ObRequest *&req);

  // receive request from network
  int recv_request(rpc::ObRequest &req);
  int push_retry_queue(rpc::ObRequest &req, const uint64_t idx);
  void handle_retry_req(bool need_clear = false);
  void set_queue_limit(int64_t limit) { req_queue_.set_limit(limit); }

  int timeup();

  TO_STRING_KV("id", id(),
               K_(runtime_meta),
               K_(min_cpu), K_(max_cpu),
               "total_worker_cnt", worker_count(),
               "idle_worker_cnt", idle_count(),
               "min_worker_cnt", min_worker_cnt(),
               "max_worker_cnt", max_worker_cnt(),
               K_(stopped),
               K_(recv_mysql_cnt), K_(recv_task_cnt),
               "workers", workers_.get_size(),
               K_(req_queue),
               "server_role", role())
public:
  void periodically_check();
  ReqQueue& get_req_queue() { return req_queue_; }
  int acquire_more_worker(int64_t num, int64_t &succ_num, bool force = false);
  bool do_add_worker();
  int64_t queue_size() const { return req_queue_.size(); }
private:
  static void sleep_and_warn(ObServerRuntime* runtime);
  static void* wait(void* runtime);
  // acquire workers if runtime doesn't have sufficient worker.
  void check_worker_count();

  OB_INLINE int pop_req(common::ObLink *&req, int64_t timeout) { return req_queue_.pop(req, timeout); }

  // read runtime variable PARALLEL_SERVERS_TARGET
  void check_parallel_servers_target();
  void check_px_thread_recycle();
  // clean buffer on time
  void check_dtl();

  int construct_module_init_ctx(const ObServerRuntimeMeta &meta, share::ObServerModuleInitCtx *&ctx);

protected:

  mutable common::TCRWLock meta_lock_;
  ObServerRuntimeMeta runtime_meta_;

protected:
  // number of active workers the runtime has owned. Only active
  // workers can make progress.
  int64_t total_ddl_thread_cnt_;
  void *gc_thread_;
  bool has_created_;
  int64_t stopped_;
  bool modules_stopped_;

  /// runtime task queue,
  // 'hp' for high priority and 'np' for normal priority
  ReqQueue req_queue_;

  //Create a timer queue group for retry requests
  ObRetryQueue retry_queue_;

  volatile uint64_t recv_mysql_cnt_;
  volatile uint64_t recv_task_cnt_;

public:
  common::ObLatch lock_;

  WList workers_;
  lib::ObMutex workers_lock_;

  std::atomic<int64_t> completion_cnt_;

}; // end of class ObServerRuntime

inline int ObServerRuntime::rdlock()
{
  return lock_.rdlock(common::ObLatchIds::SERVER_RUNTIME_LOCK) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObServerRuntime::wrlock()
{
  uint32_t puid = static_cast<uint32_t>(GETTID());
  return lock_.wrlock(common::ObLatchIds::SERVER_RUNTIME_LOCK, INT64_MAX, &puid) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObServerRuntime::try_rdlock()
{
  return lock_.try_rdlock(common::ObLatchIds::SERVER_RUNTIME_LOCK) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObServerRuntime::try_wrlock()
{
  uint32_t puid = static_cast<uint32_t>(GETTID());
  return lock_.try_wrlock(common::ObLatchIds::SERVER_RUNTIME_LOCK, &puid) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObServerRuntime::unlock()
{
  uint32_t puid = static_cast<uint32_t>(GETTID());
  return lock_.unlock(&puid) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

} // end of namespace omt
} // end of namespace oceanbase

#endif /* OCEANBASE_OBSERVER_OMT_OB_SERVER_RUNTIME_H_ */
