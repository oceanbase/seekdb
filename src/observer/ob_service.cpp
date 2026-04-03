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



#include "ob_service.h"
#include "lib/alloc/memory_dump.h"

#include "share/ob_version.h"

#include "share/ob_version.h"
#include "share/deadlock/ob_deadlock_inner_table_service.h"
#include "share/ob_tablet_replica_checksum_operator.h" // ObTabletReplicaChecksumItem

#include "sql/optimizer/ob_storage_estimator.h"
#include "rootserver/ob_bootstrap.h"
#include "rootserver/ob_tenant_event_history_table_operator.h" // TENANT_EVENT_INSTANCE
#include "observer/ob_server.h"
#include "ob_server_event_history_table_operator.h"
#include "storage/ddl/ob_tablet_lob_split_task.h"
#include "storage/ddl/ob_delete_lob_meta_row_task.h" // delete lob meta row for drop vec index
#include "storage/ddl/ob_build_index_task.h"
#include "storage/tx_storage/ob_tenant_freezer.h"
#include "logservice/ob_log_service.h"        // ObLogService
#include "storage/backup/ob_backup_handler.h"
#include "storage/backup/ob_ls_backup_clean_mgr.h"
#include "share/backup/ob_backup_connectivity.h"
#include "share/ob_ddl_sim_point.h" // for DDL_SIM
#include "storage/compaction/ob_tenant_tablet_scheduler.h"
#include "share/ob_cluster_event_history_table_operator.h"//CLUSTER_EVENT_INSTANCE
#include "share/ob_zone_merge_table_operator.h"
#include "share/ob_global_merge_table_operator.h"
#include "share/ob_column_checksum_error_operator.h"
#include "storage/meta_store/ob_server_storage_meta_service.h"
#include "storage/column_store/ob_column_store_replica_util.h"
#include "share/backup/ob_backup_config.h"
#include "share/backup/ob_log_restore_struct.h"  // ObRestoreSourceServiceAttr
// ObLogRestoreSourceMgr removed - using config parameter instead
#include "share/ob_all_tenant_info.h"  // ObAllTenantInfoProxy
#include "share/ob_server_struct.h"    // GCTX
#include "logservice/restoreservice/ob_log_restore_net_driver.h"  // ObLogRestoreNetDriver
#include "logservice/restoreservice/ob_log_restore_service.h"  // ObLogRestoreService
#include "storage/tx_storage/ob_ls_service.h"  // ObLSService
#include "share/ob_rpc_struct.h"  // ObCreateLSArg

namespace oceanbase
{

using namespace common;
using namespace rootserver;
using namespace obrpc;
using namespace share;
using namespace share::schema;
using namespace storage;
using namespace backup;
using namespace palf;

namespace share
{
extern int report_telemetry(const char *reporter, const char *event_name);
}

namespace observer
{


ObSchemaReleaseTimeTask::ObSchemaReleaseTimeTask()
: schema_updater_(nullptr), is_inited_(false)
{}

int ObSchemaReleaseTimeTask::init(ObServerSchemaUpdater &schema_updater, int tg_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(is_inited_)) {
    ret = OB_INIT_TWICE;
    LOG_WARN("ObSchemaReleaseTimeTask has already been inited", K(ret));
  } else {
    schema_updater_ = &schema_updater;
    is_inited_ = true;
    if (OB_FAIL(schedule_())) {
      LOG_WARN("fail to schedule ObSchemaReleaseTimeTask in init", KR(ret));
    }
  }
  return ret;
}


int ObSchemaReleaseTimeTask::schedule_()
{
  int ret = OB_SUCCESS;
  int64_t memory_recycle_interval = GCONF._schema_memory_recycle_interval;
  if (0 == memory_recycle_interval) {
    memory_recycle_interval = 15L * 60L * 1000L * 1000L; //15mins
  }
  if (OB_FAIL(TG_SCHEDULE(lib::TGDefIDs::ServerGTimer, *this, memory_recycle_interval, false /*not schedule repeatly*/))) {
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

TelemetryTask::TelemetryTask(bool embed_mode)
  : embed_mode_(embed_mode)
{}

void TelemetryTask::runTimerTask()
{
  const char *env_reporter = std::getenv("REPORTER");
  const char *reporter = env_reporter ? env_reporter : (embed_mode_ ? "embed" : "server");
  share::report_telemetry(reporter, "bootstraped");
}

//////////////////////////////////////

// here gctx may hasn't been initialized already
ObService::ObService(const ObGlobalContext &gctx)
    : inited_(false),
    stopped_(false),
    schema_updater_(),
    gctx_(gctx), schema_release_task_(), telemetry_task_(false),
    need_bootstrap_(false)
{
}

ObService::~ObService()
{
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
  } else if (OB_ISNULL(GCTX.kv_storage_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("kv_storage_ is not initialized", K(ret));
  } else if (OB_FAIL(GCTX.kv_storage_->init(GCTX.meta_db_pool_))) {
    FLOG_WARN("init kv storage failed", KR(ret));
  } else if (OB_FAIL(CLUSTER_EVENT_INSTANCE.init(GCTX.meta_db_pool_))) {
    FLOG_WARN("init cluster event history table failed", KR(ret));
  } else if (OB_FAIL(TENANT_EVENT_INSTANCE.init(GCTX.meta_db_pool_, gctx_.self_addr()))) {
    FLOG_WARN("init tenant event history table failed", KR(ret), K(gctx_.self_addr()));
  } else if (OB_FAIL(SERVER_EVENT_INSTANCE.init(GCTX.meta_db_pool_, gctx_.self_addr()))) {
    FLOG_WARN("init server event history table failed", KR(ret));
  } else if (OB_FAIL(DEALOCK_EVENT_INSTANCE.init(sql_proxy))) {
    FLOG_WARN("init deadlock event history cleaner failed", KR(ret));
  } else if (OB_FAIL(ObZoneMergeTableOperator::init())) {
    FLOG_WARN("init zone merge table operator failed", KR(ret));
  } else if (OB_FAIL(ObGlobalMergeTableOperator::init())) {
    FLOG_WARN("init global merge table operator failed", KR(ret));
  } else if (OB_FAIL(ObColumnChecksumErrorOperator::init())) {
    FLOG_WARN("init column checksum error operator failed", KR(ret));
  } else if (OB_FAIL(ObTabletReplicaChecksumOperator::init())) {
    FLOG_WARN("init tablet replica checksum operator failed", KR(ret));
  } else if (OB_FAIL(OB_TSC_TIMESTAMP.init())) {
    FLOG_WARN("init tsc timestamp failed", KR(ret));
  } else if (OB_FAIL(schema_release_task_.init(schema_updater_, lib::TGDefIDs::ServerGTimer))) {
    FLOG_WARN("init schema release task failed", KR(ret));
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

int ObService::start(bool embed_mode)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVICE_NOTICE] start ob_service begin");
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("ob_service is not inited", KR(ret), K_(inited));
  } else if (need_bootstrap_) {
    // Initialize tenant info from server role before bootstrap
    // This ensures tenant info is always initialized for both primary and standby clusters
    if (OB_FAIL(share::ObAllTenantInfoProxy::init_tenant_info_from_server_role(
        GCTX.server_role_))) {
      LOG_ERROR("failed to init tenant info from server role before bootstrap", KR(ret), K(GCTX.server_role_));
    } else if (GCTX.is_standby_cluster()) {
      // Standby cluster
      if (OB_FAIL(bootstrap_standby())) {
        LOG_ERROR("bootstrap standby failed", KR(ret));
      }
    } else {
      // Primary cluster
      if (OB_FAIL(bootstrap())) {
        LOG_ERROR("bootstrap failed", KR(ret));
      }
    }
    if (OB_SUCC(ret)) {
      telemetry_task_.embed_mode_ = embed_mode;
      if (OB_SUCCESS != TG_SCHEDULE(lib::TGDefIDs::ServerGTimer, telemetry_task_,
          1L * 1000 * 1000, false)) {
        FLOG_ERROR("fail to schedule telemetry task");
      }
    }
    need_bootstrap_ = false;
  } else {
    // For restart (non-bootstrap), load tenant info from KV storage
    // and update GCTX.server_role_ to match the persisted role
    // KV storage must have data (written during bootstrap), so no fallback logic
    share::ObAllTenantInfo tenant_info;
    if (OB_FAIL(share::ObAllTenantInfoProxy::load_tenant_info(
        false, tenant_info))) {
      LOG_ERROR("failed to load tenant info from KV storage on restart, KV must have data from bootstrap",
               KR(ret));
    } else {
      // Successfully loaded tenant info, update GCTX.server_role_ to match
      if (tenant_info.is_primary()) {
        GCTX.server_role_ = common::PRIMARY_CLUSTER;
      } else if (tenant_info.is_standby()) {
        GCTX.server_role_ = common::STANDBY_CLUSTER;
      }
      LOG_INFO("loaded tenant info from KV storage and updated GCTX.server_role_ on restart",
               K(tenant_info), K(GCTX.server_role_));
    }
  }
  // set server id if needed
  if (OB_FAIL(ret)) {
  } else {
    if (0 == GCTX.get_server_id()) {
      (void) GCTX.set_server_id(OB_INIT_SERVER_ID);
    }
    if (0 == GCONF.observer_id) {
      GCONF.observer_id = OB_INIT_SERVER_ID;
    }
    if (OB_NOT_NULL(GCTX.config_mgr_)) {
      if (OB_FAIL(GCTX.config_mgr_->dump2file())) {
        LOG_WARN("dump server id to file failed", K(ret));
      }
    }
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

    FLOG_INFO("begin to stop schema updater");
    schema_updater_.stop();
    FLOG_INFO("schema updater stopped");

    FLOG_INFO("begin to stop deadlock event service");
    DEALOCK_EVENT_INSTANCE.stop();
    FLOG_INFO("deadlock event service stopped");

    FLOG_INFO("begin to stop server event instance");
    SERVER_EVENT_INSTANCE.stop();
    FLOG_INFO("server event instance stopped");

    FLOG_INFO("begin to stop cluster event instance");
    CLUSTER_EVENT_INSTANCE.stop();
    FLOG_INFO("cluster event instance stopped");

    FLOG_INFO("begin to stop tenant event instance");
    TENANT_EVENT_INSTANCE.stop();
    FLOG_INFO("tenant event instance stopped");

    // Schema refresh trigger is now managed by MTL framework
  }
  FLOG_INFO("[OBSERVICE_NOTICE] observice finish stop", K_(stopped));
}

void ObService::wait()
{
  FLOG_INFO("[OBSERVICE_NOTICE] wait ob_service begin");
  if (!inited_) {
    LOG_WARN_RET(OB_NOT_INIT, "ob_service not init", K_(inited));
  } else {
    FLOG_INFO("begin to wait schema updater");
    schema_updater_.wait();
    FLOG_INFO("wait schema updater success");

    FLOG_INFO("begin to wait deadlock event service");
    DEALOCK_EVENT_INSTANCE.wait();
    FLOG_INFO("wait deadlock event service success");

    FLOG_INFO("begin to wait server event instance");
    SERVER_EVENT_INSTANCE.wait();
    FLOG_INFO("wait server event instance success");

    FLOG_INFO("begin to wait cluster event instance");
    CLUSTER_EVENT_INSTANCE.wait();
    FLOG_INFO("wait cluster event instance success");

    FLOG_INFO("begin to wait tenant event instance");
    TENANT_EVENT_INSTANCE.wait();
    FLOG_INFO("wait tenant event instance success");

    // Schema refresh trigger is now managed by MTL framework
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
    FLOG_INFO("begin to destroy schema updater");
    schema_updater_.destroy();
    FLOG_INFO("schema updater destroyed");

    FLOG_INFO("begin to destroy cluster event instance");
    CLUSTER_EVENT_INSTANCE.destroy();
    FLOG_INFO("cluster event instance destroyed");

    FLOG_INFO("begin to destroy tenant event instance");
    TENANT_EVENT_INSTANCE.destroy();
    FLOG_INFO("tenant event instance destroyed");

    FLOG_INFO("begin to destroy server event instance");
    SERVER_EVENT_INSTANCE.destroy();
    FLOG_INFO("server event instance destroyed");

    FLOG_INFO("begin to destroy deadlock event service");
    DEALOCK_EVENT_INSTANCE.destroy();
    FLOG_INFO("deadlock event service destroyed");

    // Schema refresh trigger is now managed by MTL framework
    // restore_net_driver_ is now managed by ObLogRestoreService, no need to destroy here
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
             OB_SYS_TENANT_ID, schema_version))) {
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

int ObService::submit_async_refresh_schema_task(
    const uint64_t tenant_id,
    const int64_t schema_version)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_TENANT_ID == tenant_id
             || OB_INVALID_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(tenant_id), K(schema_version));
  } else if (OB_FAIL(schema_updater_.async_refresh_schema(tenant_id, schema_version))) {
    LOG_WARN("fail to async refresh schema", KR(ret), K(tenant_id), K(schema_version));
  }
  return ret;
}

