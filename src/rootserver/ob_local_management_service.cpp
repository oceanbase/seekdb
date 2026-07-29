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
#include "ob_local_management_service.h"
#include "observer/ob_server.h"



#include "share/ob_global_stat_proxy.h"
#include "share/ob_version_parser.h"
#include "sql/resolver/ddl/ob_index_builder_util.h"
#include "storage/deadlock/ob_deadlock_inner_table_service.h"

#include "sql/engine/cmd/ob_user_cmd_executor.h"
#include "src/sql/engine/px/ob_dfo.h"
#include "observer/dbms_job/ob_dbms_job_master.h"

#include "rootserver/ob_bootstrap.h"
#include "rootserver/ob_partition_exchange.h"
#include "rootserver/ob_schema2ddl_sql.h"
#include "rootserver/ob_index_builder.h"
#include "rootserver/ob_ddl_sql_generator.h"
#include "rootserver/ddl_task/ob_ddl_task.h"
#include "rootserver/ddl_task/ob_constraint_task.h"
#include "rootserver/ob_admin_job_table_operator.h"
#include "share/ob_ddl_sim_point.h"
#include "rootserver/ob_control_event.h"
#include "share/ob_timezone_mgr.h"

#include "rootserver/freeze/ob_major_freeze_helper.h"
#include "share/ob_ddl_common.h" // for ObDDLUtil
#include "rootserver/ddl_task/ob_sys_ddl_util.h" // for ObSysDDLSchedulerUtil
#include "rootserver/ob_ddl_service_launcher.h" // for ObDDLServiceLauncher
#include "observer/ob_system_package_load_task.h"
#include "observer/ob_service.h"

#include "parallel_ddl/ob_create_table_helper.h" // ObCreateTableHelper
#include "parallel_ddl/ob_create_table_like_helper.h" // ObCreateTableLikeHelper
#include "rootserver/parallel_ddl/ob_create_view_helper.h"  // ObCreateViewHelper
#include "parallel_ddl/ob_set_comment_helper.h" //ObCommentHelper
#include "parallel_ddl/ob_create_index_helper.h" // ObCreateIndexHelper
#include "parallel_ddl/ob_update_index_status_helper.h" // ObUpdateIndexStatusHelper
#include "pl_ddl/ob_pl_ddl_service.h"
#include "parallel_ddl/ob_drop_table_helper.h" // ObDropTableHelper
#include "rootserver/ob_ai_model_ddl_service.h"
#include "lib/utility/ob_print_utils.h"     // databuff_printf
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

ObLocalManagementService::ObLocalManagementService()
: inited_(false), need_bootstrap_(false), local_services_ready_(false),
    debug_(false),
    self_addr_(), config_(NULL), config_mgr_(NULL),
    sql_proxy_(),
    schema_service_(NULL),
    root_minor_freeze_(),
    ddl_service_(),
    bootstrap_lock_(),
    load_ddl_task_timer_(),
    deadlock_event_clear_task_timer_(),
    purge_recyclebin_task_timer_(),
    load_ddl_task_(*this),
    deadlock_event_clear_task_(),
    purge_recyclebin_task_(*this),
    snapshot_manager_(),
    core_meta_table_version_(0),
    baseline_schema_version_(0),
    max_id_cache_mgr_()
{
}

ObLocalManagementService::~ObLocalManagementService()
{
  if (inited_) {
    destroy();
  }
}

int ObLocalManagementService::init(ObServerConfig &config,
                        ObConfigManager &config_mgr,
                        ObAddr &self,
                        ObMySQLProxy &sql_proxy,
                        ObMultiVersionSchemaService *schema_service,
                        const bool need_bootstrap)
{
  int ret = OB_SUCCESS;
  FLOG_INFO("begin to initialize local management services");
  if (inited_) {
    ret = OB_INIT_TWICE;
    FLOG_WARN("local management services already initialized", KR(ret));
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
    need_bootstrap_ = need_bootstrap;
  }

  if (FAILEDx(load_ddl_task_timer_.init("LocalLoadDDL", ObMemAttr("LocalLoadDDL")))) {
    FLOG_WARN("init local load ddl task timer failed", KR(ret));
  } else if (OB_FAIL(deadlock_event_clear_task_timer_.init(
                 "LocalDlkClear", ObMemAttr("LocalDlkClear")))) {
    FLOG_WARN("init local deadlock event clear task timer failed", KR(ret));
  } else if (OB_FAIL(purge_recyclebin_task_timer_.init("LocalRecycle", ObMemAttr("LocalRecycle")))) {
    FLOG_WARN("init local purge recyclebin task timer failed", KR(ret));
  } else if (OB_FAIL(root_minor_freeze_.init())) {
    // init root minor freeze
    FLOG_WARN("init root_minor_freeze_ failed", KR(ret));
  } else if (OB_FAIL(ddl_service_.init(*GCTX.sql_proxy_, *GCTX.schema_service_,
                                       snapshot_manager_, runtime_ddl_service_))) {
    // init ddl service
    FLOG_WARN("init ddl_service_ failed", KR(ret));
  } else if (OB_FAIL(runtime_ddl_service_.init(ddl_service_,
          sql_proxy_, *schema_service))) {
    // Initialize the server runtime DDL service.
    FLOG_WARN("init runtime_ddl_service_ failed", KR(ret));
  } else if (OB_FAIL(snapshot_manager_.init(self_addr_))) {
    FLOG_WARN("init snapshot manager failed", KR(ret));
  } else if (OB_FAIL(THE_ADMIN_JOB_TABLE.init())) {
    FLOG_WARN("init THE_ADMIN_JOB_TABLE failed", KR(ret));
  } else if (OB_FAIL(dbms_job::ObDBMSJobMaster::get_instance().init(&sql_proxy_,
                                                                    schema_service_))) {
    FLOG_WARN("init ObDBMSJobMaster failed", KR(ret));
  }

  if (OB_SUCC(ret)) {
    inited_ = true;
    FLOG_INFO("initialize local management services succeeded", KR(ret), K_(inited));
  } else {
    LOG_ERROR("failed to initialize local management services", KR(ret));
  }

  return ret;
}

