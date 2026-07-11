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

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_root_service.h"
#include "observer/ob_server.h"



#include "share/ob_global_stat_proxy.h"
#include "sql/resolver/ddl/ob_index_builder_util.h"
#include "observer/ob_server_event_history_table_operator.h"
#include "storage/deadlock/ob_deadlock_inner_table_service.h"
#include "observer/scheduler/ob_partition_auto_split_helper.h"

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
#include "share/ob_tenant_timezone_mgr.h"

#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_ddl_common.h" // for ObDDLUtil
#include "share/ob_cluster_event_history_table_operator.h"//CLUSTER_EVENT_INSTANCE
#include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "observer/ob_sys_tenant_load_sys_package_task.h"
#include "observer/ob_service.h"

#include "parallel_ddl/ob_create_table_helper.h" // ObCreateTableHelper
#include "parallel_ddl/ob_create_table_like_helper.h" // ObCreateTableLikeHelper
#include "rootserver/parallel_ddl/ob_create_view_helper.h"  // ObCreateViewHelper
#include "rootserver/parallel_ddl/ob_create_materialized_view_helper.h"  // ObCreateMaterializedViewHelper
#include "parallel_ddl/ob_set_comment_helper.h" //ObCommentHelper
#include "parallel_ddl/ob_create_index_helper.h" // ObCreateIndexHelper
#include "parallel_ddl/ob_update_index_status_helper.h" // ObUpdateIndexStatusHelper
#include "pl_ddl/ob_pl_ddl_service.h"
#include "parallel_ddl/ob_drop_table_helper.h" // ObDropTableHelper
#include "share/table/ob_ttl_util.h"
#include "rootserver/ob_ai_model_ddl_service.h"
#include "lib/utility/ob_print_utils.h"     // databuff_printf
#include "share/ob_thread_mgr.h"
#include "share/ob_ex_rpc.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"

namespace oceanbase
{

using namespace common;
using namespace obcall;
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
    sql_proxy_(),
    schema_service_(NULL),
    root_minor_freeze_(),
    ddl_service_(),
    bootstrap_lock_(),
    restart_task_tg_id_(-1),
    load_ddl_task_tg_id_(-1),
    event_table_clear_task_tg_id_(-1),
    purge_recyclebin_task_tg_id_(-1),
    restart_task_(*this),
    load_ddl_task_(*this),
    event_table_clear_task_(ROOTSERVICE_EVENT_INSTANCE,
                            SERVER_EVENT_INSTANCE,
                            DEALOCK_EVENT_INSTANCE),
    purge_recyclebin_task_(*this),
    snapshot_manager_(),
    core_meta_table_version_(0),
    baseline_schema_version_(0),
    start_service_time_(0),
    rs_status_(),
    fail_count_(0),
    schema_history_recycler_(),
    max_id_cache_mgr_()
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
                        ObAddr &self,
                        ObMySQLProxy &sql_proxy,
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

    self_addr_ = self;

    sql_proxy_.assign(sql_proxy);
    sql_proxy_.set_inactive();

