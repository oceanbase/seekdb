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

#define USING_LOG_PREFIX SERVER



#include "lib/stat/ob_diagnostic_info_guard.h"
#include "lib/time/ob_time_utility.h"
#include "ob_service.h"
#include "storage/ob_storage_rpc_arg.h"
#include "share/rc/ob_server_runtime.h"
#include "share/ob_telemetry.h"
#include "lib/alloc/memory_dump.h"

#include "share/ob_version.h"

#include "share/ob_version.h"
#include "storage/deadlock/ob_deadlock_inner_table_service.h"
#include "share/ob_tablet_local_checksum_operator.h" // ObTabletLocalChecksumOperator

#include "rootserver/ob_bootstrap.h"
#include "observer/ob_server.h"
#include "observer/ob_system_package_load_task.h"
#include "share/ob_structured_event_logger.h"
#include "storage/ddl/ob_delete_lob_meta_row_task.h" // delete lob meta row for drop vec index
#include "storage/ddl/ob_build_index_task.h"
#include "storage/tx_storage/ob_memstore_freezer.h"
#include "logservice/ob_log_service.h"        // ObLogService
#include "share/ob_ddl_sim_point.h" // for DDL_SIM
#include "storage/compaction/ob_tablet_scheduler.h"
#include "share/ob_global_merge_table_operator.h"
#include "share/ob_merge_info.h"
#include "common/ob_data_version_mgr.h"
#include "share/ob_column_checksum_error_operator.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "share/ob_server_info.h"  // ObServerInfoProxy
#include "share/ob_server_struct.h"    // GCTX
#include "share/ob_standby_source_util.h"
#include "storage/tx_storage/ob_ls_service.h"  // ObLSService
#include "storage/ls/ob_ls.h"
#include "storage/tx/ob_trans_service.h"
#include "data_plane/scheduler/ob_sys_task_stat.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "sql/pl/ob_pl_package_manager.h"
#include "share/ob_rpc_struct.h"  // ObCreateLSArg
#include "share/schema/ob_multi_version_schema_service.h"  // hook registration
#include "standby/ob_standby_service.h"

namespace oceanbase
{

using namespace common;
using namespace rootserver;
using namespace obcall;
using namespace share;
using namespace share::schema;
using namespace storage;
using namespace palf;

namespace observer
{

namespace
{
int submit_standby_schema_refresh_task(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  ObService *ob_service = share::server_service<ObService>();
  if (OB_ISNULL(ob_service)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob service is not ready for standby schema refresh", KR(ret), K(schema_version));
  } else if (OB_FAIL(ob_service->submit_async_refresh_schema_task(schema_version))) {
    LOG_WARN("failed to submit standby schema refresh task", KR(ret), K(schema_version));
  }
  return ret;
}
} // namespace


ObSchemaReleaseTimeTask::ObSchemaReleaseTimeTask()
: schema_updater_(nullptr), timer_(), is_inited_(false)
{}

int ObSchemaReleaseTimeTask::init(ObServerSchemaUpdater &schema_updater)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObSchemaReleaseTimeTask has already been inited", K(ret));
  } else {
    schema_updater_ = &schema_updater;
    if (OB_FAIL(timer_.init("SchemaRelease", ObMemAttr("SchemaRelease")))) {
      LOG_WARN("fail to init ObSchemaReleaseTimeTask timer", KR(ret));
    } else if (OB_FAIL(schedule_())) {
      LOG_WARN("fail to schedule ObSchemaReleaseTimeTask in init", KR(ret));
    } else {
      is_inited_ = true;
    }
  }
  return ret;
}

void ObSchemaReleaseTimeTask::stop()
{
  if (timer_.inited()) {
    timer_.stop();
  }
}

void ObSchemaReleaseTimeTask::wait()
{
  if (timer_.inited()) {
    timer_.wait();
  }
}

void ObSchemaReleaseTimeTask::destroy()
{
  timer_.destroy();
  schema_updater_ = nullptr;
  is_inited_ = false;
}

int ObSchemaReleaseTimeTask::schedule_()
{
  int ret = OB_SUCCESS;
  int64_t memory_recycle_interval = GCONF._schema_memory_recycle_interval;
  if (0 == memory_recycle_interval) {
    memory_recycle_interval = 15L * 60L * 1000L * 1000L; //15mins
  }
  if (OB_FAIL(timer_.schedule(*this, memory_recycle_interval, false /*not schedule repeatly*/))) {
    LOG_ERROR("fail to schedule task ObSchemaReleaseTimeTask", KR(ret));
  }
  return ret;
}

void ObSchemaReleaseTimeTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObSchemaReleaseTimeTask has not been inited", K(ret));
  } else if (OB_ISNULL(schema_updater_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ObSchemaReleaseTimeTask task got null ptr", K(ret));
  } else if (OB_FAIL(schema_updater_->try_release_schema())) {
    LOG_WARN("ObSchemaReleaseTimeTask failed", K(ret));
  }
  if (OB_FAIL(schedule_())) {
    // overwrite ret
    LOG_WARN("fail to schedule ObSchemaReleaseTimeTask in runTimerTask", KR(ret));
  }
}

int TelemetryTask::report()
{
  const char *env_reporter = std::getenv("REPORTER");
  const char *reporter = env_reporter ? env_reporter : (GCTX.is_embedded_mode() ? "embed" : "server");
  return share::report_telemetry(reporter, "bootstraped");
}

//////////////////////////////////////

// here gctx may hasn't been initialized already
ObService::ObService(
    const ObGlobalContext &gctx,
    query::ObIChangeStreamService &change_stream_service)
    : inited_(false),
    stopped_(false),
    schema_updater_(),
    gctx_(gctx),
    change_stream_service_(change_stream_service),
    schema_release_task_(),
    telemetry_task_(),
    need_bootstrap_(false)
{
}

ObService::~ObService()
{
}

int ObService::wait_until_change_stream_refreshed(
    common::ObMySQLProxy &mysql_proxy,
    const int64_t timeout_us)
{
  return change_stream_service_.wait_until_refreshed(mysql_proxy, timeout_us);
}

