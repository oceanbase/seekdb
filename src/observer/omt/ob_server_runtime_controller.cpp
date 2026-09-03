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
#include "lib/alloc/alloc_func.h"
#include "ob_server_runtime_controller.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "storage/tx_storage/ob_log_storage_adapter.h"
#include "storage/ob_file_system_router.h"
#include "logservice/ob_log_service.h"
#include "share/rc/ob_server_runtime.h"
#include "data_plane/report/ob_i_disk_report.h"
#include "observer/ob_server.h"
#include "ob_server_runtime.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "observer/mysql/obsm_conn_callback.h"
#include "sql/dtl/ob_dtl_fc_server.h"
#include "storage/allocator/ob_shared_memory_allocator_mgr.h"   // ObSharedMemAllocMgr
#include "ob_server_module_lifecycle.h"
#include "storage/concurrency_control/ob_multi_version_garbage_collector.h"
#include "storage/tx/ob_tx_loop_worker.h"
#include "storage/tx/ob_timestamp_service.h"
#include "storage/tx/ob_timestamp_access.h"
#include "storage/tx/ob_trans_id_service.h"
#include "storage/tx/ob_unique_id_service.h"
#include "storage/compaction/ob_tablet_scheduler.h"
#include "storage/tx_storage/ob_checkpoint_service.h"
#include "storage/tmp_file/ob_tmp_file_manager.h"
#include "storage/tx_storage/ob_memory_printer.h"
#include "storage/compaction/ob_compaction_progress.h"
#include "storage/compaction/ob_server_compaction_event_history.h"
#include "storage/memtable/ob_lock_wait_mgr.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/meta_store/ob_local_storage_meta_service.h"
#include "storage/tablelock/ob_table_lock_service.h"
#include "storage/compaction/ob_sstable_merge_info_mgr.h" // ObSSTableMergeInfoMgr
#include "storage/scheduler/ob_dag_warning_history_mgr.h"
#include "storage/access/ob_table_scan_iterator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/freeze/ob_major_freeze_service.h"
#include "observer/omt/ob_srs_service.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h" // ObDDLScheduler
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "observer/ob_system_package_load_service.h" // for ObSystemPackageLoadService
#include "observer/dbms_scheduler/ob_dbms_sched_service.h" // ObDBMSSchedService
#include "sql/plan_cache/ob_ps_cache.h"
#include "storage/access/ob_empty_read_bucket.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "observer/vector_index/ob_plugin_vector_index_service.h"
#include "observer/change_stream/ob_change_stream_mgr.h"
#include "share/roaringbitmap/ob_rb_memory_mgr.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"
#include "sql/session/ob_sql_session_mgr.h"
#include "observer/omt/ob_ai_service.h"
#include "storage/allocator/ob_memstore_allocator.h"  // relocated-definition owner
#include "share/io/ob_io_manager.h"  // relocated-definition owner
#include "share/ob_io_device_helper.h"
#include "share/rc/ob_server_module_init_ctx.h"
#include "storage/blocksstable/ob_shared_macro_block_manager.h"
// Single-runtime resource bring-up and refresh dependencies.
#include "share/resource/ob_server_resource_config.h"
#include "logservice/ob_log_allocator_mgr.h"        // LOG_ALLOCATOR_MGR_INSTANCE
#include "logservice/ob_server_log_block_mgr.h"

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
using namespace oceanbase::sql::dtl;
using namespace oceanbase::concurrency_control;
using namespace oceanbase::transaction;
using namespace oceanbase::transaction::tablelock;
using namespace oceanbase::logservice;
using namespace oceanbase::observer;
using namespace oceanbase::rootserver;
using namespace oceanbase::blocksstable;
using namespace oceanbase::tmp_file;


ObServerRuntimeController::ObServerRuntimeController()
    : is_inited_(false),
      runtime_(nullptr),
      refresh_interval_(10L * 1000L * 1000L),
      has_synced_(false),
      runtime_active_(false),
      timer_(),
      memory_printer_timer_(),
      timer_stopped_(true),
      log_block_mgr_(nullptr)
{
  if (lib::is_mini_mode()) {
    refresh_interval_ /= 2;
  }
}

template<typename T>
static int server_obj_pool_create(common::ObServerObjectPool<T> *&pool)
{
  int ret = common::OB_SUCCESS;
  pool = SERVER_NEW(common::ObServerObjectPool<T>, "TntSrvObjPool",
                    share::server_is_mini_mode(), share::server_cpu_count());
  if (OB_ISNULL(pool)) {
    ret = common::OB_ALLOCATE_MEMORY_FAILED;
  } else {
    ret = pool->init();
  }
  return ret;
}

template<typename T>
static void server_obj_pool_destroy(common::ObServerObjectPool<T> *&pool)
{
  using Pool = common::ObServerObjectPool<T>;
  SERVER_DELETE(Pool, "TntSrvObjPool", pool);
  pool = nullptr;
}

static ObLogRuntimeConfig current_log_runtime_config()
{
  return {
      GCONF.log_disk_utilization_threshold,
      GCONF.log_disk_utilization_limit_threshold,
      GCONF.log_disk_throttling_percentage,
      GCONF.log_disk_throttling_maximum_duration,
      GCONF.log_storage_warning_tolerance_time,
      GCONF._enable_log_cache,
  };
}

static int init_log_service(
    ObLogService *&log_service,
    storage::ObLogStorageAdapter *log_storage_adapter)
{
  int ret = OB_SUCCESS;
  const auto *runtime = share::server_runtime();
  const share::ObServerModuleInitCtx *module_init_ctx =
      OB_ISNULL(runtime) ? nullptr : runtime->get_module_init_ctx();
  if (OB_ISNULL(module_init_ctx) || OB_ISNULL(::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "log service composition is incomplete", KR(ret),
        KP(module_init_ctx), KP(::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>()));
  } else if (OB_FAIL(ObLogService::server_module_init(
      log_service,
      module_init_ctx->palf_options_,
      module_init_ctx->clog_dir_,
      OB_FILE_SYSTEM_ROUTER.get_clog_dir(),
      GCTX.self_addr(),
      log_storage_adapter,
      ::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>(),
      &LOCAL_DEVICE_INSTANCE,
      &OB_IO_MANAGER,
      false,
      GCONF.cpu_quota_concurrency,
      current_log_runtime_config()))) {
  } else {
    ::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>()->bind_log_service(*log_service);
  }
  return ret;
}

int ObServerRuntimeController::init(logservice::ObServerLogBlockMgr &log_block_mgr)
{
  int ret = OB_SUCCESS;

  if (is_inited_) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObServerRuntimeController has been inited", K(ret));
  }

  if (OB_SUCC(ret)) {
    log_block_mgr_ = &log_block_mgr;
    is_inited_ = true;
    LOG_INFO("succ to init multi runtime");
  }
  return ret;
}

int ObServerRuntimeController::start()
{
  int ret = OB_SUCCESS;

  ObMemoryPrinter &printer = ObMemoryPrinter::get_instance();

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (!timer_.inited()
        && OB_FAIL(timer_.init("ServerRuntimeTimer", ObMemAttr("RuntimeTimer")))) {
      LOG_ERROR("create multi runtime timer failed", K(ret));
    } else if (OB_FAIL(timer_.start())) {
    } else {
      timer_stopped_ = false;
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(timer_.schedule(*this, TIME_SLICE_PERIOD, true/*is_repeat*/))) {
    } else if (!memory_printer_timer_.inited()
        && OB_FAIL(memory_printer_timer_.init("MemPrinter", ObMemAttr("MemPrinter")))) {
      LOG_ERROR("create memory printer timer failed", K(ret));
    } else if (OB_FAIL(memory_printer_timer_.start())) {
    } else if (OB_FAIL(printer.register_timer_task(memory_printer_timer_))) {
    } else {
      LOG_INFO("succ to start multi runtime");
    }
  }


  if (OB_FAIL(ret)) {
    stop();
  }
  return ret;
}

void ObServerRuntimeController::stop()
{
  if (!timer_stopped_ && timer_.inited()) {
    timer_.stop();
    timer_stopped_ = true;
  }
  if (memory_printer_timer_.inited()) {
    memory_printer_timer_.stop();
  }
  stop_runtime_();
}

void ObServerRuntimeController::wait()
{
  if (OB_NOT_NULL(runtime_)) {
    while (OB_EAGAIN == runtime_->try_wait()) {
      usleep(50 * 1000);
    }
  }
  if (timer_.inited()) {
    timer_.wait();
  }
  if (memory_printer_timer_.inited()) {
    memory_printer_timer_.wait();
  }
}


void ObServerRuntimeController::destroy()
{
  if (OB_NOT_NULL(runtime_)) {
    runtime_->destroy();
    ob_delete(runtime_);
    runtime_ = nullptr;
  }
  int tmp_ret = OB_SUCCESS;
  if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::data_plane::ObIDiskReport>())) {
    tmp_ret = ::oceanbase::share::server_service<::oceanbase::data_plane::ObIDiskReport>()->delete_usage_stat();
    if (OB_SUCCESS != tmp_ret) {
      LOG_WARN_RET(tmp_ret, "fail to delete runtime disk usage during shutdown", K(tmp_ret));
    }
  }
  timer_.destroy();
  memory_printer_timer_.destroy();
  log_block_mgr_ = nullptr;
  is_inited_ = false;
}