    schema_service_ = schema_service;
  }

  if (FAILEDx(TG_CREATE(lib::TGDefIDs::RootServiceTaskTimer, restart_task_tg_id_))) {
    FLOG_WARN("create rs restart task timer tg failed", KR(ret));
  } else if (OB_FAIL(TG_CREATE(lib::TGDefIDs::RootServiceTaskTimer, load_ddl_task_tg_id_))) {
    FLOG_WARN("create rs load ddl task timer tg failed", KR(ret));
  } else if (OB_FAIL(TG_CREATE(lib::TGDefIDs::RootServiceTaskTimer, event_table_clear_task_tg_id_))) {
    FLOG_WARN("create rs event table clear task timer tg failed", KR(ret));
  } else if (OB_FAIL(TG_CREATE(lib::TGDefIDs::RootServiceTaskTimer, purge_recyclebin_task_tg_id_))) {
    FLOG_WARN("create rs purge recyclebin task timer tg failed", KR(ret));
  } else if (OB_FAIL(root_minor_freeze_.init())) {
    // init root minor freeze
    FLOG_WARN("init root_minor_freeze_ failed", KR(ret));
  } else if (OB_FAIL(ddl_service_.init(*GCTX.sql_proxy_, *GCTX.schema_service_,
                                       snapshot_manager_, tenant_ddl_service_))) {
    // init ddl service
    FLOG_WARN("init ddl_service_ failed", KR(ret));
  } else if (OB_FAIL(tenant_ddl_service_.init(ddl_service_,
          sql_proxy_, *schema_service))) {
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

  if (restart_task_tg_id_ != -1) {
    TG_DESTROY(restart_task_tg_id_);
    restart_task_tg_id_ = -1;
  }
  if (load_ddl_task_tg_id_ != -1) {
    TG_DESTROY(load_ddl_task_tg_id_);
    load_ddl_task_tg_id_ = -1;
  }
  if (event_table_clear_task_tg_id_ != -1) {
    TG_DESTROY(event_table_clear_task_tg_id_);
    event_table_clear_task_tg_id_ = -1;
  }
  if (purge_recyclebin_task_tg_id_ != -1) {
    TG_DESTROY(purge_recyclebin_task_tg_id_);
    purge_recyclebin_task_tg_id_ = -1;
  }
  FLOG_INFO("task timer tg destroy");

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
    tenant_ddl_service_.restart();
    if (OB_FAIL(TG_START(restart_task_tg_id_))) {
      FLOG_WARN("restart task timer tg start failed", KR(ret), K_(restart_task_tg_id));
    } else if (OB_FAIL(TG_START(load_ddl_task_tg_id_))) {
      FLOG_WARN("load ddl task timer tg start failed", KR(ret), K_(load_ddl_task_tg_id));
    } else if (OB_FAIL(TG_START(event_table_clear_task_tg_id_))) {
      FLOG_WARN("event table clear task timer tg start failed", KR(ret), K_(event_table_clear_task_tg_id));
    } else if (OB_FAIL(TG_START(purge_recyclebin_task_tg_id_))) {
      FLOG_WARN("purge recyclebin task timer tg start failed", KR(ret), K_(purge_recyclebin_task_tg_id));
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
      TG_STOP(restart_task_tg_id_);
      TG_STOP(load_ddl_task_tg_id_);
      TG_STOP(event_table_clear_task_tg_id_);
      TG_STOP(purge_recyclebin_task_tg_id_);
      FLOG_INFO("task timer tg stop");
      schema_history_recycler_.stop();
      FLOG_INFO("schema_history_recycler stop");
      dbms_job::ObDBMSJobMaster::get_instance().stop();
      FLOG_INFO("dbms job master stop");
      max_id_cache_mgr_.reset();
      FLOG_INFO("max id cache mgr reset");
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
  if (restart_task_tg_id_ != -1) { TG_WAIT(restart_task_tg_id_); }
  if (load_ddl_task_tg_id_ != -1) { TG_WAIT(load_ddl_task_tg_id_); }
  if (event_table_clear_task_tg_id_ != -1) { TG_WAIT(event_table_clear_task_tg_id_); }
  if (purge_recyclebin_task_tg_id_ != -1) { TG_WAIT(purge_recyclebin_task_tg_id_); }
  FLOG_INFO("task timer tg exit success");
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

  if (OB_FAIL(TG_SCHEDULE(purge_recyclebin_task_tg_id_,
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
  const bool did_repeat = true;
  bool task_exist = false;
#ifdef ERRSIM
  const int64_t delay = 1000L * 1000L; //1s
#else
  const int64_t delay = 5L * 1000L * 1000L; //5s
#endif
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(TG_TASK_EXIST(load_ddl_task_tg_id_, load_ddl_task_, task_exist))) {
    LOG_WARN("failed to check task exist", KR(ret));
  } else if (task_exist) {
    // ignore error
    LOG_WARN("load ddl task already exist", K(ret));
  } else if (OB_FAIL(TG_SCHEDULE(load_ddl_task_tg_id_, load_ddl_task_, delay, did_repeat))) {
    LOG_WARN("fail to add timer task", K(ret));
  } else {
    LOG_INFO("succeed to add load ddl task");
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
    const bool did_repeat = true;
    const bool immediate = delay <= 0;
    const int64_t schedule_delay = delay <= 0 ? config_->rootservice_ready_check_interval : delay;
    if (OB_FAIL(TG_SCHEDULE(restart_task_tg_id_, restart_task_,
                                           schedule_delay, did_repeat, immediate))) {
      LOG_WARN("schedule restart task failed", K(ret), K(delay), K(did_repeat));
    } else {
      LOG_INFO("submit restart task success", K(delay), K(schedule_delay), K(immediate));
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
  ObGlobalStatProxy global_proxy(sql_proxy_);
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
    ObBootstrap bootstrap(ddl_service_, tenant_ddl_service_,
        *config_);
    if (OB_FAIL(bootstrap.execute_bootstrap())) {
      LOG_ERROR("failed to execute_bootstrap", K(ret));
    }

    BOOTSTRAP_LOG(INFO, "start to do_restart");
    ObGlobalStatProxy global_proxy(sql_proxy_);
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
                               "__all_virtual_parameter_stat", name, value))) {
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
  } else if (OB_FAIL(trans.start(&sql_proxy_))) {
    LOG_WARN("trans start failed", K(ret));
  } else if (OB_FAIL(ddl_service_.get_schema_service().
                     get_tenant_refreshed_schema_version(baseline_schema_version))) {
    LOG_WARN("fail to get refreshed schema version", K(ret));
  } else {
    ObGlobalStatProxy proxy(trans);
    if (OB_FAIL(proxy.set_baseline_schema_version(baseline_schema_version))) {
      LOG_WARN("set_baseline_schema_version failed", K(baseline_schema_version), K(ret));
    }
  }
  int temp_ret = OB_SUCCESS;
  if (!trans.is_started()) {
  } else if (OB_SUCCESS != (temp_ret = trans.end(OB_SUCCESS == ret))) {
    LOG_ERROR("trans end failed", "commit", OB_SUCCESS == ret, K(temp_ret));
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
      if (OB_FAIL(multi_schema_service.gen_new_schema_version(new_schema_version))) {
        LOG_WARN("fail to gen new schema_version", K(ret), K(new_schema_version));
      } else if (OB_FAIL(ddl_sql_service.log_nop_operation(schema_operation,
                                                           new_schema_version,
                                                           schema_operation.ddl_stmt_str_,
                                                           sql_proxy))) {
        LOG_WARN("log finish bootstrap operation failed", K(ret), K(schema_operation));
      } else if (OB_FAIL(ddl_service_.refresh_schema())) {
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

int ObRootService::modify_system_variable(const obcall::ObModifySysVarArg &arg)
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

int ObRootService::parallel_ddl_pre_check_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!schema_service_->is_tenant_refreshed()) {
    // use this err to trigger DDL retry and release current thread.
    ret = OB_ERR_PARALLEL_DDL_CONFLICT;
    LOG_WARN("tenant' schema not refreshed yet, need retry", KR(ret));
  }
  return ret;
}

int ObRootService::parallel_create_table(const ObCreateTableArg &arg, ObCreateTableRes &res)
{
  LOG_TRACE("receive create table arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  bool is_parallel = arg.is_parallel_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_())) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret));
  } else if (arg.schema_.is_view_table()) {
    if (arg.schema_.is_materialized_view()) {
      ObCreateMaterializedViewHelper create_mv_helper(schema_service_, arg, res, nullptr /*external trans*/,is_parallel);
      if (OB_FAIL(create_mv_helper.init(ddl_service_))) {
        LOG_WARN("fail to init create materialized view helper", KR(ret));
      } else if (OB_FAIL(create_mv_helper.execute())) {
        LOG_WARN("fail to execute create materialized view", KR(ret));
      }
    } else {
      ObCreateViewHelper create_view_helper(schema_service_, arg, res, nullptr /*external trans*/,is_parallel);
      if (OB_FAIL(create_view_helper.init(ddl_service_))) {
        LOG_WARN("fail to init create view helper", KR(ret));
      } else if (OB_FAIL(create_view_helper.execute())) {
        LOG_WARN("fail to execute create view", KR(ret));
      }
    }
  } else {
    ObCreateTableHelper create_table_helper(schema_service_, arg, res);
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(create_table_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create table helper", KR(ret));
    } else if (OB_FAIL(create_table_helper.execute())) {
      LOG_WARN("fail to execute create table", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  LOG_TRACE("finish create table", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel create table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  return ret;
}

int ObRootService::create_table(const ObCreateTableArg &arg, ObCreateTableRes &res)
{
  LOG_TRACE("receive create table arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_())) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret));
  } else if (arg.schema_.is_view_table()) {
    ObCreateViewHelper create_view_helper(schema_service_, arg, res, nullptr/*external trans*/, false /*is_parallel*/);
    if (OB_FAIL(create_view_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create view helper", KR(ret));
    } else if (OB_FAIL(create_view_helper.execute())) {
      LOG_WARN("fail to execute create view", KR(ret));
    }
  } else {
    ObCreateTableHelper create_table_helper(schema_service_, arg, res, nullptr/*external trans*/, false /*is_parallel*/);
    if (OB_FAIL(create_table_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create table helper", KR(ret));
    } else if (OB_FAIL(create_table_helper.execute())) {
      LOG_WARN("fail to execute create table", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  LOG_TRACE("finish create table", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "create table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
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
  const uint64_t table_id = table_schema.get_table_id();
  const ObPartitionLevel part_level = table_schema.get_part_level();
  const ObPartitionFuncType part_func_type = table_schema.get_part_option().get_part_func_type();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_INVALID_ID == table_id || !is_inner_table(table_id)) {
    // skip
  } else if (table_schema.is_view_table()) {
    // no need specify tenant_id while specify table_id creating sys table
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
    table_schema.set_table_id(table_id);
    table_schema.set_tablegroup_id(OB_SYS_TABLEGROUP_ID);
    table_schema.set_tablegroup_name(OB_SYS_TABLEGROUP_NAME);
    table_schema.set_database_id(OB_SYS_DATABASE_ID);
  }
  return ret;
}

int ObRootService::fork_database(const obcall::ObForkDatabaseArg &arg, obcall::ObDDLRes &res)
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "databases", database_names_buffer);
  LOG_INFO("finish fork database ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::maintain_obj_dependency_info(const obcall::ObDependencyObjDDLArg &arg)
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

int ObRootService::mview_complete_refresh(const obcall::ObMViewCompleteRefreshArg &arg,
                                          obcall::ObMViewCompleteRefreshRes &res)
{
  LOG_DEBUG("receive mview complete refresh arg", K(arg));
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else {
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", KR(ret));
    } else if (OB_FAIL(check_parallel_ddl_conflict(schema_guard, arg))) {
      LOG_WARN("check parallel ddl conflict failed", KR(ret), K(arg));
    } else if (OB_FAIL(ddl_service_.mview_complete_refresh(arg, res, schema_guard))) {
      LOG_WARN("failed to mview complete refresh", KR(ret), K(arg));
    }
  }
  return ret;
}

int ObRootService::execute_ddl_task(const obcall::ObAlterTableArg &arg,
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
            const_cast<obcall::ObAlterTableArg &>(arg), obj_ids))) {
          LOG_WARN("failed to rebuild hidden table index in trans", K(ret));
        }
        break;
      }
      case share::REBUILD_CONSTRAINT_TASK: {
        if (OB_FAIL(ddl_service_.rebuild_hidden_table_constraints_in_trans(
            const_cast<obcall::ObAlterTableArg &>(arg), obj_ids))) {
          LOG_WARN("failed to rebuild hidden table constraints in trans", K(ret));
        }
        break;
      }
      case share::REBUILD_FOREIGN_KEY_TASK: {
        if (OB_FAIL(ddl_service_.rebuild_hidden_table_foreign_key_in_trans(
            const_cast<obcall::ObAlterTableArg &>(arg), obj_ids))) {
          LOG_WARN("failed to rebuild hidden table foreign key in trans", K(ret));
        }
        break;
      }
      case share::MAKE_DDL_TAKE_EFFECT_TASK: {
        if (arg.is_direct_load_partition_) {
          if (OB_FAIL(ddl_service_.swap_orig_and_hidden_table_partitions(
              const_cast<obcall::ObAlterTableArg &>(arg)))) {
            LOG_WARN("failed to swap orig and hidden table partitions", K(ret));
          }
        } else if (OB_FAIL(ddl_service_.swap_orig_and_hidden_table_state(
            const_cast<obcall::ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to swap orig and hidden table state", K(ret));
        }
        break;
      }
      case share::CLEANUP_GARBAGE_TASK:
      case share::PARTITION_SPLIT_RECOVERY_CLEANUP_GARBAGE_TASK: {
        if (OB_FAIL(ddl_service_.cleanup_garbage(
            const_cast<obcall::ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to cleanup garbage", K(ret));
        }
        break;
      }
      case share::MODIFY_FOREIGN_KEY_STATE_TASK: {
        if (OB_FAIL(ddl_service_.modify_hidden_table_fk_state(
            const_cast<obcall::ObAlterTableArg &>(arg)))) {
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
            const_cast<obcall::ObAlterTableArg &>(arg)))) {
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

int ObRootService::create_table_like(const ObCreateTableLikeArg &arg)
{
  int ret = OB_SUCCESS;
  obcall::ObCreateTableRes res;
  if (OB_FAIL(parallel_create_table_like(arg,res))) {
    LOG_WARN("fail to parallel create table like", KR(ret));
  }
  return ret;
}

int ObRootService::parallel_create_table_like(const obcall::ObCreateTableLikeArg &arg, obcall::ObCreateTableRes &res)
{
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else {
    ObCreateTableLikeHelper create_table_like_helper(schema_service_, arg, res,
                                                     false /*enable ddl parallel*/, nullptr);
    if (OB_FAIL(create_table_like_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create view helper", KR(ret));
    } else if (OB_FAIL(create_table_like_helper.execute())) {
      LOG_WARN("fail to execute create view", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  const char* ddl_type = (true == arg.is_parallel_) ? "parallel create table like" : "create table like";
  LOG_TRACE("finish create table like", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", ddl_type,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  return ret;
}

int ObRootService::precheck_interval_part(const obcall::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  const ObAlterTableArg::AlterPartitionType op_type = arg.alter_part_type_;
  const ObSimpleTableSchemaV2 *simple_table_schema = NULL;
  const AlterTableSchema &alter_table_schema = arg.alter_table_schema_;

  if (!alter_table_schema.is_interval_part()
      || obcall::ObAlterTableArg::ADD_PARTITION != op_type) {
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, schema service must not be NULL", K(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(schema_guard))) {
    LOG_WARN("fail to get schema guard", K(ret));
  } else if (OB_FAIL(schema_guard.get_simple_table_schema(
             alter_table_schema.get_table_id(), simple_table_schema))) {
    LOG_WARN("get table schema failed", KR(ret), K(alter_table_schema));
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

int ObRootService::create_hidden_table(const obcall::ObCreateHiddenTableArg &arg,
                                       obcall::ObCreateHiddenTableRes &res)
{
  LOG_DEBUG("receive create hidden table arg", K(arg));
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, CREATE_HIDDEN_TABLE_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, CREATE_HIDDEN_TABLE_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.create_hidden_table(arg, res))) {
    LOG_WARN("do create hidden table in trans failed", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "create hidden table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.get_table_id(),
                        "schema_version", res.schema_version_);
  LOG_INFO("finish create hidden table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::update_ddl_task_active_time(const obcall::ObUpdateDDLTaskActiveTimeArg &arg)
{
  LOG_DEBUG("receive recv ddl task status arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::update_ddl_task_active_time(ObDDLTaskID(task_id)))) {
    LOG_WARN("fail to set RegTaskTime map", K(ret), K(task_id));
  }
  return ret;
}

int ObRootService::abort_redef_table(const obcall::ObAbortRedefTableArg &arg)
{
  LOG_DEBUG("receive abort redef table arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, ABORT_REDEF_TABLE_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, ABORT_REDEF_TABLE_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::abort_redef_table(ObDDLTaskID(task_id)))) {
    LOG_WARN("cancel task failed", K(ret), K(task_id));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "abort redef table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_);
  LOG_INFO("finish abort redef table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::finish_redef_table(const obcall::ObFinishRedefTableArg &arg)
{
  LOG_DEBUG("receive finish redef table arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, FINISH_REDEF_TABLE_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, FINISH_REDEF_TABLE_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::finish_redef_table(ObDDLTaskID(task_id)))) {
    LOG_WARN("failed to finish redef table", K(ret), K(task_id));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "finish redef table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_);
  LOG_INFO("finish abort redef table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::copy_table_dependents(const obcall::ObCopyTableDependentsArg &arg)
{
  LOG_INFO("receive copy table dependents arg", K(arg));
  int ret = OB_SUCCESS;
  const int64_t task_id = arg.task_id_;
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
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, COPY_TABLE_DEPENDENTS_RPC_FAILED))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, COPY_TABLE_DEPENDENTS_RPC_SLOW))) {
    LOG_WARN("ddl sim failure", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::copy_table_dependents(ObDDLTaskID(task_id),
                                                          is_copy_constraints,
                                                          is_copy_indexes,
                                                          is_copy_triggers,
                                                          is_copy_foreign_keys,
                                                          is_ignore_errors))) {
    LOG_WARN("failed to copy table dependents", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "copy table dependents",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", task_id);
  LOG_INFO("finish copy table dependents ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::start_redef_table(const obcall::ObStartRedefTableArg &arg, obcall::ObStartRedefTableRes &res)
{
  LOG_DEBUG("receive start redef table arg", K(arg));
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::start_redef_table(arg, res))) {
    LOG_WARN("start redef table failed", K(ret));
  }
  char table_id_buffer[128];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "orig_table_id:%ld, target_table_id:%ld",
            arg.orig_table_id_, arg.target_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "redef table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish redef table ddl", K(arg), K(ret), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::set_comment(const obcall::ObSetCommentArg &arg, obcall::ObParallelDDLRes &res)
{
  LOG_TRACE("receive set comment arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_())) {
    LOG_WARN("fail to pre check parallel ddl", KR(ret));
  } else {
    ObSetCommentHelper comment_helper(schema_service_, arg, res);
    if (OB_FAIL(comment_helper.init(ddl_service_))) {
      LOG_WARN("fail to init comment helper", KR(ret));
    } else if (OB_FAIL(comment_helper.execute())) {
      LOG_WARN("fail to execute comment", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel set comment",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "schema_version", res.schema_version_);
  LOG_TRACE("finish set comment", KR(ret), K(arg), K(cost));
  return ret;
}

int ObRootService::alter_table(const obcall::ObAlterTableArg &arg, obcall::ObAlterTableRes &res)
{
  LOG_DEBUG("receive alter table arg", K(arg));
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
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
    } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
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
      if (obcall::ObAlterTableArg::DROP_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_DROP_PARTITION;
      } else if (obcall::ObAlterTableArg::DROP_SUB_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_DROP_SUB_PARTITION;
      } else if (obcall::ObAlterTableArg::TRUNCATE_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_TRUNCATE_PARTITION;
      } else if (obcall::ObAlterTableArg::TRUNCATE_SUB_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_TRUNCATE_SUB_PARTITION;
      } else if (obcall::ObAlterTableArg::RENAME_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_RENAME_PARTITION;
      } else if (obcall::ObAlterTableArg::RENAME_SUB_PARTITION == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_RENAME_SUB_PARTITION;
      } else if (obcall::ObAlterTableArg::ALTER_PARTITION_STORAGE_CACHE_POLICY == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_ALTER_PARTITION_POLICY;
      } else if (obcall::ObAlterTableArg::ALTER_SUBPARTITION_STORAGE_CACHE_POLICY == nonconst_arg.alter_part_type_) {
        ddl_type = ObDDLType::DDL_ALTER_SUBPARTITION_POLICY;
      } else {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unexpected ddl type", K(ret), K(nonconst_arg.alter_part_type_), K(nonconst_arg));
      }

      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(schema_guard.get_table_schema(nonconst_arg.alter_table_schema_.get_database_name(),
                                                        nonconst_arg.alter_table_schema_.get_origin_table_name(),
                                                        false  /* is_index*/,
                                                        orig_table_schema))) {
        LOG_WARN("fail to get and check table schema", K(ret));
      } else if (OB_ISNULL(orig_table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(nonconst_arg.alter_table_schema_));
      } else {
        ObCreateDDLTaskParam param(ddl_type,
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
      } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
        LOG_WARN("get schema guard in inner table failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_simple_table_schema(arg.alter_table_schema_.get_table_id(), simple_table_schema))) {
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish alter table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::exchange_partition(const obcall::ObExchangePartitionArg &arg, obcall::ObAlterTableRes &res)
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
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
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

int ObRootService::create_index(const ObCreateIndexArg &arg, obcall::ObAlterTableRes &res)
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
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish create index ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::create_mlog(const obcall::ObCreateMLogArg &arg, obcall::ObCreateMLogRes &res)
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
        schema_guard))) {
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

int ObRootService::parallel_create_index(const ObCreateIndexArg &arg, obcall::ObAlterTableRes &res)
{
  LOG_TRACE("receive parallel create index arg", K(arg));
  int ret = OB_SUCCESS;
  int64_t begin_time = ObTimeUtility::current_time();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_())) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret));
  } else if (share::schema::is_fts_or_multivalue_index(arg.index_type_)
            || share::schema::is_vec_index(arg.index_type_)) {
    ret = OB_NOT_SUPPORTED;
    LOG_WARN("not supported", KR(ret), K(arg.index_type_));
  } else {
    ObCreateIndexHelper create_index_helper(schema_service_, ddl_service_, arg, res);
    if (OB_FAIL(create_index_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create index helper", KR(ret));
    } else if (OB_FAIL(create_index_helper.execute())) {
      LOG_WARN("fail to execute create index table", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  char table_id_buffer[256];
  snprintf(table_id_buffer, sizeof(table_id_buffer), "data_table_id:%ld, index_table_id:%ld",
            arg.data_table_id_, arg.index_table_id_);
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel create index",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_TRACE("finish parallel create index", KR(ret), K(arg), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::fork_table(const obcall::ObForkTableArg &arg, obcall::ObDDLRes &res)
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "tables", table_names_buffer);
  LOG_INFO("finish fork table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_table(const obcall::ObDropTableArg &arg, obcall::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  uint64_t target_object_id = OB_INVALID_ID;
  int64_t schema_version = OB_INVALID_SCHEMA_VERSION;
  bool need_add_to_ddl_scheduler = arg.is_add_to_scheduler_;
  ObSchemaGetterGuard schema_guard;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
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
      if (OB_SUCCESS != (tmp_ret = ddl_service_.check_table_exists(table_item,
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
    ObCreateDDLTaskParam param(ObDDLType::DDL_DROP_TABLE,
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
      res.schema_id_ = target_object_id;
      res.task_id_ = task_record.task_id_;
    }
  } else if (OB_FAIL(ddl_service_.drop_table(arg, res))) {
    LOG_WARN("ddl service failed to drop table", K(ret), K(arg), K(res));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "drop table",
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
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_())) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret));
  } else {
    ObDropTableHelper drop_table_helper(schema_service_, arg, res);
    if (OB_FAIL(drop_table_helper.init(ddl_service_))) {
      LOG_WARN("fail to init drop table helper", KR(ret));
    } else if (OB_FAIL(drop_table_helper.execute())) {
      LOG_WARN("fail to execute drop table", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "drop table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "session_id", arg.session_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish parallel drop table ddl", KR(ret), K(arg), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_database(const obcall::ObDropDatabaseArg &arg, ObDropDatabaseRes &drop_database_res)
{
  int ret = OB_SUCCESS;
  uint64_t database_id = 0;
  int64_t schema_version = 0;
  bool need_add_to_scheduler = arg.is_add_to_scheduler_;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (need_add_to_scheduler) {
    ObSchemaGetterGuard schema_guard;
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (OB_FAIL(schema_guard.get_schema_version(schema_version))) {
      LOG_WARN("fail to get schema version", K(ret), K(arg));
    } else if (OB_FAIL(schema_guard.get_database_id(arg.database_name_, database_id))) {
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
    ObCreateDDLTaskParam param(ObDDLType::DDL_DROP_DATABASE,
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
      drop_database_res.ddl_res_.schema_id_ = database_id;
      drop_database_res.ddl_res_.task_id_ = task_record.task_id_;
    }
  } else if (OB_FAIL(ddl_service_.drop_database(arg, drop_database_res))) {
    LOG_WARN("ddl_service_ drop_database failed", K(arg), K(ret));
  }
  return ret;
}

int ObRootService::drop_tablegroup(const obcall::ObDropTablegroupArg &arg)
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

int ObRootService::alter_tablegroup(const obcall::ObAlterTablegroupArg &arg)
{
  LOG_DEBUG("receive alter tablegroup arg", K(arg));
  const ObTablegroupSchema *tablegroup_schema = NULL;
  ObSchemaGetterGuard schema_guard;
  uint64_t tablegroup_id = OB_INVALID_ID;
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
    LOG_WARN("get schema guard in inner table failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_tablegroup_id(arg.tablegroup_name_,
                                                    tablegroup_id))) {
    LOG_WARN("fail to get tablegroup id", K(ret));
  } else if (OB_INVALID_ID == tablegroup_id) {
    ret = OB_TABLEGROUP_NOT_EXIST;
    LOG_WARN("get invalid tablegroup schema", KR(ret), K(arg));
  } else if (OB_FAIL(schema_guard.get_tablegroup_schema(tablegroup_id, tablegroup_schema))) {
    LOG_WARN("fail to get tablegroup schema", K(ret), K(1UL), K(ret));
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

int ObRootService::drop_index_on_failed(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res)
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish drop index on fail ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::drop_index(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res)
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish drop index ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::rebuild_vec_index(const obcall::ObRebuildIndexArg &arg, obcall::ObAlterTableRes &res)
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "data_table_id", arg.get_data_table_id(),
                        "lob_meta_table_id", arg.get_aux_lob_meta_table_id(),
                        "lob_piece_table_id", arg.get_aux_lob_piece_table_id());
  return ret;
}


int ObRootService::send_auto_split_tablet_task_request(const obcall::ObAutoSplitTabletBatchArg &arg,
                                                       obcall::ObAutoSplitTabletBatchRes &res)
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

int ObRootService::split_global_index_tablet(const obcall::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  ObAlterTableArg &nonconst_arg = const_cast<ObAlterTableArg &>(arg);
  obcall::ObAlterTableRes res;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid()) || arg.is_add_to_scheduler_ || !arg.alter_table_schema_.is_global_index_table()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(ret), K(arg), K(arg.is_add_to_scheduler_), K(arg.alter_table_schema_.is_global_index_table()));
  } else {
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish split global index tablet ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObRootService::clean_splitted_tablet(const obcall::ObCleanSplittedTabletArg &arg)
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

int ObRootService::rename_table(const obcall::ObRenameTableArg &arg)
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

int ObRootService::truncate_table(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res)
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
    if (OB_FAIL(ObMajorFreezeHelper::get_frozen_scn(frozen_scn))) {
      LOG_WARN("get_frozen_scn failed", K(ret));
    } else if (arg.is_add_to_scheduler_) {
      ObDDLTaskRecord task_record;
      ObArenaAllocator allocator(lib::ObLabel("DdlTaskTmp"));
      ObSchemaGetterGuard schema_guard;
      const ObTableSchema *table_schema = nullptr;
      if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
        LOG_WARN("get schema guard in inner table failed", K(ret));
      } else if (OB_FAIL(schema_guard.get_table_schema(arg.database_name_,
                                                       arg.table_name_, false /* is_index */,
                                                       table_schema))) {
        LOG_WARN("fail to get table schema", K(ret));
      } else if (OB_ISNULL(table_schema)) {
        ret = OB_TABLE_NOT_EXIST;
        LOG_WARN("table not exist", K(ret), K(arg));
      } else {
        ObCreateDDLTaskParam param(ObDDLType::DDL_TRUNCATE_TABLE,
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
          res.schema_id_ = table_schema->get_table_id();
          res.task_id_ = task_record.task_id_;
        }
      }
    } else if (OB_FAIL(ddl_service_.truncate_table(arg, res, frozen_scn))) {
      LOG_WARN("ddl service failed to truncate table", K(arg), K(ret), K(frozen_scn));
    }
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "truncate table",
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
int ObRootService::truncate_table_v2(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  // Parallel truncate generates schema versions in batch (gen_batch_new_schema_versions),
  // which only works in the parallel-DDL schema-version context. The former RPC path delivered
  // this op to the dedicated PARALLEL_DDL thread (in_parallel_ddl_thread_()==true); after the RPC
  // framework removal it runs on the tenant ReqWorker, so set the batch-generate flag here to
  // restore that context (otherwise gen_batch_new_schema_versions returns OB_NOT_SUPPORTED).
  struct BatchGenSchemaVersionGuard {
    bool saved_;
    BatchGenSchemaVersionGuard() : saved_(ob_batch_generate_schema_version())
    { ob_batch_generate_schema_version() = true; }
    ~BatchGenSchemaVersionGuard() { ob_batch_generate_schema_version() = saved_; }
  } batch_gen_schema_version_guard;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    SCN frozen_scn;
    if (OB_FAIL(ObMajorFreezeHelper::get_frozen_scn(frozen_scn))) {
      LOG_WARN("get_frozen_scn failed", K(ret));
    } else if (OB_FAIL(ddl_service_.new_truncate_table(arg, res, frozen_scn))) {
      LOG_WARN("ddl service failed to truncate table", K(arg), K(ret));
    }
    ROOTSERVICE_EVENT_ADD("ddl scheduler", "truncate table new",
                          "tid", 1UL,
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

int ObRootService::flashback_table_to_time_point(const obcall::ObFlashBackTableToScnArg &arg)
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
  } else {
    mode = lib::Worker::CompatMode::MYSQL;
    const int64_t all_core_table_id = OB_ALL_CORE_TABLE_TID;
    for (int64_t i = 0; OB_SUCC(ret) && i < arg.tables_.count(); ++i) {
      SMART_VAR(obcall::ObAlterTableArg, alter_table_arg) {
        ObSqlString sql;
        const obcall::ObTableItem &table_item = arg.tables_.at(i);
        const ObTableSchema *table_schema = nullptr;
        alter_table_arg.is_alter_options_ = true;
        alter_table_arg.alter_table_schema_.set_origin_database_name(table_item.database_name_);
        alter_table_arg.alter_table_schema_.set_origin_table_name(table_item.table_name_);
        alter_table_arg.skip_sys_table_check_ = true;
        if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
          LOG_WARN("fail to get tenant schema guard", K(ret));
        } else if (OB_FAIL(schema_guard.get_table_schema(table_item.database_name_, table_item.table_name_, false/*is index*/, table_schema))) {
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
          } else {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("error unexpected, unknown mode", K(ret), K(mode));
          }
          if (OB_SUCC(ret)) {
            alter_table_arg.ddl_stmt_str_ = sql.string();
            obcall::ObAlterTableRes res;
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

int ObRootService::calc_column_checksum_repsonse(const obcall::ObCalcColumnChecksumResponseArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", K(ret));
  } else if (OB_UNLIKELY(!arg.is_valid())) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, PROCESS_COLUMN_CHECKSUM_RESPONSE_SLOW))) {
    LOG_WARN("ddl sim failure: procesc column checksum response slow", K(ret));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::on_column_checksum_calc_reply(
              arg.tablet_id_, ObDDLTaskKey(arg.target_table_id_, arg.schema_version_), arg.ret_code_))) {
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

int ObRootService::update_index_status(const obcall::ObUpdateIndexStatusArg &arg)
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
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "index_table_id", arg.index_table_id_,
                        "data_table_id", arg.data_table_id_);
  return ret;
}

int ObRootService::update_mview_status(const obcall::ObUpdateMViewStatusArg &arg)
{
  int ret = OB_SUCCESS;
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

int ObRootService::parallel_update_index_status(const obcall::ObUpdateIndexStatusArg &arg, obcall::ObParallelDDLRes &res)
{
  LOG_TRACE("receive update index status arg", K(arg));
  int64_t begin_time = ObTimeUtility::current_time();
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_UNLIKELY(!arg.is_valid() || OB_INVALID_ID == arg.data_table_id_)) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", KR(ret), K(arg));
  } else if (OB_FAIL(parallel_ddl_pre_check_())) {
    LOG_WARN("pre check failed before parallel ddl execute", KR(ret));
  } else {
    ObUpdateIndexStatusHelper update_index_status_helper(schema_service_, arg, res);
    if (OB_FAIL(update_index_status_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create table helper", KR(ret));
    } else if (OB_FAIL(update_index_status_helper.execute())) {
      LOG_WARN("fail to execute update index status helper", KR(ret));
    }
  }
  int64_t cost = ObTimeUtility::current_time() - begin_time;
  LOG_TRACE("finish update index status", KR(ret), K(arg), K(cost));
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "parallel update index status",
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

  const int64_t tenant_id = 1UL;
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

  if (FAILEDx(max_id_cache_mgr_.init(&sql_proxy_))) {
    FLOG_WARN("max id cache mgr start failed", KR(ret));
  } else {
    FLOG_INFO("success to start max id cache mgr");
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
    const obcall::ObDDLArg &arg)
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
  } else if (OB_FAIL(trans.start(&sql_proxy_))) {
    LOG_WARN("trans start failed", K(ret));
  } else {
    ObGlobalStatProxy proxy(trans);
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
      LOG_ERROR("trans end failed", "commit", OB_SUCCESS == ret, K(temp_ret));
      ret = (OB_SUCCESS == ret) ? temp_ret : ret;
    }
  }
  return ret;
}

ERRSIM_POINT_DEF(ERROR_EVENT_TABLE_CLEAR_INTERVAL);
int ObRootService::start_timer_tasks()
{
  int ret = OB_SUCCESS;
  bool task_exist = false;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }

  if (OB_SUCCESS == ret && OB_FAIL(TG_TASK_EXIST(event_table_clear_task_tg_id_, event_table_clear_task_, task_exist))) {
    LOG_WARN("failed to check event table clear task exist", KR(ret));
  }
  if (OB_SUCCESS == ret && !task_exist) {
    const int64_t delay = ERROR_EVENT_TABLE_CLEAR_INTERVAL ? 10 * 1000 * 1000 :
      ObEventHistoryTableOperator::EVENT_TABLE_CLEAR_INTERVAL;
    if (OB_FAIL(TG_SCHEDULE(event_table_clear_task_tg_id_, event_table_clear_task_, delay, true, true))) {
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
    TG_CANCEL_TASK(restart_task_tg_id_, restart_task_);
    TG_CANCEL_TASK(load_ddl_task_tg_id_, load_ddl_task_);
    TG_CANCEL_TASK(event_table_clear_task_tg_id_, event_table_clear_task_);
    TG_CANCEL_TASK(purge_recyclebin_task_tg_id_, purge_recyclebin_task_);
  }

  //stop other timer tasks here
  LOG_INFO("stop all timer tasks finish", K(ret));
  return ret;
}

ObRootService::ObRestartTask::ObRestartTask(ObRootService &root_service)
: root_service_(root_service)
{}

ObRootService::ObRestartTask::~ObRestartTask()
{
}

void ObRootService::ObRestartTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("after_restart task begin to process");
  if (GCTX.in_bootstrap_) {
    ret = OB_EAGAIN;
    LOG_INFO("in bootstrap progress, after_restart should wait", KR(ret), K(GCTX.in_bootstrap_));
  } else if (OB_FAIL(root_service_.after_restart())) {
    LOG_WARN("root service after restart failed", K(ret));
  } else {
    TG_CANCEL_TASK(root_service_.restart_task_tg_id_, *this);
  }
  FLOG_INFO("after_restart task process finish", KR(ret));
}

//-----Functions for managing privileges------
int ObRootService::create_user(obcall::ObCreateUserArg &arg,
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

int ObRootService::rename_user(const obcall::ObRenameUserArg &arg,
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

int ObRootService::alter_role(const obcall::ObAlterRoleArg &arg)
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

int ObRootService::set_passwd(const obcall::ObSetPasswdArg &arg)
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


int ObRootService::create_directory(const obcall::ObCreateDirectoryArg &arg)
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

int ObRootService::drop_directory(const obcall::ObDropDirectoryArg &arg)
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

int ObRootService::handle_catalog_ddl(const obcall::ObCatalogDDLArg &arg)
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
    LOG_WARN("Grant catalog error", K(ret), K(arg.user_id_));
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
    ObOriginalDBKey db_key(arg.user_id_, arg.db_);
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
  } else if (FALSE_IT(mode = lib::Worker::CompatMode::MYSQL)) {
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
    ObRoutinePrivSortKey routine_priv_key(arg.user_id_, arg.db_, arg.routine_,
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
    ObString database_name = arg.db_name_;
    ObSchemaGetterGuard schema_guard;
    const ObDatabaseSchema *db_schema = NULL;
    if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
      LOG_WARN("get schema guard in inner table failed", K(ret));
    } else if (database_name == OB_MOCK_DEFAULT_DATABASE_NAME) {
      // if not specify database, set default database name and database id;
      outline_info.set_database_id(OB_MOCK_DEFAULT_DATABASE_ID);
    } else if (OB_FAIL(schema_guard.get_database_schema(database_name, db_schema))) {
      LOG_WARN("get database schema failed", K(ret));
    } else if (NULL == db_schema) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_USER_ERROR(OB_ERR_BAD_DATABASE, database_name.length(), database_name.ptr());
    } else if (db_schema->is_in_recyclebin()) {
      ret = OB_ERR_OPERATION_ON_RECYCLE_OBJECT;
      LOG_WARN("Can't not create outline of db in recyclebin", K(ret), K(arg), K(*db_schema));
    } else if (OB_INVALID_ID == db_schema->get_database_id()) {
      ret = OB_ERR_BAD_DATABASE;
      LOG_WARN("database id is invalid", K(*db_schema), K(ret));
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

int ObRootService::create_user_defined_function(const obcall::ObCreateUserDefinedFunctionArg &arg)
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
  } else if (OB_FAIL(ddl_service_.check_udf_exist(arg.udf_.get_name_str(), exist, udf_id))) {
    LOG_WARN("failed to check_udf_exist", K(arg.udf_.get_name_str()), K(exist), K(ret));
  } else if (exist) {
    ret = OB_UDF_EXISTS;
    LOG_USER_ERROR(OB_UDF_EXISTS, arg.udf_.get_name_str().length(), arg.udf_.get_name_str().ptr());
  } else if (OB_FAIL(ddl_service_.create_user_defined_function(udf_info_, arg.ddl_stmt_str_))) {
    LOG_WARN("failed to create udf", K(arg), K(ret));
  } else {/*do nothing*/}
  return ret;
}

int ObRootService::drop_user_defined_function(const obcall::ObDropUserDefinedFunctionArg &arg)
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

int ObRootService::drop_outline(const obcall::ObDropOutlineArg &arg)
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
                                           obcall::ObRoutineDDLRes &res)
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
                                          obcall::ObRoutineDDLRes &res)
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


int ObRootService::create_package(const obcall::ObCreatePackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_package(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::create_package_with_res(const obcall::ObCreatePackageArg &arg,
                                           obcall::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_package(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::drop_package(const obcall::ObDropPackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_package(arg, ddl_service_));
  return ret;
}

int ObRootService::create_trigger(const obcall::ObCreateTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_trigger(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::create_trigger_with_res(const obcall::ObCreateTriggerArg &arg,
                                           obcall::ObCreateTriggerRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_trigger(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::alter_trigger(const obcall::ObAlterTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_trigger(arg, NULL, ddl_service_));
  return ret;
}

int ObRootService::alter_trigger_with_res(const obcall::ObAlterTriggerArg &arg,
                                          obcall::ObRoutineDDLRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_trigger(arg, &res, ddl_service_));
  return ret;
}

int ObRootService::drop_trigger(const obcall::ObDropTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_trigger(arg, ddl_service_));
  return ret;
}

////////////////////////////////////////////////////////////////
// sequence
////////////////////////////////////////////////////////////////
int ObRootService::do_sequence_ddl(const obcall::ObSequenceDDLArg &arg)
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
int ObRootService::do_context_ddl(const obcall::ObContextDDLArg &arg)
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
int ObRootService::schema_revise(const obcall::ObSchemaReviseArg &arg)
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
    ctx.sql_proxy_ = &sql_proxy_;
    ctx.schema_service_ = schema_service_;
    ctx.ddl_service_ = &ddl_service_;
    ctx.config_mgr_ = config_mgr_;
    ctx.root_service_ = this;
    ctx.inited_ = true;
  }
  return ret;
}

int ObRootService::admin_flush_cache(const obcall::ObAdminFlushCacheArg &arg)
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

int ObRootService::admin_merge(const obcall::ObAdminMergeArg &arg)
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

int ObRootService::admin_recovery(const obcall::ObAdminRecoveryArg &arg)
{
  int ret = OB_NOT_SUPPORTED;
  return ret;
}

int ObRootService::admin_clear_roottable(const obcall::ObAdminClearRoottableArg &arg)
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

int ObRootService::admin_refresh_schema(const obcall::ObAdminRefreshSchemaArg &arg)
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

int ObRootService::admin_set_config(obcall::ObAdminSetConfigArg &arg)
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

int ObRootService::admin_refresh_io_calibration(const obcall::ObAdminRefreshIOCalibrationArg &arg)
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

int ObRootService::admin_clear_merge_error(const obcall::ObAdminMergeArg &arg)
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

int ObRootService::broadcast_ds_action(const obcall::ObDebugSyncActionArg &arg)
{
  LOG_INFO("receive broadcast debug sync actions", K(arg));
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ex_rpc::sync_call([&]{ return GCTX.ob_service_->set_ds_action(arg); }))) {
    LOG_WARN("set server's global sync action failed", K(ret), K(arg));
  }
  return ret;
}

int ObRootService::check_dangling_replica_finish(const obcall::ObCheckDanglingReplicaFinishArg &arg)
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
    //depend on sys schema while start RS
    if (OB_FAIL(schema_service_->refresh_and_add_schema())) {
      LOG_WARN("refresh schema failed", K(ret), K(load_frozen_status));
    } else if (OB_FAIL(schema_service_->get_tenant_schema_version(schema_version))) {
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
  if (OB_FAIL(sql_proxy.write(sql, affected_rows))) {
    LOG_WARN("execute sql failed", K(sql));
  }

  return ret;
}

int ObRootService::admin_set_tracepoint(const obcall::ObAdminSetTPArg &arg)
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
int ObRootService::refresh_time_zone_info(const obcall::ObRefreshTimezoneArg &arg)
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

  ObTZMapWrap tz_map_wrap;
  ObTimeZoneInfoManager *tz_info_mgr = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(OTTZ_MGR.get_tenant_timezone(tz_map_wrap, tz_info_mgr))) {
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
  : root_service_(root_service)
{}

void ObRootService::ObLoadDDLTask::runTimerTask()
{
  int ret = ObSysDDLSchedulerUtil::recover_task();
  if (OB_FAIL(ret)) {
    LOG_WARN("recover ddl task failed", KR(ret));
  } else {
    TG_CANCEL_TASK(root_service_.load_ddl_task_tg_id_, *this);
  }
}

/////////////////////////
status::ObRootServiceStatus ObRootService::get_status() const
{
  return rs_status_.get_rs_status();
}

int ObRootService::table_allow_ddl_operation(const obcall::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *schema = NULL;
  ObSchemaGetterGuard schema_guard;
  const AlterTableSchema &alter_table_schema = arg.alter_table_schema_;
  const ObString &origin_database_name = alter_table_schema.get_origin_database_name();
  const ObString &origin_table_name = alter_table_schema.get_origin_table_name();
  schema_guard.set_session_id(arg.session_id_);
  bool is_index = arg.alter_table_schema_.is_index_table();
  if (arg.is_refresh_sess_active_time()) {
    //do nothing
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invali argument", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(schema_guard))) {
    LOG_WARN("get schema guard in inner table failed", K(ret));
  } else if (OB_FAIL(schema_guard.get_table_schema(origin_database_name,
                                                   origin_table_name, is_index, schema))) {
    LOG_WARN("fail to get table schema", K(ret), K(origin_database_name), K(origin_table_name));
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
int ObRootService::update_stat_cache(const obcall::ObUpdateStatCacheArg &arg)
{
  int ret = OB_SUCCESS;
  ObZone null_zone;
  bool evict_plan_failed = false;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(ex_rpc::sync_call([&]{ return ObOptStatManager::get_instance().add_refresh_stat_task(arg); }))) {
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
  valid = true;

  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(sys_schema_guard))) {
    LOG_WARN("get sys schema guard failed", KR(ret));
  } else {
    ObSchemaGetterGuard schema_guard;
    const ObSimpleTenantSchema *tenant_schema = NULL;
    const ObSysVarSchema *var_schema = NULL;
    ObObj obj;
    int64_t session_max_stale_time = 0;
    if (OB_SUCC(ret) && valid) {
      if (OB_FAIL(sys_schema_guard.get_tenant_info(tenant_schema))) {
        LOG_WARN("fail to get tenant schema", KR(ret));
      } else if (OB_ISNULL(tenant_schema)) {
        ret = OB_SUCCESS;
        LOG_WARN("tenant schema is null, skip and continue", KR(ret));
      } else if (!tenant_schema->is_normal()) {
        ret = OB_SUCCESS;
        LOG_WARN("tenant schema is not normal, skip and continue", KR(ret));
      } else if (OB_FAIL(GCTX.schema_service_->get_tenant_schema_guard(schema_guard))) {
        LOG_WARN("get schema guard failed", KR(ret));
      } else if (OB_FAIL(schema_guard.get_tenant_system_variable(OB_SV_MAX_READ_STALE_TIME, var_schema))) {
        LOG_WARN("get tenant system variable failed", KR(ret));
      } else if (OB_ISNULL(var_schema)) {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("var schema is null", KR(ret));
      } else if (OB_FAIL(var_schema->get_value(NULL, NULL, obj))) {
        LOG_WARN("get value failed", KR(ret), K(obj));
      } else if (OB_FAIL(obj.get_int(session_max_stale_time))) {
        LOG_WARN("get int failed", KR(ret), K(obj));
      } else if (session_max_stale_time != share::ObSysVarMeta::INVALID_MAX_READ_STALE_TIME
                 && refresh_interval > session_max_stale_time) {
        valid = false;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT,
                       "weak_read_version_refresh_interval is larger than ob_max_read_stale_time");
      }
    }
  }
  return ret;
}

int ObRootService::set_config_pre_hook(obcall::ObAdminSetConfigArg &arg)
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
    } else if (0 == STRCMP(item->name_.ptr(), LOG_DISK_UTILIZATION_LIMIT_THRESHOLD)) {
      // check log_disk_utilization_limit_threshold
      for (int i = 0; i < item->batch_ids_.count() && valid; i++) {
        valid = valid && ObConfigLogDiskLimitThresholdIntChecker::check(*item);
        if (!valid) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_disk_utilization_limit_threshold should be greater than log_disk_throttling_percentage "
                        "when log_disk_throttling_percentage is not equal to 100");
          LOG_WARN("config invalid", "item", *item, K(ret), K(i), K(item->batch_ids_.at(i)));
        }
      }
    } else if (0 == STRCMP(item->name_.ptr(), LOG_DISK_THROTTLING_PERCENTAGE)) {
      // check log_disk_throttling_percentage
      for (int i = 0; i < item->batch_ids_.count() && valid; i++) {
        valid = valid && ObConfigLogDiskThrottlingPercentageIntChecker::check(*item);
        if (!valid) {
          ret = OB_INVALID_ARGUMENT;
          LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_disk_throttling_percentage should be equal to 100 or smaller than log_disk_utilization_limit_threshold");
          LOG_WARN("config invalid", "item", *item, K(ret), K(i), K(item->batch_ids_.at(i)));
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
    for (int i = 0; i < item.batch_ids_.count() && valid; i++) {                           \
      valid = valid && FUNCTOR::check(item);                                               \
      if (!valid) {                                                                        \
        ret = OB_INVALID_ARGUMENT;                                                         \
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, LOG_INFO);                                     \
        LOG_WARN("config invalid", "item", item, K(ret), K(i), K(item.batch_ids_.at(i)));  \
      }                                                                                    \
    }                                                                                      \
  } while (0)

#define CHECK_CLUSTER_CONFIG_WITH_FUNC(FUNCTOR, LOG_INFO)                                  \
  do {                                                                                     \
    if (!FUNCTOR::check(item)) {                                                           \
      ret = OB_INVALID_ARGUMENT;                                                           \
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, LOG_INFO);                                       \
      LOG_WARN("config invalid", "item", item, K(ret));                                   \
    }                                                                                      \
  } while (0)

int ObRootService::check_tx_share_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  // There is a prefix "Incorrect arguments to " before user log so the warn log looked kinds of wired
  const char *warn_log = "tenant config _tx_share_memory_limit_percentage. "
                         "It should larger than or equal with any single module in it(Memstore, TxData, Mds, Vector)";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigTxShareMemoryLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_memstore_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "cluster config memstore_limit_percentage. "
                         "It should less than or equal with all tenant's _tx_share_memory_limit_percentage";
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else {
    CHECK_CLUSTER_CONFIG_WITH_FUNC(ObConfigMemstoreLimitChecker, warn_log);
  }
  return ret;
}

int ObRootService::check_vector_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "ob_vector_limit_percentage. "
                         "It should be less than _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigVectorMemoryChecker, warn_log);
  return ret;
}

int ObRootService::check_tenant_memstore_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant config _memstore_limit_percentage. "
    "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigMemstoreLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_tx_data_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant config _tx_data_memory_limit_percentage. "
                         "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigTxDataLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_mds_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant config _mds_memory_limit_percentage. "
                         "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigMdsLimitChecker, warn_log);
  return ret;
}

int ObRootService::check_freeze_trigger_percentage_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant freeze_trigger_percentage "
                         "which should smaller than writing_throttling_trigger_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigFreezeTriggerIntChecker, warn_log);
  return ret;
}

int ObRootService::check_write_throttle_trigger_percentage(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "tenant writing_throttling_trigger_percentage "
                         "which should greater than freeze_trigger_percentage";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigWriteThrottleTriggerIntChecker, warn_log);
  return ret;
}

int ObRootService::check_no_logging(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "set _no_logging, becacuse archivelog and _no_logging are exclusive parameters";
  CHECK_TENANTS_CONFIG_WITH_FUNC(ObConfigDDLNoLoggingChecker, warn_log);
  return ret;
}

int ObRootService::check_data_disk_write_limit_(obcall::ObAdminSetConfigItem &item)
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

int ObRootService::check_data_disk_usage_limit_(obcall::ObAdminSetConfigItem &item)
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

//ensure execute on DDL thread
int ObRootService::force_create_sys_table(const obcall::ObForceCreateSysTableArg &arg)
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



// if tenant =  OB_INVALID_TENANT_ID, indicates refresh all tenants's schema;
// otherwise, refresh specify tenant's schema. ensure schema_version not fallback by outer layer logic.
int ObRootService::broadcast_schema(const obcall::ObBroadcastSchemaArg &arg)
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
    if (true) {
      // tenant is valid, just refresh specify tenant's schema.
      schema_info.set_schema_version(arg.schema_version_);
    } else {
      // tenant =  OB_INVALID_TENANT_ID, indicates refresh all tenants's schema;
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
      schema_service_->get_ddl_epoch_mgr().remove_ddl_epoch();
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
    const obcall::ObGetSchemaArg &arg,
    obcall::ObTenantSchemaVersions &tenant_schema_versions)
{
  int ret = OB_SUCCESS;
  tenant_schema_versions.reset();
  ObSchemaGetterGuard schema_guard;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_service is null", K(ret));
  } else if (OB_FAIL(ddl_service_.get_tenant_schema_guard_with_version_in_inner_table(
                     schema_guard))) {
    LOG_WARN("fail to get schema guard", KR(ret));
  } else {
    int64_t schema_version = 0;
    {
      ObSchemaGetterGuard tenant_schema_guard;
      schema_version = 0;
      if (true
          || STANDBY_CLUSTER == ObClusterInfoGetter::get_cluster_role_v2()) {
        // For the follower, since schema_status is not advanced by the DDL thread and can accept eventual consistency,
        // Thus, only the local schema version needs to be retrieved
        if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(
                    schema_version))) {
          LOG_WARN("fail to get tenant refreshed schema version", K(ret));
        }
      } else {
        // for primary cluster, need to get newest schema_version from inner table.
        ObRefreshSchemaStatus schema_status;
        int64_t version_in_inner_table = OB_INVALID_VERSION;
        bool is_restore = false;
        if (OB_FAIL(schema_service_->check_tenant_is_restore(&schema_guard, is_restore))) {
          LOG_WARN("fail to check tenant is restore", KR(ret));
        } else if (is_restore) {
          ObSchemaStatusProxy *schema_status_proxy = GCTX.schema_status_proxy_;
          if (OB_ISNULL(schema_status_proxy)) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("schema_status_proxy is null", KR(ret));
          } else if (OB_FAIL(schema_status_proxy->get_refresh_schema_status(schema_status))) {
            LOG_WARN("failed to get tenant refresh schema status", KR(ret));
          } else if (OB_INVALID_VERSION != schema_status.readable_schema_version_) {
            ret = OB_EAGAIN;
            LOG_WARN("tenant's sys replicas are not restored yet, try later", KR(ret));
          }
        }
        if (FAILEDx(schema_service_->get_schema_version_in_inner_table(
                    sql_proxy_, schema_status, version_in_inner_table))) {
          // failed tenant creation, inner table is empty, return OB_CORE_SCHEMA_VERSION
          if (OB_EMPTY_RESULT == ret) {
            LOG_INFO("create tenant maybe failed", K(ret));
            schema_version = OB_CORE_SCHEMA_VERSION;
            ret = OB_SUCCESS;
          } else {
            LOG_WARN("fail to get latest schema version in inner table", K(ret));
          }
        } else if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(
                           schema_version))) {
          LOG_WARN("fail to get tenant refreshed schema version", K(ret));
        } else if (schema_version < version_in_inner_table) {
          if (OB_FAIL(schema_service_->refresh_and_add_schema())) {
            LOG_WARN("fail to refresh schema", K(ret));
          } else if (OB_FAIL(schema_service_->get_tenant_refreshed_schema_version(
                             schema_version))) {
            LOG_WARN("fail to get tenant refreshed schema version", K(ret));
          } else if (schema_version < version_in_inner_table) {
            ret = OB_ERR_UNEXPECTED;
            LOG_WARN("local version is still less than version in table",
                     K(ret), K(schema_version), K(version_in_inner_table));
          } else {}
        } else {}
      }
      if (OB_FAIL(ret)) {
      } else if (OB_FAIL(tenant_schema_versions.add(schema_version))) {
        LOG_WARN("fail to add tenant schema version", KR(ret), K(schema_version));
      }
      if (OB_FAIL(ret) && arg.ignore_fail_ && false) {
        int64_t invalid_schema_version = OB_INVALID_SCHEMA_VERSION;
        if (OB_FAIL(tenant_schema_versions.add(invalid_schema_version))) {
          LOG_WARN("fail to add tenant schema version", KR(ret), K(schema_version));
        }
      }
    } // end
  }
  return ret;
}


int ObRootService::get_recycle_schema_versions(
    const obcall::ObGetRecycleSchemaVersionsArg &arg,
    obcall::ObGetRecycleSchemaVersionsResult &result)
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

int ObRootService::build_ddl_single_replica_response(const obcall::ObDDLBuildSingleReplicaResponseArg &arg)
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
  } else if (OB_FAIL(DDL_SIM(arg.task_id_, PROCESS_BUILD_SSTABLE_RESPONSE_SLOW))) {
    LOG_WARN("ddl sim failure: procesc build sstable response slow", K(ret));
  } else if (OB_FAIL(ObSysDDLSchedulerUtil::on_sstable_complement_job_reply(
          arg.tablet_id_/*source tablet id*/, arg.server_addr_,
          ObDDLTaskKey(arg.dest_schema_id_, arg.dest_schema_version_),
          arg.snapshot_version_, arg.execution_id_, arg.ret_code_, info))) {
    LOG_WARN("handle column checksum calc response failed", K(ret), K(arg));
  }
  ROOTSERVICE_EVENT_ADD("ddl scheduler", "build ddl single replica response",
                        "tid", 1UL,
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
  ObSchemaGetterGuard guard;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_serviece_ is null", KR(ret));
  } else if (OB_FAIL(schema_service_->get_tenant_schema_guard(guard))) {
    LOG_WARN("fail to get sys schema guard", KR(ret));
  } else {
    const int64_t current_time = ObTimeUtility::current_time();
    obcall::Int64 expire_time = current_time - expire_timeval;
    const int64_t SLEEP_INTERVAL = 100 * 1000;  //100ms interval of send rpc
    const int64_t PURGE_EACH_RPC = 10;          //delete count per rpc
    obcall::Int64 affected_rows = 0;
    obcall::ObPurgeRecycleBinArg arg;
    int64_t purge_sum = purge_each_time;
    const bool is_standby = PRIMARY_CLUSTER != ObClusterInfoGetter::get_cluster_role_v2();
    const ObSimpleTenantSchema *simple_tenant = NULL;
    //ignore ret
    for (int i = 0; i < 1 && in_service() && purge_sum > 0; ++i) {  // lite: single sys tenant
      int64_t purge_time = GCONF._recyclebin_object_purge_frequency;
      if (purge_time <= 0) {
        break;
      }
      if (false && is_standby) {
        // standby cluster won't purge recyclebin automacially.
        LOG_TRACE("user tenant won't purge recyclebin automacially in standby cluster");
        continue;
      } else if (OB_FAIL(guard.get_tenant_info(simple_tenant))) {
        LOG_WARN("fail to get simple tenant schema", KR(ret));
      } else if (OB_ISNULL(simple_tenant)) {
        ret = OB_TENANT_NOT_EXIST;
        LOG_WARN("simple tenant schema not exist", KR(ret));
      } else if (!simple_tenant->is_normal()) {
        // only deal with normal tenant.
        LOG_TRACE("tenant which isn't normal won't purge recyclebin automacially");
        continue;
      }
      // ignore error code of different tenant
      ret = OB_SUCCESS;
      affected_rows = 0;
      arg.expire_time_ = expire_time;
      arg.auto_purge_ = true;
      LOG_INFO("start purge recycle objects of tenant", K(arg), K(purge_sum));
      while (OB_SUCC(ret) && in_service() && purge_sum > 0) {
        int64_t cal_timeout = 0;
        int64_t start_time = ObTimeUtility::current_time();
        arg.purge_num_ = purge_sum > PURGE_EACH_RPC ? PURGE_EACH_RPC : purge_sum;
        if (OB_FAIL(schema_service_->cal_purge_need_timeout(arg, cal_timeout))) {
          LOG_WARN("fail to cal purge need timeout", KR(ret), K(arg));
        } else if (0 == cal_timeout) {
          LOG_INFO("cal purge need timeout is zero, just exit", K(purge_sum));
          break;
        } else if (OB_FAIL(this->purge_expire_recycle_objects(arg, affected_rows))) {
          LOG_WARN("purge reyclebin objects failed", KR(ret),
              K(current_time), K(expire_time), K(affected_rows), K(arg));
        } else {
          purge_sum -= affected_rows;
          if (arg.purge_num_ != affected_rows) {
            int64_t cost_time = ObTimeUtility::current_time() - start_time;
            LOG_INFO("purge recycle objects", KR(ret), K(cost_time), K(purge_sum),
                                              K(cal_timeout), K(expire_time), K(current_time), K(affected_rows));
            if (OB_SUCC(ret) && in_service()) {
              ob_usleep(SLEEP_INTERVAL);
            }
            break;
          }
        }
        int64_t cost_time = ObTimeUtility::current_time() - start_time;
        LOG_INFO("purge recycle objects", KR(ret), K(cost_time), K(purge_sum),
                                          K(cal_timeout), K(expire_time), K(current_time), K(affected_rows));
        if (OB_SUCC(ret) && in_service()) {
          ob_usleep(SLEEP_INTERVAL);
        }
      }
    }
  }
  return ret;
}

int ObRootService::flush_opt_stat_monitoring_info(const obcall::ObFlushOptStatArg &arg)
{
  int ret = OB_SUCCESS;
  ObZone empty_zone;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(ex_rpc::sync_call([&]() -> int { int ret = OB_SUCCESS; MOD_SCOPE { ObOptStatMonitorManager *m = share::g_mp->opt_stat_monitor_manager(); if (OB_ISNULL(m)) { ret = OB_ERR_UNEXPECTED; } else if (OB_FAIL(m->update_opt_stat_monitoring_info(arg))) {} } return ret; }))) {
      LOG_WARN("fail to update table statistic", K(ret));
    } else { /*do nothing*/}
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

int ObRootService::recompile_all_views_batch(const obcall::ObRecompileAllViewsBatchArg &arg)
{
  int ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.recompile_all_views_batch(arg.view_ids_))) {
    LOG_WARN("failed to recompile all views", K(ret));
  }
  LOG_INFO("recompile all views batch finish", KR(ret), K(start_time),
      "cost_time", ObTimeUtility::current_time() - start_time);
  return ret;
}