// should return success if all partition have merge to specific frozen_version
int ObService::check_frozen_scn(const obrpc::ObCheckFrozenScnArg &arg)
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

int ObService::get_min_sstable_schema_version(
    const obrpc::ObGetMinSSTableSchemaVersionArg &arg,
    obrpc::ObGetMinSSTableSchemaVersionRes &result)
{
  int ret = OB_SUCCESS;
  ObMultiVersionSchemaService *schema_service = gctx_.schema_service_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(arg));
  } else if (OB_ISNULL(schema_service)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else {
    for (int i = 0; OB_SUCC(ret) && i < arg.tenant_id_arg_list_.size(); ++i) {
      // The minimum schema_version used by storage will increase with the major version,
      // storage only need to keep schema history used by a certain number major version.
      // For storage, there is no need to the server level statistics.
      // min_schema_version = scheduler.get_min_schema_version(arg.tenant_id_arg_list_.at(i));
      int tmp_ret = OB_SUCCESS;
      const uint64_t tenant_id = arg.tenant_id_arg_list_.at(i);
      int64_t min_schema_version = 0;
      int64_t tmp_min_schema_version = 0;
      if (OB_TMP_FAIL(schema_service->get_recycle_schema_version(
                         tenant_id, min_schema_version))) {
        min_schema_version = OB_INVALID_VERSION;
        LOG_WARN("fail to get recycle schema version", KR(tmp_ret), K(tenant_id));
      } else {
        MTL_SWITCH(tenant_id) {
          if (OB_TMP_FAIL(MTL(compaction::ObTenantTabletScheduler *)->get_min_dependent_schema_version(tmp_min_schema_version))) {
            min_schema_version = OB_INVALID_VERSION;
            if (OB_ENTRY_NOT_EXIST != tmp_ret) {
              LOG_WARN("failed to get min dependent schema version", K(tmp_ret));
            }
          } else if (tmp_min_schema_version != OB_INVALID_VERSION) {
            min_schema_version = MIN(min_schema_version, tmp_min_schema_version);
          }
        } else {
          if (OB_TENANT_NOT_IN_SERVER != ret) {
            STORAGE_LOG(WARN, "switch tenant failed", K(ret), K(tenant_id));
          } else {
            ret = OB_SUCCESS;
          }
        }
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(result.ret_list_.push_back(min_schema_version))) {
        LOG_WARN("push error", KR(ret), K(arg));
      }
    }
  }
  return ret;
}

int ObService::calc_column_checksum_request(const obrpc::ObCalcColumnChecksumRequestArg &arg, obrpc::ObCalcColumnChecksumRequestRes &res)
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
    const uint64_t tenant_id = arg.tenant_id_;
    int saved_ret = OB_SUCCESS;
    MTL_SWITCH(tenant_id) {
      ObGlobalUniqueIndexCallback *callback = NULL;
      ObTenantDagScheduler* dag_scheduler = nullptr;
      if (OB_ISNULL(dag_scheduler = MTL(ObTenantDagScheduler *))) {
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
          if (OB_TMP_FAIL(DDL_SIM(tenant_id, arg.task_id_, CALC_COLUMN_CHECKSUM_RPC_SLOW))) {
            LOG_WARN("ddl sim failure: calcualte column checksum rpc slow", K(tmp_ret), K(tenant_id), K(arg.task_id_));
          } else if (OB_TMP_FAIL(dag_scheduler->alloc_dag(dag))) {
            STORAGE_LOG(WARN, "fail to alloc dag", KR(tmp_ret));
          } else if (OB_TMP_FAIL(dag->init(arg.tenant_id_,
                                           calc_item.ls_id_,
                                           calc_item.tablet_id_,
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

int ObService::backup_ls_data(const obrpc::ObBackupDataArg &arg)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[BACKUP] receive backup ls data rpc", K(arg));
  ObBackupJobDesc job_desc;
  job_desc.job_id_ = arg.job_id_;
  job_desc.task_id_ = arg.task_id_;
  job_desc.trace_id_ = arg.trace_id_;
  share::ObBackupDest backup_dest;
  uint64_t tenant_id = arg.tenant_id_;
  ObBackupSetDesc backup_set_desc;
  backup_set_desc.backup_set_id_ = arg.backup_set_id_;
  backup_set_desc.backup_type_.type_ = arg.backup_type_;
  const ObLSID &ls_id = arg.ls_id_;
  const int64_t turn_id = arg.turn_id_;
  const int64_t retry_id = arg.retry_id_;
  const ObBackupDataType &backup_data_type = arg.backup_data_type_;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  if (!arg.is_valid() || OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(arg));
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(*sql_proxy, tenant_id, arg.backup_path_, backup_dest))) {
    LOG_WARN("failed to get backup dest", KR(ret), K(arg));
  } else if (OB_FAIL(ObBackupHandler::schedule_backup_data_dag(job_desc,
      backup_dest, tenant_id, backup_set_desc, ls_id, turn_id, retry_id, backup_data_type))) {
    LOG_WARN("failed to schedule backup data dag", K(ret), K(arg));
  } else {
    const char *backup_event_str = NULL;
    if (backup_data_type.is_sys_backup()) {
      backup_event_str = "schedule_backup_ls_sys_data";
    } else if (backup_data_type.is_user_backup()) {
      backup_event_str = "schedule_backup_ls_user_data";
    } else {
      backup_event_str = "unknown";
    }
    SERVER_EVENT_ADD("backup_data", backup_event_str,
      "tenant_id", arg.tenant_id_,
      "backup_set_id", arg.backup_set_id_,
      "ls_id", arg.ls_id_.id(),
      "turn_id", arg.turn_id_,
      "retry_id", arg.retry_id_,
      "trace_id", arg.trace_id_);
    LOG_INFO("success recevied backup ls data rpc", K(arg));
  }
  return ret;
}

int ObService::backup_completing_log(const obrpc::ObBackupComplLogArg &arg)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[BACKUP] receive backup completing log rpc", K(arg));
  ObBackupJobDesc job_desc;
  job_desc.job_id_ = arg.job_id_;
  job_desc.task_id_ = arg.task_id_;
  job_desc.trace_id_ = arg.trace_id_;
  share::ObBackupDest backup_dest;
  uint64_t tenant_id = arg.tenant_id_;
  ObBackupSetDesc backup_set_desc;
  backup_set_desc.backup_set_id_ = arg.backup_set_id_;
  backup_set_desc.backup_type_.type_ = arg.backup_type_;
  SCN start_scn = arg.start_scn_;
  SCN end_scn = arg.end_scn_;
  ObLSID ls_id = arg.ls_id_;
  const bool is_only_calc_stat = arg.is_only_calc_stat_;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  if (!arg.is_valid() || OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(arg));
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(*sql_proxy, tenant_id, arg.backup_path_, backup_dest))) {
    LOG_WARN("failed to get backup dest", KR(ret), K(arg));
  } else if (OB_FAIL(ObBackupHandler::schedule_backup_complement_log_dag(
      job_desc, backup_dest, tenant_id, backup_set_desc, ls_id, start_scn, end_scn, is_only_calc_stat))) {
    LOG_WARN("failed to schedule backup data dag", KR(ret), K(arg));
  } else {
    SERVER_EVENT_ADD("backup_data", "schedule_backup_complement_log",
      "tenant_id", arg.tenant_id_,
      "backup_set_id", arg.backup_set_id_,
      "ls_id", arg.ls_id_.id(),
      "start_scn", arg.start_scn_,
      "end_scn", arg.end_scn_,
      "trace_id", arg.trace_id_);
    LOG_INFO("success recevied backup compl log rpc", K(arg));
  }
  return ret;
}

int ObService::backup_build_index(const obrpc::ObBackupBuildIdxArg &arg)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[BACKUP] receive backup build index rpc", K(arg));
  ObBackupJobDesc job_desc;
  job_desc.job_id_ = arg.job_id_;
  job_desc.task_id_ = arg.task_id_;
  job_desc.trace_id_ = arg.trace_id_;
  share::ObBackupDest backup_dest;
  uint64_t tenant_id = arg.tenant_id_;
  ObBackupSetDesc backup_set_desc;
  backup_set_desc.backup_set_id_ = arg.backup_set_id_;
  backup_set_desc.backup_type_.type_ = arg.backup_type_;
  const int64_t turn_id = arg.turn_id_;
  const int64_t retry_id = arg.retry_id_;
  const share::ObBackupDataType backup_data_type = arg.backup_data_type_;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  if (!arg.is_valid() || OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(arg));
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(*sql_proxy, tenant_id, arg.backup_path_, backup_dest))) {
    LOG_WARN("failed to get backup dest", K(ret), K(arg));
  } else if (OB_FAIL(ObBackupHandler::schedule_build_tenant_level_index_dag(job_desc,
      backup_dest, tenant_id, backup_set_desc, turn_id, retry_id, backup_data_type))) {
    LOG_WARN("failed to schedule backup data dag", K(ret), K(arg));
  } else {
    SERVER_EVENT_ADD("backup_data", "schedule_build_tenant_level_index",
      "tenant_id", arg.tenant_id_,
      "backup_set_id", arg.backup_set_id_,
      "turn_id", arg.turn_id_,
      "backup_data_type", backup_data_type.type_,
      "job_id", arg.job_id_,
      "trace_id", arg.trace_id_);
  }
  LOG_INFO("success recevied backup build index rpc", K(ret), K(arg));
  return ret;
}

int ObService::backup_meta(const obrpc::ObBackupMetaArg &arg)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[BACKUP] receive backup meta rpc", K(arg));
  ObBackupJobDesc job_desc;
  job_desc.job_id_ = arg.job_id_;
  job_desc.task_id_ = arg.task_id_;
  job_desc.trace_id_ = arg.trace_id_;
  share::ObBackupDest backup_dest;
  uint64_t tenant_id = arg.tenant_id_;
  ObBackupSetDesc backup_set_desc;
  backup_set_desc.backup_set_id_ = arg.backup_set_id_;
  backup_set_desc.backup_type_.type_ = arg.backup_type_;
  const ObLSID &ls_id = arg.ls_id_;
  const int64_t turn_id = arg.turn_id_;
  const int64_t retry_id = arg.retry_id_;
  const SCN start_scn = arg.start_scn_;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  if (!arg.is_valid() || OB_ISNULL(sql_proxy)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(arg));
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(*sql_proxy, tenant_id, arg.backup_path_, backup_dest))) {
    LOG_WARN("failed to get backup dest", K(ret), K(arg));
  } else if (OB_FAIL(ObBackupHandler::schedule_backup_meta_dag(job_desc,
      backup_dest, tenant_id, backup_set_desc, ls_id, turn_id, retry_id, start_scn))) {
    LOG_WARN("failed to schedule backup data dag", KR(ret), K(arg));
  } else {
    SERVER_EVENT_ADD("backup_data", "schedule_backup_ls_meta",
      "tenant_id", arg.tenant_id_,
      "backup_set_id", arg.backup_set_id_,
      "ls_id", arg.ls_id_.id(),
      "turn_id", arg.turn_id_,
      "retry_id", arg.retry_id_,
      "trace_id", arg.trace_id_);
    LOG_INFO("success recevied backup ls meta rpc", K(arg));
  }
  return ret;
}