int ObService::init(common::ObMySQLProxy &sql_proxy,
                    bool need_bootstrap)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVICE_NOTICE] init ob_service begin");
  const static int64_t REBUILD_FLAG_REPORT_THREAD_CNT = 1;

  if (inited_) {
    ret = OB_INIT_TWICE;
    FLOG_WARN("Oceanbase service has already init", KR(ret));
  } else if (!gctx_.is_inited()) {
    ret = OB_INVALID_ARGUMENT;
    FLOG_WARN("gctx not init", "gctx inited", gctx_.is_inited(), KR(ret));
  } else if (OB_FAIL(schema_updater_.init(gctx_.self_addr(), gctx_.schema_service_))) {
    FLOG_WARN("client_manager_.initialize failed", "self_addr", gctx_.self_addr(), KR(ret));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else if (OB_FAIL(ObGlobalMergeTableOperator::init(*GCTX.meta_db_pool_))) {
    FLOG_WARN("init global merge table operator failed", KR(ret));
  } else if (OB_FAIL(ObColumnChecksumErrorOperator::init(*GCTX.meta_db_pool_))) {
    FLOG_WARN("init column checksum error operator failed", KR(ret));
  } else if (OB_FAIL(ObTabletLocalChecksumOperator::init(GCTX.meta_db_pool_))) {
    FLOG_WARN("init local tablet checksum operator failed", KR(ret));
  } else if (OB_FAIL(OB_TSC_TIMESTAMP.init())) {
    FLOG_WARN("init tsc timestamp failed", KR(ret));
  } else if (OB_FAIL(schema_release_task_.init(schema_updater_))) {
    FLOG_WARN("init schema release task failed", KR(ret));
  } else if (OB_FAIL(standby::ObStandbyService::init(submit_standby_schema_refresh_task))) {
    FLOG_WARN("init standby service failed", KR(ret));
  } else {
    need_bootstrap_ = need_bootstrap;
    inited_ = true;
  }
  FLOG_INFO("[OBSERVICE_NOTICE] init ob_service finish", KR(ret), K_(inited), K_(need_bootstrap));
  if (OB_FAIL(ret)) {
    LOG_DBA_ERROR(OB_ERR_OBSERVICE_START, "msg", "observice init() has failure", KR(ret));
  }
  return ret;
}

int ObService::start()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVICE_NOTICE] start ob_service begin");
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("ob_service is not inited", KR(ret), K_(inited));
  } else if (need_bootstrap_) {
    if (OB_FAIL(share::ObServerInfoProxy::init_server_info_from_role(
        GCTX.config_mgr_,
        GCTX.server_role_))) {
      LOG_ERROR("failed to initialize server role state before bootstrap", KR(ret), K(GCTX.server_role_));
    } else if (standby::ObStandbyService::startup_profile(GCTX.is_embedded_mode()).bootstrap_from_source_) {
      if (OB_FAIL(standby::ObStandbyService::bootstrap())) {
        LOG_ERROR("standby bootstrap failed", KR(ret));
      }
    } else if (OB_FAIL(bootstrap())) {
      LOG_ERROR("bootstrap failed", KR(ret));
    }
    if (OB_SUCC(ret)) {
      int tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = telemetry_task_.report())) {
        FLOG_WARN("fail to report bootstrap telemetry synchronously", KR(tmp_ret));
      }
    }
    need_bootstrap_ = false;
  } else {
    // Restore the persisted role after a normal restart.
    share::ObServerInfo server_info;
    if (OB_FAIL(share::ObServerInfoProxy::load_server_info(
        GCTX.config_mgr_, GCTX.server_role_, server_info))) {
      LOG_ERROR("failed to load server role state on restart",
               KR(ret));
    } else {
      if (server_info.is_primary()) {
        GCTX.server_role_ = share::ObServerRole::PRIMARY_ROLE;
      } else if (server_info.is_standby()) {
        GCTX.server_role_ = share::ObServerRole::STANDBY_ROLE;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_ERROR("invalid persisted server role", KR(ret), K(server_info));
      }
      if (OB_SUCC(ret)) {
        share::set_server_role(GCTX.server_role_);
        LOG_INFO("restored server role state", K(server_info), K(GCTX.server_role_));
      }
    }
  }
  if (OB_SUCC(ret) && OB_FAIL(standby::ObStandbyService::activate_current_role())) {
    LOG_ERROR("failed to activate current server role", KR(ret), K(GCTX.server_role_));
  }
  FLOG_INFO("[OBSERVICE_NOTICE] start ob_service end", KR(ret));
  if (OB_FAIL(ret)) {
    LOG_DBA_ERROR(OB_ERR_OBSERVICE_START, "msg", "observice start() has failure", KR(ret));
  }
  return ret;
}


void ObService::set_stop()
{
  LOG_INFO("[OBSERVICE_NOTICE] observice need stop now");
}

void ObService::stop()
{
  FLOG_INFO("[OBSERVICE_NOTICE] start to stop observice");
  if (!inited_) {
    FLOG_WARN_RET(OB_NOT_INIT, "ob_service not init", K_(inited));
  } else {
    FLOG_INFO("begin to add server event");
    SERVER_EVENT_ADD("observer", "stop");
    FLOG_INFO("add server event success");

    stopped_ = true;

    FLOG_INFO("begin to stop schema release task");
    schema_release_task_.stop();
    FLOG_INFO("schema release task stopped");
    FLOG_INFO("begin to stop schema updater");
    schema_updater_.stop();
    FLOG_INFO("schema updater stopped");

    (void)standby::ObStandbyService::stop();

  }
  FLOG_INFO("[OBSERVICE_NOTICE] observice finish stop", K_(stopped));
}

