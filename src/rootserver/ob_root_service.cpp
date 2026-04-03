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

#define USING_LOG_PREFIX RS

#include "ob_root_service.h"
#include "observer/ob_server.h"



#include "share/ob_global_stat_proxy.h"
#include "share/ob_index_builder_util.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "share/deadlock/ob_deadlock_inner_table_service.h"
#include "share/backup/ob_backup_config.h"
#include "share/scheduler/ob_partition_auto_split_helper.h"

#include "sql/engine/cmd/ob_user_cmd_executor.h"
#include "src/sql/engine/px/ob_dfo.h"
#include "observer/dbms_job/ob_dbms_job_master.h"

#include "rootserver/ob_bootstrap.h"
#include "rootserver/ob_partition_exchange.h"
#include "rootserver/ob_schema2ddl_sql.h"
#include "rootserver/ob_index_builder.h"
#include "rootserver/ob_mlog_builder.h"
#include "rootserver/ob_ddl_sql_generator.h"
#include "rootserver/ddl_task/ob_ddl_task.h"
#include "rootserver/ddl_task/ob_constraint_task.h"
#include "rootserver/ob_rs_job_table_operator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/ob_cluster_event.h"        // CLUSTER_EVENT_ADD_CONTROL
#include "observer/omt/ob_tenant_timezone_mgr.h"

#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_ddl_common.h" // for ObDDLUtil
#include "share/ob_cluster_event_history_table_operator.h"//CLUSTER_EVENT_INSTANCE
#include "rootserver/backup/ob_backup_proxy.h" //ObBackupServiceProxy
#include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "observer/ob_sys_tenant_load_sys_package_task.h"

#include "parallel_ddl/ob_create_table_helper.h" // ObCreateTableHelper
#include "parallel_ddl/ob_create_view_helper.h"  // ObCreateViewHelper
#include "parallel_ddl/ob_set_comment_helper.h" //ObCommentHelper
#include "parallel_ddl/ob_create_index_helper.h" // ObCreateIndexHelper
#include "parallel_ddl/ob_update_index_status_helper.h" // ObUpdateIndexStatusHelper
#include "parallel_ddl/ob_htable_ddl_handler.h" // ObUpdateIndexStatusHelper
#include "pl_ddl/ob_pl_ddl_service.h"
#include "parallel_ddl/ob_drop_table_helper.h" // ObDropTableHelper
#include "parallel_ddl/ob_drop_tablegroup_helper.h" // ObDropTableGroupHelper
#include "parallel_ddl/ob_create_tablegroup_helper.h" // ObCreateTableGroupHelper
#include "share/table/ob_ttl_util.h"
#include "rootserver/ob_ai_model_ddl_service.h"
#include "lib/utility/ob_print_utils.h"     // databuff_printf

namespace oceanbase
{

using namespace common;
using namespace obrpc;
using namespace share;
using namespace share::schema;
using namespace storage;
using namespace dbms_job;

namespace rootserver
{

#define PUSH_BACK_TO_ARRAY_AND_SET_RET(array, msg)                              \
  do {                                                                          \
    if (OB_FAIL(array.push_back(msg))) {                                        \
      LOG_WARN("push reason array error", KR(ret), K(array), K(msg));           \
    }                                                                           \
  } while(0)

int ObRootService::ObMinorFreezeTask::process()
{
  int ret = OB_SUCCESS;
  ObAddr rs_addr = GCTX.self_addr();
  DEBUG_SYNC(BEFORE_DO_MINOR_FREEZE);
  if (OB_ISNULL(GCTX.rs_rpc_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid global context", K(ret));
  } else if (OB_FAIL(GCTX.rs_rpc_proxy_->to(rs_addr).timeout(GCONF.rpc_timeout)
                     .root_minor_freeze(arg_))) {
    LOG_WARN("minor freeze rpc failed", K(ret), K_(arg));
  } else {
    LOG_INFO("minor freeze rpc success", K(ret), K_(arg));
  }
  return ret;
}

int64_t ObRootService::ObMinorFreezeTask::get_deep_copy_size() const
{
  return sizeof(*this);
}

ObAsyncTask *ObRootService::ObMinorFreezeTask::deep_copy(char *buf, const int64_t buf_size) const
{
  ObAsyncTask *task = NULL;
  int ret = OB_SUCCESS;
  const int64_t need_size = get_deep_copy_size();
  if (NULL == buf) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("buf is null", K(ret));
  } else if (buf_size < need_size) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("buf is not long enough", K(need_size), K(buf_size), K(ret));
  } else {
    task = new(buf) ObMinorFreezeTask(arg_);
  }
  return task;
}

////////////////////////////////////////////////////////////////

bool ObRsStatus::can_start_service() const
{
  bool bret = false;
  SpinRLockGuard guard(lock_);
  status::ObRootServiceStatus rs_status = ATOMIC_LOAD(&rs_status_);
  if (status::INIT == rs_status) {
    bret = true;
  }
  return bret;
}

bool ObRsStatus::is_start() const
{
  bool bret = false;
  SpinRLockGuard guard(lock_);
  status::ObRootServiceStatus stat = ATOMIC_LOAD(&rs_status_);
  if (status::STARTING == stat || status::IN_SERVICE == stat
      || status::FULL_SERVICE == stat || status::STARTED == stat) {
    bret = true;
  }
  return bret;
}

bool ObRsStatus::is_stopping() const
{
  bool bret = false;
  SpinRLockGuard guard(lock_);
  status::ObRootServiceStatus stat = ATOMIC_LOAD(&rs_status_);
  if (status::STOPPING == stat) {
    bret = true;
  }
  return bret;
}

bool ObRsStatus::need_do_restart() const
{
  bool bret = false;
  SpinRLockGuard guard(lock_);
  status::ObRootServiceStatus stat = ATOMIC_LOAD(&rs_status_);
  if (status::IN_SERVICE == stat) {
    bret = true;
  }
  return bret;
}

bool ObRsStatus::is_full_service() const
{
  SpinRLockGuard guard(lock_);
  bool bret = false;
  status::ObRootServiceStatus stat = ATOMIC_LOAD(&rs_status_);
  if (status::FULL_SERVICE == stat || status::STARTED == stat) {
    bret = true;
  }
  return bret;
}

bool ObRsStatus::in_service() const
{
  bool bret = false;
  SpinRLockGuard guard(lock_);
  status::ObRootServiceStatus stat = ATOMIC_LOAD(&rs_status_);
  if (status::IN_SERVICE == stat
      || status::FULL_SERVICE == stat
      || status::STARTED == stat) {
    bret = true;
  }
  return bret;
}

bool ObRsStatus::is_need_stop() const
{
  SpinRLockGuard guard(lock_);
  status::ObRootServiceStatus stat = ATOMIC_LOAD(&rs_status_);
  return status::NEED_STOP == stat;
}

status::ObRootServiceStatus ObRsStatus::get_rs_status() const
{
  SpinRLockGuard guard(lock_);
  return ATOMIC_LOAD(&rs_status_);
}

//RS need stop after leader revoke
int ObRsStatus::revoke_rs()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[ROOTSERVICE_NOTICE] try to revoke rs");
  SpinWLockGuard guard(lock_);
  if (status::IN_SERVICE == rs_status_ || status::FULL_SERVICE == rs_status_) {
    rs_status_ = status::NEED_STOP;
    FLOG_INFO("[ROOTSERVICE_NOTICE] rs_status is setted to need_stop", K_(rs_status));
  } else if (status::STOPPING != rs_status_) {
    rs_status_ = status::STOPPING;
    FLOG_INFO("[ROOTSERVICE_NOTICE] rs_status is setted to stopping", K_(rs_status));
  }
  return ret;
}

int ObRsStatus::try_set_stopping()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[ROOTSERVICE_NOTICE] try set rs_status to stopping");
  SpinWLockGuard guard(lock_);
  if (status::NEED_STOP == rs_status_) {
    rs_status_ = status::STOPPING;
    FLOG_INFO("[ROOTSERVICE_NOTICE] rs_status is setted to stopping");
  }
  return ret;
}

int ObRsStatus::set_rs_status(const status::ObRootServiceStatus status)
{
  int ret = OB_SUCCESS;
  SpinWLockGuard guard(lock_);
  const char* new_status_str = NULL;
  const char* old_status_str = NULL;
  if (OB_FAIL(get_rs_status_str(status, new_status_str))) {
    FLOG_WARN("fail to get rs status", KR(ret), K(status));
  } else if (OB_FAIL(get_rs_status_str(rs_status_, old_status_str))) {
    FLOG_WARN("fail to get rs status", KR(ret), K(rs_status_));
  } else if (OB_ISNULL(new_status_str) || OB_ISNULL(old_status_str)) {
    ret = OB_ERR_UNEXPECTED;
    FLOG_WARN("error unexpect", KR(ret), K(new_status_str), K(old_status_str));
  }
  if (OB_SUCC(ret)) {
    switch(rs_status_) {
      case status::INIT:
        {
          if (status::STARTING == status
              || status::STOPPING == status) {
            //rs.stop() will be executed while obs exit
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      case status::STARTING:
        {
          if (status::IN_SERVICE == status
              || status::STOPPING == status) {
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      case status::IN_SERVICE:
        {
          if (status::FULL_SERVICE == status
              || status::NEED_STOP == status
              || status::STOPPING == status) {
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      case status::FULL_SERVICE:
        {
          if (status::STARTED == status
              || status::NEED_STOP == status
              || status::STOPPING == status) {
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      case status::STARTED:
        {
          if (status::STOPPING == status) {
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      case status::NEED_STOP:
        {
          if (status::STOPPING == status) {
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      case status::STOPPING:
        {
          if (status::INIT == status
              || status::STOPPING == status) {
            rs_status_ = status;
            FLOG_INFO("[ROOTSERVICE_NOTICE] success to set rs status",
                      K(new_status_str), K(old_status_str), K(rs_status_));
          } else {
            ret = OB_OP_NOT_ALLOW;
            FLOG_WARN("can't set rs status", KR(ret));
          }
          break;
        }
      default:
        ret = OB_ERR_UNEXPECTED;
        FLOG_WARN("invalid rs status", KR(ret), K(rs_status_));
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////
ObRootService::ObRootService()
: inited_(false), server_refreshed_(false),
    debug_(false),
    self_addr_(), config_(NULL), config_mgr_(NULL),
    rpc_proxy_(), common_proxy_(), sql_proxy_(), restore_ctx_(NULL),
    schema_service_(NULL),
    root_minor_freeze_(),
    ddl_service_(),
    bootstrap_lock_(),
    task_queue_(),
    inspect_task_queue_(),
    restart_task_(*this),
    load_ddl_task_(*this),
    refresh_io_calibration_task_(*this),
    event_table_clear_task_(ROOTSERVICE_EVENT_INSTANCE,
                            SERVER_EVENT_INSTANCE,
                            DEALOCK_EVENT_INSTANCE,
                            task_queue_),
    purge_recyclebin_task_(*this),
    snapshot_manager_(),
    core_meta_table_version_(0),
    baseline_schema_version_(0),
    start_service_time_(0),
    rs_status_(),
    fail_count_(0),
    schema_history_recycler_(),
    alter_log_external_table_task_(*this)
{
}

ObRootService::~ObRootService()
{
  if (inited_) {
    destroy();
  }
}

int ObRootService::init(ObServerConfig &config,
                        ObConfigManager &config_mgr,
                        ObSrvRpcProxy &srv_rpc_proxy,
                        ObCommonRpcProxy &common_proxy,
                        ObAddr &self,
                        ObMySQLProxy &sql_proxy,
                        observer::ObRestoreCtx &restore_ctx,
                        ObMultiVersionSchemaService *schema_service)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[ROOTSERVICE_NOTICE] begin to init rootservice");
  if (inited_) {
    ret = OB_INIT_TWICE;
    FLOG_WARN("rootservice already inited", KR(ret));
  } else if (!self.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    FLOG_WARN("invalid self address", K(self), KR(ret));
  } else if (NULL == schema_service) {
    ret = OB_INVALID_ARGUMENT;
    FLOG_WARN("schema_service must not null", KP(schema_service), KR(ret));
  } else {
    config_ = &config;
    config_mgr_ = &config_mgr;

    rpc_proxy_ = srv_rpc_proxy;
    common_proxy_ = common_proxy;

    const bool rpc_active = false;
    common_proxy_.active(rpc_active);
    rpc_proxy_.active(rpc_active);

    self_addr_ = self;

    restore_ctx_ = &restore_ctx;

    sql_proxy_.assign(sql_proxy);
    sql_proxy_.set_inactive();

    if (OB_FAIL(oracle_sql_proxy_.init(sql_proxy.get_pool()))) {
      FLOG_WARN("init oracle sql proxy failed", KR(ret));
    } else {
      oracle_sql_proxy_.set_inactive();
    }

    schema_service_ = schema_service;
  }

  // init inner queue
  if (FAILEDx(task_queue_.init(
              config_->rootservice_async_task_thread_count,
              config_->rootservice_async_task_queue_size,
              "RSAsyncTask"))) {
    FLOG_WARN("init inner queue failed",
             "thread_count", static_cast<int64_t>(config_->rootservice_async_task_thread_count),
             "queue_size", static_cast<int64_t>(config_->rootservice_async_task_queue_size), KR(ret));
  } else if (OB_FAIL(inspect_task_queue_.init(1/*only for the inspection of RS*/,
                                              config_->rootservice_async_task_queue_size,
                                              "RSInspectTask"))) {
    FLOG_WARN("init inner queue failed",
              "thread_count", 1,
              "queue_size", static_cast<int64_t>(config_->rootservice_async_task_queue_size), KR(ret));
  } else if (OB_FAIL(root_minor_freeze_.init(rpc_proxy_))) {
    // init root minor freeze
    FLOG_WARN("init root_minor_freeze_ failed", KR(ret));
  } else if (OB_FAIL(ddl_service_.init(*GCTX.srv_rpc_proxy_, *GCTX.rs_rpc_proxy_,*GCTX.sql_proxy_, *GCTX.schema_service_,
                                       snapshot_manager_, tenant_ddl_service_))) {
    // init ddl service
    FLOG_WARN("init ddl_service_ failed", KR(ret));
  } else if (OB_FAIL(tenant_ddl_service_.init(ddl_service_, rpc_proxy_,
          common_proxy_, sql_proxy_, *schema_service))) {
    // init tenant ddl service
    FLOG_WARN("init tenant_ddl_service_ failed", KR(ret));
  } else if (OB_FAIL(snapshot_manager_.init(self_addr_))) {
    FLOG_WARN("init snapshot manager failed", KR(ret));
  } else if (OB_ISNULL(GCTX.meta_db_pool_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("meta_db_pool_ is not initialized", K(ret));
  } else if (OB_FAIL(ROOTSERVICE_EVENT_INSTANCE.init(GCTX.meta_db_pool_, self_addr_))) {
    FLOG_WARN("init rootservice event history failed", KR(ret));
  } else if (OB_FAIL(THE_RS_JOB_TABLE.init())) {
    FLOG_WARN("init THE_RS_JOB_TABLE failed", KR(ret));
  } else if (OB_FAIL(ObRsAutoSplitScheduler::get_instance().init())) {
    FLOG_WARN("init auto split task scheduler failed", K(ret));
  } else if (OB_FAIL(schema_history_recycler_.init(*schema_service_,
                                                   sql_proxy_))) {
    FLOG_WARN("fail to init schema history recycler failed", KR(ret));
  } else if (OB_FAIL(dbms_job::ObDBMSJobMaster::get_instance().init(&sql_proxy_,
                                                                    schema_service_))) {
    FLOG_WARN("init ObDBMSJobMaster failed", KR(ret));
  }

  if (OB_SUCC(ret)) {
    inited_ = true;
    FLOG_INFO("[ROOTSERVICE_NOTICE] init rootservice success", KR(ret), K_(inited));
  } else {
    LOG_ERROR("[ROOTSERVICE_NOTICE] fail to init root service", KR(ret));
    LOG_DBA_ERROR(OB_ERR_ROOTSERVICE_START, "msg", "rootservice init() has failure", KR(ret));
  }

  return ret;
}

void ObRootService::destroy()
{
  int ret = OB_SUCCESS;
  int fail_ret = OB_SUCCESS;
  FLOG_INFO("[ROOTSERVICE_NOTICE] start to destroy rootservice");
  if (in_service()) {
    if (OB_FAIL(stop_service())) {
      FLOG_WARN("stop service failed", KR(ret));
      fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
    }
  }

  // continue executing while error happen
  if (OB_FAIL(schema_history_recycler_.destroy())) {
    FLOG_WARN("schema history recycler destroy failed", KR(ret));
    fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
  } else {
    FLOG_INFO("schema history recycler destroy");
  }

  task_queue_.destroy();
  FLOG_INFO("inner queue destroy");
  inspect_task_queue_.destroy();
  FLOG_INFO("inspect queue destroy");

  ROOTSERVICE_EVENT_INSTANCE.destroy();
  FLOG_INFO("event table operator destroy");

  dbms_job::ObDBMSJobMaster::get_instance().destroy();
  FLOG_INFO("ObDBMSJobMaster destroy");

  if (OB_SUCC(ret)) {
    if (inited_) {
      inited_ = false;
    }
  }

  FLOG_INFO("[ROOTSERVICE_NOTICE] destroy rootservice end", KR(ret));
  if (OB_SUCCESS != fail_ret) {
    LOG_DBA_WARN(OB_ERR_ROOTSERVICE_STOP, "msg", "rootservice destroy() has failure", KR(fail_ret));
  }
}

ERRSIM_POINT_DEF(ERRSIM_RS_START_SERVICE_ERROR);
int ObRootService::start_service()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  start_service_time_ = ObTimeUtility::current_time();
  ROOTSERVICE_EVENT_ADD("root_service", "start_rootservice", K_(self_addr));
  FLOG_INFO("[ROOTSERVICE_NOTICE] start to start rootservice", K_(start_service_time), KCSTRING(lbt()));
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("rootservice not inited", KR(ret));
  } else if (OB_FAIL(rs_status_.set_rs_status(status::STARTING))) {
    FLOG_WARN("fail to set rs status", KR(ret));
  } else if (OB_UNLIKELY(ERRSIM_RS_START_SERVICE_ERROR)) {
    ret = ERRSIM_RS_START_SERVICE_ERROR;
    LOG_INFO("ERRSIM here", KR(ret));
  } else {
    sql_proxy_.set_active();
    oracle_sql_proxy_.set_active();
    const bool rpc_active = true;
    common_proxy_.active(rpc_active);
    rpc_proxy_.active(rpc_active);
    tenant_ddl_service_.restart();
#ifndef OB_BUILD_LITE
    if (OB_FAIL(hb_checker_.start())) {
      FLOG_WARN("hb checker start failed", KR(ret));
      } else
#endif
    if (OB_FAIL(task_queue_.start())) {
      FLOG_WARN("inner queue start failed", KR(ret));
    } else if (OB_FAIL(inspect_task_queue_.start())) {
      FLOG_WARN("inspect queue start failed", KR(ret));
    }
    if (FAILEDx(rs_status_.set_rs_status(status::IN_SERVICE))) {
      FLOG_WARN("fail to set rs status", KR(ret));
    } else if (OB_FAIL(schedule_restart_timer_task(0))) {
      FLOG_WARN("failed to schedule restart task", KR(ret));
    } else if (debug_) {
      if (OB_FAIL(init_debug_database())) {
        FLOG_WARN("init_debug_database failed", KR(ret));
      }
    }
  }

  ROOTSERVICE_EVENT_ADD("root_service", "finish_start_rootservice",
                        "result", ret, K_(self_addr));

  if (OB_FAIL(ret)) {
    // increase fail count for self checker and print log.
    update_fail_count(ret);
    FLOG_WARN("start service failed, do stop service", KR(ret));
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = rs_status_.set_rs_status(status::STOPPING))) {
      FLOG_WARN("fail to set status", KR(tmp_ret));
    } else if (OB_SUCCESS != (tmp_ret = stop_service())) {
      FLOG_WARN("stop service failed", KR(tmp_ret));
    }
  }

  FLOG_INFO("[ROOTSERVICE_NOTICE] rootservice start_service finished", KR(ret));
  return ret;
}

int ObRootService::stop_service()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("[ROOTSERVICE_NOTICE] stop service begin");
  if (OB_FAIL(stop())) {
    FLOG_WARN("fail to stop thread", KR(ret));
  } else {
    wait();
  }
  if (FAILEDx(rs_status_.set_rs_status(status::INIT))) {
    FLOG_WARN("fail to set rs status", KR(ret));
  }
  FLOG_INFO("[ROOTSERVICE_NOTICE] stop service finished", KR(ret));
  return ret;
}

ERRSIM_POINT_DEF(ERRSIM_RS_STOP_ERROR);
int ObRootService::stop()
{
  int ret = OB_SUCCESS;
  int fail_ret = OB_SUCCESS;
  start_service_time_ = 0;
  int64_t start_time = ObTimeUtility::current_time();
  ROOTSERVICE_EVENT_ADD("root_service", "stop_rootservice", K_(self_addr));
  FLOG_INFO("[ROOTSERVICE_NOTICE] start to stop rootservice", K(start_time));
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("rootservice not inited", KR(ret));
    fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
  } else if (OB_FAIL(rs_status_.set_rs_status(status::STOPPING))) {
    FLOG_WARN("fail to set rs status", KR(ret));
    fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
  } else {
    //full_service_ = false;
    server_refreshed_ = false;
    //in_service_ = false;
    sql_proxy_.set_inactive();
    FLOG_INFO("sql_proxy set inactive finished");
    oracle_sql_proxy_.set_inactive();
    FLOG_INFO("oracle_sql_proxy set inactive finished");
    const bool rpc_active = false;
    common_proxy_.active(rpc_active);
    FLOG_INFO("commom_proxy set inactive finished");
    rpc_proxy_.active(rpc_active);
    FLOG_INFO("rpc_proxy set inactive finished");

    // let RS stop failed after proxy inactive
    if (OB_UNLIKELY(ERRSIM_RS_STOP_ERROR)) {
      ret = ERRSIM_RS_STOP_ERROR;
      LOG_INFO("ERRSIM here", KR(ret));
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(stop_timer_tasks())) {
        FLOG_WARN("stop timer tasks failed", KR(ret));
        fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
      } else {
        FLOG_INFO("stop timer tasks success");
      }
    }

    if (OB_SUCC(ret)) {
      // ddl_service may be trying refresh schema, stop it
      tenant_ddl_service_.stop();
      FLOG_INFO("ddl service stop");
      root_minor_freeze_.stop();
      FLOG_INFO("minor freeze stop");
    }
    if (OB_SUCC(ret)) {
      task_queue_.stop();
      FLOG_INFO("task_queue stop");
      inspect_task_queue_.stop();
      FLOG_INFO("inspect queue stop");
      schema_history_recycler_.stop();
      FLOG_INFO("schema_history_recycler stop");
      dbms_job::ObDBMSJobMaster::get_instance().stop();
      FLOG_INFO("dbms job master stop");
    }
  }

  ROOTSERVICE_EVENT_ADD("root_service", "finish_stop_thread", KR(ret), K_(self_addr));
  FLOG_INFO("[ROOTSERVICE_NOTICE] finish stop rootservice", KR(ret));
  if (OB_SUCCESS != fail_ret) {
    LOG_DBA_WARN(OB_ERR_ROOTSERVICE_STOP, "msg", "rootservice stop() has failure", KR(fail_ret));
  }
  return ret;
}

void ObRootService::wait()
{
  FLOG_INFO("[ROOTSERVICE_NOTICE] wait rootservice begin");
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("start to wait all thread exit");
  schema_history_recycler_.wait();
  FLOG_INFO("schema_history_recycler exit success");
  task_queue_.wait();
  FLOG_INFO("task queue exit success");
  inspect_task_queue_.wait();
  FLOG_INFO("inspect queue exit success");
  THE_RS_JOB_TABLE.reset_max_job_id();
  int64_t cost = ObTimeUtility::current_time() - start_time;
  ROOTSERVICE_EVENT_ADD("root_service", "finish_wait_stop", K(cost));
  FLOG_INFO("[ROOTSERVICE_NOTICE] rootservice wait finished", K(start_time), K(cost));
  if (cost > 10 * 60 * 1000 * 1000L) { // 10min
    int ret = OB_ERROR;
    LOG_ERROR("cost too much time to wait rs stop", KR(ret), K(start_time), K(cost));
  }
}

int ObRootService::submit_ddl_single_replica_build_task(ObAsyncTask &task)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObRootService has not been inited", K(ret));
  } else if (OB_FAIL(ObSysDDLReplicaBuilderUtil::push_task(task))) {
    LOG_WARN("fail to push task to ddl builder", KR(ret));
  }
  return ret;
}

int ObRootService::schedule_recyclebin_task(int64_t delay)
{
  int ret = OB_SUCCESS;
  const bool did_repeat = false;

  if (OB_FAIL(get_inspect_task_queue().add_timer_task(
              purge_recyclebin_task_, delay, did_repeat))) {
    if (OB_CANCELED != ret) {
      LOG_ERROR("schedule purge recyclebin task failed", KR(ret), K(delay), K(did_repeat));
    } else {
      LOG_WARN("schedule purge recyclebin task failed", KR(ret), K(delay), K(did_repeat));
    }
  }

  return ret;
}

int ObRootService::schedule_load_ddl_task()
{
  int ret = OB_SUCCESS;
  const bool did_repeat = false;
#ifdef ERRSIM
  const int64_t delay = 1000L * 1000L; //1s
#else
  const int64_t delay = 5L * 1000L * 1000L; //5s
#endif
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (task_queue_.exist_timer_task(load_ddl_task_)) {
    // ignore error
    LOG_WARN("load ddl task already exist", K(ret));
  } else if (OB_FAIL(task_queue_.add_timer_task(load_ddl_task_, delay, did_repeat))) {
    LOG_WARN("fail to add timer task", K(ret));
  } else {
    LOG_INFO("succeed to add load ddl task");
  }
  return ret;
}

int ObRootService::schedule_refresh_io_calibration_task()
{
  int ret = OB_SUCCESS;
  const bool did_repeat = false;
  const int64_t delay = 5L * 1000L * 1000L; //5s
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (task_queue_.exist_timer_task(refresh_io_calibration_task_)) {
    // ignore error
    LOG_WARN("refresh io calibration task already exist", K(ret));
  } else if (OB_FAIL(task_queue_.add_timer_task(refresh_io_calibration_task_, delay, did_repeat))) {
    LOG_WARN("fail to add timer task", K(ret));
  } else {
    LOG_INFO("succeed to add refresh io calibration task");
  }
  return ret;
}

int ObRootService::schedule_alter_log_external_table_task()
{
  int ret = OB_SUCCESS;
  const bool did_repeat = false;
  const int64_t delay = 1L * 1000L * 1000L; //1s
  uint64_t current_data_version = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (task_queue_.exist_timer_task(alter_log_external_table_task_)) {
    // ignore error
    LOG_WARN("already have one alter_log_external_table_task, ignore this");
  } else if (OB_FAIL(GET_MIN_DATA_VERSION(OB_SYS_TENANT_ID, current_data_version))) {
    LOG_WARN("fail to get current data version", KR(ret), "tenant_id", OB_SYS_TENANT_ID);
  } else if (OB_FAIL(alter_log_external_table_task_.init(current_data_version))) {
    LOG_WARN("fail to init alter log external table task", KR(ret), K(current_data_version));
  } else if (OB_FAIL(task_queue_.add_timer_task(alter_log_external_table_task_, delay, did_repeat))) {
    LOG_WARN("fail to add timer task", KR(ret));
  } else {
    LOG_INFO("add alter_log_external_table_task task success", KR(ret), K(current_data_version));
  }
  return ret;
}

int ObRootService::schedule_restart_timer_task(const int64_t delay)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const bool did_repeat = false;
    if (OB_FAIL(task_queue_.add_timer_task(restart_task_,
                                           delay, did_repeat))) {
      LOG_WARN("schedule restart task failed", K(ret), K(delay), K(did_repeat));
    } else {
      LOG_INFO("submit restart task success", K(delay));
    }
  }
  return ret;
}

int ObRootService::after_restart()
{
  ObCurTraceId::init(GCONF.self_addr_);

  // avoid concurrent with bootstrap
  FLOG_INFO("[ROOTSERVICE_NOTICE] try to get lock for bootstrap in after_restart");
  ObLatchRGuard guard(bootstrap_lock_, ObLatchIds::RS_BOOTSTRAP_LOCK);

  // NOTE: Following log print after lock
  FLOG_INFO("[ROOTSERVICE_NOTICE] start to do restart task");

  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("rootservice not init", KR(ret));
  } else if (need_do_restart() && OB_FAIL(do_restart())) {
    FLOG_WARN("do restart failed, retry again", KR(ret));
  } else if (OB_FAIL(do_after_full_service())) {
    FLOG_WARN("fail to do after full service", KR(ret));
  }

  int64_t cost = ObTimeUtility::current_time() - start_service_time_;
  if (OB_FAIL(ret)) {
    FLOG_WARN("do restart task failed, retry again", KR(ret), K(cost));
  } else if (OB_FAIL(rs_status_.set_rs_status(status::STARTED))) {
    FLOG_WARN("fail to set rs status", KR(ret));
  } else {
    FLOG_INFO("do restart task success, finish restart", KR(ret), K(cost), K_(start_service_time));
  }

  if (OB_FAIL(ret)) {
    rs_status_.try_set_stopping();
    if (rs_status_.is_stopping()) {
      // need stop
      FLOG_INFO("rs_status_ is set to stopping");
    } else {
      int tmp_ret = OB_SUCCESS;
      if (OB_TMP_FAIL(reschedule_restart_timer_task_after_failure())) {
        LOG_WARN("failed to reschedule restart time task", KR(tmp_ret));
      }
    }
  }

  // NOTE: Following log print after lock
  FLOG_INFO("[ROOTSERVICE_NOTICE] finish do restart task", KR(ret));
  return ret;
}

int ObRootService::reschedule_restart_timer_task_after_failure()
{
  int ret = OB_SUCCESS;
  const int64_t RETRY_TIMES = 3;
  for (int64_t i = 0; i < RETRY_TIMES; ++i) {
    if (OB_FAIL(schedule_restart_timer_task(config_->rootservice_ready_check_interval))) {
      FLOG_WARN("fail to schedule_restart_timer_task at this retry", KR(ret), K(i));
    } else {
      FLOG_INFO("success to schedule_restart_timer_task");
      break;
    }
  }
  if (OB_FAIL(ret)) {
    LOG_ERROR("fatal error, fail to add restart task", KR(ret));
    if (OB_FAIL(rs_status_.set_rs_status(status::STOPPING))) {
      LOG_ERROR("fail to set rs status", KR(ret));
    }
  }
  return ret;
}

int ObRootService::do_after_full_service() {
  int ret = OB_SUCCESS;
  ObGlobalStatProxy global_proxy(sql_proxy_, OB_SYS_TENANT_ID);
  if (OB_FAIL(global_proxy.get_baseline_schema_version(baseline_schema_version_))) {
    LOG_WARN("fail to get baseline schema version", KR(ret));
  }
  return ret;
}

////////////////////////////////////////////////////////////////
int ObRootService::execute_bootstrap()
{
  int ret = OB_SUCCESS;
  BOOTSTRAP_LOG(INFO, "STEP_1.1:execute_bootstrap start to executor.");
  DBA_STEP_RESET(bootstrap);
  LOG_DBA_INFO_V2(OB_BOOTSTRAP_BEGIN,
                  DBA_STEP_INC_INFO(bootstrap),
                  "cluster bootstrap begin.");
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("root_service not inited", K(ret));
  } else if (!sql_proxy_.is_inited() || !sql_proxy_.is_active()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sql_proxy not inited or not active", "sql_proxy inited",
             sql_proxy_.is_inited(), "sql_proxy active", sql_proxy_.is_active(), K(ret));
  } else {
    // avoid bootstrap and do_restart run concurrently
    FLOG_INFO("[ROOTSERVICE_NOTICE] try to get lock for bootstrap in execute_bootstrap");
    ObLatchWGuard guard(bootstrap_lock_, ObLatchIds::RS_BOOTSTRAP_LOCK);
    FLOG_INFO("[ROOTSERVICE_NOTICE] success to get lock for bootstrap in execute_bootstrap");
    ObBootstrap bootstrap(rpc_proxy_, ddl_service_, tenant_ddl_service_,
        *config_, common_proxy_);
    if (OB_FAIL(bootstrap.execute_bootstrap())) {
      LOG_ERROR("failed to execute_bootstrap", K(ret));
    }

    BOOTSTRAP_LOG(INFO, "start to do_restart");
    ObGlobalStatProxy global_proxy(sql_proxy_, OB_SYS_TENANT_ID);
    ObArray<ObAddr> self_addr;
    ObTimeoutCtx ctx;
    if (OB_FAIL(ret)) {
      // load all sys package should run before do_restart
    } else if (OB_FAIL(do_restart())) {
      LOG_WARN("do restart task failed", K(ret));
    } else if (OB_FAIL(check_ddl_allowed())) {
      LOG_WARN("fail to check ddl allowed", K(ret));
    } else if (OB_FAIL(set_cluster_version())) {
      LOG_WARN("set cluster version failed", K(ret));
    } else if (OB_FAIL(pl::ObPLPackageManager::load_all_special_sys_package(sql_proxy_))) {
      LOG_WARN("failed to load all special sys package", KR(ret));
    } else if (OB_FAIL(finish_bootstrap())) {
      LOG_WARN("failed to finish bootstrap", K(ret));
    } else if (OB_FAIL(update_baseline_schema_version())) {
      LOG_WARN("failed to update baseline schema version", K(ret));
    } else if (OB_FAIL(global_proxy.get_baseline_schema_version(
                       baseline_schema_version_))) {
      LOG_WARN("fail to get baseline schema version", KR(ret));
    } else if (OB_FAIL(set_config_after_bootstrap_())) {
      LOG_WARN("failed to set config for bootstrap", KR(ret));
    } 
    if (OB_SUCC(ret)) {
      LOG_DBA_INFO_V2(OB_BOOTSTRAP_WAIT_SYS_PACKAGE_BEGIN,
                      DBA_STEP_INC_INFO(bootstrap),
                      "bootstrap wait sys package begin.");
      if (OB_FAIL(ObShareUtil::set_default_timeout_ctx(ctx, GCONF._ob_ddl_timeout))) {
        LOG_WARN("failed to set default timeout", KR(ret));
      } else if (!GCONF._enable_async_load_sys_package &&
          OB_FAIL(ObSysTenantLoadSysPackageTask::wait_sys_package_ready(ctx, ObCompatibilityMode::MYSQL_MODE))) {
        LOG_WARN("failed to wait mysql sys package ready", KR(ret), K(ctx));
      } else {
        LOG_DBA_INFO_V2(OB_BOOTSTRAP_WAIT_SYS_PACKAGE_SUCCESS,
                        DBA_STEP_INC_INFO(bootstrap),
                        "bootstrap wait sys package success.");
      }
    }

    if (OB_SUCC(ret)) {
      char ori_min_server_version[OB_SERVER_VERSION_LENGTH] = {'\0'};
      uint64_t ori_cluster_version = GET_MIN_CLUSTER_VERSION();
      share::ObBuildVersion build_version;
      if (OB_INVALID_INDEX == ObClusterVersion::print_version_str(
          ori_min_server_version, OB_SERVER_VERSION_LENGTH, ori_cluster_version)) {
         ret = OB_INVALID_ARGUMENT;
         LOG_WARN("fail to print version str", KR(ret), K(ori_cluster_version));
      } else if (OB_FAIL(observer::ObService::get_build_version(build_version))) {
        LOG_WARN("fail to get build version", KR(ret));
      } else {
        CLUSTER_EVENT_SYNC_ADD("BOOTSTRAP", "BOOTSTRAP_SUCCESS",
                               "cluster_version", ori_min_server_version,
                               "build_version", build_version.ptr());
      }
    }

    //clear bootstrap flag, regardless failure or success
    int tmp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (tmp_ret = clear_special_cluster_schema_status())) {
      LOG_WARN("failed to clear special cluster schema status",
                KR(ret), K(tmp_ret));
    }
    ret = OB_SUCC(ret) ? tmp_ret : ret;
  }
  BOOTSTRAP_LOG(INFO, "execute_bootstrap finished", K(ret));
  if (OB_FAIL(ret)) {
    LOG_DBA_FORCE_PRINT(DBA_ERROR, OB_BOOTSTRAP_FAIL, ret,
                        DBA_STEP_INC_INFO(bootstrap),
                        "cluster bootstrap fail. "
                        "you may find solutions in previous error logs or seek help from official technicians.");
  } else {
    LOG_DBA_INFO_V2(OB_BOOTSTRAP_SUCCESS,
                    DBA_STEP_INC_INFO(bootstrap),
                    "cluster bootstrap success.");
  }
  // after bootstrap success, clear bootstrap schema cache
  // because in bootstrap, bootstrap schema cache will cache all sys table schemas, after bootstrap success, we just need part of sys table schemas
  ObMultiVersionSchemaService &multi_schema_service = ddl_service_.get_schema_service();
  multi_schema_service.clear_bootstrap_schema_cache();

  return ret;
}

