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


#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_multi_tenant.h"
#include "storage/tx_storage/ob_tenant_freezer.h"  // previously hidden behind the allocator_mgr.h include chain, make the dependency explicit
#include "logservice/ob_log_service.h"  // ObLogService complete type, previously hidden behind a transitive include, make the dependency explicit
#include "share/rc/ob_module_provider.h"
#include "observer/ob_server.h"
#include "ob_tenant.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "share/schema/ob_tenant_schema_service.h"
#include "observer/mysql/obsm_conn_callback.h"
#include "sql/dtl/ob_dtl_fc_server.h"
#include "sql/das/ob_das_id_service.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"   // ObSharedMemAllocMgr
#include "share/ob_global_autoinc_service.h"
#include "ob_tenant_mtl_helper.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "storage/tx/ob_tx_loop_worker.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx/ob_timestamp_access.h"
#include "storage/tx/ob_trans_id_service.h"
#include "storage/tx/ob_unique_id_service.h"
#include "storage/tx/ob_trans_part_ctx.h"
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "storage/tx_storage/ob_checkpoint_service.h"
#include "storage/tx_storage/ob_tenant_memory_printer.h"
#include "storage/compaction/ob_tenant_compaction_progress.h"
#include "storage/compaction/ob_server_compaction_event_history.h"
#include "storage/memtable/ob_lock_wait_mgr.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_tenant_storage_meta_service.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "storage/compaction/ob_sstable_merge_info_mgr.h" // ObTenantSSTableMergeInfoMgr
#include "observer/scheduler/ob_dag_warning_history_mgr.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "observer/omt/ob_tenant_srs.h"
#include "observer/report/ob_tenant_meta_checker.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h" // ObDDLScheduler
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "observer/ob_sys_tenant_load_sys_package_service.h" // for ObSysTenantLoadSysPackageService
#include "observer/dbms_scheduler/ob_dbms_sched_service.h" // ObDBMSSchedService
#include "storage/blocksstable/ob_shared_macro_block_manager.h"
#include "observer/table_load/ob_table_load_service.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "storage/access/ob_empty_read_bucket.h"
#include "storage/fts/dict/ob_ft_dict_mgr.h"
#ifdef ERRSIM
#include "share/errsim_module/ob_tenant_errsim_module_mgr.h"
#include "share/errsim_module/ob_tenant_errsim_event_mgr.h"
#endif
#include "observer/ob_server_event_history_table_operator.h"
#include "share/index_usage/ob_index_usage_info_mgr.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "rootserver/mview/ob_mview_maintenance_service.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "observer/change_stream/ob_change_stream_mgr.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "observer/scheduler/ob_partition_auto_split_helper.h"
#include "observer/mysql/ob_query_response_time.h" //ObTenantQueryRespTimeCollector
#include "lib/resource/ob_affinity_ctrl.h"
#include "sql/ob_sql_ccl_rule_manager.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"
#include "observer/omt/ob_tenant_ai_service.h"
#include "share/resource_manager/ob_resource_plan_manager.h"  // relocated-definition owner
#include "storage/allocator/ob_memstore_allocator.h"  // relocated-definition owner
#include "share/io/ob_io_manager.h"  // relocated-definition owner
// collapsed-from-ObTenantNodeBalancer sys-tenant bring-up/refresh dependencies
#include "share/unit/ob_unit_config.h"                       // ObUnitConfig::gen_sys_tenant_unit_config
#include "logservice/ob_tenant_mutil_allocator_mgr.h"        // TMA_MGR_INSTANCE
#include "share/resource_manager/ob_resource_manager.h"      // G_RES_MGR / ObResourcePlanManager
#include "logservice/ob_server_log_block_mgr.h"              // GCTX.log_block_mgr_
#include "common/ob_tenant_data_version_mgr.h"               // ODV_MGR

using namespace oceanbase;
using namespace oceanbase::lib;
using namespace oceanbase::common;
using namespace oceanbase::omt;
using namespace oceanbase::rpc;
using namespace oceanbase::share;
using namespace oceanbase::storage;
using namespace oceanbase::storage::checkpoint;
using namespace oceanbase::obmysql;
using namespace oceanbase::sql;
namespace oceanbase { namespace omt { int refresh_global_background_cpu(share::ObResourcePlanManager &mgr); } }
using namespace oceanbase::sql::dtl;
using namespace oceanbase::concurrency_control;
using namespace oceanbase::transaction;
using namespace oceanbase::transaction::tablelock;
using namespace oceanbase::logservice;
using namespace oceanbase::observer;
using namespace oceanbase::rootserver;
using namespace oceanbase::blocksstable;
using namespace oceanbase::tmp_file;
using namespace oceanbase::table;


namespace oceanbase
{
namespace share
{
// Declared in share/ob_context.h
// Obtain tenant_ctx according to tenant (obtained from omt)
int __attribute__ ((weak)) get_tenant_ctx_with_tenant_lock(ObTenantSpace *&tenant_ctx)
{
  int ret = OB_SUCCESS;
  tenant_ctx = nullptr;

  omt::ObTenant *tenant = nullptr;
  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret));
  } else if (OB_FAIL(GCTX.omt_->get_tenant_with_tenant_lock(tenant))) {
    if (REACH_TIME_INTERVAL(1000 * 1000)) {
      LOG_WARN("get tenant from omt failed", K(ret));
    }
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret));
  } else {
    tenant_ctx = &tenant->ctx();
  }

  return ret;
}

int __attribute__ ((weak)) get_tenant_base_with_lock(
    ObTenantBase *&tenant_base, ReleaseCbFunc &release_cb)
{
  int ret = OB_SUCCESS;
  omt::ObTenant *tenant = nullptr;
  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret));
  } else if (OB_FAIL(GCTX.omt_->get_tenant_with_tenant_lock(tenant))) {
    if (REACH_TIME_INTERVAL(1000 * 1000)) {
      LOG_WARN("get tenant from omt failed", K(ret));
    }
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("null ptr", K(ret));
  } else {
    tenant_base = static_cast<ObTenantBase*>(tenant);
    release_cb = [tenant] () {
      return tenant->unlock();
    };
  }
  return ret;
}
} // end of namespace share
} // end of namespace oceanbase

ObMultiTenant::ObMultiTenant()
    : is_inited_(false),
      tenant_(nullptr),
      refresh_interval_(10L * 1000L * 1000L),
      myaddr_(),
      cpu_dump_(false),
      has_synced_(false),
      tenant_active_(false),
      timer_tg_id_(-1),
      timer_stopped_(true),
      tenant_limiter_head_(NULL),
      limiter_mutex_()

{
  if (lib::is_mini_mode()) {
    refresh_interval_ /= 2;
  }
}

static int init_compat_mode(lib::Worker::CompatMode &compat_mode)
{
  int ret = OB_SUCCESS;
  compat_mode = lib::Worker::CompatMode::MYSQL;
  return ret;
}


template<typename T>
static int server_obj_pool_mtl_new(common::ObServerObjectPool<T> *&pool)
{
  int ret = common::OB_SUCCESS;
  pool = MTL_NEW(common::ObServerObjectPool<T>, "TntSrvObjPool", false/*regist*/,
                 MTL_IS_MINI_MODE(), MTL_CPU_COUNT());
  if (OB_ISNULL(pool)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ret = pool->init();
  }
  return ret;
}

template<typename T>
static void server_obj_pool_mtl_destroy(common::ObServerObjectPool<T> *&pool)
{
  using Pool = common::ObServerObjectPool<T>;
  MTL_DELETE(Pool, "TntSrvObjPool", pool);
  pool = nullptr;
}

// lob-read domain port(common::ObILobReadService) MTL injection:
// non-owning alias pointer, points at the same tenant's already-created ObLobManager(with its own lifetime),
// so new/destroy are no-ops。it must be bound after ObLobManager bind to ensure it is bound after, so it is ready during init。
static int lob_read_service_mtl_new(common::ObILobReadService *&svc)
{
  svc = nullptr;
  return common::OB_SUCCESS;
}

int ObMultiTenant::init(ObAddr myaddr,
                        ObMySQLProxy *sql_proxy,
                        bool mtl_bind_flag)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObMultiTenant has been inited", K(ret));
  } else {
    myaddr_ = myaddr;
    // Single sys tenant: bring-up + periodic refresh are sourced directly from
    // GCONF inside ObMultiTenant (was ObTenantNodeBalancer + ObUnitInfoGetter).
    UNUSED(sql_proxy);
  }

  // Per-module bind registration removed (ObServer owns + brings up modules
  // via explicit obs_construct/init/start/stop/wait/destroy routines).
  UNUSED(mtl_bind_flag);

  if (OB_SUCC(ret)) {
    is_inited_ = true;
    LOG_INFO("succ to init multi tenant");
  }
  return ret;
}

int ObMultiTenant::start()
{
  int ret = OB_SUCCESS;

  ObTenantMemoryPrinter &printer = ObTenantMemoryPrinter::get_instance();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(create_virtual_tenants())) {
    LOG_ERROR("create virtual tenants failed", K(ret));
  } else if (OB_FAIL(TG_CREATE(lib::TGDefIDs::MultiTenantTimer, timer_tg_id_))) {
    LOG_ERROR("create multi tenant timer failed", K(ret));
  } else if (OB_FAIL(TG_START(timer_tg_id_))) {
    LOG_ERROR("start multi tenant timer failed", K(ret), K_(timer_tg_id));
  } else if (OB_FAIL(TG_SCHEDULE(timer_tg_id_, *this, TIME_SLICE_PERIOD, true/*is_repeat*/))) {
    LOG_ERROR("schedule multi tenant timer failed", K(ret), K_(timer_tg_id));
  // start memstore print timer.
  } else if (OB_FAIL(printer.register_timer_task(lib::TGDefIDs::ServerGTimer))) {
    LOG_ERROR("Fail to register timer task", K(ret));
  } else {
    timer_stopped_ = false;
    LOG_INFO("succ to start multi tenant");
  }


  if (OB_FAIL(ret)) {
    stop();
  }
  return ret;
}

void ObMultiTenant::stop()
{
  if (!timer_stopped_ && timer_tg_id_ != -1) {
    TG_STOP(timer_tg_id_);
    timer_stopped_ = true;
  }
  remove_tenant();
}

void ObMultiTenant::wait()
{
  if (OB_NOT_NULL(tenant_)) {
    while (OB_EAGAIN == tenant_->try_wait()) {
      usleep(50 * 1000);
    }
  }
  if (timer_tg_id_ != -1) {
    TG_WAIT(timer_tg_id_);
  }
}


void ObMultiTenant::destroy()
{
  if (OB_NOT_NULL(tenant_)) {
    tenant_->destroy();
  }
  if (timer_tg_id_ != -1) {
    TG_DESTROY(timer_tg_id_);
    timer_tg_id_ = -1;
  }
  is_inited_ = false;
}

int ObMultiTenant::construct_meta_for_hidden_sys(ObTenantMeta &meta)
{
  int ret = OB_SUCCESS;

  
  ObTenantSuperBlock super_block(true/*is_hidden*/);
  share::ObUnitInfoGetter::ObTenantConfig unit;
  const bool has_memstore = true;
  const int64_t create_timestamp = ObTimeUtility::current_time();
  uint64_t unit_id = 1000;

  share::ObUnitConfig unit_config;
  const bool is_hidden_sys = true;
  int64_t hidden_sys_data_disk_config_size = 0;
  if (OB_FAIL(unit_config.gen_sys_tenant_unit_config(is_hidden_sys, GCTX.log_block_mgr_->get_log_disk_size()))) {
    LOG_WARN("gen sys tenant unit config fail", KR(ret), K(is_hidden_sys));
  } else if (OB_FAIL(unit.init(unit_id,
                        share::ObUnitInfoGetter::ObUnitStatus::UNIT_NORMAL,
                        unit_config,
                        lib::Worker::CompatMode::MYSQL,
                        create_timestamp,
                        has_memstore,
                        false /*is_removed*/,
                        hidden_sys_data_disk_config_size,
                        0 /*actual_data_disk_size*/))) {
    LOG_WARN("fail to init hidden sys tenant unit", K(ret));
  } else if (OB_FAIL(meta.build(unit, super_block))) {
    LOG_WARN("fail to build tenant meta", K(ret));
  }

  return ret;
}

int ObMultiTenant::create_hidden_sys_tenant()
{
  int ret = OB_SUCCESS;
  ObTenantMeta meta;
  if (OB_FAIL(construct_meta_for_hidden_sys(meta))) {
    LOG_ERROR("fail to construct meta", K(ret));
  } else if (OB_FAIL(create_tenant(meta, true /* write_slog */))) {
    LOG_ERROR("create hidden sys tenant failed", K(ret));
  }
  return ret;
}

int ObMultiTenant::update_hidden_sys_tenant()
{
  int ret = OB_SUCCESS;
  
  omt::ObTenant *tenant = nullptr;
  SMART_VAR(ObTenantMeta, meta) {
    if (OB_FAIL(get_tenant_unsafe(tenant))) { // sys tenant will not be deleted
      LOG_WARN("failed to get sys tenant", K(ret));
    } else if (OB_FAIL(construct_meta_for_hidden_sys(meta))) {
      LOG_ERROR("fail to construct meta", K(ret));
    } else if (!tenant->is_hidden() || meta.unit_ == tenant->get_unit()) {
      // do nothing
    } else if (OB_FAIL(update_tenant_unit_no_lock(meta.unit_))) {
      LOG_WARN("fail to update tenant unit", K(ret));
    }
  }
  return ret;
}