void ObLocalManagementService::destroy()
{
  int ret = OB_SUCCESS;
  int fail_ret = OB_SUCCESS;
  FLOG_INFO("start to destroy local management services");
  if (sql_proxy_.is_active()) {
    if (OB_FAIL(stop_service())) {
      FLOG_WARN("stop service failed", KR(ret));
      fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
    }
  }

  load_ddl_task_timer_.destroy();
  deadlock_event_clear_task_timer_.destroy();
  purge_recyclebin_task_timer_.destroy();
  FLOG_INFO("task timer destroy");

  dbms_job::ObDBMSJobMaster::get_instance().destroy();
  FLOG_INFO("ObDBMSJobMaster destroy");

  if (OB_SUCC(ret)) {
    if (inited_) {
      inited_ = false;
    }
  }

  FLOG_INFO("destroy local management services finished", KR(ret));
  if (OB_SUCCESS != fail_ret) {
    LOG_WARN("destroy local management services had failures", KR(fail_ret));
  }
}

int ObLocalManagementService::start_service()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("start local management services", KCSTRING(lbt()));
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("local management services not initialized", KR(ret));
  } else if (sql_proxy_.is_active()) {
    ret = OB_INIT_TWICE;
    FLOG_WARN("local management services already started", KR(ret));
  } else {
    sql_proxy_.set_active();
    runtime_ddl_service_.restart();
    if (OB_FAIL(load_ddl_task_timer_.start())) {
      FLOG_WARN("load ddl task timer start failed", KR(ret));
    } else if (OB_FAIL(deadlock_event_clear_task_timer_.start())) {
      FLOG_WARN("event table clear task timer start failed", KR(ret));
    } else if (OB_FAIL(purge_recyclebin_task_timer_.start())) {
      FLOG_WARN("purge recyclebin task timer start failed", KR(ret));
    }
  }

  if (OB_FAIL(ret)) {
    FLOG_WARN("start local management services failed", KR(ret));
    int tmp_ret = OB_SUCCESS;
    if (sql_proxy_.is_active() && OB_SUCCESS != (tmp_ret = stop_service())) {
      FLOG_WARN("stop service failed", KR(tmp_ret));
    }
  }

  FLOG_INFO("start local management services finished", KR(ret));
  return ret;
}

int ObLocalManagementService::start_runtime_dependent_services()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("local management services not initialized", KR(ret));
  } else if (!sql_proxy_.is_active()) {
    ret = OB_NOT_INIT;
    FLOG_WARN("local management sql proxy is not active", KR(ret));
  } else {
    if (local_services_ready_) {
      FLOG_INFO("runtime dependent local services already started");
    } else if (OB_FAIL(start_local_services_())) {
      FLOG_WARN("failed to start runtime dependent local services", KR(ret));
    }
    if (OB_SUCC(ret) && debug_ && OB_FAIL(init_debug_database())) {
      FLOG_WARN("init_debug_database failed", KR(ret));
    }
  }
  return ret;
}

int ObLocalManagementService::stop_service()
{
  int ret = OB_SUCCESS;
  FLOG_INFO("stop local management services begin");
  if (OB_FAIL(stop())) {
    FLOG_WARN("fail to stop thread", KR(ret));
  } else {
    wait();
  }
  FLOG_INFO("stop local management services finished", KR(ret));
  return ret;
}

int ObLocalManagementService::stop()
{
  int ret = OB_SUCCESS;
  int fail_ret = OB_SUCCESS;
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("start to stop local management services", K(start_time));
  if (!inited_) {
    ret = OB_NOT_INIT;
    FLOG_WARN("local management services not initialized", KR(ret));
    fail_ret = OB_SUCCESS == fail_ret ? ret : fail_ret;
  } else {
    local_services_ready_ = false;
    sql_proxy_.set_inactive();
    FLOG_INFO("sql_proxy set inactive finished");

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
      runtime_ddl_service_.stop();
      FLOG_INFO("ddl service stop");
      root_minor_freeze_.stop();
      FLOG_INFO("minor freeze stop");
    }
    if (OB_SUCC(ret)) {
      load_ddl_task_timer_.stop();
      deadlock_event_clear_task_timer_.stop();
      purge_recyclebin_task_timer_.stop();
      FLOG_INFO("task timer stop");
      dbms_job::ObDBMSJobMaster::get_instance().stop();
      FLOG_INFO("dbms job master stop");
      max_id_cache_mgr_.reset();
      FLOG_INFO("max id cache mgr reset");
    }
  }

  FLOG_INFO("finish stop local management services", KR(ret));
  if (OB_SUCCESS != fail_ret) {
    LOG_WARN("stop local management services had failures", KR(fail_ret));
  }
  return ret;
}

void ObLocalManagementService::wait()
{
  FLOG_INFO("wait local management services begin");
  int64_t start_time = ObTimeUtility::current_time();
  FLOG_INFO("start to wait all thread exit");
  if (load_ddl_task_timer_.inited()) { load_ddl_task_timer_.wait(); }
  if (deadlock_event_clear_task_timer_.inited()) { deadlock_event_clear_task_timer_.wait(); }
  if (purge_recyclebin_task_timer_.inited()) { purge_recyclebin_task_timer_.wait(); }
  FLOG_INFO("task timer exit success");
  THE_ADMIN_JOB_TABLE.reset_max_job_id();
  int64_t cost = ObTimeUtility::current_time() - start_time;
  FLOG_INFO("wait local management services finished", K(start_time), K(cost));
  if (cost > 10 * 60 * 1000 * 1000L) { // 10min
    int ret = OB_ERROR;
    LOG_ERROR("cost too much time to wait for local management services", KR(ret), K(start_time), K(cost));
  }
}

int ObLocalManagementService::submit_ddl_local_build_task(ObAsyncTask &task)
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("ObLocalManagementService has not been inited", K(ret));
  } else if (OB_FAIL(ObSysDDLLocalBuilderUtil::push_task(task))) {
    LOG_WARN("fail to push task to ddl builder", KR(ret));
  }
  return ret;
}

int ObLocalManagementService::schedule_recyclebin_task(int64_t delay)
{
  int ret = OB_SUCCESS;
  const bool did_repeat = false;

  if (OB_FAIL(purge_recyclebin_task_timer_.schedule(
              purge_recyclebin_task_, delay, did_repeat))) {
    if (OB_CANCELED != ret) {
      LOG_ERROR("schedule purge recyclebin task failed", KR(ret), K(delay), K(did_repeat));
    } else {
      LOG_WARN("schedule purge recyclebin task failed", KR(ret), K(delay), K(did_repeat));
    }
  }

  return ret;
}

int ObLocalManagementService::schedule_load_ddl_task()
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
  } else if (FALSE_IT(task_exist = load_ddl_task_timer_.task_exist(load_ddl_task_))) {
  } else if (task_exist) {
    // ignore error
    LOG_WARN("load ddl task already exist", K(ret));
  } else if (OB_FAIL(load_ddl_task_timer_.schedule(load_ddl_task_, delay, did_repeat))) {
    LOG_WARN("fail to add timer task", K(ret));
  } else {
    LOG_INFO("succeed to add load ddl task");
  }
  return ret;
}