int ObRootService::check_config_result(const char *name, const char* value)
{
  int ret = OB_SUCCESS;
  const int64_t start = ObTimeUtility::current_time();
  const uint64_t DEFAULT_WAIT_US = 120 * 1000 * 1000L; //120s
  int64_t timeout = DEFAULT_WAIT_US;
  if (INT64_MAX != THIS_WORKER.get_timeout_ts()) {
    timeout = MAX(DEFAULT_WAIT_US, THIS_WORKER.get_timeout_remain());
  }
  ObSqlString sql;
  HEAP_VAR(ObMySQLProxy::MySQLResult, res) {
    common::sqlclient::ObMySQLResult *result = NULL;
    if (OB_FAIL(sql.assign_fmt("SELECT count(*) as count FROM %s "
                               "WHERE name = '%s' and value != '%s'",
                               "__all_virtual_tenant_parameter_stat", name, value))) {
      LOG_WARN("fail to append sql", K(ret));
    }
    while(OB_SUCC(ret) || OB_ERR_WAIT_REMOTE_SCHEMA_REFRESH == ret /* remote schema not ready, return -4029 on remote */) {
      if (ObTimeUtility::current_time() - start > timeout) {
        ret = OB_TIMEOUT;
        LOG_WARN("sync config info use too much time", K(ret), K(name), K(value),
                 "cost_us", ObTimeUtility::current_time() - start);
      } else {
        if (OB_FAIL(sql_proxy_.read(res, sql.ptr()))) {
          LOG_WARN("fail to execute sql", K(ret), K(sql));
        } else if (NULL == (result = res.get_result())) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("fail to get sql result", K(ret));
        } else if (OB_FAIL(result->next())) {
          LOG_WARN("fail to get result", K(ret));
        } else {
          int32_t count = OB_INVALID_COUNT;
          EXTRACT_INT_FIELD_MYSQL(*result, "count", count, int32_t);
          if (OB_SUCC(ret)) {
            if (count == 0) { break; }
          }
        }
      }
    } // while end
  }
  return ret;
}

// DDL exection depends on full_service & major_freeze_done state. the sequence of these two status in bootstrap is:
// 1.rs do_restart: major_freeze_launcher start
// 2.rs do_restart success: full_service is true
// 3.root_major_freeze success: major_freeze_done is true (need full_service is true)
// the success of do_restart does not mean to execute DDL, therefor, add wait to bootstrap, to avoid bootstrap failure cause by DDL failure
int ObRootService::check_ddl_allowed()
{
  int ret = OB_SUCCESS;
  const int64_t SLEEP_INTERVAL_US = 1 * 1000 * 1000; //1s
  while (OB_SUCC(ret) && !is_ddl_allowed()) {
    if (!in_service() && !is_start()) {
      ret = OB_RS_SHUTDOWN;
      LOG_WARN("rs shutdown", K(ret));
    } else if (THIS_WORKER.is_timeout()) {
      ret = OB_TIMEOUT;
      LOG_WARN("wait too long", K(ret));
    } else {
      ob_usleep(SLEEP_INTERVAL_US);
    }
  }
  return ret;
}

int ObRootService::update_baseline_schema_version()
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  int64_t baseline_schema_version = OB_INVALID_VERSION;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(trans.start(&sql_proxy_, OB_SYS_TENANT_ID))) {
    LOG_WARN("trans start failed", K(ret));
  } else if (OB_FAIL(ddl_service_.get_schema_service().
                     get_tenant_refreshed_schema_version(OB_SYS_TENANT_ID,
                                                         baseline_schema_version))) {
    LOG_WARN("fail to get refreshed schema version", K(ret));
  } else {
    ObGlobalStatProxy proxy(trans, OB_SYS_TENANT_ID);
    if (OB_FAIL(proxy.set_baseline_schema_version(baseline_schema_version))) {
      LOG_WARN("set_baseline_schema_version failed", K(baseline_schema_version), K(ret));
    }
  }
  int temp_ret = OB_SUCCESS;
  if (!trans.is_started()) {
  } else if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCCESS == ret))) {
    LOG_WARN("trans end failed", "commit", OB_SUCCESS == ret, K(temp_ret));
    ret = (OB_SUCCESS == ret) ? temp_ret : ret;
  }
    LOG_DEBUG("update_baseline_schema_version finish", K(ret), K(temp_ret),
              K(baseline_schema_version));
  return ret;
}

int ObRootService::finish_bootstrap()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    const int64_t tenant_id = OB_SYS_TENANT_ID;
    int64_t new_schema_version = OB_INVALID_VERSION;
    ObMultiVersionSchemaService &multi_schema_service = ddl_service_.get_schema_service();
    share::schema::ObSchemaService *tmp_schema_service = multi_schema_service.get_schema_service();
    if (OB_ISNULL(tmp_schema_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema service is null", K(ret), KP(tmp_schema_service));
    } else {
      ObMySQLProxy &sql_proxy = ddl_service_.get_sql_proxy();
      share::schema::ObDDLSqlService ddl_sql_service(*tmp_schema_service);
      share::schema::ObSchemaOperation schema_operation;
      schema_operation.op_type_ = share::schema::OB_DDL_FINISH_BOOTSTRAP;
      schema_operation.tenant_id_ = tenant_id;
      if (OB_FAIL(multi_schema_service.gen_new_schema_version(tenant_id, new_schema_version))) {
        LOG_WARN("fail to gen new schema_version", K(ret), K(tenant_id), K(new_schema_version));
      } else if (OB_FAIL(ddl_sql_service.log_nop_operation(schema_operation,
                                                           new_schema_version,
                                                           schema_operation.ddl_stmt_str_,
                                                           sql_proxy))) {
        LOG_WARN("log finish bootstrap operation failed", K(ret), K(schema_operation));
      } else if (OB_FAIL(ddl_service_.refresh_schema(OB_SYS_TENANT_ID))) {
        LOG_WARN("failed to refresh_schema", K(ret));
      } else {
        LOG_INFO("finish bootstrap", K(ret), K(new_schema_version));
      }
    }
  }
  return ret;
}

////////////////////////////////////////////////////////////////

int ObRootService::add_system_variable(const ObAddSysVarArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sysvar arg", K(arg));
  } else if (OB_FAIL(ddl_service_.add_system_variable(arg))) {
    LOG_WARN("add system variable failed", K(ret));
  }
  return ret;
}

int ObRootService::modify_system_variable(const obrpc::ObModifySysVarArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid sysvar arg", K(arg));
  } else if (OB_FAIL(ddl_service_.modify_system_variable(arg))) {
    LOG_WARN("modify system variable failed", K(ret));
  }
  return ret;
}

int ObRootService::create_database(const ObCreateDatabaseArg &arg, UInt64 &db_id)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObDatabaseSchema copied_db_schema = arg.database_schema_;
    if (OB_FAIL(ddl_service_.create_database(arg.if_not_exist_,
                                             copied_db_schema, &arg.ddl_stmt_str_))) {
      LOG_WARN("create_database failed", "if_not_exist", arg.if_not_exist_,
               K(copied_db_schema), "ddl_stmt_str", arg.ddl_stmt_str_, K(ret));
    } else {
      db_id = copied_db_schema.get_database_id();
    }
  }
  return ret;
}

int ObRootService::alter_database(const ObAlterDatabaseArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.alter_database(arg))) {
    LOG_WARN("alter database failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::create_tablegroup(const ObCreateTablegroupArg &arg, UInt64 &tg_id)
{
  LOG_INFO("receive create tablegroup arg", K(arg));
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObTablegroupSchema copied_tg_schema;
    if (OB_FAIL(copied_tg_schema.assign(arg.tablegroup_schema_))) {
      LOG_WARN("failed to assign tablegroup schema", K(ret), K(arg));
    } else if (OB_FAIL(ddl_service_.create_tablegroup(
            arg.if_not_exist_, copied_tg_schema, &arg.ddl_stmt_str_))) {
      LOG_WARN("create_tablegroup failed", "if_not_exist", arg.if_not_exist_,
               K(copied_tg_schema), "ddl_stmt_str", arg.ddl_stmt_str_, K(ret));
    } else {
      tg_id = copied_tg_schema.get_tablegroup_id();
    }
  }
  return ret;
}

int ObRootService::parallel_ddl_pre_check_(const uint64_t tenant_id)
{
  int ret = OB_SUCCESS;
  bool is_dropped = false;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(OB_INVALID_TENANT_ID == tenant_id)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid tenant_id", KR(ret), K(tenant_id));
  } else if (OB_FAIL(schema_service_->check_if_tenant_has_been_dropped(tenant_id, is_dropped))) {
    LOG_WARN("fail to check if tenant has been dropped", KR(ret), K(tenant_id));
  } else if (is_dropped) {
    ret = OB_TENANT_HAS_BEEN_DROPPED;
    LOG_WARN("tenant has been dropped", KR(ret), K(tenant_id));
  } else if (!schema_service_->is_tenant_refreshed(tenant_id)) {
    // use this err to trigger DDL retry and release current thread.
    ret = OB_ERR_PARALLEL_DDL_CONFLICT;
    LOG_WARN("tenant' schema not refreshed yet, need retry", KR(ret), K(tenant_id));
  }
  return ret;
}