int ObMultiTenant::create_virtual_tenants()
{
  int ret = OB_SUCCESS;
  // init allocator for OB_SERVER_TENANT_ID
  ObMallocAllocator *allocator = ObMallocAllocator::get_instance();
  if (!OB_ISNULL(allocator)) {
    allocator->set_tenant_limit(INT64_MAX);
  }

  return ret;
}

int ObMultiTenant::convert_hidden_to_real_sys_tenant(const ObUnitInfoGetter::ObTenantConfig &unit,
                                                     const int64_t abs_timeout_us)
{
  int ret = OB_SUCCESS;

  ObTenant *tenant = nullptr;
  const double min_cpu = static_cast<double>(unit.config_.min_cpu());
  const double max_cpu = static_cast<double>(unit.config_.max_cpu());
  
  int64_t allowed_mem_limit = 0;
  UNUSED(abs_timeout_us);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get sys tenant", K(ret));
  } else if (!tenant->is_hidden()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("must be hidden sys tenant", K(ret));
  } else {
    HEAP_VAR(ObTenantSuperBlock, new_super_block) {
      new_super_block = tenant->get_super_block();
      new_super_block.is_hidden_ = false;
      if (OB_FAIL(update_tenant_unit_no_lock(unit))) {
        LOG_WARN("fail to update_tenant_unit_no_lock", K(ret), K(unit));
      } else if (OB_FAIL(SERVER_STORAGE_META_PERSISTER.update_tenant_super_block(
          tenant->get_epoch(), new_super_block))) {
        LOG_WARN("fail to update tenant super block", K(ret), K(new_super_block));
      } else {
        tenant->set_tenant_super_block(new_super_block);
        // clear sys tenant prepare gc state
        tenant->clear_prepare_unit_gc();
      }
    }
  }

  FLOG_INFO("finish convert_hidden_to_real_sys_tenant", K(ret));

  return ret;
}

#ifdef ENABLE_DEBUG_LOG
ERRSIM_POINT_DEF(ERRSIM_CREATE_TENANT_FAILURE)
#endif

int ObMultiTenant::create_tenant(const ObTenantMeta &meta, bool write_slog, const int64_t abs_timeout_us)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  const double min_cpu = static_cast<double>(meta.unit_.config_.min_cpu());
  const double max_cpu = static_cast<double>(meta.unit_.config_.max_cpu());
  
  ObTenant *tenant = nullptr;
  ObMallocAllocator *malloc_allocator = ObMallocAllocator::get_instance();
  ObTenantCreateStep create_step = ObTenantCreateStep::STEP_BEGIN;  // step0
  const int64_t log_disk_size = meta.unit_.config_.log_disk_size();
  const int64_t effective_data_disk_size = meta.unit_.get_effective_actual_data_disk_size();
  int64_t tenant_epoch = meta.epoch_;
  UNUSED(abs_timeout_us);

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_UNLIKELY(!meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(ret), K(meta));
  } else if (OB_ISNULL(malloc_allocator)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("malloc allocator is NULL", K(ret));
  } else if (OB_SUCC(get_tenant_unsafe(tenant))) {
    ret = OB_TENANT_EXIST;
    LOG_WARN("tenant exist", K(ret));
  } else {
    ret = OB_SUCCESS;
  }

  bool tenant_allocator_created = false;
  int64_t memory_size = GMEMCONF.get_server_memory_limit();
  int64_t hard_memory_size = GMEMCONF.get_server_hard_memory_limit();
  if (OB_SUCC(ret)) {
    if (OB_FAIL(malloc_allocator->create_and_add_tenant_allocator())) {
      LOG_ERROR("create and add tenant allocator failed", K(ret));
    } else {
      tenant_allocator_created = true;
    }
    if (OB_SUCC(ret)) {
      lib::set_memory_limit(memory_size);
      if (OB_FAIL(update_tenant_memory(hard_memory_size))) {
        LOG_WARN("fail to update tenant memory", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    create_step = ObTenantCreateStep::STEP_CTX_MEM_CONFIG_SETTED; // step1
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_ISNULL(GCTX.cgroup_ctrl_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("group ctrl not init", K(ret));
  } else if (write_slog) {
    if (OB_FAIL(SERVER_STORAGE_META_PERSISTER.prepare_create_tenant(meta, tenant_epoch))) {
      LOG_ERROR("fail to write create tenant prepare slog", K(ret));
    } else {
      create_step = ObTenantCreateStep::STEP_CREATION_PREPARED; // step4
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(tenant_ = OB_NEW(
      ObTenant, ObModIds::OMT, tenant_epoch, GCONF.workers_per_cpu_quota.get_value(), *GCTX.cgroup_ctrl_))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("new tenant fail", K(ret));
  } else if (FALSE_IT(create_step = ObTenantCreateStep::STEP_TENANT_NEWED)) { //step5
  } else if (OB_FAIL(tenant_->init_ctx())) {
    LOG_WARN("init ctx fail", K(ret));
  } else {
    CREATE_WITH_TEMP_ENTITY(RESOURCE_OWNER, tenant_->id()) {
      WITH_ENTITY(&tenant_->ctx()) {
        if (OB_FAIL(tenant_->init(meta))) {
          LOG_ERROR("init tenant fail", K(ret));
        }
      }
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    ObTenantSwitchGuard guard(tenant_);
    if (OB_FAIL(share::g_mp->tenant_freezer()->set_tenant_mem_limit(meta.unit_.config_.memory_size(), memory_size))) {
      LOG_WARN("fail to set_tenant_mem_limit", K(ret));
    }
  }
  if (OB_SUCC(ret)) {
    if (write_slog && OB_FAIL(SERVER_STORAGE_META_PERSISTER.commit_create_tenant(tenant_epoch))) {
      LOG_ERROR("fail to write create tenant commit slog", K(ret));
    } else {
      tenant_->set_create_status(ObTenantCreateStatus::CREATED);
      create_step = ObTenantCreateStep::STEP_FINISH; // step6
    }
  }

  tenant_active_ = true;
  // TODO: @lingyang Expected not to fail
  if (OB_TMP_FAIL(update_tenant_config())) {
    LOG_WARN("update tenant config fail", K(tmp_ret));
  }

#ifdef ENABLE_DEBUG_LOG
  ret = ERRSIM_CREATE_TENANT_FAILURE ? ERRSIM_CREATE_TENANT_FAILURE : ret;
#endif

  if (OB_FAIL(ret)) {
    do {
      tmp_ret = OB_SUCCESS;
      if (create_step >= ObTenantCreateStep::STEP_TENANT_NEWED) {
        if (OB_NOT_NULL(tenant_)) {
          tenant_->stop();
          while (OB_SUCCESS != tenant_->try_wait()) {
            ob_usleep(100 * 1000);
          }
          tenant_->destroy();
          ob_delete(tenant_);
          tenant_ = nullptr;
        }
        // no need rollback when replaying slog and creating a virtual tenant,
        // in which two case the write_slog flag is set to false
        if (write_slog && OB_SUCCESS != (tmp_ret = SERVER_STORAGE_META_PERSISTER.clear_tenant_log_dir())) {
          LOG_ERROR("fail to clear persistent data", K(tmp_ret));
          SLEEP(1);
        }
      }
    } while (OB_SUCCESS != tmp_ret);

    do {
      tmp_ret = OB_SUCCESS;
      if (create_step >= ObTenantCreateStep::STEP_CTX_MEM_CONFIG_SETTED) {
        for (uint64_t ctx_id = 0; ctx_id < ObCtxIds::MAX_CTX_ID; ctx_id++) {
          if (NULL == malloc_allocator->get_tenant_ctx_allocator(ctx_id)) {
            // do-nothing
          } else if (OB_SUCCESS != (tmp_ret = malloc_allocator->set_tenant_ctx_idle(ctx_id, 0))) {
            LOG_ERROR("fail to cleanup ctx mem config", K(tmp_ret), K(ctx_id));
            SLEEP(1);
          }
        }
      }
    } while (OB_SUCCESS != tmp_ret);

    // no need rollback when replaying slog and creating a virtual tenant,
    // in which two cases the write_slog flag is set to false
    if (write_slog && create_step >= ObTenantCreateStep::STEP_CREATION_PREPARED) {
      if (OB_SUCCESS != (tmp_ret = SERVER_STORAGE_META_PERSISTER.abort_create_tenant( tenant_epoch))) {
        LOG_ERROR("fail to write create tenant abort slog", K(tmp_ret));
      }
    }
  }

  if (OB_FAIL(ret) && tenant_allocator_created) {
    auto& cache_washer = ObKVGlobalCache::get_instance();
    if (OB_TMP_FAIL(cache_washer.sync_flush_tenant())) {
      LOG_WARN("Fail to sync flush tenant cache", K(tmp_ret));
    }
    malloc_allocator->recycle_tenant_allocator();
  }

  FLOG_INFO("finish create new tenant", K(ret), K(write_slog), K(create_step));

  return ret;
}

int ObMultiTenant::update_tenant_unit_no_lock(const ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;
  // serialize unit-config writers (boot hidden-window: OMT timer apply vs config reload)
  lib::ObMutexGuard guard(unit_conf_lock_);

  ObTenant *tenant = nullptr;
  const double min_cpu = GCONF.get_sys_tenant_default_min_cpu();
  const double max_cpu = GCONF.get_sys_tenant_default_max_cpu();
  const int64_t log_disk_size =  GCTX.log_block_mgr_->get_log_disk_size();
  
  ObUnitInfoGetter::ObTenantConfig allowed_new_unit;
  ObUnitInfoGetter::ObTenantConfig old_unit;
  int64_t allowed_new_log_disk_size = 0;
  bool need_persist_unit = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tenant", K(ret));
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant is nullptr");
  } else if (OB_FAIL(old_unit.assign(tenant->get_unit()))) {
    LOG_ERROR("fail to assign old unit failed", K(unit));
  } else if (OB_FAIL(update_tenant_log_disk_size(old_unit.config_.log_disk_size(),
                                                 log_disk_size,
                                                 allowed_new_log_disk_size))) {
    LOG_WARN("fail to update tenant log disk size", K(ret));
  } else if (OB_FAIL(construct_allowed_unit_config(allowed_new_log_disk_size,
                                                   max_cpu, min_cpu,
                                                   unit,
                                                   allowed_new_unit))) {
    LOG_WARN("fail to construct_allowed_unit_config", K(allowed_new_log_disk_size),
             K(allowed_new_unit));
  } else if (FALSE_IT(need_persist_unit = !(old_unit == allowed_new_unit))) {
  } else if (need_persist_unit
             && OB_FAIL(SERVER_STORAGE_META_PERSISTER.update_tenant_unit(tenant->get_epoch(), allowed_new_unit))) {
    LOG_WARN("fail to update tenant unit", K(ret));
  } else if (OB_FAIL(tenant->update_thread_cnt(max_cpu))) {
    LOG_WARN("fail to update mtl module thread_cnt", K(ret));
  } else {
    if (tenant->unit_min_cpu() != min_cpu) {
      tenant->set_unit_min_cpu(min_cpu);
      set_req_chunkmgr_parallel(ObCtxIds::DEFAULT_CTX_ID, min_cpu * 8);
    }
    if (tenant->unit_max_cpu() != max_cpu) {
      tenant->set_unit_max_cpu(max_cpu);
    }
    tenant->set_tenant_unit(allowed_new_unit);
    LOG_INFO("succecc to set tenant unit config", K(need_persist_unit), K(allowed_new_unit));
  }

  return ret;
}

int ObMultiTenant::update_tenant_memory(const ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  
  int64_t memory_size = GMEMCONF.get_server_memory_limit();
  int64_t hard_memory_size = GMEMCONF.get_server_hard_memory_limit();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tenant", K(ret));
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant is nullptr");
  } else if (FALSE_IT(lib::set_memory_limit(memory_size))) {
    // unreachable
  } else if (OB_FAIL(update_tenant_memory(hard_memory_size))) {
    LOG_WARN("fail to update tenant memory", K(ret));
  } else if (OB_FAIL(update_tenant_freezer_mem_limit( memory_size, memory_size))) {
    LOG_WARN("fail to update_tenant_freezer_mem_limit", K(ret));
  } else if (OB_FAIL(update_throttle_config_())) {
    LOG_WARN("update throttle config failed", K(ret));
  } else if (FALSE_IT(tenant->set_unit_memory_size(memory_size))) {
    // unreachable
  }
  return ret;
}

int ObMultiTenant::construct_allowed_unit_config(const int64_t allowed_new_log_disk_size,
                                                 const int64_t max_cpu, const int64_t min_cpu,
                                                 const ObUnitInfoGetter::ObTenantConfig &expected_unit_config,
                                                 ObUnitInfoGetter::ObTenantConfig &allowed_new_unit)
{
  int ret = OB_SUCCESS;
  if (0 > allowed_new_log_disk_size
      || !expected_unit_config.is_valid()) {
    ret= OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(allowed_new_unit.assign(expected_unit_config))) {
    LOG_ERROR("fail to assign new unit", K(allowed_new_log_disk_size), K(expected_unit_config));
  } else {
    // construct allowed resource.
    ObUnitResource allowed_resource(
        max_cpu,
        min_cpu,
        expected_unit_config.config_.memory_size(),
        allowed_new_log_disk_size,
        expected_unit_config.config_.data_disk_size(),
        expected_unit_config.config_.max_iops(),
        expected_unit_config.config_.min_iops(),
        expected_unit_config.config_.iops_weight(),
        expected_unit_config.config_.max_net_bandwidth(),
        expected_unit_config.config_.net_bandwidth_weight());
    if (OB_FAIL(allowed_new_unit.config_.update_unit_resource(allowed_resource))) {
      LOG_WARN("update_unit_resource failed", K(allowed_new_log_disk_size), K(allowed_new_unit),
               K(allowed_resource));
    }
  }
  return ret;
}

int ObMultiTenant::update_tenant_unit(const ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(update_tenant_unit_no_lock(unit))) {
    LOG_WARN("fail to update_tenant_unit_no_lock", K(ret), K(unit));
  }

  LOG_INFO("OMT finish update tenant unit config", K(ret), K(unit));

  return ret;
}

// hard memory limit need be safely scaled down
int ObMultiTenant::update_tenant_memory(const int64_t mem_limit)
{
  int ret = OB_SUCCESS;
  ObMallocAllocator *malloc_allocator = ObMallocAllocator::get_instance();

  int64_t allowed_mem_limit = mem_limit;
  const int64_t pre_mem_limit = malloc_allocator->get_tenant_hard_limit();
  const int64_t mem_hold = malloc_allocator->get_tenant_hold();
  const int64_t target_mem_limit = mem_limit;

  if (OB_SUCC(ret)) {
    // make sure half reserve memory available
    if (target_mem_limit < pre_mem_limit) {
      allowed_mem_limit = mem_hold + static_cast<int64_t>(
          static_cast<double>(target_mem_limit) * TENANT_RESERVE_MEM_RATIO / 2.0);
      if (allowed_mem_limit < target_mem_limit) {
        allowed_mem_limit = target_mem_limit;
      }
      if (allowed_mem_limit < pre_mem_limit) {
        LOG_INFO("reduce memory quota", K(mem_limit), K(pre_mem_limit), K(target_mem_limit), K(mem_hold));
      } else {
        allowed_mem_limit = pre_mem_limit;
        LOG_WARN("try to reduce memory quota, but free memory not enough",
                 K(allowed_mem_limit), K(pre_mem_limit), K(target_mem_limit), K(mem_hold));
      }
    }

    if (allowed_mem_limit != pre_mem_limit) {
      lib::set_hard_memory_limit(allowed_mem_limit);
    }
  }

  return ret;
}

int ObMultiTenant::update_tenant_log_disk_size(const int64_t old_log_disk_size,
                                               const int64_t new_log_disk_size,
                                               int64_t &allowed_new_log_disk_size)
{
  int ret = OB_SUCCESS;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  if (OB_SUCC(guard.switch_to())) {
    ObLogService *log_service = share::g_mp->log_service();
    if (OB_ISNULL(log_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get log_service failed", K(ret));
    } else if (OB_FAIL(GCTX.log_block_mgr_->update_tenant(old_log_disk_size, new_log_disk_size,
                                                          allowed_new_log_disk_size, log_service))) {
      LOG_WARN("fail to update_tenant", K(old_log_disk_size), K(new_log_disk_size),
               K(allowed_new_log_disk_size));
    } else {
      LOG_INFO("update_tenant_log_disk_size success", K(old_log_disk_size),
               K(new_log_disk_size), K(allowed_new_log_disk_size));
    }
  } else {
    LOG_WARN("switch to tenant failed", K(ret));
  }
  return ret;
}


int ObMultiTenant::update_tenant_config()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (false == true) {
  } else {
    MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
    if (OB_SUCC(guard.switch_to())) {
      if (OB_TMP_FAIL(update_palf_config())) {
        LOG_WARN("failed to update palf disk config", K(tmp_ret));
      }
      if (OB_TMP_FAIL(update_tenant_dag_scheduler_config())) {
        LOG_WARN("failed to update tenant dag scheduler config", K(tmp_ret));
      }
      if (OB_TMP_FAIL(update_tenant_ddl_config())) {
        LOG_WARN("failed to update tenant ddl config", K(tmp_ret));
      }
      if (OB_TMP_FAIL(update_tenant_freezer_config_())) {
        LOG_WARN("failed to update tenant tenant freezer config", K(tmp_ret));
      }
      if (OB_TMP_FAIL(update_throttle_config_())) {
        LOG_WARN("update throttle config failed", K(ret));
      }
      if (OB_TMP_FAIL(update_tenant_query_response_time_flush_config())) {
        LOG_WARN("failed to update tenant query response time flush config", K(tmp_ret));
      }
    }
  }
  LOG_INFO("update_tenant_config success");
  return ret;
}

int ObMultiTenant::update_palf_config()
{
  int ret = OB_SUCCESS;
  ObLogService *log_service = share::g_mp->log_service();
  if (NULL == log_service) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = log_service->update_palf_options_except_disk_usage_limit_size();
  }
  return ret;
}

int ObMultiTenant::update_tenant_dag_scheduler_config()
{
  int ret = OB_SUCCESS;
  ObTenantDagScheduler *dag_scheduler = share::g_mp->tenant_dag_scheduler();
  if (OB_FAIL(ret)) {
    // do nothing
  } else if (OB_ISNULL(dag_scheduler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag scheduler should not be null", K(ret));
  } else {
    dag_scheduler->reload_config();
  }
  return ret;
}

int ObMultiTenant::update_tenant_ddl_config()
{
  int ret = OB_SUCCESS;
  
#ifdef ERRSIM

  if (OB_FAIL(ObDDLSimPointMgr::get_instance().set_tenant_param(GCONF.errsim_ddl_sim_point_random_control,
                                                                GCONF.errsim_ddl_sim_point_fixed_list))) {
    LOG_WARN("set tenant param for ddl sim point failed", K(ret),
        K(GCONF.errsim_ddl_sim_point_random_control), K(GCONF.errsim_ddl_sim_point_fixed_list));
  }

#endif
  return ret;
}

int ObMultiTenant::update_tenant_freezer_config_()
{
  int ret = OB_SUCCESS;
  ObTenantFreezer *freezer = share::g_mp->tenant_freezer();
  if (NULL == freezer) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant freezer should not be null", K(ret));
  } else if (OB_FAIL(freezer->reload_config())) {
    LOG_WARN("tenant freezer config update failed", K(ret));
  }
  return ret;
}

int ObMultiTenant::update_throttle_config_()
{
  int ret = OB_SUCCESS;
  {
    MOD_SCOPE {
      ObSharedMemAllocMgr *share_mem_alloc_mgr = share::g_mp->shared_mem_alloc_mgr();

      if (OB_ISNULL(share_mem_alloc_mgr)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("share mem alloc mgr should not be null", K(ret));
      } else {
        (void)share_mem_alloc_mgr->update_throttle_config();
      }
    }
  }
  return ret;
}

int ObMultiTenant::update_tenant_query_response_time_flush_config()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("sql proxy is null", K(ret));
  } else {
    int64_t flush_version = 0;
    if (OB_SUCC(ret)) {
      observer::ObTenantQueryRespTimeCollector *t_query_resp_time_collector = share::g_mp->tenant_query_resp_time_collector();
      if (OB_FAIL(ret)) {
        // do nothing
      } else if (OB_ISNULL(t_query_resp_time_collector)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("t_query_resp_time_collector should not be null", K(ret));
      } else if (flush_version > t_query_resp_time_collector->get_flush_config_version()) {
        if (!true) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tenant config is invalid",K(ret));
        } else if (GCONF.query_response_time_flush) {
          if (OB_FAIL(t_query_resp_time_collector->flush())) {
            LOG_WARN("failed to refresh tenant query response time", K(ret));
          } else {
            t_query_resp_time_collector->set_flush_config_version(flush_version);
          }
        }
      }
    }
  }
  return ret;
}