////////////////////////////////////////////////////////////////
int ObLocalManagementService::execute_bootstrap()
{
  int ret = OB_SUCCESS;
  BOOTSTRAP_LOG(INFO, "STEP_1.1:execute_bootstrap start to executor.");
  DBA_STEP_RESET(bootstrap);
  LOG_DBA_INFO_V2(OB_BOOTSTRAP_BEGIN,
                  DBA_STEP_INC_INFO(bootstrap),
                  "cluster bootstrap begin.");
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("local_management_service not inited", K(ret));
  } else if (!sql_proxy_.is_inited() || !sql_proxy_.is_active()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("sql_proxy not inited or not active", "sql_proxy inited",
             sql_proxy_.is_inited(), "sql_proxy active", sql_proxy_.is_active(), K(ret));
  } else {
    FLOG_INFO("try to get local-service lock in execute_bootstrap");
    ObLatchWGuard guard(bootstrap_lock_, ObLatchIds::RS_BOOTSTRAP_LOCK);
    FLOG_INFO("success to get local-service lock in execute_bootstrap");
    ObBootstrap bootstrap(ddl_service_, runtime_ddl_service_,
        *config_);
    if (OB_FAIL(bootstrap.execute_bootstrap())) {
      LOG_ERROR("failed to execute_bootstrap", K(ret));
    }

    BOOTSTRAP_LOG(INFO, "start local services");
    ObGlobalStatProxy global_proxy(sql_proxy_);
    ObArray<ObAddr> self_addr;
    ObTimeoutCtx ctx;
    if (OB_FAIL(ret)) {
      // load all sys packages after local schema and DDL services are ready
    } else if (OB_FAIL(start_local_services_())) {
      LOG_WARN("start local services failed", K(ret));
    } else if (FALSE_IT(need_bootstrap_ = false)) {
    } else if (debug_ && OB_FAIL(init_debug_database())) {
      LOG_WARN("init debug database failed", K(ret));
    } else if (OB_FAIL(check_ddl_allowed())) {
      LOG_WARN("fail to check ddl allowed", K(ret));
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
          OB_FAIL(ObSystemPackageLoadTask::wait_system_package_ready(ctx))) {
        LOG_WARN("failed to wait mysql sys package ready", KR(ret), K(ctx));
      } else {
        LOG_DBA_INFO_V2(OB_BOOTSTRAP_WAIT_SYS_PACKAGE_SUCCESS,
                        DBA_STEP_INC_INFO(bootstrap),
                        "bootstrap wait sys package success.");
      }
    }

    if (OB_SUCC(ret)) {
      char data_format_version[OB_SERVER_VERSION_LENGTH] = {'\0'};
      const uint64_t current_data_version = DATA_CURRENT_VERSION;
      share::ObBuildVersion build_version;
      if (OB_INVALID_INDEX == ObVersionParser::print_version_str(
          data_format_version, OB_SERVER_VERSION_LENGTH, current_data_version)) {
         ret = OB_INVALID_ARGUMENT;
         LOG_WARN("fail to print data format version", KR(ret), K(current_data_version));
      } else if (OB_FAIL(observer::ObService::get_build_version(build_version))) {
        LOG_WARN("fail to get build version", KR(ret));
      } else {
        MANAGEMENT_EVENT_ADD("BOOTSTRAP", "BOOTSTRAP_SUCCESS",
                               "data_format_version", data_format_version,
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
  return ret;
}

int ObLocalManagementService::check_config_result(const char *name, const char* value)
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

int ObLocalManagementService::check_ddl_allowed()
{
  int ret = OB_SUCCESS;
  if (!is_ddl_allowed()) {
    ret = OB_STATE_NOT_MATCH;
    LOG_WARN("local DDL service is not ready", K(ret));
  }
  return ret;
}

int ObLocalManagementService::update_baseline_schema_version()
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
                     get_runtime_refreshed_schema_version(baseline_schema_version))) {
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

int ObLocalManagementService::finish_bootstrap()
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

int ObLocalManagementService::modify_system_variable(const obcall::ObModifySysVarArg &arg)
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

int ObLocalManagementService::create_database(const ObCreateDatabaseArg &arg, UInt64 &db_id)
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

int ObLocalManagementService::alter_database(const ObAlterDatabaseArg &arg)
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

int ObLocalManagementService::parallel_ddl_pre_check_()
{
  int ret = OB_SUCCESS;
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", KR(ret));
  } else if (!schema_service_->is_runtime_schema_refreshed()) {
    // use this err to trigger DDL retry and release current thread.
    ret = OB_ERR_PARALLEL_DDL_CONFLICT;
    LOG_WARN("runtime schema not refreshed yet, need retry", KR(ret));
  }
  return ret;
}

int ObLocalManagementService::parallel_create_table(const ObCreateTableArg &arg, ObCreateTableRes &res)
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
    ObCreateViewHelper create_view_helper(schema_service_, arg, res, nullptr /*external trans*/,is_parallel);
    if (OB_FAIL(create_view_helper.init(ddl_service_))) {
      LOG_WARN("fail to init create view helper", KR(ret));
    } else if (OB_FAIL(create_view_helper.execute())) {
      LOG_WARN("fail to execute create view", KR(ret));
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "parallel create table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  return ret;
}

int ObLocalManagementService::create_table(const ObCreateTableArg &arg, ObCreateTableRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "create table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  return ret;
}