int ObRootService::parallel_create_table(const ObCreateTableArg &arg, ObCreateTableRes &res)
{
  LOG_TRACE("receive create table arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  const uint64_t tenant_id = arg.exec_tenant_id_;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_(tenant_id))) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret), K(tenant_id));
  } else if (arg.schema_.is_view_table()) {
    ObCreateViewHelper create_view_helper(schema_service_, tenant_id, arg, res);
    if (OB_FAIL(create_view_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create view helper", KR(ret), K(tenant_id));
    } else if (OB_FAIL(create_view_helper.execute())) {
      LOG_WARN("fail to execute create view", KR(ret), K(tenant_id));
    }
  } else {
    ObCreateTableHelper create_table_helper(schema_service_, tenant_id, arg, res);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(create_table_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create table helper", KR(ret), K(tenant_id));
    } else if (OB_FAIL(create_table_helper.execute())) {
      LOG_WARN("fail to execute create table", KR(ret), K(tenant_id));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  LOG_TRACE("finish create table", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel create table",
                        K(tenant_id),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  return ret;
}

int ObRootService::parallel_htable_ddl(const ObHTableDDLArg &arg, ObHTableDDLRes &res)
{
  LOG_TRACE("receive htable ddl arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  const uint64_t tenant_id = arg.exec_tenant_id_;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_(tenant_id))) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret), K(tenant_id));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else {
    ObArenaAllocator tmp_allocator(lib::ObLabel("paralHtDDL"));
    ObHTableDDLHandlerGuard guard(tmp_allocator);
    ObHTableDDLHandler *handler = nullptr;
    if (OB_FAIL(guard.get_handler(ddl_service_, *schema_service_, arg, res, handler))) {
      LOG_WARN("fail to get handler", KR(ret), K(arg), K(tenant_id));
    } else if (OB_ISNULL(handler)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("handler is null", KR(ret));
    } else if (OB_FAIL(handler->init())) {
      LOG_WARN("fail to init handle", KR(ret), K(arg), K(tenant_id));
    } else if (OB_FAIL(handler->handle())) {
      LOG_WARN("fail to handle", KR(ret), K(arg), K(tenant_id));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  LOG_TRACE("finish htable ddl", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel htable ddl",
                        K(tenant_id),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        K(cost));
  return ret;
}

int ObRootService::gen_container_table_schema_(const ObCreateTableArg &arg,
                                               ObSchemaGetterGuard &schema_guard,
                                               ObTableSchema &mv_table_schema,
                                               ObArray<ObTableSchema> &table_schemas)
{
  int ret = OB_SUCCESS;
  SMART_VAR(ObTableSchema, container_table_schema) {
    if (arg.mv_ainfo_.count() >= 2) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("container table should be less than two", KR(ret), K(arg.mv_ainfo_.count()));
    }

    for (int64_t i = 0; OB_SUCC(ret) && i < arg.mv_ainfo_.count(); i ++) {
      container_table_schema.reset();
      char buf[OB_MAX_TABLE_NAME_LENGTH];
      memset(buf, 0, OB_MAX_TABLE_NAME_LENGTH);

      if (OB_SUCC(ret)) {
        if (OB_FAIL(container_table_schema.assign(arg.mv_ainfo_.at(i).container_table_schema_))) {
          LOG_WARN("fail to assign index schema", KR(ret));
        } else if (OB_FAIL(databuff_printf(buf, OB_MAX_TABLE_NAME_LENGTH, "__mv_container_%ld", mv_table_schema.get_table_id()))) {
          LOG_WARN("fail to print table name", KR(ret));
        } else if (OB_FAIL(container_table_schema.set_table_name(buf))) {
          LOG_WARN("fail to set table_name", KR(ret));
        } else {
          container_table_schema.set_database_id(mv_table_schema.get_database_id());
        }
      }

      if (OB_SUCC(ret)) {
        ObArray<ObSchemaType> conflict_schema_types;
        if (!arg.is_alter_view_
            && OB_FAIL(schema_guard.check_oracle_object_exist(container_table_schema.get_tenant_id(),
                   container_table_schema.get_database_id(), container_table_schema.get_table_name_str(),
                   TABLE_SCHEMA, INVALID_ROUTINE_TYPE, arg.if_not_exist_, conflict_schema_types))) {
          LOG_WARN("fail to check oracle_object exist", K(ret), K(container_table_schema));
        } else if (conflict_schema_types.count() > 0) {
          ret = OB_ERR_EXIST_OBJECT;
          LOG_WARN("Name is already used by an existing object",
                   K(ret), K(container_table_schema), K(conflict_schema_types));
        }
      }
      if (OB_SUCC(ret)) { // check same table_name
        bool table_exist = false;
        ObSchemaGetterGuard::CheckTableType check_type = ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES;

        if (OB_FAIL(schema_guard.check_table_exist(container_table_schema.get_tenant_id(),
                container_table_schema.get_database_id(),
                container_table_schema.get_table_name_str(),
                false, /*is index*/
                check_type,
                table_exist))) {
          LOG_WARN("check table exist failed", KR(ret), K(container_table_schema));
        } else if (table_exist) {
          ret = OB_ERR_TABLE_EXIST;
          LOG_WARN("table exist", KR(ret), K(container_table_schema), K(arg.if_not_exist_));
        }
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(ddl_service_.generate_schema(arg, container_table_schema))) {
          LOG_WARN("fail to generate container table schema", KR(ret));
        } else {
          //table_schema.get_view_schema().set_container_table_id(container_table_schema.get_table_id());
          mv_table_schema.set_data_table_id(container_table_schema.get_table_id());
        }
      }
      if (OB_SUCC(ret)) {
        ObArray<ObTableSchema> lob_schemas;
        if (OB_FAIL(ddl_service_.build_aux_lob_table_schema_if_need(container_table_schema, lob_schemas))) {
          LOG_WARN("fail to build_aux_lob_table_schema_if_need", K(ret), K(table_schemas));
        } else if (OB_FAIL(table_schemas.push_back(container_table_schema))) {
          LOG_WARN("push_back failed", KR(ret));
        } else {
          for (int64_t i = 0; OB_SUCC(ret) && (i < lob_schemas.count()); ++i) {
            if (OB_FAIL(table_schemas.push_back(lob_schemas.at(i)))) {
              LOG_WARN("failed to push back", KR(ret));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObRootService::create_table(const ObCreateTableArg &arg, ObCreateTableRes &res)
{
  LOG_DEBUG("receive create table arg", K(arg));
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  LOG_INFO("receive create table ddl", K(begin_time));
  RS_TRACE(create_table_begin);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObArray<ObTableSchema> table_schemas;
    ObSchemaGetterGuard schema_guard;
    const ObDatabaseSchema *db_schema = NULL;
    schema_guard.set_session_id(arg.schema_.get_session_id());
    ObSchemaService *schema_service = schema_service_->get_schema_service();
    ObTableSchema table_schema;
    bool is_oracle_mode = false;
    int64_t ddl_task_id = 0;
    // generate base table schema
    if (OB_FAIL(table_schema.assign(arg.schema_))) {
      LOG_WARN("fail to assign schema", K(ret));
    } else if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_service is null", KP(schema_service), K(ret));
    } else if (OB_FAIL(generate_table_schema_in_tenant_space(arg, table_schema))) {
      LOG_WARN("fail to generate table schema in tenant space", K(ret), K(arg));
    } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(
               table_schema.get_tenant_id(), schema_guard))) {
      LOG_WARN("get_schema_guard with version in inner table failed", K(ret));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", K(ret));
    } else if (OB_FAIL(ObCompatModeGetter::check_is_oracle_mode_with_tenant_id(
                table_schema.get_tenant_id(), is_oracle_mode))) {
      LOG_WARN("fail to check is oracle mode", K(ret));
    } else if (OB_INVALID_ID == table_schema.get_database_id()) {
      ObString database_name = arg.db_name_;
      if (OB_FAIL(schema_guard.get_database_schema(table_schema.get_tenant_id(),
                                                   database_name,
                                                   db_schema))) {
        LOG_WARN("get databas schema failed", K(arg));
      } else if (OB_ISNULL(db_schema)) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
      } else if (!arg.is_inner_ && db_schema->is_in_recyclebin()) {
        ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
        LOG_WARN("Can't not create table of db in recyclebin", K(ret), K(arg), K(*db_schema));
      } else if (OB_INVALID_ID == db_schema->get_database_id()) {
        ret = OB_ERR_BAD_DATABASE;
        LOG_WARN("database id is invalid", "tenant_id",
                 table_schema.get_tenant_id(), K(database_name), K(*db_schema), K(ret));
      } else {
        table_schema.set_database_id(db_schema->get_database_id());
      }
    } else {
      // for view, database_id must be filled
    }
    if (OB_SUCC(ret)) {
      bool table_exist = false;
      ObSchemaGetterGuard::CheckTableType check_type = ObSchemaGetterGuard::ALL_NON_HIDDEN_TYPES;
      if (table_schema.is_mysql_tmp_table()) {
        check_type = ObSchemaGetterGuard::TEMP_TABLE_TYPE;
      } else if (0 == table_schema.get_session_id()) {
        //if session_id <> 0 during create table, need to exclude the existence of temporary table with the same table_name,
        //if there is, need to throw error.
        check_type = ObSchemaGetterGuard::NON_TEMP_WITH_NON_HIDDEN_TABLE_TYPE;
      }

      if (OB_SUCC(ret)) {
        ObArray<ObSchemaType> conflict_schema_types;
        if (!arg.is_alter_view_
            && OB_FAIL(schema_guard.check_oracle_object_exist(table_schema.get_tenant_id(),
                   table_schema.get_database_id(), table_schema.get_table_name_str(),
                   TABLE_SCHEMA, INVALID_ROUTINE_TYPE, arg.if_not_exist_, conflict_schema_types))) {
          LOG_WARN("fail to check oracle_object exist", K(ret), K(table_schema));
        } else if (conflict_schema_types.count() > 0) {
          ret = OB_ERR_EXIST_OBJECT;
          LOG_WARN("Name is already used by an existing object",
                   K(ret), K(table_schema), K(conflict_schema_types));
        }
      }
      if (OB_FAIL(schema_guard.check_table_exist(table_schema.get_tenant_id(),
                                                        table_schema.get_database_id(),
                                                        table_schema.get_table_name_str(),
                                                        false, /*is index*/
                                                        check_type,
                                                        table_exist))) {
        LOG_WARN("check table exist failed", K(ret), K(table_schema));
      } else if (table_exist) {
        const ObSimpleTableSchemaV2 *simple_table_schema = nullptr;
        if (OB_FAIL(schema_guard.get_simple_table_schema(
                    table_schema.get_tenant_id(),
                    table_schema.get_database_id(),
                    table_schema.get_table_name_str(),
                    false, /*is index*/
                    simple_table_schema))) {
          LOG_WARN("failed to get table schema", KR(ret), K(table_schema.get_tenant_id()),
                   K(table_schema.get_database_id()), K(table_schema.get_table_name_str()));
        } else if (OB_ISNULL(simple_table_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("simple_table_schema is null", KR(ret));
        } else if (table_schema.is_view_table() && arg.if_not_exist_) {
          //create or replace view ...
          //create user table will drop the old view and recreate it in trans
          if ((simple_table_schema->get_table_type() == SYSTEM_VIEW && GCONF.enable_sys_table_ddl)
                     || simple_table_schema->get_table_type() == USER_VIEW
                     || simple_table_schema->get_table_type() == MATERIALIZED_VIEW) {
          } else if (simple_table_schema->get_table_type() == SYSTEM_VIEW) {
            ret = OB_OP_NOT_ALLOW;
            LOG_WARN("not allowed to replace sys view when enable_sys_table_ddl is false", KR(ret), KPC(simple_table_schema));
            LOG_USER_ERROR(OB_OP_NOT_ALLOW, "replace sys view when enable_sys_table_ddl is false");
          } else {
            if (is_oracle_mode) {
              ret = OB_ERR_EXIST_OBJECT;
              LOG_WARN("name is already used by an existing object",
                       K(ret), K(table_schema.get_table_name_str()));
            } else { // mysql mode
              const ObDatabaseSchema *db_schema = nullptr;
              if (OB_FAIL(schema_guard.get_database_schema(
                          table_schema.get_tenant_id(),
                          table_schema.get_database_id(),
                          db_schema))) {
                LOG_WARN("get db schema failed", K(ret), K(table_schema.get_database_id()));
              } else if (OB_ISNULL(db_schema)) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("db schema is null", K(ret));
              } else {
                ret = OB_ERR_WRONG_OBJECT;
                ObCStringHelper helper;
                LOG_USER_ERROR(OB_ERR_WRONG_OBJECT,
                    helper.convert(db_schema->get_database_name_str()),
                    helper.convert(table_schema.get_table_name_str()), "VIEW");
                LOG_WARN("table exist", K(ret), K(table_schema));
              }
            }
          }
        } else {
          res.table_id_ = simple_table_schema->get_table_id();
          res.schema_version_ = simple_table_schema->get_schema_version();
          ret = OB_ERR_TABLE_EXIST;
          LOG_WARN("table exist", K(ret), K(table_schema), K(arg.if_not_exist_));
        }
      } else if (!table_exist && table_schema.is_view_table() && arg.is_alter_view_) {
        // the origin view must exist while alter view
        const ObSimpleDatabaseSchema *simple_db_schema = nullptr;
        if (OB_FAIL(schema_guard.get_database_schema(
                    table_schema.get_tenant_id(),
                    table_schema.get_database_id(),
                    simple_db_schema))) {
          LOG_WARN("get db schema failed", K(ret), K(table_schema.get_database_id()));
        } else if (OB_ISNULL(simple_db_schema)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("db schema is null", K(ret));
        } else {
          ret = OB_TABLE_NOT_EXIST;
          ObCStringHelper helper;
          LOG_USER_ERROR(OB_TABLE_NOT_EXIST,
                         helper.convert(simple_db_schema->get_database_name_str()),
                         helper.convert(table_schema.get_table_name_str()));
          LOG_WARN("table not exist", K(ret), K(table_schema));
        }
      }
    }
    RS_TRACE(generate_schema_start);
    //bool can_hold_new_table = false;
    common::hash::ObHashMap<ObString, uint64_t> mock_fk_parent_table_map; // name, count
    ObArray<ObMockFKParentTableSchema> tmp_mock_fk_parent_table_schema_array;
    ObArray<ObMockFKParentTableSchema> mock_fk_parent_table_schema_array;
    if (OB_FAIL(ret)) {
      //do nothing
    } else if (OB_FAIL(mock_fk_parent_table_map.create(16, "MockFKParentTbl"))) {
      LOG_WARN("fail to create mock_fk_parent_table_map", K(ret));
    } else if (OB_FAIL(ddl_service_.generate_schema(arg, table_schema))) {
      LOG_WARN("generate_schema for table failed", K(ret));
      //} else if (OB_FAIL(check_rs_capacity(table_schema, can_hold_new_table))) {
      //  LOG_WARN("fail to check rs capacity", K(ret), K(table_schema));
      //} else if (!can_hold_new_table) {
      //  ret = OB_PARTITION_CNT_REACH_ROOTSERVER_LIMIT;
      //  LOG_WARN("reach rs's limits, rootserver can only hold limited replicas");
    } else if (OB_FAIL(table_schemas.push_back(table_schema))) {
      LOG_WARN("push_back failed", K(ret));
    } else if (OB_FAIL(gen_container_table_schema_(arg, schema_guard, table_schema, table_schemas))) {
      LOG_WARN("fail to gen container table schema", KR(ret));
    }

    if (OB_SUCC(ret)) {
      RS_TRACE(generate_schema_index);
      res.table_id_ = table_schema.get_table_id();
      // generate index schemas
      ObIndexBuilder index_builder(ddl_service_);
      ObTableSchema index_schema;
      for (int64_t i = 0; OB_SUCC(ret) && i < arg.index_arg_list_.size(); ++i) {
        index_schema.reset();
        ObCreateIndexArg &index_arg = const_cast<ObCreateIndexArg&>(arg.index_arg_list_.at(i));
        //if we pass the table_schema argument, the create_index_arg can not set database_name
        //and table_name, which will used from get data table schema in generate_schema
        if (!index_arg.index_schema_.is_partitioned_table()
            && !table_schema.is_partitioned_table()
            && !table_schema.is_auto_partitioned_table()) {
          if (INDEX_TYPE_NORMAL_GLOBAL == index_arg.index_type_) {
            index_arg.index_type_ = INDEX_TYPE_NORMAL_GLOBAL_LOCAL_STORAGE;
          } else if (INDEX_TYPE_UNIQUE_GLOBAL == index_arg.index_type_) {
            index_arg.index_type_ = INDEX_TYPE_UNIQUE_GLOBAL_LOCAL_STORAGE;
          } else if (INDEX_TYPE_SPATIAL_GLOBAL == index_arg.index_type_) {
            index_arg.index_type_ = INDEX_TYPE_SPATIAL_GLOBAL_LOCAL_STORAGE;
          } else if (is_global_fts_index(index_arg.index_type_)) {
            if (index_arg.index_type_ == INDEX_TYPE_DOC_ID_ROWKEY_GLOBAL) {
              index_arg.index_type_ = INDEX_TYPE_DOC_ID_ROWKEY_GLOBAL_LOCAL_STORAGE;
            } else if (index_arg.index_type_ == INDEX_TYPE_FTS_INDEX_GLOBAL) {
              index_arg.index_type_ = INDEX_TYPE_FTS_INDEX_GLOBAL_LOCAL_STORAGE;
            } else if (index_arg.index_type_ == INDEX_TYPE_FTS_DOC_WORD_GLOBAL) {
              index_arg.index_type_ = INDEX_TYPE_FTS_DOC_WORD_GLOBAL_LOCAL_STORAGE;
            }
          }
        }
        // the global index has generated column schema during resolve, RS no need to generate index schema,
        // just assign column schema
        if (INDEX_TYPE_NORMAL_GLOBAL == index_arg.index_type_
            || INDEX_TYPE_UNIQUE_GLOBAL == index_arg.index_type_
            || INDEX_TYPE_SPATIAL_GLOBAL == index_arg.index_type_) {
          if (OB_FAIL(index_schema.assign(index_arg.index_schema_))) {
            LOG_WARN("fail to assign schema", K(ret));
          }
        }
        const bool global_index_without_column_info = false;
        ObSEArray<ObColumnSchemaV2 *, 1> gen_columns;
        ObIAllocator *allocator = index_arg.index_schema_.get_allocator();
        if (OB_FAIL(ret)) {
        } else if (OB_ISNULL(allocator)) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("invalid allocator", K(ret));
        } else if (OB_FAIL(ObIndexBuilderUtil::adjust_expr_index_args(index_arg, table_schema, *allocator, gen_columns))) {
            LOG_WARN("fail to adjust expr index args", K(ret));
        } else if (OB_FAIL(index_builder.generate_schema(index_arg,
                                                         table_schema,
                                                         global_index_without_column_info,
                                                         true, /*generate_id*/
                                                         index_schema))) {
          LOG_WARN("generate_schema for index failed", K(index_arg), K(table_schema), K(ret));
        }
        if (OB_SUCC(ret)) {
          uint64_t new_table_id = OB_INVALID_ID;
          if (OB_FAIL(schema_service->fetch_new_table_id(table_schema.get_tenant_id(), new_table_id))) {
            LOG_WARN("failed to fetch_new_table_id", "tenant_id", table_schema.get_tenant_id(), K(ret));
          } else {
            index_schema.set_table_id(new_table_id);
            //index_schema.set_data_table_id(table_id);
            if (OB_FAIL(table_schemas.push_back(index_schema))) {
              LOG_WARN("push_back failed", K(ret));
            }
          }
        }
      }
      RS_TRACE(generate_schema_lob);
      if (OB_FAIL(ret) || table_schema.is_view_table() || table_schema.is_external_table()) {
        // do nothing
      } else if (OB_FAIL(ddl_service_.build_aux_lob_table_schema_if_need(table_schema, table_schemas))) {
        LOG_WARN("fail to build_aux_lob_table_schema_if_need", K(ret), K(table_schema));
      }
      if (OB_SUCC(ret)) {
        for (int64_t i = 0; OB_SUCC(ret) && i < arg.foreign_key_arg_list_.count(); i++) {
          const ObCreateForeignKeyArg &foreign_key_arg = arg.foreign_key_arg_list_.at(i);
          ObForeignKeyInfo foreign_key_info;
          // check for duplicate constraint names of foregin key
          if (foreign_key_arg.foreign_key_name_.empty()) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("fk name is empty", K(ret));
          } else {
            bool is_foreign_key_name_exist = true;
            if (OB_FAIL(ddl_service_.check_constraint_name_is_exist(
                        schema_guard, table_schema, foreign_key_arg.foreign_key_name_, true, is_foreign_key_name_exist))) {
              LOG_WARN("fail to check foreign key name is exist or not", K(ret), K(foreign_key_arg.foreign_key_name_));
            } else if(is_foreign_key_name_exist) {
              if (is_oracle_mode) {
                ret = OB_ERR_CONSTRAINT_NAME_DUPLICATE;
                LOG_WARN("fk name is duplicate", K(ret), K(foreign_key_arg.foreign_key_name_));
              } else { // mysql mode
                ret = OB_ERR_DUP_KEY;
                LOG_USER_ERROR(OB_ERR_DUP_KEY,
                    table_schema.get_table_name_str().length(),
                    table_schema.get_table_name_str().ptr());
              }
            }
          }
          // end of check for duplicate constraint names of foregin key
          const ObTableSchema *parent_schema = NULL;
          if (OB_SUCC(ret)) {
            // get parent table schema.
            // TODO: is it necessory to determine whether it is case sensitive by check sys variable
            // check whether it belongs to self reference, if so, the parent schema is child schema.
            
            if (0 == foreign_key_arg.parent_table_.case_compare(table_schema.get_table_name_str())
                  && 0 == foreign_key_arg.parent_database_.case_compare(arg.db_name_)) {
              parent_schema = &table_schema;
              if (FK_REF_TYPE_PRIMARY_KEY == foreign_key_arg.fk_ref_type_) {
                if (is_oracle_mode) {
                  for (ObTableSchema::const_constraint_iterator iter = parent_schema->constraint_begin(); iter != parent_schema->constraint_end(); ++iter) {
                    if (CONSTRAINT_TYPE_PRIMARY_KEY == (*iter)->get_constraint_type()) {
                      foreign_key_info.fk_ref_type_ = FK_REF_TYPE_PRIMARY_KEY;
                      foreign_key_info.ref_cst_id_ = (*iter)->get_constraint_id();
                      break;
                    }
                  }
                } else {
                  foreign_key_info.fk_ref_type_ = FK_REF_TYPE_PRIMARY_KEY;
                  foreign_key_info.ref_cst_id_ = common::OB_INVALID_ID;
                }
              } else if (FK_REF_TYPE_UNIQUE_KEY == foreign_key_arg.fk_ref_type_) {
                if (OB_FAIL(ddl_service_.get_uk_cst_id_for_self_ref(table_schemas, foreign_key_arg, foreign_key_info))) {
                  LOG_WARN("failed to get uk cst id for self ref", K(ret), K(foreign_key_arg));
                }
              } else if (!lib::is_oracle_mode() && FK_REF_TYPE_NON_UNIQUE_KEY == foreign_key_arg.fk_ref_type_) {
                if (OB_FAIL(ddl_service_.get_index_cst_id_for_self_ref(table_schemas, foreign_key_arg, foreign_key_info))) {
                  LOG_WARN("failed to get index cst id for self ref", K(ret), K(foreign_key_arg));
                }
              } else {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("invalid foreign key fk ref type", K(ret), K(foreign_key_arg));
              }
            } else if (OB_FAIL(schema_guard.get_table_schema(table_schema.get_tenant_id(),
                                                             foreign_key_arg.parent_database_,
                                                             foreign_key_arg.parent_table_,
                                                             false, parent_schema))) {
              LOG_WARN("failed to get parent table schema", K(ret), K(foreign_key_arg));
            } else {
              foreign_key_info.fk_ref_type_ = foreign_key_arg.fk_ref_type_;
              foreign_key_info.ref_cst_id_ = foreign_key_arg.ref_cst_id_;
            }
          }
          const ObMockFKParentTableSchema *tmp_mock_fk_parent_table_ptr = NULL;
          ObMockFKParentTableSchema mock_fk_parent_table_schema;
          if (OB_SUCC(ret)) {
            if (foreign_key_arg.is_parent_table_mock_) {
              uint64_t dup_name_mock_fk_parent_table_count = 0;
              if (NULL != parent_schema) {
                ret = OB_ERR_PARALLEL_DDL_CONFLICT;
                LOG_WARN("the mock parent table is conflict with the real parent table, need retry",
                    K(ret), K(foreign_key_arg), K(parent_schema->get_table_id()));
              } else if (OB_FAIL(mock_fk_parent_table_map.get_refactored(foreign_key_arg.parent_table_, dup_name_mock_fk_parent_table_count))) {
                if (OB_HASH_NOT_EXIST == ret) {
                  ret = OB_SUCCESS;
                  if (OB_FAIL(mock_fk_parent_table_map.set_refactored(foreign_key_arg.parent_table_, ++dup_name_mock_fk_parent_table_count))) {
                    LOG_WARN("failed to insert into mock_fk_parent_table_map", K(ret), K(foreign_key_arg), K(dup_name_mock_fk_parent_table_count));
                  }
                } else {
                  LOG_WARN("get_refactored from mock_fk_parent_table_map failed", K(ret), K(foreign_key_arg));
                }
              } else {
                //already had dup name mock_fk_parent_table in tmp_mock_fk_parent_table_schema_array
                int64_t count = 0;
                for (int64_t i = 0; i < tmp_mock_fk_parent_table_schema_array.count(); ++i) {
                  if (0 == tmp_mock_fk_parent_table_schema_array.at(i).get_mock_fk_parent_table_name().case_compare(foreign_key_arg.parent_table_)) {
                    if (++count == dup_name_mock_fk_parent_table_count) {
                      tmp_mock_fk_parent_table_ptr = &tmp_mock_fk_parent_table_schema_array.at(i);
                      break;
                    }
                  }
                }
                if (OB_ISNULL(tmp_mock_fk_parent_table_ptr)) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("tmp_mock_fk_parent_table_ptr is null", K(ret), K(foreign_key_arg), K(tmp_mock_fk_parent_table_schema_array));
                } else if (OB_FAIL(mock_fk_parent_table_map.set_refactored(foreign_key_arg.parent_table_, ++dup_name_mock_fk_parent_table_count, true/*overwrite*/))) {
                  LOG_WARN("failed to insert into mock_fk_parent_table_map", K(ret), K(foreign_key_arg), K(dup_name_mock_fk_parent_table_count));
                }
              }
              if (OB_FAIL(ret)) {
              } else if (OB_FAIL(ddl_service_.gen_mock_fk_parent_table_for_create_fk(
                         schema_guard, table_schema.get_tenant_id(), foreign_key_arg, tmp_mock_fk_parent_table_ptr, foreign_key_info, mock_fk_parent_table_schema))) {
                LOG_WARN("failed to generate_mock_fk_parent_table_schema", K(ret), K(table_schema.get_tenant_id()), K(foreign_key_arg));
              }
            } else if (OB_ISNULL(parent_schema)) {
              ret = OB_TABLE_NOT_EXIST;
              LOG_WARN("parent table is not exist", K(ret), K(foreign_key_arg));
            } else if (false == parent_schema->is_tmp_table()
                           && 0 != parent_schema->get_session_id()
                           && OB_INVALID_ID != schema_guard.get_session_id()) {
              ret = OB_TABLE_NOT_EXIST;
              ObCStringHelper helper;
              LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(foreign_key_arg.parent_database_),
                  helper.convert(foreign_key_arg.parent_table_));
            } else if (!arg.is_inner_ && parent_schema->is_in_recyclebin()) {
              ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
              LOG_WARN("parent table is in recyclebin", K(ret), K(foreign_key_arg));
            } else if (parent_schema->get_table_id() != table_schema.get_table_id()) {
              // no need to update sync_versin_for_cascade_table while the refrence table is itself
              if (OB_FAIL(table_schema.add_depend_table_id(parent_schema->get_table_id()))) {
                LOG_WARN("failed to add depend table id", K(ret), K(foreign_key_arg));
              }
            }
          }
          // get child column schema.
          if (OB_SUCC(ret)) {
            foreign_key_info.child_table_id_ = res.table_id_;
            foreign_key_info.parent_table_id_ = foreign_key_arg.is_parent_table_mock_ ? mock_fk_parent_table_schema.get_mock_fk_parent_table_id() : parent_schema->get_table_id();
            for (int64_t j = 0; OB_SUCC(ret) && j < foreign_key_arg.child_columns_.count(); j++) {
              const ObString &column_name = foreign_key_arg.child_columns_.at(j);
              const ObColumnSchemaV2 *column_schema = table_schema.get_column_schema(column_name);
              if (OB_ISNULL(column_schema)) {
                ret = OB_ERR_COLUMN_NOT_FOUND;
                LOG_WARN("child column is not exist", K(ret), K(column_name));
              } else if (OB_FAIL(foreign_key_info.child_column_ids_.push_back(column_schema->get_column_id()))) {
                LOG_WARN("failed to push child column id", K(ret), K(column_name));
              }
            }
          }
          // get parent column schema.
          if (OB_SUCC(ret) && !foreign_key_arg.is_parent_table_mock_) {
            for (int64_t j = 0; OB_SUCC(ret) && j < foreign_key_arg.parent_columns_.count(); j++) {
              const ObString &column_name = foreign_key_arg.parent_columns_.at(j);
              const ObColumnSchemaV2 *column_schema = parent_schema->get_column_schema(column_name);
              if (OB_ISNULL(column_schema)) {
                ret = OB_ERR_COLUMN_NOT_FOUND;
                LOG_WARN("parent column is not exist", K(ret), K(column_name));
              } else if (OB_FAIL(foreign_key_info.parent_column_ids_.push_back(column_schema->get_column_id()))) {
                LOG_WARN("failed to push parent column id", K(ret), K(column_name));
              }
            }
          }
          // get reference option and foreign key name.
          if (OB_SUCC(ret)) {
            foreign_key_info.update_action_ = foreign_key_arg.update_action_;
            foreign_key_info.delete_action_ = foreign_key_arg.delete_action_;
            foreign_key_info.foreign_key_name_ = foreign_key_arg.foreign_key_name_;
            foreign_key_info.enable_flag_ = foreign_key_arg.enable_flag_;
            foreign_key_info.validate_flag_ = foreign_key_arg.validate_flag_;
            foreign_key_info.rely_flag_ = foreign_key_arg.rely_flag_;
            foreign_key_info.is_parent_table_mock_ = foreign_key_arg.is_parent_table_mock_;
            foreign_key_info.name_generated_type_ = foreign_key_arg.name_generated_type_;
          }
          // add foreign key info.
          if (OB_SUCC(ret)) {
            if (OB_FAIL(schema_service->fetch_new_constraint_id(table_schema.get_tenant_id(),
                                                                foreign_key_info.foreign_key_id_))) {
              LOG_WARN("failed to fetch new foreign key id", K(ret), K(foreign_key_arg));
            } else if (OB_FAIL(table_schema.add_foreign_key_info(foreign_key_info))) {
              LOG_WARN("failed to push foreign key info", K(ret), K(foreign_key_info));
            } else if (foreign_key_info.is_parent_table_mock_
                       && MOCK_FK_PARENT_TABLE_OP_INVALID != mock_fk_parent_table_schema.get_operation_type()) {
              if (OB_FAIL(mock_fk_parent_table_schema.add_foreign_key_info(foreign_key_info))) {
                LOG_WARN("failed to push foreign key info", K(ret), K(foreign_key_info));
              } else if (ObMockFKParentTableOperationType::MOCK_FK_PARENT_TABLE_OP_CREATE_TABLE_BY_ADD_FK_IN_CHILD_TBALE == mock_fk_parent_table_schema.get_operation_type()) {
                if (OB_FAIL(tmp_mock_fk_parent_table_schema_array.push_back(mock_fk_parent_table_schema))) {
                  LOG_WARN("failed to push mock_fk_parent_table_schema to tmp_mock_fk_parent_table_schema_array", K(ret), K(mock_fk_parent_table_schema));
                }
              } else { // ObMockFKParentTableOperationType::MOCK_FK_PARENT_TABLE_OP_CREATE_TABLE_BY_ADD_FK_IN_CHILD_TBALE != mock_fk_parent_table_schema.get_operation_type()
                if (OB_FAIL(mock_fk_parent_table_schema_array.push_back(mock_fk_parent_table_schema))) {
                  LOG_WARN("failed to push mock_fk_parent_table_schema to mock_fk_parent_table_schema_array", K(ret), K(mock_fk_parent_table_schema));
                } else if (OB_FAIL(mock_fk_parent_table_map.erase_refactored(mock_fk_parent_table_schema.get_mock_fk_parent_table_name()))) {
                  LOG_WARN("failed to delete from mock_fk_parent_table_map", K(ret), K(mock_fk_parent_table_schema.get_mock_fk_parent_table_name()));
                }
              }
            }
          }
        } // for
        if (OB_SUCC(ret)) {
          // push back to mock_fk_parent_table_schema_array with the last one of all dup name mock_fk_parent_table_schema
          if (!tmp_mock_fk_parent_table_schema_array.empty()) {
            for (int64_t i = 0; OB_SUCC(ret) && i < tmp_mock_fk_parent_table_schema_array.count(); ++i) {
              uint64_t dup_name_mock_fk_parent_table_count = 0;
              ObString mock_fk_parent_table_name;
              if (OB_FAIL(mock_fk_parent_table_map.get_refactored(tmp_mock_fk_parent_table_schema_array.at(i).get_mock_fk_parent_table_name(), dup_name_mock_fk_parent_table_count))) {
                if (OB_HASH_NOT_EXIST == ret) {
                  ret = OB_SUCCESS;
                  continue;
                } else {
                  LOG_WARN("get_refactored from mock_fk_parent_table_map failed", K(ret), K(tmp_mock_fk_parent_table_schema_array.at(i)));
                }
              } else {
                mock_fk_parent_table_name = tmp_mock_fk_parent_table_schema_array.at(i).get_mock_fk_parent_table_name();
                int64_t j = i;
                uint64_t count = 0;
                for (; count < dup_name_mock_fk_parent_table_count && j < tmp_mock_fk_parent_table_schema_array.count(); ++j) {
                  if (0 == mock_fk_parent_table_name.case_compare(tmp_mock_fk_parent_table_schema_array.at(j).get_mock_fk_parent_table_name())) {
                    ++count;
                  }
                }
                if (--j >= tmp_mock_fk_parent_table_schema_array.count()) {
                  ret = OB_ERR_UNEXPECTED;
                  LOG_WARN("j >= tmp_mock_fk_parent_table_schema_array.count()", K(ret), K(j), K(tmp_mock_fk_parent_table_schema_array.count()));
                } else {
                  for (int64_t k = 0; OB_SUCC(ret) && k < tmp_mock_fk_parent_table_schema_array.at(j).get_foreign_key_infos().count(); ++k) {
                    tmp_mock_fk_parent_table_schema_array.at(j).get_foreign_key_infos().at(k).parent_table_id_ = tmp_mock_fk_parent_table_schema_array.at(j).get_mock_fk_parent_table_id();
                  }
                }
                if (OB_FAIL(ret)) {
                } else if (OB_FAIL(mock_fk_parent_table_schema_array.push_back(tmp_mock_fk_parent_table_schema_array.at(j)))) {
                  LOG_WARN("fail to push back to mock_fk_parent_table_schema_array", K(ret), K(tmp_mock_fk_parent_table_schema_array.at(j)));
                } else if (OB_FAIL(mock_fk_parent_table_map.erase_refactored(mock_fk_parent_table_name))) {
                  LOG_WARN("failed to delete from mock_fk_parent_table_map", K(mock_fk_parent_table_name), K(ret));
                }
              }
            }
          }
        }
        if (OB_SUCC(ret)) {
          // deal with new table name which is the same to mock_fk_parent_table_name, replace mock_parent_table with this new table
          const ObMockFKParentTableSchema *ori_mock_parent_table_schema_ptr = NULL;
          if (OB_FAIL(schema_guard.get_mock_fk_parent_table_schema_with_name(
              table_schema.get_tenant_id(),
              table_schema.get_database_id(),
              table_schema.get_table_name_str(),
              ori_mock_parent_table_schema_ptr))) {
            LOG_WARN("failed to check_mock_fk_parent_table_exist_with_name");
          } else if (OB_NOT_NULL(ori_mock_parent_table_schema_ptr)) {
            ObMockFKParentTableSchema mock_fk_parent_table_schema;
            ObArray<const share::schema::ObTableSchema*> index_schemas;
            for (int64_t i = 1; OB_SUCC(ret) && i < table_schemas.count(); ++i) {
              if (table_schemas.at(i).is_unique_index()
                  && OB_FAIL(index_schemas.push_back(&table_schemas.at(i)))) {
                LOG_WARN("failed to push back index_schemas", K(ret));
              }
            }
            if (FAILEDx(ddl_service_.gen_mock_fk_parent_table_for_replacing_mock_fk_parent_table(
                schema_guard, ori_mock_parent_table_schema_ptr->get_mock_fk_parent_table_id(), table_schema, index_schemas,
                mock_fk_parent_table_schema))) {
              LOG_WARN("failed to gen_mock_fk_parent_table_for_replacing_mock_fk_parent_table", K(ret));
            } else if (OB_FAIL(mock_fk_parent_table_schema_array.push_back(mock_fk_parent_table_schema))) {
              LOG_WARN("failed to push mock_fk_parent_table_schema", K(ret), K(mock_fk_parent_table_schema));
            }
          }
        }
      } // check foreign key info end.

      if (OB_SUCC(ret)) {
        if (OB_FAIL(ObTTLUtil::check_htable_ddl_supported(schema_guard, table_schema.get_tenant_id(), arg.dep_infos_))) {
          LOG_WARN("failed to check htable ddl supported", K(ret), "tenant_id", table_schema.get_tenant_id(), K(arg.dep_infos_));
        }
      }
    }
    RS_TRACE(generate_schema_finish);
    if (OB_SUCC(ret)) {
      //table schema may be updated during analyse index schema, so reset table_schema
      const bool is_standby = PRIMARY_CLUSTER != ObClusterInfoGetter::get_cluster_role_v2();
      if (OB_FAIL(table_schemas.at(0).assign(table_schema))) {
        LOG_WARN("fail to assign schema", K(ret));
      } else if (OB_FAIL(ddl_service_.create_user_tables(
                                      arg.if_not_exist_,
                                      arg.ddl_stmt_str_,
                                      arg.error_info_,
                                      table_schemas,
                                      schema_guard,
                                      arg.sequence_ddl_arg_,
                                      arg.last_replay_log_id_,
                                      &arg.dep_infos_,
                                      mock_fk_parent_table_schema_array,
                                      ddl_task_id))) {
        LOG_WARN("create_user_tables failed", "if_not_exist", arg.if_not_exist_,
                 "ddl_stmt_str", arg.ddl_stmt_str_, K(ret));
      }
    }
    if (OB_ERR_TABLE_EXIST == ret) {
      //create table xx if not exist (...)
      //create or replace view xx as ...
      if (arg.if_not_exist_) {
        res.do_nothing_ = true;
        ret = OB_SUCCESS;
        LOG_INFO("table is exist, no need to create again, ",
                 "tenant_id", table_schema.get_tenant_id(),
                 "database_id", table_schema.get_database_id(),
                 "table_name", table_schema.get_table_name());
      } else {
        ret = OB_ERR_TABLE_EXIST;
        LOG_USER_ERROR(OB_ERR_TABLE_EXIST, table_schema.get_table_name_str().length(),
            table_schema.get_table_name_str().ptr());
        LOG_WARN("table is exist, cannot create it twice,",
                 "tenant_id", table_schema.get_tenant_id(),
                 "database_id", table_schema.get_database_id(),
                 "table_name", table_schema.get_table_name(), K(ret));
      }
    }
    // check vertical partition
    // is_primary_vp_table()
    // get_aux_vp_tid_array()
    // is_aux_vp_table()
    // get_vp_store_column_ids
    // get_vp_column_ids_without_rowkey
    if (OB_SUCC(ret)) {
      ObSchemaGetterGuard new_schema_guard;
      const ObTableSchema *new_table_schema = NULL;
      const uint64_t arg_vp_cnt = arg.vertical_partition_arg_list_.count();

      if (arg_vp_cnt == 0) {
        LOG_INFO("avg_vp_cnt is 0");
        // do-nothing
      } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(
                         table_schema.get_tenant_id(), new_schema_guard))) {
        LOG_WARN("fail to get schema guard with version in inner table",
                 K(ret), K(table_schema.get_tenant_id()));
      } else if (OB_FAIL(new_schema_guard.get_table_schema(table_schema.get_tenant_id(),
                                                           table_schema.get_table_id(),
                                                           new_table_schema))) {
        LOG_WARN("fail to get table schema", K(ret), K(table_schema));
      } else if (NULL == new_table_schema) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("NULL ptr", K(ret));
      } else if (!new_table_schema->is_primary_vp_table()) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("is_primary_vp_table is invalid", K(ret), K(arg_vp_cnt), K(new_table_schema->is_primary_vp_table()));
      } else {
        ObSEArray<uint64_t, 16> aux_vp_tid_array;
        if (OB_FAIL(new_table_schema->get_aux_vp_tid_array(aux_vp_tid_array))) {
          LOG_WARN("failed to get_aux_vp_tid_array", K(*new_table_schema));
        } else if (!((arg_vp_cnt == (aux_vp_tid_array.count()+ 1)
                      || (arg_vp_cnt == aux_vp_tid_array.count())))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("arg_vp_cnt is not equal to aux_vp_cnt_ or (aux_vp_cnt_+1)",
                   K(ret), K(arg_vp_cnt), K(aux_vp_tid_array.count()));
        } else {
          // check primary partition table include get_vp_store_column_ids and vertical partition column information
          ObArray<share::schema::ObColDesc> columns;
          const ObColumnSchemaV2 *column_schema = NULL;
          const ObCreateVertialPartitionArg primary_vp_arg = arg.vertical_partition_arg_list_.at(0);
          int64_t arg_pri_vp_col_cnt = primary_vp_arg.vertical_partition_columns_.count();
          if (OB_FAIL(new_table_schema->get_vp_store_column_ids(columns))) {
            LOG_WARN("get_vp_store_column_ids failed", K(ret));
          }
          for (int64_t i = 0; OB_SUCC(ret) && i < columns.count(); ++i) {
            LOG_INFO("column info", K(columns.at(i).col_id_), K(columns.at(i).col_type_));
            if (NULL == (column_schema = new_table_schema->get_column_schema(columns.at(i).col_id_))) {
              ret = OB_ERR_BAD_FIELD_ERROR;
              LOG_WARN("get_column_schema failed", K(columns.at(i)), K(ret));
            } else {
              ObString column_name = column_schema->get_column_name();
              LOG_INFO("column info", K(column_name),
                  K(column_schema->get_column_id()), K(column_schema->get_table_id()));
              if (column_schema->is_primary_vp_column()) {
                for (int64_t j = 0; OB_SUCC(ret) && j < primary_vp_arg.vertical_partition_columns_.count(); ++j) {
                  ObString pri_vp_col = primary_vp_arg.vertical_partition_columns_.at(j);
                  if (0 == column_name.case_compare(pri_vp_col)) {
                    arg_pri_vp_col_cnt--;
                    LOG_INFO("primary vp", K(column_name));
                    break;
                  }
                }
              } else {
                LOG_INFO("non-primary vp", K(column_name));
              }
            }
          }
          if (OB_SUCC(ret) && (0 != arg_pri_vp_col_cnt)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("mismatch primary vp column", K(ret));
            for (int64_t j = 0; j < arg_pri_vp_col_cnt; ++j) {
              ObString pri_vp_col = primary_vp_arg.vertical_partition_columns_.at(j);
              LOG_INFO("arg primary vp", K(pri_vp_col));
            }
          }

          // verify secondary partition table
          if (OB_SUCC(ret)) {
            int64_t N = aux_vp_tid_array.count();
            for (int64_t i = 0; OB_SUCC(ret) && i < N; i++) {
              const ObTableSchema *aux_vp_table_schema = NULL;
              ObArray<share::schema::ObColDesc> vp_columns;
              ObArray<share::schema::ObColDesc> store_columns;
              if (OB_FAIL(new_schema_guard.get_table_schema(table_schema.get_tenant_id(),
                          aux_vp_tid_array.at(i), aux_vp_table_schema))) {
                LOG_WARN("get_table_schema failed", "table id", aux_vp_tid_array.at(i), K(ret));
              } else if (NULL == aux_vp_table_schema) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("aux vp table is null", K(ret));
              } else if (!aux_vp_table_schema->is_aux_vp_table()
                  || AUX_VERTIAL_PARTITION_TABLE != aux_vp_table_schema->get_table_type()) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("aux vp table type is incorrect", K(ret), K(aux_vp_table_schema->is_aux_vp_table()));
              } else if (OB_FAIL(aux_vp_table_schema->get_vp_column_ids(vp_columns))) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("failed to get aux vp table columns", K(ret), K(*aux_vp_table_schema));
              } else if (OB_FAIL(aux_vp_table_schema->get_vp_store_column_ids(store_columns))) {
                ret = OB_ERR_UNEXPECTED;
                LOG_WARN("failed to get aux vp table columns", K(ret), K(*aux_vp_table_schema));
              } else {
                LOG_INFO("table info", K(aux_vp_table_schema->get_table_name()), K(aux_vp_table_schema->get_table_id()),
                        K(aux_vp_table_schema->get_data_table_id()), K(ret));
                const ObColumnSchemaV2 *column_schema = NULL;

                for (int64_t k = 0; OB_SUCC(ret) && k < vp_columns.count(); ++k) {
                  LOG_INFO("column info", K(vp_columns.at(k).col_id_), K(vp_columns.at(k).col_type_));
                  if (NULL == (column_schema = aux_vp_table_schema->get_column_schema(vp_columns.at(k).col_id_))) {
                    ret = OB_ERR_BAD_FIELD_ERROR;
                    LOG_WARN("get_column_schema failed", K(vp_columns.at(k)), K(ret));
                  } else {
                    LOG_INFO("column info", K(column_schema->get_column_name()), K(column_schema->get_column_id()),
                        K(column_schema->get_table_id()), K(ret));
                  }
                }
                // verify get_vp_store_column_ids return all vertical partition columns,
                // include vertical partition columns of primary key.
                for (int64_t k = 0; OB_SUCC(ret) && k < store_columns.count(); ++k) {
                  LOG_INFO("column info", K(store_columns.at(k).col_id_), K(store_columns.at(k).col_type_));
                  if (NULL == (column_schema = aux_vp_table_schema->get_column_schema(store_columns.at(k).col_id_))) {
                    ret = OB_ERR_BAD_FIELD_ERROR;
                    LOG_WARN("get_column_schema failed", K(store_columns.at(k)), K(ret));
                  } else {
                    LOG_INFO("column info", K(column_schema->get_column_name()), K(column_schema->get_column_id()),
                        K(column_schema->get_table_id()), K(ret));
                  }
                }
              }
            }
          }
        }
      }
    }
    if (OB_SUCC(ret)) {
      uint64_t tenant_id = table_schema.get_tenant_id();
      if (OB_FAIL(schema_service_->get_tenant_schema_version(tenant_id, res.schema_version_))) {
        LOG_WARN("failed to get tenant schema version", K(ret));
      } else {
        res.task_id_ = ddl_task_id;
      }
    }
  }

  RS_TRACE(create_table_end);
  FORCE_PRINT_TRACE(THE_RS_TRACE, "[create table]");
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "create table",
                        "tenant_id", arg.schema_.get_tenant_id(),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  LOG_INFO("finish create table ddl", K(ret), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

// create sys_table by specify table_id for tenant:
// 1. can not create table cross tenant except sys tenant.
// 2. part_type of sys table only support non-partition or only level hash_like part type.
// 3. sys table's tablegroup and database must be oceanbase
int ObRootService::generate_table_schema_in_tenant_space(
    const ObCreateTableArg &arg,
    ObTableSchema &table_schema)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.exec_tenant_id_;
  const uint64_t table_id = table_schema.get_table_id();
  const ObPartitionLevel part_level = table_schema.get_part_level();
  const ObPartitionFuncType part_func_type = table_schema.get_part_option().get_part_func_type();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == table_id || !is_inner_table(table_id)) {
    // skip
  } else if (OB_SYS_TENANT_ID != arg.exec_tenant_id_) {
    //FIXME: this restriction should be removed later.
    // only enable sys tenant create sys table
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("only sys tenant can create tenant space table", K(ret), K(arg));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "non-sys tenant creating system tables");
  } else if (table_schema.is_view_table()) {
    // no need specify tenant_id while specify table_id creating sys table
    if (OB_SYS_TENANT_ID != tenant_id) {
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("create sys view with ordinary tenant not allowed", K(ret), K(table_schema));
    }
  } else if (part_level > ObPartitionLevel::PARTITION_LEVEL_ONE
             || !is_hash_like_part(part_func_type)) {
    // sys tables do not write __all_part table, so sys table only support non-partition or only level hash_like part type.
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("sys table's partition option is invalid", K(ret), K(arg));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "invalid partition option to system table");
  } else if (0 != table_schema.get_tablegroup_name().case_compare(OB_SYS_TABLEGROUP_NAME)) {
    // sys tables's tablegroup must be oceanbase
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("sys table's tablegroup should be oceanbase", K(ret), K(arg));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "invalid tablegroup to system table");
  } else if (0 != arg.db_name_.case_compare(OB_SYS_DATABASE_NAME)) {
    // sys tables's database  must be oceanbase
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("sys table's database should be oceanbase", K(ret), K(arg));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "invalid database to sys table");
  } else {
    table_schema.set_tenant_id(tenant_id);
    table_schema.set_table_id(table_id);
    table_schema.set_tablegroup_id(OB_SYS_TABLEGROUP_ID);
    table_schema.set_tablegroup_name(OB_SYS_TABLEGROUP_NAME);
    table_schema.set_database_id(OB_SYS_DATABASE_ID);
  }
  return ret;
}

