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
#include "ob_tenant.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/resource.h>
#endif

#include "share/resource_manager/ob_resource_manager.h"
#include "sql/engine/px/ob_px_target_mgr.h"
#include "sql/dtl/ob_dtl_fc_server.h"
#include "observer/ob_srv_network_frame.h"
#include "lib/worker.h"
#include "storage/ob_file_system_router.h"
#include "storage/ob_file_system_router.h"
#include "share/rc/ob_tenant_module_init_ctx.h"
#include "sql/engine/px/ob_px_worker.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/resource/ob_affinity_ctrl.h"
#include "share/change_stream/ob_change_stream_mgr.h"

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

#define EXPAND_INTERVAL (1 * 1000 * 1000)
#define SHRINK_INTERVAL (1 * 1000 * 1000)
#define SLEEP_INTERVAL (60 * 1000 * 1000)

extern "C" {
int ob_pthread_create(void **ptr, void *(*start_routine) (void *), void *arg);
int ob_pthread_tryjoin_np(void *ptr);
}
int ObPxPools::init(uint64_t tenant_id)
{
  static int PX_POOL_COUNT = 128; // 128 groups, generally enough
  int ret = OB_SUCCESS;
  tenant_id_ = tenant_id;
  ObMemAttr attr(tenant_id, "PxPoolBkt");
  if (OB_FAIL(pool_map_.create(PX_POOL_COUNT, attr, attr))) {
    LOG_WARN("fail init pool map", K(ret));
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
        LOG_WARN("fail create pool", K(ret), K(group_id));
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
      pool = OB_NEW(ObPxPool, ObMemAttr(tenant_id_, "PxPool"));
      if (OB_ISNULL(pool)) {
        ret = common::OB_ALLOCATE_MEMORY_FAILED;
      } else {
        pool->set_tenant_id(tenant_id_);
        pool->set_group_id(group_id);
        pool->set_run_wrapper(MTL_CTX());
        if (OB_FAIL(pool->start())) {
          LOG_WARN("fail startup px pool", K(group_id), K(tenant_id_), K(ret));
        } else if (OB_FAIL(pool_map_.set_refactored(group_id, pool))) {
          LOG_WARN("fail set pool to hashmap", K(group_id), K(ret));
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
    LOG_WARN("failed to do foreach", K(ret));
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

void ObPxPools::mtl_stop(ObPxPools *&pools)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(pools)) {
    // ignore ret
    // pools will be null if it's creating tenant and failed.
    LOG_WARN("pools is null");
  } else {
    common::SpinWLockGuard g(pools->lock_);
    StopPoolFunc stop_pool_func;
    if (OB_FAIL(pools->pool_map_.foreach_refactored(stop_pool_func))) {
      LOG_WARN("failed to do foreach", K(ret));
    }
  }
}

void ObPxPools::destroy()
{
  int ret = OB_SUCCESS;
  common::SpinWLockGuard g(lock_);
  DeletePoolFunc free_pool_func;
  if (OB_FAIL(pool_map_.foreach_refactored(free_pool_func))) {
    LOG_WARN("failed to do foreach", K(ret));
  } else {
    pool_map_.destroy();
    tenant_id_ = OB_INVALID_ID;
  }
}

int ObPxPool::submit(const RunFuncT &func)
{
  int ret = OB_SUCCESS;
  if (!is_inited_) {
    queue_.set_limit(common::ObServerConfig::get_instance().tenant_task_queue_size);
    is_inited_ = true;
  }
  disable_recycle();
  ATOMIC_INC(&concurrency_);
  if (ATOMIC_LOAD(&active_threads_) < ATOMIC_LOAD(&concurrency_)) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    Task *t = OB_NEW(Task, ObMemAttr(tenant_id_, "PxTask"), func);
    if (OB_ISNULL(t)) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (OB_FAIL(queue_.push(static_cast<ObLink*>(t), 0))) {
      LOG_ERROR("px push queue failed", K(ret));
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
  ObPxWorker worker;
  Worker::set_worker_to_thread_local(&worker);
  run1();
}

void ObPxPool::run1()
{
  int ret = OB_SUCCESS;
  common::ObBackGroundSessionGuard backgroud_session_guard(tenant_id_, group_id_);
  ObDIActionGuard action_guard("PxPool", "PxWorker", "");
  set_px_thread_name();
  auto *pm = common::ObPageManager::thread_local_instance();
  if (OB_LIKELY(nullptr != pm)) {
    pm->set_tenant_ctx(tenant_id_, common::ObCtxIds::DEFAULT_CTX_ID);
  }
  //ObTaTLCacheGuard ta_guard(tenant_id_);
  CLEAR_INTERRUPTABLE();
  ObCgroupCtrl *cgroup_ctrl = GCTX.cgroup_ctrl_;
  LOG_INFO("run px pool", K(group_id_), K(tenant_id_), K_(active_threads));
  SET_GROUP_ID();

	if (!is_inited_) {
    queue_.set_limit(common::ObServerConfig::get_instance().tenant_task_queue_size);
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

ObTenant::ObTenant(const int64_t id,
                   const int64_t epoch,
                   const int64_t times_of_workers,
                   ObCgroupCtrl &cgroup_ctrl)
    : ObTenantBase(id, epoch, true),
      meta_lock_(),
      tenant_meta_(),
      total_ddl_thread_cnt_(0),
      gc_thread_(nullptr),
      has_created_(false),
      stopped_(0),
      wait_mtl_finished_(false),
      req_queue_(),
      recv_hp_rpc_cnt_(0),
      recv_np_rpc_cnt_(0),
      recv_lp_rpc_cnt_(0),
      recv_mysql_cnt_(0),
      recv_task_cnt_(0),
      recv_sql_task_cnt_(0),
      recv_large_req_cnt_(0),
      recv_retry_on_lock_rpc_cnt_(0),
      recv_retry_on_lock_mysql_cnt_(0),
      tt_large_quries_(0),
      lock_(),
      mtl_init_ctx_(nullptr),
      workers_lock_(common::ObLatchIds::TENANT_WORKER_LOCK),
      cgroup_ctrl_(cgroup_ctrl),
      disable_user_sched_(false),
      token_change_ts_(0),
      completion_cnt_(0),
      ctx_(nullptr),
      st_metrics_(),
      sql_limiter_()
{
}

ObTenant::~ObTenant() {}

int ObTenant::init_ctx()
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(CREATE_ENTITY(ctx_, this))) {
    LOG_WARN("create tenant ctx failed", K(ret));
  } else if (OB_ISNULL(ctx_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("NULL ptr", K(ret));
  }
  return ret;
}

int ObTenant::init(const ObTenantMeta &meta)
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ObTenantBase::init(&cgroup_ctrl_))) {
    LOG_WARN("fail to init tenant base", K(ret));
  } else if (OB_FAIL(req_queue_.init(AFFINITY_CTRL.get_num_nodes()))) {
    // For now only the enable_numa_aware mode can ensure the number of worker threads is at least the number of
    // NUMA node, so fallback to single-queue if enabel_numa_aware is disabled, otherwise some of the queues will
    // never be consumed if the worker thread number is small.
    LOG_WARN("fail to init tenant request queues", K(ret));
  } else if (FALSE_IT(req_queue_.set_limit(GCONF.tenant_task_queue_size))) {
  } else if (OB_FAIL(construct_mtl_init_ctx(meta, mtl_init_ctx_))) {
    LOG_WARN("construct_mtl_init_ctx failed", KR(ret), K(*this));
  } else {
    ObTenantBase::mtl_init_ctx_ = mtl_init_ctx_;
    tenant_meta_ = meta;
    set_unit_min_cpu(meta.unit_.config_.min_cpu());
    set_unit_max_cpu(meta.unit_.config_.max_cpu());
    const int64_t memory_size = static_cast<double>(tenant_meta_.unit_.config_.memory_size());
    set_unit_memory_size(memory_size);
    const int64_t data_disk_size = tenant_meta_.unit_.config_.data_disk_size();
    const int64_t actual_data_disk_size = tenant_meta_.unit_.actual_data_disk_size_;

    if (!is_virtual_tenant_id(id_)) {
      if (OB_FAIL(create_tenant_module())) {
        // do nothing
      } else if (OB_FAIL(OB_PX_TARGET_MGR.add_tenant(id_))) {
        LOG_WARN("add tenant into px target mgr failed", K(ret), K(id_));
      }
    } else {
      disable_user_sched(); // disable_user_sched for virtual tenant
    }
  }

  if (OB_SUCC(ret)) {
    timeup();
  }

  if (OB_FAIL(ret)) {
    LOG_ERROR("fail to create tenant module", K(ret));
  } else {
    start();
  }

  return ret;
}

int ObTenant::construct_mtl_init_ctx(const ObTenantMeta &meta, share::ObTenantModuleInitCtx *&ctx)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ctx = OB_NEW(share::ObTenantModuleInitCtx, ObMemAttr(id_, "ModuleInitCtx")))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("alloc ObTenantModuleInitCtx failed", K(ret));
  } else if (OB_FAIL(OB_FILE_SYSTEM_ROUTER.get_tenant_clog_dir(id_, mtl_init_ctx_->tenant_clog_dir_))) {
    LOG_ERROR("get_tenant_clog_dir failed", K(ret));
  } else {
    mtl_init_ctx_->palf_options_.disk_options_.log_disk_usage_limit_size_ = meta.unit_.config_.log_disk_size();
    mtl_init_ctx_->palf_options_.disk_options_.log_disk_utilization_threshold_ = 80;
    mtl_init_ctx_->palf_options_.disk_options_.log_disk_utilization_limit_threshold_ = 95;
    mtl_init_ctx_->palf_options_.disk_options_.log_disk_throttling_percentage_ = 100;
    mtl_init_ctx_->palf_options_.disk_options_.log_disk_throttling_maximum_duration_ = 2LL * 60 * 60 * 1000 * 1000;//2h
    mtl_init_ctx_->palf_options_.disk_options_.log_writer_parallelism_ = 3;
    ObTenantConfigGuard tenant_config(TENANT_CONF(MTL_ID()));
    if (OB_UNLIKELY(!tenant_config.is_valid())) {
      ret = is_virtual_tenant_id(id_) ? OB_SUCCESS : OB_ENTRY_NOT_EXIST;
    } else {
      mtl_init_ctx_->palf_options_.disk_options_.log_writer_parallelism_ = tenant_config->_log_writer_parallelism;
      mtl_init_ctx_->palf_options_.enable_log_cache_ = tenant_config->_enable_log_cache;
    }
    LOG_INFO("construct_mtl_init_ctx success", "palf_options", mtl_init_ctx_->palf_options_.disk_options_
             );
  }
  return ret;
}
bool ObTenant::is_hidden()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.super_block_.is_hidden_;
}

