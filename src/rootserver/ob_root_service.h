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

#ifndef OCEANBASE_ROOTSERVER_OB_ROOT_SERVICE_H_
#define OCEANBASE_ROOTSERVER_OB_ROOT_SERVICE_H_

#include "lib/net/ob_addr.h"
#include "lib/task/ob_timer.h"
#include "lib/thread/ob_async_task_queue.h"

#include "share/object_storage/ob_object_storage_struct.h"
#include "share/ob_schema_version_info.h"
#include "share/ob_unit_replica_counter.h"
#include "share/ob_ls_id.h"
#include "share/ob_max_id_cache.h"

#include "rpc/ob_packet.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/ob_tenant_ddl_service.h"
#include "rootserver/ob_root_minor_freeze.h"
#include "rootserver/ob_system_admin_util.h"
#include "rootserver/ob_root_inspection.h"
#include "rootserver/ob_rs_event_history_table_operator.h"
#include "rootserver/ob_snapshot_info_manager.h"
#include "rootserver/ob_schema_history_recycler.h"
#include "rootserver/ob_catalog_ddl_service.h"
#include "rootserver/ob_ccl_ddl_service.h"
#include "rootserver/ob_location_ddl_service.h"
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
}

namespace share
{

namespace status
{
enum ObRootServiceStatus;
}
class ObAutoincrementService;
namespace schema
{
class ObMultiVersionSchemaService;
class ObTenantSchema;
class ObDatabaseSchema;
class ObTablegroupSchema;
class ObTableSchema;
class ObSchemaGetterGuard;
}
}

namespace obcall
{
}

namespace rootserver
{
class ObRsStatus
{
public:
  ObRsStatus() : rs_status_(share::status::INIT) {}
  virtual ~ObRsStatus() {}
  int set_rs_status(const share::status::ObRootServiceStatus status);
  share::status::ObRootServiceStatus get_rs_status() const;
  bool need_do_restart() const;
  bool can_start_service() const;
  bool is_start() const;
  bool is_stopping() const;
  bool is_full_service() const;
  bool in_service() const;
  bool is_need_stop() const;
  int revoke_rs();
  int try_set_stopping();
private:
  common::SpinRWLock lock_;
  share::status::ObRootServiceStatus rs_status_;
};
// Root Service Entry Class
class ObRootService
{
public:
  friend class TestRootServiceCreateTable_check_rs_capacity_Test;

  class ObRestartTask : public common::ObTimerTask
  {
  public:
    explicit ObRestartTask(ObRootService &root_service);
    virtual ~ObRestartTask();
    virtual void runTimerTask() override;
  private:
    ObRootService &root_service_;
  private:
    DISALLOW_COPY_AND_ASSIGN(ObRestartTask);
  };

  class ObLoadDDLTask : public common::ObTimerTask
  {
  public:
    explicit ObLoadDDLTask(ObRootService &root_service);
    virtual ~ObLoadDDLTask() = default;
    virtual void runTimerTask() override;
  private:
    ObRootService &root_service_;
  };


public:
  ObRootService();
  virtual ~ObRootService();
  void reset_fail_count();
  void update_fail_count(int ret);

  int init(common::ObServerConfig &config, common::ObConfigManager &config_mgr,
           common::ObAddr &self, common::ObMySQLProxy &sql_proxy,
           share::schema::ObMultiVersionSchemaService *schema_mgr_);
  inline bool is_inited() const { return inited_; }
  void destroy();