int ObRootService::fork_database(const obrpc::ObForkDatabaseArg &arg, obrpc::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.fork_database(arg, res))) {
    LOG_WARN("fork database failed", K(ret));
  }
  char database_names_buffer[512] = {0};
  snprintf(database_names_buffer, sizeof(database_names_buffer), "%.*s -> %.*s",
           static_cast<int>(arg.src_database_name_.length()), arg.src_database_name_.ptr(),
           static_cast<int>(arg.dst_database_name_.length()), arg.dst_database_name_.ptr());
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "fork database",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "databases", database_names_buffer);
  LOG_INFO("finish fork database ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::maintain_obj_dependency_info(const obrpc::ObDependencyObjDDLArg &arg)
{
  LOG_DEBUG("receive maintain obj dependency info arg", K(arg));
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.maintain_obj_dependency_info(arg))) {
    LOG_WARN("failed to maintain obj dependency info", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::mview_complete_refresh(const obrpc::ObMViewCompleteRefreshArg &arg,
                                          obrpc::ObMViewCompleteRefreshRes &res)
{
  LOG_DEBUG("receive mview complete refresh arg", K(arg));
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.tenant_id_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else {
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", KR(ret), K(tenant_id));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", KR(ret), K(arg));
    } else if (OB_FAIL(ddl_service_.mview_complete_refresh(arg, res, schema_guard))) {
      LOG_WARN("failed to mview complete refresh", KR(ret), K(arg));
    }
  }
  return ret;
}

int ObRootService::execute_ddl_task(const obrpc::ObAlterTableArg &arg,
                                    common::ObSArray<uint64_t> &obj_ids)
{
  LOG_DEBUG("receive execute ddl task arg", K(arg));
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    switch (arg.ddl_task_type_) {
      case share::REBUILD_INDEX_TASK: {
        if (OB_FAIL(ddl_service_.rebuild_hidden_table_index_in_trans(
            const_cast<obrpc::ObAlterTableArg &>(arg), obj_ids))) {
          LOG_WARN("failed to rebuild hidden table index in trans", K(ret));
        }
        break;
      }
      case share::REBUILD_CONSTRAINT_TASK: {
        if (OB_FAIL(ddl_service_.rebuild_hidden_table_constraints_in_trans(
            const_cast<obrpc::ObAlterTableArg &>(arg), obj_ids))) {
          LOG_WARN("failed to rebuild hidden table constraints in trans", K(ret));
        }
        break;
      }
      case share::REBUILD_FOREIGN_KEY_TASK: {
        if (OB_FAIL(ddl_service_.rebuild_hidden_table_foreign_key_in_trans(
            const_cast<obrpc::ObAlterTableArg &>(arg), obj_ids))) {
          LOG_WARN("failed to rebuild hidden table foreign key in trans", K(ret));
        }
        break;
      }
      case share::MAKE_DDL_TAKE_EFFECT_TASK: {
        if (arg.is_direct_load_partition_) {
          if (OB_FAIL(ddl_service_.swap_orig_and_hidden_table_partitions(
              const_cast<obrpc::ObAlterTableArg &>(arg)))) {
            LOG_WARN("failed to swap orig and hidden table partitions", K(ret));
          }
        } else if (OB_FAIL(ddl_service_.swap_orig_and_hidden_table_state(
            const_cast<obrpc::ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to swap orig and hidden table state", K(ret));
        }
        break;
      }
      case share::CLEANUP_GARBAGE_TASK:
      case share::PARTITION_SPLIT_RECOVERY_CLEANUP_GARBAGE_TASK: {
        if (OB_FAIL(ddl_service_.cleanup_garbage(
            const_cast<obrpc::ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to cleanup garbage", K(ret));
        }
        break;
      }
      case share::MODIFY_FOREIGN_KEY_STATE_TASK: {
        if (OB_FAIL(ddl_service_.modify_hidden_table_fk_state(
            const_cast<obrpc::ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to modify hidden table fk state", K(ret));
        }
        break;
      }
      case share::DELETE_COLUMN_FROM_SCHEMA: {
        if (OB_FAIL(ddl_service_.delete_column_from_schema(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("fail to set column to no minor status", K(ret), K(arg));
        }
        break;
      }
      // remap all index tables to hidden table and take effect concurrently.
      case share::REMAP_INDEXES_AND_TAKE_EFFECT_TASK: {
        if (OB_FAIL(ddl_service_.remap_index_tablets_and_take_effect(
            const_cast<obrpc::ObAlterTableArg &>(arg)))) {
          LOG_WARN("fail to remap index tables to hidden table and take effect", K(ret));
        }
        break;
      }
      case share::UPDATE_AUTOINC_SCHEMA: {
        if (OB_FAIL(ddl_service_.update_autoinc_schema(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("fail to update autoinc schema", K(ret), K(arg));
        }
        break;
      }
      case share::MODIFY_NOT_NULL_COLUMN_STATE_TASK: {
        if (OB_FAIL(ddl_service_.modify_hidden_table_not_null_column_state(arg))) {
          LOG_WARN("failed to modify hidden table cst state", K(ret));
        }
        break;
      }
      case share::MAKE_RECOVER_RESTORE_TABLE_TASK_TAKE_EFFECT: {
        if (OB_FAIL(ddl_service_.make_recover_restore_tables_visible(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("make recovert restore task visible failed", K(ret), K(arg));
        }
        break;
      }
      case share::PARTITION_SPLIT_RECOVERY_TASK: {
        if (OB_FAIL(ddl_service_.restore_the_table_to_split_completed_state(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to restore the table to split completed state", K(ret));
        }
        break;
      }
      case share::SWITCH_VEC_INDEX_NAME_TASK: {
        if (OB_FAIL(ddl_service_.switch_index_name_and_status_for_vec_index_table(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("make recovert restore task visible failed", K(ret), K(arg));
        }
        break;
      }
      case share::SWITCH_MLOG_NAME_TASK: {
        if (OB_FAIL(ddl_service_.switch_index_name_and_status_for_mlog_table(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to switch index name and status for mlog table", K(ret), K(arg));
        }
        break;
      }
      default:
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unknown ddl task type", K(ret), K(arg.ddl_task_type_));
    }
  }
  return ret;
}

int ObRootService::precheck_interval_part(const obrpc::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObAlterTableArg::AlterPartitionType op_type = arg.alter_part_type_;
  const ObSimpleTableSchemaV2 *simple_table_schema = NULL;
  const AlterTableSchema &alter_table_schema = arg.alter_table_schema_;
  int64_t tenant_id = alter_table_schema.get_tenant_id();

  if (!alter_table_schema.is_interval_part()
      || obrpc::ObAlterTableArg::ADD_PARTITION != op_type) {
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, schema service must not be NULL", K(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(tenant_id, schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_simple_table_schema(tenant_id,
             alter_table_schema.get_table_id(), simple_table_schema))) {
    LOG_WARN("get table schema failed", KR(ret), K(tenant_id), K(alter_table_schema));
  } else if (OB_ISNULL(simple_table_schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("simple_table_schema is null", K(ret), K(alter_table_schema));
  } else if (simple_table_schema->get_schema_version() < alter_table_schema.get_schema_version()) {
  } else if (simple_table_schema->get_interval_range() != alter_table_schema.get_interval_range()
             || simple_table_schema->get_transition_point() != alter_table_schema.get_transition_point()) {
    ret = OB_ERR_INTERVAL_PARTITION_ERROR;
    LOG_WARN("interval_range or transition_point is changed", KR(ret), \
             KPC(simple_table_schema), K(alter_table_schema));
  } else {
    int64_t j = 0;
    const ObRowkey *rowkey_orig= NULL;
    bool is_all_exist = true;
    ObPartition **inc_part_array = alter_table_schema.get_part_array();
    ObPartition **orig_part_array = simple_table_schema->get_part_array();
    if (OB_ISNULL(inc_part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ptr is null", K(ret), K(alter_table_schema), KPC(simple_table_schema));
    } else if (OB_ISNULL(orig_part_array)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("ptr is null", K(ret), K(alter_table_schema), KPC(simple_table_schema));
    }
    for (int64_t i = 0; is_all_exist && OB_SUCC(ret) && i < alter_table_schema.get_part_option().get_part_num(); ++i) {
      const ObRowkey *rowkey_cur = NULL;
      if (OB_ISNULL(inc_part_array[i])) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ptr is null", K(ret), K(alter_table_schema), KPC(simple_table_schema));
      } else if (OB_UNLIKELY(NULL == (rowkey_cur = &inc_part_array[i]->get_high_bound_val()))) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("ptr is null", K(ret), K(alter_table_schema), KPC(simple_table_schema));
      }
      while (is_all_exist && OB_SUCC(ret) && j < simple_table_schema->get_part_option().get_part_num()) {
        if (OB_ISNULL(orig_part_array[j])) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ptr is null", K(ret), K(alter_table_schema), KPC(simple_table_schema));
        } else if (OB_UNLIKELY(NULL == (rowkey_orig = &orig_part_array[j]->get_high_bound_val()))) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ptr is null", K(ret), K(alter_table_schema), KPC(simple_table_schema));
        } else if (*rowkey_orig < *rowkey_cur) {
          j++;
        } else {
          break;
        }
      }
      if (OB_FAIL(ret)) {
      } else if (*rowkey_orig != *rowkey_cur) {
        is_all_exist = false;
      }
    }
    if (OB_FAIL(ret)) {
    } else if (is_all_exist) {
      LOG_INFO("all interval part for add is exist", K(alter_table_schema), KPC(simple_table_schema));
      ret = OB_ERR_INTERVAL_PARTITION_EXIST;
    }
  }
  return ret;
}

int ObRootService::create_hidden_table(const obrpc::ObCreateHiddenTableArg &arg,
                                       obrpc::ObCreateHiddenTableRes &res)
{
  LOG_DEBUG("receive create hidden table arg", K(arg));
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.get_tenant_id();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.get_tenant_id(), arg.task_id_, CREATE_HIDDEN_TABLE_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.get_tenant_id(), arg.task_id_, CREATE_HIDDEN_TABLE_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.create_hidden_table(arg, res))) {
    LOG_WARN("do create hidden table in trans failed", K(ret), K(arg));
  }
  char tenant_id_buffer[128];
  snprintf(tenant_id_buffer, sizeof(tenant_id_buffer), "orig_tenant_id:%ld, target_tenant_id:%ld",
            arg.get_tenant_id(), arg.get_dest_tenant_id());
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "create hidden table",
                        "tenant_id", tenant_id_buffer,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.get_table_id(),
                        "schema_version", res.schema_version_);
  LOG_INFO("finish create hidden table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::update_ddl_task_active_time(const obrpc::ObUpdateDDLTaskActiveTimeArg &arg)
{
  LOG_DEBUG("receive recv ddl task status arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  const uint64_t tenant_id = arg.tenant_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::update_ddl_task_active_time(ObDDLTaskID(tenant_id, task_id)))) {
    LOG_WARN("fail to set RegTaskTime map", K(ret), K(tenant_id), K(task_id));
  }
  return ret;
}

int ObRootService::abort_redef_table(const obrpc::ObAbortRedefTableArg &arg)
{
  LOG_DEBUG("receive abort redef table arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  const uint64_t tenant_id = arg.tenant_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, ABORT_REDEF_TABLE_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, ABORT_REDEF_TABLE_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::abort_redef_table(ObDDLTaskID(tenant_id, task_id)))) {
    LOG_WARN("cancel task failed", K(ret), K(tenant_id), K(task_id));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "abort redef table",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_);
  LOG_INFO("finish abort redef table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::finish_redef_table(const obrpc::ObFinishRedefTableArg &arg)
{
  LOG_DEBUG("receive finish redef table arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  const uint64_t tenant_id = arg.tenant_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, FINISH_REDEF_TABLE_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, FINISH_REDEF_TABLE_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::finish_redef_table(ObDDLTaskID(tenant_id, task_id)))) {
    LOG_WARN("failed to finish redef table", K(ret), K(task_id), K(tenant_id));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "finish redef table",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_);
  LOG_INFO("finish abort redef table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::copy_table_dependents(const obrpc::ObCopyTableDependentsArg &arg)
{
  LOG_INFO("receive copy table dependents arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  const uint64_t tenant_id = arg.tenant_id_;
  const bool is_copy_indexes = arg.copy_indexes_;
  const bool is_copy_triggers = arg.copy_triggers_;
  const bool is_copy_constraints = arg.copy_constraints_;
  const bool is_copy_foreign_keys = arg.copy_foreign_keys_;
  const bool is_ignore_errors = arg.ignore_errors_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, COPY_TABLE_DEPENDENTS_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, COPY_TABLE_DEPENDENTS_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::copy_table_dependents(ObDDLTaskID(tenant_id, task_id),
                                                          is_copy_constraints,
                                                          is_copy_indexes,
                                                          is_copy_triggers,
                                                          is_copy_foreign_keys,
                                                          is_ignore_errors))) {
    LOG_WARN("failed to copy table dependents", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "copy table dependents",
                        "tenant_id", tenant_id,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", task_id);
  LOG_INFO("finish copy table dependents ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::start_redef_table(const obrpc::ObStartRedefTableArg &arg, obrpc::ObStartRedefTableRes &res)
{
  LOG_DEBUG("receive start redef table arg", K(arg));
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.orig_tenant_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::start_redef_table(arg, res))) {
    LOG_WARN("start redef table failed", K(ret));
  }
  char tenant_id_buffer[128];
  snprintf(tenant_id_buffer, sizeof(tenant_id_buffer), "orig_tenant_id:%ld, target_tenant_id:%ld",
            arg.orig_tenant_id_, arg.target_tenant_id_);
  char table_id_buffer[128];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "orig_table_id:%ld, target_table_id:%ld",
            arg.orig_table_id_, arg.target_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "redef table",
                        "tenant_id", tenant_id_buffer,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish redef table ddl", K(arg), K(ret), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::recover_restore_table_ddl(const obrpc::ObRecoverRestoreTableDDLArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.recover_restore_table_ddl_task(arg))) {
    LOG_WARN("recover restore table ddl task failed", K(ret), K(arg));
  }
  LOG_INFO("recover restore table ddl finish", K(ret), K(arg));
  return ret;
}

int ObRootService::set_comment(const obrpc::ObSetCommentArg &arg, obrpc::ObParallelDDLRes &res)
{
  LOG_TRACE("receive set comment arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  const uint64_t tenant_id = arg.exec_tenant_id_;
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_(tenant_id))) {
    LOG_WARN("fail to pre check parallel ddl", KR(ret), K(tenant_id));
  } else {
    ObSetCommentHelper comment_helper(schema_service_, tenant_id, arg, res);
    if (OB_FAIL(comment_helper.init(ddl_service_))) {
      LOG_WARN("fail to init comment helper", KR(ret), K(tenant_id));
    } else if (OB_FAIL(comment_helper.execute())) {
      LOG_WARN("fail to execute comment", KR(ret), K(tenant_id));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel set comment",
                        K(tenant_id),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "schema_version", res.schema_version_);
  LOG_TRACE("finish set comment", KR(ret), K(arg), K(cost));
  return ret;
}

int ObRootService::alter_table(const obrpc::ObAlterTableArg &arg, obrpc::ObAlterTableRes &res)
{
  LOG_DEBUG("receive alter table arg", K(arg));
  int ret = OB_SUCCESS;
  bool is_oracle_mode = false;
  ObSchemaGetterGuard schema_guard;
  const uint64_t tenant_id = arg.alter_table_schema_.get_tenant_id();
  ObAlterTableArg &nonconst_arg = const_cast<ObAlterTableArg &>(arg);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(precheck_interval_part(arg))) {
    if (ret != OB_ERR_INTERVAL_PARTITION_EXIST) {
      LOG_WARN("fail to precheck_interval_part", K(arg), KR(ret));
    }
  } else {
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", K(ret));
    } else if (OB_FAIL(table_allow_ddl_operation(arg))) {
      LOG_WARN("table can't do ddl now", K(ret));
    } else if (nonconst_arg.is_add_to_scheduler_) {
      ObDDLTaskRecord task_record;
      ObArenaAllocator allocator(lib::ObLabel("DdlTaskTmp"));
      ObDDLType ddl_type = ObDDLType::DDL_INVALID;
      const ObTableSchema *orig_table_schema = nullptr;
      schema_guard.set_session_id(arg.session_id_);
      if (obrpc::ObAlterTableArg::DROP_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_DROP_PARTITION;
      } else if (obrpc::ObAlterTableArg::DROP_SUB_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_DROP_SUB_PARTITION;
      } else if (obrpc::ObAlterTableArg::TRUNCATE_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_TRUNCATE_PARTITION;
      } else if (obrpc::ObAlterTableArg::TRUNCATE_SUB_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_TRUNCATE_SUB_PARTITION;
      } else if (obrpc::ObAlterTableArg::RENAME_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_RENAME_PARTITION;
      } else if (obrpc::ObAlterTableArg::RENAME_SUB_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_RENAME_SUB_PARTITION;
      } else if (obrpc::ObAlterTableArg::ALTER_PARTITION_STORAGE_CACHE_POLICY == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_ALTER_PARTITION_POLICY;
      } else if (obrpc::ObAlterTableArg::ALTER_SUBPARTITION_STORAGE_CACHE_POLICY == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_ALTER_SUBPARTITION_POLICY;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ddl type", K(ret), K(nonconst_arg.alter_part_type_), K(nonconst_arg));
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id,
                                                        nonconst_arg.alter_table_schema_.get_database_name(),
                                                        nonconst_arg.alter_table_schema_.get_origin_table_name(),
                                                        false  /* is_index*/,
                                                        orig_table_schema))) {
        LOG_WARN("fail to get and check table schema", K(ret));
      } else if (OB_ISNULL(orig_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(tenant_id), K(nonconst_arg.alter_table_schema_));
      } else {
        ObCreateDDLTaskParam param(tenant_id,
                                   ddl_type,
                                   nullptr,
                                   nullptr,
                                   orig_table_schema->get_table_id(),
                                   orig_table_schema->get_schema_version(),
                                   arg.parallelism_,
                                   arg.consumer_group_id_,
                                   &allocator,
                                   &arg,
                                   0 /*parent task id*/);
        if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param, sql_proxy_, task_record))) {
          LOG_WARN("submit ddl task failed", K(ret), K(arg));
        } else if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
          LOG_WARN("fail to schedule ddl task", K(ret), K(task_record));
        } else {
          res.ddl_type_ = ddl_type;
          res.task_id_ = task_record.task_id_;
        }
      }
    } else if (OB_FAIL(ddl_service_.alter_table(nonconst_arg, res))) {
      LOG_WARN("alter_user_table failed", K(arg), K(ret));
    } else {
      const ObSimpleTableSchemaV2 *simple_table_schema = NULL;
      // there are multiple DDL except alter table, ctas, comment on, eg.
      // but only alter_table specify table_id, so if no table_id, it indicates DDL is not alter table, skip.
      if (OB_INVALID_ID == arg.alter_table_schema_.get_table_id()) {
        // skip
      } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
        LOG_WARN("get schema guard in inner table failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_simple_table_schema(tenant_id, arg.alter_table_schema_.get_table_id(), simple_table_schema))) {
        LOG_WARN("fail to get table schema", K(ret), K(arg.alter_table_schema_.get_table_id()));
      } else if (OB_ISNULL(simple_table_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("simple_table_schema is NULL ptr", K(ret), K(simple_table_schema), K(ret));
      } else {
        res.schema_version_ = simple_table_schema->get_schema_version();
      }
    }
  }
  char table_id_buffer[256];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "table_id:%ld, hidden_table_id:%ld",
            arg.table_id_, arg.hidden_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "alter table",
                        K(tenant_id),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish alter table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::exchange_partition(const obrpc::ObExchangePartitionArg &arg, obrpc::ObAlterTableRes &res)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  schema_guard.set_session_id(arg.session_id_);
  LOG_DEBUG("receive exchange partition arg", K(ret), K(arg));
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(arg.tenant_id_, schema_guard))) {
    LOG_WARN("get schema guard in inner table failed", K(ret));
  } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
    LOG_WARN("check parallel ddl conflict failed", K(ret));
  } else {
    ObPartitionExchange partition_exchange(ddl_service_);
    if (OB_FAIL(partition_exchange.check_and_exchange_partition(arg, res, schema_guard))) {
      LOG_WARN("fail to check and exchange partition", K(ret), K(arg), K(res));
    }
  }
  char table_id_buffer[256];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "table_id:%ld, exchange_table_id:%ld",
            arg.base_table_id_, arg.inc_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "alter table",
                        K(arg.tenant_id_),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish alter table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::create_aux_index(
    const ObCreateAuxIndexArg &arg,
    ObCreateAuxIndexRes &result)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.create_aux_index(arg, result))) {
    LOG_WARN("failed to generate aux index schema", K(ret), K(arg), K(result));
  }
  LOG_INFO("finish generate aux index schema", K(ret), K(arg), K(result), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::create_index(const ObCreateIndexArg &arg, obrpc::ObAlterTableRes &res)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  LOG_DEBUG("receive create index arg", K(arg));
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObIndexBuilder index_builder(ddl_service_);
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(arg.tenant_id_, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", K(ret));
    } else if (OB_FAIL(index_builder.create_index(arg, res))) {
      LOG_WARN("create_index failed", K(arg), K(ret));
    }
  }
  char table_id_buffer[256];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "data_table_id:%ld, index_table_id:%ld",
            arg.data_table_id_, arg.index_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "create index",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish create index ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::create_mlog(const obrpc::ObCreateMLogArg &arg, obrpc::ObCreateMLogRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else {
    ObSchemaGetterGuard schema_guard;
    ObMLogBuilder mlog_builder(ddl_service_);
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(
        arg.tenant_id_, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", K(ret));
    } else if (OB_FAIL(mlog_builder.init())) {
      LOG_WARN("failed to init mlog builder", KR(ret));
    } else if (OB_FAIL(mlog_builder.create_or_replace_mlog(schema_guard, arg, res))) {
      LOG_WARN("failed to create mlog", KR(ret), K(arg));
    }
  }
  return ret;
}

int ObRootService::parallel_create_index(const ObCreateIndexArg &arg, obrpc::ObAlterTableRes &res)
{
  LOG_TRACE("receive parallel create index arg", K(arg));
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  const uint64_t tenant_id = arg.exec_tenant_id_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_(tenant_id))) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret), K(tenant_id));
  } else if (share::schema::is_fts_or_multivalue_index(arg.index_type_)
            || share::schema::is_vec_index(arg.index_type_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported", KR(ret), K(arg.index_type_));
  } else {
    ObCreateIndexHelper create_index_helper(schema_service_, tenant_id, ddl_service_, arg, res);
    if (OB_FAIL(create_index_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create index helper", KR(ret), K(tenant_id));
    } else if (OB_FAIL(create_index_helper.execute())) {
      LOG_WARN("fail to execute create index table", KR(ret), K(tenant_id));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  char table_id_buffer[256];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "data_table_id:%ld, index_table_id:%ld",
            arg.data_table_id_, arg.index_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel create index",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_TRACE("finish parallel create index", KR(ret), K(arg), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::fork_table(const obrpc::ObForkTableArg &arg, obrpc::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.fork_table(arg, res))) {
    LOG_WARN("fork table failed", K(ret));
  }
  char table_names_buffer[512] = {0};
  snprintf(table_names_buffer, sizeof(table_names_buffer), "%.*s -> %.*s",
           static_cast<int>(arg.src_table_name_.length()), arg.src_table_name_.ptr(),
           static_cast<int>(arg.dst_table_name_.length()), arg.dst_table_name_.ptr());
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "fork table",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "tables", table_names_buffer);
  LOG_INFO("finish fork table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_table(const obrpc::ObDropTableArg &arg, obrpc::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  uint64_t target_object_id = OB_INVALID_ID;
  int64_t schema_version = OB_INVALID_SCHEMA_VERSION;
  bool need_add_to_ddl_scheduler = arg.is_add_to_scheduler_;
  const uint64_t tenant_id = arg.tenant_id_;
  ObSchemaGetterGuard schema_guard;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("fail to get schema guard with version in inner table", K(ret), K(arg));
  } else if (need_add_to_ddl_scheduler) {
    // to decide wherther to add to ddl scheduler.
    // 1. do not add to scheduler if all tables do not exist.
    // 2. do not add to scheduler if all existed tables are temporary tables.
    need_add_to_ddl_scheduler = arg.tables_.count() == 0 ? false : true;
    for (int64_t i = 0; OB_SUCC(ret) && need_add_to_ddl_scheduler && i < arg.tables_.count(); ++i) {
      int tmp_ret = OB_SUCCESS;
      const ObTableItem &table_item = arg.tables_.at(i);
      const ObTableSchema *table_schema = nullptr;
      if (OB_SUCCESS != (tmp_ret = ddl_service_.check_table_exists(tenant_id,
                                                                   table_item,
                                                                   arg.table_type_,
                                                                   schema_guard,
                                                                   &table_schema))) {
        LOG_INFO("check table exist failed, generate error msg in ddl service later", K(ret), K(tmp_ret));
      }
      if (OB_FAIL(ret)) {
      } else if (nullptr != table_schema) {
        if (table_schema->is_tmp_table()) {
          // do nothing.
        } else if (OB_INVALID_ID == target_object_id || OB_INVALID_SCHEMA_VERSION == schema_version) {
          // regard table_id, schema_version of the the first table as the tag to submit ddl task.
          target_object_id = table_schema->get_table_id();
          schema_version = table_schema->get_schema_version();
        }
      }
    }
    // all tables do not exist, or all existed tables are temporary tables.
    if (OB_INVALID_ID == target_object_id || OB_INVALID_SCHEMA_VERSION == schema_version) {
      need_add_to_ddl_scheduler = false;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (need_add_to_ddl_scheduler) {
    ObDDLTaskRecord task_record;
    ObArenaAllocator allocator(lib::ObLabel("DdlTaskTmp"));
    ObCreateDDLTaskParam param(tenant_id,
                               ObDDLType::DDL_DROP_TABLE,
                               nullptr,
                               nullptr,
                               target_object_id,
                               schema_version,
                               arg.parallelism_,
                               arg.consumer_group_id_,
                               &allocator,
                               &arg,
                               0 /* parent task id*/);
    if (OB_UNLIKELY(OB_INVALID_ID == target_object_id || OB_INVALID_SCHEMA_VERSION == schema_version)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("error unexpected", K(ret), K(arg), K(target_object_id), K(schema_version));
    } else if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param, sql_proxy_, task_record))) {
      LOG_WARN("submit ddl task failed", K(ret), K(arg));
    } else if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
      LOG_WARN("fail to schedule ddl task", K(ret), K(task_record));
    } else {
      res.tenant_id_ = tenant_id;
      res.schema_id_ = target_object_id;
      res.task_id_ = task_record.task_id_;
    }
  } else if (OB_FAIL(ddl_service_.drop_table(arg, res))) {
    LOG_WARN("ddl service failed to drop table", K(ret), K(arg), K(res));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "drop table",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "session_id", arg.session_id_,
                        "schema_version", res.schema_id_);
  LOG_INFO("finish drop table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::parallel_drop_table(const ObDropTableArg &arg, ObDropTableRes &res)
{
  int ret = OB_SUCCESS;

  LOG_TRACE("receive parallel drop table arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  const uint64_t tenant_id = arg.exec_tenant_id_;

  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_(tenant_id))) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret), K(tenant_id));
  } else {
    ObDropTableHelper drop_table_helper(schema_service_, tenant_id, arg, res);
    if (OB_FAIL(drop_table_helper.init(ddl_service_))) {
      LOG_WARN("fail to init drop table helper", KR(ret), K(tenant_id));
    } else if (OB_FAIL(drop_table_helper.execute())) {
      LOG_WARN("fail to execute drop table", KR(ret), K(tenant_id));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "drop table",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "session_id", arg.session_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish parallel drop table ddl", KR(ret), K(arg), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_database(const obrpc::ObDropDatabaseArg &arg, ObDropDatabaseRes &drop_database_res)
{
  int ret = OB_SUCCESS;
  uint64_t database_id = 0;
  int64_t schema_version = 0;
  bool need_add_to_scheduler = arg.is_add_to_scheduler_;
  const uint64_t tenant_id = arg.tenant_id_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (need_add_to_scheduler) {
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret), K(tenant_id));
    } else if (OB_FAIL(schema_guard.get_schema_version(tenant_id, schema_version))) {
      LOG_WARN("fail to get schema version", K(ret), K(arg));
    } else if (OB_FAIL(schema_guard.get_database_id(tenant_id, arg.database_name_, database_id))) {
      LOG_WARN("fail to get database id");
    } else if (OB_INVALID_ID == database_id) {
      // drop database if exists xxx.
      need_add_to_scheduler = false;
    }
  }

  if (OB_FAIL(ret)) {
  } else if (need_add_to_scheduler) {
    ObDDLTaskRecord task_record;
    ObArenaAllocator allocator(lib::ObLabel("DdlTaskTmp"));
    ObCreateDDLTaskParam param(tenant_id,
                                ObDDLType::DDL_DROP_DATABASE,
                                nullptr,
                                nullptr,
                                database_id,
                                schema_version,
                                arg.parallelism_,
                                arg.consumer_group_id_,
                                &allocator,
                                &arg,
                                0 /* parent task id*/);
    if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param, sql_proxy_, task_record))) {
      LOG_WARN("submit ddl task failed", K(ret), K(arg));
    } else if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
      LOG_WARN("fail to schedule ddl task", K(ret), K(task_record));
    } else {
      drop_database_res.ddl_res_.tenant_id_ = tenant_id;
      drop_database_res.ddl_res_.schema_id_ = database_id;
      drop_database_res.ddl_res_.task_id_ = task_record.task_id_;
    }
  } else if (OB_FAIL(ddl_service_.drop_database(arg, drop_database_res))) {
    LOG_WARN("ddl_service_ drop_database failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::drop_tablegroup(const obrpc::ObDropTablegroupArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.drop_tablegroup(arg))) {
    LOG_WARN("ddl_service_ drop_tablegroup failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::alter_tablegroup(const obrpc::ObAlterTablegroupArg &arg)
{
  LOG_DEBUG("receive alter tablegroup arg", K(arg));
  const ObTablegroupSchema *tablegroup_schema = NULL;
  ObSchemaGetterGuard schema_guard;
  uint64_t tablegroup_id = OB_INVALID_ID;
  const uint64_t tenant_id = arg.tenant_id_;
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("get schema guard in inner table failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_tablegroup_id(tenant_id,
                                                    arg.tablegroup_name_,
                                                    tablegroup_id))) {
    LOG_WARN("fail to get tablegroup id", K(ret));
  } else if (OB_INVALID_ID == tablegroup_id) {
    ret = OB_TABLEGROUP_NOT_EXIST;
    LOG_WARN("get invalid tablegroup schema", KR(ret), K(arg));
  } else if (OB_FAIL(schema_guard.get_tablegroup_schema(tenant_id, tablegroup_id, tablegroup_schema))) {
    LOG_WARN("fail to get tablegroup schema", K(ret), K(tenant_id), K(ret));
  } else if (OB_ISNULL(tablegroup_schema)) {
    ret = OB_TABLEGROUP_NOT_EXIST;
    LOG_WARN("get invalid tablegroup schema", K(ret));
  } else if (tablegroup_schema->is_in_splitting()) {
    ret = OB_OP_NOT_ALLOW;
    LOG_WARN("tablegroup is splitting, refuse to alter now", K(ret), K(tablegroup_id));
    LOG_USER_ERROR(OB_OP_NOT_ALLOW, "tablegroup is splitting, alter tablegroup");
  }
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(ddl_service_.alter_tablegroup(arg))) {
    LOG_WARN("ddl_service_ alter tablegroup failed", K(arg), K(ret));
  } else {
  }
  return ret;
}

