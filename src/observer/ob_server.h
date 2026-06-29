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

#ifndef _OCEABASE_OBSERVER_OB_SERVER_H_
#define _OCEABASE_OBSERVER_OB_SERVER_H_

#ifndef _WIN32
#include <sys/statvfs.h>
#endif
#include "lib/net/ob_net_util.h"
#include "lib/random/ob_mysql_random.h"
#include "lib/container/ob_iarray.h"

#include "share/stat/ob_opt_stat_service.h"
#include "share/config/ob_config_manager.h"

#include "share/tablet/ob_tablet_table_operator.h"
#include "share/location_cache/ob_location_service.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "share/ob_kv_storage.h"
#ifdef _WIN32
#include "diagnose/lua/ob_lua_handler_win.h"
#else
#include "diagnose/lua/ob_lua_handler.h"
#endif

#include "sql/ob_sql.h"
#include "sql/engine/cmd/ob_load_data_rpc.h"
#include "sql/das/ob_data_access_service.h"
#include "sql/session/ob_user_resource_mgr.h"
#include "sql/executor/ob_executor_rpc_impl.h"

#include "pl/ob_pl.h"


#include "rootserver/ob_root_service.h"

#include "observer/mysql/ob_diag.h"

#include "share/resource_manager/ob_cgroup_ctrl.h"
#include "observer/omt/ob_multi_tenant.h"
#include "observer/omt/ob_worker_processor.h"
#include "share/rc/ob_module_provider.h"   // ObIModuleProvider / g_mp (ObServer owns modules)

#include "observer/virtual_table/ob_virtual_data_access_service.h"

#include "observer/ob_signal_handle.h"
#include "observer/ob_tenant_duty_task.h"
#include "observer/ob_inner_sql_connection_pool.h"
#include "observer/ob_resource_inner_sql_connection_pool.h"
#include "observer/ob_srv_network_frame.h"
#include "observer/ob_service.h"
#include "observer/ob_server_reload_config.h"
#include "observer/ob_root_service_monitor.h"
#include "observer/ob_inner_sql_transmit_struct.h"
#include "observer/ob_startup_accel_task_handler.h"
#include "storage/ddl/ob_ddl_heart_beat_task.h"

#include "storage/ob_disk_usage_reporter.h"
#include "logservice/ob_server_log_block_mgr.h"



namespace oceanbase
{
namespace omt
{
class ObTenantTimezoneMgr;
}
namespace observer
{

class ObServerOptions;

// This the class definition of ObAddr which responds the server
// itself. It's designed as a singleton in program. This class is
// structure aggregated but not logical processing. Please don't put
// cumbersome logical processes into it. Here's a typical usage:
//
//   ObServer server;
//   server.init(...);
//   server.set_xxx(...);
//   server.start(); // blocked only program is coming to stop
//   server.destory();
//
class ObServer : public share::ObIModuleProvider
{
public:
  static const int64_t DEFAULT_ETHERNET_SPEED = 10000 / 8 * 1024 * 1024; // change from default 1250m/s  10000Mbit to 1250MBps 10000Mbit
  static const int64_t DISK_USAGE_REPORT_INTERVAL = 1000L * 1000L * 60L; // 1min
  static const uint64_t DEFAULT_CPU_FREQUENCY = 2500 * 1000; // 2500 * 1000 khz
  static ObServer &get_instance();

public:
  int init(const ObServerOptions &opts, const ObPLogWriterCfg &log_cfg);
  void destroy();

  // Start OceanBase server, this function is blocked after invoking
  // until the server itself stops it.
  int start(bool embed_mode);
  int wait();
  void prepare_stop();
  bool is_prepare_stopped();
  void set_stop();
  bool is_stopped();

public:
  //Refer to ObPurgeCompletedMonitorInfoTask
  class ObCTASCleanUpTask: public common::ObTimerTask
  {
  public:
    ObCTASCleanUpTask();
    virtual ~ObCTASCleanUpTask() {}
    int init(ObServer *observer, int tg_id);
    virtual void runTimerTask() override;
  private:
    const static int64_t CLEANUP_INTERVAL = 60L * 1000L * 1000L;//60s
    ObServer *obs_;
    bool is_inited_;
  };

  class ObRefreshTimeTask: public common::ObTimerTask
  {
  public:
    ObRefreshTimeTask();
    virtual ~ObRefreshTimeTask() {}
    int init(ObServer *observer, int tg_id);
    virtual void runTimerTask() override;
  private:
    const static int64_t REFRESH_INTERVAL = 60LL * 60 * 1000 * 1000;//1hr
    ObServer *obs_;
    bool is_inited_;
  };

