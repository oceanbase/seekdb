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

#define USING_LOG_PREFIX SERVER_OMT
#include "ob_server_runtime.h"
#include "observer/ob_server.h"   // T3d
#include "share/rc/ob_server_runtime.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/statistic_event/ob_stat_event.h"
#include "share/interrupt/ob_global_interrupt_call.h"
#include "share/ob_cpu_share_calculator.h"

#include "sql/engine/px/ob_px_target_monitor.h"
#include "sql/dtl/ob_dtl_fc_server.h"
#include "observer/ob_srv_network_frame.h"
#include "lib/worker.h"
#include "storage/ob_file_system_router.h"
#include "share/rc/ob_server_module_init_ctx.h"
#include "sql/engine/px/ob_px_worker.h"
#include "observer/change_stream/ob_change_stream_mgr.h"

using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::omt;
using namespace oceanbase::rpc;
using namespace oceanbase::share;
using namespace oceanbase::share::schema;
using namespace oceanbase::storage;
using namespace oceanbase::sql::dtl;
using namespace oceanbase::obcall;

#define GET_OTHER_TSI_ADDR(var_name, addr) \
const int64_t var_name##_offset = ((int64_t)addr - (int64_t)pthread_self()); \
decltype(*addr) var_name = *(decltype(addr))(thread_base + var_name##_offset);

extern "C" {
int ob_pthread_create(void **ptr, void *(*start_routine) (void *), void *arg);
int ob_pthread_tryjoin_np(void *ptr);
}
int ObPxPools::init()
{
  static int PX_POOL_COUNT = 128; // 128 groups, generally enough
  int ret = OB_SUCCESS;
  ObMemAttr attr("PxPoolBkt");
  if (OB_FAIL(pool_map_.create(PX_POOL_COUNT, attr, attr))) {
  }
  return ret;
}

int ObPxPools::get_or_create(int64_t group_id, ObPxPool *&pool)
{
  int ret = OB_SUCCESS;
  if (!pool_map_.created()) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(pool_map_.get_refactored(group_id, pool))) {
    if (OB_HASH_NOT_EXIST == ret) {
      if (OB_FAIL(create_pool(group_id, pool))) {
      }
    } else {
      LOG_WARN("fail get group id from hashmap", K(ret), K(group_id));
    }
  }
  return ret;
}

int ObPxPools::create_pool(int64_t group_id, ObPxPool *&pool)
{
  static constexpr uint64_t MAX_TASKS_PER_CPU = 1;
  int ret = OB_SUCCESS;
  common::SpinWLockGuard g(lock_);
  if (OB_FAIL(pool_map_.get_refactored(group_id, pool))) {
    if (OB_HASH_NOT_EXIST == ret) {
      pool = OB_NEW(ObPxPool, ObMemAttr("PxPool"));
      if (OB_ISNULL(pool)) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else {

        pool->set_group_id(group_id);
        pool->set_run_wrapper(share::server_runtime());
        if (OB_FAIL(pool->start())) {
        } else if (OB_FAIL(pool_map_.set_refactored(group_id, pool))) {
        }
      }
    } else {
      LOG_WARN("fail get group id from hashmap", K(ret), K(group_id));
    }
  }
  return ret;
}

int ObPxPools::thread_recycle()
{
  int ret = OB_SUCCESS;
  common::SpinWLockGuard g(lock_);
  ThreadRecyclePoolFunc recycle_pool_func;
  if (OB_FAIL(pool_map_.foreach_refactored(recycle_pool_func))) {
  }
  return ret;
}

int ObPxPools::ThreadRecyclePoolFunc::operator() (common::hash::HashMapPair<int64_t, ObPxPool*> &kv)
{
  int ret = OB_SUCCESS;
  int64_t &group_id = kv.first;
  ObPxPool *pool = kv.second;
  if (NULL == pool) {
    LOG_WARN("pool is null", K(group_id));
  } else {
    IGNORE_RETURN pool->thread_recycle();
  }
  return ret;
}