int ObLocalManagementService::fork_database(const obcall::ObForkDatabaseArg &arg, obcall::ObDDLRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "fork database",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "databases", database_names_buffer);
  LOG_INFO("finish fork database ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::maintain_obj_dependency_info(const obcall::ObDependencyObjDDLArg &arg)
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

int ObLocalManagementService::execute_ddl_task(const obcall::ObAlterTableArg &arg,
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
        if (OB_FAIL(ddl_service_.swap_orig_and_hidden_table_state(
            const_cast<obcall::ObAlterTableArg &>(arg)))) {
          LOG_WARN("failed to swap orig and hidden table state", K(ret));
        }
        break;
      }
      case share::CLEANUP_GARBAGE_TASK:
      {
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
      case share::SWITCH_VEC_INDEX_NAME_TASK: {
        if (OB_FAIL(ddl_service_.switch_index_name_and_status_for_vec_index_table(const_cast<ObAlterTableArg &>(arg)))) {
          LOG_WARN("make recovert restore task visible failed", K(ret), K(arg));
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

int ObLocalManagementService::create_table_like(const ObCreateTableLikeArg &arg)
{
  int ret = OB_SUCCESS;
  obcall::ObCreateTableRes res;
  if (OB_FAIL(parallel_create_table_like(arg,res))) {
    LOG_WARN("fail to parallel create table like", KR(ret));
  }
  return ret;
}

int ObLocalManagementService::parallel_create_table_like(const obcall::ObCreateTableLikeArg &arg, obcall::ObCreateTableRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", ddl_type,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", res.table_id_,
                        "schema_version", res.schema_version_,
                        K(cost));
  return ret;
}

int ObLocalManagementService::precheck_interval_part(const obcall::ObAlterTableArg &arg)
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
  } else if (OB_FAIL(schema_service_->get_runtime_schema_guard(schema_guard))) {
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

int ObLocalManagementService::update_ddl_task_active_time(const obcall::ObUpdateDDLTaskActiveTimeArg &arg)
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

int ObLocalManagementService::abort_redef_table(const obcall::ObAbortRedefTableArg &arg)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "abort redef table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_);
  LOG_INFO("finish abort redef table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::finish_redef_table(const obcall::ObFinishRedefTableArg &arg)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "finish redef table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_);
  LOG_INFO("finish abort redef table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::copy_table_dependents(const obcall::ObCopyTableDependentsArg &arg)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "copy table dependents",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", task_id);
  LOG_INFO("finish copy table dependents ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::start_redef_table(const obcall::ObStartRedefTableArg &arg, obcall::ObStartRedefTableRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "redef table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish redef table ddl", K(arg), K(ret), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::set_comment(const obcall::ObSetCommentArg &arg, obcall::ObParallelDDLRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "parallel set comment",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "schema_version", res.schema_version_);
  LOG_TRACE("finish set comment", KR(ret), K(arg), K(cost));
  return ret;
}

int ObLocalManagementService::alter_table(const obcall::ObAlterTableArg &arg, obcall::ObAlterTableRes &res)
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
    } else if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
      } else if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "alter table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish alter table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::exchange_partition(const obcall::ObExchangePartitionArg &arg, obcall::ObAlterTableRes &res)
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
  } else if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "alter table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish alter table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::create_aux_index(
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

int ObLocalManagementService::create_index(const ObCreateIndexArg &arg, obcall::ObAlterTableRes &res)
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
    if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "create index",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish create index ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::parallel_create_index(const ObCreateIndexArg &arg, obcall::ObAlterTableRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "parallel create index",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", table_id_buffer,
                        "schema_version", res.schema_version_);
  LOG_TRACE("finish parallel create index", KR(ret), K(arg), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::fork_table(const obcall::ObForkTableArg &arg, obcall::ObDDLRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "fork table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "tables", table_names_buffer);
  LOG_INFO("finish fork table ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::drop_table(const obcall::ObDropTableArg &arg, obcall::ObDDLRes &res)
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
  } else if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "drop table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "session_id", arg.session_id_,
                        "schema_version", res.schema_id_);
  LOG_INFO("finish drop table ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::parallel_drop_table(const ObDropTableArg &arg, ObDropTableRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "drop table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "session_id", arg.session_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish parallel drop table ddl", KR(ret), K(arg), K(cost), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::drop_database(const obcall::ObDropDatabaseArg &arg, ObDropDatabaseRes &drop_database_res)
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
    if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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

int ObLocalManagementService::drop_index_on_failed(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "drop index on failed",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish drop index on fail ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::drop_index(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "drop index",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish drop index ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::rebuild_vec_index(const obcall::ObRebuildIndexArg &arg, obcall::ObAlterTableRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "rebuild index",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", res.task_id_,
                        "table_id", arg.index_table_id_,
                        "schema_version", res.schema_version_);
  LOG_INFO("finish rebuild index ddl", K(ret), K(arg), K(res), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::drop_lob(const ObDropLobArg &arg)
{
  return ddl_service_.drop_lob(arg);
}

int ObLocalManagementService::force_drop_lonely_lob_aux_table(const ObForceDropLonelyLobAuxTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (OB_FAIL(ddl_service_.force_drop_lonely_lob_aux_table(arg)))  {
    LOG_WARN("drop fail", KR(ret), K(arg));
  }
  MANAGEMENT_EVENT_ADD("ddl scheduler", "force drop lonely lob table",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "data_table_id", arg.get_data_table_id(),
                        "lob_meta_table_id", arg.get_aux_lob_meta_table_id(),
                        "lob_piece_table_id", arg.get_aux_lob_piece_table_id());
  return ret;
}


int ObLocalManagementService::purge_index(const ObPurgeIndexArg &arg)
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

int ObLocalManagementService::rename_table(const obcall::ObRenameTableArg &arg)
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

int ObLocalManagementService::truncate_table(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res)
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
      if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "truncate table",
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
int ObLocalManagementService::truncate_table_v2(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res)
{
  int ret = OB_SUCCESS;
  // Parallel truncate generates schema versions in batch (gen_batch_new_schema_versions),
  // which requires an explicit batch schema-version context on the runtime ReqWorker.
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
    MANAGEMENT_EVENT_ADD("ddl scheduler", "truncate table new",
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
int ObLocalManagementService::restore_table_from_recyclebin(const ObRecyclebinRestoreTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.restore_table_from_recyclebin(arg))) {
    LOG_WARN("failed to flash back table", K(ret));
  }
  return ret;
}

int ObLocalManagementService::purge_table(const ObPurgeTableArg &arg)
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

int ObLocalManagementService::restore_database(const ObRecyclebinRestoreDatabaseArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid argument", K(arg), K(ret));
  } else if (OB_FAIL(ddl_service_.restore_database(arg))) {
    LOG_WARN("failed to flash back database", K(ret));
  }
  return ret;
}

int ObLocalManagementService::purge_database(const ObPurgeDatabaseArg &arg)
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

int ObLocalManagementService::purge_expire_recycle_objects(const ObPurgeRecycleBinArg &arg, Int64 &affected_rows)
{
  int ret = OB_SUCCESS;
  int64_t purged_objects = 0;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(ddl_service_.purge_expired_recycle_objects(arg, purged_objects))) {
    LOG_WARN("failed to purge expire recyclebin objects", K(ret), K(arg));
  } else {
    affected_rows = purged_objects;
  }
  return ret;
}

int ObLocalManagementService::optimize_table(const ObOptimizeTableArg &arg)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard schema_guard;
  LOG_INFO("receive optimize table request", K(arg));
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arguments", K(ret), K(arg));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("error unexpected, schema service must not be NULL", K(ret));
  } else {
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
        if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
          LOG_WARN("fail to get runtime schema guard", K(ret));
        } else if (OB_FAIL(schema_guard.get_table_schema(table_item.database_name_, table_item.table_name_, false/*is index*/, table_schema))) {
          LOG_WARN("fail to get table schema", K(ret));
        } else if (nullptr == table_schema) {
          // skip deleted table
        } else if (all_core_table_id == table_schema->get_table_id()) {
          // do nothing
        } else {
          if (OB_FAIL(sql.append_fmt("OPTIMIZE TABLE `%.*s`",
              table_item.table_name_.length(), table_item.table_name_.ptr()))) {
            LOG_WARN("fail to assign sql stmt", K(ret));
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

int ObLocalManagementService::calc_column_checksum_repsonse(const obcall::ObCalcColumnChecksumResponseArg &arg)
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

int ObLocalManagementService::root_minor_freeze(const ObMinorFreezeArg &arg)
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
  MANAGEMENT_EVENT_ADD("management_service", "root_minor_freeze", K(ret), K(arg));
  return ret;
}

int ObLocalManagementService::update_index_status(const obcall::ObUpdateIndexStatusArg &arg)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "update index status",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "index_table_id", arg.index_table_id_,
                        "data_table_id", arg.data_table_id_);
  return ret;
}

int ObLocalManagementService::parallel_update_index_status(const obcall::ObUpdateIndexStatusArg &arg, obcall::ObParallelDDLRes &res)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "parallel update index status",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "index_table_id", arg.index_table_id_,
                        "data_table_id", arg.data_table_id_);

  return ret;
}