ObTenantCreateStatus ObTenant::get_create_status()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.create_status_;
}
void ObTenant::set_create_status(const ObTenantCreateStatus status)
{
  TCWLockGuard guard(meta_lock_);
  LOG_INFO("set create status",
      "tenant_id", id_,
      "unit_id", tenant_meta_.unit_.unit_id_,
      "new_status", status,
      "old_status", tenant_meta_.create_status_,
      K_(tenant_meta));
  tenant_meta_.create_status_ = status;
}

ObTenantMeta ObTenant::get_tenant_meta()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_;
}

ObUnitInfoGetter::ObTenantConfig ObTenant::get_unit()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.unit_;
}

uint64_t ObTenant::get_unit_id()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.unit_.unit_id_;
}

ObTenantSuperBlock ObTenant::get_super_block()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.super_block_;
}

void ObTenant::set_tenant_unit(const ObUnitInfoGetter::ObTenantConfig &unit)
{
  TCWLockGuard guard(meta_lock_);
  tenant_meta_.unit_ = unit;
}

void ObTenant::set_tenant_super_block(const ObTenantSuperBlock &super_block)
{
  TCWLockGuard guard(meta_lock_);
  tenant_meta_.super_block_ = super_block;
}

Worker::CompatMode ObTenant::get_compat_mode() const
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.unit_.mode_;
}