void ObService::wait()
{
  FLOG_INFO("[OBSERVICE_NOTICE] wait ob_service begin");
  if (!inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "ob_service not init", K_(inited));
  } else {
    FLOG_INFO("begin to wait schema release task");
    schema_release_task_.wait();
    FLOG_INFO("wait schema release task success");
    FLOG_INFO("begin to wait schema updater");
    schema_updater_.wait();
    FLOG_INFO("wait schema updater success");

    (void)standby::ObStandbyService::wait();

  }
  FLOG_INFO("[OBSERVICE_NOTICE] wait ob_service end");
}

int ObService::destroy()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVICE_NOTICE] destroy ob_service begin");
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_service not init", KR(ret), K_(inited));
  } else {
    FLOG_INFO("begin to destroy schema release task");
    schema_release_task_.destroy();
    FLOG_INFO("schema release task destroyed");
    FLOG_INFO("begin to destroy schema updater");
    schema_updater_.destroy();
    FLOG_INFO("schema updater destroyed");

    standby::ObStandbyService::destroy();
  }
  FLOG_INFO("[OBSERVICE_NOTICE] destroy ob_service end", KR(ret));
  return ret;
}


// used by standby cluster
int ObService::update_baseline_schema_version(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
    ObMultiVersionSchemaService *schema_service = gctx_.schema_service_;
  if (schema_version <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(schema_version));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid schema service", KR(ret));
  } else if (OB_FAIL(schema_service->update_baseline_schema_version(
             schema_version))) {
    LOG_WARN("fail to update baseline schema version", KR(ret), K(schema_version));
  } else {
    LOG_INFO("update baseline schema version success", K(schema_version));
  }
  return ret;
}

const ObAddr &ObService::get_self_addr()
{
  return gctx_.self_addr();
}

int ObService::submit_async_refresh_schema_task(const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(schema_updater_.async_refresh_schema(schema_version))) {
    LOG_WARN("fail to async refresh schema", KR(ret), K(schema_version));
  }
  return ret;
}

// should return success if all partition have merge to specific frozen_version
int ObService::check_frozen_scn(const obcall::ObCheckFrozenScnArg &arg)
{
  LOG_INFO("receive check frozen SCN request", K(arg));
  int ret = OB_SUCCESS;
  SCN last_merged_scn = SCN::min_scn();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(arg));
  } else if (arg.frozen_scn_ != last_merged_scn) {
    ret = OB_ERR_CHECK_DROP_COLUMN_FAILED;
    LOG_WARN("last merged version not match", KR(ret), K(arg), K(last_merged_scn));
  }
  return ret;
}

int ObService::calc_column_checksum_request(const obcall::ObCalcColumnChecksumRequestArg &arg, obcall::ObCalcColumnChecksumRequestRes &res)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObService has not been inited", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", KR(ret), K(arg));
  } else {
    // schedule unique checking task
    
    int saved_ret = OB_SUCCESS;
    SERVER_MODULE_SCOPE {
      ObGlobalUniqueIndexCallback *callback = NULL;
      ObDagScheduler* dag_scheduler = nullptr;
      if (OB_ISNULL(dag_scheduler = ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, dag scheduler must not be nullptr", KR(ret));
      } else if (OB_FAIL(res.ret_codes_.reserve(arg.calc_items_.count()))) {
        LOG_WARN("reserve return code array failed", K(ret), K(arg.calc_items_.count()));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < arg.calc_items_.count(); ++i) {
          const ObCalcColumnChecksumRequestArg::SingleItem &calc_item = arg.calc_items_.at(i);
          ObUniqueCheckingDag *dag = NULL;
          int tmp_ret = OB_SUCCESS;
          saved_ret = OB_SUCCESS;
          if (OB_TMP_FAIL(DDL_SIM(arg.task_id_, CALC_COLUMN_CHECKSUM_RPC_SLOW))) {
            LOG_WARN("ddl sim failure: calcualte column checksum rpc slow", K(tmp_ret), K(arg.task_id_));
          } else if (OB_TMP_FAIL(dag_scheduler->alloc_dag(dag))) {
            STORAGE_LOG(WARN, "fail to alloc dag", KR(tmp_ret));
          } else if (OB_TMP_FAIL(dag->init(calc_item.tablet_id_,
                                           calc_item.calc_table_id_ == arg.target_table_id_,
                                           arg.target_table_id_,
                                           arg.schema_version_,
                                           arg.task_id_,
                                           arg.execution_id_,
                                           arg.snapshot_version_,
                                           arg.user_parallelism_))) {
            STORAGE_LOG(WARN, "fail to init ObUniqueCheckingDag", KR(tmp_ret));
          } else if (OB_TMP_FAIL(dag->alloc_global_index_task_callback(calc_item.tablet_id_,
                                                                       arg.target_table_id_,
                                                                       arg.source_table_id_,
                                                                       arg.schema_version_,
                                                                       arg.task_id_,
                                                                       callback))) {
            STORAGE_LOG(WARN, "fail to alloc global index task callback", KR(tmp_ret));
          } else if (OB_TMP_FAIL(dag->alloc_unique_checking_prepare_task(dag->get_param(), dag->get_context()))) {
            STORAGE_LOG(WARN, "fail to alloc unique checking prepare task", KR(tmp_ret));
          } else if (OB_TMP_FAIL(dag_scheduler->add_dag(dag))) {
            saved_ret = tmp_ret;
            if (OB_EAGAIN == tmp_ret) {
              tmp_ret = OB_SUCCESS;
            } else if (OB_SIZE_OVERFLOW == tmp_ret) {
              tmp_ret = OB_EAGAIN;
            } else {
              STORAGE_LOG(WARN, "fail to add dag to queue", KR(tmp_ret));
            }
          }
          saved_ret = OB_SUCCESS != saved_ret ? saved_ret : tmp_ret;
          if (OB_SUCCESS != saved_ret && NULL != dag) {
            dag_scheduler->free_dag(*dag);
            dag = NULL;
          }
          if (OB_SUCC(ret)) {
            if (OB_FAIL(res.ret_codes_.push_back(tmp_ret))) {
              LOG_WARN("push back return code failed", K(ret), K(tmp_ret));
            }
          }
        }
      }
    }
    LOG_INFO("receive column checksum request", K(arg));
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_CHECK_BACKUP_TASK_EXIST_ERROR);