int ObMultiTenant::update_tenant_freezer_mem_limit(const int64_t tenant_min_mem,
                                                   const int64_t tenant_max_mem)
{
  int ret = OB_SUCCESS;

  ObTenantFreezer *freezer = nullptr;
  if (FALSE_IT(freezer = share::g_mp->tenant_freezer())) {
  } else if (freezer->is_tenant_mem_changed(tenant_min_mem, tenant_max_mem)) {
    if (OB_FAIL(freezer->set_tenant_mem_limit(tenant_min_mem, tenant_max_mem))) {
      LOG_WARN("set tenant mem limit failed", K(ret));
    }
  }
  return ret;
}

int ObMultiTenant::get_tenant_unit(ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tenant", K(ret));
  } else {
    unit = tenant->get_unit();
  }

  return ret;
}

int ObMultiTenant::get_unit_id(uint64_t &unit_id)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tenant", K(ret));
  } else {
    unit_id = tenant->get_unit_id();
  }
  return ret;
}

int ObMultiTenant::get_tenant_meta(ObTenantMeta &meta, bool &exist)
{
  int ret = OB_SUCCESS;
  exist = false;
  if (OB_ISNULL(tenant_) || !tenant_active_) {
  } else if (tenant_->is_hidden()) {
    // skip
  } else {
    meta = tenant_->get_tenant_meta();
    exist = true;
  }
  return ret;
}

int ObMultiTenant::get_tenant_meta_for_ckpt(ObTenantMeta &meta, bool &exist)
{
  int ret = OB_SUCCESS;
  // Single-tenant: tenant_ never swaps at runtime (built once at boot, freed at
  // shutdown), so ckpt can read it directly without the former create/remove
  // exclusion lock.
  exist = false;
  if (OB_ISNULL(tenant_) || !tenant_active_) {
  } else {
    meta = tenant_->get_tenant_meta();
    exist = true;
  }

  return ret;
}

int ObMultiTenant::modify_tenant_io(const ObUnitConfig &unit_config)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = NULL;

  if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("can't modify tenant which doesn't exist", K(ret));
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected condition, tenant is NULL", K(tenant));
  } else {
    ObTenantIOConfig::UnitConfig io_unit_config(unit_config);
    ObTenantIOConfig::ParamConfig io_param_config;
    if (!true) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("tenant config is invalid", K(ret));
    } else {
      io_param_config.memory_limit_ = unit_config.memory_size();
      io_param_config.callback_thread_count_ = GCONF._io_callback_thread_count;
      io_param_config.object_storage_io_timeout_ms_ = GCONF._object_storage_io_timeout / 1000L;
      if (OB_FAIL(OB_IO_MANAGER.refresh_tenant_io_unit_config( io_unit_config))) {
        LOG_WARN("refresh tenant io unit config failed", K(ret), K(io_unit_config));
      } else if (OB_FAIL(OB_IO_MANAGER.refresh_tenant_io_param_config( io_param_config))) {
        LOG_WARN("refresh tenant io param config failed", K(ret), K(io_param_config));
      }
    }
  }
  return ret;
}

bool ObMultiTenant::has_tenant() const
{
  ObTenant *tenant = NULL;
  int ret = get_tenant_unsafe(tenant);
  return OB_SUCCESS == ret && NULL != tenant;
}

bool ObMultiTenant::is_available_tenant() const
{
  ObTenant *tenant = NULL;
  bool available = false;
  int ret = get_tenant_unsafe(tenant);
  if (OB_SUCCESS == ret && NULL != tenant) {
    if (tenant->get_create_status() == ObTenantCreateStatus::CREATED) {
      ObUnitInfoGetter::ObUnitStatus unit_status = tenant->get_unit().unit_status_;
      available = share::ObUnitInfoGetter::is_valid_tenant(unit_status);
    }
  }
  return available;
}

int ObMultiTenant::check_if_hidden_sys(bool &is_hidden_sys)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tennat", K(ret));
  } else {
    is_hidden_sys = tenant->is_hidden();
  }

  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_REMOVE_TENANT_LOCK_ERROR);
// Ensure the remove_tenant function can be called repeatedly, because deleting a tenant may fail and require multiple retries,
// Here we only delete the memory structure, the persisted data is still there.
void ObMultiTenant::remove_tenant()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(tenant_) || !tenant_active_) {
    ret = OB_TENANT_NOT_IN_SERVER;
    LOG_WARN("tenant has been removed", K(ret));
  } else if (OB_ISNULL(GCTX.session_mgr_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected condition", K(ret));
  } else {
    LOG_INFO("removed_tenant begin to stop");
    bool need_force_kill_session = false;
    bool is_prepare_unit_gc = false;
    int64_t prepare_unit_gc_ts = false;
    tenant_->stop();
    is_prepare_unit_gc = tenant_->is_prepare_unit_gc();
    prepare_unit_gc_ts = tenant_->get_prepare_unit_gc_ts();
    const int64_t unit_gc_wait_time = GCONF.unit_gc_wait_time;
    if (GCONF._enable_unit_gc_wait) {
      if (!is_prepare_unit_gc) {
        tenant_->set_prepare_unit_gc();
        need_force_kill_session = false;
      } else {
        need_force_kill_session = (prepare_unit_gc_ts > 0 &&
            ObTimeUtility::current_time() - prepare_unit_gc_ts > unit_gc_wait_time);
      }
    } else {
      need_force_kill_session = true;
    }
    LOG_INFO("removed_tenant begin to kill tenant session", K(prepare_unit_gc_ts), K(need_force_kill_session), K(GCONF._enable_unit_gc_wait));
    if (OB_FAIL(GCTX.session_mgr_->kill_tenant( need_force_kill_session))) {
      LOG_WARN("fail to kill tenant session", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(tenant_->try_wait())) {
      LOG_WARN("remove tenant try_wait failed", K(ret));
    } else if (OB_FAIL(ERRSIM_REMOVE_TENANT_LOCK_ERROR)) {
      LOG_WARN("errsim lock tenant error", KR(ret));
    } else if (OB_FAIL(tenant_->try_wrlock())) {
      LOG_WARN("can't get tenant wlock to remove tenant", K(ret),
          KP(tenant_), K(tenant_->lock_));
    } else {
      tenant_active_ = false;
    }

    if (OB_SUCC(ret)) {
      tenant_->destroy();
      ob_delete(tenant_);
      LOG_INFO("remove tenant success");
    }
  }

  if (OB_SUCC(ret)) {
    ObMallocAllocator *malloc_allocator = ObMallocAllocator::get_instance();
    if (OB_ISNULL(malloc_allocator)) {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("malloc allocator is NULL", K(ret));
    } else {
      auto& cache_washer = ObKVGlobalCache::get_instance();
      if (OB_FAIL(cache_washer.sync_flush_tenant())) {
        LOG_WARN("Fail to sync flush tenant cache", K(ret));
      }
    }
  }
  if (OB_SUCC(ret)) {
    if (OB_ISNULL(GCTX.disk_reporter_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_ERROR("disk reporter is null", K(ret));
    } else if (OB_FAIL(GCTX.disk_reporter_->delete_tenant_usage_stat())) {
      LOG_WARN("failed to delete_tenant_usage_stat", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    // only report event when ret = success
    ROOTSERVICE_EVENT_ADD("remove_tenant", "remove_tenant",
        "addr", GCTX.self_addr(),
        "result", ret);
  }

  if (OB_SUCC(ret)) {
    if (OB_NOT_NULL(GCTX.conn_res_mgr_)
               && OB_FAIL(GCTX.conn_res_mgr_->erase_tenant_conn_res_map())) {
      LOG_WARN("erase tenant conn res map failed", K(ret));
    }
  }
}

int ObMultiTenant::update_tenant(std::function<int(ObTenant&)> &&func)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("get tenant by tenant id fail", K(ret));
  } else {
    ret = func(*tenant);
  }
  return ret;
}

int ObMultiTenant::get_tenant(
    ObTenant *&tenant) const
{
  return get_tenant_unsafe(tenant);
}

int ObMultiTenant::get_tenant_with_tenant_lock(
  ObTenant *&tenant) const
{
  ObTenant *tenant_tmp = nullptr;
  int ret = get_tenant_unsafe(tenant_tmp);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(tenant_tmp->try_rdlock())) {
      if (tenant_tmp->has_stopped()) {
        // in some cases this error code is handled specially
        ret = OB_TENANT_NOT_IN_SERVER;
        LOG_WARN("fail to try rdlock tenant", K(ret));
      }
    } else {
      // assign tenant when get rdlock succ
      tenant = tenant_tmp;
    }
    if (OB_UNLIKELY(tenant_tmp->has_stopped())) {
      LOG_WARN("get rdlock when tenant has stopped", K(lbt()));
    }
  }
  return ret;
}