ObUnitInfoGetter::ObUnitStatus  ObTenant::get_unit_status()
{
  TCRLockGuard guard(meta_lock_);
  return tenant_meta_.unit_.unit_status_;
}

void ObTenant::mark_tenant_is_removed()
{
  TCWLockGuard guard(meta_lock_);
  LOG_INFO("mark tenant is removed",
      "tenant_id", id_,
      "unit_id", tenant_meta_.unit_.unit_id_,
      K_(tenant_meta));
  tenant_meta_.unit_.is_removed_ = true;
  set_prepare_unit_gc();
}

ERRSIM_POINT_DEF(CREATE_MTL_MODULE_FAIL)
// Initialize tenant sub-modules, ensuring synchronous execution during initialization, because it depends on thread-local variables and stack variables
int ObTenant::create_tenant_module()
{
  int ret = OB_SUCCESS;
  const uint64_t &tenant_id = id_;
  const double max_cpu = static_cast<double>(tenant_meta_.unit_.config_.max_cpu());
  // set tenant ctx to global
  ObTenantSwitchGuard guard(this);
  // set tenant init param
  FLOG_INFO("begin create mtl module>>>>", K(tenant_id), K(MTL_ID()));

  // Point g_tenant_ptr at this before create_mtl_module() so that
  // module constructors can access get_tenant() without nullptr deref.
  g_tenant_ptr = this;

  bool mtl_init = false;
  if (OB_FAIL(ObTenantBase::create_mtl_module())) {
    LOG_ERROR("create mtl module failed", K(tenant_id), K(ret));
  } else if (CREATE_MTL_MODULE_FAIL) {
    ret = CREATE_MTL_MODULE_FAIL;
    LOG_ERROR("create_tenant_module failed because of tracepoint CREATE_MTL_MODULE_FAIL",
              K(tenant_id), K(ret));
  } else if (FALSE_IT(mtl_init = true)) {
  } else if (OB_FAIL(ObTenantBase::init_mtl_module())) {
    LOG_ERROR("init mtl module failed", K(tenant_id), K(ret));
  } else if (OB_FAIL(ObTenantBase::start_mtl_module())) {
    LOG_ERROR("start mtl module failed", K(tenant_id), K(ret));
  } else if (OB_FAIL(update_thread_cnt(max_cpu))) {
    LOG_ERROR("update mtl module thread cnt fail", K(tenant_id), K(ret));
  }

  FLOG_INFO("finish create mtl module>>>>", K(tenant_id), K(MTL_ID()), K(ret));

  if (OB_FAIL(ret)) {
    if (mtl_init) {
      ObTenantBase::stop_mtl_module();
      ObTenantBase::wait_mtl_module();
    }
    ObTenantBase::destroy_mtl_module();
  }

  return ret;
}