int ObRootService::drop_index_on_failed(const obrpc::ObDropIndexArg &arg, obrpc::ObDropIndexRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else {
    ObIndexBuilder index_builder(ddl_service_);
    if (OB_FAIL(index_builder.drop_index_on_failed(arg, res))) {
      LOG_WARN("index_builder drop_index_on_failed failed", K(ret), K(arg));
    }
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "drop index on failed",
                        "tenant_id", res.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish drop index on fail ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_index(const obrpc::ObDropIndexArg &arg, obrpc::ObDropIndexRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObIndexBuilder index_builder(ddl_service_);
    if (OB_FAIL(index_builder.drop_index(arg, res))) {
      LOG_WARN("index_builder drop_index failed", K(arg), K(ret));
    }
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "drop index",
                        "tenant_id", res.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish drop index ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::rebuild_vec_index(const obrpc::ObRebuildIndexArg &arg, obrpc::ObAlterTableRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.rebuild_vec_index(arg, res))) {
    LOG_WARN("ddl_service rebuild index failed", K(arg), K(ret));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "rebuild index",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish rebuild index ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_lob(const ObDropLobArg &arg)
{
  return ddl_service_.drop_lob(arg);
}

int ObRootService::force_drop_lonely_lob_aux_table(const ObForceDropLonelyLobAuxTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ddl_service_.force_drop_lonely_lob_aux_table(arg)))  {
    LOG_WARN("drop fail", KR(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "force drop lonely lob table",
                        "tenant_id", arg.get_tenant_id(),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "data_table_id", arg.get_data_table_id(),
                        "lob_meta_table_id", arg.get_aux_lob_meta_table_id(),
                        "lob_piece_table_id", arg.get_aux_lob_piece_table_id());
  return ret;
}


int ObRootService::send_auto_split_tablet_task_request(const obrpc::ObAutoSplitTabletBatchArg &arg,
                                                       obrpc::ObAutoSplitTabletBatchRes &res)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret), K(inited_));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::cache_auto_split_task(arg, res))) {
    LOG_WARN("fail to cache auto split task", K(ret), K(arg), K(res));
  }
  return ret;
}

int ObRootService::split_global_index_tablet(const obrpc::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  bool is_oracle_mode = false;
  ObSchemaGetterGuard schema_guard;
  const uint64_t tenant_id = arg.alter_table_schema_.get_tenant_id();
  ObAlterTableArg &nonconst_arg = const_cast<ObAlterTableArg &>(arg);
  obrpc::ObAlterTableRes res;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid()) || arg.is_add_to_scheduler_ || !arg.alter_table_schema_.is_global_index_table()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg), K(arg.is_add_to_scheduler_), K(arg.alter_table_schema_.is_global_index_table()));
  } else {
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", K(ret));
    } else if (OB_FAIL(table_allow_ddl_operation(arg))) {
      LOG_WARN("table can't do ddl now", K(ret));
    } else if (OB_FAIL(ddl_service_.split_global_index_partitions(nonconst_arg, res))) {
      LOG_WARN("split global index failed", K(arg), K(ret));
    }
  }
  char table_id_buffer[256];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "table_id:%ld, hidden_table_id:%ld",
            arg.table_id_, arg.hidden_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "split global index",
                        K(tenant_id),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish split global index tablet ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::clean_splitted_tablet(const obrpc::ObCleanSplittedTabletArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.clean_splitted_tablet(arg))) {
    LOG_WARN("ddl_service clean splitted tablet failed", KR(ret), K(arg));
  }
  return ret;
}

int ObRootService::flashback_index(const ObFlashBackIndexArg &arg) {
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.flashback_index(arg))) {
    LOG_WARN("failed to flashback index", K(ret));
  }

  return ret;
}

int ObRootService::purge_index(const ObPurgeIndexArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.purge_index(arg))) {
    LOG_WARN("failed to purge index", K(ret));
  }

  return ret;
}

int ObRootService::rename_table(const obrpc::ObRenameTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.rename_table(arg))){
    LOG_WARN("rename table failed", K(ret));
  }
  return ret;
}

int ObRootService::truncate_table(const obrpc::ObTruncateTableArg &arg, obrpc::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    SCN frozen_scn;
    if (OB_FAIL(ObMajorFreezeHelper::get_frozen_scn(arg.tenant_id_, frozen_scn))) {
      LOG_WARN("get_frozen_scn failed", K(ret));
    } else if (arg.is_add_to_scheduler_) {
      ObDDLTaskRecord task_record;
      ObArenaAllocator allocator(lib::ObLabel("DdlTaskTmp"));
      ObSchemaGetterGuard schema_guard;
      const ObTableSchema *table_schema = nullptr;
      const uint64_t tenant_id = arg.tenant_id_;
      if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
        LOG_WARN("get schema guard in inner table failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id, arg.database_name_,
                                                       arg.table_name_, false /* is_index */,
                                                       table_schema))) {
        LOG_WARN("fail to get table schema", K(ret));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(arg));
      } else {
        ObCreateDDLTaskParam param(tenant_id,
                                   ObDDLType::DDL_TRUNCATE_TABLE,
                                   nullptr,
                                   nullptr,
                                   table_schema->get_table_id(),
                                   table_schema->get_schema_version(),
                                   arg.parallelism_,
                                   arg.consumer_group_id_,
                                   &allocator,
                                   &arg,
                                   0 /* parent task id*/);
        if (OB_FAIL(ObSysDDLSchedulerUtil::create_ddl_task(param, sql_proxy_, task_record))) {
          LOG_WARN("submit ddl task failed", K(ret), K(arg));
        } else if (OB_FAIL(ObSysDDLSchedulerUtil::schedule_ddl_task(task_record))) {
          LOG_WARN("fail to schedule ddl task", K(ret), K(task_record));
        } else {
          res.tenant_id_ = tenant_id;
          res.schema_id_ = table_schema->get_table_id();
          res.task_id_ = task_record.task_id_;
        }
      }
    } else if (OB_FAIL(ddl_service_.truncate_table(arg, res, frozen_scn))) {
      LOG_WARN("ddl service failed to truncate table", K(arg), K(ret), K(frozen_scn));
    }
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "truncate table",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.table_name_,
                        "schema_version", res.schema_id_);
  LOG_INFO("finish truncate table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

/*
 * new parallel truncate table
 */
int ObRootService::truncate_table_v2(const obrpc::ObTruncateTableArg &arg, obrpc::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    SCN frozen_scn;
    if (OB_FAIL(ObMajorFreezeHelper::get_frozen_scn(arg.tenant_id_, frozen_scn))) {
      LOG_WARN("get_frozen_scn failed", K(ret));
    } else if (OB_FAIL(ddl_service_.new_truncate_table(arg, res, frozen_scn))) {
      LOG_WARN("ddl service failed to truncate table", K(arg), K(ret));
    }
    ROOTSERVICE_EVENT_ADD("ddl scheduler", "truncate table new",
                          "tenant_id", arg.tenant_id_,
                          "ret", ret,
                          "trace_id", *ObCurTraceId::get_trace_id(),
                          "task_id", res.task_id_,
                          "table_name", arg.table_name_,
                          "schema_version", res.schema_id_,
                          frozen_scn);
    LOG_INFO("finish new truncate table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  }
  return ret;
}

int ObRootService::create_table_like(const ObCreateTableLikeArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    if (OB_FAIL(ddl_service_.create_table_like(arg))) {
      if (OB_ERR_TABLE_EXIST == ret) {
        //create table xx if not exist like
        if (arg.if_not_exist_) {
          LOG_USER_NOTE(OB_ERR_TABLE_EXIST,
                        arg.new_table_name_.length(), arg.new_table_name_.ptr());
          LOG_WARN("table is exist, no need to create again", K(arg), K(ret));
          ret = OB_SUCCESS;
        } else {
          ret = OB_ERR_TABLE_EXIST;
          LOG_USER_ERROR(OB_ERR_TABLE_EXIST, arg.new_table_name_.length(), arg.new_table_name_.ptr());
          LOG_WARN("table is exist, cannot create it twice", K(arg), K(ret));
        }
      }
    }
  }
  return ret;
}

/**
 * recyclebin related
 */
int ObRootService::flashback_table_from_recyclebin(const ObFlashBackTableFromRecyclebinArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.flashback_table_from_recyclebin(arg))) {
    LOG_WARN("failed to flash back table", K(ret));
  }
  return ret;
}

int ObRootService::flashback_table_to_time_point(const obrpc::ObFlashBackTableToScnArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive flashback table arg", K(arg));

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.flashback_table_to_time_point(arg))) {
    LOG_WARN("failed to flash back table", K(ret));
  }
  return ret;
}

int ObRootService::purge_table(const ObPurgeTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.purge_table(arg))) {
    LOG_WARN("failed to purge table", K(ret));
  }
  return ret;
}

int ObRootService::flashback_database(const ObFlashBackDatabaseArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.flashback_database(arg))) {
    LOG_WARN("failed to flash back database", K(ret));
  }
  return ret;
}

int ObRootService::purge_database(const ObPurgeDatabaseArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.purge_database(arg))) {
    LOG_WARN("failed to purge database", K(ret));
  }
  return ret;
}

int ObRootService::purge_expire_recycle_objects(const ObPurgeRecycleBinArg &arg, Int64 &affected_rows)
{
  int ret = OB_SUCCESS;
  int64_t purged_objects = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.purge_tenant_expire_recycle_objects(arg, purged_objects))) {
    LOG_WARN("failed to purge expire recyclebin objects", K(ret), K(arg));
  } else {
    affected_rows = purged_objects;
  }
  return ret;
}

int ObRootService::optimize_table(const ObOptimizeTableArg &arg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  LOG_INFO("receive optimize table request", K(arg));
  lib::Worker::CompatMode mode;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, schema service must not be NULL", K(ret));
  } else if (OB_FAIL(ObCompatModeGetter::get_tenant_mode(arg.tenant_id_, mode))) {
    LOG_WARN("fail to get tenant mode", K(ret));
  } else {
    const int64_t all_core_table_id = OB_ALL_CORE_TABLE_TID;
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.tables_.count(); ++i) {
      SMART_VAR(obrpc::ObAlterTableArg, alter_table_arg) {
        ObSqlString sql;
        const obrpc::ObTableItem &table_item = arg.tables_.at(i);
        const ObTableSchema *table_schema = nullptr;
        alter_table_arg.is_alter_options_ = true;
        alter_table_arg.alter_table_schema_.set_origin_database_name(table_item.database_name_);
        alter_table_arg.alter_table_schema_.set_origin_table_name(table_item.table_name_);
        alter_table_arg.alter_table_schema_.set_tenant_id(arg.tenant_id_);
        alter_table_arg.skip_sys_table_check_ = true;
        //exec_tenant_id_ is used in standby cluster
        alter_table_arg.exec_tenant_id_ = arg.exec_tenant_id_;
        if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(arg.tenant_id_, schema_guard))) {
          LOG_WARN("fail to get tenant schema guard", K(ret));
        } else if (OB_FAIL(schema_guard.get_table_schema(arg.tenant_id_, table_item.database_name_, table_item.table_name_, false/*is index*/, table_schema))) {
          LOG_WARN("fail to get table schema", K(ret));
        } else if (nullptr == table_schema) {
          // skip deleted table
        } else if (all_core_table_id == table_schema->get_table_id()) {
          // do nothing
        } else {
          if (lib::Worker::CompatMode::MYSQL == mode) {
            if (OB_FAIL(sql.append_fmt("OPTIMIZE TABLE `%.*s`",
                table_item.table_name_.length(), table_item.table_name_.ptr()))) {
              LOG_WARN("fail to assign sql stmt", K(ret));
            }
          } else if (lib::Worker::CompatMode::ORACLE == mode) {
            if (OB_FAIL(sql.append_fmt("ALTER TABLE \"%.*s\" SHRINK SPACE",
                table_item.table_name_.length(), table_item.table_name_.ptr()))) {
              LOG_WARN("fail to append fmt", K(ret));
            }
          } else {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("error unexpected, unknown mode", K(ret), K(mode));
          }
          if (OB_SUCC(ret)) {
            alter_table_arg.ddl_stmt_str_ = sql.string();
            obrpc::ObAlterTableRes res;
            if (OB_FAIL(alter_table_arg.alter_table_schema_.alter_option_bitset_.add_member(ObAlterTableArg::PROGRESSIVE_MERGE_ROUND))) {
              LOG_WARN("fail to add member", K(ret));
            } else if (OB_FAIL(alter_table(alter_table_arg, res))) {
              LOG_WARN("fail to alter table", K(ret), K(alter_table_arg));
            }
          }
        }
      }
    }
  }
  return ret;
}

int ObRootService::calc_column_checksum_repsonse(const obrpc::ObCalcColumnChecksumResponseArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, PROCESS_COLUMN_CHECKSUM_RESPONSE_SLOW))) {
    LOG_WARN("ddl sim failure: procesc column checksum response slow", K(ret));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::on_column_checksum_calc_reply(
              arg.tablet_id_, ObDDLTaskKey(arg.tenant_id_, arg.target_table_id_, arg.schema_version_), arg.ret_code_))) {
    LOG_WARN("handle column checksum calc response failed", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::root_minor_freeze(const ObRootMinorFreezeArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive minor freeze request", K(arg));

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(root_minor_freeze_.try_minor_freeze(arg))) {
    LOG_WARN("minor freeze failed", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("root_service", "root_minor_freeze", K(ret), K(arg));
  return ret;
}

int ObRootService::update_index_status(const obrpc::ObUpdateIndexStatusArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.update_index_status(arg))) {
    LOG_WARN("update index table status failed", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "update index status",
                        "tenant_id", arg.exec_tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "index_table_id", arg.index_table_id_,
                        "data_table_id", arg.data_table_id_);
  return ret;
}

int ObRootService::update_mview_status(const obrpc::ObUpdateMViewStatusArg &arg)
{
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.exec_tenant_id_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.update_mview_status(arg))) {
    LOG_WARN("update mview table status failed", KR(ret), K(arg));
  }
  return ret;
}