int ObServerRuntimeController::construct_bootstrap_meta(ObServerRuntimeMeta &meta)
{
  int ret = OB_SUCCESS;


  ObServerRuntimeSuperBlock super_block(true/*is_hidden*/);
  share::ObServerRuntimeConfig runtime_config;
  const bool has_memstore = true;
  share::ObServerResourceConfig resource_config;
  if (OB_ISNULL(log_block_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("log block manager is not initialized", KR(ret));
  } else if (OB_FAIL(resource_config.generate_default(log_block_mgr_->get_log_disk_size()))) {
  } else if (OB_FAIL(runtime_config.init(resource_config,
                        lib::Worker::CompatMode::MYSQL,
                        has_memstore))) {
  } else if (OB_FAIL(meta.build(runtime_config, super_block))) {
  }

  return ret;
}

int ObServerRuntimeController::create_bootstrap_runtime()
{
  int ret = OB_SUCCESS;
  ObServerRuntimeMeta meta;
  if (OB_FAIL(construct_bootstrap_meta(meta))) {
  } else if (OB_FAIL(create_runtime(meta, true /* write_slog */))) {
  }
  return ret;
}

int ObServerRuntimeController::refresh_runtime_resources()
{
  int ret = OB_SUCCESS;

  omt::ObServerRuntime *runtime = nullptr;
  SMART_VAR(ObServerRuntimeMeta, meta) {
    if (OB_FAIL(get_runtime_unsafe(runtime))) {
    } else if (OB_FAIL(construct_bootstrap_meta(meta))) {
    } else if (!runtime->is_hidden() || meta.runtime_config_ == runtime->get_runtime_config()) {
      // do nothing
    } else if (OB_FAIL(update_server_resources_no_lock(meta.runtime_config_))) {
    }
  }
  return ret;
}

int ObServerRuntimeController::activate_runtime(const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;

  ObServerRuntime *runtime = nullptr;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (!runtime->is_hidden()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime is already active", K(ret));
  } else {
    HEAP_VAR(ObServerRuntimeSuperBlock, new_super_block) {
      new_super_block = runtime->get_super_block();
      new_super_block.is_hidden_ = false;
      if (OB_FAIL(update_server_resources_no_lock(runtime_config))) {
      } else if (OB_FAIL(SERVER_STORAGE_META_PERSISTER.update_runtime_super_block(new_super_block))) {
      } else {
        runtime->set_server_super_block(new_super_block);
      }
    }
  }

  FLOG_INFO("finish activate_runtime", K(ret));

  return ret;
}

#ifdef ENABLE_DEBUG_LOG
ERRSIM_POINT_DEF(ERRSIM_CREATE_RUNTIME_FAILURE)
#endif

int ObServerRuntimeController::create_runtime(const ObServerRuntimeMeta &meta, bool write_slog)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;

  ObServerRuntime *runtime = nullptr;
  ObRuntimeCreateStep create_step = ObRuntimeCreateStep::STEP_BEGIN;  // step0

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_ERROR("not init", K(ret));
  } else if (OB_UNLIKELY(!meta.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_ERROR("invalid argument", K(ret), K(meta));
  } else if (OB_SUCC(get_runtime_unsafe(runtime))) {
    ret = OB_SERVER_RUNTIME_ALREADY_ACTIVE;
    LOG_WARN("runtime exist", K(ret));
  } else {
    ret = OB_SUCCESS;
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else if (write_slog) {
    if (OB_FAIL(SERVER_STORAGE_META_PERSISTER.prepare_create_runtime(meta))) {
    } else {
      create_step = ObRuntimeCreateStep::STEP_CREATION_PREPARED; // step4
    }
  }

  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(runtime_ = OB_NEW(ObServerRuntime, ObModIds::OMT))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    LOG_WARN("new runtime fail", K(ret));
  } else if (FALSE_IT(create_step = ObRuntimeCreateStep::STEP_RUNTIME_CREATED)) { //step5
  } else {
    CREATE_WITH_TEMP_ENTITY(RESOURCE_OWNER, runtime_->id()) {
      if (OB_FAIL(runtime_->init(meta))) {
      }
    }
  }

  if (OB_FAIL(ret)) {
    // do nothing
  } else {
    const int64_t memory_budget = lib::get_memory_budget();
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>()
                    ->set_memory_limit(memory_budget, memory_budget))) {
    }
  }
  if (OB_SUCC(ret)) {
    if (write_slog && OB_FAIL(SERVER_STORAGE_META_PERSISTER.commit_create_runtime())) {
      LOG_ERROR("fail to write create runtime commit slog", K(ret));
    } else {
      runtime_->set_create_status(ObServerRuntimeCreateStatus::CREATED);
      create_step = ObRuntimeCreateStep::STEP_FINISH; // step6
    }
  }

  runtime_active_ = true;
  // TODO: @lingyang Expected not to fail
  if (OB_TMP_FAIL(update_server_config())) {
  }

#ifdef ENABLE_DEBUG_LOG
  ret = ERRSIM_CREATE_RUNTIME_FAILURE ? ERRSIM_CREATE_RUNTIME_FAILURE : ret;
#endif

  if (OB_FAIL(ret)) {
    do {
      tmp_ret = OB_SUCCESS;
      if (create_step >= ObRuntimeCreateStep::STEP_RUNTIME_CREATED) {
        if (OB_NOT_NULL(runtime_)) {
          runtime_->stop();
          while (OB_SUCCESS != runtime_->try_wait()) {
            ob_usleep(100 * 1000);
          }
          runtime_->destroy();
          ob_delete(runtime_);
          runtime_ = nullptr;
        }
        if (write_slog && OB_SUCCESS != (tmp_ret = SERVER_STORAGE_META_PERSISTER.clear_runtime_log_dirs())) {
          LOG_ERROR("fail to clear persistent data", K(tmp_ret));
          SLEEP(1);
        }
      }
    } while (OB_SUCCESS != tmp_ret);

    if (write_slog && create_step >= ObRuntimeCreateStep::STEP_CREATION_PREPARED) {
      if (OB_SUCCESS != (tmp_ret = SERVER_STORAGE_META_PERSISTER.abort_create_runtime())) {
      }
    }
  }

  FLOG_INFO("finish create new runtime", K(ret), K(write_slog), K(create_step));

  return ret;
}

int ObServerRuntimeController::update_server_resources_no_lock(const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  // Serialize resource writers during bootstrap and configuration reload.
  lib::ObMutexGuard guard(resource_conf_lock_);

  ObServerRuntime *runtime = nullptr;
  const double min_cpu = GCONF.get_server_default_min_cpu();
  const double max_cpu = GCONF.get_server_default_max_cpu();
  int64_t log_disk_size = 0;

  ObServerRuntimeConfig allowed_runtime_config;
  ObServerRuntimeConfig old_runtime_config;
  int64_t allowed_new_log_disk_size = 0;
  bool need_persist_config = false;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(log_block_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("log block manager is not initialized", KR(ret));
  } else if (FALSE_IT(log_disk_size = runtime_config.resource_config_.log_disk_size())) {
  } else if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("runtime is nullptr");
  } else if (OB_FAIL(old_runtime_config.assign(runtime->get_runtime_config()))) {
  } else if (OB_FAIL(update_server_log_disk_size(old_runtime_config.resource_config_.log_disk_size(),
                                                 log_disk_size,
                                                 allowed_new_log_disk_size))) {
  } else if (OB_FAIL(construct_allowed_runtime_config(allowed_new_log_disk_size,
                                                   max_cpu, min_cpu,
                                                   runtime_config,
                                                   allowed_runtime_config))) {
  } else if (FALSE_IT(need_persist_config = !(old_runtime_config == allowed_runtime_config))) {
  } else if (need_persist_config
             && OB_FAIL(SERVER_STORAGE_META_PERSISTER.update_server_resources(allowed_runtime_config))) {
    LOG_WARN("failed to update runtime config", K(ret));
  } else {
    if (runtime->min_cpu() != min_cpu) {
      runtime->set_min_cpu(min_cpu);
    }
    if (runtime->max_cpu() != max_cpu) {
      runtime->set_max_cpu(max_cpu);
    }
    runtime->set_server_resources(allowed_runtime_config);
    LOG_INFO("succeeded in setting runtime config", K(need_persist_config), K(allowed_runtime_config));
  }

  return ret;
}

int ObServerRuntimeController::update_server_memory(const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = nullptr;

  const int64_t memory_budget = GMEMCONF.get_server_memory_budget();
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("runtime is nullptr");
  } else if (OB_FAIL(update_freezer_mem_limit(memory_budget, memory_budget))) {
  } else if (OB_FAIL(update_throttle_config_())) {
  } else if (FALSE_IT(runtime->set_memory_size(memory_budget))) {
    // unreachable
  }
  return ret;
}

int ObServerRuntimeController::construct_allowed_runtime_config(const int64_t allowed_new_log_disk_size,
                                                 const int64_t max_cpu, const int64_t min_cpu,
                                                 const ObServerRuntimeConfig &expected_runtime_config,
                                                 ObServerRuntimeConfig &allowed_runtime_config)
{
  int ret = OB_SUCCESS;
  if (0 > allowed_new_log_disk_size
      || !expected_runtime_config.is_valid()) {
    ret= OB_INVALID_ARGUMENT;
  } else if (OB_FAIL(allowed_runtime_config.assign(expected_runtime_config))) {
  } else {
    // construct allowed resource.
    ObServerResource allowed_resource(
        max_cpu,
        min_cpu,
        expected_runtime_config.resource_config_.memory_size(),
        allowed_new_log_disk_size,
        expected_runtime_config.resource_config_.max_iops(),
        expected_runtime_config.resource_config_.min_iops(),
        expected_runtime_config.resource_config_.iops_weight(),
        expected_runtime_config.resource_config_.max_net_bandwidth(),
        expected_runtime_config.resource_config_.net_bandwidth_weight());
    if (OB_FAIL(allowed_runtime_config.resource_config_.update_resource(allowed_resource))) {
    }
  }
  return ret;
}

int ObServerRuntimeController::update_server_resources(const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;

  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(update_server_resources_no_lock(runtime_config))) {
  }

  LOG_INFO("finished updating runtime config", K(ret), K(runtime_config));

  return ret;
}

int ObServerRuntimeController::update_server_log_disk_size(const int64_t old_log_disk_size,
                                               const int64_t new_log_disk_size,
                                               int64_t &allowed_new_log_disk_size)
{
  int ret = OB_SUCCESS;
  ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (OB_ISNULL(log_block_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("log block manager is not initialized", K(ret));
  } else if (OB_ISNULL(log_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get log_service failed", K(ret));
  } else if (OB_FAIL(log_block_mgr_->update_log_disk_size(
                 old_log_disk_size,
                 new_log_disk_size,
                 allowed_new_log_disk_size,
                 log_service))) {
  } else {
    LOG_INFO("update_server_log_disk_size success", K(old_log_disk_size),
             K(new_log_disk_size), K(allowed_new_log_disk_size));
  }
  return ret;
}


int ObServerRuntimeController::update_server_config()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_TMP_FAIL(update_palf_config())) {
  }
  if (OB_TMP_FAIL(update_dag_scheduler_config())) {
  }
  if (OB_TMP_FAIL(update_freezer_config_())) {
  }
  if (OB_TMP_FAIL(update_throttle_config_())) {
  }
  LOG_INFO("update_server_config success");
  return ret;
}

int ObServerRuntimeController::update_palf_config()
{
  int ret = OB_SUCCESS;
  ObLogService *log_service = ::oceanbase::share::server_service<::oceanbase::logservice::ObLogService>();
  if (NULL == log_service) {
    ret = OB_ERR_UNEXPECTED;
  } else {
    ret = log_service->update_palf_options_except_disk_usage_limit_size(
        current_log_runtime_config());
  }
  return ret;
}