int ObRootService::check_transfer_task_tablet_count_threshold_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  bool valid = true;
  int64_t value = ObConfigIntParser::get(item.value_.ptr(), valid);
  if (valid && (value > OB_MAX_TRANSFER_BINDING_TABLET_CNT)) {
    valid = false;
    char err_msg[DEFAULT_BUF_LENGTH];
    (void)snprintf(err_msg, sizeof(err_msg), "_transfer_task_tablet_count_threshold of tenant 1, "
        "it cannot be greater than %ld", OB_MAX_TRANSFER_BINDING_TABLET_CNT);
    LOG_USER_ERROR(OB_INVALID_ARGUMENT, err_msg);
  }
  if (!valid) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("config invalid", KR(ret), K(value), K(item));
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
      MOD_SCOPE {
        rootserver::ObDDLServiceLauncher* ddl_service_launcher = share::g_mp->ddl_service_launcher();
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

int ObRootService::create_ccl_rule_ddl(const obcall::ObCreateCCLRuleArg &arg)
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

int ObRootService::drop_ccl_rule_ddl(const obcall::ObDropCCLRuleArg &arg)
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

int ObRootService::create_ai_model(const obcall::ObCreateAiModelArg &arg)
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

int ObRootService::drop_ai_model(const obcall::ObDropAiModelArg &arg)
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



int ObRootService::create_location(const obcall::ObCreateLocationArg &arg)
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

int ObRootService::drop_location(const obcall::ObDropLocationArg &arg)
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
    ObObjMysqlPrivSortKey object_key(arg.user_id_, arg.obj_name_, arg.obj_type_);
    OZ (objpriv_mysql_ddl_service.revoke_object(object_key, arg.priv_set_, arg.grantor_, arg.grantor_host_));
  }
  return ret;
}


} // end namespace rootserver
} // end namespace oceanbase