int ObMultiTenant::get_active_tenant_with_tenant_lock(
  ObTenant *&tenant) const
{
  ObTenant *tenant_tmp = nullptr;
  int ret = get_tenant_unsafe(tenant_tmp);
  if (OB_SUCC(ret)) {
    if (tenant_tmp->has_stopped()) {
      ret = OB_TENANT_NOT_IN_SERVER;
    } else if (OB_FAIL(tenant_tmp->try_rdlock())) {
      if (tenant_tmp->has_stopped()) {
        // in some cases this error code is handled specially
        ret = OB_TENANT_NOT_IN_SERVER;
        LOG_WARN("fail to try rdlock tenant", K(ret));
      }
    } else {
      // assign tenant when get rdlock succ
      tenant = tenant_tmp;
    }
    if (OB_UNLIKELY(tenant_tmp->has_stopped())) {
      LOG_WARN("get rdlock when tenant has stopped", K(lbt()));
    }
  }
  return ret;
}

int ObMultiTenant::get_tenant_unsafe(ObTenant *&tenant) const
{
  int ret = OB_SUCCESS;
  tenant = NULL;
  if (OB_ISNULL(tenant_) || !tenant_active_) {
    ret = OB_TENANT_NOT_IN_SERVER;
  } else {
    tenant = tenant_;
  }
  return ret;
}

int ObMultiTenant::recv_request(ObRequest &req)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_ERROR("get tenant failed", K(ret));
  } else if (NULL == tenant) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("tenant is null", K(ret));
  } else if (OB_FAIL(tenant->recv_request(req))) {
    LOG_ERROR("recv request failed", K(ret));
  } else {
    // do nothing
  }
  return ret;
}



int ObMultiTenant::get_tenant_cpu_usage(double &usage) const
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  usage = 0.;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
  } else {
    usage = tenant->get_token_usage() * tenant->unit_min_cpu();
  }
  return ret;
}

int ObMultiTenant::get_tenant_worker_time(int64_t &worker_time) const
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  worker_time = 0.;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
  } else {
    worker_time = tenant->get_worker_time();
  }
  return ret;
}

int ObMultiTenant::get_tenant_cpu_time(int64_t &cpu_time) const
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = nullptr;
  cpu_time = 0;
  if (OB_NOT_NULL(GCTX.cgroup_ctrl_) && GCTX.cgroup_ctrl_->is_valid()) {
    ret = GCTX.cgroup_ctrl_->get_cpu_time(cpu_time);
  } else if (OB_FAIL(get_tenant_unsafe(tenant))) {
  } else {
    cpu_time = tenant->get_cpu_time();
  }
  return ret;
}


int ObMultiTenant::get_tenant_cpu(double &min_cpu, double &max_cpu) const
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = NULL;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
  } else if (NULL != tenant) {
    min_cpu = tenant->unit_min_cpu();
    max_cpu = tenant->unit_max_cpu();
  }
  return ret;
}

// ==== sys-tenant bring-up & periodic GCONF refresh (collapsed from ObTenantNodeBalancer) ====

// Materialize the single sys-tenant unit fresh from GCONF each call (replaces the
// mocked ObUnitInfoGetter::get_server_tenant_configs). Uses the resolved
// log_block_mgr log-disk size (matches update_tenant_unit_no_lock and boot).
int ObMultiTenant::gen_sys_tenant_unit_(ObUnitInfoGetter::ObTenantConfig &unit)
{
  int ret = OB_SUCCESS;
  ObUnitConfig unit_config;
  int64_t hidden_sys_data_disk_config_size = 0;
  if (OB_ISNULL(GCTX.log_block_mgr_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), KP(GCTX.log_block_mgr_));
  } else if (OB_FAIL(unit_config.gen_sys_tenant_unit_config(false/*is_hidden_sys*/,
                                                            GCTX.log_block_mgr_->get_log_disk_size()))) {
    LOG_WARN("gen sys tenant unit config fail", KR(ret));
  } else if (OB_FAIL(unit.init(1/*unit_id*/,
                               ObUnitInfoGetter::ObUnitStatus::UNIT_NORMAL,
                               unit_config,
                               lib::Worker::CompatMode::MYSQL/*compat_mode*/,
                               0/*create_timestamp*/,
                               true/*has_memstore*/,
                               false/*is_removed*/,
                               hidden_sys_data_disk_config_size,
                               unit.gen_init_actual_data_disk_size(unit_config)))) {
    LOG_WARN("fail to init sys tenant config", KR(ret), K(unit_config));
  }
  return ret;
}

// Apply a freshly-built sys unit to the single live tenant: flip hidden->real
// (boot only) then refresh cpu/log-disk/memory/iops from GCONF. This is the
// load-bearing half of the former ObTenantNodeBalancer::check_new_tenant.
int ObMultiTenant::apply_sys_tenant_unit_(const ObUnitInfoGetter::ObTenantConfig &unit,
                                          const int64_t abs_timeout_us)
{
  int ret = OB_SUCCESS;
  
  ObTenant *tenant = nullptr;
  if (OB_FAIL(get_tenant(tenant))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("real or hidden sys tenant must be exist", K(ret));
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant should not be null here", KR(ret));
  } else if (tenant->get_unit_status() == ObUnitInfoGetter::ObUnitStatus::UNIT_DELETING_IN_OBSERVER
             || tenant->has_stopped()) {
    LOG_INFO("tenant has been stopped, no need to update", KR(ret));
  } else {
    if (tenant->is_hidden() && OB_FAIL(convert_hidden_to_real_sys_tenant(unit, abs_timeout_us))) {
      LOG_WARN("fail to create real sys tenant", K(unit));
    }
    if (OB_SUCC(ret) && OB_FAIL(update_tenant_unit(unit))) {
      LOG_WARN("fail to update tenant unit", K(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(update_tenant_memory(unit))) {
      LOG_ERROR("fail to update tenant memory", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(modify_tenant_io(unit.config_))) {
        LOG_WARN("modify tenant io config failed", K(ret), K(unit.config_));
      }
    }
  }
  return ret;
}

// Bring the single sys tenant fully up at boot (was ObTenantNodeBalancer::notify_create_tenant
// + the timer's first refresh_tenant -> set_synced). Called from ObServer::try_update_hidden_sys.
int ObMultiTenant::bring_up_sys_tenant_()
{
  int ret = OB_SUCCESS;
  ObUnitInfoGetter::ObTenantConfig unit;
  if (OB_FAIL(gen_sys_tenant_unit_(unit))) {
    LOG_WARN("fail to gen sys tenant unit", KR(ret));
  } else if (OB_FAIL(apply_sys_tenant_unit_(unit, INT64_MAX))) {
    LOG_WARN("fail to bring up sys tenant", KR(ret), K(unit));
  } else {
    set_synced();
    LOG_INFO("succ to bring up sys tenant", K(unit));
  }
  return ret;
}

int ObMultiTenant::bring_up_sys_tenant()
{
  return bring_up_sys_tenant_();
}

// One periodic GCONF-refresh pass on the single sys tenant (collapsed from
// ObTenantNodeBalancer::handle): rebuild the unit from GCONF, re-apply
// cpu/log-disk/memory/iops, then refresh memstore-limit (TMA) + data-version (ODV)
// + per-tenant upkeep (PX/DTL/resource-plan).
int ObMultiTenant::refresh_sys_tenant_config_()
{
  int ret = OB_SUCCESS;
  ObUnitInfoGetter::ObTenantConfig unit;
  ObCurTraceId::init(GCONF.self_addr_);
  if (!SERVER_STORAGE_META_SERVICE.is_started()) {
    // do nothing if not finish replaying slog
    LOG_INFO("server slog not finish replaying, need wait");
    ret = OB_NEED_RETRY;
  } else if (OB_FAIL(gen_sys_tenant_unit_(unit))) {
    LOG_WARN("fail to gen sys tenant unit", KR(ret));
  } else if (OB_FAIL(apply_sys_tenant_unit_(unit, INT64_MAX))) {
    LOG_WARN("failed to refresh sys tenant", KR(ret), K(unit));
  } else {
    set_synced();
    periodically_check_sys_tenant_();
  }

  FLOG_INFO("refresh tenant units", K(unit), KR(ret));

  // will try to update tma whether tenant unit is changed or not,
  // because memstore_limit_percentage may be changed
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = TMA_MGR_INSTANCE.update_tenant_mem_limit(unit))) {
    LOG_WARN("TMA_MGR_INSTANCE.update_tenant_mem_limit failed", K(tmp_ret));
  }

  if (!SERVER_STORAGE_META_SERVICE.is_started()) {
    // do nothing if not finish replaying slog
    LOG_INFO("server slog not finish replaying, need wait");
    ret = OB_NEED_RETRY;
  } else if (OB_FAIL(ODV_MGR.set(GCONF.compatible))) {
    LOG_WARN("set sys tenant data version failed", K(ret));
  }

  FLOG_INFO("refresh tenant config", K(ret));

  return ret;
}

int ObMultiTenant::refresh_sys_tenant()
{
  return refresh_sys_tenant_config_();
}

// Per-tick upkeep on the single sys tenant (collapsed from
// ObTenantNodeBalancer::periodically_check_tenant): PX/DTL/parallel-servers-target
// via tenant->periodically_check() + resource-plan refresh. Independent of unit-diff.
void ObMultiTenant::periodically_check_sys_tenant_()
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = get_tenant_instance();
  bool locked = false;
  if (!OB_ISNULL(tenant) && !tenant->has_stopped()) {
    if (OB_FAIL(tenant->rdlock())) {
      LOG_WARN("failed to rd lock tenant", K(ret));
    } else {
      locked = true;
    }
  }
  refresh_global_background_cpu(G_RES_MGR.get_plan_mgr());
  if (locked) {
    tenant->periodically_check();
    IGNORE_RETURN tenant->unlock();
  }
  ObResourcePlanManager &plan_mgr = G_RES_MGR.get_plan_mgr();
  LOG_INFO("refresh resource manager plan", K(plan_mgr));
}

int64_t ObMultiTenant::get_sys_refresh_interval_()
{
  if (!has_synced()) {
    return BOOTSTRAP_REFRESH_INTERVAL;
  } else {
    return refresh_interval_;
  }
}

// Aggregate server resource over the (single) live tenant. Relocated verbatim
// from ObTenantNodeBalancer::get_server_allocated_resource; consumed by
// ObService::get_server_resource_info.
int ObMultiTenant::get_server_allocated_resource(ServerResource &server_resource)
{
  int ret = OB_SUCCESS;
  server_resource.reset();
  if (OB_ISNULL(tenant_) || !tenant_active_ || tenant_->is_hidden()) {
    // no live tenant -> zero resource
  } else {
    const share::ObUnitInfoGetter::ObTenantConfig unit = tenant_->get_unit();
    server_resource.max_cpu_ += unit.config_.max_cpu();
    server_resource.min_cpu_ += unit.config_.min_cpu();
    server_resource.memory_size_ += max(ObMallocAllocator::get_instance()->get_tenant_limit(),
                                        unit.config_.memory_size());
    server_resource.log_disk_size_ += unit.config_.log_disk_size();
    server_resource.data_disk_size_ += unit.get_effective_actual_data_disk_size();
  }
  return ret;
}