int ObServerRuntimeController::update_dag_scheduler_config()
{
  int ret = OB_SUCCESS;
  ObDagScheduler *dag_scheduler = ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>();
  if (OB_ISNULL(dag_scheduler)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag scheduler should not be null", K(ret));
  } else {
    dag_scheduler->reload_config();
  }
  return ret;
}

int ObServerRuntimeController::update_freezer_config_()
{
  int ret = OB_SUCCESS;
  ObMemstoreFreezer *freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>();
  if (NULL == freezer) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("runtime freezer should not be null", K(ret));
  } else if (OB_FAIL(freezer->reload_config())) {
  }
  return ret;
}

int ObServerRuntimeController::update_throttle_config_()
{
  int ret = OB_SUCCESS;
  {
    SERVER_MODULE_SCOPE {
      ObSharedMemAllocMgr *share_mem_alloc_mgr = ::oceanbase::share::server_service<::oceanbase::share::ObSharedMemAllocMgr>();

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

int ObServerRuntimeController::update_freezer_mem_limit(const int64_t server_min_mem,
                                                        const int64_t server_max_mem)
{
  int ret = OB_SUCCESS;

  ObMemstoreFreezer *freezer = nullptr;
  if (FALSE_IT(freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>())) {
  } else if (freezer->is_memory_limit_changed(server_min_mem, server_max_mem)) {
    if (OB_FAIL(freezer->set_memory_limit(server_min_mem, server_max_mem))) {
    }
  }
  return ret;
}

int ObServerRuntimeController::get_server_resources(ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = nullptr;
  if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else {
    runtime_config = runtime->get_runtime_config();
  }

  return ret;
}

int ObServerRuntimeController::get_server_log_disk_size(int64_t &log_disk_size)
{
  int ret = OB_SUCCESS;
  ObServerRuntimeConfig runtime_config;
  if (OB_FAIL(get_server_resources(runtime_config))) {
  } else {
    log_disk_size = runtime_config.resource_config_.log_disk_size();
  }
  return ret;
}

int ObServerRuntimeController::get_runtime_meta_for_ckpt(ObServerRuntimeMeta &meta, bool &exist)
{
  int ret = OB_SUCCESS;
  // The runtime pointer is stable between startup and shutdown.
  exist = false;
  if (OB_ISNULL(runtime_) || !runtime_active_) {
  } else {
    meta = runtime_->get_runtime_meta();
    exist = true;
  }

  return ret;
}

storage::ObServerRuntimeSuperBlock ObServerRuntimeController::get_super_block()
{
  return runtime_->get_super_block();
}

void ObServerRuntimeController::set_server_super_block(
    const storage::ObServerRuntimeSuperBlock &super_block)
{
  runtime_->set_server_super_block(super_block);
}

bool ObServerRuntimeController::is_hidden()
{
  return runtime_->is_hidden();
}

int ObServerRuntimeController::modify_server_io(const ObServerResourceConfig &resource_config)
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = NULL;

  if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected condition, runtime is NULL", K(runtime));
  } else {
    ObIOServiceConfig::ResourceConfig io_resource_config(resource_config);
    ObIOServiceConfig::ParamConfig io_param_config;
    io_param_config.memory_limit_ = resource_config.memory_size();
    io_param_config.callback_thread_count_ = GCONF._io_callback_thread_count;
    if (OB_FAIL(OB_IO_MANAGER.refresh_io_resource_config(io_resource_config))) {
    } else if (OB_FAIL(OB_IO_MANAGER.refresh_io_param_config(io_param_config))) {
    }
  }
  return ret;
}

bool ObServerRuntimeController::has_runtime() const
{
  ObServerRuntime *runtime = NULL;
  int ret = get_runtime_unsafe(runtime);
  return OB_SUCCESS == ret && NULL != runtime;
}

void ObServerRuntimeController::stop_runtime_()
{
  int ret = OB_SUCCESS;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(runtime_) || !runtime_active_) {
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("unexpected condition", K(ret));
  } else {
    runtime_->stop();
    runtime_active_ = false;
    if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->kill_all_sessions(true))) {
    }
  }
}

int ObServerRuntimeController::get_runtime(
    ObServerRuntime *&runtime) const
{
  return get_runtime_unsafe(runtime);
}

int ObServerRuntimeController::lock_runtime(
  ObServerRuntime *&runtime) const
{
  ObServerRuntime *runtime_tmp = nullptr;
  int ret = get_runtime_unsafe(runtime_tmp);
  if (OB_SUCC(ret)) {
    if (OB_FAIL(runtime_tmp->try_rdlock())) {
      if (runtime_tmp->has_stopped()) {
        // in some cases this error code is handled specially
        ret = OB_SERVER_RUNTIME_NOT_READY;
        LOG_WARN("fail to try rdlock runtime", K(ret));
      }
    } else {
      // assign runtime when get rdlock succ
      runtime = runtime_tmp;
    }
    if (OB_UNLIKELY(runtime_tmp->has_stopped())) {
      LOG_WARN("get rdlock when runtime has stopped", K(lbt()));
    }
  }
  return ret;
}

int ObServerRuntimeController::get_runtime_unsafe(ObServerRuntime *&runtime) const
{
  int ret = OB_SUCCESS;
  runtime = NULL;
  if (OB_ISNULL(runtime_) || !runtime_active_) {
    ret = OB_SERVER_RUNTIME_NOT_READY;
  } else {
    runtime = runtime_;
  }
  return ret;
}

int ObServerRuntimeController::recv_request(ObRequest &req) const
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = NULL;
  if (IS_NOT_INIT) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (NULL == runtime) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("runtime is null", K(ret));
  } else if (OB_FAIL(runtime->recv_request(req))) {
  } else {
    // do nothing
  }
  return ret;
}



int ObServerRuntimeController::get_server_cpu(double &min_cpu, double &max_cpu) const
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = NULL;
  if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (NULL != runtime) {
    min_cpu = runtime->min_cpu();
    max_cpu = runtime->max_cpu();
  }
  return ret;
}

// Materialize the single runtime resource config from GCONF.
int ObServerRuntimeController::build_server_resource_config_(ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;
  int64_t log_disk_size = 0;
  ObServerResourceConfig resource_config;
  if (OB_ISNULL(log_block_mgr_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("log block manager is not initialized", KR(ret));
  // Keep the default automatic limit chosen during bootstrap stable. An
  // explicit size or percentage remains dynamically effective.
  } else if (0 == GCONF.log_disk_size
             && 0 == GCONF.log_disk_percentage
             && has_runtime()) {
    if (OB_FAIL(get_server_log_disk_size(log_disk_size))) {
      LOG_WARN("fail to get persisted runtime log disk size", KR(ret));
    }
  } else if (FALSE_IT(log_disk_size = log_block_mgr_->get_log_disk_size())) {
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(resource_config.generate_default(log_disk_size))) {
  } else if (OB_FAIL(runtime_config.init(resource_config,
                               lib::Worker::CompatMode::MYSQL/*compat_mode*/,
                               true/*has_memstore*/))) {
  }
  return ret;
}

// Apply the resource config to the live runtime.
int ObServerRuntimeController::apply_server_resource_config_(const ObServerRuntimeConfig &runtime_config)
{
  int ret = OB_SUCCESS;

  ObServerRuntime *runtime = nullptr;
  if (OB_FAIL(get_runtime(runtime))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("server runtime must exist", K(ret));
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime should not be null here", KR(ret));
  } else if (runtime->has_stopped()) {
    LOG_INFO("runtime has been stopped, no need to update", KR(ret));
  } else {
    if (runtime->is_hidden() && OB_FAIL(activate_runtime(runtime_config))) {
      LOG_WARN("fail to activate server runtime", K(runtime_config));
    }
    if (OB_SUCC(ret) && OB_FAIL(update_server_resources(runtime_config))) {
      LOG_WARN("failed to update runtime config", K(ret));
    }
    if (OB_SUCC(ret) && OB_FAIL(update_server_memory(runtime_config))) {
      LOG_ERROR("fail to update runtime memory", K(ret));
    }
    if (OB_SUCC(ret)) {
      if (OB_FAIL(modify_server_io(runtime_config.resource_config_))) {
      }
    }
  }
  return ret;
}

// Bring the single runtime fully up from the bootstrap state.
int ObServerRuntimeController::bring_up_runtime_()
{
  int ret = OB_SUCCESS;
  ObServerRuntimeConfig runtime_config;
  if (OB_FAIL(build_server_resource_config_(runtime_config))) {
  } else if (OB_FAIL(apply_server_resource_config_(runtime_config))) {
  } else {
    set_synced();
    LOG_INFO("server runtime is ready", K(runtime_config));
  }
  return ret;
}

int ObServerRuntimeController::bring_up_runtime()
{
  return bring_up_runtime_();
}

// Refresh the live resource config from GCONF.
int ObServerRuntimeController::refresh_server_config_()
{
  int ret = OB_SUCCESS;
  ObServerRuntimeConfig runtime_config;
  ObCurTraceId::init(GCONF.self_addr_);
  if (!SERVER_STORAGE_META_SERVICE.is_started()) {
    // do nothing if not finish replaying slog
    LOG_INFO("server slog not finish replaying, need wait");
    ret = OB_NEED_RETRY;
  } else if (OB_FAIL(build_server_resource_config_(runtime_config))) {
  } else if (OB_FAIL(apply_server_resource_config_(runtime_config))) {
  } else {
    set_synced();
    periodically_check_runtime_();
  }

  FLOG_INFO("refresh runtime resource config", K(runtime_config), KR(ret));

  // Keep the log allocator sizing aligned with the current runtime memory budget.
  int tmp_ret = OB_SUCCESS;
  if (OB_SUCCESS != (tmp_ret = LOG_ALLOCATOR_MGR_INSTANCE.update_memory_limit(runtime_config))) {
  }

  FLOG_INFO("refresh runtime config", K(ret));

  return ret;
}

// Per-tick runtime upkeep.
void ObServerRuntimeController::periodically_check_runtime_()
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = this->runtime();
  bool locked = false;
  if (!OB_ISNULL(runtime) && !runtime->has_stopped()) {
    if (OB_FAIL(runtime->rdlock())) {
    } else {
      locked = true;
    }
  }
  if (locked) {
    runtime->periodically_check();
    IGNORE_RETURN runtime->unlock();
  }
}