int ObService::minor_freeze(const obcall::ObMinorFreezeArg &arg,
                            obcall::Int64 &result)
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObTimeUtility::current_time();
  LOG_INFO("receive minor freeze request", K(arg));

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (arg.tablet_id_.is_valid()) {
    ret = handle_tablet_freeze_req_(arg.tablet_id_);
  } else {
    ret = handle_server_freeze_req_(arg);
  }

  result = ret;
  const int64_t cost_ts = ObTimeUtility::current_time() - start_ts;
  LOG_INFO("finish minor freeze request", K(ret), K(arg), K(cost_ts));
  return ret;
}

int ObService::handle_server_freeze_req_(const obcall::ObMinorFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = server_freeze_()))) {
    LOG_WARN("fail to freeze server memstores", K(tmp_ret));
  }
  if (OB_SUCCESS != tmp_ret && OB_SUCC(ret)) {
    ret = tmp_ret;
  }
  return ret;
}

int ObService::handle_tablet_freeze_req_(const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;

  {
    SERVER_MODULE_SCOPE {
      storage::ObMemstoreFreezer* freezer = nullptr;
      if (OB_ISNULL(freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ObMemstoreFreezer shouldn't be null", K(ret));
      } else if (tablet_id.is_valid()) {
        // tablet freeze
        const bool is_sync = true;
        if (OB_FAIL(freezer->tablet_freeze(tablet_id,
                                           is_sync,
                                           0 /*max_retry_time_us*/,
                                           false, /*rewrite_tablet_meta*/
                                           ObFreezeSourceFlag::USER_MINOR_FREEZE))) {
          if (OB_EAGAIN == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_ERROR("fail to freeze tablet", K(ret), K(tablet_id));
          }
        } else {
          LOG_INFO("succeed to freeze tablet", K(ret), K(tablet_id));
        }
      }
    }
  }

  return ret;
}

int ObService::server_freeze_()
{
  int ret = OB_SUCCESS;

  {
    SERVER_MODULE_SCOPE {
      storage::ObMemstoreFreezer* freezer = nullptr;
      if (OB_ISNULL(freezer = ::oceanbase::share::server_service<::oceanbase::storage::ObMemstoreFreezer>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ObMemstoreFreezer shouldn't be null", K(ret));
      } else if (freezer->exist_ls_freezing()) {
        LOG_INFO("exist running ls_freeze", K(ret));
      } else if (OB_FAIL(freezer->freeze_all(ObFreezeSourceFlag::USER_MINOR_FREEZE))) {
        if (OB_ENTRY_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_ERROR("fail to freeze server memstores", K(ret));
        }
      } else {
        LOG_INFO("succeed to freeze server memstores", K(ret));
      }
    } else {
      LOG_WARN("fail to enter server runtime", K(ret));
    }
  }

  return ret;
}

int ObService::tablet_major_freeze(const obcall::ObTabletMajorFreezeArg &arg,
                            obcall::Int64 &result)
{
  int ret = OB_SUCCESS;
  const int64_t start_ts = ObTimeUtility::fast_current_time();
  LOG_INFO("receive tablet major freeze request", K(arg));

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    SERVER_MODULE_SCOPE {
      if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::compaction::ObTabletScheduler>()->user_request_schedule_medium_merge(
        arg.tablet_id_))) {
        LOG_WARN("failed to try schedule tablet major freeze", K(ret), K(arg));
      }
    }
  }

  result = ret;
  const int64_t cost_ts = ObTimeUtility::fast_current_time() - start_ts;
  LOG_INFO("finish tablet major freeze request", K(ret), K(arg), K(cost_ts));
  return ret;
}

int ObService::check_modify_time_elapsed(
    const obcall::ObCheckModifyTimeElapsedArg &arg,
    obcall::ObCheckModifyTimeElapsedResult &result)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive get checksum cal snapshot", K(arg));
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    SERVER_MODULE_SCOPE {
      SCN tmp_scn;
      transaction::ObTransService *txs = ::oceanbase::share::server_service<::oceanbase::transaction::ObTransService>();
      ObLSService *ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>();
      if (OB_FAIL(result.results_.reserve(arg.tablets_.count()))) {
        LOG_WARN("reserve result array failed", K(ret), K(arg.tablets_.count()));
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablets_.count(); ++i) {
        ObTabletHandle tablet_handle;
        ObLS *ls = nullptr;
        const ObTabletID &tablet_id = arg.tablets_.at(i).tablet_id_;
        SCN snapshot_version;
        ObCheckTransElapsedResult single_result;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(DDL_SIM(arg.ddl_task_id_, CHECK_MODIFY_TIME_ELAPSED_SLOW))) {
          LOG_WARN("ddl sim failure: check modify time elapsed slow", K(tmp_ret), K(arg.ddl_task_id_));
        } else if (OB_TMP_FAIL(ls_service->get_ls(ls))) {
          LOG_WARN("get ls failed", K(tmp_ret));
        } else if (OB_TMP_FAIL(ls->check_modify_time_elapsed(tablet_id,
                                                                             arg.sstable_exist_ts_,
                                                                             single_result.pending_tx_id_))) {
          if (OB_EAGAIN != tmp_ret) {
            LOG_WARN("check schema version elapsed failed", K(tmp_ret), K(arg));
          }
        } else if (OB_TMP_FAIL(txs->get_max_commit_version(snapshot_version))) {
          LOG_WARN("fail to get max commit version", K(tmp_ret));
        } else {
          single_result.snapshot_ = snapshot_version.get_val_for_tx();
        }
        if (OB_SUCC(ret)) {
          single_result.ret_code_ = tmp_ret;
          if (OB_FAIL(result.results_.push_back(single_result))) {
            LOG_WARN("push back single result failed", K(ret), K(i), K(single_result));
          }
        }
      }
    }
  }
  return ret;
}