int ObRootService::parallel_update_index_status(const obrpc::ObUpdateIndexStatusArg &arg, obrpc::ObParallelDDLRes &res)
{
  LOG_TRACE("receive update index status arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  const uint64_t tenant_id = arg.exec_tenant_id_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid() || OB_INVALID_ID == arg.data_table_id_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_(tenant_id))) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret), K(tenant_id));
  } else {
    ObUpdateIndexStatusHelper update_index_status_helper(schema_service_, tenant_id, arg, res);
    if (OB_FAIL(update_index_status_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create table helper", KR(ret), K(tenant_id));
    } else if (OB_FAIL(update_index_status_helper.execute())) {
      LOG_WARN("fail to execute update index status helper", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  LOG_TRACE("finish update index status", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel update index status",
                        "tenant_id", arg.exec_tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "index_table_id", arg.index_table_id_,
                        "data_table_id", arg.data_table_id_);

  return ret;
}

int ObRootService::init_debug_database()
{
  const schema_create_func *creator_ptr_array[] = {
    core_table_schema_creators,
    sys_table_schema_creators,
    NULL};

  int ret = OB_SUCCESS;
  HEAP_VAR(char[OB_MAX_SQL_LENGTH], sql) {
    if (!inited_) {
      ret = OB_NOT_INIT;
      LOG_WARN("not init", K(ret));
    }

    ObTableSchema table_schema;
    ObSqlString create_func_sql;
    ObSqlString del_sql;
    for (const schema_create_func **creator_ptr_ptr = creator_ptr_array;
         OB_SUCCESS == ret && NULL != *creator_ptr_ptr; ++creator_ptr_ptr) {
      for (const schema_create_func *creator_ptr = *creator_ptr_ptr;
           OB_SUCCESS == ret && NULL != *creator_ptr; ++creator_ptr) {
        table_schema.reset();
        create_func_sql.reset();
        del_sql.reset();
        if (OB_FAIL((*creator_ptr)(table_schema))) {
          LOG_WARN("create table schema failed", K(ret));
          ret = OB_SCHEMA_ERROR;
        } else {
          int64_t affected_rows = 0;
          // ignore create function result
          int temp_ret = OB_SUCCESS;
          if (OB_SUCCESS != (temp_ret = create_func_sql.assign(
                      "create function time_to_usec(t timestamp) "
                      "returns bigint(20) deterministic begin return unix_timestamp(t); end;"))) {
            LOG_WARN("create_func_sql assign failed", K(temp_ret));
          } else if (OB_SUCCESS != (temp_ret = sql_proxy_.write(
                      create_func_sql.ptr(), affected_rows))) {
            LOG_WARN("execute sql failed", K(create_func_sql), K(temp_ret));
          } else if (OB_SUCCESS != (temp_ret = create_func_sql.assign(
                      "create function usec_to_time(u bigint(20)) "
                      "returns timestamp deterministic begin return from_unixtime(u); end;"))) {
            LOG_WARN("create_func_sql assign failed", K(temp_ret));
          } else if (OB_SUCCESS != (temp_ret = sql_proxy_.write(
                      create_func_sql.ptr(), affected_rows))) {
            LOG_WARN("execute sql failed", K(create_func_sql), K(temp_ret));
          }

          memset(sql, 0, sizeof(sql));
          if (OB_FAIL(del_sql.assign_fmt(
                      "DROP table IF EXISTS %s", table_schema.get_table_name()))) {
            LOG_WARN("assign sql failed", K(ret));
          } else if (OB_FAIL(sql_proxy_.write(del_sql.ptr(), affected_rows))) {
            LOG_WARN("execute sql failed", K(ret));
          } else if (OB_FAIL(ObSchema2DDLSql::convert(
                      table_schema, sql, sizeof(sql)))) {
            LOG_WARN("convert table schema to create table sql failed", K(ret));
          } else if (OB_FAIL(sql_proxy_.write(sql, affected_rows))) {
            LOG_WARN("execute sql failed", K(ret), K(sql));
          }
        }
      }
    }

    LOG_INFO("init debug database finish.", K(ret));
  }
  return ret;
}

int ObRootService::do_restart()
{
  int ret = OB_SUCCESS;

  const int64_t tenant_id = OB_SYS_TENANT_ID;
  // NOTE: following log print after lock
  FLOG_INFO("[ROOTSERVICE_NOTICE] start do_restart");

  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("not init", KR(ret));
  }

  if (OB_SUCC(ret)) {
    //standby cluster trigger load_refresh_schema_status by heartbeat.
    //due to switchover, primary cluster need to load schema_status too.
    ObSchemaStatusProxy *schema_status_proxy = GCTX.schema_status_proxy_;
    if (OB_ISNULL(schema_status_proxy)) {
      ret = OB_ERR_UNEXPECTED;
      FLOG_WARN("schema_status_proxy is null", KR(ret));
    } else if (OB_FAIL(schema_status_proxy->load_refresh_schema_status())) {
      FLOG_WARN("fail to load refresh schema status", KR(ret));
    } else {
      FLOG_INFO("load schema status success");
    }
  }

  bool load_frozen_status = true;
  const bool refresh_server_need_retry = false; // no need retry
  // try fast recover
  if (OB_SUCC(ret)) {
    int tmp_ret = refresh_schema(load_frozen_status);
    if (OB_SUCCESS != tmp_ret) {
      FLOG_WARN("refresh schema failed", KR(tmp_ret), K(load_frozen_status));
    }
  }
  load_frozen_status = false;
  // refresh schema
  if (FAILEDx(refresh_schema(load_frozen_status))) {
    FLOG_WARN("refresh schema failed", KR(ret), K(load_frozen_status));
  } else {
    FLOG_INFO("success to refresh schema", K(load_frozen_status));
  }

  // start timer tasks
  if (FAILEDx(start_timer_tasks())) {
    FLOG_WARN("start timer tasks failed", KR(ret));
  } else {
    FLOG_INFO("success to start timer tasks");
  }

  if (FAILEDx(schema_history_recycler_.start())) {
    FLOG_WARN("schema_history_recycler start failed", KR(ret));
  } else {
    FLOG_INFO("success to start schema_history_recycler");
  }

  if (FAILEDx(dbms_job::ObDBMSJobMaster::get_instance().start())) {
    FLOG_WARN("failed to start dbms job master", KR(ret));
  } else {
    FLOG_INFO("success to start dbms job master");
  }

  // Schema refresh trigger is now managed by MTL framework
  // It will be started automatically when tenant is created
  // and checks tenant role at runtime to decide whether to refresh schema

  // to avoid increase rootservice_epoch while fail to restart RS,
  // put it and the end of restart RS.
  // start_ddl_service_ is compatible with old logic to increase rootservice_epoch.
  if (OB_FAIL(ret)) {
  } else if (OB_FAIL(start_ddl_service_())) {
    FLOG_WARN("failed to start ddl service", KR(ret));
  } else {
    FLOG_INFO("success to start ddl service", KR(ret));
  }

  if (FAILEDx(rs_status_.set_rs_status(status::FULL_SERVICE))) {
    FLOG_WARN("fail to set rs status", KR(ret));
  } else {
    FLOG_INFO("full_service !!! start to work!!");
    ROOTSERVICE_EVENT_ADD("root_service", "full_rootservice",
                          "result", ret, K_(self_addr));
    root_minor_freeze_.start();
    FLOG_INFO("root_minor_freeze_ started");
    int64_t now = ObTimeUtility::current_time();
    core_meta_table_version_ = now;
    // reset fail count for self checker and print log.
    reset_fail_count();
  }

  if (OB_FAIL(ret)) {
    update_fail_count(ret);
  }

  FLOG_INFO("[ROOTSERVICE_NOTICE] finish do_restart", KR(ret));
  return ret;
}

bool ObRootService::in_service() const
{
  return rs_status_.in_service();
}

bool ObRootService::is_full_service() const
{
  return rs_status_.is_full_service();
}

bool ObRootService::is_start() const
{
  return rs_status_.is_start();
}

bool ObRootService::is_stopping() const
{
  return rs_status_.is_stopping();
}

bool ObRootService::is_need_stop() const
{
  return rs_status_.is_need_stop();
}

bool ObRootService::can_start_service() const
{
  return rs_status_.can_start_service();
}


bool ObRootService::need_do_restart() const
{
  return rs_status_.need_do_restart();
}

int ObRootService::revoke_rs()
{
  return rs_status_.revoke_rs();
}
int ObRootService::check_parallel_ddl_conflict(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const obrpc::ObDDLArg &arg)
{
  return ddl_service_.check_parallel_ddl_conflict(schema_guard, arg);
}

int ObRootService::increase_rs_epoch_and_get_proposal_id_(
    int64_t &new_rs_epoch,
    int64_t &proposal_id_to_check)
{
  int ret = OB_SUCCESS;
  ObMySQLTransaction trans;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", KR(ret), K(schema_service_));
  } else if (OB_FAIL(trans.start(&sql_proxy_, OB_SYS_TENANT_ID))) {
    LOG_WARN("trans start failed", K(ret));
  } else {
    ObGlobalStatProxy proxy(trans, OB_SYS_TENANT_ID);
    ObSchemaService *schema_service = schema_service_->get_schema_service();
    int64_t schema_version = OB_INVALID_VERSION;
    ObRefreshSchemaInfo schema_info;
    common::ObRole role = FOLLOWER;
    int64_t proposal_id_double_check = 0;
    // 1. get role and proposal id from PALF to make sure local is leader
    // ATTENTION:
    //   start_ddl_service will check ObDDLServiceLauncher::is_ddl_service_started_
    //   to decide whether start ddl service with old logic
    //   we can ensure that RS try start ddl service after __all_core_table be readable
    //   because operations like unit_manager_.load() can make sure sys leader's
    //   switch_to_leader() successfully called.
    //   In other words, sys leader's switch_to_leader() must before RS start_ddl_service()
    //   Based on this reason, we can make sure RS can start with old logic by checking
    //   ObDDLServiceLauncher::is_ddl_service_started_
    //   So we have to check log handle leader here
    if (OB_FAIL(ObDDLUtil::get_sys_log_handler_role_and_proposal_id(
                    role, proposal_id_to_check))) {
      LOG_WARN("fail to get sys log handler role and proposal id", KR(ret));
    } else if (OB_UNLIKELY(!is_strong_leader(role))) {
      ret = OB_LS_NOT_LEADER;
      LOG_WARN("local is not sys tenant leader", KR(ret), K(role), K(proposal_id_to_check));
    // 2. increase rootservice_epoch in __all_core_table and make sure it is valid
    } else if (OB_FAIL(proxy.inc_rootservice_epoch())) {
      LOG_WARN("fail to increase rootservice_epoch", KR(ret));
    } else if (OB_FAIL(proxy.get_rootservice_epoch(new_rs_epoch))) {
      LOG_WARN("fail to get rootservice start times", KR(ret), K(new_rs_epoch));
    } else if (new_rs_epoch <= 0) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("invalid rootservice_epoch", KR(ret), K(new_rs_epoch));
    // 3. double check local is still leader and proposal id not changed before commit
    //    it's ok to remove double check here, we just want to let it fail as soon as possible
    } else if (OB_FAIL(ObDDLUtil::get_sys_log_handler_role_and_proposal_id(
                       role, proposal_id_double_check))) {
      LOG_WARN("fail to get sys log handler role and proposal id", KR(ret));
    } else if (OB_UNLIKELY(!is_strong_leader(role))
               || OB_UNLIKELY(proposal_id_double_check != proposal_id_to_check)) {
      ret = OB_LS_NOT_LEADER;
      LOG_WARN("local is not sys tenant leader now", KR(ret), K(role), K(proposal_id_double_check));
    }
    // 4. commit transation
    int temp_ret = OB_SUCCESS;
    if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCCESS == ret))) {
      LOG_WARN("trans end failed", "commit", OB_SUCCESS == ret, K(temp_ret));
      ret = (OB_SUCCESS == ret) ? temp_ret : ret;
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERROR_EVENT_TABLE_CLEAR_INTERVAL);
int ObRootService::start_timer_tasks()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }

  if (OB_SUCCESS == ret && !task_queue_.exist_timer_task(event_table_clear_task_)) {
    const int64_t delay = ERROR_EVENT_TABLE_CLEAR_INTERVAL ? 10 * 1000 * 1000 :
      ObEventHistoryTableOperator::EVENT_TABLE_CLEAR_INTERVAL;
    if (OB_FAIL(task_queue_.add_repeat_timer_task_schedule_immediately(event_table_clear_task_, delay))) {
      LOG_WARN("start event table clear task failed", K(delay), K(ret));
    } else {
      LOG_INFO("added event_table_clear_task", K(delay));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(schedule_load_ddl_task())) {
      LOG_WARN("schedule load ddl task failed", K(ret));
    }
  }

  if (OB_SUCC(ret)) {
    if (OB_FAIL(schedule_refresh_io_calibration_task())) {
      LOG_WARN("schedule refresh io calibration task failed", K(ret));
    }
  }

  LOG_INFO("start all timer tasks finish", K(ret));
  return ret;
}

int ObRootService::stop_timer_tasks()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    task_queue_.cancel_timer_task(restart_task_);
    task_queue_.cancel_timer_task(event_table_clear_task_);
    inspect_task_queue_.cancel_timer_task(purge_recyclebin_task_);
  }

  //stop other timer tasks here
  LOG_INFO("stop all timer tasks finish", K(ret));
  return ret;
}

ObRootService::ObRestartTask::ObRestartTask(ObRootService &root_service)
:ObAsyncTimerTask(root_service.task_queue_),
    root_service_(root_service)
{
  set_retry_times(0);  // don't retry when failed
}

ObRootService::ObRestartTask::~ObRestartTask()
{
}

int ObRootService::ObRestartTask::process()
{
  int ret = OB_SUCCESS;
  int tmp_ret = OB_SUCCESS;
  FLOG_INFO("after_restart task begin to process");
  if (GCTX.in_bootstrap_) {
    ret = OB_EAGAIN;
    LOG_INFO("in bootstrap progress, after_restart should wait", KR(ret), K(GCTX.in_bootstrap_));
    if (OB_TMP_FAIL(root_service_.reschedule_restart_timer_task_after_failure())) {
      LOG_WARN("failed to reschedule restart time task", KR(tmp_ret));
    }
  } else if (OB_FAIL(root_service_.after_restart())) {
    LOG_WARN("root service after restart failed", K(ret));
  }
  FLOG_INFO("after_restart task process finish", KR(ret));
  return ret;
}

ObAsyncTask *ObRootService::ObRestartTask::deep_copy(char *buf, const int64_t buf_size) const
{
  ObRestartTask *task = NULL;
  if (NULL == buf || buf_size < static_cast<int64_t>(sizeof(*this))) {
    LOG_WARN_RET(OB_BUF_NOT_ENOUGH, "buffer not large enough", K(buf_size));
  } else {
    task = new(buf) ObRestartTask(root_service_);
  }
  return task;
}

//-----Functions for managing privileges------
int ObRootService::create_user(obrpc::ObCreateUserArg &arg,
                               common::ObSArray<int64_t> &failed_index)
{
  int ret = OB_SUCCESS;
  failed_index.reset();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.create_user(arg, failed_index))){
    LOG_WARN("create user failed", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::drop_user(const ObDropUserArg &arg,
                             common::ObSArray<int64_t> &failed_index)
{
  int ret = OB_SUCCESS;
  failed_index.reset();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.drop_user(arg, failed_index))) {
    LOG_WARN("drop user failed", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::rename_user(const obrpc::ObRenameUserArg &arg,
                               common::ObSArray<int64_t> &failed_index)
{
  int ret = OB_SUCCESS;
  failed_index.reset();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.rename_user(arg, failed_index))){
    LOG_WARN("rename user failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::alter_role(const obrpc::ObAlterRoleArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if(!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.alter_role(arg))) {
    LOG_WARN("alter role failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::set_passwd(const obrpc::ObSetPasswdArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.set_passwd(arg))){
    LOG_WARN("set passwd failed",  K(arg), K(ret));
  }
  return ret;
}

int ObRootService::grant(const ObGrantArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.grant(arg))) {
    LOG_WARN("Grant user failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::revoke_user(const ObRevokeUserArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.revoke(arg))) {
    LOG_WARN("revoke privilege failed", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::lock_user(const ObLockUserArg &arg, ObSArray<int64_t> &failed_index)
{
  int ret = OB_SUCCESS;
  failed_index.reset();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.lock_user(arg, failed_index))){
    LOG_WARN("lock user failed", K(arg), K(ret));
  }
  return ret;
}


int ObRootService::create_directory(const obrpc::ObCreateDirectoryArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.create_directory(arg, &arg.ddl_stmt_str_))) {
    LOG_WARN("create directory failed", K(arg.schema_), K(ret));
  }
  return ret;
}

int ObRootService::drop_directory(const obrpc::ObDropDirectoryArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.drop_directory(arg, &arg.ddl_stmt_str_))) {
    LOG_WARN("drop directory failed", K(arg.directory_name_), K(ret));
  }
  return ret;
}

int ObRootService::handle_catalog_ddl(const obrpc::ObCatalogDDLArg &arg)
{
  int ret = OB_SUCCESS;
  uint64_t data_version = 0;
  ObCatalogDDLService catalog_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(catalog_ddl_service.handle_catalog_ddl(arg))) {
    LOG_WARN("handle ddl failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::revoke_catalog(const ObRevokeCatalogArg &arg)
{
  int ret = OB_SUCCESS;
  ObCatalogDDLService catalog_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(catalog_ddl_service.revoke_catalog(arg))) {
    LOG_WARN("Grant catalog error", K(ret), K(arg.tenant_id_), K(arg.user_id_));
  }
  return ret;
}

int ObRootService::revoke_database(const ObRevokeDBArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObOriginalDBKey db_key(arg.tenant_id_, arg.user_id_, arg.db_);
    if (OB_FAIL(ddl_service_.revoke_database(db_key, arg.priv_set_))) {
      LOG_WARN("Revoke db failed", K(arg), K(ret));
    }
  }
  return ret;
}

int ObRootService::revoke_table(const ObRevokeTableArg &arg)
{
  int ret = OB_SUCCESS;
  lib::Worker::CompatMode mode;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ObCompatModeGetter::get_tenant_mode(arg.tenant_id_, mode))) {
    LOG_WARN("fail to get tenant mode", K(ret));
  } else if (lib::Worker::CompatMode::ORACLE == mode) {
    ObTablePrivSortKey table_priv_key(arg.tenant_id_, arg.user_id_, arg.db_, arg.table_);
    ObObjPrivSortKey obj_priv_key(arg.tenant_id_,
                                  arg.obj_id_,
                                  arg.obj_type_,
                                  OBJ_LEVEL_FOR_TAB_PRIV,
                                  arg.grantor_id_,
                                  arg.user_id_);
    OZ (ddl_service_.revoke_table(arg,
                                  table_priv_key,
                                  arg.priv_set_,
                                  obj_priv_key,
                                  arg.obj_priv_array_,
                                  arg.revoke_all_ora_));
  } else if (lib::Worker::CompatMode::MYSQL == mode) {
    if (OB_FAIL(ddl_service_.revoke_table_and_column_mysql(arg))) {
      LOG_WARN("revoke table and col failed", K(ret));
    }
  } else {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("unexpected feature action", K(ret));
  }
  return ret;
}

int ObRootService::revoke_routine(const ObRevokeRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObRoutinePrivSortKey routine_priv_key(arg.tenant_id_, arg.user_id_, arg.db_, arg.routine_,
                            (arg.obj_type_ == (int64_t)ObObjectType::PROCEDURE) ? ObRoutineType::ROUTINE_PROCEDURE_TYPE
                           : (arg.obj_type_ == (int64_t)ObObjectType::FUNCTION) ? ObRoutineType::ROUTINE_FUNCTION_TYPE
                           : ObRoutineType::INVALID_ROUTINE_TYPE);
    OZ (ddl_service_.revoke_routine(routine_priv_key, arg.priv_set_, arg.grantor_, arg.grantor_host_));
  }
  return ret;
}



//-----End of functions for managing privileges-----

//-----Functions for managing outlines-----
int ObRootService::create_outline(const ObCreateOutlineArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObOutlineInfo outline_info = arg.outline_info_;
    const bool is_or_replace = arg.or_replace_;
    uint64_t tenant_id = outline_info.get_tenant_id();
    ObString database_name = arg.db_name_;
    ObSchemaGetterGuard schema_guard;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (database_name == OB_MOCK_DEFAULT_DATABASE_NAME) {
      // if not specify database, set default database name and database id;
      outline_info.set_database_id(OB_MOCK_DEFAULT_DATABASE_ID);
    } else if (OB_FAIL(schema_guard.get_database_schema(tenant_id, database_name, db_schema))) {
      LOG_WARN("get database schema failed", K(ret));
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("Can't not create outline of db in recyclebin", K(ret), K(arg), K(*db_schema));
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database id is invalid", K(tenant_id), K(*db_schema), K(ret));
    } else {
      outline_info.set_database_id(db_schema->get_database_id());
    }

    bool is_update = false;
    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service_.check_outline_exist(outline_info, is_or_replace, is_update))) {
        LOG_WARN("failed to check_outline_exist", K(outline_info), K(is_or_replace), K(is_update), K(ret));
      }
    }

    if (OB_SUCC(ret)) {
      if (OB_FAIL(ddl_service_.create_outline(outline_info, is_update, &arg.ddl_stmt_str_, schema_guard))) {
        LOG_WARN("create_outline failed", K(outline_info), K(is_update), K(ret));
      }
    }
  }
  return ret;
}

int ObRootService::create_user_defined_function(const obrpc::ObCreateUserDefinedFunctionArg &arg)
{
  int ret = OB_SUCCESS;
  bool exist = false;
  uint64_t udf_id = OB_INVALID_ID;
  ObUDF udf_info_ = arg.udf_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.check_udf_exist(arg.udf_.get_tenant_id(), arg.udf_.get_name_str(), exist, udf_id))) {
    LOG_WARN("failed to check_udf_exist", K(arg.udf_.get_tenant_id()), K(arg.udf_.get_name_str()), K(exist), K(ret));
  } else if (exist) {
    ret = OB_UDF_EXISTS;
    LOG_USER_ERROR(OB_UDF_EXISTS, arg.udf_.get_name_str().length(), arg.udf_.get_name_str().ptr());
  } else if (OB_FAIL(ddl_service_.create_user_defined_function(udf_info_, arg.ddl_stmt_str_))) {
    LOG_WARN("failed to create udf", K(arg), K(ret));
  } else {/*do nothing*/}
  return ret;
}

int ObRootService::drop_user_defined_function(const obrpc::ObDropUserDefinedFunctionArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.drop_user_defined_function(arg))) {
    LOG_WARN("failed to alter udf", K(arg), K(ret));
  } else {/*do nothing*/}

  return ret;
}


int ObRootService::alter_outline(const ObAlterOutlineArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.alter_outline(arg))) {
    LOG_WARN("failed to alter outline", K(arg), K(ret));
  } else {/*do nothing*/}
  return ret;
}

int ObRootService::drop_outline(const obrpc::ObDropOutlineArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    if (OB_FAIL(ddl_service_.drop_outline(arg))) {
      LOG_WARN("ddl service failed to drop outline", K(arg), K(ret));
    }
  }
  return ret;
}
//-----End of functions for managing outlines-----

int ObRootService::create_routine(const ObCreateRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_routine(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::create_routine_with_res(const ObCreateRoutineArg &arg,
                                           obrpc::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_routine(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::alter_routine(const ObCreateRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_routine(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::alter_routine_with_res(const ObCreateRoutineArg &arg,
                                          obrpc::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_routine(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::drop_routine(const ObDropRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_routine(arg, ddl_service_));
  return ret;
}

int ObRootService::admin_sync_rewrite_rules(const obrpc::ObSyncRewriteRuleArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminSyncRewriteRules admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("dispatch sync rewrite rules failed", K(arg), K(ret));
      }
      ROOTSERVICE_EVENT_ADD("root_service", "admin_sync_rewrite_rules", K(ret), K(arg));
    }
  }
  return ret;
}

int ObRootService::create_package(const obrpc::ObCreatePackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_package(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::create_package_with_res(const obrpc::ObCreatePackageArg &arg,
                                           obrpc::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_package(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::alter_package(const obrpc::ObAlterPackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_package(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::alter_package_with_res(const obrpc::ObAlterPackageArg &arg,
                                          obrpc::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_package(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::drop_package(const obrpc::ObDropPackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_package(arg, ddl_service_));
  return ret;
}

int ObRootService::create_trigger(const obrpc::ObCreateTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_trigger(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::create_trigger_with_res(const obrpc::ObCreateTriggerArg &arg,
                                           obrpc::ObCreateTriggerRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_trigger(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::alter_trigger(const obrpc::ObAlterTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_trigger(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::alter_trigger_with_res(const obrpc::ObAlterTriggerArg &arg,
                                          obrpc::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_trigger(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::drop_trigger(const obrpc::ObDropTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_trigger(arg, ddl_service_));
  return ret;
}

////////////////////////////////////////////////////////////////
// sequence
////////////////////////////////////////////////////////////////
int ObRootService::do_sequence_ddl(const obrpc::ObSequenceDDLArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.do_sequence_ddl(arg))) {
    LOG_WARN("do sequence ddl failed", K(arg), K(ret));
  }
  return ret;
}

////////////////////////////////////////////////////////////////
// context
////////////////////////////////////////////////////////////////
int ObRootService::do_context_ddl(const obrpc::ObContextDDLArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.do_context_ddl(arg))) {
    LOG_WARN("do context ddl failed", K(arg), K(ret));
  }
  return ret;
}

////////////////////////////////////////////////////////////////
// schema revise
////////////////////////////////////////////////////////////////
int ObRootService::schema_revise(const obrpc::ObSchemaReviseArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.do_schema_revise(arg))) {
    LOG_WARN("schema revise failed", K(arg), K(ret));
  }
  return ret;
}

////////////////////////////////////////////////////////////////
// system admin command (alter system ...)
////////////////////////////////////////////////////////////////
int ObRootService::init_sys_admin_ctx(ObSystemAdminCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ctx.rs_status_ = &rs_status_;
    ctx.rpc_proxy_ = &rpc_proxy_;
    ctx.sql_proxy_ = &sql_proxy_;
    ctx.schema_service_ = schema_service_;
    ctx.ddl_service_ = &ddl_service_;
    ctx.config_mgr_ = config_mgr_;
    ctx.root_service_ = this;
    ctx.inited_ = true;
  }
  return ret;
}

int ObRootService::admin_flush_cache(const obrpc::ObAdminFlushCacheArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminFlushCache admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("dispatch flush cache failed", K(arg), K(ret));
      }
      ROOTSERVICE_EVENT_ADD("root_service", "admin_flush_cache", K(ret), K(arg));
    }
  }
  return ret;
}

int ObRootService::admin_merge(const obrpc::ObAdminMergeArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminMerge admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute merge control failed", K(arg), K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_merge", K(ret), K(arg));
  return ret;
}

int ObRootService::admin_recovery(const obrpc::ObAdminRecoveryArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRootService::admin_clear_roottable(const obrpc::ObAdminClearRoottableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminClearRoottable admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute clear root table failed", K(arg), K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_clear_roottable", K(ret), K(arg));
  return ret;
}

int ObRootService::admin_refresh_schema(const obrpc::ObAdminRefreshSchemaArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminRefreshSchema admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute refresh schema failed", K(arg), K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_refresh_schema", K(ret), K(arg));
  return ret;
}

int ObRootService::admin_set_config(obrpc::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (arg.is_backup_config_) {
    if (OB_FAIL(admin_set_backup_config(arg))) {
      LOG_WARN("fail to set backup config", K(ret), K(arg));
    }
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      bool lock_succ = false;
      ObAdminSetConfig admin_util(ctx);
      if (OB_FAIL(set_config_lock_.wrlock(ObLatchIds::CONFIG_LOCK, THIS_WORKER.get_timeout_ts()))) {
        LOG_WARN("fail to wrlock CONFIG_LOCK", KR(ret), "abs_timeout", THIS_WORKER.get_timeout_ts());
      } else if (FALSE_IT(lock_succ = true)) {
      } else if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute set config failed", K(arg), K(ret));
      }
      if (lock_succ) {
        int tmp_ret = OB_SUCCESS;
        if (OB_TMP_FAIL(set_config_lock_.unlock())) {
          LOG_ERROR("unlock failed", KR(tmp_ret), KR(ret));
        }
      }
    }
  }
  // Add event one by one if more than one parameters are set
  for (int i = 0; i < arg.items_.count(); i++) {
    ROOTSERVICE_EVENT_ADD_TRUNCATE("root_service", "admin_set_config", K(ret), "arg", arg.items_.at(i), "is_inner", arg.is_inner_);
  }
  return ret;
}

int ObRootService::admin_refresh_memory_stat(const ObAdminRefreshMemStatArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminRefreshMemStat admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute refresh memory stat failed", K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_refresh_memory_stat", K(ret));
  return ret;
}

int ObRootService::admin_wash_memory_fragmentation(const ObAdminWashMemFragmentationArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminWashMemFragmentation admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute refresh memory stat failed", K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_wash_memory_fragmentation", K(ret));
  return ret;
}

int ObRootService::admin_refresh_io_calibration(const obrpc::ObAdminRefreshIOCalibrationArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminRefreshIOCalibration admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute refresh io calibration failed", K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_refresh_io_calibration", K(ret));
  return ret;
}

int ObRootService::admin_clear_merge_error(const obrpc::ObAdminMergeArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("admin receive clear_merge_error request");
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", KR(ret));
    } else {
      ObAdminClearMergeError admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute clear merge error failed", KR(ret), K(arg));
      }
      ROOTSERVICE_EVENT_ADD("root_service", "clear_merge_error", KR(ret), K(arg));
    }
  }
  return ret;
}

int ObRootService::admin_upgrade_virtual_schema()
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("upgrade in lite version not supported", KR(ret));
  return ret;
}

