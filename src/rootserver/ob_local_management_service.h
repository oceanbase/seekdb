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

#ifndef OCEANBASE_ROOTSERVER_OB_LOCAL_MANAGEMENT_SERVICE_H_
#define OCEANBASE_ROOTSERVER_OB_LOCAL_MANAGEMENT_SERVICE_H_

#include "lib/net/ob_addr.h"
#include "lib/task/ob_timer.h"
#include "lib/thread/ob_async_task_queue.h"

#include "share/ob_schema_version_info.h"
#include "share/ob_max_id_cache.h"

#include "rpc/ob_packet.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/ob_runtime_ddl_service.h"
#include "rootserver/ob_root_minor_freeze.h"
#include "rootserver/ob_system_admin_util.h"
#include "rootserver/ob_root_inspection.h"
#include "share/ob_structured_event_logger.h"
#include "rootserver/ob_snapshot_info_manager.h"
#include "rootserver/ob_objpriv_mysql_ddl_service.h"

namespace oceanbase
{

namespace common
{
class ObPacket;
class ObServerConfig;
class ObConfigManager;
class ObMySQLProxy;
class ObRequestTZInfoResult;
class ObRequestTZInfoArg;
class ObString;
class ObTabletID;
}

namespace share
{

class ObAutoincrementService;
namespace schema
{
class ObMultiVersionSchemaService;
class ObServerRuntimeSchema;
class ObDatabaseSchema;
class ObTableSchema;
class ObSchemaGetterGuard;
}
}

namespace obcall
{
}

namespace rootserver
{
// Process-local management entry point for schema, DDL, jobs, freeze and recycle-bin work.
class ObLocalManagementService
{
public:
  friend class TestLocalManagementServiceCreateTable_check_rs_capacity_Test;

  class ObLoadDDLTask : public common::ObTimerTask
  {
  public:
    explicit ObLoadDDLTask(ObLocalManagementService &local_management_service);
    virtual ~ObLoadDDLTask() = default;
    virtual void runTimerTask() override;
  private:
    ObLocalManagementService &local_management_service_;
  };

  class ObDeadlockEventClearTask : public common::ObTimerTask
  {
  public:
    virtual ~ObDeadlockEventClearTask() = default;
    virtual void runTimerTask() override;
  };


public:
  ObLocalManagementService();
  virtual ~ObLocalManagementService();

  int init(common::ObServerConfig &config, common::ObConfigManager &config_mgr,
           common::ObAddr &self, common::ObMySQLProxy &sql_proxy,
           share::schema::ObMultiVersionSchemaService *schema_mgr_,
           const bool need_bootstrap);
  inline bool is_inited() const { return inited_; }
  void destroy();

  // add virtual make the following functions mockable
  virtual int start_service();
  int start_runtime_dependent_services();
  virtual int stop_service();
  virtual int stop();
  virtual void wait();
  virtual bool is_ddl_allowed() const { return local_services_ready_; }
  bool in_debug() const { return debug_; }
  void set_debug() { debug_ = true; }
  int reload_config();
  virtual bool check_config(const ObConfigItem &item, const char *&err_info);
  // misc get functions
  share::schema::ObMultiVersionSchemaService &get_schema_service() { return *schema_service_; }
  common::ObMySQLProxy &get_sql_proxy() { return sql_proxy_; }
  common::ObServerConfig *get_server_config() { return config_; }
  int64_t get_core_meta_table_version() { return core_meta_table_version_; }
  ObRootMinorFreeze &get_root_minor_freeze() { return root_minor_freeze_; }

  int execute_bootstrap();

  int check_config_result(const char *name, const char *value);
  int check_ddl_allowed();

  int merge_finish(const obcall::ObMergeFinishArg &arg);