int ObPxPools::StopPoolFunc::operator() (common::hash::HashMapPair<int64_t, ObPxPool*> &kv)
{
  int ret = OB_SUCCESS;
  int64_t &group_id = kv.first;
  ObPxPool *pool = kv.second;
  if (NULL == pool) {
    LOG_WARN("pool is null", K(group_id));
  } else {
    pool->stop();
    LOG_INFO("DEL_POOL_STEP_1: mark px pool stop succ!", K(group_id));
  }
  return ret;
}

int ObPxPools::DeletePoolFunc::operator() (common::hash::HashMapPair<int64_t, ObPxPool*> &kv)
{
  int ret = OB_SUCCESS;
  int64_t &group_id = kv.first;
  ObPxPool *pool = kv.second;
  if (NULL == pool) {
    LOG_WARN("pool is null", K(group_id));
  } else {
    pool->wait();
    LOG_INFO("DEL_POOL_STEP_2: wait pool empty succ!", K(group_id));
    pool->destroy();
    LOG_INFO("DEL_POOL_STEP_3: pool destroy succ!", K(group_id), K(pool->get_queue_size()));
    common::ob_delete(pool);
  }
  return ret;
}

void ObPxPools::server_module_stop(ObPxPools *&pools)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pools)) {
    // ignore ret
    // pools will be null if it's creating runtime and failed.
    LOG_WARN("pools is null");
  } else {
    common::SpinWLockGuard g(pools->lock_);
    StopPoolFunc stop_pool_func;
    if (OB_FAIL(pools->pool_map_.foreach_refactored(stop_pool_func))) {
    }
  }
}

void ObPxPools::destroy()
{
  int ret = OB_SUCCESS;
  common::SpinWLockGuard g(lock_);
  DeletePoolFunc free_pool_func;
  if (OB_FAIL(pool_map_.foreach_refactored(free_pool_func))) {
  } else {
    pool_map_.destroy();
  }
}

int ObPxPool::submit(const RunFuncT &func)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    queue_.set_limit(common::ObServerConfig::get_instance().server_task_queue_size);
    is_inited_ = true;
  }
  disable_recycle();
  ATOMIC_INC(&concurrency_);
  if (ATOMIC_LOAD(&active_threads_) < ATOMIC_LOAD(&concurrency_)) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    Task *t = OB_NEW(Task, ObMemAttr("PxTask"), func);
    if (OB_ISNULL(t)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(queue_.push(static_cast<ObLink*>(t), 0))) {
    }
  }
  if (ret != OB_SUCCESS) {
    ATOMIC_DEC(&concurrency_);
  }
  enable_recycle();
  return ret;
}

void ObPxPool::handle(ObLink *task)
{
  Task *t  = static_cast<Task*>(task);
  if (t == nullptr) {
    LOG_ERROR_RET(OB_INVALID_ARGUMENT, "px task is invalid");
  } else {
    bool need_exec = true;
    t->func_(need_exec);
    OB_DELETE(Task, "PxTask", t);
  }
  ATOMIC_DEC(&concurrency_);
}

void ObPxPool::set_px_thread_name()
{
  char buf[32];
  snprintf(buf, 32, "PX_G%ld", group_id_);
  lib::set_thread_name(buf);
}

void ObPxPool::run(int64_t idx)
{
  ATOMIC_INC(&active_threads_);
  set_thread_idx(idx);
  // Create worker for current thread.
  sql::ObPxWorker worker;
  Worker::set_worker_to_thread_local(&worker);
  run1();
}

void ObPxPool::run1()
{
  int ret = OB_SUCCESS;
  ObDIActionGuard action_guard("PxPool", "PxWorker", "");
  set_px_thread_name();
  CLEAR_INTERRUPTABLE();
  LOG_INFO("run px pool", K(group_id_), K_(active_threads));

	if (!is_inited_) {
    queue_.set_limit(common::ObServerConfig::get_instance().server_task_queue_size);
    is_inited_ = true;
  }

  ObLink *task = nullptr;
  int64_t idle_time = 0;
  while (!Thread::current().has_set_stop()) {
	  if (!is_inited_) {
      ob_usleep(10 * 1000L);
    } else {
      if (OB_SUCC(queue_.pop(task, QUEUE_WAIT_TIME))) {
        handle(task);
        idle_time = 0; // reset recycle timer
      } else {
        idle_time += QUEUE_WAIT_TIME;
        // if idle for more than 10 min, exit thread
        try_recycle(idle_time);
      }
    }
  }
}