int64_t ObServerRuntimeController::get_refresh_interval_()
{
  if (!has_synced()) {
    return BOOTSTRAP_REFRESH_INTERVAL;
  } else {
    return refresh_interval_;
  }
}

int ObServerRuntimeController::get_server_allocated_resource(ServerResource &server_resource)
{
  int ret = OB_SUCCESS;
  server_resource.reset();
  if (OB_ISNULL(runtime_) || !runtime_active_ || runtime_->is_hidden()) {
    // no live runtime -> zero resource
  } else {
    const share::ObServerRuntimeConfig runtime_config = runtime_->get_runtime_config();
    server_resource.max_cpu_ += runtime_config.resource_config_.max_cpu();
    server_resource.min_cpu_ += runtime_config.resource_config_.min_cpu();
    server_resource.memory_size_ += runtime_config.resource_config_.memory_size();
    server_resource.log_disk_size_ += runtime_config.resource_config_.log_disk_size();
  }
  return ret;
}

void ObServerRuntimeController::runTimerTask()
{
  if (OB_NOT_NULL(runtime_) && runtime_active_) {
    runtime_->timeup();
  }

  if (is_inited_ && REACH_TIME_INTERVAL(get_refresh_interval_())) {
    refresh_server_config_();
  }

  if (REACH_TIME_INTERVAL(10000000L)) {  // every 10s
    ObDIActionGuard ag("dump runtime info");
    if (!OB_ISNULL(runtime_)) {
      ObTaskController::get().allow_next_syslog();
      LOG_INFO("dump runtime info", "runtime", *runtime_);
    }
  }
}

void ObServerRuntimeController::reload_request_queue_size()
{
  if (OB_NOT_NULL(runtime_)) {
    runtime_->set_queue_limit(GCONF.server_task_queue_size);
  }
}

int ObSharedTimer::server_module_init(ObSharedTimer *&st)
{
  int ret = common::OB_SUCCESS;
  if (st != NULL) {
    if (OB_FAIL(st->timer_.init("TntSharedTimer", common::ObMemAttr("TntSharedTimer")))) {
    }
  }
  return ret;
}

int ObSharedTimer::server_module_start(ObSharedTimer *&st)
{
  int ret = common::OB_SUCCESS;
  if (st != NULL) {
    if (OB_FAIL(st->timer_.start())) {
    }
  }
  return ret;
}

void ObSharedTimer::server_module_stop(ObSharedTimer *&st)
{
  if (st != NULL && st->timer_.inited()) {
    st->timer_.stop();
  }
}

void ObSharedTimer::server_module_wait(ObSharedTimer *&st)
{
  if (st != NULL && st->timer_.inited()) {
    st->timer_.wait();
  }
}

void ObSharedTimer::destroy()
{
  timer_.destroy();
}

int ObSharedTimer::schedule(common::ObTimerTask &task, const int64_t delay,
    const bool repeat, const bool immediate)
{
  return timer_.schedule(task, delay, repeat, immediate);
}

int ObSharedTimer::cancel_task(const common::ObTimerTask &task)
{
  return timer_.cancel_task(task);
}

int ObSharedTimer::wait_task(const common::ObTimerTask &task)
{
  return timer_.wait_task(task);
}

bool ObSharedTimer::task_exist(const common::ObTimerTask &task)
{
  return timer_.task_exist(task);
}

int ObServerRuntimeController::inc_ddl_count(const int64_t cpu_quota_concurrency)
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = NULL;
  if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime is null", KR(ret));
  } else {
    if (runtime->check_ddl_thread_is_limit(cpu_quota_concurrency)) {
      ret = OB_DDL_RESOURCE_NOT_ENOUGH;
      LOG_WARN("runtime ddl task larger than limit, need retry", KR(ret), K(runtime->cur_ddl_thread_count()));
    } else {
      lib::Thread::set_doing_ddl(true);
      runtime->inc_ddl_thread_count();
    }
  }
  return ret;
}

int ObServerRuntimeController::dec_ddl_count()
{
  int ret = OB_SUCCESS;
  ObServerRuntime *runtime = NULL;
  if (OB_FAIL(get_runtime_unsafe(runtime))) {
  } else if (OB_ISNULL(runtime)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("runtime is null", KR(ret));
  } else {
    lib::Thread::set_doing_ddl(false);
    runtime->dec_ddl_thread_count();
    if (runtime->cur_ddl_thread_count() < 0) {
      LOG_ERROR("runtime ddl count is less than 0, please check", K(runtime->cur_ddl_thread_count()));
    } else {
      LOG_TRACE("runtime ddl count", K(runtime->cur_ddl_thread_count()));
    }
  }
  return ret;
}