int ObService::backup_fuse_tablet_meta(const obrpc::ObBackupFuseTabletMetaArg &arg)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[BACKUP] receive backup fuse tablet meta rpc", K(arg));
  ObBackupJobDesc job_desc;
  job_desc.job_id_ = arg.job_id_;
  job_desc.task_id_ = arg.task_id_;
  job_desc.trace_id_ = arg.trace_id_;
  share::ObBackupDest backup_dest;
  uint64_t tenant_id = arg.tenant_id_;
  ObBackupSetDesc backup_set_desc;
  backup_set_desc.backup_set_id_ = arg.backup_set_id_;
  backup_set_desc.backup_type_.type_ = arg.backup_type_;
  const ObLSID &ls_id = arg.ls_id_;
  const int64_t turn_id = arg.turn_id_;
  const int64_t retry_id = arg.retry_id_;
  ObMySQLProxy *sql_proxy = GCTX.sql_proxy_;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("get invalid args", KR(ret), K(arg));
  } else if (OB_ISNULL(sql_proxy)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("sql proxy should not be null", K(ret), KP(sql_proxy));
  } else if (OB_FAIL(ObBackupStorageInfoOperator::get_backup_dest(*sql_proxy, tenant_id, arg.backup_path_, backup_dest))) {
    LOG_WARN("failed to get backup dest", K(ret), K(arg));
  } else if (OB_FAIL(ObBackupHandler::schedule_backup_fuse_tablet_meta_dag(
      job_desc, backup_dest, tenant_id, backup_set_desc, ls_id, turn_id, retry_id))) {
    LOG_WARN("failed to schedule backup data dag", KR(ret), K(arg));
  } else {
    SERVER_EVENT_ADD("backup_data", "schedule_backup_fuse_tablet_meta",
      "tenant_id", arg.tenant_id_,
      "backup_set_id", arg.backup_set_id_,
      "ls_id", arg.ls_id_.id(),
      "turn_id", arg.turn_id_,
      "retry_id", arg.retry_id_,
      "trace_id", arg.trace_id_);
    LOG_INFO("success received backup merge tablet meta rpc", K(arg));
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_CHECK_BACKUP_TASK_EXIST_ERROR);
int ObService::check_backup_task_exist(const ObBackupCheckTaskArg &arg, bool &res)
{
  int ret = OB_SUCCESS;
  res = false;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    MTL_SWITCH(arg.tenant_id_) {
      ObTenantDagScheduler* dag_scheduler = nullptr;
      if (OB_ISNULL(dag_scheduler = MTL(ObTenantDagScheduler *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, dag scheduler must not be nullptr", K(ret));
      } else if (OB_FAIL(dag_scheduler->check_dag_net_exist(arg.trace_id_, res))) {
        LOG_WARN("failed to check dag net exist", K(ret), K(arg));
      }
    }
  }
#ifdef ERRSIM
  if (OB_SUCC(ret) && ERRSIM_CHECK_BACKUP_TASK_EXIST_ERROR) {
    res = true;
    ret = ERRSIM_CHECK_BACKUP_TASK_EXIST_ERROR;
    LOG_WARN("check backup task exist failed", K(ret), K(arg));
  }
#endif
  return ret;
}

int ObService::delete_backup_ls_task(const obrpc::ObLSBackupCleanArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive delete backup ls task request", K(arg));
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObService not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(ObLSBackupCleanScheduler::schedule_backup_clean_dag(arg))) {
    LOG_WARN("failed to schedule backup clean dag", K(ret), K(arg));
  } else {
    LOG_INFO("success receive delete backup ls task rpc", K(arg));
  }

  return ret;
}

int ObService::notify_archive(const obrpc::ObNotifyArchiveArg &arg)
{
  return OB_NOT_SUPPORTED;
}



int ObService::minor_freeze(const obrpc::ObMinorFreezeArg &arg,
                            obrpc::Int64 &result)
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
  } else if (arg.ls_id_.is_valid() || arg.tablet_id_.is_valid()) {
    ret = handle_ls_freeze_req_(arg);
  } else if (arg.tenant_ids_.count() > 0) {
    ret = handle_tenant_freeze_req_(arg);
  } else {
    ret = handle_server_freeze_req_(arg);
  }

  result = ret;
  const int64_t cost_ts = ObTimeUtility::current_time() - start_ts;
  LOG_INFO("finish minor freeze request", K(ret), K(arg), K(cost_ts));
  return ret;
}

int ObService::handle_server_freeze_req_(const obrpc::ObMinorFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    SERVER_LOG(WARN, "failed to get multi tenant from GCTX", K(ret));
  } else {
    int tmp_ret = OB_SUCCESS;
    uint64_t tenant_id = OB_SYS_TENANT_ID;
    if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = tenant_freeze_(tenant_id)))) {
      if (OB_TENANT_NOT_IN_SERVER == tmp_ret) {
        LOG_INFO("skip freeze stopped tenant", K(tmp_ret), K(tenant_id));
        tmp_ret = OB_SUCCESS;
      } else {
        LOG_WARN("fail to freeze tenant", K(tmp_ret), K(tenant_id));
      }
    }
  }
  return ret;
}

int ObService::handle_tenant_freeze_req_(const obrpc::ObMinorFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  for (int i = 0; i < arg.tenant_ids_.count(); ++i) {
    int tmp_ret = OB_SUCCESS;
    uint64_t tenant_id = arg.tenant_ids_.at(i);
    if (OB_UNLIKELY(OB_SUCCESS != (tmp_ret = tenant_freeze_(tenant_id)))) {
      LOG_WARN("fail to freeze tenant", K(tmp_ret), K(tenant_id));
    }
    // record the first error code
    if (OB_SUCCESS != tmp_ret && OB_SUCC(ret)) {
      ret = tmp_ret;
    }
  }
  return ret;
}

int ObService::handle_ls_freeze_req_(const obrpc::ObMinorFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  if (1 != arg.tenant_ids_.count()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("only one tenant is needed", K(ret), K(arg.tenant_ids_), K(arg.tablet_id_));
  } else if (OB_FAIL(handle_ls_freeze_req_(arg.tenant_ids_.at(0), arg.ls_id_, arg.tablet_id_))) {
    LOG_WARN("fail to freeze tablet", K(ret), K(arg));
  }
  return ret;
}

int ObService::handle_ls_freeze_req_(const uint64_t tenant_id,
                                     const share::ObLSID &ls_id,
                                     const common::ObTabletID &tablet_id)
{
  int ret = OB_SUCCESS;

  if (is_virtual_tenant_id(tenant_id)) {
    LOG_INFO("no need to freeze virtual tenant", K(ret), K(tenant_id), K(tablet_id));
  } else {
    MTL_SWITCH(tenant_id) {
      storage::ObTenantFreezer* freezer = nullptr;
      if (OB_ISNULL(freezer = MTL(storage::ObTenantFreezer*))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ObTenantFreezer shouldn't be null", K(ret), K(tenant_id));
      } else if (tablet_id.is_valid()) {
        // tablet freeze
        const bool is_sync = true;
        if (OB_FAIL(freezer->tablet_freeze(ls_id,
                                           tablet_id,
                                           is_sync,
                                           0 /*max_retry_time_us*/,
                                           false, /*rewrite_tablet_meta*/
                                           ObFreezeSourceFlag::USER_MINOR_FREEZE))) {
          if (OB_EAGAIN == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("fail to freeze tablet", K(ret), K(tenant_id), K(ls_id), K(tablet_id));
          }
        } else {
          LOG_INFO("succeed to freeze tablet", K(ret), K(tenant_id), K(ls_id), K(tablet_id));
        }
      } else {
        // logstream freeze
        if (OB_FAIL(freezer->ls_freeze_all_unit(ls_id, ObFreezeSourceFlag::USER_MINOR_FREEZE))) {
          if (OB_EAGAIN == ret) {
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("fail to freeze ls", K(ret), K(tenant_id), K(ls_id), K(tablet_id));
          }
        } else {
          LOG_INFO("succeed to freeze ls", K(ret), K(tenant_id), K(ls_id), K(tablet_id));
        }
      }
    }
  }

  return ret;
}

int ObService::tenant_freeze_(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;

  if (is_virtual_tenant_id(tenant_id)) {
    LOG_INFO("no need to freeze virtual tenant", K(ret), K(tenant_id));
  } else {
    MTL_SWITCH(tenant_id) {
      storage::ObTenantFreezer* freezer = nullptr;
      if (OB_ISNULL(freezer = MTL(storage::ObTenantFreezer*))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ObTenantFreezer shouldn't be null", K(ret), K(tenant_id));
      } else if (freezer->exist_ls_freezing()) {
        LOG_INFO("exist running ls_freeze", K(ret), K(tenant_id));
      } else if (OB_FAIL(freezer->tenant_freeze(ObFreezeSourceFlag::USER_MINOR_FREEZE))) {
        if (OB_ENTRY_EXIST == ret) {
          ret = OB_SUCCESS;
        } else {
          LOG_WARN("fail to freeze tenant", K(tenant_id), K(ret));
        }
      } else {
        LOG_INFO("succeed to freeze tenant", K(tenant_id), K(ret));
      }
    } else {
      LOG_WARN("fail to switch tenant", K(ret), K(tenant_id));
    }
  }

  return ret;
}