  // add virtual make the following functions mockable
  virtual int start_service();
  int revoke_rs();
  bool is_need_stop() const;
  virtual int stop_service();
  virtual int stop();
  virtual void wait();
  virtual bool in_service() const;
  bool need_do_restart() const;
  virtual bool is_full_service() const;
  virtual bool is_ddl_allowed() const { return is_full_service(); }
  bool can_start_service() const;
  bool is_stopping() const;
  bool is_start() const;
  share::status::ObRootServiceStatus get_status() const;
  bool in_debug() const { return debug_; }
  void set_debug() { debug_ = true; }
  int reload_config();
  virtual bool check_config(const ObConfigItem &item, const char *&err_info);
  // misc get functions
  share::schema::ObMultiVersionSchemaService &get_schema_service() { return *schema_service_; }
  common::ObMySQLProxy &get_sql_proxy() { return sql_proxy_; }
  common::ObServerConfig *get_server_config() { return config_; }
  int64_t get_core_meta_table_version() { return core_meta_table_version_; }
  ObSchemaHistoryRecycler &get_schema_history_recycler() { return schema_history_recycler_; }
  ObRootMinorFreeze &get_root_minor_freeze() { return root_minor_freeze_; }

  int execute_bootstrap();

  int check_config_result(const char *name, const char *value);
  int check_ddl_allowed();

  int merge_finish(const obcall::ObMergeFinishArg &arg);

  int broadcast_ds_action(const obcall::ObDebugSyncActionArg &arg);
  int check_dangling_replica_finish(const obcall::ObCheckDanglingReplicaFinishArg &arg);
  int get_tenant_schema_versions(const obcall::ObGetSchemaArg &arg,
                                 obcall::ObTenantSchemaVersions &tenant_schema_versions);

  // ddl related
  int modify_system_variable(const obcall::ObModifySysVarArg &arg);
  int add_system_variable(const obcall::ObAddSysVarArg &arg);
  int create_database(const obcall::ObCreateDatabaseArg &arg, obcall::UInt64 &db_id);
  int create_tablegroup(const obcall::ObCreateTablegroupArg &arg, obcall::UInt64 &tg_id);
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
  int create_hidden_table(const obcall::ObCreateHiddenTableArg &arg, obcall::ObCreateHiddenTableRes &res);
  int send_auto_split_tablet_task_request(const obcall::ObAutoSplitTabletBatchArg &arg, obcall::ObAutoSplitTabletBatchRes &res);
  int split_global_index_tablet(const obcall::ObAlterTableArg &arg);
  int execute_ddl_task(const obcall::ObAlterTableArg &arg, common::ObSArray<uint64_t> &obj_ids);
  int cancel_ddl_task(const obcall::ObCancelDDLTaskArg &arg);
  int alter_tablegroup(const obcall::ObAlterTablegroupArg &arg);
  int maintain_obj_dependency_info(const obcall::ObDependencyObjDDLArg &arg);
  int mview_complete_refresh(const obcall::ObMViewCompleteRefreshArg &arg, obcall::ObMViewCompleteRefreshRes &res);
  int rename_table(const obcall::ObRenameTableArg &arg);
  int fork_table(const obcall::ObForkTableArg &arg, obcall::ObDDLRes &res);
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
  int drop_tablegroup(const obcall::ObDropTablegroupArg &arg);
  int drop_index(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res);
  int create_mlog(const obcall::ObCreateMLogArg &arg, obcall::ObCreateMLogRes &res);
  int drop_lob(const obcall::ObDropLobArg &arg);
  int force_drop_lonely_lob_aux_table(const obcall::ObForceDropLonelyLobAuxTableArg &drop_table_arg);
  int rebuild_vec_index(const obcall::ObRebuildIndexArg &arg, obcall::ObAlterTableRes &res);

  // the interface only for gc splitted source tablet
  int clean_splitted_tablet(const obcall::ObCleanSplittedTabletArg &arg);