namespace oceanbase
{
namespace common
{
void ObIOManager::print_service_status()
{
  int ret = OB_SUCCESS;
  if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>())) {
    {
      ObRefHolder<ObIOService> service_holder;
      if (OB_FAIL(get_io_service(service_holder))) {
        if (OB_HASH_NOT_EXIST != ret) {
          LOG_WARN("get runtime io manager failed", K(ret), K(1UL));
        } else {
          ret = OB_SUCCESS;
        }
      } else {
        service_holder.get_ptr()->print_io_status();
      }
    }
  }
  if (OB_NOT_NULL(io_service_)) {
    io_service_->print_io_status();
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
  omt::ObRuntimeConfigGuard runtime_config(RUNTIME_CONF());
  if (runtime_config.is_valid()) {
    max_schema_slot_num = runtime_config->_max_schema_slot_num;
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
int ObServer::set_memstore_threshold()
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(mods_shared_mem_alloc_mgr_)) {
    ret = OB_NOT_INIT;
  } else {
    ret = mods_shared_mem_alloc_mgr_->memstore_allocator().set_memstore_threshold();
  }
  return ret;
}

int ObServer::obs_construct_modules()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_shared_timer_))) { SERVER_LOG(WARN, "mods_shared_timer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_shared_macro_block_mgr_))) { SERVER_LOG(WARN, "mods_shared_macro_block_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObStorageMetaMemMgr::server_module_new(mods_storage_meta_mem_mgr_))) { SERVER_LOG(WARN, "mods_storage_meta_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_obj_pool_create<ObTableScanIterator>(mods_table_scan_iterator_obj_pool_))) { SERVER_LOG(WARN, "mods_table_scan_iterator_obj_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObIOService::server_module_new(mods_io_service_))) { SERVER_LOG(WARN, "mods_io_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_mds_service_))) { SERVER_LOG(WARN, "mods_mds_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_shared_mem_alloc_mgr_))) { SERVER_LOG(WARN, "mods_shared_mem_alloc_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_trans_service_))) { SERVER_LOG(WARN, "mods_trans_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_log_storage_adapter_))) { SERVER_LOG(WARN, "mods_log_storage_adapter_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_log_service_))) { SERVER_LOG(WARN, "mods_log_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_ls_service_))) { SERVER_LOG(WARN, "mods_ls_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_local_storage_meta_service_))) { SERVER_LOG(WARN, "mods_local_storage_meta_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tmp_file_manager_))) { SERVER_LOG(WARN, "mods_tmp_file_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_compaction_progress_mgr_))) { SERVER_LOG(WARN, "mods_compaction_progress_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_server_compaction_event_history_))) { SERVER_LOG(WARN, "mods_server_compaction_event_history_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tablet_stat_mgr_))) { SERVER_LOG(WARN, "mods_tablet_stat_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_lock_wait_mgr_))) { SERVER_LOG(WARN, "mods_lock_wait_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_table_lock_service_))) { SERVER_LOG(WARN, "mods_table_lock_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_primary_major_freeze_service_))) { SERVER_LOG(WARN, "mods_primary_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_restore_major_freeze_service_))) { SERVER_LOG(WARN, "mods_restore_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tablet_runtime_meta_updater_))) { SERVER_LOG(WARN, "mods_tablet_runtime_meta_updater_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_sstable_merge_info_mgr_))) { SERVER_LOG(WARN, "mods_sstable_merge_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_dag_warning_history_manager_))) { SERVER_LOG(WARN, "mods_dag_warning_history_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_schedule_suspect_info_mgr_))) { SERVER_LOG(WARN, "mods_schedule_suspect_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_compaction_suggestion_mgr_))) { SERVER_LOG(WARN, "mods_compaction_suggestion_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_diagnose_tablet_mgr_))) { SERVER_LOG(WARN, "mods_diagnose_tablet_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObLobManager::server_module_new(mods_lob_manager_))) { SERVER_LOG(WARN, "mods_lob_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_dead_lock_detector_mgr_))) { SERVER_LOG(WARN, "mods_dead_lock_detector_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_timestamp_service_))) { SERVER_LOG(WARN, "mods_timestamp_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_timestamp_access_))) { SERVER_LOG(WARN, "mods_timestamp_access_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_trans_id_service_))) { SERVER_LOG(WARN, "mods_trans_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_unique_id_service_))) { SERVER_LOG(WARN, "mods_unique_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_ps_cache_))) { SERVER_LOG(WARN, "mods_ps_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_plan_cache_))) { SERVER_LOG(WARN, "mods_plan_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDfc::server_module_new(mods_dfc_))) { SERVER_LOG(WARN, "mods_dfc_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_px_pools_))) { SERVER_LOG(WARN, "mods_px_pools_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObSqlMemoryManager::server_module_new(mods_sql_memory_manager_))) { SERVER_LOG(WARN, "mods_sql_memory_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_dtl_interm_result_manager_))) { SERVER_LOG(WARN, "mods_dtl_interm_result_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_data_access_service_))) { SERVER_LOG(WARN, "mods_data_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_schema_runtime_service_))) { SERVER_LOG(WARN, "mods_schema_runtime_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_memstore_freezer_))) { SERVER_LOG(WARN, "mods_memstore_freezer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_check_point_service_))) { SERVER_LOG(WARN, "mods_check_point_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tablet_gc_service_))) { SERVER_LOG(WARN, "mods_tablet_gc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tablet_scheduler_))) { SERVER_LOG(WARN, "mods_tablet_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_medium_checker_))) { SERVER_LOG(WARN, "mods_medium_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_compaction_mem_pool_))) { SERVER_LOG(WARN, "mods_compaction_mem_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_dag_scheduler_))) { SERVER_LOG(WARN, "mods_dag_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_freeze_info_mgr_))) { SERVER_LOG(WARN, "mods_freeze_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tx_loop_worker_))) { SERVER_LOG(WARN, "mods_tx_loop_worker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_access_service_))) { SERVER_LOG(WARN, "mods_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_multi_version_garbage_collector_))) { SERVER_LOG(WARN, "mods_multi_version_garbage_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_empty_read_bucket_))) { SERVER_LOG(WARN, "mods_empty_read_bucket_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_dbms_sched_service_))) { SERVER_LOG(WARN, "mods_dbms_sched_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_opt_stat_monitor_manager_))) { SERVER_LOG(WARN, "mods_opt_stat_monitor_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_srs_service_))) { SERVER_LOG(WARN, "mods_srs_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_tablet_memtable_mgr_pool_))) { SERVER_LOG(WARN, "mods_tablet_memtable_mgr_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_resource_limit_calculator_))) { SERVER_LOG(WARN, "mods_resource_limit_calculator_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_global_iterator_pool_))) { SERVER_LOG(WARN, "mods_global_iterator_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_rb_mem_mgr_))) { SERVER_LOG(WARN, "mods_rb_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_plugin_vector_index_service_))) { SERVER_LOG(WARN, "mods_plugin_vector_index_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_ddl_service_launcher_))) { SERVER_LOG(WARN, "mods_ddl_service_launcher_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_system_package_load_service_))) { SERVER_LOG(WARN, "mods_system_package_load_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_ddl_scheduler_))) { SERVER_LOG(WARN, "mods_ddl_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_ai_service_))) { SERVER_LOG(WARN, "mods_ai_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_new_default(mods_change_stream_mgr_))) { SERVER_LOG(WARN, "mods_change_stream_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret)) {
#define BIND_SERVICE(getter, type) \
    ::oceanbase::share::bind_server_service<type>(getter())
    ::oceanbase::share::bind_server_service<share::ObIMemstoreRuntime>(this);
    ::oceanbase::share::bind_server_service<query::ObILocalCommandService>(
        &ob_service_);
    ::oceanbase::share::bind_server_service<rootserver::ObLocalManagementService>(
        &local_management_service_);
    ::oceanbase::share::bind_server_service<observer::ObService>(&ob_service_);
    ::oceanbase::share::bind_server_service<sql::ObSQLSessionMgr>(&session_mgr_);
    ::oceanbase::share::bind_server_service<omt::ObServerRuntimeController>(
        &server_runtime_controller_);
    ::oceanbase::share::bind_server_service<observer::ObVTIterCreator>(
        &vt_data_service_.get_vt_iter_factory().get_vt_iter_creator());
    ::oceanbase::share::bind_server_service<common::ObIVirtualTableScan>(
        &vt_data_service_);
    ::oceanbase::share::bind_server_service<observer::ObSrvNetworkFrame>(&net_frame_);
    ::oceanbase::share::bind_server_service<data_plane::ObIDiskReport>(
        &disk_usage_report_task_);
    ::oceanbase::share::bind_server_service<logservice::ObServerLogBlockMgr>(
        &log_block_mgr_);
    ::oceanbase::share::bind_server_service<sql::ObConnectResourceMgr>(
        &conn_res_mgr_);
    ::oceanbase::share::bind_server_service<storage::ObStartupAccelTaskHandler>(
        &startup_accel_handler_);
    BIND_SERVICE(ls_service, storage::ObLSService);
    BIND_SERVICE(storage_meta_mem_mgr, storage::ObStorageMetaMemMgr);
    BIND_SERVICE(trans_service, transaction::ObTransService);
    BIND_SERVICE(dead_lock_detector_mgr, share::detector::ObDeadLockDetectorMgr);
    BIND_SERVICE(shared_mem_alloc_mgr, share::ObSharedMemAllocMgr);
    BIND_SERVICE(access_service, storage::ObAccessService);
    BIND_SERVICE(dag_scheduler, share::ObDagScheduler);
    BIND_SERVICE(memstore_freezer, storage::ObMemstoreFreezer);
    BIND_SERVICE(plugin_vector_index_service, share::ObPluginVectorIndexService);
    BIND_SERVICE(lock_wait_mgr, memtable::ObLockWaitMgr);
    BIND_SERVICE(lob_manager, storage::ObLobManager);
    BIND_SERVICE(log_service, logservice::ObLogService);
    BIND_SERVICE(local_storage_meta_service, storage::ObLocalStorageMetaService);
    BIND_SERVICE(freeze_info_mgr, storage::ObFreezeInfoMgr);
    BIND_SERVICE(schema_runtime_service, share::schema::ObSchemaRuntimeService);
    BIND_SERVICE(tmp_file_manager, tmp_file::ObTmpFileManager);
    BIND_SERVICE(vector_index_runtime, storage::ObIVectorIndexRuntime);
    BIND_SERVICE(tablet_scheduler, compaction::ObTabletScheduler);
    BIND_SERVICE(tablet_stat_mgr, storage::ObTabletStatMgr);
    BIND_SERVICE(plan_cache, sql::ObPlanCache);
    BIND_SERVICE(dtl_interm_result_manager, sql::dtl::ObDTLIntermResultManager);
    BIND_SERVICE(shared_macro_block_mgr, blocksstable::ObSharedMacroBlockMgr);
    BIND_SERVICE(server_runtime_service, storage::ObIServerRuntime);
    BIND_SERVICE(table_lock_service, transaction::tablelock::ObTableLockService);
    BIND_SERVICE(shared_timer, share::ObISharedTimer);
    BIND_SERVICE(schedule_suspect_info_mgr, compaction::ObScheduleSuspectInfoMgr);
    BIND_SERVICE(mds_service, storage::mds::ObMdsService);
    BIND_SERVICE(lob_read_service, common::ObILobReadService);
    BIND_SERVICE(global_iterator_pool, storage::ObGlobalIteratorPool);
    BIND_SERVICE(diagnose_tablet_mgr, compaction::ObDiagnoseTabletMgr);
    BIND_SERVICE(change_stream_mgr, share::ObChangeStreamMgr);
    BIND_SERVICE(vector_index_service, query::ObIVectorIndexService);
    BIND_SERVICE(data_access_service, sql::ObDataAccessService);
    BIND_SERVICE(dag_warning_history_manager, share::ObDagWarningHistoryManager);
    BIND_SERVICE(sql_memory_manager, sql::ObSqlMemoryManager);
    BIND_SERVICE(dml_service, data_plane::ObIDmlService);
    BIND_SERVICE(compaction_progress_mgr, compaction::ObCompactionProgressMgr);
    BIND_SERVICE(timestamp_service, transaction::ObTimestampService);
    BIND_SERVICE(tablet_autoincrement_admin, share::ObITabletAutoincrementAdmin);
    BIND_SERVICE(sql_proxy, common::ObMySQLProxy);
    BIND_SERVICE(trans_id_service, transaction::ObTransIDService);
    BIND_SERVICE(tablet_runtime_meta_updater, observer::ObTabletRuntimeMetaUpdater);
    BIND_SERVICE(sstable_merge_info_mgr, storage::ObSSTableMergeInfoMgr);
    BIND_SERVICE(scheduler_service, query::ObISchedulerService);
    BIND_SERVICE(rb_mem_mgr, common::ObRbMemMgr);
    BIND_SERVICE(opt_stat_monitor_manager, common::ObOptStatMonitorManager);
    BIND_SERVICE(compaction_mem_pool, storage::ObCompactionMemPool);
    BIND_SERVICE(write_context_service, data_plane::ObIWriteContextService);
    BIND_SERVICE(major_freeze_coordinator, data_plane::ObIMajorFreezeCoordinator);
    BIND_SERVICE(dfc_manager, sql::dtl::ObDfc);
    BIND_SERVICE(compaction_suggestion_mgr, compaction::ObCompactionSuggestionMgr);
    BIND_SERVICE(ai_endpoint_resolver, query::ObIAiEndpointResolver);
    BIND_SERVICE(tablet_autoincrement_service, share::ObITabletAutoincrementService);
    BIND_SERVICE(server_compaction_event_history, compaction::ObServerCompactionEventHistory);
    BIND_SERVICE(resource_limit_calculator, share::ObResourceLimitCalculator);
    BIND_SERVICE(ps_cache, sql::ObPsCache);
    BIND_SERVICE(multi_version_garbage_collector, concurrency_control::ObMultiVersionGarbageCollector);
    BIND_SERVICE(medium_checker, compaction::ObMediumChecker);
    BIND_SERVICE(ls_runtime_adapter, storage::ObILSRuntimeAdapter);
    BIND_SERVICE(check_point_service, storage::checkpoint::ObCheckPointService);
    BIND_SERVICE(timestamp_access, transaction::ObTimestampAccess);
    BIND_SERVICE(tablet_scan_service, common::ObITabletScan);
    BIND_SERVICE(tablet_memtable_mgr_pool, storage::ObTabletMemtableMgrPool);
    BIND_SERVICE(storage_estimator, data_plane::ObIStorageEstimator);
    BIND_SERVICE(restore_major_freeze_service, rootserver::ObRestoreMajorFreezeService);
    BIND_SERVICE(range_service, data_plane::ObIRangeService);
    BIND_SERVICE(primary_major_freeze_service, rootserver::ObPrimaryMajorFreezeService);
    BIND_SERVICE(optimizer_storage_service, data_plane::ObIOptimizerStorageService);
    BIND_SERVICE(memory_pressure_service, data_plane::ObIMemoryPressureService);
    BIND_SERVICE(empty_read_bucket, storage::ObEmptyReadBucket);
    BIND_SERVICE(ddl_scheduler, rootserver::ObDDLScheduler);
    BIND_SERVICE(ai_service, omt::ObAiService);
    BIND_SERVICE(unique_id_service, transaction::ObUniqueIDService);
    BIND_SERVICE(table_scan_iterator_obj_pool, share::ObTableScanIteratorObjPool);
    BIND_SERVICE(srs_service, omt::ObSrsService);
    BIND_SERVICE(rootserver_local_runtime, rootserver::ObIRootserverLocalRuntime);
    BIND_SERVICE(read_timestamp_service, data_plane::ObIReadTimestampService);
    BIND_SERVICE(px_pools, omt::ObPxPools);
    BIND_SERVICE(lock_wait_service, data_plane::ObILockWaitService);
    BIND_SERVICE(internal_table_refresh_handler, logservice::ObILocalLogHandler);
    BIND_SERVICE(inner_connection_lock_runtime, transaction::tablelock::ObIInnerConnectionLockRuntime);
    BIND_SERVICE(ddl_service_launcher, rootserver::ObDDLServiceLauncher);
    BIND_SERVICE(dbms_sched_service, rootserver::ObDBMSSchedService);
    BIND_SERVICE(active_snapshot_service, query::ObIActiveSnapshotService);
#undef BIND_SERVICE
  }
  return ret;
}