  class ObRefreshCpuFreqTimeTask: public common::ObTimerTask
  {
  public:
    ObRefreshCpuFreqTimeTask();
    virtual ~ObRefreshCpuFreqTimeTask() {}
    int init(ObServer *observer, int tg_id);
    virtual void runTimerTask() override;
  private:
    const static int64_t REFRESH_INTERVAL = 10 * 1000L * 1000L;//10s
    ObServer *obs_;
    bool is_inited_;
  };

  class ObRefreshTime {
  public:
    explicit ObRefreshTime(ObServer *obs): obs_(obs){}
    virtual ~ObRefreshTime(){}
    bool operator()(sql::ObSQLSessionMgr::Key key, sql::ObSQLSessionInfo *sess_info);
  private:
    ObServer *obs_;
    DISALLOW_COPY_AND_ASSIGN(ObRefreshTime);
  };

  class ObCTASCleanUp
  {
  public:
    explicit ObCTASCleanUp(ObServer *obs, bool drop_flag): obs_(obs), session_id_(0),
              schema_version_(0), drop_flag_(drop_flag), cleanup_rule_type_(0) {}
    virtual ~ObCTASCleanUp(){}
    bool operator()(sql::ObSQLSessionMgr::Key key, sql::ObSQLSessionInfo *sess_info);
    inline void set_session_id(uint64_t  session_id) {session_id_ = session_id; }
    inline uint64_t get_session_id() { return session_id_; }
    inline void set_schema_version(int64_t  schema_version) {schema_version_ = schema_version; }
    inline int64_t get_schema_version() { return schema_version_; }
    inline void set_drop_flag(bool drop_flag) { drop_flag_ = drop_flag; }
    inline bool get_drop_flag() { return drop_flag_; }
    inline void set_cleanup_type(int type) { cleanup_rule_type_ = type; }
    inline int get_cleanup_type() { return cleanup_rule_type_; }
    enum CLEANUP_RULE
    {
      CTAS_RULE,          //Query cleanup rules for table creation
      TEMP_TAB_RULE,      //Cleanup rules for temporary tables (direct connection)
      TEMP_TAB_PROXY_RULE //Temporary table cleanup rules (PROXY)
    };
  private:
    ObServer *obs_;
    uint64_t session_id_;      //Determine whether the sesion_id of the table schema needs to be dropped
    int64_t schema_version_;  //Determine whether the version number of the table schema that needs to be dropped
    bool drop_flag_;           //Do you need a drop table
    int cleanup_rule_type_;    //According to the temporary table rules or query table building rules
    DISALLOW_COPY_AND_ASSIGN(ObCTASCleanUp);
  };
  share::schema::ObMultiVersionSchemaService &get_schema_service() { return schema_service_; }
  ObInOutBandwidthThrottle &get_bandwidth_throttle() { return bandwidth_throttle_; }
  uint64_t get_cpu_frequency_khz() { return cpu_frequency_; }
  int64_t get_network_speed() const { return ethernet_speed_; }
  const common::ObAddr &get_self() const { return self_addr_; }
  const ObGlobalContext &get_gctx() const { return gctx_; }
  ObGlobalContext &get_gctx() { return gctx_; }
  ObSrvNetworkFrame& get_net_frame() { return net_frame_; }

  int reload_config();
  bool is_log_dir_empty() const { return is_log_dir_empty_; }
  sql::ObSQLSessionMgr &get_sql_session_mgr() { return session_mgr_; }
  rootserver::ObRootService &get_root_service() { return root_service_; }
  common::ObMySQLProxy &get_mysql_proxy() { return sql_proxy_; }
  int64_t get_start_time() const { return start_time_; }
  sql::ObConnectResourceMgr& get_conn_res_mgr() { return conn_res_mgr_; }
  share::ObLocationService &get_location_service() { return location_service_; }
private:
  int stop();
  int wait_no_client();

private:
  ObServer();
  ~ObServer();