void ObTenant::sleep_and_warn(ObTenant* tenant)
{
  ob_usleep(10_ms);
  const int64_t ts = ObTimeUtility::current_time() - tenant->stopped_;
  if (ts >= 3L * 60 * 1000 * 1000 && TC_REACH_TIME_INTERVAL(3L * 60 * 1000 * 1000)) {
    LOG_ERROR_RET(OB_SUCCESS, "tenant destructed for too long time.", K_(tenant->id), K(ts));
  }
}

void* ObTenant::wait(void* t)
{
  int ret = OB_SUCCESS;
  ObTenant* tenant = (ObTenant*)t;
  lib::set_thread_name("UnitGC");
  lib::Thread::update_loop_ts();
  tenant->handle_retry_req(true);
  while (tenant->req_queue_.size() > 0) {
    sleep_and_warn(tenant);
  }
  while (tenant->workers_.get_size() > 0) {
    if (OB_SUCC(tenant->workers_lock_.trylock())) {
      DLIST_FOREACH_REMOVESAFE(wnode, tenant->workers_) {
        const auto w = static_cast<ObThWorker*>(wnode->get_data());
        tenant->workers_.remove(wnode);
        destroy_worker(w);
      }
      IGNORE_RETURN tenant->workers_lock_.unlock();
      if (REACH_TIME_INTERVAL(10_s)) {
        LOG_INFO(
            "Tenant has some workers need stop", K_(tenant->id),
            "workers", tenant->workers_.get_size(),
            K_(tenant->req_queue));
      }
    }
    sleep_and_warn(tenant);
  }

  if (!is_virtual_tenant_id(tenant->id_) && !tenant->wait_mtl_finished_) {
    ObTenantSwitchGuard guard(tenant);
    tenant->stop_mtl_module();
    OB_PX_TARGET_MGR.delete_tenant(tenant->id_);
    tenant->wait_mtl_module();
    tenant->wait_mtl_finished_ = true;
  }
  LOG_INFO("finish waiting", K_(tenant->id));
  return nullptr;
}

int ObTenant::try_wait()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(ATOMIC_LOAD(&gc_thread_))) {
    if (!ATOMIC_BCAS(&has_created_, false, true)) {
      // there will be double-try_wait when kill -15 or failure of locking,
      // so we have to tolerate that and return OB_SUCCESS although it is not correct.
      // ret = OB_ERR_UNEXPECTED;
      LOG_WARN("try_wait again after wait successfully, there may be `kill -15` or failure of locking", K(id_), K(wait_mtl_finished_));
    } else {
      // it may takes too much time for killing session after remove_tenant, we should recalculate.
      ATOMIC_STORE(&stopped_, ObTimeUtility::current_time()); // update, it is not 0 before here.
      if (OB_FAIL(ob_pthread_create(&gc_thread_, wait, this))) {
        ATOMIC_STORE(&has_created_, false);
        LOG_ERROR("tenant gc thread create failed", K(ret), K(errno), K(id_));
      } else {
        ret = OB_EAGAIN;
        LOG_INFO("tenant pthread_create gc thread successfully", K(id_), K(gc_thread_));
      }
    }
  } else {
    if (OB_FAIL(ob_pthread_tryjoin_np(gc_thread_))) {
      LOG_WARN("tenant pthread_tryjoin_np failed", K(errno), K(id_));
    } else {
      ATOMIC_STORE(&gc_thread_, nullptr); // avoid try_wait again after wait success
      LOG_INFO("tenant pthread_tryjoin_np successfully", K(id_));
    }
    const int64_t ts = ObTimeUtility::current_time() - stopped_;
    // only warn for one time in all tenant.
    if (ts >= 3L * 60 * 1000 * 1000 && REACH_TIME_INTERVAL(3L * 60 * 1000 * 1000)) {
      LOG_ERROR_RET(OB_SUCCESS, "tenant destructed for too long time.", K_(id), K(ts));
    }
  }
  return ret;
}