int ObService::check_schema_version_elapsed(
    const obcall::ObCheckSchemaVersionElapsedArg &arg,
    obcall::ObCheckSchemaVersionElapsedResult &result)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive check schema version elapsed", K(arg));
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    SERVER_MODULE_SCOPE {
      ObLSService *ls_service = nullptr;
      if (OB_ISNULL(ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, get ls service failed", K(ret));
      } else if (OB_FAIL(result.results_.reserve(arg.tablets_.count()))) {
        LOG_WARN("reserve result array failed", K(ret), K(arg.tablets_.count()));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablets_.count(); ++i) {
        ObTabletHandle tablet_handle;
        ObLS *ls = nullptr;
        const ObTabletID &tablet_id = arg.tablets_.at(i).tablet_id_;
        ObCheckTransElapsedResult single_result;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(DDL_SIM(arg.ddl_task_id_, CHECK_SCHEMA_TRANS_END_SLOW))) {
          LOG_WARN("ddl sim failure: check schema version elapsed slow", K(tmp_ret), K(arg));
        } else if (OB_TMP_FAIL(ls_service->get_ls(ls))) {
          LOG_WARN("get ls failed", K(tmp_ret), K(i));
        } else if (OB_TMP_FAIL(ls->get_tablet(tablet_id,
                                                              tablet_handle,
                                                              ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
                                                              ObMDSGetTabletMode::READ_ALL_COMMITED))) {
          LOG_WARN("fail to get tablet", K(tmp_ret), K(i), K(tablet_id));
        } else if (OB_TMP_FAIL(tablet_handle.get_obj()->check_schema_version_elapsed(arg.schema_version_,
                                                                                     arg.need_wait_trans_end_,
                                                                                     single_result.snapshot_,
                                                                                     single_result.pending_tx_id_))) {
          LOG_WARN("check schema version elapsed failed", K(tmp_ret), K(arg), K(tablet_id));
        }
        if (OB_SUCC(ret)) {
          single_result.ret_code_ = tmp_ret;
          if (OB_FAIL(result.results_.push_back(single_result))) {
            LOG_WARN("push back single result failed", K(ret), K(i), K(single_result));
          }
        }
      }
    }
  }
  return ret;
}

// 1. minor freeze
// 2. get memtable cnt
int ObService::check_ddl_tablet_merge_status(
    const obcall::ObDDLCheckTabletMergeStatusArg &arg,
    obcall::ObDDLCheckTabletMergeStatusResult &result)
{
  int ret = OB_SUCCESS;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    result.reset();
    SERVER_MODULE_SCOPE {
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablet_ids_.count(); ++i) {
        const common::ObTabletID &tablet_id = arg.tablet_ids_.at(i);
        ObTabletHandle tablet_handle;
        ObLS *ls = nullptr;
        ObDDLKvMgrHandle ddl_kv_mgr_handle;
        ObLSService *ls_service = nullptr;
        bool status = false;

        if (OB_ISNULL(ls_service = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, get ls service failed", K(ret));
        } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid arguments", K(ret), K(arg));
        } else if (OB_FAIL(ls_service->get_ls(ls))) {
          LOG_WARN("get ls failed", K(ret), K(arg));
        } else if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))) {
          LOG_WARN("get tablet failed", K(ret));
        }
        // check and update major status
        if (OB_SUCC(ret)) {
          ObTabletMemberWrapper<ObTabletTableStore> table_store_wrapper;
          if (OB_FAIL(tablet_handle.get_obj()->fetch_table_store(table_store_wrapper))) {
            LOG_WARN("fail to fetch table store", K(ret));
          } else {
            ObSSTable *latest_major_sstable = static_cast<ObSSTable *>(
              table_store_wrapper.get_member()->get_major_sstables().get_boundary_table(true/*last*/));
            status = nullptr != latest_major_sstable;
            if (OB_FAIL(result.merge_status_.push_back(status))) {
              LOG_WARN("fail to push back to array", K(ret), K(status), K(tablet_id));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObService::bootstrap()
{
  int ret = OB_SUCCESS;

  if (OB_FAIL(ret)) {
  } else if (!inited_) {
    ret = OB_NOT_INIT;
    BOOTSTRAP_LOG(WARN, "not init", K(ret));
  } else if (!need_bootstrap_) {
    ret = OB_ERR_UNEXPECTED;
    BOOTSTRAP_LOG(INFO, "no need to bootstrap", K(ret));
  } else if (OB_ISNULL(
                 share::server_service<rootserver::ObLocalManagementService>())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("local management service is null", K(ret));
  } else {
    BOOTSTRAP_LOG(INFO, "begin bootstrap");
    ObPreBootstrap pre_bootstrap(*gctx_.config_);
    ObAddr master_rs;
    bool server_empty = false;
    if (OB_FAIL(check_server_empty(server_empty))) {
      BOOTSTRAP_LOG(WARN, "check_server_empty failed", K(ret));
    } else if (!server_empty) {
      ret = OB_ERR_SYS;
      BOOTSTRAP_LOG(WARN, "this observer is not empty", KR(ret), K(GCTX.self_addr()));
    } else if (OB_FAIL(pre_bootstrap.prepare_bootstrap(master_rs))) {
      BOOTSTRAP_LOG(ERROR, "failed to prepare boot strap", K(ret));
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(
                   share::server_service<rootserver::ObLocalManagementService>()
                       ->execute_bootstrap())) {
      BOOTSTRAP_LOG(ERROR, "failed to execute bootstrap", K(ret));
    } else {
      BOOTSTRAP_LOG(INFO, "succeed to do_boot_strap", K(master_rs));
    }
  }

  return ret;
}

int ObService::get_server_resource_info(share::ObServerResourceInfo &resource_info)
{
  int ret = OB_SUCCESS;
  omt::ObServerRuntimeController::ServerResource svr_res_assigned;
  int64_t clog_in_use_size_byte = 0;
  int64_t clog_total_size_byte = 0;
  logservice::ObServerLogBlockMgr *log_block_mgr = ::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>();
  resource_info.reset();
  int64_t reserved_size = 0;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K(inited_));
  } else if (OB_ISNULL(log_block_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log_block_mgr is null", KR(ret), K(::oceanbase::share::server_service<::oceanbase::logservice::ObServerLogBlockMgr>()));
  } else if (OB_ISNULL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("omt is null", KR(ret));
  } else if (OB_FAIL(::oceanbase::share::server_service<::oceanbase::omt::ObServerRuntimeController>()->get_server_allocated_resource(svr_res_assigned))) {
    LOG_WARN("fail to get server allocated resource", KR(ret));
  } else if (OB_FAIL(log_block_mgr->get_disk_usage(clog_in_use_size_byte))) {
    LOG_WARN("Failed to get clog stat ", KR(ret));
  } else if (FALSE_IT(clog_total_size_byte = log_block_mgr->get_log_disk_size())) {
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.get_reserved_size(reserved_size))) {
    LOG_WARN("Failed to get reserved size ", KR(ret), K(reserved_size));
  } else {
    // cpu
    resource_info.cpu_ = get_cpu_count();
    resource_info.report_cpu_assigned_ = svr_res_assigned.min_cpu_;
    resource_info.report_cpu_max_assigned_ = svr_res_assigned.max_cpu_;
    // mem
    resource_info.report_mem_assigned_ = svr_res_assigned.memory_size_;
    resource_info.mem_in_use_ = 0;
    resource_info.mem_total_ = GMEMCONF.get_server_memory_budget();
    // log_disk
    resource_info.log_disk_total_ = clog_total_size_byte;
    resource_info.log_disk_in_use_ = clog_in_use_size_byte;
    resource_info.report_log_disk_assigned_ = svr_res_assigned.log_disk_size_;
    // data_disk
    {
      resource_info.data_disk_total_
          = OB_STORAGE_OBJECT_MGR.get_max_macro_block_count(reserved_size) * OB_STORAGE_OBJECT_MGR.get_macro_block_size();
      resource_info.data_disk_in_use_
          = OB_STORAGE_OBJECT_MGR.get_used_macro_block_count() * OB_STORAGE_OBJECT_MGR.get_macro_block_size();
    }
  }
  return ret;
}

int ObService::get_build_version(share::ObBuildVersion &build_version)
{
  int ret = OB_SUCCESS;
  char build_version_char_array[common::OB_SERVER_VERSION_LENGTH] = {0};
  build_version.reset();
  if (OB_FAIL(get_package_and_svn(build_version_char_array, sizeof(build_version_char_array)))) {
    LOG_WARN("fail to get build_version", KR(ret));
  } else if (OB_FAIL(build_version.assign(build_version_char_array))) {
    LOG_WARN("fail to assign build_version", KR(ret), K(build_version_char_array));
  }
  return ret;
}

int ObService::get_build_version(char *buf, int64_t buf_len)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(buf) || buf_len <= 0) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid build version buffer", KR(ret), KP(buf), K(buf_len));
  } else if (OB_FAIL(get_package_and_svn(buf, buf_len))) {
    LOG_WARN("fail to get build version", KR(ret), K(buf_len));
  }
  return ret;
}