int ObLocalManagementService::init_debug_database()
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

int ObLocalManagementService::start_local_services_()
{
  int ret = OB_SUCCESS;

  FLOG_INFO("start local services");

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

  if (FAILEDx(dbms_job::ObDBMSJobMaster::get_instance().start())) {
    FLOG_WARN("failed to start dbms job master", KR(ret));
  } else {
    FLOG_INFO("success to start dbms job master");
  }

  // Schema refresh trigger is now managed by the server module lifecycle.
  // It starts with the server runtime and follows the database role when refreshing schema.

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

  if (OB_SUCC(ret)) {
    ObGlobalStatProxy global_proxy(sql_proxy_);
    if (OB_FAIL(global_proxy.get_baseline_schema_version(baseline_schema_version_))) {
      LOG_WARN("fail to get baseline schema version", KR(ret));
    }
  }

  if (OB_SUCC(ret)) {
    local_services_ready_ = true;
    root_minor_freeze_.start();
    FLOG_INFO("root_minor_freeze_ started");
    int64_t now = ObTimeUtility::current_time();
    core_meta_table_version_ = now;
  }

  FLOG_INFO("finish starting local services", KR(ret));
  return ret;
}
int ObLocalManagementService::check_parallel_ddl_conflict(
    share::schema::ObSchemaGetterGuard &schema_guard,
    const obcall::ObDDLArg &arg)
{
  return ddl_service_.check_parallel_ddl_conflict(schema_guard, arg);
}

ERRSIM_POINT_DEF(ERROR_DEADLOCK_EVENT_CLEAR_INTERVAL);
void ObLocalManagementService::ObDeadlockEventClearTask::runTimerTask()
{
  int ret = OB_SUCCESS;
  if (!DEALOCK_EVENT_INSTANCE.is_inited()) {
    ret = OB_INNER_STAT_ERROR;
    LOG_WARN("deadlock event history operator not initialized", K(ret));
  } else if (OB_FAIL(DEALOCK_EVENT_INSTANCE.async_delete())) {
    LOG_WARN("failed to clear expired deadlock events", K(ret));
  }
}

int ObLocalManagementService::start_timer_tasks()
{
  int ret = OB_SUCCESS;
  bool task_exist = false;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  }

  if (OB_SUCCESS == ret) {
    task_exist = deadlock_event_clear_task_timer_.task_exist(deadlock_event_clear_task_);
  }
  if (OB_SUCCESS == ret && !task_exist) {
    const int64_t delay = ERROR_DEADLOCK_EVENT_CLEAR_INTERVAL ? 10 * 1000 * 1000 :
      2LL * 3600LL * 1000LL * 1000LL;
    if (OB_FAIL(deadlock_event_clear_task_timer_.schedule(deadlock_event_clear_task_, delay, true, true))) {
      LOG_WARN("start deadlock event clear task failed", K(delay), K(ret));
    } else {
      LOG_INFO("added deadlock event clear task", K(delay));
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

int ObLocalManagementService::stop_timer_tasks()
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    load_ddl_task_timer_.cancel_task(load_ddl_task_);
    deadlock_event_clear_task_timer_.cancel_task(deadlock_event_clear_task_);
    purge_recyclebin_task_timer_.cancel_task(purge_recyclebin_task_);
  }

  //stop other timer tasks here
  LOG_INFO("stop all timer tasks finish", K(ret));
  return ret;
}

//-----Functions for managing privileges------
int ObLocalManagementService::create_user(obcall::ObCreateUserArg &arg,
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

int ObLocalManagementService::drop_user(const ObDropUserArg &arg,
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

int ObLocalManagementService::rename_user(const obcall::ObRenameUserArg &arg,
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

int ObLocalManagementService::alter_role(const obcall::ObAlterRoleArg &arg)
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

int ObLocalManagementService::set_passwd(const obcall::ObSetPasswdArg &arg)
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

int ObLocalManagementService::grant(const ObGrantArg &arg)
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

int ObLocalManagementService::revoke_user(const ObRevokeUserArg &arg)
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

int ObLocalManagementService::lock_user(const ObLockUserArg &arg, ObSArray<int64_t> &failed_index)
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


int ObLocalManagementService::revoke_database(const ObRevokeDBArg &arg)
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

int ObLocalManagementService::revoke_table(const ObRevokeTableArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invalid arg", K(arg), K(ret));
  } else {
    if (OB_FAIL(ddl_service_.revoke_table_and_column_mysql(arg))) {
      LOG_WARN("revoke table and col failed", K(ret));
    }
  }
  return ret;
}

int ObLocalManagementService::revoke_routine(const ObRevokeRoutineArg &arg)
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
int ObLocalManagementService::create_outline(const ObCreateOutlineArg &arg)
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
    if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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

int ObLocalManagementService::alter_outline(const ObAlterOutlineArg &arg)
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

int ObLocalManagementService::drop_outline(const obcall::ObDropOutlineArg &arg)
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

int ObLocalManagementService::create_routine(const ObCreateRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_routine(arg, ddl_service_));
  return ret;
}

int ObLocalManagementService::alter_routine(const ObCreateRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_routine(arg, ddl_service_));
  return ret;
}