  int init_config(const ObServerOptions &opts);
  int init_opts_config(const ObServerOptions &opts, const char *optstr); // init configs from command line
  int init_create_func();
  int init_data_dir_and_redo_dir(const ObServerOptions &opts);
  int init_self_addr();
  int init_config_module(const char *optstr);
  int init_tz_info_mgr();
  int init_pre_setting();
  int init_network();
  int init_interrupt();
  int init_plugin();
  int init_multi_tenant();
  int init_sql_proxy();
  int init_io();
  int init_schema();
  int init_inner_table_monitor();
  int init_autoincrement_service();
  int init_tablet_autoincrement_service();
  int init_global_kvcache();
  int init_global_session_info();
  int init_ob_service(bool need_bootstrap);
  int init_root_service();
  int init_sql();
  int init_sql_runner();
  int init_sequence();
  int init_pl();
  int init_global_context();
  int parse_role_and_restore_source(const ObServerOptions &opts);
  int init_version();
  int init_ts_mgr();
  int init_px_target_mgr();
  int init_storage();
  int init_tx_data_cache();
  int init_log_kv_cache();
  int init_gc_partition_adapter();
  int init_loaddata_global_stat();
  int init_bandwidth_throttle();
  int init_table_lock_rpc_client();
  int start_log_mgr();
  int stop_log_mgr();
  int refresh_cpu_frequency();
  int clean_up_invalid_tables();
  int clean_up_invalid_tables_by_tenant();
  int init_ctas_clean_up_task(); //Regularly clean up the residuals related to querying and building tables and temporary tables
  int init_redef_heart_beat_task();
  int init_ddl_heart_beat_task_container();
  int refresh_temp_table_sess_active_time();
  int init_refresh_active_time_task(); //Regularly update the sess_active_time of the temporary table created by the proxy connection sess
  int init_refresh_cpu_frequency();
  int set_running_mode();
  void check_user_tenant_schema_refreshed(const common::ObIArray<uint64_t> &batch_ids, const int64_t expire_time);
  void check_log_replay_over(const common::ObIArray<uint64_t> &batch_ids, const int64_t expire_time);
  int try_update_hidden_sys();
  int check_if_multi_tenant_synced();
  int check_if_schema_ready();
  int check_if_timezone_usable();
  int parse_mode();
  void deinit_plugin();

public:
  static int get_network_speed_from_config_file(int64_t &network_speed);
  ObInnerSQLConnectionPool &get_inner_sql_conn_pool() { return sql_conn_pool_; }
public:
  volatile bool need_ctas_cleanup_; //true: ObCTASCleanUpTask should traverse all table schemas to find the one need be dropped
private:
  ObSignalHandle signal_handle_;
  // gctx, aka global context, stores pointers to objects or services
  // which should share with all, or in part, of classes using in
  // observer. The whole pointers stored in gctx wouldn't be changed
  // once they're assigned. So other class can only get a reference of
  // constant gctx.
  ObGlobalContext &gctx_;

  // self addr
  common::ObAddr self_addr_;
  bool prepare_stop_;
  bool stop_;
  volatile bool has_stopped_;
  bool has_destroy_;
  bool embedded_ = false;
  int clients_fd_ = -1;
#ifdef _WIN32
  HANDLE clients_h_ = INVALID_HANDLE_VALUE;
#endif
  // The network framework in OceanBase is all defined at ObServerNetworkFrame.
  ObSrvNetworkFrame net_frame_;


  ObInnerSQLConnectionPool sql_conn_pool_;
  ObInnerSQLConnectionPool ddl_conn_pool_;
  ObResourceInnerSQLConnectionPool res_inner_conn_pool_;


  // The proxy by which local OceanBase server has ability to
  // communicate with other server.
  obcall::ObStorageRpcProxy storage_rpc_proxy_;
  common::ObMySQLProxy sql_proxy_;
  common::ObMySQLProxy ddl_sql_proxy_;
  common::ObCommonSqlProxy ddl_oracle_sql_proxy_;
  sql::ObExecutorRpcImpl executor_rpc_;

  // The OceanBase configuration relating to.
  common::ObServerConfig &config_;
  ObServerReloadConfig reload_config_;
  common::ObConfigManager config_mgr_;
  omt::ObTenantTimezoneMgr &tenant_timezone_mgr_;

  // The Oceanbase schema relating to.
  share::schema::ObMultiVersionSchemaService &schema_service_;

  // The SQL Engine
  sql::ObSql sql_engine_;

  // The PL Engine
  pl::ObPL pl_engine_;

  // Shared SQLite connection pool for meta database (config and tablet_meta tables)
  share::ObSQLiteConnectionPool meta_db_pool_;

  // KV storage for simple information (cluster role, switchover status, etc.)
  share::ObKVStorage kv_storage_;

  // The Oceanbase partition table relating to
  share::ObTabletTableOperator tablet_operator_;
  share::ObLocationService location_service_;

  // storage related
  common::ObInOutBandwidthThrottle bandwidth_throttle_;
  int64_t sys_bkgd_net_percentage_;
  int64_t ethernet_speed_;
  uint64_t cpu_frequency_;

  // sql session_mgr
  sql::ObSQLSessionMgr session_mgr_;

  // All operations and processing logic relating to root server is
  // defined in root_service_.
  rootserver::ObRootService root_service_;
  // Start && stop root service.