void ObPxPool::try_recycle(int64_t idle_time)
{
  // recycle thread policy:
  // 1. first N threads reserved for first 10 min idle period
  // 2. no thread reserved after 1 hour idle period
  //
  // impl. note: must ensure active_threads_ > concurrency_, otherwise may hang task
  const int N = 8;
  if ((idle_time > 10LL * 60 * 1000 * 1000 && get_thread_count() >= N)
      || idle_time > 60LL * 60 * 1000 * 1000) {
    if (OB_SUCCESS == recycle_lock_.trylock()) {
      if (ATOMIC_LOAD(&active_threads_) > ATOMIC_LOAD(&concurrency_)) {
        ATOMIC_DEC(&active_threads_);
        // when thread marked as stopped,
        // it will exit the event loop and recycled by background deamon
        Thread::current().stop();
      }
      recycle_lock_.unlock();
    }
  }
}

void ObPxPool::stop()
{
  int ret = OB_SUCCESS;
  Threads::stop();
  ObLink *task = nullptr;
  bool need_exec = false;
  while (OB_SUCC(queue_.pop(task, QUEUE_WAIT_TIME))) {
    Task *t  = static_cast<Task*>(task);
    if (OB_NOT_NULL(t)) {
      t->func_(need_exec);
      OB_DELETE(Task, "PxTask", t);
    }
    ATOMIC_DEC(&concurrency_);
  }
}

ObServerRuntime::ObServerRuntime()
    : ObServerRuntimeState(),
      meta_lock_(),
      runtime_meta_(),
      total_ddl_thread_cnt_(0),
      gc_thread_(nullptr),
      has_created_(false),
      stopped_(0),
      modules_stopped_(false),
      req_queue_(),
      recv_mysql_cnt_(0),
      recv_task_cnt_(0),
      lock_(),
      workers_lock_(common::ObLatchIds::SERVER_RUNTIME_WORKER_LOCK),
      completion_cnt_(0)
{
}

ObServerRuntime::~ObServerRuntime() {}

int ObServerRuntime::init(const ObServerRuntimeMeta &meta)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObServerRuntimeState::init())) {
  } else {
    req_queue_.set_limit(GCONF.server_task_queue_size);
    if (OB_FAIL(construct_module_init_ctx(meta, module_init_ctx_))) {
    } else {
      runtime_meta_ = meta;
      // Carry the persisted profile selected before runtime construction. The
      // command-line role remains only a fresh-directory fallback.
      set_role(share::server_role());
      set_write_enabled(share::server_is_write_enabled());
      set_recovery_mode(share::server_is_recovery_mode());
      set_min_cpu(meta.runtime_config_.resource_config_.min_cpu());
      set_max_cpu(meta.runtime_config_.resource_config_.max_cpu());
      const int64_t memory_size = static_cast<double>(runtime_meta_.runtime_config_.resource_config_.memory_size());
      set_memory_size(memory_size);

      if (OB_FAIL(create_modules())) {
        // do nothing
      }
    }
  }

  if (OB_SUCC(ret)) {
    timeup();
  }

  if (OB_FAIL(ret)) {
  } else {
    start();
  }

  return ret;
}

int ObServerRuntime::construct_module_init_ctx(const ObServerRuntimeMeta &meta, share::ObServerModuleInitCtx *&ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx = OB_NEW(share::ObServerModuleInitCtx, ObMemAttr("ModuleInitCtx")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc ObServerModuleInitCtx failed", K(ret));
  } else if (OB_FAIL(OB_FILE_SYSTEM_ROUTER.get_server_clog_dir(ctx->clog_dir_))) {
  } else {
    ctx->palf_options_.disk_options_.log_disk_usage_limit_size_ = meta.runtime_config_.resource_config_.log_disk_size();
    ctx->palf_options_.disk_options_.log_disk_utilization_threshold_ = 80;
    ctx->palf_options_.disk_options_.log_disk_utilization_limit_threshold_ = 95;
    ctx->palf_options_.disk_options_.log_disk_throttling_percentage_ = 100;
    ctx->palf_options_.disk_options_.log_disk_throttling_maximum_duration_ = 2LL * 60 * 60 * 1000 * 1000;//2h
    ctx->palf_options_.enable_log_cache_ = GCONF._enable_log_cache;
    LOG_INFO("construct_module_init_ctx success", "palf_options", ctx->palf_options_.disk_options_
             );
  }
  return ret;
}
bool ObServerRuntime::is_hidden()
{
  TCRLockGuard guard(meta_lock_);
  return runtime_meta_.super_block_.is_hidden_;
}