  int apply_ds_action(const obcall::ObDebugSyncActionArg &arg);
  // ddl related
  int modify_system_variable(const obcall::ObModifySysVarArg &arg);
  int create_database(const obcall::ObCreateDatabaseArg &arg, obcall::UInt64 &db_id);
  int parallel_create_table(const obcall::ObCreateTableArg &arg, obcall::ObCreateTableRes &res);
  int create_table(const obcall::ObCreateTableArg &arg, obcall::ObCreateTableRes &res);
  int alter_database(const obcall::ObAlterDatabaseArg &arg);
  int set_comment(const obcall::ObSetCommentArg &arg, obcall::ObParallelDDLRes &res);
  int alter_table(const obcall::ObAlterTableArg &arg, obcall::ObAlterTableRes &res);
  int start_redef_table(const obcall::ObStartRedefTableArg &arg, obcall::ObStartRedefTableRes &res);
  int copy_table_dependents(const obcall::ObCopyTableDependentsArg &arg);
  int finish_redef_table(const obcall::ObFinishRedefTableArg &arg);
  int abort_redef_table(const obcall::ObAbortRedefTableArg &arg);
  int update_ddl_task_active_time(const obcall::ObUpdateDDLTaskActiveTimeArg &arg);
  int execute_ddl_task(const obcall::ObAlterTableArg &arg, common::ObSArray<uint64_t> &obj_ids);
  int cancel_ddl_task(const obcall::ObCancelDDLTaskArg &arg);
  int maintain_obj_dependency_info(const obcall::ObDependencyObjDDLArg &arg);
  int rename_table(const obcall::ObRenameTableArg &arg);
  virtual int fork_table(const obcall::ObForkTableArg &arg, obcall::ObDDLRes &res);
  int fork_database(const obcall::ObForkDatabaseArg &arg, obcall::ObDDLRes &res);
  int truncate_table(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res);
  int truncate_table_v2(const obcall::ObTruncateTableArg &arg, obcall::ObDDLRes &res);
  int exchange_partition(const obcall::ObExchangePartitionArg &arg, obcall::ObAlterTableRes &res);
  int create_aux_index(
      const obcall::ObCreateAuxIndexArg &arg,
      obcall::ObCreateAuxIndexRes &result);
  int create_index(const obcall::ObCreateIndexArg &arg, obcall::ObAlterTableRes &res);
  int parallel_create_index(const obcall::ObCreateIndexArg &arg, obcall::ObAlterTableRes &res);
  int drop_table(const obcall::ObDropTableArg &arg, obcall::ObDDLRes &res);
  int parallel_drop_table(const obcall::ObDropTableArg &arg, obcall::ObDropTableRes &res);
  int drop_database(const obcall::ObDropDatabaseArg &arg, obcall::ObDropDatabaseRes &drop_database_res);
  int drop_index(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res);
  int drop_lob(const obcall::ObDropLobArg &arg);
  int force_drop_lonely_lob_aux_table(const obcall::ObForceDropLonelyLobAuxTableArg &drop_table_arg);
  int rebuild_vec_index(const obcall::ObRebuildIndexArg &arg, obcall::ObAlterTableRes &res);


  //the interface only for switchover: execute skip check enable_ddl
  int purge_index(const obcall::ObPurgeIndexArg &arg);
  int create_table_like(const obcall::ObCreateTableLikeArg &arg);
  int parallel_create_table_like(const obcall::ObCreateTableLikeArg &arg, obcall::ObCreateTableRes &res);
  int root_minor_freeze(const obcall::ObMinorFreezeArg &arg);
  int update_index_status(const obcall::ObUpdateIndexStatusArg &arg);
  int parallel_update_index_status(const obcall::ObUpdateIndexStatusArg &arg, obcall::ObParallelDDLRes &res);
  int purge_table(const obcall::ObPurgeTableArg &arg);
  int restore_table_from_recyclebin(const obcall::ObRecyclebinRestoreTableArg &arg);
  int purge_database(const obcall::ObPurgeDatabaseArg &arg);
  int restore_database(const obcall::ObRecyclebinRestoreDatabaseArg &arg);

  int drop_index_on_failed(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res);