int ObService::wait_system_package_ready(const common::ObTimeoutCtx &ctx)
{
  return ObSystemPackageLoadTask::wait_system_package_ready(ctx);
}

int ObService::clear_expired_deadlock_events()
{
  int ret = OB_SUCCESS;
  if (!DEALOCK_EVENT_INSTANCE.is_inited()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("deadlock event history operator not initialized", KR(ret));
  } else if (OB_FAIL(DEALOCK_EVENT_INSTANCE.async_delete())) {
    LOG_WARN("failed to clear expired deadlock events", KR(ret));
  }
  return ret;
}

int ObService::load_all_special_system_packages()
{
  return pl::ObPLPackageManager::load_all_special_sys_package(*gctx_.sql_proxy_);
}

int ObService::refresh_stat_cache(const obcall::ObUpdateStatCacheArg &arg)
{
  return ObOptStatManager::get_instance().refresh_stat_cache(arg);
}

int ObService::update_opt_stat_monitoring_info(
    const obcall::ObFlushOptStatArg &arg)
{
  int ret = OB_SUCCESS;
  SERVER_MODULE_SCOPE {
    ObOptStatMonitorManager *manager =
        ::oceanbase::share::server_service<::oceanbase::common::ObOptStatMonitorManager>();
    if (OB_ISNULL(manager)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("optimizer stat monitor manager is null", KR(ret));
    } else if (OB_FAIL(manager->update_opt_stat_monitoring_info(arg))) {
      LOG_WARN("failed to update optimizer stat monitoring info", KR(ret));
    }
  }
  return ret;
}

int ObService::check_server_empty(bool &is_empty)
{
  int ret = OB_SUCCESS;
  is_empty = true;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (is_empty) {
      if (!OBSERVER.is_log_dir_empty()) {
        FLOG_WARN("[CHECK_SERVER_EMPTY] log dir is not empty");
        is_empty = false;
      }
    }
    if (is_empty) {
      if (DATA_VERSION_MGR.get_file_exists_when_loading()) {
        // ignore ret
        FLOG_WARN("[CHECK_SERVER_EMPTY] data_version file exists");
        is_empty = false;
      }
    }
  }
  return ret;
}

int ObService::set_ds_action(const obcall::ObDebugSyncActionArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(GDS.set_global_action(arg.reset_, arg.clear_, arg.action_))) {
    LOG_WARN("set debug sync global action failed", K(ret), K(arg));
  }
  return ret;
}