void ObMultiTenant::runTimerTask()
{
  {
    bool need_regist_cgroup = false;
    if (REACH_TIME_INTERVAL(1 * 1000 * 1000L)) {  // every 1s
      if (OB_NOT_NULL(GCTX.cgroup_ctrl_)) {
        need_regist_cgroup = GCTX.cgroup_ctrl_->check_cgroup_status();
      }
    }
    if (OB_ISNULL(tenant_) || !tenant_active_) {
    } else {
      if (need_regist_cgroup) {
        tenant_->regist_threads_to_cgroup();
      }
      tenant_->timeup();
    }
  }

  if (is_inited_ && REACH_TIME_INTERVAL(get_sys_refresh_interval_())) {
    refresh_sys_tenant_config_();
  }

  if (REACH_TIME_INTERVAL(10000000L)) {  // every 10s
    ObDIActionGuard ag("dump tenant info");
    if (!OB_ISNULL(tenant_)) {
      ObTaskController::get().allow_next_syslog();
      LOG_INFO("dump tenant info", "tenant", *tenant_);
      if (OB_NOT_NULL(GCTX.cgroup_ctrl_) && GCTX.cgroup_ctrl_->is_valid()) {
        tenant_->print_throttled_time();
      }
    }
  }
}

void ObMultiTenant::reload_tenant_task_queue_size()
{
  if (OB_NOT_NULL(tenant_)) {
    tenant_->set_queue_limit(GCONF.tenant_task_queue_size);
  }
}

int ObSrvNetworkFrame::reload_sql_thread_config()
{
  int ret = OB_SUCCESS;

  int sql_net_thread_count = (int)GCONF.sql_net_thread_count;
  if (sql_net_thread_count == 0) {
    if (GCONF.net_thread_count == 0) {
      sql_net_thread_count = get_default_net_thread_count();
    } else {
      sql_net_thread_count = GCONF.net_thread_count;
    }
  }

  if (OB_NOT_NULL(obmysql::global_sql_nio_server)) {
    int cur_sql_net_thread_count =
        obmysql::global_sql_nio_server->get_nio()->get_thread_count();
    if (sql_net_thread_count < cur_sql_net_thread_count) {
      LOG_WARN("decrease sql_net_thread_count not allowed", K(ret),
               K(sql_net_thread_count), K(cur_sql_net_thread_count));
      GCONF.sql_net_thread_count = cur_sql_net_thread_count;
    } else if (OB_FAIL(obmysql::global_sql_nio_server->set_thread_count(
                   sql_net_thread_count))) {
      LOG_WARN("update sql_net_thread_count error", K(ret));
    }
  }

  return ret;
}

int ObSharedTimer::mtl_init(ObSharedTimer *&st)
{
  int ret = common::OB_SUCCESS;
  if (st != NULL) {
    int &tg_id = st->tg_id_;
    if (OB_FAIL(TG_CREATE_TENANT(lib::TGDefIDs::TntSharedTimer, tg_id))) {
      LOG_WARN("init shared timer failed", K(ret));
    }
  }
  return ret;
}

int ObSharedTimer::mtl_start(ObSharedTimer *&st)
{
  int ret = common::OB_SUCCESS;
  if (st != NULL) {
    int &tg_id = st->tg_id_;
    if (OB_FAIL(TG_START(tg_id))) {
      LOG_WARN("init shared timer failed", K(ret), K(tg_id));
    }
  }
  return ret;
}

void ObSharedTimer::mtl_stop(ObSharedTimer *&st)
{
  if (st != NULL) {
    int &tg_id = st->tg_id_;
    if (tg_id > 0) {
      TG_STOP(tg_id);
    }
  }
}

void ObSharedTimer::mtl_wait(ObSharedTimer *&st)
{
  if (st != NULL) {
    int &tg_id = st->tg_id_;
    if (tg_id > 0) {
      TG_WAIT_ONLY(tg_id);
    }
  }
}

void ObSharedTimer::destroy()
{
  if (tg_id_ > 0) {
    TG_DESTROY(tg_id_);
    tg_id_ = -1;
  }
}

int ObMultiTenant::inc_tenant_ddl_count(const int64_t cpu_quota_concurrency)
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = NULL;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tenant", KR(ret));
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant is null", KR(ret));
  } else {
    if (tenant->check_ddl_thread_is_limit(cpu_quota_concurrency)) {
      ret = OB_ERR_DDL_RESOURCE_NOT_ENOUGH;
      LOG_WARN("tenant ddl task larger than limit, need retry", KR(ret), K(tenant->cur_ddl_thread_count()));
    } else {
      lib::Thread::set_doing_ddl(true);
      tenant->inc_ddl_thread_count();
    }
  }
  return ret;
}

int ObMultiTenant::dec_tenant_ddl_count()
{
  int ret = OB_SUCCESS;
  ObTenant *tenant = NULL;
  if (OB_FAIL(get_tenant_unsafe(tenant))) {
    LOG_WARN("fail to get tenant", KR(ret));
  } else if (OB_ISNULL(tenant)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("tenant is null", KR(ret));
  } else {
    lib::Thread::set_doing_ddl(false);
    tenant->dec_ddl_thread_count();
    if (tenant->cur_ddl_thread_count() < 0) {
      LOG_ERROR("tenant ddl count is less than 0, please check", K(tenant->cur_ddl_thread_count()));
    } else {
      LOG_TRACE("tenant ddl count", K(tenant->cur_ddl_thread_count()));
    }
  }
  return ret;
}

// ===== moved from share::ObResourcePlanManager and demoted to omt free function(truly observer-bound: omt tenant iteration/MTL/cgroup) =====
namespace oceanbase
{
namespace omt
{

int refresh_global_background_cpu(share::ObResourcePlanManager &mgr)
{
  int ret = OB_SUCCESS;
  if (GCONF.enable_global_background_resource_isolation && GCTX.cgroup_ctrl_->is_valid()) {
    double cpu = static_cast<double>(GCONF.global_background_cpu_quota);
    if (cpu <= 0) {
      cpu = -1;
    }
    if (cpu >= 0 && OB_FAIL(GCTX.cgroup_ctrl_->set_cpu_shares(cpu,
                        OB_INVALID_GROUP_ID,
                        true /* is_background */))) {
      LOG_WARN("fail to set background cpu shares", K(ret));
    }
    int compare_ret = 0;
    if (OB_SUCC(ret) && OB_SUCC(GCTX.cgroup_ctrl_->compare_cpu(mgr.get_background_quota(), cpu, compare_ret))) {
      if (0 == compare_ret) {
        // do nothing
      } else if (OB_FAIL(GCTX.cgroup_ctrl_->set_cpu_cfs_quota(cpu,
                     OB_INVALID_GROUP_ID,
                     true /* is_background */))) {
        LOG_WARN("fail to set background cpu cfs quota", K(ret));
      } else {
        if (compare_ret < 0) {
#ifdef _WIN32
          SYSTEM_INFO si;
          GetSystemInfo(&si);
          const int64_t phy_cpu_cnt = static_cast<int64_t>(si.dwNumberOfProcessors);
#else
          const int64_t phy_cpu_cnt = sysconf(_SC_NPROCESSORS_ONLN);
#endif
          int tmp_ret = OB_SUCCESS;
          {
            double target_cpu = -1;
            {
              target_cpu = (phy_cpu_cnt <= 4) ? 1.0 : OB_DTL_CPU;
            }
            if (OB_TMP_FAIL(GCTX.cgroup_ctrl_->compare_cpu(target_cpu, cpu, compare_ret))) {
              LOG_WARN_RET(tmp_ret, "compare tenant cpu failed", K(tmp_ret), K(1UL));
            } else if (compare_ret > 0) {
              target_cpu = cpu;
            }
            if (OB_TMP_FAIL(GCTX.cgroup_ctrl_->set_cpu_cfs_quota(target_cpu, OB_INVALID_GROUP_ID, true /* is_background */))) {
              LOG_WARN_RET(tmp_ret, "set tenant cpu cfs quota failed", K(tmp_ret), K(1UL));
            }
          }
        }
      }
      if (OB_SUCC(ret) && 0 != compare_ret) {
        mgr.set_background_quota(cpu);
      }
    }
  }
  return ret;
}

}  // namespace omt
}  // namespace oceanbase

// ===== calc_nway file-local helper moved together =====
namespace oceanbase { namespace share {
namespace {
static int64_t calc_nway(int64_t cpu, int64_t mem)
{
  return std::min(cpu, mem/20/ObFifoArena::ALLOC_PAGE_SIZE);
}

}
} }
// ===== definition moved from share memstore_allocator/index_usage(omt real user) =====
namespace oceanbase
{
namespace share
{

int64_t ObMemstoreAllocator::nway_per_group()
{
  int ret = OB_SUCCESS;

  double min_cpu = 0;
  double max_cpu = 0;
  int64_t max_memory = 0;
  int64_t min_memory = 0;
  omt::ObMultiTenant *omt = GCTX.omt_;

  MOD_SCOPE {
    storage::ObTenantFreezer *freezer = nullptr;
    if (NULL == omt) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(WARN, "omt should not be null", K(ret));
    } else if (OB_FAIL(omt->get_tenant_cpu(min_cpu, max_cpu))) {
      COMMON_LOG(WARN, "get tenant cpu failed", K(ret));
    } else if (FALSE_IT(freezer = share::g_mp->tenant_freezer())) {
    } else if (OB_FAIL(freezer->get_tenant_mem_limit(min_memory, max_memory))) {
      COMMON_LOG(WARN, "get tenant mem limit failed", K(ret));
    }
  }
  return OB_SUCCESS == ret? calc_nway((int64_t)max_cpu, min_memory): 0;
}

void ObIndexUsageInfoMgr::destroy() 
{
  if (is_inited_) {
    // cancel report task
    if (report_task_.get_is_inited()) {
      bool is_exist = true;
      if (TG_TASK_EXIST(share::g_mp->shared_timer()->get_tg_id(), report_task_, is_exist) == OB_SUCCESS && is_exist) {
        TG_CANCEL_TASK(share::g_mp->shared_timer()->get_tg_id(), report_task_);
        TG_WAIT_TASK(share::g_mp->shared_timer()->get_tg_id(), report_task_);
        report_task_.destroy();
      }
    } 
    if (refresh_conf_task_.get_is_inited()) {
      bool is_exist = true;
      if (TG_TASK_EXIST(share::g_mp->shared_timer()->get_tg_id(), refresh_conf_task_, is_exist) == OB_SUCCESS && is_exist) {
        TG_CANCEL_TASK(share::g_mp->shared_timer()->get_tg_id(), refresh_conf_task_);
        TG_WAIT_TASK(share::g_mp->shared_timer()->get_tg_id(), refresh_conf_task_);
        refresh_conf_task_.destroy();
      }
    }
    is_inited_ = false;
    is_enabled_ = false;
    destroy_hash_map();
    allocator_.reset();
  }
}

}  // namespace share
}  // namespace oceanbase