  ObRootServiceMonitor root_service_monitor_;

  // All operations and processing logic relating to ob server is
  // defined in oceanbase_service_.
  ObService ob_service_;

  omt::ObMultiTenant multi_tenant_;

  // virtual table related
  ObVirtualDataAccessService vt_data_service_;
  // Weakly Consistent Read Service
  // blacklist service
  // Tenant isolation resource management
  share::ObCgroupCtrl cgroup_ctrl_;

  //observer start time
  int64_t start_time_;
  int64_t warm_up_start_time_;
  obmysql::ObDiag diag_;
  common::ObMysqlRandom scramble_rand_;
  ObTenantDutyTask duty_task_;
  ObTenantSqlMemoryTimerTask sql_mem_task_;
  ObCTASCleanUpTask ctas_clean_up_task_;     // repeat & no retry
  ObRedefTableHeartBeatTask redef_table_heart_beat_task_;
  ObRefreshTimeTask refresh_active_time_task_; // repeat & no retry
  ObRefreshCpuFreqTimeTask refresh_cpu_frequency_task_;
  blocksstable::ObStorageEnv storage_env_;
  share::ObSchemaStatusProxy schema_status_proxy_;

  bool is_log_dir_empty_;
  sql::ObConnectResourceMgr conn_res_mgr_;
  diagnose::ObUnixDomainListener unix_domain_listener_;
  ObDiskUsageReportTask disk_usage_report_task_;

  logservice::ObServerLogBlockMgr log_block_mgr_;