// Get the runtime's refreshed schema version.
int ObService::set_tracepoint(const obcall::ObSetTracepointParam &param)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    EventItem item;
    item.error_code_ = param.error_code_;
    item.occur_ = param.occur_;
    item.trigger_freq_ = param.trigger_freq_;
    item.cond_ = param.cond_;
    if (param.event_name_.length() > 0) {
      ObSqlString str;
      if (OB_FAIL(str.assign(param.event_name_))) {
        LOG_WARN("string assign failed", K(ret));
      } else if (OB_FAIL(EventTable::instance().set_event(str.ptr(), item))) {
        LOG_WARN("Failed to set tracepoint event, tp_name does not exist.", K(ret), K(param.event_name_));
      }
    } else if (OB_FAIL(EventTable::instance().set_event(param.event_no_, item))) {
      LOG_WARN("Failed to set tracepoint event, tp_no does not exist.", K(ret), K(param.event_no_));
    }
    LOG_INFO("set event", K(param));
  }
  return ret;
}

int ObService::cancel_sys_task(
    const share::ObTaskId &task_id)
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (task_id.is_invalid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid args", K(ret), K(task_id));
  } else if (OB_FAIL(SYS_TASK_STATUS_MGR.cancel_task(task_id))) {
    LOG_WARN("failed to cancel sys task", K(ret), K(task_id));
  }
  return ret;
}

int ObService::stop_partition_write(const obcall::Int64 &switchover_timestamp, obcall::Int64 &result)
{
  //TODO for switchover
  int ret = OB_SUCCESS;
  result = switchover_timestamp;
  return ret;
}

int ObService::check_partition_log(const obcall::Int64 &switchover_timestamp, obcall::Int64 &result)
{
  UNUSEDx(switchover_timestamp, result);
  // Check that the log of all replicas in local have reached synchronization status
  // The primary has stopped writing
  int ret = OB_NOT_SUPPORTED;

  return ret;
}