int ObService::tablet_major_freeze(const obrpc::ObTabletMajorFreezeArg &arg,
                            obrpc::Int64 &result)
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
    MTL_SWITCH(arg.tenant_id_) {
      if (OB_FAIL(MTL(compaction::ObTenantTabletScheduler *)->user_request_schedule_medium_merge(
        arg.ls_id_, arg.tablet_id_, arg.is_rebuild_column_group_))) {
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
    const obrpc::ObCheckModifyTimeElapsedArg &arg,
    obrpc::ObCheckModifyTimeElapsedResult &result)
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
    MTL_SWITCH(arg.tenant_id_) {
      ObLSHandle ls_handle;
      SCN tmp_scn;
      transaction::ObTransService *txs = MTL(transaction::ObTransService *);
      ObLSService *ls_service = MTL(ObLSService *);
      if (OB_FAIL(result.results_.reserve(arg.tablets_.count()))) {
        LOG_WARN("reserve result array failed", K(ret), K(arg.tablets_.count()));
      }

      for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablets_.count(); ++i) {
        ObTabletHandle tablet_handle;
        ObLSHandle ls_handle;
        const ObLSID &ls_id = arg.tablets_.at(i).ls_id_;
        const ObTabletID &tablet_id = arg.tablets_.at(i).tablet_id_;
        SCN snapshot_version;
        ObCheckTransElapsedResult single_result;
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(DDL_SIM(arg.tenant_id_, arg.ddl_task_id_, CHECK_MODIFY_TIME_ELAPSED_SLOW))) {
          LOG_WARN("ddl sim failure: check modify time elapsed slow", K(tmp_ret), K(arg.tenant_id_), K(arg.ddl_task_id_));
        } else if (OB_TMP_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
          LOG_WARN("get ls failed", K(tmp_ret), K(ls_id));
        } else if (OB_TMP_FAIL(ls_handle.get_ls()->check_modify_time_elapsed(tablet_id,
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
    const obrpc::ObCheckSchemaVersionElapsedArg &arg,
    obrpc::ObCheckSchemaVersionElapsedResult &result)
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
    MTL_SWITCH(arg.tenant_id_) {
      ObLSService *ls_service = nullptr;
      if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("error unexpected, get ls service failed", K(ret));
      } else if (OB_FAIL(result.results_.reserve(arg.tablets_.count()))) {
        LOG_WARN("reserve result array failed", K(ret), K(arg.tablets_.count()));
      }
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablets_.count(); ++i) {
        ObTabletHandle tablet_handle;
        ObLSHandle ls_handle;
        const ObLSID &ls_id = arg.tablets_.at(i).ls_id_;
        const ObTabletID &tablet_id = arg.tablets_.at(i).tablet_id_;
        ObCheckTransElapsedResult single_result;
        int tmp_ret = OB_SUCCESS;
        bool is_leader_serving = false;
        if (OB_TMP_FAIL(DDL_SIM(arg.tenant_id_, arg.ddl_task_id_, CHECK_SCHEMA_TRANS_END_SLOW))) {
          LOG_WARN("ddl sim failure: check schema version elapsed slow", K(tmp_ret), K(arg));
        } else if (OB_TMP_FAIL(ls_service->get_ls(ls_id, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
          LOG_WARN("get ls failed", K(tmp_ret), K(i), K(ls_id));
        } else if (OB_TMP_FAIL(ls_handle.get_ls()->get_tx_svr()->check_in_leader_serving_state(is_leader_serving))) {
          LOG_WARN("fail to check ls in leader serving state", K(tmp_ret), K(ls_id));
        } else if (!is_leader_serving) {
          tmp_ret = OB_NOT_MASTER;   // check is leader ready
          LOG_WARN("ls leader is not ready, should not provide service", K(ret));
        } else if (OB_TMP_FAIL(ls_handle.get_ls()->get_tablet(tablet_id,
                                                              tablet_handle,
                                                              ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US,
                                                              ObMDSGetTabletMode::READ_ALL_COMMITED))) {
          LOG_WARN("fail to get tablet", K(tmp_ret), K(i), K(ls_id), K(tablet_id));
        } else if (OB_TMP_FAIL(tablet_handle.get_obj()->check_schema_version_elapsed(arg.schema_version_,
                                                                                     arg.need_wait_trans_end_,
                                                                                     single_result.snapshot_,
                                                                                     single_result.pending_tx_id_))) {
          LOG_WARN("check schema version elapsed failed", K(tmp_ret), K(arg), K(ls_id), K(tablet_id));
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
int ObService::check_memtable_cnt(
    const obrpc::ObCheckMemtableCntArg &arg,
    obrpc::ObCheckMemtableCntResult &result)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive check memtable cnt request", K(arg));
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    ObMinorFreezeArg minor_freeze_arg;
    minor_freeze_arg.ls_id_ = arg.ls_id_;
    minor_freeze_arg.tablet_id_ = arg.tablet_id_;
    if (OB_FAIL(minor_freeze_arg.tenant_ids_.push_back(arg.tenant_id_))) {
      LOG_WARN("failed to push back tenant id", K(ret));
    } else if (OB_FAIL(handle_ls_freeze_req_(minor_freeze_arg))) {
      LOG_WARN("failed to handle tablet freeze", K(ret));
    } else {
      MTL_SWITCH(arg.tenant_id_) {
        bool freeze_finished = false;
        ObTabletID tablet_id = arg.tablet_id_;
        const int64_t expire_renew_time = INT64_MAX;
        bool is_cache_hit = false;
        ObLSID ls_id = arg.ls_id_;
        ObLSService *ls_srv = MTL(ObLSService *);
        ObLSHandle ls_handle;
        ObLS *ls = nullptr;
        ObLSTabletService *ls_tablet_service = nullptr;
        ObTabletHandle tablet_handle;
        ObTablet *tablet = nullptr;
        ObArray<ObTableHandleV2> memtable_handles;
        if (OB_FAIL(ls_srv->get_ls(ls_id, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
          LOG_WARN("fail to get ls", K(ret), K(ls_id));
        } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ls is null", K(ret), K(ls_id));
        } else if (OB_ISNULL(ls_tablet_service = ls->get_tablet_svr())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("tablet service should not be null", K(ret), K(ls_id));
        } else if (OB_FAIL(ls_tablet_service->get_tablet(tablet_id,
                tablet_handle, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_10_S, ObMDSGetTabletMode::READ_ALL_COMMITED))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("get tablet handle failed", K(ret), K(tablet_id));
        } else if (FALSE_IT(tablet = tablet_handle.get_obj())) {
        } else if (OB_FAIL(tablet->get_all_memtables_from_memtable_mgr(memtable_handles))) {
          LOG_WARN("failed to get_memtable_mgr for get all memtable", K(ret), KPC(tablet));
        } else {
          result.memtable_cnt_ = memtable_handles.count();
          freeze_finished = result.memtable_cnt_ == 0 ? true : false;
          if (freeze_finished) {
            share::SCN unused_scn;
            ObTabletFreezeLog freeze_log;
            freeze_log.tablet_id_ = tablet_id;
            if (OB_FAIL(storage::ObDDLRedoLogWriter::
                  write_auto_split_log(ls_id,
                                       ObDDLClogType::DDL_TABLET_FREEZE_LOG,
                                       logservice::ObReplayBarrierType::STRICT_BARRIER,
                                       freeze_log, unused_scn))) {
              LOG_WARN("write tablet freeze log failed", K(ret), K(freeze_log));
            }
          }
        }
      } // MTL_SWITCH
    }
  }
  LOG_INFO("finish check memtable cnt request", K(ret), K(arg));
  return ret;
}

// possible results:
// 1. ret != OB_SUCCESS
// 2. ret == OB_SUCCESS && info_list_cnt_ > 0 && invalid compaction_scn
// 3. ret == OB_SUCCESS && info_list_cnt_ == 0 && valid primary_compaction_scn_
int ObService::check_medium_compaction_info_list_cnt(
    const obrpc::ObCheckMediumCompactionInfoListArg &arg,
    obrpc::ObCheckMediumCompactionInfoListResult &result)
{
  return ObTabletSplitUtil::check_medium_compaction_info_list_cnt(arg, result);
}

int ObService::prepare_tablet_split_task_ranges(
    const obrpc::ObPrepareSplitRangesArg &arg,
    obrpc::ObPrepareSplitRangesRes &result)
{
  int ret = OB_SUCCESS;
  result.parallel_datum_rowkey_list_.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(ObTabletSplitUtil::split_task_ranges(result.rowkey_allocator_, arg.ddl_type_, arg.ls_id_,
      arg.tablet_id_, arg.user_parallelism_, arg.schema_tablet_size_, result.parallel_datum_rowkey_list_))) {
    LOG_WARN("split task ranges failed", K(ret));
  }
  return ret;
}

int ObService::check_ddl_tablet_merge_status(
    const obrpc::ObDDLCheckTabletMergeStatusArg &arg,
    obrpc::ObDDLCheckTabletMergeStatusResult &result)
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
    MTL_SWITCH(arg.tenant_id_) {
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablet_ids_.count(); ++i) {
        const common::ObTabletID &tablet_id = arg.tablet_ids_.at(i);
        ObTabletHandle tablet_handle;
        ObLSHandle ls_handle;
        ObDDLKvMgrHandle ddl_kv_mgr_handle;
        ObLSService *ls_service = nullptr;
        bool status = false;

        if (OB_ISNULL(ls_service = MTL(ObLSService *))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("error unexpected, get ls service failed", K(ret));
        } else if (OB_UNLIKELY(!tablet_id.is_valid())) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("invalid arguments", K(ret), K(arg));
        } else if (OB_FAIL(ls_service->get_ls(arg.ls_id_, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
          LOG_WARN("get ls failed", K(ret), K(arg));
        } else if (OB_FAIL(ls_handle.get_ls()->get_tablet(tablet_id, tablet_handle))) {
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

int ObService::batch_switch_rs_leader(const ObAddr &arg)
{
  UNUSEDx(arg);
  int ret = OB_NOT_SUPPORTED;
  // LOG_INFO("receive batch switch rs leader request", K(arg));

  // int64_t start_timestamp = ObTimeUtility::current_time();
  // if (OB_UNLIKELY(!inited_)) {
  //   ret = OB_NOT_INIT;
  //   LOG_WARN("not init", KR(ret));
  // } else if (OB_ISNULL(gctx_.par_ser_)) {
  //   ret = OB_ERR_UNEXPECTED;
  //   LOG_WARN("gctx par_ser is NULL", K(arg));
  // } else if (!arg.is_valid()) {
  //   if (OB_FAIL(gctx_.par_ser_->auto_batch_change_rs_leader())) {
  //     LOG_WARN("fail to auto batch change rs leader", KR(ret));
  //   }
  // } else if (OB_FAIL(gctx_.par_ser_->batch_change_rs_leader(arg))) {
  //   LOG_WARN("fail to batch change rs leader", K(arg), KR(ret));
  // }

  // int64_t cost = ObTimeUtility::current_time() - start_timestamp;
  // SERVER_EVENT_ADD("election", "batch_switch_rs_leader", K(ret),
  //                  "leader", arg,
  //                  K(cost));
  return ret;
}

int ObService::switch_schema(
    const obrpc::ObSwitchSchemaArg &arg,
    obrpc::ObSwitchSchemaResult &result)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("start to switch schema", K(arg));
  const ObRefreshSchemaInfo &schema_info = arg.schema_info_;
  const int64_t schema_version = schema_info.get_schema_version();
  const uint64_t tenant_id = schema_info.get_tenant_id();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(schema_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",  KR(ret), K(schema_version));
  } else if (arg.is_async_) {
    const bool set_received_schema_version = true;
    if (OB_FAIL(schema_updater_.try_reload_schema(
        schema_info, set_received_schema_version))) {
      LOG_WARN("reload schema failed", KR(ret), K(schema_info));
    }
  } else {
    ObSEArray<uint64_t, 1> tenant_ids;
    ObMultiVersionSchemaService *schema_service = gctx_.schema_service_;
    int64_t local_schema_version = OB_INVALID_VERSION;
    int64_t abs_timeout = OB_INVALID_TIMESTAMP;
    if (OB_UNLIKELY(!schema_info.is_valid() || !is_valid_tenant_id(tenant_id))) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("invalid schema info", KR(ret), K(schema_info), K(tenant_id));
    } else if (OB_FAIL(tenant_ids.push_back(tenant_id))) {
      LOG_WARN("fail to push back tenant_id", KR(ret), K(tenant_id));
    } else if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema service is null", KR(ret));
    } else if (OB_FAIL(ObShareUtil::get_abs_timeout(GCONF.rpc_timeout, abs_timeout))) {
      LOG_WARN("fail to get abs timeout", KR(ret), "default_timeout", static_cast<int64_t>(GCONF.rpc_timeout));
    } else {
      // To set the received_schema_version period in advance,
      // let refresh_schema can execute before analyze_dependencies logic;
      int64_t LEFT_TIME = 200 * 1000;// 200ms
      int64_t origin_timeout_ts = THIS_WORKER.get_timeout_ts();
      if (INT64_MAX != origin_timeout_ts
          && origin_timeout_ts >= ObTimeUtility::current_time() + LEFT_TIME) {
        THIS_WORKER.set_timeout_ts(origin_timeout_ts - LEFT_TIME);
      }
      if (OB_FAIL(schema_service->async_refresh_schema(tenant_id, schema_version))) {
        LOG_WARN("fail to async schema version", KR(ret), K(tenant_id), K(schema_version));
      }
      THIS_WORKER.set_timeout_ts(origin_timeout_ts);
      int64_t tmp_ret = OB_SUCCESS;
      if (OB_SUCCESS != (tmp_ret = schema_service->set_tenant_received_broadcast_version(tenant_id, schema_version))) {
        LOG_WARN("failt to update received schema version", KR(tmp_ret), K(tenant_id), K(schema_version));
        ret = OB_SUCC(ret) ? tmp_ret : ret;
      }
      if (THIS_WORKER.is_timeout_ts_valid()
          && !THIS_WORKER.is_timeout()
          && OB_TIMEOUT == ret) {
        // To set set_tenant_received_broadcast_version in advance, we reduce the abs_time,
        // if not timeout after first async_refresh_schema, we should execute async_refresh_schema again and overwrite the ret code
        if (OB_FAIL(schema_service->async_refresh_schema(tenant_id, schema_version))) {
          LOG_WARN("fail to async schema version", KR(ret), K(tenant_id), K(schema_version));
        }
      }
      if (OB_FAIL(ret)) {
      } else if (schema_info.get_schema_version() <= 0) {
        // skip
      } else if (OB_FAIL(schema_service->get_tenant_refreshed_schema_version(
                         tenant_id, local_schema_version))) {
        LOG_WARN("fail to get local tenant schema_version", KR(ret), K(tenant_id));
      } else if (OB_UNLIKELY(schema_info.get_schema_version() > local_schema_version)) {
        ret = OB_EAGAIN;
        LOG_WARN("schema is not new enough", KR(ret), K(schema_info), K(local_schema_version));
      }
    }
  }
  FLOG_INFO("switch schema", KR(ret), K(schema_info));
  //SERVER_EVENT_ADD("schema", "switch_schema", K(ret), K(schema_info));
  result.set_ret(ret);
  return ret;
}

int ObService::broadcast_consensus_version(
    const obrpc::ObBroadcastConsensusVersionArg &arg,
    obrpc::ObBroadcastConsensusVersionRes &result)
{
  int ret = OB_SUCCESS;
  int64_t local_consensus_version = OB_INVALID_VERSION;
  const uint64_t tenant_id = arg.get_tenant_id();
  const int64_t consensus_version = arg.get_consensus_version();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(consensus_version <= 0)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument",  KR(ret), K(consensus_version));
  } else if (OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret));
  } else if (OB_FAIL(gctx_.schema_service_->get_tenant_broadcast_consensus_version(tenant_id, local_consensus_version))) {
    LOG_WARN("fail to get local tenant consensus_version", KR(ret), K(tenant_id));
  } else if (OB_UNLIKELY(consensus_version < local_consensus_version)) {
    ret = OB_EAGAIN;
    LOG_WARN("consensus version is less than local consensus version", KR(ret), K(consensus_version), K(local_consensus_version));
  } else if (OB_FAIL(gctx_.schema_service_->set_tenant_broadcast_consensus_version(tenant_id, consensus_version))) {
    LOG_WARN("failt to update received schema version", KR(ret), K(tenant_id), K(consensus_version));
  }
  result.set_ret(ret);
  return OB_SUCCESS;
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
  } else if (OB_ISNULL(gctx_.root_service_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("root service is null", K(ret));
  } else {
    BOOTSTRAP_LOG(INFO, "begin bootstrap");
    ObPreBootstrap pre_bootstrap(*gctx_.srv_rpc_proxy_,
                                 *gctx_.config_,
                                 *gctx_.rs_rpc_proxy_);
    ObAddr master_rs;
    bool server_empty = false;
    if (OB_FAIL(check_server_empty(server_empty))) {
      BOOTSTRAP_LOG(WARN, "check_server_empty failed", K(ret));
    } else if (!server_empty) {
      ret = OB_ERR_SYS;
      BOOTSTRAP_LOG(WARN, "this observer is not empty", KR(ret), K(GCTX.self_addr()));
    } else if (OB_FAIL(pre_bootstrap.prepare_bootstrap(master_rs))) {
      BOOTSTRAP_LOG(ERROR, "failed to prepare boot strap", K(ret));
    } else {
      BOOTSTRAP_LOG(INFO, "waiting for root service to be in service");
      while (OB_SUCC(ret) && !gctx_.root_service_->in_service()) {
        ob_throttle_usleep(200 * 1000, OB_RS_NOT_MASTER);
      }
      BOOTSTRAP_LOG(INFO, "root service is in service");
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(gctx_.root_service_->execute_bootstrap())) {
      BOOTSTRAP_LOG(ERROR, "failed to execute bootstrap", K(ret));
    } else {
      BOOTSTRAP_LOG(INFO, "succeed to do_boot_strap", K(master_rs));
    }
  }

  return ret;
}

int ObService::bootstrap_standby()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[OBSERVICE_NOTICE] bootstrap standby begin");

  // 1. Skip normal bootstrap (standby_restore not ready yet)
  // 2. Create sys tenant if not exists (simplified: use hidden sys tenant)
  // 3. Initialize merge info tables (same as primary cluster)
  // 4. Create sys tenant log stream if not exists (empty LS for standby)
  // 5. Setup log sync from primary

  if (!GCTX.is_standby_cluster()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("bootstrap_standby called but not standby cluster", KR(ret));
  } else {
    // TODO: Create sys tenant if not exists
    // For now, assume hidden sys tenant is already created during ObServer::init()
    // This will be handled by ObMultiTenant::create_hidden_sys_tenant()

    // Initialize merge info tables (same logic as primary cluster in insert_tenant_merge_info_)
    if (OB_FAIL(init_tenant_merge_info_(OB_SYS_TENANT_ID))) {
      LOG_ERROR("init tenant merge info failed", KR(ret), K(OB_SYS_TENANT_ID));
    }

    // Create sys tenant log stream if not exists (empty LS for standby)
    if (OB_SUCC(ret)) {
      MTL_SWITCH(OB_SYS_TENANT_ID) {
        if (OB_FAIL(create_sys_ls())) {
          LOG_ERROR("create sys log stream failed", KR(ret));
        } else if (OB_FAIL(schedule_standby_restore_task())) {
          LOG_WARN("failed to schedule standby sys ls restore task", KR(ret));
        }
      }
    }

    // Note: log sync is now handled by ObLogRestoreService background thread
    // which reads restore source from config parameter log_restore_source
    // Use: -o log_restore_source='SERVICE=ip:port USER=user@tenant PASSWORD=xxx'

    // Schema refresh trigger is now managed by MTL framework
    // It will be started automatically when tenant is created
    // and checks tenant role at runtime to decide whether to refresh schema
  }

  FLOG_INFO("[OBSERVICE_NOTICE] bootstrap standby end", KR(ret));
  return ret;
}