void OB_WEAK_SYMBOL print_all_thread(const char* desc, uint64_t tenant_id)
{
  UNUSED(desc);
  UNUSED(tenant_id);
}

void ObTenant::destroy()
{
  int tmp_ret = OB_SUCCESS;
  if (ctx_ != nullptr) {
    DESTROY_ENTITY(ctx_);
    ctx_ = nullptr;
  }
  ObTenantSwitchGuard guard(this);
  print_all_thread("TENANT_BEFORE_DESTROY", id_);
  destroy_mtl_module();
  ObTenantBase::destroy();

  if (nullptr != mtl_init_ctx_) {
    common::ob_delete(mtl_init_ctx_);
    mtl_init_ctx_ = nullptr;
  }

  if (!cgroup_ctrl_.is_valid()) {
    // do nothing
  } else if (OB_TMP_FAIL(cgroup_ctrl_.remove_cgroup(id_))) {
    LOG_WARN_RET(tmp_ret, "remove tenant cgroup failed", K(tmp_ret), K_(id));
  }

  req_queue_.destroy();
}

void ObTenant::set_unit_max_cpu(double cpu)
{
  int tmp_ret = OB_SUCCESS;
  unit_max_cpu_ = cpu;
  if (!cgroup_ctrl_.is_valid() || is_sys_tenant(id_) || is_meta_tenant(id_)) {
    // do nothing
    // meta tenant and sys tenant are unlimited
  } else if (OB_TMP_FAIL(cgroup_ctrl_.set_cpu_cfs_quota(id_, cpu))) {
    _LOG_WARN_RET(tmp_ret, "set tenant cpu cfs quota failed, tenant_id=%lu, cpu=%.2f", id_, cpu);
  }
}

void ObTenant::set_unit_min_cpu(double cpu)
{
  int tmp_ret = OB_SUCCESS;
  unit_min_cpu_ = cpu;
  if (!cgroup_ctrl_.is_valid()) {
    // do nothing
  } else if (OB_TMP_FAIL(cgroup_ctrl_.set_cpu_shares(id_, cpu))) {
    _LOG_WARN_RET(tmp_ret, "set tenant cpu shares failed, tenant_id=%lu, cpu=%.2f", id_, cpu);
  }
}

int64_t ObTenant::cpu_quota_concurrency() const
{
  ObTenantConfigGuard tenant_config(TENANT_CONF(id_));
  return static_cast<int64_t>((tenant_config.is_valid() ? tenant_config->cpu_quota_concurrency : 4));
}

int64_t ObTenant::min_worker_cnt() const
{
  return 2 + std::max(static_cast<int64_t>(1L),
             static_cast<int64_t>(unit_min_cpu() * cpu_quota_concurrency()));
}

int64_t ObTenant::max_worker_cnt() const
{
  int64_t cnt = std::max(tenant_meta_.unit_.config_.memory_size() / 20 / (GCONF.stack_size + (3 << 20) + (512 << 10)),
                  static_cast<int64_t>(150L));
  if (GCONF._enable_numa_aware) {
    int numa_node_count = AFFINITY_CTRL.get_num_nodes();
    if (cnt < numa_node_count) {
      cnt = common::upper_align(cnt, numa_node_count);
    }
  }
  return cnt;
}

int ObTenant::get_new_request(
    ObThWorker &w,
    int64_t timeout,
    rpc::ObRequest *&req)
{
  int ret = OB_SUCCESS;
  ObLink* task = nullptr;

  req = nullptr;
  w.set_large_query(false);
  ret = req_queue_.pop(task, timeout);

  if (OB_SUCC(ret)) {
    if (nullptr == req && nullptr != task) {
      req = static_cast<rpc::ObRequest*>(task);
    }
    if (nullptr != req) {
      if (req->large_retry_flag()) {
        w.set_large_query();
      }
    }
  }
  return ret;
}