void ObServerRuntime::set_create_status(const ObServerRuntimeCreateStatus status)
{
  TCWLockGuard guard(meta_lock_);
  LOG_INFO("set create status",
      "new_status", status,
      "old_status", runtime_meta_.create_status_,
      K_(runtime_meta));
  runtime_meta_.create_status_ = status;
}

ObServerRuntimeMeta ObServerRuntime::get_runtime_meta()
{
  TCRLockGuard guard(meta_lock_);
  return runtime_meta_;
}

ObServerRuntimeConfig ObServerRuntime::get_runtime_config()
{
  TCRLockGuard guard(meta_lock_);
  return runtime_meta_.runtime_config_;
}

ObServerRuntimeSuperBlock ObServerRuntime::get_super_block()
{
  TCRLockGuard guard(meta_lock_);
  return runtime_meta_.super_block_;
}

void ObServerRuntime::set_server_resources(const ObServerRuntimeConfig &runtime_config)
{
  TCWLockGuard guard(meta_lock_);
  runtime_meta_.runtime_config_ = runtime_config;
}

void ObServerRuntime::set_server_super_block(const ObServerRuntimeSuperBlock &super_block)
{
  TCWLockGuard guard(meta_lock_);
  runtime_meta_.super_block_ = super_block;
}

ERRSIM_POINT_DEF(CREATE_MODULES_FAIL)
int ObServerRuntime::create_modules()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("begin create modules");

  // Publish the single runtime before constructing modules so their thread
  // wrappers can resolve the server context during initialization.
  g_server_runtime = this;

  bool modules_constructed = false;
  if (OB_FAIL(OBSERVER.obs_construct_modules())) {
  } else if (CREATE_MODULES_FAIL) {
    ret = CREATE_MODULES_FAIL;
    LOG_ERROR("create_modules failed because of tracepoint CREATE_MODULES_FAIL",
              K(ret));
  } else if (FALSE_IT(modules_constructed = true)) {
  } else if (OB_FAIL(OBSERVER.obs_init_modules())) {
  } else if (OB_FAIL(OBSERVER.obs_start_modules())) {
  }

  FLOG_INFO("finish create modules", K(ret));

  if (OB_FAIL(ret)) {
    if (modules_constructed) {
      OBSERVER.obs_stop_modules();
      OBSERVER.obs_wait_modules();
    }
    OBSERVER.obs_destroy_modules();
  } else {
    ::oceanbase::share::g_server_modules_ready = true;
  }

  return ret;
}

void ObServerRuntime::sleep_and_warn(ObServerRuntime* runtime)
{
  ob_usleep(10_ms);
  const int64_t ts = ObTimeUtility::current_time() - runtime->stopped_;
  if (ts >= 3L * 60 * 1000 * 1000 && TC_REACH_TIME_INTERVAL(3L * 60 * 1000 * 1000)) {
    LOG_ERROR_RET(OB_SUCCESS, "runtime destructed for too long time.", K(runtime->id()), K(ts));
  }
}