// ===== definition moved from share index_usage(start/stop/wait)+io_manager(gc/print, omt real user) =====
namespace oceanbase
{
namespace share
{

int ObIndexUsageInfoMgr::start() 
{
  int ret = OB_SUCCESS;
  if (is_inited_) {
    // report index usage
    if (OB_FAIL(TG_SCHEDULE(share::g_mp->shared_timer()->get_tg_id(), report_task_, INDEX_USAGE_REPORT_INTERVAL, true))) {
      LOG_WARN("failed to schedule index usage report task", K(ret));
    } else if (OB_FAIL(report_task_.init(this))) {
      LOG_WARN("fail to init report task", K(ret));
    } else if (OB_FAIL(TG_SCHEDULE(share::g_mp->shared_timer()->get_tg_id(), refresh_conf_task_, INDEX_USAGE_REFRESH_CONF_INTERVAL, true))) {
      LOG_WARN("failed to schedule index usage refresh conf task", K(ret));
    } else if (OB_FAIL(refresh_conf_task_.init((this)))) {
      LOG_WARN("fail to init refresh conf task", K(ret));
    } else {
      LOG_TRACE("success to start ObIndexUsageInfoMgr");
    }
  }
  return ret;
}

void ObIndexUsageInfoMgr::stop() 
{
  if (OB_LIKELY(report_task_.get_is_inited())) {
    TG_CANCEL_TASK(share::g_mp->shared_timer()->get_tg_id(), report_task_);
  }
  if (OB_LIKELY(refresh_conf_task_.get_is_inited())) {
    TG_CANCEL_TASK(share::g_mp->shared_timer()->get_tg_id(), refresh_conf_task_);
  }
}

void ObIndexUsageInfoMgr::wait() 
{
  if (OB_LIKELY(report_task_.get_is_inited())) {
    TG_WAIT_TASK(share::g_mp->shared_timer()->get_tg_id(), report_task_);
  }
  if (OB_LIKELY(refresh_conf_task_.get_is_inited())) {
    TG_WAIT_TASK(share::g_mp->shared_timer()->get_tg_id(), refresh_conf_task_);
  }
}

}  // namespace share
namespace common
{

int ObTrafficControl::gc_tenant_infos()
{
  int ret = OB_SUCCESS;
  if (REACH_TIME_INTERVAL(1 * 60 * 1000L * 1000L)) {  // 60s
    DRWLock::WRLockGuard guard(rw_lock_);
    struct GCTenantSharedDeviceInfosV2
    {
      GCTenantSharedDeviceInfosV2(
          const ObVector<uint64_t> &keep_keys, ObSEArray<ObTrafficControl::ObStorageKey, 7> &gc_tenant_infos)
          : keep_keys_(keep_keys), gc_tenant_infos_(gc_tenant_infos)
      {}
      int operator()(hash::HashMapPair<ObTrafficControl::ObStorageKey, ObTrafficControl::ObSharedDeviceControlV2 *> &pair)
      {
        bool is_find = false;
        for (int i = 0; !is_find && i < keep_keys_.size(); ++i) {
          if (keep_keys_.at(i) == 1UL) {
            is_find = true;
          }
        }
        if (false == is_find) {
          gc_tenant_infos_.push_back(pair.first);
        }
        return OB_SUCCESS;
      }
      const ObVector<uint64_t> &keep_keys_;
      ObSEArray<ObTrafficControl::ObStorageKey, 7> &gc_tenant_infos_;
    };
    struct GCTenantRecordInfos
    {
      GCTenantRecordInfos(
          const ObVector<uint64_t> &keep_keys, ObSEArray<ObTrafficControl::ObIORecordKey, 7> &gc_tenant_infos)
          : keep_keys_(keep_keys), gc_tenant_infos_(gc_tenant_infos)
      {}
      int operator()(hash::HashMapPair<ObTrafficControl::ObIORecordKey, ObTrafficControl::ObSharedDeviceIORecord> &pair)
      {
        bool is_find = false;
        for (int i = 0; !is_find && i < keep_keys_.size(); ++i) {
          if (keep_keys_.at(i) == 1UL) {
            is_find = true;
          }
        }
        if (false == is_find) {
          gc_tenant_infos_.push_back(pair.first);
        }
        return OB_SUCCESS;
      }
      const ObVector<uint64_t> &keep_keys_;
      ObSEArray<ObTrafficControl::ObIORecordKey, 7> &gc_tenant_infos_;
    };
    ObVector<uint64_t> keep_keys;
    ObSEArray<ObTrafficControl::ObIORecordKey, 7> gc_tenant_record_infos;
    ObSEArray<ObTrafficControl::ObStorageKey, 7> gc_tenant_shared_device_infos_v2;
    GCTenantRecordInfos fn(keep_keys, gc_tenant_record_infos);
    GCTenantSharedDeviceInfosV2 fn3(keep_keys, gc_tenant_shared_device_infos_v2);
    if(OB_ISNULL(GCTX.omt_)) {
    } else if (FALSE_IT((keep_keys.clear(), keep_keys.push_back(1UL)))) {
    } else if (OB_FAIL(io_record_map_.foreach_refactored(fn))) {
      LOG_WARN("SSNT:failed to get gc tenant record infos", K(ret));
    } else if (OB_FAIL(shared_device_map_v2_.foreach_refactored(fn3))) {
      LOG_WARN("SSNT:failed to get gc tenant shared device infos", K(ret));
    } else {
      for (int i = 0; i < gc_tenant_record_infos.count(); ++i) {
        if (OB_SUCCESS != io_record_map_.erase_refactored(gc_tenant_record_infos.at(i))) {
          LOG_WARN("SSNT:failed to erase gc tenant record infos", K(ret), K(gc_tenant_record_infos.at(i)));
        } else {
          LOG_INFO("SSNT:erase gc tenant record infos", K(ret), K(gc_tenant_record_infos.at(i)));
        }
      }
      for (int i = 0; i < gc_tenant_shared_device_infos_v2.count(); ++i) {
        int tmp_ret = OB_SUCCESS;
        ObTrafficControl::ObSharedDeviceControlV2 *val_ptr = nullptr;
        if (OB_TMP_FAIL(shared_device_map_v2_.erase_refactored(gc_tenant_shared_device_infos_v2.at(i), &val_ptr))) {
          LOG_WARN("SSNT:failed to erase gc tenant shared device infos", K(tmp_ret), K(gc_tenant_shared_device_infos_v2.at(i)), K(val_ptr));
        } else if (OB_ISNULL(val_ptr)) {
          tmp_ret = OB_ERR_UNEXPECTED;
          LOG_ERROR("SSNT:failed to erase gc tenant shared device infos", K(tmp_ret), K(gc_tenant_shared_device_infos_v2.at(i)), K(val_ptr));
        } else if (FALSE_IT(val_ptr->destroy())) {
          LOG_WARN("SSNT:failed to destroy shared device control", K(tmp_ret), K(gc_tenant_shared_device_infos_v2.at(i)), K(val_ptr));
        } else if (FALSE_IT(ob_delete(val_ptr))) {
        } else {
          LOG_INFO("SSNT:erase gc tenant shared device infos succ", K(ret), K(tmp_ret), K(gc_tenant_shared_device_infos_v2.at(i)), K(val_ptr));
        }
      }
    }
  }
  return ret;
}

void ObIOManager::print_tenant_status()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(GCTX.omt_)) {
    {
      ObRefHolder<ObTenantIOManager> tenant_holder;
      if (OB_FAIL(get_tenant_io_manager(tenant_holder))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("get tenant io manager failed", K(ret), K(1UL));
        } else {
          ret = OB_SUCCESS;
        }
      } else {
        tenant_holder.get_ptr()->print_io_status();
      }
    }
  }
  if (OB_NOT_NULL(server_io_manager_)) {
    server_io_manager_->print_io_status();
  }
}

}  // namespace common
}  // namespace oceanbase

// the config read endpoint: share must not depend upward on observer
namespace oceanbase
{
namespace share
{
namespace schema
{
int64_t get_max_schema_slot_num_for_add_schema(const int64_t default_val)
{
  int64_t max_schema_slot_num = default_val;
  omt::ObTenantConfigGuard tenant_config(TENANT_CONF());
  if (tenant_config.is_valid()) {
    max_schema_slot_num = tenant_config->_max_schema_slot_num;
  }
  return max_schema_slot_num;
}
}  // namespace schema
}  // namespace share
}  // namespace oceanbase

// ===== ObServer explicit module lifecycle =====
namespace oceanbase
{
namespace observer
{
int ObServer::obs_construct_modules()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_shared_timer_))) { SERVER_LOG(WARN, "mods_shared_timer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantSQLSessionMgr::mtl_new(mods_tenant_sql_session_mgr_))) { SERVER_LOG(WARN, "mods_tenant_sql_session_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantMetaMemMgr::mtl_new(mods_tenant_meta_mem_mgr_))) { SERVER_LOG(WARN, "mods_tenant_meta_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_obj_pool_mtl_new<ObPartTransCtx>(mods_part_trans_ctx_obj_pool_))) { SERVER_LOG(WARN, "mods_part_trans_ctx_obj_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_obj_pool_mtl_new<ObTableScanIterator>(mods_table_scan_iterator_obj_pool_))) { SERVER_LOG(WARN, "mods_table_scan_iterator_obj_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantIOManager::mtl_new(mods_tenant_io_manager_))) { SERVER_LOG(WARN, "mods_tenant_io_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_mds_service_))) { SERVER_LOG(WARN, "mods_tenant_mds_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_shared_macro_block_mgr_))) { SERVER_LOG(WARN, "mods_shared_macro_block_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_shared_mem_alloc_mgr_))) { SERVER_LOG(WARN, "mods_shared_mem_alloc_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_trans_service_))) { SERVER_LOG(WARN, "mods_trans_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_log_service_))) { SERVER_LOG(WARN, "mods_log_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_ls_service_))) { SERVER_LOG(WARN, "mods_ls_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_storage_meta_service_))) { SERVER_LOG(WARN, "mods_tenant_storage_meta_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_tmp_file_manager_))) { SERVER_LOG(WARN, "mods_tenant_tmp_file_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_compaction_progress_mgr_))) { SERVER_LOG(WARN, "mods_tenant_compaction_progress_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_server_compaction_event_history_))) { SERVER_LOG(WARN, "mods_server_compaction_event_history_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_tablet_stat_mgr_))) { SERVER_LOG(WARN, "mods_tenant_tablet_stat_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_lock_wait_mgr_))) { SERVER_LOG(WARN, "mods_lock_wait_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_table_lock_service_))) { SERVER_LOG(WARN, "mods_table_lock_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_primary_major_freeze_service_))) { SERVER_LOG(WARN, "mods_primary_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_restore_major_freeze_service_))) { SERVER_LOG(WARN, "mods_restore_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_meta_checker_))) { SERVER_LOG(WARN, "mods_tenant_meta_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tablet_table_updater_))) { SERVER_LOG(WARN, "mods_tablet_table_updater_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_ss_table_merge_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_ss_table_merge_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_dag_warning_history_manager_))) { SERVER_LOG(WARN, "mods_dag_warning_history_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_schedule_suspect_info_mgr_))) { SERVER_LOG(WARN, "mods_schedule_suspect_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_compaction_suggestion_mgr_))) { SERVER_LOG(WARN, "mods_compaction_suggestion_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_diagnose_tablet_mgr_))) { SERVER_LOG(WARN, "mods_diagnose_tablet_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObLobManager::mtl_new(mods_lob_manager_))) { SERVER_LOG(WARN, "mods_lob_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_global_auto_inc_service_))) { SERVER_LOG(WARN, "mods_global_auto_inc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_dead_lock_detector_mgr_))) { SERVER_LOG(WARN, "mods_dead_lock_detector_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_timestamp_service_))) { SERVER_LOG(WARN, "mods_timestamp_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_timestamp_access_))) { SERVER_LOG(WARN, "mods_timestamp_access_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_trans_id_service_))) { SERVER_LOG(WARN, "mods_trans_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_unique_id_service_))) { SERVER_LOG(WARN, "mods_unique_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_ps_cache_))) { SERVER_LOG(WARN, "mods_ps_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_plan_cache_))) { SERVER_LOG(WARN, "mods_plan_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantDfc::mtl_new(mods_tenant_dfc_))) { SERVER_LOG(WARN, "mods_tenant_dfc_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_px_pools_))) { SERVER_LOG(WARN, "mods_px_pools_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantSqlMemoryManager::mtl_new(mods_tenant_sql_memory_manager_))) { SERVER_LOG(WARN, "mods_tenant_sql_memory_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_dtl_interm_result_manager_))) { SERVER_LOG(WARN, "mods_dtl_interm_result_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_plan_monitor_node_list_))) { SERVER_LOG(WARN, "mods_plan_monitor_node_list_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_data_access_service_))) { SERVER_LOG(WARN, "mods_data_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_dasid_service_))) { SERVER_LOG(WARN, "mods_dasid_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_schema_service_))) { SERVER_LOG(WARN, "mods_tenant_schema_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_freezer_))) { SERVER_LOG(WARN, "mods_tenant_freezer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_check_point_service_))) { SERVER_LOG(WARN, "mods_check_point_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tablet_gc_service_))) { SERVER_LOG(WARN, "mods_tablet_gc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_tablet_scheduler_))) { SERVER_LOG(WARN, "mods_tenant_tablet_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_medium_checker_))) { SERVER_LOG(WARN, "mods_tenant_medium_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_compaction_mem_pool_))) { SERVER_LOG(WARN, "mods_tenant_compaction_mem_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_ddl_merge_bucket_lock_))) { SERVER_LOG(WARN, "mods_ddl_merge_bucket_lock_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_direct_load_mgr_))) { SERVER_LOG(WARN, "mods_tenant_direct_load_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_dag_scheduler_))) { SERVER_LOG(WARN, "mods_tenant_dag_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_freeze_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_freeze_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tx_loop_worker_))) { SERVER_LOG(WARN, "mods_tx_loop_worker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_access_service_))) { SERVER_LOG(WARN, "mods_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTableLoadService::mtl_new(mods_table_load_service_))) { SERVER_LOG(WARN, "mods_table_load_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_table_load_resource_service_))) { SERVER_LOG(WARN, "mods_table_load_resource_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_multi_version_garbage_collector_))) { SERVER_LOG(WARN, "mods_multi_version_garbage_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_flt_span_mgr_))) { SERVER_LOG(WARN, "mods_flt_span_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_cg_read_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_cg_read_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_empty_read_bucket_))) { SERVER_LOG(WARN, "mods_empty_read_bucket_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_dbms_sched_service_))) { SERVER_LOG(WARN, "mods_dbms_sched_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_opt_stat_monitor_manager_))) { SERVER_LOG(WARN, "mods_opt_stat_monitor_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_srs_))) { SERVER_LOG(WARN, "mods_tenant_srs_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_index_usage_info_mgr_))) { SERVER_LOG(WARN, "mods_index_usage_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_ft_dict_mgr_))) { SERVER_LOG(WARN, "mods_ft_dict_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tablet_memtable_mgr_pool_))) { SERVER_LOG(WARN, "mods_tablet_memtable_mgr_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_m_view_maintenance_service_))) { SERVER_LOG(WARN, "mods_m_view_maintenance_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_resource_limit_calculator_))) { SERVER_LOG(WARN, "mods_resource_limit_calculator_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_global_iterator_pool_))) { SERVER_LOG(WARN, "mods_global_iterator_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_rb_mem_mgr_))) { SERVER_LOG(WARN, "mods_rb_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_plugin_vector_index_service_))) { SERVER_LOG(WARN, "mods_plugin_vector_index_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_auto_split_task_cache_))) { SERVER_LOG(WARN, "mods_auto_split_task_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_query_resp_time_collector_))) { SERVER_LOG(WARN, "mods_tenant_query_resp_time_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_ddl_service_launcher_))) { SERVER_LOG(WARN, "mods_ddl_service_launcher_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_sys_tenant_load_sys_package_service_))) { SERVER_LOG(WARN, "mods_sys_tenant_load_sys_package_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_ddl_scheduler_))) { SERVER_LOG(WARN, "mods_ddl_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObSQLCCLRuleManager::mtl_new(mods_sqlccl_rule_manager_))) { SERVER_LOG(WARN, "mods_sqlccl_rule_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_tenant_ai_service_))) { SERVER_LOG(WARN, "mods_tenant_ai_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_new_default(mods_change_stream_mgr_))) { SERVER_LOG(WARN, "mods_change_stream_mgr_ fail", KR(ret)); }
  return ret;
}