int ObLocalManagementService::drop_routine(const ObDropRoutineArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_routine(arg, ddl_service_));
  return ret;
}


int ObLocalManagementService::create_package(const obcall::ObCreatePackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_package(arg, ddl_service_));
  return ret;
}

int ObLocalManagementService::drop_package(const obcall::ObDropPackageArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_package(arg, ddl_service_));
  return ret;
}

int ObLocalManagementService::create_trigger(const obcall::ObCreateTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_trigger(arg, NULL, ddl_service_));
  return ret;
}

int ObLocalManagementService::create_trigger_with_res(const obcall::ObCreateTriggerArg &arg,
                                           obcall::ObCreateTriggerRes &res)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::create_trigger(arg, &res, ddl_service_));
  return ret;
}

int ObLocalManagementService::alter_trigger(const obcall::ObAlterTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::alter_trigger(arg, ddl_service_));
  return ret;
}

int ObLocalManagementService::drop_trigger(const obcall::ObDropTriggerArg &arg)
{
  int ret = OB_SUCCESS;
  OV (inited_, OB_NOT_INIT);
  OZ (ObPLDDLService::drop_trigger(arg, ddl_service_));
  return ret;
}

////////////////////////////////////////////////////////////////
// schema revise
////////////////////////////////////////////////////////////////
int ObLocalManagementService::schema_revise(const obcall::ObSchemaReviseArg &arg)
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
int ObLocalManagementService::init_sys_admin_ctx(ObSystemAdminCtx &ctx)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    ctx.sql_proxy_ = &sql_proxy_;
    ctx.schema_service_ = schema_service_;
    ctx.ddl_service_ = &ddl_service_;
    ctx.config_mgr_ = config_mgr_;
    ctx.local_management_service_ = this;
    ctx.inited_ = true;
  }
  return ret;
}

int ObLocalManagementService::admin_set_config(obcall::ObAdminSetConfigArg &arg)
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
    MANAGEMENT_EVENT_ADD_TRUNCATE("management_service", "admin_set_config", K(ret), "arg", arg.items_.at(i), "is_inner", arg.is_inner_);
  }
  return ret;
}

int ObLocalManagementService::apply_ds_action(const obcall::ObDebugSyncActionArg &arg)
{
  LOG_INFO("apply debug sync action", K(arg));
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

int ObLocalManagementService::refresh_schema(const bool load_frozen_status)
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
    // The local management service depends on the system schema during startup.
    if (OB_FAIL(schema_service_->refresh_and_add_schema())) {
      LOG_WARN("refresh schema failed", K(ret), K(load_frozen_status));
    } else if (OB_FAIL(schema_service_->get_runtime_schema_version(schema_version))) {
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

int ObLocalManagementService::request_time_zone_info(const ObRequestTZInfoArg &arg, ObRequestTZInfoResult &result)
{
  UNUSED(arg);
  int ret = OB_SUCCESS;

  ObTZMapWrap tz_map_wrap;
  ObTimeZoneInfoManager *tz_info_mgr = NULL;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else if (OB_FAIL(OTTZ_MGR.get_timezone(tz_map_wrap, tz_info_mgr))) {
    LOG_WARN("get time zone failed", K(ret));
  } else if (OB_ISNULL(tz_info_mgr)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("get_tz_mgr failed", K(ret), K(tz_info_mgr));
  } else if (OB_FAIL(tz_info_mgr->response_time_zone_info(result))) {
    LOG_WARN("fail to response tz_info", K(ret));
  } else {
    LOG_INFO("local management service responded with the latest time-zone info",
             "server", arg.obs_addr_, "last_version", result.last_version_);
  }
  return ret;
}

bool ObLocalManagementService::check_config(const ObConfigItem &item, const char *&err_info)
{
  bool bret = true;
  err_info = NULL;
  if (!inited_) {
    bret = false;
    LOG_WARN_RET(OB_NOT_INIT, "service not init");
  }
  return bret;
}

ObLocalManagementService::ObLoadDDLTask::ObLoadDDLTask(ObLocalManagementService &local_management_service)
  : local_management_service_(local_management_service)
{}

void ObLocalManagementService::ObLoadDDLTask::runTimerTask()
{
  int ret = ObSysDDLSchedulerUtil::recover_task();
  if (OB_FAIL(ret)) {
    LOG_WARN("recover ddl task failed", KR(ret));
  } else {
    local_management_service_.load_ddl_task_timer_.cancel_task(*this);
  }
}

int ObLocalManagementService::table_allow_ddl_operation(const obcall::ObAlterTableArg &arg)
{
  int ret = OB_SUCCESS;
  const ObTableSchema *schema = NULL;
  ObSchemaGetterGuard schema_guard;
  const AlterTableSchema &alter_table_schema = arg.alter_table_schema_;
  const ObString &origin_database_name = alter_table_schema.get_origin_database_name();
  const ObString &origin_table_name = alter_table_schema.get_origin_table_name();
  schema_guard.set_session_id(arg.session_id_);
  bool is_index = arg.alter_table_schema_.is_index_table();
  if (!arg.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
    LOG_WARN("invali argument", K(ret), K(arg));
  } else if (OB_FAIL(ddl_service_.get_runtime_schema_guard_with_version_in_inner_table(schema_guard))) {
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
  }
  return ret;
}

// Update optimizer statistic caches on this server.
int ObLocalManagementService::update_stat_cache(const obcall::ObUpdateStatCacheArg &arg)
{
  int ret = OB_SUCCESS;
  bool evict_plan_failed = false;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(ex_rpc::sync_call([&]{ return ObOptStatManager::get_instance().refresh_stat_cache(arg); }))) {
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

int ObLocalManagementService::check_weak_read_version_refresh_interval(int64_t refresh_interval, bool &valid)
{
  int ret = OB_SUCCESS;
  ObSchemaGetterGuard sys_schema_guard;
  valid = true;

  if (OB_ISNULL(GCTX.schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(sys_schema_guard))) {
    LOG_WARN("get sys schema guard failed", KR(ret));
  } else {
    ObSchemaGetterGuard schema_guard;
    const ObSimpleServerRuntimeSchema *runtime_schema = NULL;
    const ObSysVarSchema *var_schema = NULL;
    ObObj obj;
    int64_t session_max_stale_time = 0;
    if (OB_SUCC(ret) && valid) {
      if (OB_FAIL(sys_schema_guard.get_server_runtime_info(runtime_schema))) {
        LOG_WARN("fail to get runtime schema", KR(ret));
      } else if (OB_ISNULL(runtime_schema)) {
        ret = OB_SUCCESS;
        LOG_WARN("runtime schema is null, skip validation", KR(ret));
      } else if (!runtime_schema->is_normal()) {
        ret = OB_SUCCESS;
        LOG_WARN("runtime schema is not normal, skip validation", KR(ret));
      } else if (OB_FAIL(GCTX.schema_service_->get_runtime_schema_guard(schema_guard))) {
        LOG_WARN("get schema guard failed", KR(ret));
      } else if (OB_FAIL(schema_guard.get_system_variable(OB_SV_MAX_READ_STALE_TIME, var_schema))) {
        LOG_WARN("get runtime system variable failed", KR(ret));
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

int ObLocalManagementService::set_config_pre_hook(obcall::ObAdminSetConfigArg &arg)
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
    } else if (0 == STRCMP(item->name_.ptr(), INTERNAL_MEMSTORE_LIMIT_PERCENTAGE)) {
      ret = check_internal_memstore_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), _TX_DATA_MEMORY_LIMIT_PERCENTAGE)) {
      ret = check_tx_data_memory_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), _MDS_MEMORY_LIMIT_PERCENTAGE)) {
      ret = check_mds_memory_limit_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), FREEZE_TRIGGER_PERCENTAGE)) {
      ret = check_freeze_trigger_percentage_(*item);
    } else if (0 == STRCMP(item->name_.ptr(), WRITING_THROTTLEIUNG_TRIGGER_PERCENTAGE)) {
      ret = check_write_throttle_trigger_percentage(*item);
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
      valid = ObConfigLogDiskLimitThresholdIntChecker::check(*item);
      if (!valid) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_disk_utilization_limit_threshold should be greater than log_disk_throttling_percentage "
                      "when log_disk_throttling_percentage is not equal to 100");
        LOG_WARN("config invalid", "item", *item, K(ret));
      }
    } else if (0 == STRCMP(item->name_.ptr(), LOG_DISK_THROTTLING_PERCENTAGE)) {
      // check log_disk_throttling_percentage
      valid = ObConfigLogDiskThrottlingPercentageIntChecker::check(*item);
      if (!valid) {
        ret = OB_INVALID_ARGUMENT;
        LOG_USER_ERROR(OB_INVALID_ARGUMENT, "log_disk_throttling_percentage should be equal to 100 or smaller than log_disk_utilization_limit_threshold");
        LOG_WARN("config invalid", "item", *item, K(ret));
      }
    }
  }
  return ret;
}