int ObService::init_tenant_merge_info_(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  // Same logic as ObTenantDDLService::insert_tenant_merge_info_
  HEAP_VARS_2((ObGlobalMergeInfo, global_info),
              (ObZoneMergeInfo, zone_merge_info)) {
    global_info.tenant_id_ = tenant_id;
    zone_merge_info.tenant_id_ = tenant_id;

    // Insert global merge info
    if (OB_FAIL(ObGlobalMergeTableOperator::insert_global_merge_info(*GCTX.sql_proxy_,
        tenant_id, global_info))) {
      LOG_WARN("fail to insert global merge info", KR(ret), K(tenant_id), K(global_info));
    } else {
      // Insert zone merge info (one zone for standby)
      ObSEArray<ObZoneMergeInfo, 1> merge_info_array;
      if (OB_FAIL(merge_info_array.push_back(zone_merge_info))) {
        LOG_WARN("fail to push_back zone merge info", KR(ret));
      } else if (OB_FAIL(ObZoneMergeTableOperator::insert_zone_merge_infos(*GCTX.sql_proxy_,
          tenant_id, merge_info_array))) {
        LOG_WARN("fail to insert zone merge infos", KR(ret), K(tenant_id));
      } else {
        LOG_INFO("succ to init tenant merge info for standby bootstrap",
                 K(tenant_id), K(global_info), K(zone_merge_info));
      }
    }
  }
  return ret;
}

int ObService::create_sys_ls()
{
  int ret = OB_SUCCESS;
  LOG_INFO("create empty LS for standby");
  storage::ObLSService *ls_service = nullptr;
  logservice::ObLogService *log_service = nullptr;
  if (!GCTX.is_standby_cluster()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("create_sys_tenant_ls_if_not_exists called but not standby cluster", KR(ret));
  } else if (OB_ISNULL(ls_service = MTL(storage::ObLSService *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls_service is null");
  } else if (OB_ISNULL(log_service = MTL(logservice::ObLogService *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log_service is null");
  } else {
    if (OB_FAIL(ls_service->create_ls_for_ha())) {
      LOG_WARN("create ls failed", KR(ret));
    }
  }

  return ret;
}


int ObService::build_restore_source_attr(const common::ObAddr &primary_addr,
                                          share::ObRestoreSourceServiceAttr &source_attr)
{
  int ret = OB_SUCCESS;

  if (!primary_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("primary_addr is invalid", K(primary_addr), K(ret));
  } else {
    source_attr.reset();

    // Add primary address to addr list
    if (OB_FAIL(source_attr.addr_.push_back(primary_addr))) {
      LOG_WARN("failed to push back primary addr", K(primary_addr), K(ret));
    } else if (!source_attr.is_valid()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("built restore source attr is invalid", K(source_attr), K(ret));
    } else {
      LOG_INFO("built restore source attr", K(source_attr));
    }
  }

  return ret;
}

int ObService::schedule_standby_restore_task()
{
  int ret = OB_SUCCESS;
  common::ObAddr primary_addr;
  oceanbase::restore::ObRestoreTask task;
  ObLS *ls = NULL;
  ObLSService *ls_srv = MTL(ObLSService *);
  ObLSID ls_id(share::ObLSID::SYS_LS_ID);
  ObLSHandle ls_handle;
  restore::ObRestoreHandler *ls_restore_handler = nullptr;

  const common::ObString config_value = GCONF.log_restore_source.str();
  if (!GCTX.is_standby_cluster()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schedule_standby_restore_task called but not standby cluster", KR(ret));
  } else if (config_value.empty()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("log_restore_source config is empty", KR(ret));
  } else if (OB_FAIL(primary_addr.parse_from_string(config_value))) {
    LOG_WARN("failed to parse primary_addr from log_restore_source", K(config_value), KR(ret));
  } else if (!primary_addr.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("primary_addr is invalid", K(primary_addr), KR(ret));
  } else if (OB_ISNULL(ls_srv)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls_service is null", KR(ret));
  } else if (OB_FAIL(ls_srv->get_ls(ls_id, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
    LOG_WARN("failed to get ls", K(ret));
  } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls is null", KR(ret));
  } else if (OB_ISNULL(ls_restore_handler = ls->get_ls_restore_handler())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("ls_restore_handler is null", KR(ret));
  } else {
    task.task_id_.init(primary_addr);
    task.type_ = restore::ObRestoreTaskType::STANDBY_RESTORE_TASK;
    task.src_info_ = primary_addr;
    if (OB_FAIL(ls_restore_handler->add_ls_restore_task(task))) {
      LOG_WARN("failed to add ls restore task", KR(ret));
    } else {
      LOG_INFO("scheduled standby restore task", K(primary_addr));
    }
  }
  return ret;
}

int ObService::set_server_id_(const int64_t server_id)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!is_valid_server_id(server_id))) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid server_id", KR(ret), K(server_id));
  } else if (is_valid_server_id(GCTX.get_server_id()) || is_valid_server_id(GCONF.observer_id)) {
    ret = OB_ERR_UNEXPECTED;
    uint64_t server_id_in_gconf = GCONF.observer_id;
    LOG_WARN("server_id is only expected to be set once", KR(ret),
             K(server_id), K(GCTX.get_server_id()), K(server_id_in_gconf));
  } else {
    (void) GCTX.set_server_id(server_id);
    GCONF.observer_id = server_id;
    if (OB_ISNULL(GCTX.config_mgr_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("GCTX.config_mgr_ is null", KR(ret));
    } else if (OB_FAIL(GCTX.config_mgr_->dump2file())) {
      LOG_WARN("fail to execute dump2file, this server cannot be added, "
          "please clear it and try again", KR(ret));
    }
  }
  return ret;
}

int ObService::check_server_empty(const obrpc::ObCheckServerEmptyArg &arg, obrpc::Bool &is_empty)
{
  int ret = OB_SUCCESS;
  obrpc::ObCheckServerEmptyResult result;
  if (OB_FAIL(check_server_empty_with_result(arg, result))) {
    LOG_WARN("failed to call check_server_empty_with_result", KR(ret));
  } else {
    is_empty = result.get_server_empty();
  }
  return ret;
}
int ObService::check_server_empty_with_result(const obrpc::ObCheckServerEmptyArg &arg, obrpc::ObCheckServerEmptyResult &result)
{
  int ret = OB_SUCCESS;
  uint64_t sys_data_version = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    bool server_empty = false;
    ObZone zone;
    if (OB_FAIL(check_server_empty(server_empty))) {
      LOG_WARN("check_server_empty failed", K(ret));
    } else if (OB_FAIL(zone.assign(GCONF.zone.str()))) {
      LOG_WARN("assign zone failed", KR(ret), K(GCONF.zone));
    } else if (OB_FAIL(result.init(server_empty, zone))) {
      LOG_WARN("failed to init ObCheckServerEmptyResult", KR(ret), K(server_empty), K(zone));
    }
    if (OB_FAIL(ret) || !server_empty) {
      // do_nothing
    } else if (ObCheckServerEmptyArg::BOOTSTRAP == arg.get_mode()) {
      // for rs_list nodes, set server_id for the first time here
      const uint64_t server_id = arg.get_server_id();
      if (OB_FAIL(set_server_id_(server_id))) {
        LOG_WARN("failed to set server_id", KR(ret), K(server_id));
      } else {
        GCTX.in_bootstrap_ = true;
      }
    }
  }
  return ret;
}

int ObService::get_server_resource_info(
    const obrpc::ObGetServerResourceInfoArg &arg,
    obrpc::ObGetServerResourceInfoResult &result)
{
  int ret = OB_SUCCESS;
  const ObAddr &my_addr = GCONF.self_addr_;
  share::ObServerResourceInfo resource_info;
  result.reset();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K(inited_));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(arg));
  } else if (OB_FAIL(get_server_resource_info(resource_info))) {
    LOG_WARN("fail to get server resource info", KR(ret));
  } else if (OB_FAIL(result.init(my_addr, resource_info))) {
    LOG_WARN("fail to init result", KR(ret), K(my_addr), K(resource_info));
  }
  FLOG_INFO("get server resource info", KR(ret), K(arg), K(result));
  return ret;
}