int ObServer::obs_init_modules()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_FAIL(ObSharedTimer::mtl_init(mods_shared_timer_))) { SERVER_LOG(WARN, "mods_shared_timer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantSQLSessionMgr::mtl_init(mods_tenant_sql_session_mgr_))) { SERVER_LOG(WARN, "mods_tenant_sql_session_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_init_default(mods_tenant_meta_mem_mgr_))) { SERVER_LOG(WARN, "mods_tenant_meta_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantIOManager::mtl_init(mods_tenant_io_manager_))) { SERVER_LOG(WARN, "mods_tenant_io_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::mds::ObTenantMdsService::mtl_init(mods_tenant_mds_service_))) { SERVER_LOG(WARN, "mods_tenant_mds_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObSharedMacroBlockMgr::mtl_init(mods_shared_macro_block_mgr_))) { SERVER_LOG(WARN, "mods_shared_macro_block_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(share::ObSharedMemAllocMgr::mtl_init(mods_shared_mem_alloc_mgr_))) { SERVER_LOG(WARN, "mods_shared_mem_alloc_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTransService::mtl_init(mods_trans_service_))) { SERVER_LOG(WARN, "mods_trans_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObLogService::mtl_init(mods_log_service_))) { SERVER_LOG(WARN, "mods_log_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObLSService::mtl_init(mods_ls_service_))) { SERVER_LOG(WARN, "mods_ls_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantStorageMetaService::mtl_init(mods_tenant_storage_meta_service_))) { SERVER_LOG(WARN, "mods_tenant_storage_meta_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(tmp_file::ObTenantTmpFileManager::mtl_init(mods_tenant_tmp_file_manager_))) { SERVER_LOG(WARN, "mods_tenant_tmp_file_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObTenantCompactionProgressMgr::mtl_init(mods_tenant_compaction_progress_mgr_))) { SERVER_LOG(WARN, "mods_tenant_compaction_progress_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObServerCompactionEventHistory::mtl_init(mods_server_compaction_event_history_))) { SERVER_LOG(WARN, "mods_server_compaction_event_history_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObTenantTabletStatMgr::mtl_init(mods_tenant_tablet_stat_mgr_))) { SERVER_LOG(WARN, "mods_tenant_tablet_stat_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(memtable::ObLockWaitMgr::mtl_init(mods_lock_wait_mgr_))) { SERVER_LOG(WARN, "mods_lock_wait_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTableLockService::mtl_init(mods_table_lock_service_))) { SERVER_LOG(WARN, "mods_table_lock_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObPrimaryMajorFreezeService::mtl_init(mods_primary_major_freeze_service_))) { SERVER_LOG(WARN, "mods_primary_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObRestoreMajorFreezeService::mtl_init(mods_restore_major_freeze_service_))) { SERVER_LOG(WARN, "mods_restore_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantMetaChecker::mtl_init(mods_tenant_meta_checker_))) { SERVER_LOG(WARN, "mods_tenant_meta_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTabletTableUpdater::mtl_init(mods_tablet_table_updater_))) { SERVER_LOG(WARN, "mods_tablet_table_updater_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObTenantSSTableMergeInfoMgr::mtl_init(mods_tenant_ss_table_merge_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_ss_table_merge_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(share::ObDagWarningHistoryManager::mtl_init(mods_dag_warning_history_manager_))) { SERVER_LOG(WARN, "mods_dag_warning_history_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObScheduleSuspectInfoMgr::mtl_init(mods_schedule_suspect_info_mgr_))) { SERVER_LOG(WARN, "mods_schedule_suspect_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObCompactionSuggestionMgr::mtl_init(mods_compaction_suggestion_mgr_))) { SERVER_LOG(WARN, "mods_compaction_suggestion_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObDiagnoseTabletMgr::mtl_init(mods_diagnose_tablet_mgr_))) { SERVER_LOG(WARN, "mods_diagnose_tablet_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_init_default(mods_lob_manager_))) { SERVER_LOG(WARN, "mods_lob_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObGlobalAutoIncService::mtl_init(mods_global_auto_inc_service_))) { SERVER_LOG(WARN, "mods_global_auto_inc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(share::detector::ObDeadLockDetectorMgr::mtl_init(mods_dead_lock_detector_mgr_))) { SERVER_LOG(WARN, "mods_dead_lock_detector_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTimestampService::mtl_init(mods_timestamp_service_))) { SERVER_LOG(WARN, "mods_timestamp_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTimestampAccess::mtl_init(mods_timestamp_access_))) { SERVER_LOG(WARN, "mods_timestamp_access_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTransIDService::mtl_init(mods_trans_id_service_))) { SERVER_LOG(WARN, "mods_trans_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObUniqueIDService::mtl_init(mods_unique_id_service_))) { SERVER_LOG(WARN, "mods_unique_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPsCache::mtl_init(mods_ps_cache_))) { SERVER_LOG(WARN, "mods_ps_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPlanCache::mtl_init(mods_plan_cache_))) { SERVER_LOG(WARN, "mods_plan_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantDfc::mtl_init(mods_tenant_dfc_))) { SERVER_LOG(WARN, "mods_tenant_dfc_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPxPools::mtl_init(mods_px_pools_))) { SERVER_LOG(WARN, "mods_px_pools_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(init_compat_mode(mods_compat_mode_))) { SERVER_LOG(WARN, "mods_compat_mode_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantSqlMemoryManager::mtl_init(mods_tenant_sql_memory_manager_))) { SERVER_LOG(WARN, "mods_tenant_sql_memory_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDTLIntermResultManager::mtl_init(mods_dtl_interm_result_manager_))) { SERVER_LOG(WARN, "mods_dtl_interm_result_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPlanMonitorNodeList::mtl_init(mods_plan_monitor_node_list_))) { SERVER_LOG(WARN, "mods_plan_monitor_node_list_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDataAccessService::mtl_init(mods_data_access_service_))) { SERVER_LOG(WARN, "mods_data_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDASIDService::mtl_init(mods_dasid_service_))) { SERVER_LOG(WARN, "mods_dasid_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantSchemaService::mtl_init(mods_tenant_schema_service_))) { SERVER_LOG(WARN, "mods_tenant_schema_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantFreezer::mtl_init(mods_tenant_freezer_))) { SERVER_LOG(WARN, "mods_tenant_freezer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObCheckPointService::mtl_init(mods_check_point_service_))) { SERVER_LOG(WARN, "mods_check_point_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTabletGCService::mtl_init(mods_tablet_gc_service_))) { SERVER_LOG(WARN, "mods_tablet_gc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObTenantTabletScheduler::mtl_init(mods_tenant_tablet_scheduler_))) { SERVER_LOG(WARN, "mods_tenant_tablet_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObTenantMediumChecker::mtl_init(mods_tenant_medium_checker_))) { SERVER_LOG(WARN, "mods_tenant_medium_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObTenantCompactionMemPool::mtl_init(mods_tenant_compaction_mem_pool_))) { SERVER_LOG(WARN, "mods_tenant_compaction_mem_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDDLMergeBucketLock::mtl_init(mods_ddl_merge_bucket_lock_))) { SERVER_LOG(WARN, "mods_ddl_merge_bucket_lock_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantDirectLoadMgr::mtl_init(mods_tenant_direct_load_mgr_))) { SERVER_LOG(WARN, "mods_tenant_direct_load_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantDagScheduler::mtl_init(mods_tenant_dag_scheduler_))) { SERVER_LOG(WARN, "mods_tenant_dag_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantFreezeInfoMgr::mtl_init(mods_tenant_freeze_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_freeze_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTxLoopWorker::mtl_init(mods_tx_loop_worker_))) { SERVER_LOG(WARN, "mods_tx_loop_worker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObAccessService::mtl_init(mods_access_service_))) { SERVER_LOG(WARN, "mods_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_init_default(mods_table_load_service_))) { SERVER_LOG(WARN, "mods_table_load_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(observer::ObTableLoadResourceService::mtl_init(mods_table_load_resource_service_))) { SERVER_LOG(WARN, "mods_table_load_resource_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObMultiVersionGarbageCollector::mtl_init(mods_multi_version_garbage_collector_))) { SERVER_LOG(WARN, "mods_multi_version_garbage_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObFLTSpanMgr::mtl_init(mods_flt_span_mgr_))) { SERVER_LOG(WARN, "mods_flt_span_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantCGReadInfoMgr::mtl_init(mods_tenant_cg_read_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_cg_read_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObEmptyReadBucket::mtl_init(mods_empty_read_bucket_))) { SERVER_LOG(WARN, "mods_empty_read_bucket_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObDBMSSchedService::mtl_init(mods_dbms_sched_service_))) { SERVER_LOG(WARN, "mods_dbms_sched_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObOptStatMonitorManager::mtl_init(mods_opt_stat_monitor_manager_))) { SERVER_LOG(WARN, "mods_opt_stat_monitor_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(omt::ObTenantSrs::mtl_init(mods_tenant_srs_))) { SERVER_LOG(WARN, "mods_tenant_srs_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObIndexUsageInfoMgr::mtl_init(mods_index_usage_info_mgr_))) { SERVER_LOG(WARN, "mods_index_usage_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObFTDictMgr::mtl_init(mods_ft_dict_mgr_))) { SERVER_LOG(WARN, "mods_ft_dict_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObTabletMemtableMgrPool::mtl_init(mods_tablet_memtable_mgr_pool_))) { SERVER_LOG(WARN, "mods_tablet_memtable_mgr_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObMViewMaintenanceService::mtl_init(mods_m_view_maintenance_service_))) { SERVER_LOG(WARN, "mods_m_view_maintenance_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObResourceLimitCalculator::mtl_init(mods_resource_limit_calculator_))) { SERVER_LOG(WARN, "mods_resource_limit_calculator_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObGlobalIteratorPool::mtl_init(mods_global_iterator_pool_))) { SERVER_LOG(WARN, "mods_global_iterator_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(common::ObRbMemMgr::mtl_init(mods_rb_mem_mgr_))) { SERVER_LOG(WARN, "mods_rb_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPluginVectorIndexService::mtl_init(mods_plugin_vector_index_service_))) { SERVER_LOG(WARN, "mods_plugin_vector_index_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObAutoSplitTaskCache::mtl_init(mods_auto_split_task_cache_))) { SERVER_LOG(WARN, "mods_auto_split_task_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(observer::ObTenantQueryRespTimeCollector::mtl_init(mods_tenant_query_resp_time_collector_))) { SERVER_LOG(WARN, "mods_tenant_query_resp_time_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObDDLServiceLauncher::mtl_init(mods_ddl_service_launcher_))) { SERVER_LOG(WARN, "mods_ddl_service_launcher_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObSysTenantLoadSysPackageService::mtl_init(mods_sys_tenant_load_sys_package_service_))) { SERVER_LOG(WARN, "mods_sys_tenant_load_sys_package_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObDDLScheduler::mtl_init(mods_ddl_scheduler_))) { SERVER_LOG(WARN, "mods_ddl_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObSQLCCLRuleManager::mtl_init(mods_sqlccl_rule_manager_))) { SERVER_LOG(WARN, "mods_sqlccl_rule_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTenantAiService::mtl_init(mods_tenant_ai_service_))) { SERVER_LOG(WARN, "mods_tenant_ai_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObChangeStreamMgr::mtl_init(mods_change_stream_mgr_))) { SERVER_LOG(WARN, "mods_change_stream_mgr_ fail", KR(ret)); }
  return ret;
}

int ObServer::obs_start_modules()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_FAIL(ObSharedTimer::mtl_start(mods_shared_timer_))) { SERVER_LOG(WARN, "mods_shared_timer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_meta_mem_mgr_))) { SERVER_LOG(WARN, "mods_tenant_meta_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_io_manager_))) { SERVER_LOG(WARN, "mods_tenant_io_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::mds::ObTenantMdsService::mtl_start(mods_tenant_mds_service_))) { SERVER_LOG(WARN, "mods_tenant_mds_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_shared_macro_block_mgr_))) { SERVER_LOG(WARN, "mods_shared_macro_block_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_shared_mem_alloc_mgr_))) { SERVER_LOG(WARN, "mods_shared_mem_alloc_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_trans_service_))) { SERVER_LOG(WARN, "mods_trans_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_log_service_))) { SERVER_LOG(WARN, "mods_log_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_ls_service_))) { SERVER_LOG(WARN, "mods_ls_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_storage_meta_service_))) { SERVER_LOG(WARN, "mods_tenant_storage_meta_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_tmp_file_manager_))) { SERVER_LOG(WARN, "mods_tenant_tmp_file_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_lock_wait_mgr_))) { SERVER_LOG(WARN, "mods_lock_wait_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_table_lock_service_))) { SERVER_LOG(WARN, "mods_table_lock_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_primary_major_freeze_service_))) { SERVER_LOG(WARN, "mods_primary_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_restore_major_freeze_service_))) { SERVER_LOG(WARN, "mods_restore_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_meta_checker_))) { SERVER_LOG(WARN, "mods_tenant_meta_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_lob_manager_))) { SERVER_LOG(WARN, "mods_lob_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_dead_lock_detector_mgr_))) { SERVER_LOG(WARN, "mods_dead_lock_detector_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_timestamp_service_))) { SERVER_LOG(WARN, "mods_timestamp_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDTLIntermResultManager::mtl_start(mods_dtl_interm_result_manager_))) { SERVER_LOG(WARN, "mods_dtl_interm_result_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_freezer_))) { SERVER_LOG(WARN, "mods_tenant_freezer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_check_point_service_))) { SERVER_LOG(WARN, "mods_check_point_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tablet_gc_service_))) { SERVER_LOG(WARN, "mods_tablet_gc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_tablet_scheduler_))) { SERVER_LOG(WARN, "mods_tenant_tablet_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_freeze_info_mgr_))) { SERVER_LOG(WARN, "mods_tenant_freeze_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tx_loop_worker_))) { SERVER_LOG(WARN, "mods_tx_loop_worker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_table_load_service_))) { SERVER_LOG(WARN, "mods_table_load_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_table_load_resource_service_))) { SERVER_LOG(WARN, "mods_table_load_resource_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_multi_version_garbage_collector_))) { SERVER_LOG(WARN, "mods_multi_version_garbage_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_dbms_sched_service_))) { SERVER_LOG(WARN, "mods_dbms_sched_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObOptStatMonitorManager::mtl_start(mods_opt_stat_monitor_manager_))) { SERVER_LOG(WARN, "mods_opt_stat_monitor_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_srs_))) { SERVER_LOG(WARN, "mods_tenant_srs_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_index_usage_info_mgr_))) { SERVER_LOG(WARN, "mods_index_usage_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_ft_dict_mgr_))) { SERVER_LOG(WARN, "mods_ft_dict_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_m_view_maintenance_service_))) { SERVER_LOG(WARN, "mods_m_view_maintenance_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_rb_mem_mgr_))) { SERVER_LOG(WARN, "mods_rb_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_plugin_vector_index_service_))) { SERVER_LOG(WARN, "mods_plugin_vector_index_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_tenant_ai_service_))) { SERVER_LOG(WARN, "mods_tenant_ai_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mtl_start_default(mods_change_stream_mgr_))) { SERVER_LOG(WARN, "mods_change_stream_mgr_ fail", KR(ret)); }
  return ret;
}