  //for inner table monitor, purge in fixed time
  int purge_expire_recycle_objects(const obcall::ObPurgeRecycleBinArg &arg, obcall::Int64 &affected_rows);
  int calc_column_checksum_repsonse(const obcall::ObCalcColumnChecksumResponseArg &arg);
  int handle_ddl_local_build_response(const obcall::ObDDLLocalBuildResponse &arg);
  int optimize_table(const obcall::ObOptimizeTableArg &arg);

  //----Functions for managing privileges----
  int create_user(obcall::ObCreateUserArg &arg,
                  common::ObSArray<int64_t> &failed_index);
  int drop_user(const obcall::ObDropUserArg &arg,
                common::ObSArray<int64_t> &failed_index);
  int rename_user(const obcall::ObRenameUserArg &arg,
                  common::ObSArray<int64_t> &failed_index);
  int set_passwd(const obcall::ObSetPasswdArg &arg);
  int grant(const obcall::ObGrantArg &arg);
  int revoke_user(const obcall::ObRevokeUserArg &arg);
  int lock_user(const obcall::ObLockUserArg &arg, common::ObSArray<int64_t> &failed_index);
  int revoke_database(const obcall::ObRevokeDBArg &arg);
  int revoke_table(const obcall::ObRevokeTableArg &arg);
  int revoke_routine(const obcall::ObRevokeRoutineArg &arg);
  int alter_role(const obcall::ObAlterRoleArg &arg);
  int revoke_object(const obcall::ObRevokeObjMysqlArg &arg);
  //----End of functions for managing privileges----

  //----Functions for managing outlines----
  int create_outline(const obcall::ObCreateOutlineArg &arg);
  int alter_outline(const obcall::ObAlterOutlineArg &arg);
  int drop_outline(const obcall::ObDropOutlineArg &arg);
  //----End of functions for managing outlines----

  //----Functions for managing schema revise----
  int schema_revise(const obcall::ObSchemaReviseArg &arg);
  //----End of functions for managing schema revise----

  //----Functions for managing routines----
  int create_routine(const obcall::ObCreateRoutineArg &arg);
  int drop_routine(const obcall::ObDropRoutineArg &arg);
  int alter_routine(const obcall::ObCreateRoutineArg &arg);
  //----End of functions for managing routines----

  //----Functions for managing routines----
  //----End of functions for managing routines----


  //----Functions for managing package----
  int create_package(const obcall::ObCreatePackageArg &arg);
  int drop_package(const obcall::ObDropPackageArg &arg);
  //----End of functions for managing package----

  //----Functions for managing trigger----
  int create_trigger(const obcall::ObCreateTriggerArg &arg);
  int create_trigger_with_res(const obcall::ObCreateTriggerArg &arg,
                              obcall::ObCreateTriggerRes &res);
  int alter_trigger(const obcall::ObAlterTriggerArg &arg);
  int drop_trigger(const obcall::ObDropTriggerArg &arg);
  //----End of functions for managing trigger----

  //----Functions for managing ai model----
  int create_ai_model(const obcall::ObCreateAiModelArg &arg);
  int drop_ai_model(const obcall::ObDropAiModelArg &arg);
  //----End of functions for managing ai model----

  // system admin command (alter system ...)
  int admin_set_config(obcall::ObAdminSetConfigArg &arg);
  int request_time_zone_info(const common::ObRequestTZInfoArg &arg, common::ObRequestTZInfoResult &result);
  // async tasks and callbacks
  int submit_ddl_local_build_task(share::ObAsyncTask &task);
  int check_weak_read_version_refresh_interval(int64_t refresh_interval, bool &valid);
  // may modify arg before taking effect
  int set_config_pre_hook(obcall::ObAdminSetConfigArg &arg);

  int schedule_temporary_offline_timer_task();
  // @see ObRefreshServerTask
  int schedule_refresh_server_timer_task(const int64_t delay);
  int schedule_primary_cluster_inspection_task();
  int schedule_recyclebin_task(int64_t delay);
  //update statistic cache
  int update_stat_cache(const obcall::ObUpdateStatCacheArg &arg);