int ObService::get_server_resource_info(share::ObServerResourceInfo &resource_info)
{
  int ret = OB_SUCCESS;
  omt::ObTenantNodeBalancer::ServerResource svr_res_assigned;
  int64_t clog_in_use_size_byte = 0;
  int64_t clog_total_size_byte = 0;
  logservice::ObServerLogBlockMgr *log_block_mgr = GCTX.log_block_mgr_;
  resource_info.reset();
  int64_t reserved_size = 0;
#ifdef OB_BUILD_SHARED_STORAGE
  int64_t shared_storage_data_disk_in_use = 0;
#endif

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K(inited_));
  } else if (OB_ISNULL(log_block_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("log_block_mgr is null", KR(ret), K(GCTX.log_block_mgr_));
  } else if (OB_FAIL(omt::ObTenantNodeBalancer::get_instance().get_server_allocated_resource(svr_res_assigned))) {
    LOG_WARN("fail to get server allocated resource", KR(ret));
  } else if (OB_FAIL(log_block_mgr->get_disk_usage(clog_in_use_size_byte))) {
    LOG_WARN("Failed to get clog stat ", KR(ret));
  } else if (FALSE_IT(clog_total_size_byte = log_block_mgr->get_log_disk_size())) {
  } else if (OB_FAIL(SERVER_STORAGE_META_SERVICE.get_reserved_size(reserved_size))) {
    LOG_WARN("Failed to get reserved size ", KR(ret), K(reserved_size));
#ifdef OB_BUILD_SHARED_STORAGE
  } else if (GCTX.is_shared_storage_mode()
             && OB_FAIL(OB_SERVER_DISK_SPACE_MGR.get_used_disk_size(shared_storage_data_disk_in_use))) {
    LOG_WARN("Failed to get used_disk_size", KR(ret));
#endif
  } else {
    // cpu
    resource_info.cpu_ = get_cpu_count();
    resource_info.report_cpu_assigned_ = svr_res_assigned.min_cpu_;
    resource_info.report_cpu_max_assigned_ = svr_res_assigned.max_cpu_;
    // mem
    resource_info.report_mem_assigned_ = svr_res_assigned.memory_size_;
    resource_info.mem_in_use_ = 0;
    resource_info.mem_total_ = GMEMCONF.get_server_memory_avail();
    // log_disk
    resource_info.log_disk_total_ = clog_total_size_byte;
    resource_info.log_disk_in_use_ = clog_in_use_size_byte;
    resource_info.report_log_disk_assigned_ = svr_res_assigned.log_disk_size_;
    // data_disk
#ifdef OB_BUILD_SHARED_STORAGE
    if (GCTX.is_shared_storage_mode()) {
      // shared-storage mode
      resource_info.data_disk_total_ = OB_SERVER_DISK_SPACE_MGR.get_disk_size_capacity();
      resource_info.data_disk_in_use_ = shared_storage_data_disk_in_use;
      resource_info.report_data_disk_assigned_ = svr_res_assigned.data_disk_size_;
      resource_info.report_data_disk_suggested_size_ = OB_SERVER_DISK_SPACE_MGR.get_data_disk_suggested_size();
      resource_info.report_data_disk_suggested_operation_ = OB_SERVER_DISK_SPACE_MGR.get_data_disk_suggested_operation();
    } else
    // shared-nothing mode
#endif
    {
      resource_info.data_disk_total_
          = OB_STORAGE_OBJECT_MGR.get_max_macro_block_count(reserved_size) * OB_STORAGE_OBJECT_MGR.get_macro_block_size();
      resource_info.data_disk_in_use_
          = OB_STORAGE_OBJECT_MGR.get_used_macro_block_count() * OB_STORAGE_OBJECT_MGR.get_macro_block_size();
      resource_info.report_data_disk_assigned_ = ObUnitResource::DEFAULT_DATA_DISK_SIZE;
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
int ObService::get_partition_count(obrpc::ObGetPartitionCountResult &result)
{
  UNUSEDx(result);
  int ret = OB_NOT_SUPPORTED;
  // result.reset();

  // if (!inited_) {
  //   ret = OB_NOT_INIT;
  //   LOG_WARN("not inited", K(ret));
  // } else if (OB_FAIL(gctx_.par_ser_->get_partition_count(result.partition_count_))) {
  //   LOG_WARN("failed to get partition count", K(ret));
  // }
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
    uint64_t server_id_in_GCONF = GCONF.observer_id;
    if (is_empty) {
      if (is_valid_server_id(GCTX.get_server_id()) || is_valid_server_id(server_id_in_GCONF)) {
        is_empty = false;
        FLOG_WARN("[CHECK_SERVER_EMPTY] server_id exists", K(GCTX.get_server_id()), K(server_id_in_GCONF));
      }
    }
    if (is_empty) {
      if (!OBSERVER.is_log_dir_empty()) {
        FLOG_WARN("[CHECK_SERVER_EMPTY] log dir is not empty");
        is_empty = false;
      }
    }
    if (is_empty) {
      if (ODV_MGR.get_file_exists_when_loading()) {
        // ignore ret
        FLOG_WARN("[CHECK_SERVER_EMPTY] data_version file exists");
        is_empty = false;
      }
    }
  }
  return ret;
}

int ObService::set_ds_action(const obrpc::ObDebugSyncActionArg &arg)
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

// get tenant's refreshed schema version in new mode
int ObService::get_tenant_refreshed_schema_version(
    const obrpc::ObGetTenantSchemaVersionArg &arg,
    obrpc::ObGetTenantSchemaVersionResult &result)
{
  int ret = OB_SUCCESS;
  result.schema_version_ = OB_INVALID_VERSION;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret));
  } else if (OB_ISNULL(gctx_.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret));
  } else if (OB_FAIL(gctx_.schema_service_->get_tenant_refreshed_schema_version(
             arg.tenant_id_, result.schema_version_, false/*core_version*/))) {
    LOG_WARN("fail to get tenant refreshed schema version", K(ret), K(arg));
  }
  return ret;
}

int ObService::sync_partition_table(const obrpc::Int64 &arg)
{
  return OB_NOT_SUPPORTED;
}

int ObService::set_tracepoint(const obrpc::ObAdminSetTPArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    EventItem item;
    item.error_code_ = arg.error_code_;
    item.occur_ = arg.occur_;
    item.trigger_freq_ = arg.trigger_freq_;
    item.cond_ = arg.cond_;
    if (arg.event_name_.length() > 0) {
      ObSqlString str;
      if (OB_FAIL(str.assign(arg.event_name_))) {
        LOG_WARN("string assign failed", K(ret));
      } else if (OB_FAIL(EventTable::instance().set_event(str.ptr(), item))) {
        LOG_WARN("Failed to set tracepoint event, tp_name does not exist.", K(ret), K(arg.event_name_));
      }
    } else if (OB_FAIL(EventTable::instance().set_event(arg.event_no_, item))) {
      LOG_WARN("Failed to set tracepoint event, tp_no does not exist.", K(ret), K(arg.event_no_));
    }
    LOG_INFO("set event", K(arg));
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

int ObService::stop_partition_write(const obrpc::Int64 &switchover_timestamp, obrpc::Int64 &result)
{
  //TODO for switchover
  int ret = OB_SUCCESS;
  result = switchover_timestamp;
  return ret;
}

int ObService::check_partition_log(const obrpc::Int64 &switchover_timestamp, obrpc::Int64 &result)
{
  UNUSEDx(switchover_timestamp, result);
  // Check that the log of all replicas in local have reached synchronization status
  // The primary has stopped writing
  int ret = OB_NOT_SUPPORTED;

  // ObTenantDagScheduler is at tenant level now, check all tenants task
  // int64_t balance_task = 0;
  // omt::TenantIdList all_tenants;
  // all_tenants.set_label(ObModIds::OB_TENANT_ID_LIST);
  // if (OB_ISNULL(GCTX.omt_)) {
  //   ret = OB_ERR_UNEXPECTED;
  //   LOG_WARN("failed to get multi tenant from GCTX", K(ret));
  // } else {
  //   GCTX.omt_->get_tenant_ids(all_tenants);
  //   for (int64_t i = 0; OB_SUCC(ret) && i < all_tenants.size(); ++i) {
  //     uint64_t tenant_id = all_tenants[i];
  //     if (!is_virtual_tenant_id(tenant_id)) { // skip virtual tenant
  //       if (OB_SUCC(guard.switch_to(tenant_id))) {
  //         ObTenantDagScheduler *scheduler = nullptr;
  //         if (OB_ISNULL(scheduler = MTL(ObTenantDagScheduler *))) {
  //           ret = OB_ERR_UNEXPECTED;
  //           FLOG_WARN("MTL ObTenantDagScheduler is NULL", K(ret), K(scheduler), K(tenant_id));
  //         } else {
  //           balance_task += scheduler->get_dag_count(ObDagType::DAG_TYPE_MIGRATE);
  //         }
  //       }
  //     }
  //   }
  // }

  // if (OB_SUCC(ret)) {
  //   if (balance_task > 0) {
  //     ret = OB_EAGAIN;
  //     result = switchover_timestamp;
  //     LOG_INFO("observer already has task to do", K(switchover_timestamp), K(balance_task));
  //   } else if (OB_FAIL(gctx_.par_ser_->check_all_partition_sync_state(switchover_timestamp))) {
  //     LOG_WARN("fail to check_all_partition_sync_state", K(ret));
  //   } else {
  //     result = switchover_timestamp;
  //   }
  // }
  return ret;
}

int ObService::estimate_partition_rows(const obrpc::ObEstPartArg &arg,
                                       obrpc::ObEstPartRes &res) const
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("receive estimate rows request", K(arg));
  if (!inited_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("service is not inited", K(ret));
  } else if (OB_FAIL(sql::ObStorageEstimator::estimate_row_count(arg, res))) {
    LOG_WARN("failed to estimate partition rowcount", K(ret));
  }
  return ret;
}

int ObService::get_wrs_info(const obrpc::ObGetWRSArg &arg,
                            obrpc::ObGetWRSResult &result)
{
  UNUSEDx(arg, result);
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObService::refresh_memory_stat()
{
  return ObMemoryDump::get_instance().generate_mod_stat_task();
}

int ObService::wash_memory_fragmentation()
{
  ObMallocAllocator::get_instance()->sync_wash();
  return OB_SUCCESS;
}

int ObService::build_split_tablet_data_start_request(const obrpc::ObTabletSplitStartArg &arg,  obrpc::ObTabletSplitStartResult &res)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.split_info_array_.count(); i++) {
      share::SCN start_scn;
      const ObTabletSplitArg &each_arg = arg.split_info_array_.at(i);
      if (OB_FAIL(ObTabletLobSplitUtil::process_write_split_start_log_request(each_arg, start_scn))) {
        LOG_WARN("process write split start log failed", K(ret), K(tmp_ret), K(arg));
      } else if (0 == i) {
        if (!start_scn.is_valid_and_not_min()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("unexpected start scn", K(ret), K(start_scn));
        } else {
          res.min_split_start_scn_ = start_scn;
        }
      }
      if (OB_TMP_FAIL(res.ret_codes_.push_back(ret))) {
        LOG_WARN("push back result failed", K(ret), K(tmp_ret));
      }
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  LOG_INFO("process write split start log finished", K(ret), K(arg));
  return ret;
}

int ObService::build_split_tablet_data_finish_request(const obrpc::ObTabletSplitFinishArg &arg, obrpc::ObTabletSplitFinishResult &res)
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.split_info_array_.count(); i++) {
      ObTabletSplitFinishResult unused_res;
      const ObTabletSplitArg &each_arg = arg.split_info_array_.at(i);
      if (OB_FAIL(ObTabletLobSplitUtil::process_tablet_split_request(
          each_arg.lob_col_idxs_.count() > 0/*is_lob_tablet*/,
          false/*is_start_request*/,
          static_cast<const void *>(&each_arg),
          static_cast<void *>(&unused_res)))) {
        LOG_WARN("process split finish request failed", K(ret), K(arg));
      }
      if (OB_TMP_FAIL(res.ret_codes_.push_back(ret))) {
        LOG_WARN("push back failed", K(ret), K(tmp_ret));
      }
      ret = OB_SUCC(ret) ? tmp_ret : ret;
    }
  }
  LOG_INFO("process split finish request succ", K(ret), K(arg));
  return ret;
}

int ObService::fetch_split_tablet_info(const ObFetchSplitTabletInfoArg &arg,
                                       ObFetchSplitTabletInfoRes &res,
                                       const int64_t abs_timeout_us)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    MTL_SWITCH(arg.tenant_id_) {
      ObLSService *ls_service = MTL(ObLSService *);
      ObLSHandle ls_handle;
      ObLS *ls = nullptr;
      ObRole role = INVALID_ROLE;
      if (OB_ISNULL(ls_service)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ls_service or log_service", K(ret));
      } else if (OB_FAIL(ls_service->get_ls(arg.ls_id_, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
        LOG_WARN("get ls failed", K(ret), K(arg));
      } else if (OB_ISNULL(ls = ls_handle.get_ls())) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("invalid ls", K(ret), K(arg.ls_id_));
      } else if (OB_FAIL(ls->get_ls_role(role))) {
        LOG_WARN("get role failed", K(ret), K(MTL_ID()), K(arg.ls_id_));
      } else if (OB_UNLIKELY(ObRole::LEADER != role)) {
        ret = OB_NOT_MASTER;
        LOG_WARN("ls not leader", K(ret), K(MTL_ID()), K(arg.ls_id_));
      } else {
        for (int64_t i = 0; OB_SUCC(ret) && i < arg.tablet_ids_.count(); i++) {
          const ObTabletID &tablet_id = arg.tablet_ids_.at(i);
          ObTabletHandle tablet_handle;
          ObTabletCreateDeleteMdsUserData user_data;
          if (OB_FAIL(ls->get_tablet(tablet_id, tablet_handle))) {
            LOG_WARN("failed to get tablet", K(ret), K(tablet_id));
          } else if (OB_FAIL(tablet_handle.get_obj()->ObITabletMdsInterface::get_tablet_status(
                  share::SCN::max_scn(), user_data, ObTabletCommon::DEFAULT_GET_TABLET_DURATION_US))) {
            LOG_WARN("failed to get tablet status", K(ret), K(arg.ls_id_), K(tablet_id));
          } else if (OB_FAIL(res.create_commit_versions_.push_back(user_data.create_commit_version_))) {
            LOG_WARN("failed to push back", K(ret));
          } else if (OB_FAIL(res.tablet_sizes_.push_back(tablet_handle.get_obj()->get_tablet_meta().space_usage_.all_sstable_data_required_size_))) {
            LOG_WARN("failed to push back", K(ret));
          }
        }
      }
    }
  }
  return ret;
}