  // This handler is used to process tasks during startup. it can speed up the startup process.
  // If you have tasks that need to be processed in parallel, you can use this handler,
  // but please note that this handler will be destroyed after observer startup.
  ObStartupAccelTaskHandler startup_accel_handler_;

public:
  // ===== ObIModuleProvider impl over ObServer-owned modules =====
  // Module accessors, order == MTL_MEMBERS.
  omt::ObSharedTimer * shared_timer() override { return mods_shared_timer_; }
  oceanbase::sql::ObTenantSQLSessionMgr * tenant_sql_session_mgr() override { return mods_tenant_sql_session_mgr_; }
  storage::ObTenantMetaMemMgr * tenant_meta_mem_mgr() override { return mods_tenant_meta_mem_mgr_; }
  storage::ObTenantFTPluginMgr * tenant_ft_plugin_mgr() override { return mods_tenant_ft_plugin_mgr_; }
  ObPartTransCtxObjPool * part_trans_ctx_obj_pool() override { return mods_part_trans_ctx_obj_pool_; }
  ObTableScanIteratorObjPool * table_scan_iterator_obj_pool() override { return mods_table_scan_iterator_obj_pool_; }
  common::ObTenantIOManager * tenant_io_manager() override { return mods_tenant_io_manager_; }
  storage::mds::ObTenantMdsService * tenant_mds_service() override { return mods_tenant_mds_service_; }
  blocksstable::ObSharedMacroBlockMgr * shared_macro_block_mgr() override { return mods_shared_macro_block_mgr_; }
  share::ObSharedMemAllocMgr * shared_mem_alloc_mgr() override { return mods_shared_mem_alloc_mgr_; }
  transaction::ObTransService * trans_service() override { return mods_trans_service_; }
  logservice::ObLogService * log_service() override { return mods_log_service_; }
  storage::ObLSService * ls_service() override { return mods_ls_service_; }
  storage::ObTenantStorageMetaService * tenant_storage_meta_service() override { return mods_tenant_storage_meta_service_; }
  tmp_file::ObTenantTmpFileManager * tenant_tmp_file_manager() override { return mods_tenant_tmp_file_manager_; }
  compaction::ObTenantCompactionProgressMgr * tenant_compaction_progress_mgr() override { return mods_tenant_compaction_progress_mgr_; }
  compaction::ObServerCompactionEventHistory * server_compaction_event_history() override { return mods_server_compaction_event_history_; }
  storage::ObTenantTabletStatMgr * tenant_tablet_stat_mgr() override { return mods_tenant_tablet_stat_mgr_; }
  memtable::ObLockWaitMgr * lock_wait_mgr() override { return mods_lock_wait_mgr_; }
  transaction::tablelock::ObTableLockService * table_lock_service() override { return mods_table_lock_service_; }
  rootserver::ObPrimaryMajorFreezeService * primary_major_freeze_service() override { return mods_primary_major_freeze_service_; }
  rootserver::ObRestoreMajorFreezeService * restore_major_freeze_service() override { return mods_restore_major_freeze_service_; }
  observer::ObTenantMetaChecker * tenant_meta_checker() override { return mods_tenant_meta_checker_; }
  observer::ObTabletTableUpdater * tablet_table_updater() override { return mods_tablet_table_updater_; }
  storage::ObStorageHAHandlerService * storage_ha_handler_service() override { return mods_storage_ha_handler_service_; }
  storage::ObTenantSSTableMergeInfoMgr * tenant_ss_table_merge_info_mgr() override { return mods_tenant_ss_table_merge_info_mgr_; }
  share::ObDagWarningHistoryManager * dag_warning_history_manager() override { return mods_dag_warning_history_manager_; }
  compaction::ObScheduleSuspectInfoMgr * schedule_suspect_info_mgr() override { return mods_schedule_suspect_info_mgr_; }
  compaction::ObCompactionSuggestionMgr * compaction_suggestion_mgr() override { return mods_compaction_suggestion_mgr_; }
  compaction::ObDiagnoseTabletMgr * diagnose_tablet_mgr() override { return mods_diagnose_tablet_mgr_; }
  storage::ObLobManager * lob_manager() override { return mods_lob_manager_; }
  share::ObGlobalAutoIncService * global_auto_inc_service() override { return mods_global_auto_inc_service_; }
  share::detector::ObDeadLockDetectorMgr * dead_lock_detector_mgr() override { return mods_dead_lock_detector_mgr_; }
  transaction::ObTimestampService * timestamp_service() override { return mods_timestamp_service_; }
  transaction::ObTimestampAccess * timestamp_access() override { return mods_timestamp_access_; }
  transaction::ObTransIDService * trans_id_service() override { return mods_trans_id_service_; }
  transaction::ObUniqueIDService * unique_id_service() override { return mods_unique_id_service_; }
  sql::ObPlanBaselineMgr * plan_baseline_mgr() override { return mods_plan_baseline_mgr_; }
  sql::ObPsCache * ps_cache() override { return mods_ps_cache_; }
  sql::ObPlanCache * plan_cache() override { return mods_plan_cache_; }
  sql::dtl::ObTenantDfc * tenant_dfc() override { return mods_tenant_dfc_; }
  omt::ObPxPools * px_pools() override { return mods_px_pools_; }
  lib::Worker::CompatMode compat_mode() override { return mods_compat_mode_; }
  sql::ObTenantSqlMemoryManager * tenant_sql_memory_manager() override { return mods_tenant_sql_memory_manager_; }
  sql::dtl::ObDTLIntermResultManager * dtl_interm_result_manager() override { return mods_dtl_interm_result_manager_; }
  sql::ObPlanMonitorNodeList * plan_monitor_node_list() override { return mods_plan_monitor_node_list_; }
  sql::ObDataAccessService * data_access_service() override { return mods_data_access_service_; }
  sql::ObDASIDService * dasid_service() override { return mods_dasid_service_; }
  share::schema::ObTenantSchemaService * tenant_schema_service() override { return mods_tenant_schema_service_; }
  storage::ObTenantFreezer * tenant_freezer() override { return mods_tenant_freezer_; }
  storage::checkpoint::ObCheckPointService * check_point_service() override { return mods_check_point_service_; }
  storage::checkpoint::ObTabletGCService * tablet_gc_service() override { return mods_tablet_gc_service_; }
  compaction::ObTenantTabletScheduler * tenant_tablet_scheduler() override { return mods_tenant_tablet_scheduler_; }
  compaction::ObTenantMediumChecker * tenant_medium_checker() override { return mods_tenant_medium_checker_; }
  storage::ObTenantCompactionMemPool * tenant_compaction_mem_pool() override { return mods_tenant_compaction_mem_pool_; }
  storage::ObDDLMergeBucketLock * ddl_merge_bucket_lock() override { return mods_ddl_merge_bucket_lock_; }
  storage::ObTenantDirectLoadMgr * tenant_direct_load_mgr() override { return mods_tenant_direct_load_mgr_; }
  share::ObTenantDagScheduler * tenant_dag_scheduler() override { return mods_tenant_dag_scheduler_; }
  storage::ObStorageHAService * storage_ha_service() override { return mods_storage_ha_service_; }
  storage::ObTenantFreezeInfoMgr * tenant_freeze_info_mgr() override { return mods_tenant_freeze_info_mgr_; }
  transaction::ObTxLoopWorker * tx_loop_worker() override { return mods_tx_loop_worker_; }
  storage::ObAccessService * access_service() override { return mods_access_service_; }
  datadict::ObDataDictService * data_dict_service() override { return mods_data_dict_service_; }
  observer::ObTableLoadService * table_load_service() override { return mods_table_load_service_; }
  observer::ObTableLoadResourceService * table_load_resource_service() override { return mods_table_load_resource_service_; }
  concurrency_control::ObMultiVersionGarbageCollector * multi_version_garbage_collector() override { return mods_multi_version_garbage_collector_; }
  sql::ObFLTSpanMgr * flt_span_mgr() override { return mods_flt_span_mgr_; }
  storage::ObTenantCGReadInfoMgr * tenant_cg_read_info_mgr() override { return mods_tenant_cg_read_info_mgr_; }
  ObTestModule * test_module() override { return mods_test_module_; }
  storage::ObEmptyReadBucket * empty_read_bucket() override { return mods_empty_read_bucket_; }
  rootserver::ObDBMSSchedService * dbms_sched_service() override { return mods_dbms_sched_service_; }
  oceanbase::common::ObOptStatMonitorManager * opt_stat_monitor_manager() override { return mods_opt_stat_monitor_manager_; }
  omt::ObTenantSrs * tenant_srs() override { return mods_tenant_srs_; }
  table::ObHTableLockMgr * h_table_lock_mgr() override { return mods_h_table_lock_mgr_; }
  table::ObTTLService * ttl_service() override { return mods_ttl_service_; }
  table::ObTableObjectPoolMgr * table_object_pool_mgr() override { return mods_table_object_pool_mgr_; }
  share::ObIndexUsageInfoMgr * index_usage_info_mgr() override { return mods_index_usage_info_mgr_; }
  storage::ObTabletMemtableMgrPool * tablet_memtable_mgr_pool() override { return mods_tablet_memtable_mgr_pool_; }
  rootserver::ObMViewMaintenanceService * m_view_maintenance_service() override { return mods_m_view_maintenance_service_; }
  share::ObResourceLimitCalculator * resource_limit_calculator() override { return mods_resource_limit_calculator_; }
  storage::checkpoint::ObCheckpointDiagnoseMgr * checkpoint_diagnose_mgr() override { return mods_checkpoint_diagnose_mgr_; }
  storage::ObGlobalIteratorPool * global_iterator_pool() override { return mods_global_iterator_pool_; }
  common::ObRbMemMgr * rb_mem_mgr() override { return mods_rb_mem_mgr_; }
  share::ObPluginVectorIndexService * plugin_vector_index_service() override { return mods_plugin_vector_index_service_; }
  share::ObAutoSplitTaskCache * auto_split_task_cache() override { return mods_auto_split_task_cache_; }
  observer::ObTenantQueryRespTimeCollector * tenant_query_resp_time_collector() override { return mods_tenant_query_resp_time_collector_; }
  table::ObTableGroupCommitMgr * table_group_commit_mgr() override { return mods_table_group_commit_mgr_; }
  observer::ObTableQueryASyncMgr * table_query_a_sync_mgr() override { return mods_table_query_a_sync_mgr_; }
  table::ObTableClientInfoMgr * table_client_info_mgr() override { return mods_table_client_info_mgr_; }
  table::ObHTableRowkeyMgr * h_table_rowkey_mgr() override { return mods_h_table_rowkey_mgr_; }
  rootserver::ObDDLServiceLauncher * ddl_service_launcher() override { return mods_ddl_service_launcher_; }
  rootserver::ObSysTenantLoadSysPackageService * sys_tenant_load_sys_package_service() override { return mods_sys_tenant_load_sys_package_service_; }
  rootserver::ObDDLScheduler * ddl_scheduler() override { return mods_ddl_scheduler_; }
  sql::ObSQLCCLRuleManager * sqlccl_rule_manager() override { return mods_sqlccl_rule_manager_; }
  omt::ObTenantAiService * tenant_ai_service() override { return mods_tenant_ai_service_; }
  share::ObChangeStreamMgr * change_stream_mgr() override { return mods_change_stream_mgr_; }
  // Explicit module lifecycle (ObServer owns modules; defined in ob_multi_tenant.cpp).
  int obs_construct_modules();
  int obs_init_modules();
  int obs_start_modules();
  void obs_stop_modules();
  void obs_wait_modules();
  void obs_destroy_modules();

private:
  // ===== module instances (ObServer is the sole owner; created by
  // obs_construct_modules() at boot, accessed via the ObIModuleProvider facade) =====
  omt::ObSharedTimer * mods_shared_timer_ = nullptr;
  oceanbase::sql::ObTenantSQLSessionMgr * mods_tenant_sql_session_mgr_ = nullptr;
  storage::ObTenantMetaMemMgr * mods_tenant_meta_mem_mgr_ = nullptr;
  storage::ObTenantFTPluginMgr * mods_tenant_ft_plugin_mgr_ = nullptr;
  ObPartTransCtxObjPool * mods_part_trans_ctx_obj_pool_ = nullptr;
  ObTableScanIteratorObjPool * mods_table_scan_iterator_obj_pool_ = nullptr;
  common::ObTenantIOManager * mods_tenant_io_manager_ = nullptr;
  storage::mds::ObTenantMdsService * mods_tenant_mds_service_ = nullptr;
  blocksstable::ObSharedMacroBlockMgr * mods_shared_macro_block_mgr_ = nullptr;
  share::ObSharedMemAllocMgr * mods_shared_mem_alloc_mgr_ = nullptr;
  transaction::ObTransService * mods_trans_service_ = nullptr;
  logservice::ObLogService * mods_log_service_ = nullptr;
  storage::ObLSService * mods_ls_service_ = nullptr;
  storage::ObTenantStorageMetaService * mods_tenant_storage_meta_service_ = nullptr;
  tmp_file::ObTenantTmpFileManager * mods_tenant_tmp_file_manager_ = nullptr;
  compaction::ObTenantCompactionProgressMgr * mods_tenant_compaction_progress_mgr_ = nullptr;
  compaction::ObServerCompactionEventHistory * mods_server_compaction_event_history_ = nullptr;
  storage::ObTenantTabletStatMgr * mods_tenant_tablet_stat_mgr_ = nullptr;
  memtable::ObLockWaitMgr * mods_lock_wait_mgr_ = nullptr;
  transaction::tablelock::ObTableLockService * mods_table_lock_service_ = nullptr;
  rootserver::ObPrimaryMajorFreezeService * mods_primary_major_freeze_service_ = nullptr;
  rootserver::ObRestoreMajorFreezeService * mods_restore_major_freeze_service_ = nullptr;
  observer::ObTenantMetaChecker * mods_tenant_meta_checker_ = nullptr;
  observer::ObTabletTableUpdater * mods_tablet_table_updater_ = nullptr;
  storage::ObStorageHAHandlerService * mods_storage_ha_handler_service_ = nullptr;
  storage::ObTenantSSTableMergeInfoMgr * mods_tenant_ss_table_merge_info_mgr_ = nullptr;
  share::ObDagWarningHistoryManager * mods_dag_warning_history_manager_ = nullptr;
  compaction::ObScheduleSuspectInfoMgr * mods_schedule_suspect_info_mgr_ = nullptr;
  compaction::ObCompactionSuggestionMgr * mods_compaction_suggestion_mgr_ = nullptr;
  compaction::ObDiagnoseTabletMgr * mods_diagnose_tablet_mgr_ = nullptr;
  storage::ObLobManager * mods_lob_manager_ = nullptr;
  share::ObGlobalAutoIncService * mods_global_auto_inc_service_ = nullptr;
  share::detector::ObDeadLockDetectorMgr * mods_dead_lock_detector_mgr_ = nullptr;
  transaction::ObTimestampService * mods_timestamp_service_ = nullptr;
  transaction::ObTimestampAccess * mods_timestamp_access_ = nullptr;
  transaction::ObTransIDService * mods_trans_id_service_ = nullptr;
  transaction::ObUniqueIDService * mods_unique_id_service_ = nullptr;
  sql::ObPlanBaselineMgr * mods_plan_baseline_mgr_ = nullptr;
  sql::ObPsCache * mods_ps_cache_ = nullptr;
  sql::ObPlanCache * mods_plan_cache_ = nullptr;
  sql::dtl::ObTenantDfc * mods_tenant_dfc_ = nullptr;
  omt::ObPxPools * mods_px_pools_ = nullptr;
  lib::Worker::CompatMode mods_compat_mode_;
  sql::ObTenantSqlMemoryManager * mods_tenant_sql_memory_manager_ = nullptr;
  sql::dtl::ObDTLIntermResultManager * mods_dtl_interm_result_manager_ = nullptr;
  sql::ObPlanMonitorNodeList * mods_plan_monitor_node_list_ = nullptr;
  sql::ObDataAccessService * mods_data_access_service_ = nullptr;
  sql::ObDASIDService * mods_dasid_service_ = nullptr;
  share::schema::ObTenantSchemaService * mods_tenant_schema_service_ = nullptr;
  storage::ObTenantFreezer * mods_tenant_freezer_ = nullptr;
  storage::checkpoint::ObCheckPointService * mods_check_point_service_ = nullptr;
  storage::checkpoint::ObTabletGCService * mods_tablet_gc_service_ = nullptr;
  compaction::ObTenantTabletScheduler * mods_tenant_tablet_scheduler_ = nullptr;
  compaction::ObTenantMediumChecker * mods_tenant_medium_checker_ = nullptr;
  storage::ObTenantCompactionMemPool * mods_tenant_compaction_mem_pool_ = nullptr;
  storage::ObDDLMergeBucketLock * mods_ddl_merge_bucket_lock_ = nullptr;
  storage::ObTenantDirectLoadMgr * mods_tenant_direct_load_mgr_ = nullptr;
  share::ObTenantDagScheduler * mods_tenant_dag_scheduler_ = nullptr;
  storage::ObStorageHAService * mods_storage_ha_service_ = nullptr;
  storage::ObTenantFreezeInfoMgr * mods_tenant_freeze_info_mgr_ = nullptr;
  transaction::ObTxLoopWorker * mods_tx_loop_worker_ = nullptr;
  storage::ObAccessService * mods_access_service_ = nullptr;
  datadict::ObDataDictService * mods_data_dict_service_ = nullptr;
  observer::ObTableLoadService * mods_table_load_service_ = nullptr;
  observer::ObTableLoadResourceService * mods_table_load_resource_service_ = nullptr;
  concurrency_control::ObMultiVersionGarbageCollector * mods_multi_version_garbage_collector_ = nullptr;
  sql::ObFLTSpanMgr * mods_flt_span_mgr_ = nullptr;
  storage::ObTenantCGReadInfoMgr * mods_tenant_cg_read_info_mgr_ = nullptr;
  ObTestModule * mods_test_module_ = nullptr;
  storage::ObEmptyReadBucket * mods_empty_read_bucket_ = nullptr;
  rootserver::ObDBMSSchedService * mods_dbms_sched_service_ = nullptr;
  oceanbase::common::ObOptStatMonitorManager * mods_opt_stat_monitor_manager_ = nullptr;
  omt::ObTenantSrs * mods_tenant_srs_ = nullptr;
  table::ObHTableLockMgr * mods_h_table_lock_mgr_ = nullptr;
  table::ObTTLService * mods_ttl_service_ = nullptr;
  table::ObTableObjectPoolMgr * mods_table_object_pool_mgr_ = nullptr;
  share::ObIndexUsageInfoMgr * mods_index_usage_info_mgr_ = nullptr;
  storage::ObTabletMemtableMgrPool * mods_tablet_memtable_mgr_pool_ = nullptr;
  rootserver::ObMViewMaintenanceService * mods_m_view_maintenance_service_ = nullptr;
  share::ObResourceLimitCalculator * mods_resource_limit_calculator_ = nullptr;
  storage::checkpoint::ObCheckpointDiagnoseMgr * mods_checkpoint_diagnose_mgr_ = nullptr;
  storage::ObGlobalIteratorPool * mods_global_iterator_pool_ = nullptr;
  common::ObRbMemMgr * mods_rb_mem_mgr_ = nullptr;
  share::ObPluginVectorIndexService * mods_plugin_vector_index_service_ = nullptr;
  share::ObAutoSplitTaskCache * mods_auto_split_task_cache_ = nullptr;
  observer::ObTenantQueryRespTimeCollector * mods_tenant_query_resp_time_collector_ = nullptr;
  table::ObTableGroupCommitMgr * mods_table_group_commit_mgr_ = nullptr;
  observer::ObTableQueryASyncMgr * mods_table_query_a_sync_mgr_ = nullptr;
  table::ObTableClientInfoMgr * mods_table_client_info_mgr_ = nullptr;
  table::ObHTableRowkeyMgr * mods_h_table_rowkey_mgr_ = nullptr;
  rootserver::ObDDLServiceLauncher * mods_ddl_service_launcher_ = nullptr;
  rootserver::ObSysTenantLoadSysPackageService * mods_sys_tenant_load_sys_package_service_ = nullptr;
  rootserver::ObDDLScheduler * mods_ddl_scheduler_ = nullptr;
  sql::ObSQLCCLRuleManager * mods_sqlccl_rule_manager_ = nullptr;
  omt::ObTenantAiService * mods_tenant_ai_service_ = nullptr;
  share::ObChangeStreamMgr * mods_change_stream_mgr_ = nullptr;
}; // end of class ObServer

inline ObServer &ObServer::get_instance()
{
  static ObServer THE_ONE;
  return THE_ONE;
}

} // end of namespace observer
} // end of namespace oceanbase

#define OBSERVER (::oceanbase::observer::ObServer::get_instance())
#define MYADDR (OBSERVER.get_self())

#endif /* _OCEABASE_OBSERVER_OB_SERVER_H_ */