void* ObServerRuntime::wait(void* t)
{
  int ret = OB_SUCCESS;
  ObServerRuntime* runtime = (ObServerRuntime*)t;
  lib::set_thread_name("UnitGC");
  lib::Thread::update_loop_ts();
  runtime->handle_retry_req(true);
  while (runtime->req_queue_.size() > 0) {
    sleep_and_warn(runtime);
  }
  while (runtime->workers_.get_size() > 0) {
    if (OB_SUCC(runtime->workers_lock_.trylock())) {
      DLIST_FOREACH_REMOVESAFE(wnode, runtime->workers_) {
        const auto w = static_cast<ObThWorker*>(wnode->get_data());
        runtime->workers_.remove(wnode);
        destroy_worker(w);
      }
      IGNORE_RETURN runtime->workers_lock_.unlock();
      if (REACH_TIME_INTERVAL(10_s)) {
        LOG_INFO(
            "Runtime has some workers that need to stop", K(runtime->id()),
            "workers", runtime->workers_.get_size(),
            K_(runtime->req_queue));
      }
    }
    sleep_and_warn(runtime);
  }

  if (!runtime->modules_stopped_) {
    OBSERVER.obs_stop_modules();
    OBSERVER.obs_wait_modules();
    runtime->modules_stopped_ = true;
  }
  LOG_INFO("finish waiting", K(runtime->id()));
  return nullptr;
}

int ObServerRuntime::try_wait()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ATOMIC_LOAD(&gc_thread_))) {
    if (!ATOMIC_BCAS(&has_created_, false, true)) {
      // there will be double-try_wait when kill -15 or failure of locking,
      // so we have to tolerate that and return OB_SUCCESS although it is not correct.
      // ret = OB_ERR_UNEXPECTED;
      LOG_WARN("try_wait again after wait successfully, there may be `kill -15` or failure of locking", K(id()), K(modules_stopped_));
    } else {
      // Recalculate after the process-shutdown session cleanup.
      ATOMIC_STORE(&stopped_, ObTimeUtility::current_time()); // update, it is not 0 before here.
      if (OB_FAIL(ob_pthread_create(&gc_thread_, wait, this))) {
        ATOMIC_STORE(&has_created_, false);
        LOG_ERROR("runtime gc thread create failed", K(ret), K(errno), K(id()));
      } else {
        ret = OB_EAGAIN;
        LOG_INFO("runtime pthread_create gc thread successfully", K(id()), K(gc_thread_));
      }
    }
  } else {
    if (OB_FAIL(ob_pthread_tryjoin_np(gc_thread_))) {
    } else {
      ATOMIC_STORE(&gc_thread_, nullptr); // avoid try_wait again after wait success
      LOG_INFO("runtime pthread_tryjoin_np successfully", K(id()));
    }
    const int64_t ts = ObTimeUtility::current_time() - stopped_;
    // only warn for one time in all runtime.
    if (ts >= 3L * 60 * 1000 * 1000 && REACH_TIME_INTERVAL(3L * 60 * 1000 * 1000)) {
      LOG_ERROR_RET(OB_SUCCESS, "runtime destructed for too long time.", K(id()), K(ts));
    }
  }
  return ret;
}

void OB_WEAK_SYMBOL print_all_thread(const char* desc)
{
  UNUSED(desc);
}

void ObServerRuntime::destroy()
{
  ::oceanbase::share::g_server_modules_ready = false;
  print_all_thread("SERVER_RUNTIME_BEFORE_DESTROY");
  OBSERVER.obs_destroy_modules();
  ::oceanbase::share::g_server_runtime = &::oceanbase::share::g_bootstrap_server_runtime;
  ObServerRuntimeState::destroy();

  if (nullptr != module_init_ctx_) {
    common::ob_delete(module_init_ctx_);
    module_init_ctx_ = nullptr;
  }
}

void ObServerRuntime::set_max_cpu(double cpu)
{
  max_cpu_ = cpu;
}

void ObServerRuntime::set_min_cpu(double cpu)
{
  min_cpu_ = cpu;
}

int64_t ObServerRuntime::cpu_quota_concurrency() const
{
  return static_cast<int64_t>(GCONF.cpu_quota_concurrency);
}

int64_t ObServerRuntime::min_worker_cnt() const
{
  return 2 + std::max(static_cast<int64_t>(1L),
             static_cast<int64_t>(min_cpu() * cpu_quota_concurrency()));
}

int64_t ObServerRuntime::max_worker_cnt() const
{
  int64_t cnt = std::max(runtime_meta_.runtime_config_.resource_config_.memory_size() / 20 / (GCONF.stack_size + (3 << 20) + (512 << 10)),
                  static_cast<int64_t>(150L));
  return cnt;
}

