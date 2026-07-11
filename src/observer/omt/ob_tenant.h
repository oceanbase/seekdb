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

#ifndef _OCEABASE_OBSERVER_OMT_OB_TENANT_H_
#define _OCEABASE_OBSERVER_OMT_OB_TENANT_H_

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
#include "share/rc/ob_tenant_base.h"
#include "share/rc/ob_context.h"
#include "observer/omt/ob_th_worker.h"
#include "ob_retry_queue.h"
#include "lib/utility/ob_query_rate_limiter.h"
#include "share/resource_manager/ob_cgroup_ctrl.h"
#include "observer/omt/ob_tenant_meta.h"
#include "lib/thread/ob_adaptive_worker_pool.h"
#include "lib/lock/ob_tc_rwlock.h"      // TCRWLock

namespace oceanbase
{
namespace observer
{
class ObAllVirtualDumpTenantInfo;
}
namespace omt
{
typedef common::ObPriorityQueue2<1, QQ_MAX_PRIO - 1, RQ_MAX_PRIO - QQ_MAX_PRIO, OB_MAX_NUMA_NUM> ReqQueue;
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
  static int mtl_init(ObPxPools *&pools)
  {
    int ret = common::OB_SUCCESS;
    
    if (OB_FAIL(pools->init())) {
    }
    return ret;
  }
  static void mtl_stop(ObPxPools *&pools);
  static void mtl_destroy(ObPxPools *&pools)
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

struct ObSqlThrottleMetrics
{
  int64_t priority_;
  double rt_;
  double cpu_;
  int64_t io_;
  double network_;
  int64_t logical_reads_;
  double queue_time_;

  ObSqlThrottleMetrics()
      : priority_(-1),
        rt_(-1),
        cpu_(-1),
        io_(-1),
        network_(-1),
        logical_reads_(-1),
        queue_time_(-1)
  {}