int ObServer::obs_init_modules()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_FAIL(ObSharedTimer::server_module_init(mods_shared_timer_))) { SERVER_LOG(WARN, "mods_shared_timer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_init_default(mods_shared_macro_block_mgr_))) { SERVER_LOG(WARN, "mods_shared_macro_block_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_init_default(mods_storage_meta_mem_mgr_))) { SERVER_LOG(WARN, "mods_storage_meta_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObIOService::server_module_init(mods_io_service_))) { SERVER_LOG(WARN, "mods_io_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::mds::ObMdsService::server_module_init(mods_mds_service_))) { SERVER_LOG(WARN, "mods_mds_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(share::ObSharedMemAllocMgr::server_module_init(mods_shared_mem_alloc_mgr_))) { SERVER_LOG(WARN, "mods_shared_mem_alloc_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTransService::server_module_init(mods_trans_service_))) { SERVER_LOG(WARN, "mods_trans_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(mods_log_storage_adapter_->init(
      mods_ls_service_, mods_memstore_freezer_))) {
    SERVER_LOG(WARN, "mods_log_storage_adapter_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(init_log_service(
      mods_log_service_, mods_log_storage_adapter_))) {
    SERVER_LOG(WARN, "mods_log_service_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(ObLSService::server_module_init(mods_ls_service_))) { SERVER_LOG(WARN, "mods_ls_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObLocalStorageMetaService::server_module_init(mods_local_storage_meta_service_))) { SERVER_LOG(WARN, "mods_local_storage_meta_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(tmp_file::ObTmpFileManager::server_module_init(mods_tmp_file_manager_))) { SERVER_LOG(WARN, "mods_tmp_file_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObCompactionProgressMgr::server_module_init(mods_compaction_progress_mgr_))) { SERVER_LOG(WARN, "mods_compaction_progress_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObServerCompactionEventHistory::server_module_init(mods_server_compaction_event_history_))) { SERVER_LOG(WARN, "mods_server_compaction_event_history_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObTabletStatMgr::server_module_init(mods_tablet_stat_mgr_))) { SERVER_LOG(WARN, "mods_tablet_stat_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(memtable::ObLockWaitMgr::server_module_init(
      mods_lock_wait_mgr_, session_mgr_, *this))) {
    SERVER_LOG(WARN, "mods_lock_wait_mgr_ fail", KR(ret));
  }
  if (OB_SUCC(ret) &&
      (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()) ||
       OB_FAIL(ObTableLockService::server_module_init(
           mods_table_lock_service_, *::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>())))) {
    if (OB_SUCC(ret)) {
      ret = OB_ERR_UNEXPECTED;
    }
    SERVER_LOG(WARN, "mods_table_lock_service_ fail", KR(ret), KP(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()));
  }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObPrimaryMajorFreezeService::server_module_init(mods_primary_major_freeze_service_))) { SERVER_LOG(WARN, "mods_primary_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObRestoreMajorFreezeService::server_module_init(mods_restore_major_freeze_service_))) { SERVER_LOG(WARN, "mods_restore_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(major_freeze_coordinator_adapter_.init(
      *mods_primary_major_freeze_service_,
      *mods_restore_major_freeze_service_))) {
    SERVER_LOG(WARN, "major_freeze_coordinator_adapter_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(ObTabletRuntimeMetaUpdater::server_module_init(mods_tablet_runtime_meta_updater_))) { SERVER_LOG(WARN, "mods_tablet_runtime_meta_updater_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObSSTableMergeInfoMgr::server_module_init(mods_sstable_merge_info_mgr_))) { SERVER_LOG(WARN, "mods_sstable_merge_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(share::ObDagWarningHistoryManager::server_module_init(mods_dag_warning_history_manager_))) { SERVER_LOG(WARN, "mods_dag_warning_history_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObScheduleSuspectInfoMgr::server_module_init(mods_schedule_suspect_info_mgr_))) { SERVER_LOG(WARN, "mods_schedule_suspect_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObCompactionSuggestionMgr::server_module_init(mods_compaction_suggestion_mgr_))) { SERVER_LOG(WARN, "mods_compaction_suggestion_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObDiagnoseTabletMgr::server_module_init(mods_diagnose_tablet_mgr_))) { SERVER_LOG(WARN, "mods_diagnose_tablet_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_init_default(mods_lob_manager_))) { SERVER_LOG(WARN, "mods_lob_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(share::detector::ObDeadLockDetectorMgr::server_module_init(mods_dead_lock_detector_mgr_))) { SERVER_LOG(WARN, "mods_dead_lock_detector_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTimestampService::server_module_init(mods_timestamp_service_))) { SERVER_LOG(WARN, "mods_timestamp_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTimestampAccess::server_module_init(mods_timestamp_access_))) { SERVER_LOG(WARN, "mods_timestamp_access_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTransIDService::server_module_init(mods_trans_id_service_))) { SERVER_LOG(WARN, "mods_trans_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObUniqueIDService::server_module_init(mods_unique_id_service_))) { SERVER_LOG(WARN, "mods_unique_id_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPsCache::server_module_init(mods_ps_cache_))) { SERVER_LOG(WARN, "mods_ps_cache_ fail", KR(ret)); }
  if (OB_SUCC(ret) &&
      OB_FAIL(ObPlanCache::server_module_init(mods_plan_cache_, OBSERVER))) {
    SERVER_LOG(WARN, "mods_plan_cache_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(ObDfc::server_module_init(mods_dfc_))) { SERVER_LOG(WARN, "mods_dfc_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPxPools::server_module_init(mods_px_pools_))) { SERVER_LOG(WARN, "mods_px_pools_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObSqlMemoryManager::server_module_init(mods_sql_memory_manager_))) { SERVER_LOG(WARN, "mods_sql_memory_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDTLIntermResultManager::server_module_init(mods_dtl_interm_result_manager_))) { SERVER_LOG(WARN, "mods_dtl_interm_result_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObSchemaRuntimeService::server_module_init(
      mods_schema_runtime_service_, schema_service_))) {
    SERVER_LOG(WARN, "mods_schema_runtime_service_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(ObMemstoreFreezer::server_module_init(mods_memstore_freezer_))) { SERVER_LOG(WARN, "mods_memstore_freezer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObCheckPointService::server_module_init(mods_check_point_service_))) { SERVER_LOG(WARN, "mods_check_point_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTabletGCService::server_module_init(mods_tablet_gc_service_))) { SERVER_LOG(WARN, "mods_tablet_gc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObTabletScheduler::server_module_init(mods_tablet_scheduler_))) { SERVER_LOG(WARN, "mods_tablet_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(compaction::ObMediumChecker::server_module_init(mods_medium_checker_))) { SERVER_LOG(WARN, "mods_medium_checker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::ObCompactionMemPool::server_module_init(mods_compaction_mem_pool_))) { SERVER_LOG(WARN, "mods_compaction_mem_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDagScheduler::server_module_init(mods_dag_scheduler_))) { SERVER_LOG(WARN, "mods_dag_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObFreezeInfoMgr::server_module_init(mods_freeze_info_mgr_))) { SERVER_LOG(WARN, "mods_freeze_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObTxLoopWorker::server_module_init(mods_tx_loop_worker_))) { SERVER_LOG(WARN, "mods_tx_loop_worker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObAccessService::server_module_init(mods_access_service_))) { SERVER_LOG(WARN, "mods_access_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObMultiVersionGarbageCollector::server_module_init(mods_multi_version_garbage_collector_))) { SERVER_LOG(WARN, "mods_multi_version_garbage_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObEmptyReadBucket::server_module_init(mods_empty_read_bucket_))) { SERVER_LOG(WARN, "mods_empty_read_bucket_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObDBMSSchedService::server_module_init(mods_dbms_sched_service_))) { SERVER_LOG(WARN, "mods_dbms_sched_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObOptStatMonitorManager::server_module_init(mods_opt_stat_monitor_manager_))) { SERVER_LOG(WARN, "mods_opt_stat_monitor_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(omt::ObSrsService::server_module_init(mods_srs_service_))) { SERVER_LOG(WARN, "mods_srs_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(sql_engine_.init(
      &ObOptStatManager::get_instance(),
      &vt_data_service_,
      self_addr_,
      *mods_plan_cache_,
      *mods_ps_cache_,
      pl_engine_,
      *this,
      *this,
      local_management_service_,
      ob_service_,
      *this,
      *this,
      *this,
      *mods_srs_service_,
      *mods_lob_manager_))) {
    SERVER_LOG(WARN, "sql_engine_ init failed", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(internal_table_refresh_adapter_.init(
      timezone_mgr_, *mods_srs_service_))) {
    SERVER_LOG(WARN, "internal_table_refresh_adapter_ init failed", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(mods_resource_limit_calculator_->init(
      mods_storage_meta_mem_mgr_->get_t3m_limit_calculator()))) {
    SERVER_LOG(WARN, "mods_resource_limit_calculator_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(pl_engine_.init(
      sql_proxy_, *this, *mods_resource_limit_calculator_))) {
    SERVER_LOG(WARN, "pl_engine_ init failed", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(ObGlobalIteratorPool::server_module_init(mods_global_iterator_pool_))) { SERVER_LOG(WARN, "mods_global_iterator_pool_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(common::ObRbMemMgr::server_module_init(mods_rb_mem_mgr_))) { SERVER_LOG(WARN, "mods_rb_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObPluginVectorIndexService::server_module_init(
      mods_plugin_vector_index_service_, mods_lob_manager_))) {
    SERVER_LOG(WARN, "mods_plugin_vector_index_service_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObDDLServiceLauncher::server_module_init(mods_ddl_service_launcher_))) { SERVER_LOG(WARN, "mods_ddl_service_launcher_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObSystemPackageLoadService::server_module_init(mods_system_package_load_service_))) { SERVER_LOG(WARN, "mods_system_package_load_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(rootserver::ObDDLScheduler::server_module_init(mods_ddl_scheduler_))) { SERVER_LOG(WARN, "mods_ddl_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObAiService::server_module_init(mods_ai_service_))) { SERVER_LOG(WARN, "mods_ai_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObChangeStreamMgr::server_module_init(
      mods_change_stream_mgr_,
      *mods_log_storage_adapter_,
      schema_publish_signal_,
      share::server_runtime()))) {
    SERVER_LOG(WARN, "mods_change_stream_mgr_ fail", KR(ret));
  }
  if (OB_SUCC(ret) && OB_FAIL(ls_runtime_adapter_.init(
      *mods_primary_major_freeze_service_,
      *mods_dbms_sched_service_,
      *mods_ddl_scheduler_,
      *mods_ddl_service_launcher_,
      *mods_system_package_load_service_,
      *mods_plugin_vector_index_service_))) {
    SERVER_LOG(WARN, "ls_runtime_adapter_ init failed", KR(ret));
  }
  if (OB_SUCC(ret)) {
    session_mgr_.bind_lifecycle_services(
        *mods_ps_cache_,
        debug_sync_broadcaster_,
        conn_res_mgr_);
  }
  return ret;
}

int ObServer::obs_start_modules()
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && OB_FAIL(ObSharedTimer::server_module_start(mods_shared_timer_))) { SERVER_LOG(WARN, "mods_shared_timer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_shared_macro_block_mgr_))) { SERVER_LOG(WARN, "mods_shared_macro_block_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_storage_meta_mem_mgr_))) { SERVER_LOG(WARN, "mods_storage_meta_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_io_service_))) { SERVER_LOG(WARN, "mods_io_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(storage::mds::ObMdsService::server_module_start(mods_mds_service_))) { SERVER_LOG(WARN, "mods_mds_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_shared_mem_alloc_mgr_))) { SERVER_LOG(WARN, "mods_shared_mem_alloc_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_trans_service_))) { SERVER_LOG(WARN, "mods_trans_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_log_service_))) { SERVER_LOG(WARN, "mods_log_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_ls_service_))) { SERVER_LOG(WARN, "mods_ls_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_local_storage_meta_service_))) { SERVER_LOG(WARN, "mods_local_storage_meta_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_tmp_file_manager_))) { SERVER_LOG(WARN, "mods_tmp_file_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_lock_wait_mgr_))) { SERVER_LOG(WARN, "mods_lock_wait_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_table_lock_service_))) { SERVER_LOG(WARN, "mods_table_lock_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_primary_major_freeze_service_))) { SERVER_LOG(WARN, "mods_primary_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_restore_major_freeze_service_))) { SERVER_LOG(WARN, "mods_restore_major_freeze_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_lob_manager_))) { SERVER_LOG(WARN, "mods_lob_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_dead_lock_detector_mgr_))) { SERVER_LOG(WARN, "mods_dead_lock_detector_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_timestamp_service_))) { SERVER_LOG(WARN, "mods_timestamp_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObDTLIntermResultManager::server_module_start(mods_dtl_interm_result_manager_))) { SERVER_LOG(WARN, "mods_dtl_interm_result_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_memstore_freezer_))) { SERVER_LOG(WARN, "mods_memstore_freezer_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_check_point_service_))) { SERVER_LOG(WARN, "mods_check_point_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_tablet_gc_service_))) { SERVER_LOG(WARN, "mods_tablet_gc_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_tablet_scheduler_))) { SERVER_LOG(WARN, "mods_tablet_scheduler_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_freeze_info_mgr_))) { SERVER_LOG(WARN, "mods_freeze_info_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_tx_loop_worker_))) { SERVER_LOG(WARN, "mods_tx_loop_worker_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_multi_version_garbage_collector_))) { SERVER_LOG(WARN, "mods_multi_version_garbage_collector_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_dbms_sched_service_))) { SERVER_LOG(WARN, "mods_dbms_sched_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(ObOptStatMonitorManager::server_module_start(mods_opt_stat_monitor_manager_))) { SERVER_LOG(WARN, "mods_opt_stat_monitor_manager_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_rb_mem_mgr_))) { SERVER_LOG(WARN, "mods_rb_mem_mgr_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_plugin_vector_index_service_))) { SERVER_LOG(WARN, "mods_plugin_vector_index_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_ai_service_))) { SERVER_LOG(WARN, "mods_ai_service_ fail", KR(ret)); }
  if (OB_SUCC(ret) && OB_FAIL(server_module_start_default(mods_change_stream_mgr_))) { SERVER_LOG(WARN, "mods_change_stream_mgr_ fail", KR(ret)); }
  return ret;
}