  int schedule_load_ddl_task();
  // ob_admin command, must be called in ddl thread
  int force_create_sys_table(const obcall::ObForceCreateSysTableArg &arg);
  ObDDLService &get_ddl_service() { return ddl_service_; }
  ObMaxIdCacheMgr &get_max_id_cache_mgr() { return max_id_cache_mgr_; }
  int purge_recyclebin_objects(int64_t purge_each_time);
  int flush_opt_stat_monitoring_info(const obcall::ObFlushOptStatArg &arg);
  int recompile_all_views_batch(const obcall::ObRecompileAllViewsBatchArg &arg);
private:
  int check_parallel_ddl_conflict(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const obcall::ObDDLArg &arg);
  // create system table in mysql backend for debugging mode.
  int init_debug_database();
  int start_local_services_();
  int refresh_schema(const bool fast_recover);
  int init_sequence_id();
  int start_timer_tasks();
  int stop_timer_tasks();
  int init_sys_admin_ctx(ObSystemAdminCtx &ctx);
  int clear_special_cluster_schema_status();
  int check_database_config(bool &db_config_ok,
                            share::schema::ObSchemaGetterGuard &schema_guard);
  int query_ddl_table_after_major_freeze(int &row_cnt, int64_t &schema_version_cursor,
                                         ObArray<uint64_t> &batch_ids);
  bool continue_check(const int ret);

  int table_allow_ddl_operation(const obcall::ObAlterTableArg &arg);
  int get_table_schema(const common::ObString &database_name,
                       const common::ObString &table_name,
                       const bool is_index,
                       const int64_t session_id,
                       const share::schema::ObTableSchema *&table_schema);
  int update_baseline_schema_version();
  int finish_bootstrap();
  int set_config_after_bootstrap_();


  int parallel_ddl_pre_check_();
  int check_tx_share_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int check_memstore_limit_(obcall::ObAdminSetConfigItem &item);
  int check_internal_memstore_limit_(obcall::ObAdminSetConfigItem &item);
  int check_tx_data_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int check_mds_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int check_freeze_trigger_percentage_(obcall::ObAdminSetConfigItem &item);
  int check_write_throttle_trigger_percentage(obcall::ObAdminSetConfigItem &item);
  int check_data_disk_write_limit_(obcall::ObAdminSetConfigItem &item);
  int check_data_disk_usage_limit_(obcall::ObAdminSetConfigItem &item);
  int check_vector_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int start_ddl_service_();
private:
  bool inited_;
  bool need_bootstrap_;
  bool local_services_ready_;
  // use mysql server backend for debug.
  bool debug_;

  common::ObAddr self_addr_;
  common::ObServerConfig *config_;
  common::ObConfigManager *config_mgr_;

  common::ObMySQLProxy sql_proxy_;
  share::schema::ObMultiVersionSchemaService *schema_service_;

  // minor freeze
  ObRootMinorFreeze root_minor_freeze_;

  // ddl related
  ObDDLService ddl_service_;
  // Server runtime DDL service.
  ObRuntimeDDLService runtime_ddl_service_;

  // avoid starting local services concurrently with bootstrap
  common::ObLatch bootstrap_lock_;

  // timers for local periodic tasks
  common::ObTimer load_ddl_task_timer_;
  common::ObTimer deadlock_event_clear_task_timer_;
  common::ObTimer purge_recyclebin_task_timer_;

  // async timer tasks
  ObLoadDDLTask load_ddl_task_; // repeat on failure and cancel on success
  ObDeadlockEventClearTask deadlock_event_clear_task_;  // repeat & no retry

  ObPurgeRecyclebinTask purge_recyclebin_task_;     // periodic schedule
  // for set_config
  ObLatch set_config_lock_;

  ObSnapshotInfoManager snapshot_manager_;
  int64_t core_meta_table_version_;
  int64_t baseline_schema_version_;

  // max id cache for object_id and tablet_id
  ObMaxIdCacheMgr max_id_cache_mgr_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObLocalManagementService);
};
} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_LOCAL_MANAGEMENT_SERVICE_H_