  //the interface only for switchover: execute skip check enable_ddl
  int purge_index(const obcall::ObPurgeIndexArg &arg);
  int create_table_like(const obcall::ObCreateTableLikeArg &arg);
  int parallel_create_table_like(const obcall::ObCreateTableLikeArg &arg, obcall::ObCreateTableRes &res);
  int root_minor_freeze(const obcall::ObRootMinorFreezeArg &arg);
  int update_index_status(const obcall::ObUpdateIndexStatusArg &arg);
  int update_mview_status(const obcall::ObUpdateMViewStatusArg &arg);
  int parallel_update_index_status(const obcall::ObUpdateIndexStatusArg &arg, obcall::ObParallelDDLRes &res);
  int purge_table(const obcall::ObPurgeTableArg &arg);
  int restore_table_from_recyclebin(const obcall::ObRecyclebinRestoreTableArg &arg);
  int purge_database(const obcall::ObPurgeDatabaseArg &arg);
  int restore_database(const obcall::ObRecyclebinRestoreDatabaseArg &arg);

  int drop_index_on_failed(const obcall::ObDropIndexArg &arg, obcall::ObDropIndexRes &res);

  //for inner table monitor, purge in fixed time
  int purge_expire_recycle_objects(const obcall::ObPurgeRecycleBinArg &arg, obcall::Int64 &affected_rows);
  int calc_column_checksum_repsonse(const obcall::ObCalcColumnChecksumResponseArg &arg);
  int build_ddl_single_replica_response(const obcall::ObDDLBuildSingleReplicaResponseArg &arg);
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
  int revoke_catalog(const obcall::ObRevokeCatalogArg &arg);
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

  //----Functions for managing UDF----
  int create_user_defined_function(const obcall::ObCreateUserDefinedFunctionArg &arg);
  int drop_user_defined_function(const obcall::ObDropUserDefinedFunctionArg &arg);
  //----End of functions for managing UDF----

  //----Functions for managing routines----
  int create_routine(const obcall::ObCreateRoutineArg &arg);
  int create_routine_with_res(const obcall::ObCreateRoutineArg &arg,
                              obcall::ObRoutineDDLRes &res);
  int drop_routine(const obcall::ObDropRoutineArg &arg);
  int alter_routine(const obcall::ObCreateRoutineArg &arg);
  int alter_routine_with_res(const obcall::ObCreateRoutineArg &arg,
                             obcall::ObRoutineDDLRes &res);
  //----End of functions for managing routines----

  //----Functions for managing routines----
  //----End of functions for managing routines----


  //----Functions for managing package----
  int create_package(const obcall::ObCreatePackageArg &arg);
  int create_package_with_res(const obcall::ObCreatePackageArg &arg,
                              obcall::ObRoutineDDLRes &res);
  int drop_package(const obcall::ObDropPackageArg &arg);
  //----End of functions for managing package----

  //----Functions for managing trigger----
  int create_trigger(const obcall::ObCreateTriggerArg &arg);
  int create_trigger_with_res(const obcall::ObCreateTriggerArg &arg,
                              obcall::ObCreateTriggerRes &res);
  int alter_trigger(const obcall::ObAlterTriggerArg &arg);
  int alter_trigger_with_res(const obcall::ObAlterTriggerArg &arg,
                             obcall::ObRoutineDDLRes &res);
  int drop_trigger(const obcall::ObDropTriggerArg &arg);
  //----End of functions for managing trigger----

  //----Functions for managing sequence----
  // create alter drop actions all in one, avoid noodle-like code
  int do_sequence_ddl(const obcall::ObSequenceDDLArg &arg);
  //----End of functions for managing sequence----

  //----Functions for managing context----
  // create alter drop actions all in one, avoid noodle-like code
  int do_context_ddl(const obcall::ObContextDDLArg &arg);
  //----End of functions for managing context----

  //----Functions for directory object----
  int create_directory(const obcall::ObCreateDirectoryArg &arg);
  int drop_directory(const obcall::ObDropDirectoryArg &arg);
  //----End of functions for directory object----

  //----Functions for managing catalog----
  int handle_catalog_ddl(const obcall::ObCatalogDDLArg &arg);
  //----End of functions for managing catalog----
  int create_ccl_rule_ddl(const obcall::ObCreateCCLRuleArg &arg);
  int drop_ccl_rule_ddl(const obcall::ObDropCCLRuleArg &arg);