  TO_STRING_KV(
    K_(priority),
    K_(rt),
    K_(cpu),
    K_(io),
    K_(network),
    K_(logical_reads),
    K_(queue_time));
};

// Forward declarations
class ObThWorker;

// Type aliases
typedef common::ObDLinkNode<ObThWorker*> WorkerNode;
typedef common::ObDList<WorkerNode> WorkerList;

//================================= ObTenant ====================================//
// Except for get_new_request wakeup_paused_worker recv_request, all
// other functions aren't thread safe.
class ObTenant : public share::ObTenantBase,
                 public lib::ObAdaptiveWorkerPool<ObTenant>
{
  friend class observer::ObAllVirtualDumpTenantInfo;
  friend int create_worker(ObThWorker* &worker, ObTenant *tenant);
  friend int destroy_worker(ObThWorker *worker);
  friend class ObThWorker;
  using WListNode = common::ObDLinkNode<lib::Worker*>;
  using WList = common::ObDList<WListNode>;

public:
  static constexpr int64_t KEEP_ALIVE_TIMEOUT = 10 * 1000 * 1000L;  // 10s

  ObTenant(const int64_t epoch,
           const int64_t times_of_workers,
           share::ObCgroupCtrl &cgroup_ctrl,
           const bool embedded = false);
  virtual ~ObTenant();

  ObTenant(const ObTenant &) = delete;
  ObTenant &operator=(const ObTenant &) = delete;

  int init_ctx();
  int init(const ObTenantMeta &meta);
  void stop() { ATOMIC_STORE(&stopped_, ObTimeUtility::current_time()); }
  void start() { ATOMIC_STORE(&stopped_, 0); }
  int try_wait();
  void destroy();
  bool has_stopped() const { return stopped_ != 0; }

  ObTenantMeta get_tenant_meta();
  bool is_hidden();
  storage::ObTenantCreateStatus get_create_status();
  void set_create_status(const storage::ObTenantCreateStatus status);

  int create_tenant_module();

  share::ObUnitInfoGetter::ObTenantConfig get_unit();
  uint64_t get_unit_id();
  storage::ObTenantSuperBlock get_super_block();
  void set_tenant_unit(const share::ObUnitInfoGetter::ObTenantConfig &unit);
  void set_tenant_super_block(const storage::ObTenantSuperBlock &super_block);
  void mark_tenant_is_removed();
  share::ObUnitInfoGetter::ObUnitStatus get_unit_status();

  void set_unit_max_cpu(double cpu);
  void set_unit_min_cpu(double cpu);
  int64_t cpu_quota_concurrency() const;
  int64_t min_worker_cnt() const;
  int64_t max_worker_cnt() const;
  int64_t cur_ddl_thread_count() {return ATOMIC_LOAD(&total_ddl_thread_cnt_);}
  void inc_ddl_thread_count() { ATOMIC_INC(&total_ddl_thread_cnt_); };
  void dec_ddl_thread_count() { ATOMIC_DEC(&total_ddl_thread_cnt_); };
  bool check_ddl_thread_is_limit(const int64_t cpu_quota_concurrency) { return ATOMIC_LOAD(&total_ddl_thread_cnt_) >= static_cast<int64_t>(unit_min_cpu() * cpu_quota_concurrency); }
  lib::Worker::CompatMode get_compat_mode() const;
  OB_INLINE share::ObTenantSpace &ctx() { return *ctx_; }
  int rdlock();
  int wrlock();
  int try_rdlock();
  int try_wrlock();
  virtual int unlock() override;
  virtual void on_schema_publish() override;

  // get request from request queue, waiting at most TIMEOUT us.
  // if IN_HIGH_PRIORITY is set, get request from hp queue.
  int get_new_request(ObThWorker &w, int64_t timeout, rpc::ObRequest *&req);

  // receive request from network
  int recv_request(rpc::ObRequest &req);
  int push_retry_queue(rpc::ObRequest &req, const uint64_t idx);
  void handle_retry_req(bool need_clear = false);
  void set_queue_limit(int64_t limit) { req_queue_.set_limit(limit); }

  int timeup();
  int get_default_group_throttled_time(int64_t &default_group_throttled_time);
  void print_throttled_time();
  void regist_threads_to_cgroup();

  TO_STRING_KV("id", id(),
               K_(tenant_meta),
               K_(unit_min_cpu), K_(unit_max_cpu),
               "total_worker_cnt", worker_count(),
               "idle_worker_cnt", idle_count(),
               "min_worker_cnt", min_worker_cnt(),
               "max_worker_cnt", max_worker_cnt(),
               K_(stopped),
               "worker_us", get_worker_time(),
               K_(recv_hp_rpc_cnt), K_(recv_np_rpc_cnt),
               K_(recv_lp_rpc_cnt), K_(recv_mysql_cnt),
               K_(recv_task_cnt),
               "workers", workers_.get_size(),
               K_(req_queue),
               K_(token_change_ts),
               "tenant_role", get_tenant_role())
public:
  static bool equal(const ObTenant *t1, const ObTenant *t2)
  {
    return (!OB_ISNULL(t1) && !OB_ISNULL(t2) && t1->id() == t2->id());
  }

  OB_INLINE void disable_user_sched() { disable_user_sched_ = true; }
  OB_INLINE bool user_sched_enabled() const { return !disable_user_sched_; }
  OB_INLINE double get_token_usage() const { return 0; }
  OB_INLINE int64_t get_worker_time() const { return 0; }
  int64_t get_cpu_time() const;
  // sql throttle
  void update_sql_throttle_metrics(const ObSqlThrottleMetrics &metrics)
  { st_metrics_ = metrics; }
  const ObSqlThrottleMetrics &get_sql_throttle_metrics() const
  { return st_metrics_; }

  void update_sql_throughput(const int64_t throughput)
  {
    if (throughput < 0) {
      sql_limiter_.set_rate(-1);
    } else {
      sql_limiter_.set_rate(throughput);
    }
  }
  lib::ObRateLimiter &get_sql_rate_limiter()
  { return sql_limiter_; }

  // Node balance thread would periodically check tenant status by
  // calling this function.
  void periodically_check();
  int64_t lq_retry_queue_size()
  {
    return 0;
  }
  ReqQueue& get_req_queue() { return req_queue_; }
  int acquire_more_worker(int64_t num, int64_t &succ_num, bool force = false);
  bool do_add_worker();
  int64_t queue_size() const { return req_queue_.size(); }
private:
  static void sleep_and_warn(ObTenant* tenant);
  static void* wait(void* tenant);
  // acquire workers if tenant doesn't have sufficient worker.
  void check_worker_count();

  OB_INLINE int pop_req(common::ObLink *&req, int64_t timeout) { return req_queue_.pop(req, timeout); }

  // read tenant variable PARALLEL_SERVERS_TARGET
  void check_parallel_servers_target();
  void check_px_thread_recycle();
  // clean buffer on time
  void check_dtl();

  int construct_mtl_init_ctx(const ObTenantMeta &meta, share::ObTenantModuleInitCtx *&ctx);

protected:

  mutable common::TCRWLock meta_lock_;
  ObTenantMeta tenant_meta_;

protected:
  // number of active workers the tenant has owned. Only active
  // workers can make progress.
  int64_t total_ddl_thread_cnt_;
  void *gc_thread_;
  bool has_created_;
  int64_t stopped_;
  bool wait_mtl_finished_;

  /// tenant task queue,
  // 'hp' for high priority and 'np' for normal priority
  ReqQueue req_queue_;

  //Create a timer queue group for retry requests
  ObRetryQueue retry_queue_;

  volatile uint64_t recv_hp_rpc_cnt_;
  volatile uint64_t recv_np_rpc_cnt_;
  volatile uint64_t recv_lp_rpc_cnt_;
  volatile uint64_t recv_mysql_cnt_;
  volatile uint64_t recv_task_cnt_;
  volatile uint64_t recv_sql_task_cnt_;
  volatile uint64_t recv_large_req_cnt_;
  volatile uint64_t recv_retry_on_lock_rpc_cnt_;
  volatile uint64_t recv_retry_on_lock_mysql_cnt_;
  volatile uint64_t tt_large_quries_;

public:
  common::ObLatch lock_;

  // Variables for V2
  WList workers_;
  share::ObTenantModuleInitCtx *mtl_init_ctx_;

  lib::ObMutex workers_lock_;

  share::ObCgroupCtrl &cgroup_ctrl_;
  bool embedded_;

  bool disable_user_sched_;

  int64_t token_change_ts_ CACHE_ALIGNED;
  std::atomic<int64_t> completion_cnt_;

  share::ObTenantSpace *ctx_;

  ObSqlThrottleMetrics st_metrics_;
  lib::ObQueryRateLimiter sql_limiter_;
  int64_t default_group_throttled_time_us_;
}; // end of class ObTenant

inline int ObTenant::rdlock()
{
  return lock_.rdlock(common::ObLatchIds::TENANT_LOCK) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObTenant::wrlock()
{
  uint32_t puid = static_cast<uint32_t>(GETTID());
  return lock_.wrlock(common::ObLatchIds::TENANT_LOCK, INT64_MAX, &puid) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObTenant::try_rdlock()
{
  return lock_.try_rdlock(common::ObLatchIds::TENANT_LOCK) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObTenant::try_wrlock()
{
  uint32_t puid = static_cast<uint32_t>(GETTID());
  return lock_.try_wrlock(common::ObLatchIds::TENANT_LOCK, &puid) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

inline int ObTenant::unlock()
{
  uint32_t puid = static_cast<uint32_t>(GETTID());
  return lock_.unlock(&puid) == common::OB_SUCCESS
      ? common::OB_SUCCESS
      : common::OB_EAGAIN;
}

} // end of namespace omt
} // end of namespace oceanbase

#endif /* _OCEABASE_OBSERVER_OMT_OB_TENANT_H_ */