int ObServerRuntime::get_new_request(
    int64_t timeout,
    rpc::ObRequest *&req)
{
  int ret = OB_SUCCESS;
  ObLink* task = nullptr;

  req = nullptr;
  ret = req_queue_.pop(task, timeout);

  if (OB_SUCC(ret)) {
    if (nullptr == req && nullptr != task) {
      req = static_cast<rpc::ObRequest*>(task);
    }
  }
  return ret;
}

int ObServerRuntime::recv_request(ObRequest &req)
{
  int ret = OB_SUCCESS;
  // Single classification point: the same high-priority decision that picks
  // the queue priority also drives the foreground expansion limit, so the
  // two can never drift apart (and expansion does not re-derive request
  // internals).
  bool is_high_prio = false;
  if (has_stopped()) {
    ret = OB_SERVER_RUNTIME_NOT_READY;
    LOG_WARN("receive request but runtime has already stopped", K(ret), K(id()));
  } else {
    req.set_enqueue_timestamp(ObTimeUtility::current_time());
    req.set_trace_point(ObRequest::OB_REQUEST_RUNTIME_RECEIVED);
    switch (req.get_type()) {
      case ObRequest::OB_MYSQL: {
        if (!req.is_retry_on_lock()) {
          ATOMIC_INC(&recv_mysql_cnt_);
        }
        // Keep authentication ahead of normal SQL regardless of whether the
        // client uses TCP, a Unix domain socket, or a Windows named pipe.
        is_high_prio = req.is_retry_on_lock() || req.is_auth_request();
        if (OB_FAIL(req_queue_.push(&req, is_high_prio ? RQ_HIGH : RQ_NORMAL, true))) {
        }
        break;
      }
      case ObRequest::OB_TASK:
      {
        ATOMIC_INC(&recv_task_cnt_);
        is_high_prio = true;
        if (OB_FAIL(req_queue_.push(&req, RQ_HIGH, true))) {
        }
        break;
      }
      case ObRequest::OB_SQL_TASK: {
        is_high_prio = false;
        if (OB_FAIL(req_queue_.push(&req, RQ_NORMAL, true))) {
        }
        break;
      }
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unknown request type", K(ret));
        break;
      }
    }
  }

  if (OB_SUCC(ret)) {
    EVENT_INC(REQUEST_ENQUEUE_COUNT);
    // Expand from foreground when no idle worker, in case the only
    // worker is busy and cannot fire the worker-loop expand signal.
    // Regular requests stay bounded by min_worker_cnt; high-priority
    // requests jump straight to max_worker_cnt so a fresh worker is
    // spawned on the spot instead of waiting for the 3s stalled-completion
    // rescue in timeup(). Spawned workers are reused by subsequent requests
    // and idle out through the normal 10s keep-alive shrink, so no threads
    // are permanently reserved.
    if (idle_count() == 0) {
      try_expand_one(is_high_prio ? max_worker_cnt() : min_worker_cnt());
    }
  }

  return ret;
}

int ObServerRuntime::push_retry_queue(rpc::ObRequest &req, const uint64_t timestamp)
{
  int ret = OB_SUCCESS;
  if (has_stopped()) {
    ret = OB_IN_STOP_STATE;
    LOG_WARN("receive retry request but runtime has already stopped", K(ret), K(id()));
  } else if (OB_FAIL(retry_queue_.push(req, timestamp))) {
  }
  return ret;
}

int ObServerRuntime::timeup()
{
  int ret = OB_SUCCESS;
  if (!has_stopped() && OB_SUCC(try_rdlock())) {
    // it may fail during drop runtime, try next time.
    if (!has_stopped()) {
      // timeup ticks at 100ms so retry_queue_ is drained promptly
      // (packet-retry latency is bounded by this period); the costly
      // worker-count maintenance keeps its relaxed 1s cadence.
      if (REACH_TIME_INTERVAL(1 * 1000 * 1000L)) {
        check_worker_count();
      }
      // Rescue expansion: if request completion stalls for 3s while
      // queue is non-empty and workers are at min_worker_cnt, workers
      // may be deadlocked — expand up to max_worker_cnt.
      if (REACH_TIME_INTERVAL(3 * 1000 * 1000L)) {
        static int64_t last_completion_cnt = 0;
        int64_t completion_cnt = completion_cnt_.load(std::memory_order_relaxed);
        if (worker_count() >= min_worker_cnt() && queue_size() > 0
            && completion_cnt == last_completion_cnt) {
          try_expand_one(max_worker_cnt());
        }
        last_completion_cnt = completion_cnt;
      }
      handle_retry_req();
    }
    IGNORE_RETURN unlock();
  }
  return OB_SUCCESS;
}