int ObRootService::admin_upgrade_cmd(const obrpc::Bool &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminUpgradeCmd admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("begin upgrade failed", K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_upgrade_cmd", K(ret), K(arg));
  return ret;
}

int ObRootService::admin_rolling_upgrade_cmd(const obrpc::ObAdminRollingUpgradeArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminRollingUpgradeCmd admin_util(ctx);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("begin upgrade failed", K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_rolling_upgrade_cmd", K(ret), K(arg));
  return ret;
}

int ObRootService::run_upgrade_job(const obrpc::ObUpgradeJobArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_WARN("upgrade in lite version not supported now", KR(ret), K(arg));
  return ret;
}

int ObRootService::broadcast_ds_action(const obrpc::ObDebugSyncActionArg &arg)
{
  LOG_INFO("receive broadcast debug sync actions", K(arg));
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(rpc_proxy_.to(GCTX.self_addr()).timeout(config_->rpc_timeout).set_debug_sync_action(arg))) {
    LOG_WARN("set server's global sync action failed", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::check_dangling_replica_finish(const obrpc::ObCheckDanglingReplicaFinishArg &arg)
{
  UNUSED(arg);
  return OB_NOT_SUPPORTED;
}

int ObRootService::refresh_schema(const bool load_frozen_status)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObTimeoutCtx ctx;
    int64_t schema_version = OB_INVALID_VERSION;
    if (load_frozen_status) {
      ctx.set_timeout(config_->rpc_timeout);
    }
    ObArray<uint64_t> tenant_ids; //depend on sys schema while start RS
    if (OB_FAIL(tenant_ids.push_back(OB_SYS_TENANT_ID))) {
      LOG_WARN("fail to refresh sys schema", K(ret));
    } else if (OB_FAIL(schema_service_->refresh_and_add_schema(tenant_ids))) {
      LOG_WARN("refresh schema failed", K(ret), K(load_frozen_status));
    } else if (OB_FAIL(schema_service_->get_tenant_schema_version(OB_SYS_TENANT_ID, schema_version))) {
      LOG_WARN("fail to get max schema version", K(ret));
    } else {
      LOG_INFO("refresh schema with new mode succeed", K(load_frozen_status), K(schema_version));
    }
    if (OB_SUCC(ret)) {
      ObSchemaService *schema_service = schema_service_->get_schema_service();
      if (NULL == schema_service) {
        ret = OB_ERR_SYS;
        LOG_WARN("schema_service can't be null", K(ret), K(schema_version));
      } else {
        schema_service->set_refreshed_schema_version(schema_version);
        LOG_INFO("set schema version succeed", K(ret), K(schema_service), K(schema_version));
      }
    }
  }
  return ret;
}

int ObRootService::set_cluster_version()
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  char sql[1024] = {0};
  ObMySQLProxy &sql_proxy = ddl_service_.get_sql_proxy();

  snprintf(sql, sizeof(sql), "alter system set min_observer_version = '%s'", PACKAGE_VERSION);
  if (OB_FAIL(sql_proxy.write(OB_SYS_TENANT_ID, sql, affected_rows))) {
    LOG_WARN("execute sql failed", K(sql));
  }

  return ret;
}

int ObRootService::admin_set_tracepoint(const obrpc::ObAdminSetTPArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    ObSystemAdminCtx ctx;
    if (OB_FAIL(init_sys_admin_ctx(ctx))) {
      LOG_WARN("init_sys_admin_ctx failed", K(ret));
    } else {
      ObAdminSetTP admin_util(ctx, arg);
      if (OB_FAIL(admin_util.execute(arg))) {
        LOG_WARN("execute report replica failed", K(arg), K(ret));
      }
    }
  }
  ROOTSERVICE_EVENT_ADD("root_service", "admin_set_tracepoint", K(ret), K(arg));
  return ret;
}

// RS may receive refresh time zone from observer with old binary during upgrade.
// do notiong
int ObRootService::refresh_time_zone_info(const obrpc::ObRefreshTimezoneArg &arg)
{
  int ret = OB_SUCCESS;
  UNUSED(arg);
  ROOTSERVICE_EVENT_ADD("root_service", "refresh_time_zone_info", K(ret), K(arg));
  return ret;
}

int ObRootService::request_time_zone_info(const ObRequestTZInfoArg &arg, ObRequestTZInfoResult &result)
{
  UNUSED(arg);
  int ret = OB_SUCCESS;
  uint64_t tenant_id = OB_SYS_TENANT_ID;

  ObTZMapWrap tz_map_wrap;
  ObTimeZoneInfoManager *tz_info_mgr = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(OTTZ_MGR.get_tenant_timezone(tenant_id, tz_map_wrap, tz_info_mgr))) {
    LOG_WARN("get tenant timezone failed", K(ret));
  } else if (OB_ISNULL(tz_info_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_tz_mgr failed", K(ret), K(tz_info_mgr));
  } else if (OB_FAIL(tz_info_mgr->response_time_zone_info(result))) {
    LOG_WARN("fail to response tz_info", K(ret));
  } else {
    LOG_INFO("rs success to response lastest tz_info to server", "server", arg.obs_addr_, "last_version", result.last_version_);
  }
  return ret;
}

bool ObRootService::check_config(const ObConfigItem &item, const char *&err_info)
{
  bool bret = true;
  err_info = NULL;
  if (!inited_) {
    bret = false;
    LOG_WARN_RET(OB_NOT_INIT, "service not init");
  } else if (0 == STRCMP(item.name(), MIN_OBSERVER_VERSION)) {
    if (OB_SUCCESS != ObClusterVersion::is_valid(item.str())) {
      LOG_WARN_RET(OB_INVALID_ERROR, "fail to parse min_observer_version value");
      bret = false;
    }
  }
  return bret;
}

ObRootService::ObLoadDDLTask::ObLoadDDLTask(ObRootService &root_service)
  : ObAsyncTimerTask(root_service.task_queue_), root_service_(root_service)
{
  set_retry_times(INT64_MAX);
}

int ObRootService::ObLoadDDLTask::process()
{
  return ObSysDDLSchedulerUtil::recover_task();
}

ObAsyncTask *ObRootService::ObLoadDDLTask::deep_copy(char *buf, const int64_t buf_size) const
{
  ObLoadDDLTask *task = nullptr;
  if (nullptr == buf || buf_size < static_cast<int64_t>(sizeof(*this))) {
    LOG_WARN_RET(OB_BUF_NOT_ENOUGH, "buf is not enough", K(buf_size), "request_size", sizeof(*this));
  } else {
    task = new (buf) ObLoadDDLTask(root_service_);
  }
  return task;
}

ObRootService::ObRefreshIOCalibrationTask::ObRefreshIOCalibrationTask(ObRootService &root_service)
  : ObAsyncTimerTask(root_service.task_queue_), root_service_(root_service)
{
  set_retry_times(INT64_MAX);
}

int ObRootService::ObRefreshIOCalibrationTask::process()
{
  int ret = OB_SUCCESS;
  obrpc::ObAdminRefreshIOCalibrationArg arg;
  arg.only_refresh_ = true;
  if (OB_FAIL(root_service_.admin_refresh_io_calibration(arg))) {
    LOG_WARN("refresh io calibration failed", K(ret), K(arg));
  } else {
    LOG_INFO("refresh io calibration succeeded");
  }
  return ret;
}

ObAsyncTask *ObRootService::ObRefreshIOCalibrationTask::deep_copy(char *buf, const int64_t buf_size) const
{
  ObRefreshIOCalibrationTask *task = nullptr;
  if (nullptr == buf || buf_size < static_cast<int64_t>(sizeof(*this))) {
    LOG_WARN_RET(OB_BUF_NOT_ENOUGH, "buf is not enough", K(buf_size), "request_size", sizeof(*this));
  } else {
    task = new (buf) ObRefreshIOCalibrationTask(root_service_);
  }
  return task;
}

/////////////////////////
status::ObRootServiceStatus ObRootService::get_status() const
{
  return rs_status_.get_rs_status();
}

int ObRootService::table_allow_ddl_operation(const obrpc::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *schema = NULL;
  ObSchemaGetterGuard schema_guard;
  const AlterTableSchema &alter_table_schema = arg.alter_table_schema_;
  const uint64_t tenant_id = alter_table_schema.get_tenant_id();
  const ObString &origin_database_name = alter_table_schema.get_origin_database_name();
  const ObString &origin_table_name = alter_table_schema.get_origin_table_name();
  schema_guard.set_session_id(arg.session_id_);
  bool is_index = arg.alter_table_schema_.is_index_table();
  if (arg.is_refresh_sess_active_time()) {
    //do nothing
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invali argument", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(tenant_id, schema_guard))) {
    LOG_WARN("get schema guard in inner table failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema(tenant_id, origin_database_name,
                                                   origin_table_name, is_index, schema))) {
    LOG_WARN("fail to get table schema", K(ret), K(tenant_id), K(origin_database_name), K(origin_table_name));
  } else if (OB_ISNULL(schema)) {
    ret = OB_TABLE_NOT_EXIST;
    LOG_WARN("invalid schema", K(ret));
    ObCStringHelper helper;
    LOG_USER_ERROR(OB_TABLE_NOT_EXIST, helper.convert(origin_database_name), helper.convert(origin_table_name));
  } else if (schema->is_ctas_tmp_table()) {
    if (!alter_table_schema.alter_option_bitset_.has_member(ObAlterTableArg::SESSION_ID)) {
      //to prevet alter table after failed to create table, the table is invisible.
      ret = OB_OP_NOT_ALLOW;
      LOG_WARN("try to alter invisible table schema", K(schema->get_session_id()), K(arg));
      LOG_USER_ERROR(OB_OP_NOT_ALLOW, "try to alter invisible table");
    }
  } else if ((schema->required_by_mview_refresh() || schema->is_mlog_table()) &&
             !arg.is_alter_mlog_attributes_) {
    if (OB_FAIL(ObResolverUtils::check_allowed_alter_operations_for_mlog(arg, *schema))) {
      LOG_WARN("failed to check allowed alter operation for mlog", KR(ret), K(arg));
    }
  }
  return ret;
}

// ask each server to update statistic
int ObRootService::update_stat_cache(const obrpc::ObUpdateStatCacheArg &arg)
{
  int ret = OB_SUCCESS;
  ObZone null_zone;
  bool evict_plan_failed = false;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(rpc_proxy_.to(GCTX.self_addr()).update_local_stat_cache(arg))) {
      LOG_WARN("fail to update table statistic", K(ret));
      // OB_SQL_PC_NOT_EXIST represent evict plan failed
      if (OB_SQL_PC_NOT_EXIST == ret) {
        ret = OB_SUCCESS;
        evict_plan_failed = true;
      }
    } else { /*do nothing*/}
  }
  if (OB_SUCC(ret) && evict_plan_failed) {
    ret = OB_SQL_PC_NOT_EXIST;
  }
  return ret;
}

int ObRootService::check_weak_read_version_refresh_interval(int64_t refresh_interval, bool &valid)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard sys_schema_guard;
  ObArray<uint64_t> tenant_ids;
  valid = true;

  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, sys_schema_guard))) {
    LOG_WARN("get sys schema guard failed", KR(ret));
  } else if (OB_FAIL(sys_schema_guard.get_tenant_ids(tenant_ids))) {
    LOG_WARN("get tenant ids failed", KR(ret));
  } else {
    ObSchemaGetterGuard schema_guard;
    const ObSimpleTenantSchema *tenant_schema = NULL;
    const ObSysVarSchema *var_schema = NULL;
    ObObj obj;
    int64_t session_max_stale_time = 0;
    uint64_t tenant_id = OB_INVALID_TENANT_ID;
    for (int64_t i = 0; OB_SUCC(ret) && valid && i < tenant_ids.count(); i++) {
      tenant_id = tenant_ids[i];
      if (OB_FAIL(sys_schema_guard.get_tenant_info(tenant_id, tenant_schema))) {
        LOG_WARN("fail to get tenant schema", KR(ret), K(tenant_id));
      } else if (OB_ISNULL(tenant_schema)) {
        ret = OB_SUCCESS;
        LOG_WARN("tenant schema is null, skip and continue", KR(ret), K(tenant_id));
      } else if (!tenant_schema->is_normal()) {
        ret = OB_SUCCESS;
        LOG_WARN("tenant schema is not normal, skip and continue", KR(ret), K(tenant_id));
      } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(tenant_id, schema_guard))) {
        LOG_WARN("get schema guard failed", KR(ret), K(tenant_id));
      } else if (OB_FAIL(schema_guard.get_tenant_system_variable(tenant_id,
                         OB_SV_MAX_READ_STALE_TIME, var_schema))) {
        LOG_WARN("get tenant system variable failed", KR(ret), K(tenant_id));
      } else if (OB_ISNULL(var_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("var schema is null", KR(ret), K(tenant_id));
      } else if (OB_FAIL(var_schema->get_value(NULL, NULL, obj))) {
        LOG_WARN("get value failed", KR(ret), K(tenant_id), K(obj));
      } else if (OB_FAIL(obj.get_int(session_max_stale_time))) {
        LOG_WARN("get int failed", KR(ret), K(tenant_id), K(obj));
      } else if (session_max_stale_time != share::ObSysVarFactory::INVALID_MAX_READ_STALE_TIME
                 && refresh_interval > session_max_stale_time) {
        valid = false;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                       "weak_read_version_refresh_interval is larger than ob_max_read_stale_time");
      }
    }
  }
  return ret;
}

int ObRootService::set_config_pre_hook(obrpc::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  }
  FOREACH_X(item, arg.items_, OB_SUCCESS == ret) {
    bool valid = true;
    if (item->name_.is_empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("empty config name", "item", *item, K(ret));
    } else if (0 == STRCMP(item->name_.ptr(), _TX_SHARE_MEMORY_LIMIT_PERCENTAGE)) {
      ret = check_tx_share_memory_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), MEMSTORE_LIMIT_PERCENTAGE)) {
      ret = check_memstore_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), OB_VECTOR_MEMORY_LIMIT_PERCENTAGE)) {
      ret = check_vector_memory_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), DATA_DISK_WRITE_LIMIT_PERCENTAGE)) {
      ret = check_data_disk_write_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), DATA_DISK_USAGE_LIMIT_PERCENTAGE)) {
      ret = check_data_disk_usage_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), TENANT_MEMSTORE_LIMIT_PERCENTAGE)) {
      ret = check_tenant_memstore_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), _TX_DATA_MEMORY_LIMIT_PERCENTAGE)) {
      ret = check_tx_data_memory_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), _MDS_MEMORY_LIMIT_PERCENTAGE)) {
      ret = check_mds_memory_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), FREEZE_TRIGGER_PERCENTAGE)) {
      ret = check_freeze_trigger_percentage_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), WRITING_THROTTLEIUNG_TRIGGER_PERCENTAGE)) {
      ret = check_write_throttle_trigger_percentage(*item);
    } else if (0 == STRCMP(item->name_.ptr(), _NO_LOGGING)) {
      ret = check_no_logging(*item);
    } else if (0 == STRCMP(item->name_.ptr(), WEAK_READ_VERSION_REFRESH_INTERVAL)) {
      int64_t refresh_interval = ObConfigTimeParser::get(item->value_.ptr(), valid);
      if (valid && OB_FAIL(check_weak_read_version_refresh_interval(refresh_interval, valid))) {
        LOG_WARN("check refresh interval failed ", KR(ret), K(*item));
      } else if (!valid) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("config invalid", KR(ret), K(*item));
      }
    } else if (0 == STRCMP(item->name_.ptr(), PARTITION_BALANCE_SCHEDULE_INTERVAL)) {
      const int64_t DEFAULT_BALANCER_IDLE_TIME = 10 * 1000 * 1000L; // 10s
      for (int i = 0; i < item->tenant_ids_.count() && valid; i++) {
        const uint64_t tenant_id = item->tenant_ids_.at(i);
        omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id));
        int64_t balancer_idle_time = tenant_config.is_valid() ? tenant_config->balancer_idle_time : DEFAULT_BALANCER_IDLE_TIME;
        int64_t interval = ObConfigTimeParser::get(item->value_.ptr(), valid);
        if (valid) {
          if (0 == interval) {
            valid = true;
          } else if (interval >= balancer_idle_time) {
            valid = true;
          } else {
            valid = false;
            char err_msg[DEFAULT_BUF_LENGTH];
            (void)snprintf(err_msg, sizeof(err_msg), "partition_balance_schedule_interval of tenant %ld, "
                "it should not be less than balancer_idle_time", tenant_id);
            LOG_USER_ERROR(OB_INVALID_ARGUMENT, err_msg);
          }
        }
        if (!valid) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("config invalid", KR(ret), K(*item), K(balancer_idle_time), K(tenant_id));
        }
      }
    } else if (0 == STRCMP(item->name_.ptr(), BALANCER_IDLE_TIME)) {
      const int64_t DEFAULT_PARTITION_BALANCE_SCHEDULE_INTERVAL = 2 * 3600 * 1000 * 1000L; // 2h
      for (int i = 0; i < item->tenant_ids_.count() && valid; i++) {
        const uint64_t tenant_id = item->tenant_ids_.at(i);
        omt::ObTenantConfigGuard tenant_config(TENANT_CONF(tenant_id));
        int64_t interval = tenant_config.is_valid()
            ? tenant_config->partition_balance_schedule_interval
            : DEFAULT_PARTITION_BALANCE_SCHEDULE_INTERVAL;
        int64_t idle_time = ObConfigTimeParser::get(item->value_.ptr(), valid);
        if (valid && (idle_time > interval)) {
          valid = false;
          char err_msg[DEFAULT_BUF_LENGTH];
          (void)snprintf(err_msg, sizeof(err_msg), "balancer_idle_time of tenant %ld, "
              "it should not be longer than partition_balance_schedule_interval", tenant_id);
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, err_msg);
        }
        if (!valid) {
          ret = OB_INVALID_ARGUMENT;
          LOG_WARN("config invalid", KR(ret), K(*item), K(interval), K(tenant_id));
        }
      }
    } else if (0 == STRCMP(item->name_.ptr(), LOG_DISK_UTILIZATION_LIMIT_THRESHOLD)) {
      // check log_disk_utilization_limit_threshold
      for (int i = 0; i < item->tenant_ids_.count() && valid; i++) {
        valid = valid && ObConfigLogDiskLimitThresholdIntChecker::check(item->tenant_ids_.at(i), *item);
        if (!valid) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_disk_utilization_limit_threshold should be greater than log_disk_throttling_percentage "
                        "when log_disk_throttling_percentage is not equal to 100");
          LOG_WARN("config invalid", "item", *item, K(ret), K(i), K(item->tenant_ids_.at(i)));
        }
      }
    } else if (0 == STRCMP(item->name_.ptr(), LOG_DISK_THROTTLING_PERCENTAGE)) {
      // check log_disk_throttling_percentage
      for (int i = 0; i < item->tenant_ids_.count() && valid; i++) {
        valid = valid && ObConfigLogDiskThrottlingPercentageIntChecker::check(item->tenant_ids_.at(i), *item);
        if (!valid) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_disk_throttling_percentage should be equal to 100 or smaller than log_disk_utilization_limit_threshold");
          LOG_WARN("config invalid", "item", *item, K(ret), K(i), K(item->tenant_ids_.at(i)));
        }
      }
    } else if (0 == STRCMP(item->name_.ptr(), _TRANSFER_TASK_TABLET_COUNT_THRESHOLD)) {
      ret = check_transfer_task_tablet_count_threshold_(*item);
    }
  }
  return ret;
}

#define CHECK_TENANTS_CONFIG_WITH_FUNC(FUNCTOR, LOG_INFO)                                  \
  do {                                                                                     \
    bool valid = true;                                                                     \
    for (int i = 0; i < item.tenant_ids_.count() && valid; i++) {                          \
      valid = valid && FUNCTOR::check(item.tenant_ids_.at(i), item);                       \
      if (!valid) {                                                                        \
        ret = OB_INVALID_ARGUMENT;                                                         \
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, LOG_INFO);                                     \
        LOG_WARN("config invalid", "item", item, K(ret), K(i), K(item.tenant_ids_.at(i))); \
      }                                                                                    \
    }                                                                                      \
  } while (0)

#define CHECK_CLUSTER_CONFIG_WITH_FUNC(FUNCTOR, LOG_INFO)                                  \
  do {                                                                                     \
    bool valid = true;                                                                     \
    for (int i = 0; i < tenant_ids.count() && valid; i++) {                                \
      valid = valid && FUNCTOR::check(tenant_ids.at(i), item);                             \
      if (!valid) {                                                                        \
        ret = OB_INVALID_ARGUMENT;                                                         \
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, LOG_INFO);                                     \
        LOG_WARN("config invalid", "item", item, K(ret), K(i), K(tenant_ids.at(i)));       \
      }                                                                                    \
    }                                                                                      \
  } while (0)

int ObRootService::check_tx_share_memory_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  // There is a prefix "Incorrect arguments to " before user log so the warn log looked kinds of wired
  const char *warn_log = "tenant config _tx_share_memory_limit_percentage. "
                         "It should larger than or equal with any single module in it(Memstore, TxData, Mds, Vector)";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigTxShareMemoryLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_memstore_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "cluster config memstore_limit_percentage. "
                         "It should less than or equal with all tenant's _tx_share_memory_limit_percentage";
  ObArray<uint64_t> tenant_ids;
  ObSchemaGetterGuard schema_guard;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("get schema guard failed", KR(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_ids(tenant_ids))) {
    LOG_WARN("failed to get all tenant ids", KR(ret), K(tenant_ids));
  } else {
    CHECK_CLUSTER_CONFIG_WITH_FUNC(ObConfigMemstoreLimitChecker, warn_log);
  }
  return ret;
}

int ObRootService::check_vector_memory_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "ob_vector_limit_percentage. "
                         "It should be less than _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigVectorMemoryChecker, warn_log);
  return ret;
}

int ObRootService::check_tenant_memstore_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant config _memstore_limit_percentage. "
    "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigMemstoreLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_tx_data_memory_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant config _tx_data_memory_limit_percentage. "
                         "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigTxDataLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_mds_memory_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant config _mds_memory_limit_percentage. "
                         "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigMdsLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_freeze_trigger_percentage_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant freeze_trigger_percentage "
                         "which should smaller than writing_throttling_trigger_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigFreezeTriggerIntChecker, warn_log);
  return ret;
}

int ObRootService::check_write_throttle_trigger_percentage(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant writing_throttling_trigger_percentage "
                         "which should greater than freeze_trigger_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigWriteThrottleTriggerIntChecker, warn_log);
  return ret;
}

int ObRootService::check_no_logging(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "set _no_logging, becacuse archivelog and _no_logging are exclusive parameters";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigDDLNoLoggingChecker, warn_log);
  return ret;
}

int ObRootService::check_data_disk_write_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(item.value_.ptr(), is_valid);
  const char *warn_log = "cluster config data_disk_write_limit_percentage. "
    "It should greater than or equal with data_disk_usage_limit_percentage";
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (!is_valid) {
    // invalid argument
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(value));
  } else if (value == 0) {
    // does not need check data disk write limit percentage
  } else if (value < GCONF.data_disk_usage_limit_percentage) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, warn_log);
  }
  return ret;
}

int ObRootService::check_data_disk_usage_limit_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  bool is_valid = false;
  int64_t value = ObConfigIntParser::get(item.value_.ptr(), is_valid);
  const char *warn_log = "cluster config data_disk_usage_limit_percentage. "
    "It should less than or equal with data_disk_write_limit_percentage";
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (!is_valid) {
    // invalid argument
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(value));
  } else if (0 == GCONF.data_disk_write_limit_percentage) {
    // does not need check data disk write limit percentage
  } else if (value > GCONF.data_disk_write_limit_percentage) {
    ret = OB_INVALID_ARGUMENT;
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, warn_log);
  }
  return ret;
}

#undef CHECK_TENANTS_CONFIG_WITH_FUNC
#undef CHECK_CLUSTER_CONFIG_WITH_FUNC

int ObRootService::set_config_post_hook(const obrpc::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(ret), K(arg));
  }
  FOREACH_X(item, arg.items_, OB_SUCCESS == ret) {
    if (item->name_.is_empty()) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("empty config name", "item", *item, K(ret));
    } else if (0 == STRCMP(item->name_.ptr(), ENABLE_REBALANCE)
               || 0 == STRCMP(item->name_.ptr(), ENABLE_REREPLICATION)) {
      // TODO: @wanhong.wwh SUPPORT clear DR task after disable rebalance and rereplication
    } else if (0 == STRCMP(item->name_.ptr(), MERGER_CHECK_INTERVAL)) {
      //daily_merge_scheduler_.wakeup();
    } else if (0 == STRCMP(item->name_.ptr(), ENABLE_AUTO_LEADER_SWITCH)) {
      //wake_up leader_cooridnator
    } else if (0 == STRCMP(item->name_.ptr(), OBCONFIG_URL)) {
    } else if (0 == STRCMP(item->name_.ptr(), SCHEMA_HISTORY_RECYCLE_INTERVAL)) {
      schema_history_recycler_.wakeup();
      LOG_INFO("schema_history_recycle_interval parameters updated, wakeup schema_history_recycler",
               KPC(item));
    }
  }
  return ret;
}

//ensure execute on DDL thread
int ObRootService::force_create_sys_table(const obrpc::ObForceCreateSysTableArg &arg)
{
  return OB_NOT_SUPPORTED;
}

int ObRootService::clear_special_cluster_schema_status()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else {
    ObSchemaService *schema_service = schema_service_->get_schema_service();
    if (OB_ISNULL(schema_service)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema service is null", K(ret));
    } else {
      schema_service->set_cluster_schema_status(
          ObClusterSchemaStatus::NORMAL_STATUS);
    }
  }
  return ret;
}



// if tenant_id =  OB_INVALID_TENANT_ID, indicates refresh all tenants's schema;
// otherwise, refresh specify tenant's schema. ensure schema_version not fallback by outer layer logic.
int ObRootService::broadcast_schema(const obrpc::ObBroadcastSchemaArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receieve broadcast_schema request", K(arg));
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_ISNULL(schema_service_)
             || OB_ISNULL(schema_service_->get_schema_service())) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret), KP_(schema_service));
  } else {
    ObRefreshSchemaInfo schema_info;
    ObSchemaService *schema_service = schema_service_->get_schema_service();
    if (OB_INVALID_TENANT_ID != arg.tenant_id_) {
      // tenant_id is valid, just refresh specify tenant's schema.
      schema_info.set_tenant_id(arg.tenant_id_);
      schema_info.set_schema_version(arg.schema_version_);
    } else {
      // tenant_id =  OB_INVALID_TENANT_ID, indicates refresh all tenants's schema;
      if (OB_FAIL(schema_service->inc_sequence_id())) {
        LOG_WARN("increase sequence_id failed", K(ret));
      }
    }
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(schema_service->inc_sequence_id())) {
      LOG_WARN("increase sequence_id failed", K(ret));
    } else if (OB_FAIL(schema_service->set_refresh_schema_info(schema_info))) {
      LOG_WARN("fail to set refresh schema info", K(ret), K(schema_info));
    }
    // if switchover to primary tenant, we should clear ddl epoch in RS
    // if not clear ddl epoch in RS, we could loss some DDL changes under
    // previous primary_tenant in another cluster
    if (OB_FAIL(ret)) {
    } else if (arg.need_clear_ddl_epoch()) {
      // only switchover need clear ddl epoch by broadcast schema
      // tenant id should be valid under this case
      if (OB_UNLIKELY(!is_valid_tenant_id(arg.tenant_id_))) {
        ret = OB_INVALID_ARGUMENT;
        LOG_WARN("tenant id should be valid if need_clear_ddl_epoch", KR(ret), K(arg));
      } else {
        schema_service_->get_ddl_epoch_mgr().remove_ddl_epoch(arg.tenant_id_);
      }
    }
  }
  LOG_INFO("end broadcast_schema request", K(ret), K(arg));
  return ret;
}

/*
 * standby_cluster, will return local tenant's schema_version
 * primary_cluster, will return tenant's newest schema_version
 *   - schema_version = OB_CORE_SCHEMA_VERSION, indicate the tenant is garbage.
 *   - schema_version = OB_INVALID_VERSION, indicate that it is failed to get schame_version.
 *   - schema_version > OB_CORE_SCHEMA_VERSION, indicate that the schema_version is valid.
 */