void ObServer::obs_stop_modules()
{
  server_module_stop_default(mods_shared_macro_block_mgr_);
  server_module_stop_default(mods_change_stream_mgr_);
  server_module_stop_default(mods_ai_service_);
  rootserver::ObDDLScheduler::server_module_stop(mods_ddl_scheduler_);
  server_module_stop_default(mods_plugin_vector_index_service_);
  server_module_stop_default(mods_rb_mem_mgr_);
  ObOptStatMonitorManager::server_module_stop(mods_opt_stat_monitor_manager_);
  server_module_stop_default(mods_dbms_sched_service_);
  server_module_stop_default(mods_multi_version_garbage_collector_);
  server_module_stop_default(mods_tx_loop_worker_);
  server_module_stop_default(mods_freeze_info_mgr_);
  server_module_stop_default(mods_dag_scheduler_);
  server_module_stop_default(mods_compaction_mem_pool_);
  server_module_stop_default(mods_tablet_scheduler_);
  server_module_stop_default(mods_tablet_gc_service_);
  server_module_stop_default(mods_check_point_service_);
  server_module_stop_default(mods_memstore_freezer_);
  ObDTLIntermResultManager::server_module_stop(mods_dtl_interm_result_manager_);
  ObPxPools::server_module_stop(mods_px_pools_);
  ObPlanCache::server_module_stop(mods_plan_cache_);
  ObPsCache::server_module_stop(mods_ps_cache_);
  server_module_stop_default(mods_timestamp_service_);
  server_module_stop_default(mods_dead_lock_detector_mgr_);
  server_module_stop_default(mods_lob_manager_);
  server_module_stop_default(mods_tablet_runtime_meta_updater_);
  server_module_stop_default(mods_restore_major_freeze_service_);
  server_module_stop_default(mods_primary_major_freeze_service_);
  server_module_stop_default(mods_table_lock_service_);
  server_module_stop_default(mods_lock_wait_mgr_);
  server_module_stop_default(mods_tablet_stat_mgr_);
  server_module_stop_default(mods_tmp_file_manager_);
  server_module_stop_default(mods_local_storage_meta_service_);
  server_module_stop_default(mods_ls_service_);
  server_module_stop_default(mods_log_service_);
  server_module_stop_default(mods_trans_service_);
  server_module_stop_default(mods_shared_mem_alloc_mgr_);
  storage::mds::ObMdsService::server_module_stop(mods_mds_service_);
  server_module_stop_default(mods_io_service_);
  server_module_stop_default(mods_storage_meta_mem_mgr_);
  ObSharedTimer::server_module_stop(mods_shared_timer_);
}

void ObServer::obs_wait_modules()
{
  server_module_wait_default(mods_shared_macro_block_mgr_);
  server_module_wait_default(mods_change_stream_mgr_);
  server_module_wait_default(mods_ai_service_);
  rootserver::ObDDLScheduler::server_module_wait(mods_ddl_scheduler_);
  server_module_wait_default(mods_plugin_vector_index_service_);
  server_module_wait_default(mods_rb_mem_mgr_);
  ObOptStatMonitorManager::server_module_wait(mods_opt_stat_monitor_manager_);
  server_module_wait_default(mods_dbms_sched_service_);
  server_module_wait_default(mods_multi_version_garbage_collector_);
  server_module_wait_default(mods_tx_loop_worker_);
  server_module_wait_default(mods_freeze_info_mgr_);
  server_module_wait_default(mods_dag_scheduler_);
  server_module_wait_default(mods_compaction_mem_pool_);
  server_module_wait_default(mods_tablet_scheduler_);
  server_module_wait_default(mods_tablet_gc_service_);
  server_module_wait_default(mods_check_point_service_);
  server_module_wait_default(mods_memstore_freezer_);
  ObDTLIntermResultManager::server_module_wait(mods_dtl_interm_result_manager_);
  server_module_wait_default(mods_timestamp_service_);
  server_module_wait_default(mods_dead_lock_detector_mgr_);
  server_module_wait_default(mods_lob_manager_);
  server_module_wait_default(mods_tablet_runtime_meta_updater_);
  server_module_wait_default(mods_restore_major_freeze_service_);
  server_module_wait_default(mods_primary_major_freeze_service_);
  server_module_wait_default(mods_table_lock_service_);
  server_module_wait_default(mods_lock_wait_mgr_);
  server_module_wait_default(mods_tablet_stat_mgr_);
  server_module_wait_default(mods_tmp_file_manager_);
  server_module_wait_default(mods_local_storage_meta_service_);
  server_module_wait_default(mods_ls_service_);
  server_module_wait_default(mods_log_service_);
  server_module_wait_default(mods_trans_service_);
  server_module_wait_default(mods_shared_mem_alloc_mgr_);
  storage::mds::ObMdsService::server_module_wait(mods_mds_service_);
  server_module_wait_default(mods_storage_meta_mem_mgr_);
  if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>())) {
    ::oceanbase::share::server_service<::oceanbase::sql::ObSQLSessionMgr>()->wait_sessions_drained();
  }
  ObSharedTimer::server_module_wait(mods_shared_timer_);
}