void ObServerRuntime::handle_retry_req(bool need_clear)
{
  int ret = OB_SUCCESS;
  ObLink* task = nullptr;
  ObRequest *req = NULL;
  // even if ret != OB_SUCCESS, the loop must continue to pop all requests
  while (OB_SUCC(retry_queue_.pop(task, need_clear))) {
    // if pop returns OB_SUCCESS, then the task must not be NULL.
    req = static_cast<rpc::ObRequest*>(task);
    if (OB_FAIL(recv_request(*req))) {
      LOG_WARN("runtime patrol push req into common queue fail, "
          "and the req well be destroyed", "req", *req, K(ret));
      on_translate_fail(req, ret);
    }
  }
}

void ObServerRuntime::check_worker_count()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(workers_lock_.trylock())) {
    // Reap stopped workers (those that exited via try_shrink).
    DLIST_FOREACH_REMOVESAFE_NORET(wnode, workers_) {
      const auto w = static_cast<ObThWorker*>(wnode->get_data());
      if (w->has_set_stop()) {
        workers_.remove(wnode);
        destroy_worker(w);
      }
    }
    IGNORE_RETURN workers_lock_.unlock();
  }
}

int ObServerRuntime::acquire_more_worker(int64_t num, int64_t &succ_num, bool force)
{
  int ret = OB_SUCCESS;
  succ_num = 0;

  while (OB_SUCC(ret) && num > succ_num) {
    ObThWorker *w = nullptr;
    if (OB_FAIL(create_worker(w, this))) {
    } else {
      lib::ObMutexGuard g(workers_lock_);
      if (!workers_.add_last(&w->worker_node_)) {
        ob_abort();
      }
      succ_num++;
    }
  }

  return ret;
}

bool ObServerRuntime::do_add_worker()
{
  int64_t succ_num = 0;
  int ret = acquire_more_worker(1, succ_num);
  if (OB_FAIL(ret) || succ_num != 1) {
    LOG_WARN("do_add_worker failed", K(ret), K(succ_num),
             "max_worker_cnt", max_worker_cnt());
  }
  return OB_SUCCESS == ret && succ_num == 1;
}


void ObServerRuntime::periodically_check()
{
  check_parallel_servers_target();
  check_dtl();
  check_px_thread_recycle();
}

void ObServerRuntime::check_dtl()
{
  int ret = OB_SUCCESS;
  auto dfc_manager = ::oceanbase::share::server_service<::oceanbase::sql::dtl::ObDfc>();
  if (OB_NOT_NULL(dfc_manager)) {
    dfc_manager->check_dtl();
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime dtl fc server is null", K(id()), K(ret));
  }
}

void ObServerRuntime::check_parallel_servers_target()
{
  int ret = OB_SUCCESS;
  int64_t val = 0;
  if (OB_FAIL(ObSchemaUtils::get_runtime_int_variable(
              *GCTX.schema_service_,
              SYS_VAR_PARALLEL_SERVERS_TARGET,
              val))) {
  } else {
    val = ObCpuShareCalculator::resolve_parallel_servers_target(
        val,
        static_cast<int64_t>(GCONF.get_server_default_min_cpu()),
        GCONF.px_workers_per_cpu_quota);
    OB_PX_TARGET_MONITOR.set_parallel_servers_target(val);
  }
}

void ObServerRuntime::check_px_thread_recycle()
{
  int ret = OB_SUCCESS;
  auto px_pools = ::oceanbase::share::server_service<::oceanbase::omt::ObPxPools>();
  if (OB_NOT_NULL(px_pools)) {
    px_pools->thread_recycle();
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime px pools is null", K(id()), K(ret));
  }
}

void ObServerRuntime::on_schema_publish()
{
  // Schema publication now notifies the shared publish signal directly.
}