#define CHECK_CONFIG_WITH_FUNC(FUNCTOR, LOG_INFO)                                          \
  do {                                                                                     \
    if (!FUNCTOR::check(item)) {                                                           \
      ret = OB_INVALID_ARGUMENT;                                                           \
      LOG_USER_ERROR(OB_INVALID_ARGUMENT, LOG_INFO);                                       \
      LOG_WARN("config invalid", "item", item, K(ret));                                   \
    }                                                                                      \
  } while (0)

int ObLocalManagementService::check_tx_share_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  // There is a prefix "Incorrect arguments to " before user log so the warn log looked kinds of wired
  const char *warn_log = "internal config _tx_share_memory_limit_percentage. "
                         "It should larger than or equal with any single module in it(Memstore, TxData, Mds, Vector)";
  CHECK_CONFIG_WITH_FUNC(ObConfigTxShareMemoryLimitChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_memstore_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "cluster config memstore_limit_percentage. "
                         "It should be less than or equal to the runtime _tx_share_memory_limit_percentage";
  if (OB_UNLIKELY(!inited_)) {
    ret = OB_NOT_INIT;
    LOG_WARN("not inited", KR(ret));
  } else if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema service is null", KR(ret));
  } else {
    CHECK_CONFIG_WITH_FUNC(ObConfigMemstoreLimitChecker, warn_log);
  }
  return ret;
}