int ObService::build_ddl_single_replica_request(const ObDDLBuildSingleReplicaRequestArg &arg,
                                                ObDDLBuildSingleReplicaRequestResult &res)
{
  int ret = OB_SUCCESS;
  ObTenantDagScheduler *dag_scheduler = nullptr;
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_ISNULL(dag_scheduler = MTL(ObTenantDagScheduler *))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("dag scheduler is null", K(ret));
  } else {
    if (share::is_tablet_split(ObDDLType(arg.ddl_type_))) {
      if (OB_FAIL(ObTabletLobSplitUtil::process_tablet_split_request(
            arg.lob_col_idxs_.count() > 0/*is_lob_tablet*/,
            true/*is_start_request*/,
            static_cast<const void *>(&arg),
            static_cast<void *>(&res)))) {
        LOG_WARN("process split start request failed", K(ret), K(arg));
      }
    } else if (is_complement_data_relying_on_dag(ObDDLType(arg.ddl_type_))) {
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
      ObTenantDagScheduler *dag_scheduler = nullptr;
      ObDeleteLobMetaRowDag *dag = nullptr;
      if (OB_ISNULL(dag_scheduler = MTL(ObTenantDagScheduler *))) {
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
  LOG_INFO("receive build single replica request", K(ret), K(arg));
  return ret;
}

int ObService::check_and_cancel_ddl_complement_data_dag(const ObDDLBuildSingleReplicaRequestArg &arg, bool &is_dag_exist)
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
    ObTenantDagScheduler *dag_scheduler = nullptr;
    ObComplementDataDag *dag = nullptr;
    if (OB_ISNULL(dag_scheduler = MTL(ObTenantDagScheduler *))) {
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

int ObService::check_and_cancel_delete_lob_meta_row_dag(const obrpc::ObDDLBuildSingleReplicaRequestArg &arg, bool &is_dag_exist)
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
    ObTenantDagScheduler *dag_scheduler = nullptr;
    ObComplementDataDag *dag = nullptr;
    if (OB_ISNULL(dag_scheduler = MTL(ObTenantDagScheduler *))) {
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
    const int64_t tenant_id,
    const ObTabletID &tablet_id,
    storage::ObLS *ls,
    ObTabletReplica &tablet_replica,
    share::ObTabletReplicaChecksumItem &tablet_checksum,
    const bool need_checksum)
{
  ObLSHandle ls_handle;
  ObTabletHandle tablet_handle;
  int ret = OB_SUCCESS;
  bool need_wait_for_report = false;
  ObTablet *tablet = nullptr;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret));
  } else if (!tablet_id.is_valid()
             || OB_INVALID_TENANT_ID == tenant_id
             || OB_ISNULL(ls)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument or nullptr", KR(ret), K(tablet_id), K(tenant_id));
  } else if (OB_ISNULL(ls->get_tablet_svr())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_tablet_svr is null", KR(ret), K(tenant_id), K(tablet_id));
  } else if (OB_FAIL(ls->get_tablet_svr()->get_tablet(
      tablet_id,
      tablet_handle,
      0,
      ObMDSGetTabletMode::READ_WITHOUT_CHECK))) {
    if (OB_TABLET_NOT_EXIST != ret) {
      LOG_WARN("get tablet failed", KR(ret), K(tenant_id), K(tablet_id));
    }
  } else if (OB_UNLIKELY(!tablet_handle.is_valid() || OB_ISNULL(tablet = tablet_handle.get_obj()))) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get invalid tablet handle", KR(ret), K(tenant_id), K(tablet_id), K(tablet_handle), KPC(tablet));
  } else if (OB_ISNULL(gctx_.config_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("gctx_.config_ is null", KR(ret), K(tenant_id), K(tablet_id));
  } else if (OB_FAIL(ObCSReplicaUtil::check_need_wait_for_report(*ls, *tablet, need_wait_for_report))) {
    LOG_WARN("fail to check need wait report", K(ret), KPC(ls), KPC(tablet));
  } else if (need_wait_for_report) {
    ret = OB_EAGAIN;
    LOG_WARN("need wait report for cs replica", K(ret), K(tablet_id));
  } else if (OB_FAIL(tablet->get_tablet_report_info(
     gctx_.self_addr(), tablet_replica, tablet_checksum, need_checksum, ls->is_cs_replica()))) {
    LOG_WARN("fail to get tablet report info from tablet", KR(ret), K(tenant_id),
      "ls_id", ls->get_ls_id(), "is_cs_replica", ls->is_cs_replica(), K(tablet_id));
  }
  return ret;
}

int ObService::fill_tablet_report_info(
    const uint64_t tenant_id,
    const ObLSID &ls_id,
    const ObTabletID &tablet_id,
    ObTabletReplica &tablet_replica,
    share::ObTabletReplicaChecksumItem &tablet_checksum,
    const bool need_checksum)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("service not inited", KR(ret));
  } else if (!tablet_id.is_valid() || !ls_id.is_valid() || OB_INVALID_TENANT_ID == tenant_id) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", KR(ret), K(tablet_id), K(ls_id), K(tenant_id));
  } else {
    MTL_SWITCH(tenant_id) {
      ObSharedGuard<ObLSIterator> ls_iter;
      ObTabletHandle tablet_handle;
      ObLSHandle ls_handle;
      storage::ObLS *ls = nullptr;
      ObLSService* ls_svr = nullptr;
      if (OB_ISNULL(ls_svr = MTL(ObLSService*))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("MTL ObLSService is null", KR(ret), K(tenant_id));
      } else if (OB_FAIL(ls_svr->get_ls(ls_id, ls_handle, ObLSGetMod::OBSERVER_MOD))) {
        if (OB_LS_NOT_EXIST != ret) {
          LOG_WARN("fail to get log_stream's ls_handle", KR(ret), K(tenant_id), K(ls_id));
        } else {
          LOG_TRACE("log stream not exist in this tenant", KR(ret), K(tenant_id), K(ls_id));
        }
      } else if (FALSE_IT(ls = ls_handle.get_ls())) {
      } else if (OB_FAIL(inner_fill_tablet_info_(tenant_id,
                                                 tablet_id,
                                                 ls,
                                                 tablet_replica,
                                                 tablet_checksum,
                                                 need_checksum))) {
        if (OB_TABLET_NOT_EXIST != ret) {
          LOG_WARN("fail to inner fill tenant's tablet replica", KR(ret),
                    K(tenant_id), K(tablet_id), K(ls), K(tablet_replica), K(tablet_checksum), K(need_checksum));
        } else {
          LOG_TRACE("tablet not exist in this log stream", KR(ret),
                    K(tenant_id), K(tablet_id), K(ls), K(tablet_replica), K(tablet_checksum), K(need_checksum));
        }
      }
    }
  }
  return ret;
}

int ObService::report_backup_over(const obrpc::ObBackupTaskRes &res)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObService::report_backup_clean_over(const obrpc::ObBackupTaskRes &res)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObService::check_backup_dest_connectivity(const obrpc::ObCheckBackupConnectivityArg &arg)
{
  int ret = OB_SUCCESS;
  share::ObBackupDestCheck backup_check;
  share::ObBackupPath path;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObService not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(path.init(arg.check_path_))) {
    LOG_WARN("failed to init path", K(ret), K(arg));
  } else if (OB_FAIL(backup_check.check_backup_dest_connectivity(arg.tenant_id_, arg.backup_path_, path))) {
    LOG_WARN("failed to check backup dest connectivity", K(ret), K(arg));
  }
  return ret;
}

int ObService::estimate_tablet_block_count(const obrpc::ObEstBlockArg &arg,
                                           obrpc::ObEstBlockRes &res) const
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("receive estimate tablet block count request", K(arg));
  if (!inited_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("service is not inited", K(ret));
  } else if (OB_FAIL(sql::ObStorageEstimator::estimate_block_count_and_row_count(arg, res))) {
    LOG_WARN("failed to estimate block count and row count", K(ret));
  }
  return ret;
}

int ObService::estimate_skip_rate(const obrpc::ObEstSkipRateArg &arg,
                                  obrpc::ObEstSkipRateRes &res) const
{
  int ret = OB_SUCCESS;
  LOG_DEBUG("receive estimate tablet skip rate request", K(arg));
  if (!inited_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("service is not inited", K(ret));
  } else if (OB_FAIL(sql::ObStorageEstimator::estimate_skip_rate(arg, res))) {
    LOG_WARN("failed to estimate skip rate", K(ret));
  }
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_GET_LS_SYNC_SCN_ERROR);
ERRSIM_POINT_DEF(ERRSIM_GET_SYS_LS_SYNC_SCN_ERROR);
int ObService::get_ls_sync_scn(
    const ObGetLSSyncScnArg &arg,
    ObGetLSSyncScnRes &result)
{
  int ret = OB_SUCCESS;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  ObLSService *ls_svr = nullptr;
  SCN cur_sync_scn = SCN::min_scn();
  SCN cur_restore_source_max_scn = SCN::min_scn();
  bool restore_to_newest = false;
  LOG_INFO("start get_ls_sync_scn", K(arg));

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invaild", KR(ret), K(arg));
  } else if (arg.get_tenant_id() != MTL_ID() && OB_FAIL(guard.switch_to(arg.get_tenant_id()))) {
    LOG_WARN("switch tenant failed", KR(ret), K(arg));
  }

  if (OB_SUCC(ret)) {
    ls_svr = MTL(ObLSService*);
    logservice::ObLogService *log_ls_svr = MTL(logservice::ObLogService*);
    ObLS *ls = nullptr;
    ObLSHandle handle;
    logservice::ObLogHandler *log_handler = NULL;
    logservice::ObLogRestoreHandler *restore_handler = NULL;
    ObLSID ls_id = arg.get_ls_id();
    common::ObRole role;
    int64_t first_leader_epoch = 0;
    int64_t second_leader_epoch = 0;
    if (OB_ISNULL(ls_svr) || OB_ISNULL(log_ls_svr)) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(ERROR, "should not be null", KR(ret), KP(ls_svr), KP(log_ls_svr));
    } else if (OB_FAIL(log_ls_svr->get_palf_role(ls_id, role, first_leader_epoch))) {
      COMMON_LOG(WARN, "failed to get palf role", KR(ret), K(ls_id));
    } else if (!is_strong_leader(role)) {
      ret = OB_NOT_MASTER;
      LOG_WARN("ls on this server is not master", KR(ret), K(ls_id));
    } else if (OB_FAIL(ls_svr->get_ls(ls_id, handle, ObLSGetMod::OBSERVER_MOD))) {
      COMMON_LOG(WARN, "get ls failed", KR(ret), K(ls_id));
    } else if (OB_ISNULL(ls = handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(ERROR, "ls should not be null", KR(ret), K(ls_id));
    } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log_handler is null", KR(ret), K(ls_id), KP(ls));
    } else if (OB_ISNULL(restore_handler = ls->get_log_restore_handler())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("get restore_handler failed", KR(ret));
    // scn get order, sync_scn from leader before archive_scn
    } else if (OB_FAIL(log_handler->get_end_scn(cur_sync_scn))) {
      LOG_WARN("failed to get ls cur_sync_scn", KR(ret), K(ls_id), KPC(ls));
    } else if (arg.check_sync_to_latest()
               && OB_FAIL(restore_handler->check_restore_to_newest(cur_sync_scn, cur_restore_source_max_scn))) {
      LOG_WARN("failed to check_restore_to_newest", KR(ret), K(arg), KPC(ls));
    } else if (OB_FAIL(result.init(arg.get_tenant_id(), ls_id, cur_sync_scn, cur_restore_source_max_scn))) {
      LOG_WARN("failed to init res", KR(ret), K(arg.get_tenant_id()), K(ls_id), K(cur_sync_scn),
                                     K(cur_restore_source_max_scn));
    } else if (OB_FAIL(log_ls_svr->get_palf_role(ls_id, role, second_leader_epoch))) {
      COMMON_LOG(WARN, "failed to get palf role", KR(ret), K(ls_id));
    } else if (first_leader_epoch != second_leader_epoch || !is_strong_leader(role)) {
      ret = OB_NOT_MASTER;
      LOG_WARN("the ls not master", KR(ret), K(ls_id), K(first_leader_epoch),
          K(second_leader_epoch), K(role));
    }
    if (OB_SUCC(ret) && ERRSIM_GET_LS_SYNC_SCN_ERROR) {
      cur_sync_scn = ls_id.is_sys_ls() ? cur_sync_scn : SCN::minus(cur_sync_scn, 1000);
      ret = result.init(arg.get_tenant_id(), ls_id, cur_sync_scn, cur_restore_source_max_scn);
      LOG_WARN("user ls errsim enabled", KR(ret), K(arg.get_tenant_id()), K(ls_id), K(cur_sync_scn), K(cur_restore_source_max_scn));
    }
    if (OB_SUCC(ret) && ERRSIM_GET_SYS_LS_SYNC_SCN_ERROR) {
      cur_sync_scn = ls_id.is_sys_ls() ? SCN::minus(cur_sync_scn, 1000) : cur_sync_scn;
      ret = result.init(arg.get_tenant_id(), ls_id, cur_sync_scn, cur_restore_source_max_scn);
      LOG_WARN("sys ls errsim enabled", KR(ret), K(arg.get_tenant_id()), K(ls_id), K(cur_sync_scn), K(cur_restore_source_max_scn));
    }
  }
  LOG_INFO("finish get_ls_sync_scn", KR(ret), K(cur_sync_scn), K(cur_restore_source_max_scn), K(arg), K(result));
  return ret;
}