int ObRootService::get_tenant_schema_versions(
    const obrpc::ObGetSchemaArg &arg,
    obrpc::ObTenantSchemaVersions &tenant_schema_versions)
{
  int ret = OB_SUCCESS;
  tenant_schema_versions.reset();
  ObSchemaGetterGuard schema_guard;
  ObArray<uint64_t> tenant_ids;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(
                     OB_SYS_TENANT_ID, schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else if (OB_FAIL(schema_guard.get_tenant_ids(tenant_ids))) {
    LOG_WARN("fail to get tenant ids", KR(ret));
  } else {
    int64_t tenant_id = OB_INVALID_TENANT_ID;
    int64_t schema_version = 0;
    for (int64_t i = 0; i < tenant_ids.count() && OB_SUCC(ret); i++) {
      ObSchemaGetterGuard tenant_schema_guard;
      tenant_id = tenant_ids.at(i);
      schema_version = 0;
      if (OB_SYS_TENANT_ID == tenant_id
          || STANDBY_CLUSTER == ObClusterInfoGetter::get_cluster_role_v2()) {
        // For the follower, since schema_status is not advanced by the DDL thread and can accept eventual consistency,
        // Thus, only the local schema version needs to be retrieved
        if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(
                    tenant_id, schema_version))) {
          LOG_WARN("fail to get tenant refreshed schema version", K(ret), K(tenant_id));
        }
      } else {
        // for primary cluster, need to get newest schema_version from inner table.
        ObRefreshSchemaStatus schema_status;
        schema_status.tenant_id_ = tenant_id;
        int64_t version_in_inner_table = OB_INVALID_VERSION;
        bool is_restore = false;
        if (OB_FAIL(schema_service_->check_tenant_is_restore(&schema_guard, tenant_id, is_restore))) {
          LOG_WARN("fail to check tenant is restore", KR(ret), K(tenant_id));
        } else if (is_restore) {
          ObSchemaStatusProxy *schema_status_proxy = GCTX.schema_status_proxy_;
          if (OB_ISNULL(schema_status_proxy)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("schema_status_proxy is null", KR(ret));
          } else if (OB_FAIL(schema_status_proxy->get_refresh_schema_status(tenant_id, schema_status))) {
            LOG_WARN("failed to get tenant refresh schema status", KR(ret), K(tenant_id));
          } else if (OB_INVALID_VERSION != schema_status.readable_schema_version_) {
            ret = OB_EAGAIN;
            LOG_WARN("tenant's sys replicas are not restored yet, try later", KR(ret), K(tenant_id));
          }
        }
        if (FAILEDx(schema_service_->get_schema_version_in_inner_table(
                    sql_proxy_, schema_status, version_in_inner_table))) {
          // failed tenant creation, inner table is empty, return OB_CORE_SCHEMA_VERSION
          if (OB_EMPTY_RESULT == ret) {
            LOG_INFO("create tenant maybe failed", K(ret), K(tenant_id));
            schema_version = OB_CORE_SCHEMA_VERSION;
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("fail to get latest schema version in inner table", K(ret));
          }
        } else if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(
                           tenant_id, schema_version))) {
          LOG_WARN("fail to get tenant refreshed schema version", K(ret), K(tenant_id));
        } else if (schema_version < version_in_inner_table) {
          ObArray<uint64_t> tenant_ids;
          if (OB_FAIL(tenant_ids.push_back(tenant_id))) {
            LOG_WARN("fail to push back tenant_id", K(ret), K(tenant_id));
          } else if (OB_FAIL(schema_service_->refresh_and_add_schema(tenant_ids))) {
            LOG_WARN("fail to refresh schema", K(ret), K(tenant_id));
          } else if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(
                             tenant_id, schema_version))) {
            LOG_WARN("fail to get tenant refreshed schema version", K(ret), K(tenant_id));
          } else if (schema_version < version_in_inner_table) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("local version is still less than version in table",
                     K(ret), K(tenant_id), K(schema_version), K(version_in_inner_table));
          } else {}
        } else {}
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(tenant_schema_versions.add(tenant_id, schema_version))) {
        LOG_WARN("fail to add tenant schema version", KR(ret), K(tenant_id), K(schema_version));
      }
      if (OB_FAIL(ret) && arg.ignore_fail_ && OB_SYS_TENANT_ID != tenant_id) {
        int64_t invalid_schema_version = OB_INVALID_SCHEMA_VERSION;
        if (OB_FAIL(tenant_schema_versions.add(tenant_id, invalid_schema_version))) {
          LOG_WARN("fail to add tenant schema version", KR(ret), K(tenant_id), K(schema_version));
        }
      }
    } // end for
  }
  return ret;
}


int ObRootService::get_recycle_schema_versions(
    const obrpc::ObGetRecycleSchemaVersionsArg &arg,
    obrpc::ObGetRecycleSchemaVersionsResult &result)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive get recycle schema versions request", K(arg));
  bool in_service = is_full_service();
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("arg is invalid", K(ret), K(arg));
  } else if (!in_service) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("should be rs in service",
             KR(ret), K(in_service));
  } else if (OB_FAIL(schema_history_recycler_.get_recycle_schema_versions(arg, result))) {
    LOG_WARN("fail to get recycle schema versions", KR(ret), K(arg));
  }
  LOG_INFO("get recycle schema versions", KR(ret), K(arg), K(result));
  return ret;
}

int ObRootService::rebuild_index_in_restore(
    const obrpc::ObRebuildIndexInRestoreArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(arg);
  return ret;
}

int ObRootService::handle_archive_log(const obrpc::ObArchiveLogArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("handle_archive_log", K(arg));
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObBackupServiceProxy::handle_archive_log(arg))) {
    LOG_WARN("failed to handle archive log", K(ret));
  }
  return ret;
}

int ObRootService::handle_backup_database(const obrpc::ObBackupDatabaseArg &in_arg)
{
  int ret = OB_SUCCESS;
	if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObBackupServiceProxy::handle_backup_database(in_arg))) {
    LOG_WARN("failed to handle backup database", K(ret), K(in_arg));
  }
  FLOG_INFO("handle_backup_database", K(ret), K(in_arg));
  return ret;
}

int ObRootService::handle_validate_database(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_ERROR("not supported now", K(ret), K(arg));
  return ret;
}

int ObRootService::handle_validate_backupset(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_ERROR("not supported now", K(ret), K(arg));
  return ret;
}

int ObRootService::handle_cancel_validate(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_ERROR("not supported now", K(ret), K(arg));
  return ret;
}

int ObRootService::handle_backup_manage(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_SUCCESS;

  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    switch (arg.type_) {
    case ObBackupManageArg::CANCEL_BACKUP: {
      if (OB_FAIL(handle_backup_database_cancel(arg))) {
        LOG_WARN("failed to handle backup database cancel", K(ret), K(arg));
      }
      break;
    };
    case ObBackupManageArg::VALIDATE_DATABASE: {
      if (OB_FAIL(handle_validate_database(arg))) {
        LOG_WARN("failed to handle validate database", K(ret), K(arg));
      }
      break;
    };
    case ObBackupManageArg::VALIDATE_BACKUPSET: {
      if (OB_FAIL(handle_validate_backupset(arg))) {
        LOG_WARN("failed to handle validate backupset", K(ret), K(arg));
      }
      break;
    };
    case ObBackupManageArg::CANCEL_VALIDATE: {
      if (OB_FAIL(handle_cancel_validate(arg))) {
        LOG_WARN("failed to handle cancel validate", K(ret), K(arg));
      }
      break;
    };
    case ObBackupManageArg::CANCEL_BACKUP_BACKUPSET: {
      if (OB_FAIL(handle_cancel_backup_backup(arg))) {
        LOG_WARN("failed to handle cancel backup backup", K(ret), K(arg));
      }
      break;
    }
    case ObBackupManageArg::CANCEL_BACKUP_BACKUPPIECE: {
      if (OB_FAIL(handle_cancel_backup_backup(arg))) {
        LOG_WARN("failed to handle cancel backup backup", K(ret), K(arg));
      }
      break;
    }
    case ObBackupManageArg::CANCEL_ALL_BACKUP_FORCE: {
      if (OB_FAIL(handle_cancel_all_backup_force(arg))) {
        LOG_WARN("failed to handle cancel all backup force", K(ret), K(arg));
      }
      break;
    };
    default: {
      ret = OB_INVALID_ARGUMENT;
      LOG_ERROR("invalid backup manage arg", K(ret), K(arg));
      break;
    }
    }
  }

  FLOG_INFO("finish handle_backup_manage", K(ret), K(arg));
  return ret;
}

int ObRootService::handle_backup_delete(const obrpc::ObBackupCleanArg &arg)
{
  int ret = OB_SUCCESS;
	if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ObBackupServiceProxy::handle_backup_delete(arg))) {
    LOG_WARN("failed to handle backup delete", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::handle_delete_policy(const obrpc::ObDeletePolicyArg &arg)
{
  int ret = OB_SUCCESS;
	if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
	} else if (OB_FAIL(ObBackupServiceProxy::handle_delete_policy(arg))) {
    LOG_WARN("failed to handle delete policy", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::handle_backup_database_cancel(
    const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (ObBackupManageArg::CANCEL_BACKUP != arg.type_) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("handle backup database cancel get invalid argument", K(ret), K(arg));
  } else if (OB_FAIL(ObBackupServiceProxy::handle_backup_database_cancel(arg))) {
    LOG_WARN("failed to start schedule backup cancel", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::check_backup_scheduler_working(Bool &is_working)
{
  int ret = OB_NOT_SUPPORTED;
  is_working = true;

  FLOG_INFO("not support check backup scheduler working, should not use anymore", K(ret), K(is_working));
  return ret;
}

/////////////////////////
ObRootService::ObAlterLogExternalTableTask::ObAlterLogExternalTableTask(ObRootService &root_service)
  : ObAsyncTimerTask(root_service.task_queue_),
    root_service_(root_service)
{
  set_retry_times(INT64_MAX);
}

int ObRootService::ObAlterLogExternalTableTask::init(const uint64_t &data_version)
{
  int ret = OB_SUCCESS;
  pre_data_version_ = data_version;
  return ret;
}

ObAsyncTask *ObRootService::ObAlterLogExternalTableTask::deep_copy(char *buf,
    const int64_t buf_size) const
{
  ObAlterLogExternalTableTask *task = NULL;
  if (NULL == buf || buf_size < static_cast<int64_t>(sizeof(*this))) {
    LOG_WARN_RET(OB_BUF_NOT_ENOUGH, "buffer not large enough", K(buf_size), KP(buf));
  } else {
    task = new (buf) ObAlterLogExternalTableTask(root_service_);
    task->init(pre_data_version_);
  }
  return task;
}

int ObRootService::ObAlterLogExternalTableTask::process()
{
  int ret = OB_SUCCESS;
  LOG_INFO("table has been altered, no need to alter log external table again", KR(ret), K(pre_data_version_));
  return ret;
}

int ObRootService::ObAlterLogExternalTableTask::alter_log_external_table_()
{
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  const char *alter_table_sql = "alter external table sys_external_tbs.__all_external_alert_log_info auto_refresh immediate";
  if (OB_ISNULL(GCTX.sql_proxy_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_ERROR("sql_proxy_ expected not null", KR(ret), K(lbt()));
  } else if (OB_FAIL(GCTX.sql_proxy_->write(OB_SYS_TENANT_ID, alter_table_sql, affected_rows))) {
    LOG_WARN("fail to execute sql", KR(ret), K(alter_table_sql));
  } else if (0 != affected_rows) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("affected_rows expected to be zero", KR(ret), K(affected_rows), K(alter_table_sql));
  } else {
    LOG_INFO("seccess to alter auto_refresh flag", KR(ret), K(alter_table_sql));
  }
  return ret;
}
int ObRootService::handle_cancel_backup_backup(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_ERROR("not supported now", K(ret), K(arg));
  return ret;
}

int ObRootService::handle_cancel_all_backup_force(const obrpc::ObBackupManageArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  LOG_ERROR("not support now", K(ret), K(arg));
  return ret;
}

void ObRootService::reset_fail_count()
{
  ATOMIC_STORE(&fail_count_, 0);
}

void ObRootService::update_fail_count(int ret)
{
  int64_t count = ATOMIC_AAF(&fail_count_, 1);
  LOG_WARN("rs_monitor_check : fail to start root service", KR(ret), K(count));
  LOG_DBA_WARN(OB_ERR_ROOTSERVICE_START, "msg", "rootservice start()/do_restart() has failure",
               KR(ret), "fail_cnt", count);
}

int ObRootService::create_restore_point(const obrpc::ObCreateRestorePointArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(arg);
  LOG_WARN("craete restpre point is not supported now", K(ret));
  return ret;
}

int ObRootService::drop_restore_point(const obrpc::ObDropRestorePointArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  UNUSED(arg);
  LOG_WARN("drop restpre point is not supported now", K(ret));
  return ret;
}

int ObRootService::build_ddl_single_replica_response(const obrpc::ObDDLBuildSingleReplicaResponseArg &arg)
{
  int ret = OB_SUCCESS;
  ObDDLTaskInfo info;
  info.row_scanned_ = arg.row_scanned_;
  info.row_inserted_ = arg.row_inserted_;
  info.physical_row_count_ = arg.physical_row_count_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.tenant_id_, arg.task_id_, PROCESS_BUILD_SSTABLE_RESPONSE_SLOW))) {
    LOG_WARN("ddl sim failure: procesc build sstable response slow", K(ret));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::on_sstable_complement_job_reply(
          arg.tablet_id_/*source tablet id*/, arg.server_addr_,
          ObDDLTaskKey(arg.dest_tenant_id_, arg.dest_schema_id_, arg.dest_schema_version_),
          arg.snapshot_version_, arg.execution_id_, arg.ret_code_, info))) {
    LOG_WARN("handle column checksum calc response failed", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "build ddl single replica response",
                        "tenant_id", arg.tenant_id_,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "tablet_id", arg.tablet_id_,
                        "dag_result", arg.ret_code_,
                        arg.snapshot_version_);
  LOG_INFO("finish build ddl single replica response ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::purge_recyclebin_objects(int64_t purge_each_time)
{
  int ret = OB_SUCCESS;
  // always passed
  int64_t expire_timeval = GCONF.recyclebin_object_expire_time;
  ObSEArray<uint64_t, 16> tenant_ids;
  ObSchemaGetterGuard guard;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_serviece_ is null", KR(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, guard))) {
    LOG_WARN("fail to get sys schema guard", KR(ret));
  } else if (OB_FAIL(guard.get_tenant_ids(tenant_ids))) {
    LOG_WARN("get all tenants failed", KR(ret));
  } else {
    const int64_t current_time = ObTimeUtility::current_time();
    obrpc::Int64 expire_time = current_time - expire_timeval;
    const int64_t SLEEP_INTERVAL = 100 * 1000;  //100ms interval of send rpc
    const int64_t PURGE_EACH_RPC = 10;          //delete count per rpc
    obrpc::Int64 affected_rows = 0;
    obrpc::ObPurgeRecycleBinArg arg;
    int64_t purge_sum = purge_each_time;
    const bool is_standby = PRIMARY_CLUSTER != ObClusterInfoGetter::get_cluster_role_v2();
    const ObSimpleTenantSchema *simple_tenant = NULL;
    //ignore ret
    for (int i = 0; i < tenant_ids.count() && in_service() && purge_sum > 0; ++i) {
      int64_t purge_time = GCONF._recyclebin_object_purge_frequency;
      const uint64_t tenant_id = tenant_ids.at(i);
      if (purge_time <= 0) {
        break;
      }
      if (OB_SYS_TENANT_ID != tenant_id && is_standby) {
        // standby cluster won't purge recyclebin automacially.
        LOG_TRACE("user tenant won't purge recyclebin automacially in standby cluster", K(tenant_id));
        continue;
      } else if (OB_FAIL(guard.get_tenant_info(tenant_id, simple_tenant))) {
        LOG_WARN("fail to get simple tenant schema", KR(ret), K(tenant_id));
      } else if (OB_ISNULL(simple_tenant)) {
        ret = OB_TENANT_NOT_EXIST;
        LOG_WARN("simple tenant schema not exist", KR(ret), K(tenant_id));
      } else if (!simple_tenant->is_normal()) {
        // only deal with normal tenant.
        LOG_TRACE("tenant which isn't normal won't purge recyclebin automacially", K(tenant_id));
        continue;
      }
      // ignore error code of different tenant
      ret = OB_SUCCESS;
      affected_rows = 0;
      arg.tenant_id_ = tenant_id;
      arg.expire_time_ = expire_time;
      arg.auto_purge_ = true;
      arg.exec_tenant_id_ = tenant_id;
      LOG_INFO("start purge recycle objects of tenant", K(arg), K(purge_sum));
      while (OB_SUCC(ret) && in_service() && purge_sum > 0) {
        int64_t cal_timeout = 0;
        int64_t start_time = ObTimeUtility::current_time();
        arg.purge_num_ = purge_sum > PURGE_EACH_RPC ? PURGE_EACH_RPC : purge_sum;
        if (OB_FAIL(schema_service_->cal_purge_need_timeout(arg, cal_timeout))) {
          LOG_WARN("fail to cal purge need timeout", KR(ret), K(arg));
        } else if (0 == cal_timeout) {
          LOG_INFO("cal purge need timeout is zero, just exit", K(tenant_id), K(purge_sum));
          break;
        } else if (OB_FAIL(common_proxy_.timeout(cal_timeout).purge_expire_recycle_objects(arg, affected_rows))) {
          LOG_WARN("purge reyclebin objects failed", KR(ret),
              K(current_time), K(expire_time), K(affected_rows), K(arg));
        } else {
          purge_sum -= affected_rows;
          if (arg.purge_num_ != affected_rows) {
            int64_t cost_time = ObTimeUtility::current_time() - start_time;
            LOG_INFO("purge recycle objects", KR(ret), K(tenant_id), K(cost_time), K(purge_sum),
                                              K(cal_timeout), K(expire_time), K(current_time), K(affected_rows));
            if (OB_SUCC(ret) && in_service()) {
              ob_usleep(SLEEP_INTERVAL);
            }
            break;
          }
        }
        int64_t cost_time = ObTimeUtility::current_time() - start_time;
        LOG_INFO("purge recycle objects", KR(ret), K(tenant_id), K(cost_time), K(purge_sum),
                                          K(cal_timeout), K(expire_time), K(current_time), K(affected_rows));
        if (OB_SUCC(ret) && in_service()) {
          ob_usleep(SLEEP_INTERVAL);
        }
      }
    }
  }
  return ret;
}

int ObRootService::flush_opt_stat_monitoring_info(const obrpc::ObFlushOptStatArg &arg)
{
  int ret = OB_SUCCESS;
  ObZone empty_zone;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(rpc_proxy_.to(GCTX.self_addr()).flush_local_opt_stat_monitoring_info(arg))) {
      LOG_WARN("fail to update table statistic", K(ret));
    } else { /*do nothing*/}
  }
  return ret;
}

int ObRootService::admin_set_backup_config(const obrpc::ObAdminSetConfigArg &arg)
{
  int ret = OB_SUCCESS;  
  if (!arg.is_valid()) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("invalid backup config arg", K(ret));
  } else if (!arg.is_backup_config_) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("admin set config type not backup config", K(ret), K(arg));
  }
  share::BackupConfigItemPair config_item;
  share::ObBackupConfigParserMgr config_parser_mgr;
  ARRAY_FOREACH_X(arg.items_, i , cnt, OB_SUCC(ret)) {
    const ObAdminSetConfigItem &item = arg.items_.at(i);
    uint64_t exec_tenant_id = OB_INVALID_TENANT_ID;
    ObMySQLTransaction trans;
    config_parser_mgr.reset();
    if ((common::is_sys_tenant(item.exec_tenant_id_) && item.tenant_name_.is_empty())
        || (common::is_user_tenant(item.exec_tenant_id_) && !item.tenant_name_.is_empty())
        || common::is_meta_tenant(item.exec_tenant_id_)) {
      ret = OB_NOT_SUPPORTED;
      LOG_WARN("backup config only support user tenant", K(ret));
    } else if (!item.tenant_name_.is_empty()) {
      schema::ObSchemaGetterGuard guard;
      if (OB_ISNULL(schema_service_)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("schema service must not be null", K(ret));
      } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(OB_SYS_TENANT_ID, guard))) {
        LOG_WARN("fail to get tenant schema guard", K(ret));
      } else if (OB_FAIL(guard.get_tenant_id(ObString(item.tenant_name_.ptr()), exec_tenant_id))) {
        LOG_WARN("fail to get tenant id", K(ret));
      }
    } else {
      exec_tenant_id = item.exec_tenant_id_;
    }

    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(trans.start(&sql_proxy_, gen_meta_tenant_id(exec_tenant_id)))) {
      LOG_WARN("fail to start trans", K(ret));
    } else {
      common::ObSqlString name;
      common::ObSqlString value;
      if (OB_FAIL(name.assign(item.name_.ptr()))) {
        LOG_WARN("fail to assign name", K(ret));
      } else if (OB_FAIL(value.assign(item.value_.ptr()))) {
        LOG_WARN("fail to assign value", K(ret));
      } else if (OB_FAIL(config_parser_mgr.init(name, value, exec_tenant_id))) {
        LOG_WARN("fail to init backup config parser mgr", K(ret), K(item));
      } else if (OB_FAIL(config_parser_mgr.update_inner_config_table(rpc_proxy_, trans))) {
        LOG_WARN("fail to update inner config table", K(ret));
      }

      if (OB_SUCC(ret)) {
        if (OB_FAIL(trans.end(true))) {
          LOG_WARN("fail to commit trans", K(ret));
        }
      } else {
        int tmp_ret = OB_SUCCESS;
        if (OB_SUCCESS != (tmp_ret = trans.end(false))) {
          LOG_WARN("fail to rollback trans", K(tmp_ret));
        }
      }
    }
  }
  return ret;
}

int ObRootService::cancel_ddl_task(const ObCancelDDLTaskArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_INFO("receive cancel ddl task", K(arg));
  if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_FAIL(SYS_TASK_STATUS_MGR.cancel_task(arg.get_task_id()))) {
    LOG_WARN("cancel task failed", K(ret));
  } else {
    LOG_INFO("succeed to cancel ddl task", K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "cancel ddl task",
                        "tenant_id", MTL_ID(),
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.get_task_id());
  LOG_INFO("finish cancel ddl task ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::set_config_after_bootstrap_()
{
  // configs will be sent to other servers when set in rs, so there is no need to wait config set
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString sql;

  const char* configs[][2] = {
    {"_use_odps_jni_connector", "true"},
    {"enable_record_trace_log", "false"},
    {"_enable_dbms_job_package", "false"},
    {"_bloom_filter_ratio", "3"},
    {"_enable_mysql_compatible_dates", "true"},
    {"_ob_enable_pl_dynamic_stack_check", "true"}
  };
  if (OB_FAIL(sql.assign("ALTER SYSTEM SET"))) {
    LOG_WARN("failed to assign sql string", KR(ret));
  } else {
    for (int64_t i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(configs); i++) {
      if (OB_FAIL(sql.append_fmt("%c %s = %s", (i == 0 ? ' ' : ','), configs[i][0], configs[i][1]))) {
        LOG_WARN("failed to append_fmt", KR(ret), K(sql), K(configs[i][0]), K(configs[i][1]));
      }
    }
    if (FAILEDx(sql_proxy_.write(sql.ptr(), affected_rows))) {
      LOG_WARN("failed to set configs", KR(ret), K(sql));
    } else {
      for (int64_t i = 0; OB_SUCC(ret) && i < ARRAYSIZEOF(configs); i++) {
        if (OB_FAIL(check_config_result(configs[i][0], configs[i][1]))) {
          LOG_WARN("failed to check_config_result", KR(ret), K(configs[i][0]), K(configs[i][1]));
        }
      }
    }
  }
  return ret;
}

int ObRootService::handle_recover_table(const obrpc::ObRecoverTableArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRootService::recompile_all_views_batch(const obrpc::ObRecompileAllViewsBatchArg &arg)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.recompile_all_views_batch(arg.tenant_id_, arg.view_ids_))) {
    LOG_WARN("failed to recompile all views", K(ret), K(arg.tenant_id_));
  }
  LOG_INFO("recompile all views batch finish", KR(ret), K(start_time),
      "cost_time", ObTimeUtility::current_time() - start_time);
  return ret;
}

int ObRootService::check_transfer_task_tablet_count_threshold_(obrpc::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  bool valid = true;
  for (int i = 0; i < item.tenant_ids_.count() && valid; i++) {
    const uint64_t tenant_id = item.tenant_ids_.at(i);
    int64_t value = ObConfigIntParser::get(item.value_.ptr(), valid);
    if (valid && (value > OB_MAX_TRANSFER_BINDING_TABLET_CNT)) {
      valid = false;
      char err_msg[DEFAULT_BUF_LENGTH];
      (void)snprintf(err_msg, sizeof(err_msg), "_transfer_task_tablet_count_threshold of tenant %ld, "
          "it cannot be greater than %ld", tenant_id, OB_MAX_TRANSFER_BINDING_TABLET_CNT);
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, err_msg);
    }
    if (!valid) {
      ret = OB_INVALID_ARGUMENT;
      LOG_WARN("config invalid", KR(ret), K(value), K(item), K(tenant_id));
    }
  }
  return ret;
}

int ObRootService::start_ddl_service_()
{
  // TODO@jingyu.cr: this step should move to observer's precedure after RS is removed
  int ret = OB_SUCCESS;
  if (!GCTX.is_standby_cluster()) {
    // 1. primary cluster
    if (ObDDLServiceLauncher::is_ddl_service_started()) {
      // good, ObDDLServiceLauncher already started
      FLOG_INFO("ddl service is already started", KR(ret));
    } else {
      // ObDDLServiceLauncher should be started when sys log stream's leader take over
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("primary cluster should with ObDDLServiceLauncher enabled now", KR(ret));
    }
  } else {
    // 2. standby cluster
    if (ObDDLServiceLauncher::is_ddl_service_started()) {
      // STANDBY_ROLE can not trigger ObDDLServiceLauncher's switch_to_leader automatically
      ret = OB_STATE_NOT_MATCH;
      LOG_WARN("standby cluster should with ObDDLServiceLauncher disabled at begining", KR(ret));
    } else {
      MTL_SWITCH(OB_SYS_TENANT_ID) {
        rootserver::ObDDLServiceLauncher* ddl_service_launcher = MTL(rootserver::ObDDLServiceLauncher*);
        if (OB_ISNULL(ddl_service_launcher)) {
          ret = OB_ERR_UNEXPECTED;
          FLOG_WARN("ddl service is null", KR(ret), KP(ddl_service_launcher));
        } else if (OB_FAIL(ddl_service_launcher->switch_to_leader())) {
          FLOG_WARN("fail to start ddl service", KR(ret));
        } else {
          FLOG_INFO("success to start ddl service", KR(ret));
        }
      }
    }
  }
  return ret;
}

int ObRootService::create_ccl_rule_ddl(const obrpc::ObCreateCCLRuleArg &arg)
{
  int ret = OB_SUCCESS;
  ObCclDDLService ccl_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ccl_ddl_service.create_ccl_ddl(arg))) {
    LOG_WARN("handle ddl failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::drop_ccl_rule_ddl(const obrpc::ObDropCCLRuleArg &arg)
{
  int ret = OB_SUCCESS;
  ObCclDDLService ccl_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ccl_ddl_service.drop_ccl_ddl(arg))) {
    LOG_WARN("handle ddl failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::create_ai_model(const obrpc::ObCreateAiModelArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("receive create ai model arg", K(arg));
  ObAiModelDDLService ai_model_ddl_service(ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(arg.check_valid())) {
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ai_model_ddl_service.create_ai_model(arg))) {
    LOG_WARN("failed to create ai model", K(ret), K(arg));
  }

  LOG_TRACE("finish create ai model", K(ret), K(arg));

  return ret;
}

int ObRootService::drop_ai_model(const obrpc::ObDropAiModelArg &arg)
{
  int ret = OB_SUCCESS;
  LOG_TRACE("receive drop ai model arg", K(arg));
  ObAiModelDDLService ai_model_ddl_service(ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ai_model_ddl_service.drop_ai_model(arg))) {
    LOG_WARN("failed to drop ai model", K(ret), K(arg));
  }

  LOG_TRACE("finish drop ai model", K(ret), K(arg));

  return ret;
}



int ObRootService::create_location(const obrpc::ObCreateLocationArg &arg)
{
  int ret = OB_SUCCESS;
  ObLocationDDLService location_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(location_ddl_service.create_location(arg, &arg.ddl_stmt_str_))) {
    LOG_WARN("handle ddl failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::drop_location(const obrpc::ObDropLocationArg &arg)
{
  int ret = OB_SUCCESS;
  ObLocationDDLService location_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(location_ddl_service.drop_location(arg, &arg.ddl_stmt_str_))) {
    LOG_WARN("drop location failed", K(arg.location_name_), K(ret));
  }
  return ret;
}

int ObRootService::revoke_object(const ObRevokeObjMysqlArg &arg)
{
  int ret = OB_SUCCESS;
  ObObjPrivMysqlDDLService objpriv_mysql_ddl_service(&ddl_service_);
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ObObjMysqlPrivSortKey object_key(arg.tenant_id_, arg.user_id_, arg.obj_name_, arg.obj_type_);
    OZ (objpriv_mysql_ddl_service.revoke_object(object_key, arg.priv_set_, arg.grantor_, arg.grantor_host_));
  }
  return ret;
}


} // end namespace rootserver
} // end namespace oceanbase