  //----Functions for managing ai model----
  int create_ai_model(const obcall::ObCreateAiModelArg &arg);
  int drop_ai_model(const obcall::ObDropAiModelArg &arg);
  //----End of functions for managing ai model----

  //----Functions for location object----
  int create_location(const obcall::ObCreateLocationArg &arg);
  int drop_location(const obcall::ObDropLocationArg &arg);
  //----End of functions for location object----

  // system admin command (alter system ...)
  int admin_merge(const obcall::ObAdminMergeArg &arg);
  int admin_recovery(const obcall::ObAdminRecoveryArg &arg);
  int admin_clear_roottable(const obcall::ObAdminClearRoottableArg &arg);
  int admin_refresh_schema(const obcall::ObAdminRefreshSchemaArg &arg);
  int admin_set_config(obcall::ObAdminSetConfigArg &arg);
  int admin_refresh_memory_stat(const obcall::ObAdminRefreshMemStatArg &arg);
  int admin_refresh_io_calibration(const obcall::ObAdminRefreshIOCalibrationArg &arg);
  int admin_clear_merge_error(const obcall::ObAdminMergeArg &arg);
  int admin_upgrade_virtual_schema();
  int admin_flush_cache(const obcall::ObAdminFlushCacheArg &arg);
  int admin_set_tracepoint(const obcall::ObAdminSetTPArg &arg);
  int refresh_time_zone_info(const obcall::ObRefreshTimezoneArg &arg);
  int request_time_zone_info(const common::ObRequestTZInfoArg &arg, common::ObRequestTZInfoResult &result);
  // async tasks and callbacks
  int report_replica();
  int submit_max_availability_mode_task(const common::ObProtectionLevel level, const int64_t cluster_version);

  int submit_ddl_single_replica_build_task(share::ObAsyncTask &task);
  int check_weak_read_version_refresh_interval(int64_t refresh_interval, bool &valid);
  // may modify arg before taking effect
  int set_config_pre_hook(obcall::ObAdminSetConfigArg &arg);

  // @see ObRestartTask
  int after_restart();
  int do_after_full_service();
  int schedule_restart_timer_task(const int64_t delay);
  int reschedule_restart_timer_task_after_failure();
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
  int broadcast_schema(const obcall::ObBroadcastSchemaArg &arg);
  ObDDLService &get_ddl_service() { return ddl_service_; }
  ObMaxIdCacheMgr &get_max_id_cache_mgr() { return max_id_cache_mgr_; }
  int get_recycle_schema_versions(
      const obcall::ObGetRecycleSchemaVersionsArg &arg,
      obcall::ObGetRecycleSchemaVersionsResult &result);
  int standby_upgrade_virtual_schema(const obcall::ObDDLNopOpreatorArg &arg);
  int purge_recyclebin_objects(int64_t purge_each_time);
  int flush_opt_stat_monitoring_info(const obcall::ObFlushOptStatArg &arg);
  int recompile_all_views_batch(const obcall::ObRecompileAllViewsBatchArg &arg);
private:
  int check_parallel_ddl_conflict(
      share::schema::ObSchemaGetterGuard &schema_guard,
      const obcall::ObDDLArg &arg);
  int increase_rs_epoch_and_get_proposal_id_(
      int64_t &new_rs_epoch,
      int64_t &proposal_id_to_check);
  // create system table in mysql backend for debugging mode.
  int init_debug_database();
  int do_restart();
  int refresh_schema(const bool fast_recover);
  int init_sequence_id();
  int start_timer_tasks();
  int stop_timer_tasks();
  int init_sys_admin_ctx(ObSystemAdminCtx &ctx);
  int set_cluster_version();
  bool is_replica_count_reach_rs_limit(int64_t replica_count) { return replica_count > OB_MAX_CLUSTER_REPLICA_COUNT; }
  int generate_table_schema_in_tenant_space(
      const obcall::ObCreateTableArg &arg,
      share::schema::ObTableSchema &table_schema);
  int clear_special_cluster_schema_status();
  int check_tenant_gts_config(bool &tenant_gts_config_ok,
                              share::schema::ObSchemaGetterGuard &schema_guard);
  int check_database_config(bool &db_config_ok,
                            share::schema::ObSchemaGetterGuard &schema_guard);
  int check_table_config(bool &table_config_ok, bool &table_split_ok,
                         share::schema::ObSchemaGetterGuard &schema_guard,
                         const int64_t snapshot_schema_version);
  int check_tablegroup_config(bool &tablegroup_config_ok,
                              bool &tablegroup_split_ok,
                              share::schema::ObSchemaGetterGuard &schema_guard,
                              const int64_t snapshot_schema_version);
  int get_tenants_created_after_snapshot(const int64_t snapshot_schema_version,
                                         ObArray<uint64_t> &batch_ids);
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