void ObServer::obs_destroy_modules()
{
  server_module_destroy_default(mods_shared_macro_block_mgr_);
  server_module_destroy_default(mods_change_stream_mgr_);
  server_module_destroy_default(mods_ai_service_);
  server_module_destroy_default(mods_ddl_scheduler_);
  server_module_destroy_default(mods_system_package_load_service_);
  server_module_destroy_default(mods_ddl_service_launcher_);
  server_module_destroy_default(mods_plugin_vector_index_service_);
  server_module_destroy_default(mods_rb_mem_mgr_);
  ObGlobalIteratorPool::server_module_destroy(mods_global_iterator_pool_);
  server_module_destroy_default(mods_resource_limit_calculator_);
  server_module_destroy_default(mods_tablet_memtable_mgr_pool_);
  server_module_destroy_default(mods_srs_service_);
  server_module_destroy_default(mods_opt_stat_monitor_manager_);
  server_module_destroy_default(mods_dbms_sched_service_);
  ObEmptyReadBucket::server_module_destroy(mods_empty_read_bucket_);
  server_module_destroy_default(mods_multi_version_garbage_collector_);
  server_module_destroy_default(mods_access_service_);
  server_module_destroy_default(mods_tx_loop_worker_);
  server_module_destroy_default(mods_freeze_info_mgr_);
  server_module_destroy_default(mods_dag_scheduler_);
  server_module_destroy_default(mods_compaction_mem_pool_);
  server_module_destroy_default(mods_medium_checker_);
  server_module_destroy_default(mods_tablet_scheduler_);
  server_module_destroy_default(mods_tablet_gc_service_);
  server_module_destroy_default(mods_check_point_service_);
  server_module_destroy_default(mods_memstore_freezer_);
  server_module_destroy_default(mods_schema_runtime_service_);
  ObDataAccessService::server_module_destroy(mods_data_access_service_);
  ObDTLIntermResultManager::server_module_destroy(mods_dtl_interm_result_manager_);
  ObSqlMemoryManager::server_module_destroy(mods_sql_memory_manager_);
  ObPxPools::server_module_destroy(mods_px_pools_);
  ObDfc::server_module_destroy(mods_dfc_);
  server_module_destroy_default(mods_plan_cache_);
  server_module_destroy_default(mods_ps_cache_);
  server_module_destroy_default(mods_unique_id_service_);
  server_module_destroy_default(mods_trans_id_service_);
  server_module_destroy_default(mods_timestamp_access_);
  server_module_destroy_default(mods_timestamp_service_);
  server_module_destroy_default(mods_dead_lock_detector_mgr_);
  server_module_destroy_default(mods_lob_manager_);
  server_module_destroy_default(mods_diagnose_tablet_mgr_);
  server_module_destroy_default(mods_compaction_suggestion_mgr_);
  server_module_destroy_default(mods_schedule_suspect_info_mgr_);
  server_module_destroy_default(mods_dag_warning_history_manager_);
  server_module_destroy_default(mods_sstable_merge_info_mgr_);
  server_module_destroy_default(mods_tablet_runtime_meta_updater_);
  major_freeze_coordinator_adapter_.reset();
  server_module_destroy_default(mods_restore_major_freeze_service_);
  server_module_destroy_default(mods_primary_major_freeze_service_);
  server_module_destroy_default(mods_table_lock_service_);
  server_module_destroy_default(mods_lock_wait_mgr_);
  server_module_destroy_default(mods_tablet_stat_mgr_);
  server_module_destroy_default(mods_server_compaction_event_history_);
  server_module_destroy_default(mods_compaction_progress_mgr_);
  server_module_destroy_default(mods_tmp_file_manager_);
  server_module_destroy_default(mods_local_storage_meta_service_);
  server_module_destroy_default(mods_ls_service_);
  if (OB_NOT_NULL(::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>()) && OB_NOT_NULL(mods_log_service_)) {
    ::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>()->unbind_log_service(*mods_log_service_);
  }
  ObLogService::server_module_destroy(mods_log_service_);
  server_module_destroy_default(mods_log_storage_adapter_);
  server_module_destroy_default(mods_trans_service_);
  server_module_destroy_default(mods_shared_mem_alloc_mgr_);
  server_module_destroy_default(mods_mds_service_);
  ObIOService::server_module_destroy(mods_io_service_);
  server_obj_pool_destroy<ObTableScanIterator>(mods_table_scan_iterator_obj_pool_);
  server_module_destroy_default(mods_storage_meta_mem_mgr_);
  server_module_destroy_default(mods_shared_timer_);

  // Keep the slots alive while module destructors run, then invalidate every
  // process-owned service before any later teardown or test reinitialization.
#define UNBIND_SERVICE(type) ::oceanbase::share::unbind_server_service<type>()
  UNBIND_SERVICE(share::ObIMemstoreRuntime);
  UNBIND_SERVICE(query::ObILocalCommandService);
  UNBIND_SERVICE(rootserver::ObLocalManagementService);
  UNBIND_SERVICE(observer::ObService);
  UNBIND_SERVICE(sql::ObSQLSessionMgr);
  UNBIND_SERVICE(omt::ObServerRuntimeController);
  UNBIND_SERVICE(observer::ObVTIterCreator);
  UNBIND_SERVICE(observer::ObSrvNetworkFrame);
  UNBIND_SERVICE(data_plane::ObIDiskReport);
  UNBIND_SERVICE(logservice::ObServerLogBlockMgr);
  UNBIND_SERVICE(sql::ObConnectResourceMgr);
  UNBIND_SERVICE(storage::ObStartupAccelTaskHandler);
  UNBIND_SERVICE(storage::ObLSService);
  UNBIND_SERVICE(storage::ObStorageMetaMemMgr);
  UNBIND_SERVICE(transaction::ObTransService);
  UNBIND_SERVICE(share::detector::ObDeadLockDetectorMgr);
  UNBIND_SERVICE(share::ObSharedMemAllocMgr);
  UNBIND_SERVICE(storage::ObAccessService);
  UNBIND_SERVICE(share::ObDagScheduler);
  UNBIND_SERVICE(storage::ObMemstoreFreezer);
  UNBIND_SERVICE(share::ObPluginVectorIndexService);
  UNBIND_SERVICE(memtable::ObLockWaitMgr);
  UNBIND_SERVICE(storage::ObLobManager);
  UNBIND_SERVICE(logservice::ObLogService);
  UNBIND_SERVICE(storage::ObLocalStorageMetaService);
  UNBIND_SERVICE(storage::ObFreezeInfoMgr);
  UNBIND_SERVICE(share::schema::ObSchemaRuntimeService);
  UNBIND_SERVICE(tmp_file::ObTmpFileManager);
  UNBIND_SERVICE(storage::ObIVectorIndexRuntime);
  UNBIND_SERVICE(compaction::ObTabletScheduler);
  UNBIND_SERVICE(storage::ObTabletStatMgr);
  UNBIND_SERVICE(sql::ObPlanCache);
  UNBIND_SERVICE(sql::dtl::ObDTLIntermResultManager);
  UNBIND_SERVICE(blocksstable::ObSharedMacroBlockMgr);
  UNBIND_SERVICE(storage::ObIServerRuntime);
  UNBIND_SERVICE(transaction::tablelock::ObTableLockService);
  UNBIND_SERVICE(share::ObISharedTimer);
  UNBIND_SERVICE(compaction::ObScheduleSuspectInfoMgr);
  UNBIND_SERVICE(storage::mds::ObMdsService);
  UNBIND_SERVICE(common::ObILobReadService);
  UNBIND_SERVICE(storage::ObGlobalIteratorPool);
  UNBIND_SERVICE(compaction::ObDiagnoseTabletMgr);
  UNBIND_SERVICE(share::ObChangeStreamMgr);
  UNBIND_SERVICE(query::ObIVectorIndexService);
  UNBIND_SERVICE(sql::ObDataAccessService);
  UNBIND_SERVICE(share::ObDagWarningHistoryManager);
  UNBIND_SERVICE(sql::ObSqlMemoryManager);
  UNBIND_SERVICE(data_plane::ObIDmlService);
  UNBIND_SERVICE(compaction::ObCompactionProgressMgr);
  UNBIND_SERVICE(transaction::ObTimestampService);
  UNBIND_SERVICE(share::ObITabletAutoincrementAdmin);
  UNBIND_SERVICE(common::ObMySQLProxy);
  UNBIND_SERVICE(transaction::ObTransIDService);
  UNBIND_SERVICE(observer::ObTabletRuntimeMetaUpdater);
  UNBIND_SERVICE(storage::ObSSTableMergeInfoMgr);
  UNBIND_SERVICE(query::ObISchedulerService);
  UNBIND_SERVICE(common::ObRbMemMgr);
  UNBIND_SERVICE(common::ObOptStatMonitorManager);
  UNBIND_SERVICE(storage::ObCompactionMemPool);
  UNBIND_SERVICE(data_plane::ObIWriteContextService);
  UNBIND_SERVICE(data_plane::ObIMajorFreezeCoordinator);
  UNBIND_SERVICE(sql::dtl::ObDfc);
  UNBIND_SERVICE(compaction::ObCompactionSuggestionMgr);
  UNBIND_SERVICE(query::ObIAiEndpointResolver);
  UNBIND_SERVICE(share::ObITabletAutoincrementService);
  UNBIND_SERVICE(compaction::ObServerCompactionEventHistory);
  UNBIND_SERVICE(share::ObResourceLimitCalculator);
  UNBIND_SERVICE(sql::ObPsCache);
  UNBIND_SERVICE(concurrency_control::ObMultiVersionGarbageCollector);
  UNBIND_SERVICE(compaction::ObMediumChecker);
  UNBIND_SERVICE(storage::ObILSRuntimeAdapter);
  UNBIND_SERVICE(storage::checkpoint::ObCheckPointService);
  UNBIND_SERVICE(transaction::ObTimestampAccess);
  UNBIND_SERVICE(common::ObITabletScan);
  UNBIND_SERVICE(common::ObIVirtualTableScan);
  UNBIND_SERVICE(storage::ObTabletMemtableMgrPool);
  UNBIND_SERVICE(data_plane::ObIStorageEstimator);
  UNBIND_SERVICE(rootserver::ObRestoreMajorFreezeService);
  UNBIND_SERVICE(data_plane::ObIRangeService);
  UNBIND_SERVICE(rootserver::ObPrimaryMajorFreezeService);
  UNBIND_SERVICE(data_plane::ObIOptimizerStorageService);
  UNBIND_SERVICE(data_plane::ObIMemoryPressureService);
  UNBIND_SERVICE(storage::ObEmptyReadBucket);
  UNBIND_SERVICE(rootserver::ObDDLScheduler);
  UNBIND_SERVICE(omt::ObAiService);
  UNBIND_SERVICE(transaction::ObUniqueIDService);
  UNBIND_SERVICE(share::ObTableScanIteratorObjPool);
  UNBIND_SERVICE(omt::ObSrsService);
  UNBIND_SERVICE(rootserver::ObIRootserverLocalRuntime);
  UNBIND_SERVICE(data_plane::ObIReadTimestampService);
  UNBIND_SERVICE(omt::ObPxPools);
  UNBIND_SERVICE(data_plane::ObILockWaitService);
  UNBIND_SERVICE(logservice::ObILocalLogHandler);
  UNBIND_SERVICE(transaction::tablelock::ObIInnerConnectionLockRuntime);
  UNBIND_SERVICE(rootserver::ObDDLServiceLauncher);
  UNBIND_SERVICE(rootserver::ObDBMSSchedService);
  UNBIND_SERVICE(query::ObIActiveSnapshotService);
#undef UNBIND_SERVICE
}

} // namespace observer
} // namespace oceanbase