int ObTenant::recv_request(ObRequest &req)
{
  int ret = OB_SUCCESS;
  if (has_stopped()) {
    ret = OB_TENANT_NOT_IN_SERVER;
    LOG_WARN("receive request but tenant has already stopped", K(ret), K(id_));
  } else {
    // Request would been pushed into corresponding queue by rule.
    //
    //   1. RPC with high or normal priority goes into quick queue.
    //   2. RPC with low priority, usually trivial task, goes into normal queue with low priority.
    //   3. SQL goes into normal queue with normal priority.
    //   4. Server task, session close task, goes into normal queue with high priority.
    //
    req.set_enqueue_timestamp(ObTimeUtility::current_time());
    req.set_trace_point(ObRequest::OB_EASY_REQUEST_TENANT_RECEIVED);
    switch (req.get_type()) {
      case ObRequest::OB_RPC: {
        // obcall RPC transport removed (single-replica): no OB_RPC request is
        // ever delivered to a tenant. Treat any arrival as unexpected.
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("unexpected OB_RPC request after rpc removal", K(ret), K(id_));
        break;
      }
      case ObRequest::OB_MYSQL: {
        if (req.is_retry_on_lock()) {
          ATOMIC_INC(&recv_retry_on_lock_mysql_cnt_);
          if (OB_FAIL(req_queue_.push(&req, RQ_HIGH, true))) {
            LOG_WARN("push request to RQ_HIGH queue fail", K(ret), K(this));
          }
        } else {
          ATOMIC_INC(&recv_mysql_cnt_);
          if (OB_FAIL(req_queue_.push(&req, RQ_NORMAL, true))) {
            LOG_WARN("push request to queue fail", K(ret), K(this));
          }
        }
        break;
      }
      case ObRequest::OB_TASK:
      case ObRequest::OB_TS_TASK: {
        ATOMIC_INC(&recv_task_cnt_);
        if (OB_FAIL(req_queue_.push(&req, RQ_HIGH, true))) {
          LOG_WARN("push request to queue fail", K(ret), K(this));
        }
        break;
      }
      case ObRequest::OB_SQL_TASK: {
        ATOMIC_INC(&recv_sql_task_cnt_);
        if (OB_FAIL(req_queue_.push(&req, RQ_NORMAL, true))) {
          LOG_WARN("push request to queue fail", K(ret), K(this));
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
    // try_expand_one enforces the min_worker_cnt upper bound via CAS.
    if (idle_count() == 0) {
      try_expand_one(min_worker_cnt());
    }
  }

  if (OB_SIZE_OVERFLOW == ret || (GCONF._faststack_req_queue_size_threshold.get_value() > 0 &&
      req_queue_.size() >= GCONF._faststack_req_queue_size_threshold.get_value())) {
    IGNORE_RETURN faststack();
  }

  return ret;
}

int ObTenant::push_retry_queue(rpc::ObRequest &req, const uint64_t timestamp)
{
  int ret = OB_SUCCESS;
  if (has_stopped()) {
    ret = OB_IN_STOP_STATE;
    LOG_WARN("receive retry request but tenant has already stopped", K(ret), K(id_));
  } else if (OB_FAIL(retry_queue_.push(req, timestamp))) {
    LOG_ERROR("push retry queue failed", K(ret), K(id_));
  }
  return ret;
}

int ObTenant::timeup()
{
  int ret = OB_SUCCESS;
  if (!has_stopped() && OB_SUCC(try_rdlock())) {
    // it may fail during drop tenant, try next time.
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

int ObTenant::get_default_group_throttled_time(int64_t &default_group_throttled_time)
{
  int ret = OB_SUCCESS;
  int64_t current_default_group_throttled_time_us = -1;
  if (OB_FAIL(GCTX.cgroup_ctrl_->get_throttled_time(id_, current_default_group_throttled_time_us, OBCG_DEFAULT_GROUP_ID))) {
    LOG_WARN("get throttled time failed", K(ret), K(id_));
  } else if (current_default_group_throttled_time_us > 0) {
    default_group_throttled_time = current_default_group_throttled_time_us - default_group_throttled_time_us_;
    default_group_throttled_time_us_ = current_default_group_throttled_time_us;
  }
  return ret;
}

void ObTenant::print_throttled_time()
{
  class ThrottledTimeLog
  {
  public:
    ThrottledTimeLog(ObTenant *tenant) : tenant_(tenant)
    {}
    ~ThrottledTimeLog()
    {}
    int64_t to_string(char *buf, const int64_t len) const
    {
      int64_t pos = 0;
      int tmp_ret = OB_SUCCESS;
      int64_t tenant_throttled_time = 0;
      int64_t group_throttled_time = 0;

      if (OB_TMP_FAIL(tenant_->get_default_group_throttled_time(group_throttled_time))) {
        LOG_WARN_RET(tmp_ret, "get throttled time failed", K(tmp_ret));
      } else {
        tenant_throttled_time += group_throttled_time;
        databuff_printf(buf, len, pos, "group_id: 0, group: OBCG_DEFAULT, throttled_time: %ld;", group_throttled_time);
      }

      ObCgSet &set = ObCgSet::instance();

      ObRefHolder<ObTenantIOManager> tenant_holder;
      if (OB_TMP_FAIL(OB_IO_MANAGER.get_tenant_io_manager(tenant_->id_, tenant_holder))) {
        LOG_WARN_RET(tmp_ret, "get tenant io manager failed", K(tmp_ret), K(tenant_->id_));
      } else {
        const uint64_t MODE_CNT = static_cast<uint64_t>(ObIOMode::MAX_MODE) + 1;
        for (int64_t i = 0; i < tenant_holder.get_ptr()->get_group_num(); i++) {
          uint64_t group_config_index = i * MODE_CNT;
          if (!tenant_holder.get_ptr()->get_io_config().group_configs_.at(group_config_index).deleted_) {
            uint64_t group_id = tenant_holder.get_ptr()->get_io_config().group_configs_.at(group_config_index).group_id_;
            if (OB_TMP_FAIL(tenant_holder.get_ptr()->get_throttled_time(group_id, group_throttled_time))) {
              LOG_WARN_RET(tmp_ret, "get throttled time failed", K(tmp_ret), K(group_id));
            } else {
              tenant_throttled_time += group_throttled_time;
              databuff_printf(buf,
                  len,
                  pos,
                  "group_id: %ld, throttled_time: %ld;",
                  group_id,
                  group_throttled_time);
            }
          }
        }
      }
      databuff_printf(
          buf, len, pos, "tenant_id: %lu, tenant_throttled_time: %ld;", tenant_->id_, tenant_throttled_time);
      return pos;
    }
    ObTenant *tenant_;
  };
  ThrottledTimeLog throttled_time_log(this);
  LOG_INFO("dump throttled time info", K(id_), K(throttled_time_log));
}

void ObTenant::regist_threads_to_cgroup()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  // set cgroup configs
  if (OB_TMP_FAIL(cgroup_ctrl_.set_cpu_shares(id_, unit_min_cpu_, OB_INVALID_GROUP_ID))) {
    LOG_WARN_RET(tmp_ret, "set tenant cpu shares failed", K(tmp_ret), K_(id), K_(unit_min_cpu));
  } else if (is_meta_tenant(id_)) {
    // do nothing
  } else if (OB_TMP_FAIL(
                 cgroup_ctrl_.set_cpu_cfs_quota(id_, is_sys_tenant(id_) ? -1 : unit_max_cpu_, OB_INVALID_GROUP_ID))) {
    LOG_WARN_RET(tmp_ret, "set tenant cpu cfs quota failed", K(tmp_ret), K_(id), K_(unit_max_cpu));
  }

  if (OB_SUCC(thread_list_lock_.trylock())) {
#ifndef _WIN32
    DLIST_FOREACH_REMOVESAFE(thread_list_node_, thread_list_)
    {
      Thread *thread = thread_list_node_->get_data();
      char *thread_base = (char *)thread->get_pthread();
      Worker *worker = nullptr;
      if (OB_NOT_NULL(thread_base)) {
        GET_OTHER_TSI_ADDR(worker, &Worker::self_);
        if (OB_NOT_NULL(worker) && OB_NOT_NULL(GCTX.cgroup_ctrl_) && GCTX.cgroup_ctrl_->is_valid() &&
            OB_FAIL(GCTX.cgroup_ctrl_->add_thread_to_cgroup_(thread->get_tid(), id_))) {
          LOG_WARN("regist thread to cgroup failed",
              K(ret),
              K(thread->get_tid()),
              K(id_),
              KP(worker),
              K(worker->get_group_id()));
        }
      }
    }
#endif
    LOG_INFO("regist threads to cgroup from thread list", K(ret), K(id_), K(thread_list_.get_size()));
    thread_list_lock_.unlock();
  }
}

void ObTenant::handle_retry_req(bool need_clear)
{
  int ret = OB_SUCCESS;
  ObLink* task = nullptr;
  ObRequest *req = NULL;
  // even if ret != OB_SUCCESS, the loop must continue to pop all requests
  while (OB_SUCC(retry_queue_.pop(task, need_clear))) {
    // if pop returns OB_SUCCESS, then the task must not be NULL.
    req = static_cast<rpc::ObRequest*>(task);
    if (req->large_retry_flag()) {
      if (OB_FAIL(recv_request(*req))) {
        LOG_WARN("tenant patrol push req into large_query queue fail, "
            "and the req well be destroyed", "tenant_id", id_, "req", *req, K(ret));
        on_translate_fail(req, ret);
      }
    } else {
      if (OB_FAIL(recv_request(*req))) {
        LOG_WARN("tenant patrol push req into common queue fail, "
            "and the req well be destroyed", "tenant_id", id_, "req", *req, K(ret));
        on_translate_fail(req, ret);
      }
    }
  }
}

void ObTenant::check_worker_count()
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

// This interface is unnecessary after adding htap
int ObTenant::acquire_more_worker(int64_t num, int64_t &succ_num, bool force)
{
  int ret = OB_SUCCESS;
  succ_num = 0;

  ObTenantSwitchGuard guard(this);
  while (OB_SUCC(ret) && num > succ_num) {
    ObThWorker *w = nullptr;
    if (OB_FAIL(create_worker(w, this))) {
      LOG_WARN("create worker failed", K(ret));
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

bool ObTenant::do_add_worker()
{
  int64_t succ_num = 0;
  int ret = acquire_more_worker(1, succ_num);
  if (OB_FAIL(ret) || succ_num != 1) {
    LOG_WARN("do_add_worker failed", K(ret), K(succ_num),
             "max_worker_cnt", max_worker_cnt());
  }
  return OB_SUCCESS == ret && succ_num == 1;
}

int64_t ObTenant::get_cpu_time() const
{
#ifdef _WIN32
  FILETIME creation, exit, kernel, user;
  if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) {
    return 0;
  }
  auto filetime_to_us = [](const FILETIME &ft) -> int64_t {
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    return static_cast<int64_t>(u.QuadPart / 10); // 100ns -> us
  };
  return filetime_to_us(user) + filetime_to_us(kernel);
#else
  struct rusage usage;
  if (getrusage(RUSAGE_SELF, &usage) != 0) {
    return 0;
  }
  return (int64_t)usage.ru_utime.tv_sec * 1000000LL + usage.ru_utime.tv_usec
       + (int64_t)usage.ru_stime.tv_sec * 1000000LL + usage.ru_stime.tv_usec;
#endif
}

void ObTenant::periodically_check()
{
  int ret = OB_SUCCESS;
  WITH_ENTITY(ctx_) {
    check_parallel_servers_target();
    check_dtl();
    check_px_thread_recycle();
  }
}

void ObTenant::check_dtl()
{
  int ret = OB_SUCCESS;
  if (is_virtual_tenant_id(id_)) {
    // Except for system rentals, internal tenants do not allocate px threads
  } else {
    ObTenantSwitchGuard guard(this);
    auto tenant_dfc = MTL(ObTenantDfc*);
    if (OB_NOT_NULL(tenant_dfc)) {
      tenant_dfc->check_dtl(id_);
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to switch to tenant", K(id_), K(ret));
    }
  }
}

void ObTenant::check_parallel_servers_target()
{
  int ret = OB_SUCCESS;
  int64_t val = 0;
  if (is_virtual_tenant_id(id_)) {
    // Except for system rentals, internal tenants do not allocate px threads
  } else if (OB_FAIL(ObSchemaUtils::get_tenant_int_variable(
              id_,
              SYS_VAR_PARALLEL_SERVERS_TARGET,
              val))) {
    LOG_WARN("fail read tenant variable", K_(id), K(ret));
  } else if (OB_FAIL(OB_PX_TARGET_MGR.set_parallel_servers_target(id_, val))) {
    LOG_WARN("set parallel_servers_target failed", K(ret), K(id_), K(val));
  }
}

void ObTenant::check_px_thread_recycle()
{
  int ret = OB_SUCCESS;
  if (is_virtual_tenant_id(id_)) {
    // Except for system rentals, internal tenants do not allocate px threads
  } else {
    ObTenantSwitchGuard guard(this);
    auto px_pools = MTL(ObPxPools*);
    if (OB_NOT_NULL(px_pools)) {
      px_pools->thread_recycle();
    } else {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("failed to switch to tenant", K(id_), K(ret));
    }
  }
}

void ObTenant::on_schema_publish()
{
  int ret = OB_SUCCESS;
  ObChangeStreamMgr *mgr = get<ObChangeStreamMgr *>();
  if (OB_NOT_NULL(mgr) && mgr->is_inited()) {
    mgr->get_fetcher().notify_schema_changed();
  }
}