void ObServer::obs_stop_modules()
{
  mtl_stop_default(mods_ft_dict_mgr_);
  mtl_stop_default(mods_change_stream_mgr_);
  mtl_stop_default(mods_tenant_ai_service_);
  rootserver::ObDDLScheduler::mtl_stop(mods_ddl_scheduler_);
  mtl_stop_default(mods_plugin_vector_index_service_);
  mtl_stop_default(mods_rb_mem_mgr_);
  mtl_stop_default(mods_m_view_maintenance_service_);
  mtl_stop_default(mods_index_usage_info_mgr_);
  mtl_stop_default(mods_tenant_srs_);
  ObOptStatMonitorManager::mtl_stop(mods_opt_stat_monitor_manager_);
  mtl_stop_default(mods_dbms_sched_service_);
  mtl_stop_default(mods_multi_version_garbage_collector_);
  mtl_stop_default(mods_table_load_resource_service_);
  mtl_stop_default(mods_table_load_service_);
  mtl_stop_default(mods_tx_loop_worker_);
  mtl_stop_default(mods_tenant_freeze_info_mgr_);
  mtl_stop_default(mods_tenant_dag_scheduler_);
  mtl_stop_default(mods_tenant_compaction_mem_pool_);
  mtl_stop_default(mods_tenant_tablet_scheduler_);
  mtl_stop_default(mods_tablet_gc_service_);
  mtl_stop_default(mods_check_point_service_);
  mtl_stop_default(mods_tenant_freezer_);
  ObDTLIntermResultManager::mtl_stop(mods_dtl_interm_result_manager_);
  ObPxPools::mtl_stop(mods_px_pools_);
  ObPlanCache::mtl_stop(mods_plan_cache_);
  ObPsCache::mtl_stop(mods_ps_cache_);
  mtl_stop_default(mods_timestamp_service_);
  mtl_stop_default(mods_dead_lock_detector_mgr_);
  mtl_stop_default(mods_lob_manager_);
  mtl_stop_default(mods_tablet_table_updater_);
  mtl_stop_default(mods_tenant_meta_checker_);
  mtl_stop_default(mods_restore_major_freeze_service_);
  mtl_stop_default(mods_primary_major_freeze_service_);
  mtl_stop_default(mods_table_lock_service_);
  mtl_stop_default(mods_lock_wait_mgr_);
  mtl_stop_default(mods_tenant_tablet_stat_mgr_);
  mtl_stop_default(mods_tenant_tmp_file_manager_);
  mtl_stop_default(mods_tenant_storage_meta_service_);
  mtl_stop_default(mods_ls_service_);
  mtl_stop_default(mods_log_service_);
  mtl_stop_default(mods_trans_service_);
  mtl_stop_default(mods_shared_mem_alloc_mgr_);
  mtl_stop_default(mods_shared_macro_block_mgr_);
  storage::mds::ObTenantMdsService::mtl_stop(mods_tenant_mds_service_);
  mtl_stop_default(mods_tenant_io_manager_);
  mtl_stop_default(mods_tenant_meta_mem_mgr_);
  ObSharedTimer::mtl_stop(mods_shared_timer_);
}

void ObServer::obs_wait_modules()
{
  mtl_wait_default(mods_ft_dict_mgr_);
  mtl_wait_default(mods_change_stream_mgr_);
  mtl_wait_default(mods_tenant_ai_service_);
  rootserver::ObDDLScheduler::mtl_wait(mods_ddl_scheduler_);
  mtl_wait_default(mods_plugin_vector_index_service_);
  mtl_wait_default(mods_rb_mem_mgr_);
  mtl_wait_default(mods_m_view_maintenance_service_);
  mtl_wait_default(mods_index_usage_info_mgr_);
  mtl_wait_default(mods_tenant_srs_);
  ObOptStatMonitorManager::mtl_wait(mods_opt_stat_monitor_manager_);
  mtl_wait_default(mods_dbms_sched_service_);
  mtl_wait_default(mods_multi_version_garbage_collector_);
  mtl_wait_default(mods_table_load_resource_service_);
  mtl_wait_default(mods_table_load_service_);
  mtl_wait_default(mods_tx_loop_worker_);
  mtl_wait_default(mods_tenant_freeze_info_mgr_);
  mtl_wait_default(mods_tenant_dag_scheduler_);
  mtl_wait_default(mods_tenant_compaction_mem_pool_);
  mtl_wait_default(mods_tenant_tablet_scheduler_);
  mtl_wait_default(mods_tablet_gc_service_);
  mtl_wait_default(mods_check_point_service_);
  mtl_wait_default(mods_tenant_freezer_);
  ObDTLIntermResultManager::mtl_wait(mods_dtl_interm_result_manager_);
  mtl_wait_default(mods_timestamp_service_);
  mtl_wait_default(mods_dead_lock_detector_mgr_);
  mtl_wait_default(mods_lob_manager_);
  mtl_wait_default(mods_tablet_table_updater_);
  mtl_wait_default(mods_tenant_meta_checker_);
  mtl_wait_default(mods_restore_major_freeze_service_);
  mtl_wait_default(mods_primary_major_freeze_service_);
  mtl_wait_default(mods_table_lock_service_);
  mtl_wait_default(mods_lock_wait_mgr_);
  mtl_wait_default(mods_tenant_tablet_stat_mgr_);
  mtl_wait_default(mods_tenant_tmp_file_manager_);
  mtl_wait_default(mods_tenant_storage_meta_service_);
  mtl_wait_default(mods_ls_service_);
  mtl_wait_default(mods_log_service_);
  mtl_wait_default(mods_trans_service_);
  mtl_wait_default(mods_shared_mem_alloc_mgr_);
  mtl_wait_default(mods_shared_macro_block_mgr_);
  storage::mds::ObTenantMdsService::mtl_wait(mods_tenant_mds_service_);
  mtl_wait_default(mods_tenant_meta_mem_mgr_);
  ObTenantSQLSessionMgr::mtl_wait(mods_tenant_sql_session_mgr_);
  ObSharedTimer::mtl_wait(mods_shared_timer_);
}

void ObServer::obs_destroy_modules()
{
  mtl_destroy_default(mods_ft_dict_mgr_);
  mtl_destroy_default(mods_change_stream_mgr_);
  mtl_destroy_default(mods_tenant_ai_service_);
  ObSQLCCLRuleManager::mtl_destroy(mods_sqlccl_rule_manager_);
  mtl_destroy_default(mods_ddl_scheduler_);
  mtl_destroy_default(mods_sys_tenant_load_sys_package_service_);
  mtl_destroy_default(mods_ddl_service_launcher_);
  observer::ObTenantQueryRespTimeCollector::mtl_destroy(mods_tenant_query_resp_time_collector_);
  mtl_destroy_default(mods_auto_split_task_cache_);
  mtl_destroy_default(mods_plugin_vector_index_service_);
  mtl_destroy_default(mods_rb_mem_mgr_);
  ObGlobalIteratorPool::mtl_destroy(mods_global_iterator_pool_);
  mtl_destroy_default(mods_resource_limit_calculator_);
  mtl_destroy_default(mods_m_view_maintenance_service_);
  mtl_destroy_default(mods_tablet_memtable_mgr_pool_);
  mtl_destroy_default(mods_index_usage_info_mgr_);
  mtl_destroy_default(mods_tenant_srs_);
  mtl_destroy_default(mods_opt_stat_monitor_manager_);
  mtl_destroy_default(mods_dbms_sched_service_);
  ObEmptyReadBucket::mtl_destroy(mods_empty_read_bucket_);
  mtl_destroy_default(mods_tenant_cg_read_info_mgr_);
  ObFLTSpanMgr::mtl_destroy(mods_flt_span_mgr_);
  mtl_destroy_default(mods_multi_version_garbage_collector_);
  mtl_destroy_default(mods_table_load_resource_service_);
  ObTableLoadService::mtl_destroy(mods_table_load_service_);
  mtl_destroy_default(mods_access_service_);
  mtl_destroy_default(mods_tx_loop_worker_);
  mtl_destroy_default(mods_tenant_freeze_info_mgr_);
  mtl_destroy_default(mods_tenant_dag_scheduler_);
  mtl_destroy_default(mods_tenant_direct_load_mgr_);
  mtl_destroy_default(mods_ddl_merge_bucket_lock_);
  mtl_destroy_default(mods_tenant_compaction_mem_pool_);
  mtl_destroy_default(mods_tenant_medium_checker_);
  mtl_destroy_default(mods_tenant_tablet_scheduler_);
  mtl_destroy_default(mods_tablet_gc_service_);
  mtl_destroy_default(mods_check_point_service_);
  mtl_destroy_default(mods_tenant_freezer_);
  mtl_destroy_default(mods_tenant_schema_service_);
  mtl_destroy_default(mods_dasid_service_);
  ObDataAccessService::mtl_destroy(mods_data_access_service_);
  ObPlanMonitorNodeList::mtl_destroy(mods_plan_monitor_node_list_);
  ObDTLIntermResultManager::mtl_destroy(mods_dtl_interm_result_manager_);
  ObTenantSqlMemoryManager::mtl_destroy(mods_tenant_sql_memory_manager_);
  ObPxPools::mtl_destroy(mods_px_pools_);
  ObTenantDfc::mtl_destroy(mods_tenant_dfc_);
  mtl_destroy_default(mods_plan_cache_);
  mtl_destroy_default(mods_ps_cache_);
  mtl_destroy_default(mods_unique_id_service_);
  mtl_destroy_default(mods_trans_id_service_);
  mtl_destroy_default(mods_timestamp_access_);
  mtl_destroy_default(mods_timestamp_service_);
  mtl_destroy_default(mods_dead_lock_detector_mgr_);
  mtl_destroy_default(mods_global_auto_inc_service_);
  mtl_destroy_default(mods_lob_manager_);
  mtl_destroy_default(mods_diagnose_tablet_mgr_);
  mtl_destroy_default(mods_compaction_suggestion_mgr_);
  mtl_destroy_default(mods_schedule_suspect_info_mgr_);
  mtl_destroy_default(mods_dag_warning_history_manager_);
  mtl_destroy_default(mods_tenant_ss_table_merge_info_mgr_);
  mtl_destroy_default(mods_tablet_table_updater_);
  mtl_destroy_default(mods_tenant_meta_checker_);
  mtl_destroy_default(mods_restore_major_freeze_service_);
  mtl_destroy_default(mods_primary_major_freeze_service_);
  mtl_destroy_default(mods_table_lock_service_);
  mtl_destroy_default(mods_lock_wait_mgr_);
  mtl_destroy_default(mods_tenant_tablet_stat_mgr_);
  mtl_destroy_default(mods_server_compaction_event_history_);
  mtl_destroy_default(mods_tenant_compaction_progress_mgr_);
  mtl_destroy_default(mods_tenant_tmp_file_manager_);
  mtl_destroy_default(mods_tenant_storage_meta_service_);
  mtl_destroy_default(mods_ls_service_);
  ObLogService::mtl_destroy(mods_log_service_);
  mtl_destroy_default(mods_trans_service_);
  mtl_destroy_default(mods_shared_mem_alloc_mgr_);
  mtl_destroy_default(mods_shared_macro_block_mgr_);
  mtl_destroy_default(mods_tenant_mds_service_);
  ObTenantIOManager::mtl_destroy(mods_tenant_io_manager_);
  server_obj_pool_mtl_destroy<ObTableScanIterator>(mods_table_scan_iterator_obj_pool_);
  server_obj_pool_mtl_destroy<ObPartTransCtx>(mods_part_trans_ctx_obj_pool_);
  mtl_destroy_default(mods_tenant_meta_mem_mgr_);
  ObTenantSQLSessionMgr::mtl_destroy(mods_tenant_sql_session_mgr_);
  mtl_destroy_default(mods_shared_timer_);
}

} // namespace observer
} // namespace oceanbase