int ObLocalManagementService::check_vector_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "ob_vector_limit_percentage. "
                         "It should be less than _tx_share_memory_limit_percentage";
  CHECK_CONFIG_WITH_FUNC(ObConfigVectorMemoryChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_internal_memstore_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "internal config _memstore_limit_percentage. "
    "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_CONFIG_WITH_FUNC(ObConfigMemstoreLimitChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_tx_data_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "internal config _tx_data_memory_limit_percentage. "
                         "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_CONFIG_WITH_FUNC(ObConfigTxDataLimitChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_mds_memory_limit_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "internal config _mds_memory_limit_percentage. "
                         "It should less than or equal with _tx_share_memory_limit_percentage";
  CHECK_CONFIG_WITH_FUNC(ObConfigMdsLimitChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_freeze_trigger_percentage_(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "runtime freeze_trigger_percentage "
                         "which should smaller than writing_throttling_trigger_percentage";
  CHECK_CONFIG_WITH_FUNC(ObConfigFreezeTriggerIntChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_write_throttle_trigger_percentage(obcall::ObAdminSetConfigItem &item)
{
  int ret = OB_SUCCESS;
  const char *warn_log = "runtime writing_throttling_trigger_percentage "
                         "which should greater than freeze_trigger_percentage";
  CHECK_CONFIG_WITH_FUNC(ObConfigWriteThrottleTriggerIntChecker, warn_log);
  return ret;
}

int ObLocalManagementService::check_data_disk_write_limit_(obcall::ObAdminSetConfigItem &item)
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

int ObLocalManagementService::check_data_disk_usage_limit_(obcall::ObAdminSetConfigItem &item)
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

#undef CHECK_CONFIG_WITH_FUNC

//ensure execute on DDL thread
int ObLocalManagementService::force_create_sys_table(const obcall::ObForceCreateSysTableArg &arg)
{
  return OB_NOT_SUPPORTED;
}

int ObLocalManagementService::clear_special_cluster_schema_status()
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



int ObLocalManagementService::handle_ddl_local_build_response(const obcall::ObDDLLocalBuildResponse &arg)
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
          arg.tablet_id_/*source tablet id*/,
          ObDDLTaskKey(arg.dest_schema_id_, arg.dest_schema_version_),
          arg.snapshot_version_, arg.execution_id_, arg.ret_code_, info))) {
    LOG_WARN("handle column checksum calc response failed", K(ret), K(arg));
  }
  MANAGEMENT_EVENT_ADD("ddl scheduler", "build ddl local build response",
                        "tid", 1UL,
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.task_id_,
                        "tablet_id", arg.tablet_id_,
                        "dag_result", arg.ret_code_,
                        arg.snapshot_version_);
  LOG_INFO("finish build ddl local build response ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::purge_recyclebin_objects(int64_t purge_each_time)
{
  int ret = OB_SUCCESS;
  // always passed
  int64_t expire_timeval = GCONF.recyclebin_object_expire_time;
  ObSchemaGetterGuard guard;
  if (OB_ISNULL(schema_service_)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("schema_serviece_ is null", KR(ret));
  } else if (OB_FAIL(schema_service_->get_runtime_schema_guard(guard))) {
    LOG_WARN("fail to get runtime schema guard", KR(ret));
  } else {
    const int64_t current_time = ObTimeUtility::current_time();
    const obcall::Int64 expire_time = current_time - expire_timeval;
    const int64_t SLEEP_INTERVAL_US = 100 * 1000;
    const int64_t PURGE_EACH_BATCH = 10;
    const int64_t purge_interval = GCONF._recyclebin_object_purge_frequency;
    int64_t purge_sum = purge_each_time;
    const ObSimpleServerRuntimeSchema *simple_runtime = NULL;
    if (purge_interval <= 0 || !sql_proxy_.is_active() || purge_sum <= 0) {
      // Purging is disabled or there is no work to do.
    } else if (OB_FAIL(guard.get_server_runtime_info(simple_runtime))) {
      LOG_WARN("fail to get simple runtime schema", KR(ret));
      ret = OB_SUCCESS; // periodic maintenance retries after the next schema refresh
    } else if (OB_ISNULL(simple_runtime)) {
      LOG_WARN_RET(OB_RUNTIME_SCHEMA_NOT_READY, "simple runtime schema does not exist");
    } else if (!simple_runtime->is_normal()) {
      LOG_TRACE("skip recycle-bin purge while the runtime is not normal");
    } else {
      obcall::Int64 affected_rows = 0;
      obcall::ObPurgeRecycleBinArg arg;
      ret = OB_SUCCESS;
      arg.expire_time_ = expire_time;
      arg.auto_purge_ = true;
      LOG_INFO("start purging runtime recycle-bin objects", K(arg), K(purge_sum));
      while (OB_SUCC(ret) && sql_proxy_.is_active() && purge_sum > 0) {
        int64_t cal_timeout = 0;
        int64_t start_time = ObTimeUtility::current_time();
        arg.purge_num_ = purge_sum > PURGE_EACH_BATCH ? PURGE_EACH_BATCH : purge_sum;
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
            if (OB_SUCC(ret) && sql_proxy_.is_active()) {
              ob_usleep(SLEEP_INTERVAL_US);
            }
            break;
          }
        }
        int64_t cost_time = ObTimeUtility::current_time() - start_time;
        LOG_INFO("purge recycle objects", KR(ret), K(cost_time), K(purge_sum),
                                          K(cal_timeout), K(expire_time), K(current_time), K(affected_rows));
        if (OB_SUCC(ret) && sql_proxy_.is_active()) {
          ob_usleep(SLEEP_INTERVAL_US);
        }
      }
    }
  }
  return ret;
}

int ObLocalManagementService::flush_opt_stat_monitoring_info(const obcall::ObFlushOptStatArg &arg)
{
  int ret = OB_SUCCESS;
  if (!inited_) {
    ret = OB_NOT_INIT;
    LOG_WARN("not init", K(ret));
  } else {
    if (OB_FAIL(ex_rpc::sync_call([&]() -> int { int ret = OB_SUCCESS; SERVER_MODULE_SCOPE { ObOptStatMonitorManager *m = share::g_mp->opt_stat_monitor_manager(); if (OB_ISNULL(m)) { ret = OB_ERR_UNEXPECTED; } else if (OB_FAIL(m->update_opt_stat_monitoring_info(arg))) {} } return ret; }))) {
      LOG_WARN("fail to update table statistic", K(ret));
    } else { /*do nothing*/}
  }
  return ret;
}


int ObLocalManagementService::cancel_ddl_task(const ObCancelDDLTaskArg &arg)
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
  MANAGEMENT_EVENT_ADD("ddl scheduler", "cancel ddl task",
                        "ret", ret,
                        "trace_id", *ObCurTraceId::get_trace_id(),
                        "task_id", arg.get_task_id());
  LOG_INFO("finish cancel ddl task ddl", K(ret), K(arg), "ddl_event_info", ObDDLEventInfo());
  return ret;
}

int ObLocalManagementService::set_config_after_bootstrap_()
{
  // Configuration is applied by the local management service before bootstrap completes.
  int ret = OB_SUCCESS;
  int64_t affected_rows = 0;
  ObSqlString sql;

  const char* configs[][2] = {
    {"enable_record_trace_log", "false"},
    {"_enable_dbms_job_package", "false"},
    {"_bloom_filter_ratio", "3"},
    {"_enable_mysql_compatible_dates", "true"}
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

int ObLocalManagementService::recompile_all_views_batch(const obcall::ObRecompileAllViewsBatchArg &arg)
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

int ObLocalManagementService::start_ddl_service_()
{
  // TODO@jingyu.cr: move this step into the observer startup procedure.
  int ret = OB_SUCCESS;
  if (!GCTX.is_standby_server()) {
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
      SERVER_MODULE_SCOPE {
        rootserver::ObDDLServiceLauncher* ddl_service_launcher = share::g_mp->ddl_service_launcher();
        if (OB_ISNULL(ddl_service_launcher)) {
          ret = OB_ERR_UNEXPECTED;
          FLOG_WARN("ddl service is null", KR(ret), KP(ddl_service_launcher));
        } else if (OB_FAIL(ddl_service_launcher->activate())) {
          FLOG_WARN("fail to start ddl service", KR(ret));
        } else {
          FLOG_INFO("success to start ddl service", KR(ret));
        }
      }
    }
  }
  return ret;
}

int ObLocalManagementService::create_ai_model(const obcall::ObCreateAiModelArg &arg)
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

int ObLocalManagementService::drop_ai_model(const obcall::ObDropAiModelArg &arg)
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



int ObLocalManagementService::revoke_object(const ObRevokeObjMysqlArg &arg)
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