int ObService::get_wrs_info(const obcall::ObGetWRSArg &arg,
                            obcall::ObGetWRSResult &result)
{
  UNUSEDx(arg, result);
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObService::refresh_memory_stat()
{
  return ObMemoryDump::get_instance().generate_mod_stat_task();
}

int ObService::build_ddl_local(const ObDDLLocalBuildArg &arg,
                               ObDDLLocalBuildResult &res)
{
  int ret = OB_SUCCESS;
  ObDagScheduler *dag_scheduler = nullptr;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_ISNULL(dag_scheduler = ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag scheduler is null", K(ret));
  } else {
    if (is_complement_data_relying_on_dag(ObDDLType(arg.ddl_type_))) {
      int saved_ret = OB_SUCCESS;
      ObComplementDataDag *dag = nullptr;
      if (OB_FAIL(dag_scheduler->alloc_dag(dag))) {
        LOG_WARN("fail to alloc dag", K(ret));
      } else if (OB_ISNULL(dag)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error, dag is null", K(ret), KP(dag));
      } else if (OB_FAIL(dag->init(arg))) {
        LOG_WARN("fail to init complement data dag", K(ret), K(arg));
      } else if (OB_FAIL(dag->create_first_task())) {
        LOG_WARN("create first task failed", K(ret));
      } else if (OB_FAIL(add_dag_and_get_progress<ObComplementDataDag>(dag, res.row_inserted_, res.physical_row_count_))) {
        saved_ret = ret;
        if (OB_EAGAIN == ret) {
          ret = OB_SUCCESS;
        } else if (OB_SIZE_OVERFLOW == ret) {
          ret = OB_EAGAIN;
        } else {
          LOG_WARN("add dag and get progress failed", K(ret));
        }
      } else {
        dag = nullptr;
      }

      if (OB_NOT_NULL(dag)) {
        // to free dag.
        dag_scheduler->free_dag(*dag);
        dag = nullptr;
      }
      if (OB_FAIL(ret)) {
        // RS does not retry send RPC to tablet leader when the dag exists.
        ret = OB_EAGAIN == saved_ret ? OB_SUCCESS : ret;
        ret = OB_SIZE_OVERFLOW == saved_ret ? OB_EAGAIN : ret;
      }
      LOG_INFO("obs get rpc to build drop column dag", K(ret));
    } else if (ObDDLType(arg.ddl_type_) == ObDDLType::DDL_DROP_VEC_INDEX) {
      ObDagScheduler *dag_scheduler = nullptr;
      ObDeleteLobMetaRowDag *dag = nullptr;
      if (OB_ISNULL(dag_scheduler = ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("dag scheduler is null", K(ret));
      } else if (OB_FAIL(dag_scheduler->alloc_dag(dag))) {
        LOG_WARN("fail to alloc dag", K(ret));
      } else if (OB_ISNULL(dag)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected error, dag is null", K(ret), KP(dag));
      } else if (OB_FAIL(dag->init(arg))) {
        LOG_WARN("fail to init delete drop lob meta row dag", K(ret), K(arg));
      } else if (OB_FAIL(dag->create_first_task())) {
        LOG_WARN("create first task failed", K(ret));
      } else if (OB_FAIL(dag_scheduler->add_dag(dag))) {
        if (OB_EAGAIN == ret) {
          LOG_WARN("delete lob meta row dag already exists, no need to schedule once again", KR(ret));
          ret = OB_SUCCESS;
        } else if (OB_SIZE_OVERFLOW == ret) {
          LOG_WARN("dag is full", KR(ret));
          ret = OB_EAGAIN;
        } else {
          LOG_WARN("fail to add dag to queue", KR(ret));
        }
      } else {
        dag = nullptr;
      }
      if (OB_NOT_NULL(dag_scheduler) && OB_NOT_NULL(dag)) {
        (void) dag->handle_init_failed_ret_code(ret);
        dag_scheduler->free_dag(*dag);
        dag = nullptr;
      }
    } else {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid ddl type request", K(ret), K(arg));
    }
  }
  LOG_INFO("receive build local build request", K(ret), K(arg));
  return ret;
}

int ObService::check_and_cancel_ddl_complement_data_dag(const ObDDLLocalBuildArg &arg, bool &is_dag_exist)
{
  int ret = OB_SUCCESS;
  is_dag_exist = true;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_UNLIKELY(!is_complement_data_relying_on_dag(ObDDLType(arg.ddl_type_)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ddl type", K(ret), K(arg));
  } else {
    ObDagScheduler *dag_scheduler = nullptr;
    ObComplementDataDag *dag = nullptr;
    if (OB_ISNULL(dag_scheduler = ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dag scheduler is null", K(ret));
    } else if (OB_FAIL(dag_scheduler->alloc_dag(dag))) {
      LOG_WARN("fail to alloc dag", K(ret));
    } else if (OB_ISNULL(dag)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, dag is null", K(ret), KP(dag));
    } else if (OB_FAIL(dag->init(arg))) {
      LOG_WARN("fail to init complement data dag", K(ret), K(arg));
    } else if (OB_FAIL(dag_scheduler->check_dag_exist(dag, is_dag_exist))) {
      LOG_WARN("check dag exist failed", K(ret));
    } else if (is_dag_exist && OB_FAIL(dag_scheduler->cancel_dag(dag, true/*force_cancel, to cancel running dag by yield.*/))) {
      // sync to cancel ready dag only, not including running dag.
      LOG_WARN("cancel dag failed", KP(dag), K(ret));
    }
    if (OB_NOT_NULL(dag)) {
      (void) dag->handle_init_failed_ret_code(ret);
      dag_scheduler->free_dag(*dag);
      dag = nullptr;
    }
  }
  if (REACH_COUNT_INTERVAL(1000L)) {
    LOG_INFO("receive cancel ddl complement dag request", K(ret), K(is_dag_exist), K(arg));
  }
  return ret;
}

int ObService::check_and_cancel_delete_lob_meta_row_dag(const obcall::ObDDLLocalBuildArg &arg, bool &is_dag_exist)
{
  int ret = OB_SUCCESS;
  is_dag_exist = true;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_UNLIKELY(!is_delete_lob_meta_row_relying_on_dag(ObDDLType(arg.ddl_type_)))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid ddl type", K(ret), K(arg));
  } else {
    ObDagScheduler *dag_scheduler = nullptr;
    ObComplementDataDag *dag = nullptr;
    if (OB_ISNULL(dag_scheduler = ::oceanbase::share::server_service<::oceanbase::share::ObDagScheduler>())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("dag scheduler is null", K(ret));
    } else if (OB_FAIL(dag_scheduler->alloc_dag(dag))) {
      LOG_WARN("fail to alloc dag", K(ret));
    } else if (OB_ISNULL(dag)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("unexpected error, dag is null", K(ret), KP(dag));
    } else if (OB_FAIL(dag->init(arg))) {
      LOG_WARN("fail to init complement data dag", K(ret), K(arg));
    } else if (OB_FAIL(dag_scheduler->check_dag_exist(dag, is_dag_exist))) {
      LOG_WARN("check dag exist failed", K(ret));
    } else if (is_dag_exist && OB_FAIL(dag_scheduler->cancel_dag(dag))) {
      // sync to cancel ready dag only, not including running dag.
      LOG_WARN("cancel dag failed", K(ret));
    }
    if (OB_NOT_NULL(dag)) {
      dag_scheduler->free_dag(*dag);
      dag = nullptr;
    }
  }
  if (REACH_COUNT_INTERVAL(1000L)) {
    LOG_INFO("receive cancel ddl complement dag request", K(ret), K(is_dag_exist), K(arg));
  }
  return ret;
}

int ObService::inner_fill_tablet_info_(
    const ObTabletID &tablet_id,
    storage::ObLS *ls,
    ObTabletRuntimeInfo &runtime_info,
    share::ObTabletLocalChecksumItem &tablet_checksum)
{
  ObTabletHandle tablet_handle;
  int ret = OB_SUCCESS;
  ObTablet *tablet = nullptr;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret));
  } else if (!tablet_id.is_valid() || OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument or nullptr", KR(ret), K(tablet_id));
  } else if (OB_ISNULL(ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_tablet_svr is null", KR(ret), K(tablet_id));
  } else if (OB_FAIL(ls->get_tablet_svr()->get_tablet(
      tablet_id,
      tablet_handle,
      0,
      ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("get tablet failed", KR(ret), K(tablet_id));
    }
  } else if (OB_UNLIKELY(!tablet_handle.is_valid() || OB_ISNULL(tablet = tablet_handle.get_obj()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid tablet handle", KR(ret), K(tablet_id), K(tablet_handle), KPC(tablet));
  } else if (OB_FAIL(tablet->get_tablet_runtime_info(
     runtime_info, tablet_checksum))) {
    LOG_WARN("fail to get tablet runtime info", KR(ret), K(tablet_id));
  }
  return ret;
}

int ObService::fill_tablet_runtime_info(const ObTabletID &tablet_id,
    ObTabletRuntimeInfo &runtime_info,
    share::ObTabletLocalChecksumItem &tablet_checksum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret));
  } else if (!tablet_id.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id));
  } else {
    SERVER_MODULE_SCOPE {
      storage::ObLS *ls = nullptr;
      ObLSService* ls_svr = nullptr;
      if (OB_ISNULL(ls_svr = ::oceanbase::share::server_service<::oceanbase::storage::ObLSService>())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("server ObLSService is null", KR(ret));
      } else if (OB_FAIL(ls_svr->get_ls(ls))) {
        if (OB_LS_NOT_EXIST != ret) {
          LOG_WARN("fail to get local log stream", KR(ret));
        } else {
          LOG_TRACE("log stream does not exist in this runtime", KR(ret));
        }
      } else if (OB_ISNULL(ls)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("local log stream is null", KR(ret));
      } else if (OB_FAIL(inner_fill_tablet_info_(tablet_id,
                                                 ls,
                                                 runtime_info,
                                                 tablet_checksum))) {
        if (OB_TABLET_NOT_EXIST != ret) {
          LOG_WARN("fail to fill tablet runtime info", KR(ret), K(tablet_id), K(ls), K(runtime_info), K(tablet_checksum));
        } else {
          LOG_TRACE("tablet not exist in this log stream", KR(ret), K(tablet_id), K(ls), K(runtime_info), K(tablet_checksum));
        }
      }
    }
  }
  return ret;
}

}// end namespace observer
}// end namespace oceanbase