int ObService::force_set_ls_as_single_replica(
    const ObForceSetLSAsSingleReplicaArg &arg)
{
  int ret = OB_SUCCESS;
  MAKE_TENANT_SWITCH_SCOPE_GUARD(guard);
  ObLSService *ls_svr = nullptr;
  LOG_INFO("force_set_ls_as_single_replica", K(arg));

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invaild", KR(ret), K(arg));
  } else if (arg.get_tenant_id() != MTL_ID() && OB_FAIL(guard.switch_to(arg.get_tenant_id()))) {
    LOG_WARN("switch tenant failed", KR(ret), K(arg));
  }

  if (OB_SUCC(ret)) {
    ls_svr = MTL(ObLSService*);
    logservice::ObLogService *log_ls_svr = MTL(logservice::ObLogService*);
    ObLS *ls = nullptr;
    ObLSHandle handle;
    logservice::ObLogHandler *log_handler = NULL;
    ObLSID ls_id = arg.get_ls_id();
    if (OB_ISNULL(ls_svr) || OB_ISNULL(log_ls_svr)) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(ERROR, "should not be null", KR(ret), KP(ls_svr), KP(log_ls_svr));
    } else if (OB_FAIL(ls_svr->get_ls(ls_id, handle, ObLSGetMod::OBSERVER_MOD))) {
      COMMON_LOG(WARN, "get ls failed", KR(ret), K(ls_id));
    } else if (OB_ISNULL(ls = handle.get_ls())) {
      ret = OB_ERR_UNEXPECTED;
      COMMON_LOG(ERROR, "ls should not be null", KR(ret), K(ls_id));
    } else if (OB_ISNULL(log_handler = ls->get_log_handler())) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("log_handler is null", KR(ret), K(ls_id), KP(ls));
    } else if (OB_FAIL(log_handler->force_set_as_single_replica())) {
      LOG_WARN("failed to force_set_as_single_replica", KR(ret), K(ls_id), KPC(ls));
    }
  }
  LOG_INFO("finish force_set_ls_as_single_replica", KR(ret), K(arg));
  return ret;
}

int ObService::force_set_server_list(const obrpc::ObForceSetServerListArg &arg, obrpc::ObForceSetServerListResult &result)
{
  int ret = OB_SUCCESS;
  LOG_INFO("force_set_server_list", K(arg));

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ob_service is not inited", KR(ret));
  } else if (OB_ISNULL(GCTX.omt_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("GCTX.omt_ is null", KR(ret), K(arg));
  } else {
    common::ObArray<uint64_t> tenant_ids;
    (void) GCTX.omt_->get_mtl_tenant_ids(tenant_ids);
    const int64_t new_membership_timestamp = ObTimeUtility::current_time();
    bool all_succeed = true;
    for (int64_t i = 0; OB_SUCC(ret) && i < tenant_ids.size(); ++i) {
      const int64_t tenant_id = tenant_ids[i];
      COMMON_LOG(INFO, "start to excute force_set_server_list", K(tenant_id));
      MTL_SWITCH(tenant_id) {
        ObLSService *ls_svr = MTL(ObLSService*);
        logservice::ObLogService *log_service = MTL(logservice::ObLogService*);
        ObSharedGuard<storage::ObLSIterator> ls_iter_guard;
        ObForceSetServerListResult::ResultInfo result_info(tenant_id);

        if (OB_ISNULL(ls_svr) || OB_ISNULL(log_service)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ptr is null", KR(ret), K(tenant_id), KP(ls_svr), KP(log_service));
        } else if (OB_FAIL(ls_svr->get_ls_iter(ls_iter_guard, ObLSGetMod::OBSERVER_MOD))) {
          LOG_WARN("fail to get ls iter guard", KR(ret), K(tenant_id));
        }

        if (OB_FAIL(ret)) {
          all_succeed = false;
          COMMON_LOG(WARN, "force_set_server_list with current tenant failed", KR(ret), K(tenant_id));
        }

        while (OB_SUCC(ret)) {
          COMMON_LOG(INFO, "start to iterate every log stream of tenant", K(tenant_id));
          ObLS *ls = nullptr;
          logservice::ObLogHandler *log_handler = NULL;
          if (OB_FAIL(ls_iter_guard->get_next(ls))) {
            if (OB_ITER_END != ret) {
              LOG_WARN("fail to get next ls", KR(ret), K(tenant_id));
            }
          } else if (OB_ISNULL(ls)){
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("ls is nullptr", KR(ret), K(tenant_id));
          } else {
            common::ObMemberList old_member_list;
            int64_t old_replica_num = 0;
            ObLSID ls_id = ls->get_ls_id();

            if (OB_ISNULL(log_handler = ls->get_log_handler())) {
              ret = OB_ERR_UNEXPECTED;
              COMMON_LOG(ERROR, "log_handler is null", KR(ret), K(tenant_id), K(ls_id), KP(ls));
            } else if (OB_FAIL(log_handler->get_paxos_member_list(old_member_list, old_replica_num))) {
              LOG_WARN("get old_member_list failed", KR(ret), K(tenant_id), K(ls_id), KP(ls));
            } else {
              common::ObMemberList new_member_list;
              // new_member_list is the intersection of args.server_list_ and old_member_list
              for (int64_t j = 0; OB_SUCC(ret) && j < arg.server_list_.size(); ++j) {
                const common::ObAddr &server = arg.server_list_[j];
                if (!old_member_list.contains(server)) {
                } else if (OB_FAIL(new_member_list.add_member(ObMember(server, new_membership_timestamp)))){
                  LOG_WARN("new_member_list add_member failed", K(ret), K(server));
                }
              }

              if (OB_FAIL(ret)) {
              } else if (arg.replica_num_ != new_member_list.get_member_number()) {
                ret = OB_STATE_NOT_MATCH;
                LOG_WARN("new_member_list number does not equal to arg.replica_num", K(ret), K(arg), K(new_member_list.get_member_number()));
              } else if (OB_FAIL(log_handler->force_set_member_list(new_member_list, arg.replica_num_))) {
                LOG_WARN("force_set_server_list failed", KR(ret), K(arg), K(tenant_id), K(ls_id));
              } else {
                COMMON_LOG(INFO, "execute force_set_server_list successfully with "
                           "current tenant and ls", K(arg), K(tenant_id), K(ls_id));
              }
            }

            int tmp_ret = OB_SUCCESS;
            if (OB_TMP_FAIL(result_info.add_ls_info(ls_id, ret))) {
              LOG_WARN("add_result_info failed", K(tmp_ret), K(ls_id), "actual ret", ret);
            }

            if (OB_FAIL(ret)) {
              COMMON_LOG(WARN, "failed to execute force_set_server_list with "
                         "current tenant and ls", KR(ret), K(arg), K(tenant_id), K(ls_id));
              ret = OB_SUCCESS; // ignore failed return code, keep executing next ls
            }
          } // end if
        } // end while

        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(result.result_list_.push_back(result_info))) {
          LOG_WARN("result_list_ push_back failed", K(tmp_ret), K(tenant_id), K(result_info));
        }
        if (0 != result_info.failed_ls_info_.size()) {
          all_succeed = false;
        }
        ret = OB_SUCCESS; // ignore failed return code, keep executing next tenant
      } // MTL_SWITCH end
    } // for end

    if (all_succeed) {
      result.ret_ = OB_SUCCESS;
    } else {
      result.ret_ = OB_PARTIAL_FAILED;
    }
  }
  return ret;
}

int ObService::init_tenant_config(
    const obrpc::ObInitTenantConfigArg &arg,
    obrpc::ObInitTenantConfigRes &result)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("service is not inited", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", KR(ret), K(arg));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.get_tenant_configs().count(); i++) {
      const ObTenantConfigArg &config = arg.get_tenant_configs().at(i);
      if (OB_FAIL(GCTX.config_mgr_->init_tenant_config(config)))  {
        LOG_WARN("fail to init tenant config", KR(ret), K(config));
      }
    } // end for
  }
  (void) result.set_ret(ret);
  FLOG_INFO("init tenant config", KR(ret), K(arg));
  // use result to pass ret
  return OB_SUCCESS;
}

int ObService::change_external_storage_dest(obrpc::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (3 != arg.items_.count()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    const uint64_t tenant_id = gen_user_tenant_id(arg.items_.at(0).exec_tenant_id_);
    const common::ObFixedLengthString<common::OB_MAX_CONFIG_VALUE_LEN> &path = arg.items_.at(0).value_;
    const common::ObFixedLengthString<common::OB_MAX_CONFIG_VALUE_LEN> &access_info = arg.items_.at(1).value_;
    const common::ObFixedLengthString<common::OB_MAX_CONFIG_VALUE_LEN> &attribute = arg.items_.at(2).value_;

    ChangeExternalStorageDestMgr change_mgr;
    const bool has_access_info = !access_info.is_empty();
    const bool has_attribute = !attribute.is_empty();
    ObBackupPathString backup_path;
    ObBackupDestAttribute access_info_option;
    ObBackupDestAttribute attribute_option;
    ObMySQLTransaction trans;

    if (OB_ISNULL(GCTX.sql_proxy_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("sql proxy is null", K(ret), K(tenant_id));
    } else if (!is_user_tenant(tenant_id)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("The instruction is only supported by user tenant.", K(ret));
      LOG_USER_ERROR(OB_NOT_SUPPORTED, " using the syntax under sys tenant or meta tenant is");
    } else if (OB_FAIL(change_mgr.init(tenant_id, path, *GCTX.sql_proxy_))) {
      LOG_WARN("failed to init change_external_storage_dest_mgr", K(ret));
    }

    if (OB_SUCC(ret) && has_access_info) {
      if (OB_FAIL(ObBackupDestAttributeParser::parse_access_info(access_info.str(), access_info_option))) {
        LOG_WARN("failed to parse attribute", K(ret), K(access_info));
      } else if (OB_FAIL(change_mgr.update_and_validate_authorization(
                            access_info_option.access_id_, access_info_option.access_key_))) {
        LOG_WARN("failed to reset access id and access key", K(ret), K(access_info_option));
      }
    }
    if (OB_SUCC(ret) && has_attribute) {
      if (OB_FAIL(ObBackupDestAttributeParser::parse(attribute.str(), attribute_option))) {
        LOG_WARN("failed to parse attribute", K(ret), K(attribute));
      }
    }

    if (FAILEDx(trans.start(GCTX.sql_proxy_, gen_meta_tenant_id(tenant_id)))) {
      LOG_WARN("failed to start trans", K(ret), K(tenant_id));
    } else {
      if (has_access_info && OB_FAIL(change_mgr.update_inner_table_authorization(trans))) {
        LOG_WARN("failed to update backup authorization", K(ret), K(tenant_id));
      }

      if (OB_SUCC(ret) && has_attribute) {
        if (FAILEDx(ObBackupStorageInfoOperator::update_backup_dest_attribute(
            trans, tenant_id, change_mgr.backup_dest_, attribute_option.max_iops_, attribute_option.max_bandwidth_))) {
          LOG_WARN("failed to update backup dest attribute", K(ret), K(tenant_id));
        } else {
          LOG_INFO("admin change external storage dest", K(arg));
        }
      }
      if (trans.is_started()) {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(trans.end(OB_SUCC(ret)))) {
          LOG_WARN("trans end failed", "is_commit", OB_SUCCESS == ret, K(tmp_ret));
          ret = COVER_SUCC(tmp_ret);
        }
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "change_external_storage_dest", K(ret), K(arg));
  return ret;
}

}// end namespace observer
}// end namespace oceanbase