  int precheck_interval_part(const obcall::ObAlterTableArg &arg);

  int parallel_ddl_pre_check_();
  int check_tx_share_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int check_memstore_limit_(obcall::ObAdminSetConfigItem &item);
  int check_tenant_memstore_limit_(obcall::ObAdminSetConfigItem &item);
  int check_tx_data_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int check_mds_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int check_freeze_trigger_percentage_(obcall::ObAdminSetConfigItem &item);
  int check_write_throttle_trigger_percentage(obcall::ObAdminSetConfigItem &item);
  int check_no_logging(obcall::ObAdminSetConfigItem &item);
  int check_data_disk_write_limit_(obcall::ObAdminSetConfigItem &item);
  int check_data_disk_usage_limit_(obcall::ObAdminSetConfigItem &item);
  int check_vector_memory_limit_(obcall::ObAdminSetConfigItem &item);
  int start_ddl_service_();
private:
  static const int64_t OB_MAX_CLUSTER_REPLICA_COUNT = 10000000;
  static const int64_t OB_ROOT_SERVICE_START_FAIL_COUNT_UPPER_LIMIT = 5;
  static const int64_t WAIT_RS_IN_SERVICE_TIMEOUT_US = 40 * 1000 * 1000; //40s
  bool inited_;
  volatile bool server_refreshed_; // server manager reload and force request heartbeat
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
  // tenant ddl related(create tenant, modify tenant, drop tenant, ...)
  ObTenantDDLService tenant_ddl_service_;

  // avoid concurrent run of do_restart and bootstrap
  common::ObLatch bootstrap_lock_;

  // timers for rootservice periodic tasks
  common::ObTimer restart_task_timer_;
  common::ObTimer load_ddl_task_timer_;
  common::ObTimer event_table_clear_task_timer_;
  common::ObTimer purge_recyclebin_task_timer_;

  // async timer tasks
  ObRestartTask restart_task_;  // repeat on failure and cancel on success
  ObLoadDDLTask load_ddl_task_; // repeat on failure and cancel on success
  share::ObEventTableClearTask event_table_clear_task_;  // repeat & no retry

  ObPurgeRecyclebinTask purge_recyclebin_task_;     // periodic schedule
  // for set_config
  ObLatch set_config_lock_;

  ObSnapshotInfoManager snapshot_manager_;
  int64_t core_meta_table_version_;
  int64_t baseline_schema_version_;

  int64_t start_service_time_;
  ObRsStatus rs_status_;

  int64_t fail_count_;
  ObSchemaHistoryRecycler schema_history_recycler_;
  //rebuild tablet

  // max id cache for object_id and tablet_id
  ObMaxIdCacheMgr max_id_cache_mgr_;

private:
  DISALLOW_COPY_AND_ASSIGN(ObRootService);
};
} // end namespace rootserver
} // end namespace oceanbase

#endif // OCEANBASE_ROOTSERVER_OB_ROOT_SERVICE_H_
