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
#include <memory>
#include "seekdb/plugin/execution_spi.h"
#include "seekdb/plugin/extension_spi.h"
#include "lib/net/ob_net_util.h"
#include "lib/task/ob_timer.h"
#include "lib/random/ob_mysql_random.h"
#include "lib/container/ob_iarray.h"

#include "sql/optimizer/stat/ob_opt_stat_service.h"
#include "share/config/ob_config_manager.h"

#include "share/tablet/ob_tablet_table_operator.h"
#include "share/storage/ob_sqlite_connection_pool.h"
#include "sql/ob_sql.h"
#include "sql/das/ob_data_access_service.h"
#include "sql/engine/px/ob_ddl_slice_store.h"
#include "sql/session/ob_user_resource_mgr.h"
#include "data_plane/ob_i_memory_pressure_service.h"

#include "pl/ob_pl.h"


#include "rootserver/ob_local_management_service.h"
#include "rootserver/ob_debug_sync_broadcaster_adapter.h"
#include "rootserver/freeze/ob_major_freeze_coordinator_adapter.h"

#include "observer/mysql/ob_diag.h"

#include "observer/omt/ob_server_runtime_controller.h"
#include "observer/omt/ob_worker_processor.h"
#include "data_plane/memtable/ob_lock_wait_service.h"
#include "query/ai/ob_ai_endpoint_resolver.h"
#include "query/ai/ob_ai_endpoint_admin.h"
#include "query/change_stream/ob_change_stream_service.h"
#include "query/ddl/ob_ddl_execution_guard.h"
#include "query/plan_cache/ob_plan_cache_access_service.h"
#include "query/runtime/ob_query_runtime_environment.h"
#include "query/virtual_table/ob_virtual_table_factory_provider.h"
#include "share/rc/ob_server_runtime.h"
#include "share/rc/ob_module_provider.h"
#include "share/schema/ob_schema_publish_signal.h"

#include "observer/virtual_table/ob_virtual_data_access_service.h"

#include "observer/ob_signal_handle.h"
#include "observer/ob_internal_table_refresh_adapter.h"
#include "observer/ob_ls_runtime_adapter.h"
#include "observer/ob_server_duty_task.h"
#include "observer/ob_srv_network_frame.h"
#include "observer/ob_service.h"
#include "observer/ob_server_reload_config.h"
#include "storage/slog_ckpt/ob_startup_accel_task_handler.h"
#include "storage/ddl/ob_ddl_heart_beat_task.h"

#include "storage/ob_disk_usage_reporter.h"
#include "storage/tx_storage/ob_access_service.h"
#include "logservice/ob_server_log_block_mgr.h"



namespace oceanbase
{
namespace common
{
class ObIOService;
class ObOptStatMonitorManager;
class ObRbMemMgr;
class ObILobReadService;
class ObITabletScan;
}
namespace blocksstable
{
class ObSharedMacroBlockMgr;
}
namespace omt
{
class ObSharedTimer;
class ObPxPools;
class ObSrsService;
class ObAiService;
class ObTimezoneMgr;
}
namespace sql
{
class ObSqlMemoryManager;
class ObPlanCache;
class ObPsCache;
namespace dtl
{
class ObDfc;
class ObDTLIntermResultManager;
}
}
namespace query
{
class ObIActiveSnapshotService;
class ObISchedulerService;
class ObIVectorIndexService;
}
namespace data_plane
{
class ObIDmlService;
class ObIMajorFreezeCoordinator;
class ObIOptimizerStorageService;
class ObIReadTimestampService;
class ObIRangeService;
class ObIStorageEstimator;
class ObIWriteContextService;
}
namespace rootserver
{
class ObMaxIdCacheAdapter;
class ObPrimaryMajorFreezeService;
class ObRestoreMajorFreezeService;
class ObDBMSSchedService;
class ObDDLServiceLauncher;
class ObSystemPackageLoadService;
class ObDDLScheduler;
class ObIRootserverLocalRuntime;
}
namespace share
{
class ObISharedTimer;
class ObDagScheduler;
class ObDagWarningHistoryManager;
class ObSharedMemAllocMgr;
class ObResourceLimitCalculator;
class ObPluginVectorIndexService;
class ObChangeStreamMgr;
class ObITabletAutoincrementService;
class ObITabletAutoincrementAdmin;
struct ObFreezeInfo;
namespace schema
{
class ObSchemaServiceSQLImpl;
class ObSchemaRuntimeService;
}
namespace detector { class ObDeadLockDetectorMgr; }
}
namespace storage
{
class ObIServerRuntime;
class ObStorageMetaMemMgr;
class ObLSService;
class ObLocalStorageMetaService;
class ObTabletStatMgr;
class ObSSTableMergeInfoMgr;
class ObLobManager;
class ObMemstoreFreezer;
class ObCompactionMemPool;
class ObFreezeInfoMgr;
class ObAccessService;
class ObEmptyReadBucket;
class ObTabletMemtableMgrPool;
class ObGlobalIteratorPool;
class ObILSRuntimeAdapter;
class ObIVectorIndexRuntime;
class ObLogStorageAdapter;
namespace mds { class ObMdsService; }
namespace checkpoint
{
class ObCheckPointService;
class ObTabletGCService;
}
}
namespace tmp_file { class ObTmpFileManager; }
namespace transaction
{
class ObTransService;
class ObTimestampService;
class ObTimestampAccess;
class ObTransIDService;
class ObUniqueIDService;
class ObTxLoopWorker;
namespace tablelock
{
class ObIInnerConnectionLockRuntime;
class ObTableLockService;
}
}
namespace logservice
{
class ObLogService;
class ObILocalLogHandler;
}
namespace compaction
{
class ObCompactionProgressMgr;
class ObServerCompactionEventHistory;
class ObScheduleSuspectInfoMgr;
class ObCompactionSuggestionMgr;
class ObDiagnoseTabletMgr;
class ObTabletScheduler;
class ObMediumChecker;
}
namespace memtable { class ObLockWaitMgr; }
namespace concurrency_control { class ObMultiVersionGarbageCollector; }
namespace standby { class StandbyModule; }
namespace observer
{

class ObServerOptions;
class ObSchemaRefreshSchedulerAdapter;
class ObServerPluginRuntime;

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
class ObServer : public share::ObIModuleProvider,
                 public share::ObIMemstoreRuntime,
                 public data_plane::ObILockWaitService,
                 public data_plane::ObIMemoryPressureService,
                 public query::ObIAiEndpointAdmin,
                 public query::ObIAiEndpointResolver,
                 public query::ObIChangeStreamService,
                 public query::ObIDdlExecutionLimiter,
                 public query::ObIPlanCacheAccessService,
                 public query::ObIQueryRuntimeEnvironment,
                 public sql::ObIVirtualTableFactoryProvider,
                 public sql::ObIDdlSliceStore
{
public:
  static const int64_t DEFAULT_ETHERNET_SPEED = common::OB_DEFAULT_ETHERNET_SPEED; // single source of truthin share/io/ob_io_define.h
  static const int64_t DISK_USAGE_REPORT_INTERVAL = 1000L * 1000L * 60L; // 1min
  static const uint64_t DEFAULT_CPU_FREQUENCY = 2500 * 1000; // 2500 * 1000 khz
  static ObServer &get_instance();

public:
  int init(const ObServerOptions &opts, const ObPLogWriterCfg &log_cfg);
  void destroy();

  // Start OceanBase server, this function is blocked after invoking
  // until the server itself stops it.
  int start();
  int wait();
  void prepare_stop();
  bool is_prepare_stopped();
  void set_stop();
  bool is_stopped();
  int wait_until_refreshed(
      common::ObMySQLProxy &mysql_proxy,
      int64_t timeout_us) override;

public:
  //Refer to ObPurgeCompletedMonitorInfoTask
  class ObCTASCleanUpTask: public common::ObTimerTask
  {
  public:
    ObCTASCleanUpTask();
    virtual ~ObCTASCleanUpTask() {}
    int init(ObServer *observer, common::ObTimer &timer);
    virtual void runTimerTask() override;
  private:
    const static int64_t CLEANUP_INTERVAL = 60L * 1000L * 1000L;//60s
    ObServer *obs_;
    bool is_inited_;
  };

  class ObRefreshCpuFreqTimeTask: public common::ObTimerTask
  {
  public:
    ObRefreshCpuFreqTimeTask();
    virtual ~ObRefreshCpuFreqTimeTask() {}
    int init(ObServer *observer, common::ObTimer &timer);
    virtual void runTimerTask() override;
  private:
    const static int64_t REFRESH_INTERVAL = 10 * 1000L * 1000L;//10s
    ObServer *obs_;
    bool is_inited_;
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
      TEMP_TAB_RULE       //Cleanup rules for temporary tables
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
  sql::ObSql &get_sql_engine() { return sql_engine_; }
  rootserver::ObLocalManagementService &get_local_management_service() { return local_management_service_; }
  common::ObMySQLProxy &get_mysql_proxy() { return sql_proxy_; }
  int64_t get_start_time() const { return start_time_; }
  sql::ObConnectResourceMgr& get_conn_res_mgr() { return conn_res_mgr_; }
private:
  int stop();
  int wait_no_client();

private:
  ObServer();
  ~ObServer();

  int init_config(const ObServerOptions &opts);
  int init_plugin_runtime(const ObServerOptions &opts);
  int check_plugin_server_ready();
  void destroy_plugin_runtime() noexcept;
  int init_opts_config(const ObServerOptions &opts, const char *optstr); // init configs from command line
  int init_data_dir_and_redo_dir(const ObServerOptions &opts);
  int init_self_addr();
  int init_config_module(const char *optstr);
  int init_tz_info_mgr();
  int init_pre_setting();
  int init_network();
  int init_interrupt();
  int init_fts();
  int init_server_runtime();
  int init_sql_proxy();
  int init_io();
  int init_schema();
  int init_inner_table_monitor();
  int init_autoincrement_service();
  int init_tablet_autoincrement_service();
  int init_global_kvcache();
  int init_global_session_info();
  int init_ob_service(bool need_bootstrap);
  int init_local_management_service(const bool need_bootstrap);
  int init_sql();
  int init_sql_runner();
  int init_pl();
  int init_global_context();
  int parse_role(const ObServerOptions &opts);
  int init_px_target_mgr();
  int init_storage();
  int init_tx_data_cache();
  int init_gc_partition_adapter();
  int init_loaddata_global_stat();
  int init_bandwidth_throttle();
  int start_log_mgr();
  int stop_log_mgr();
  int refresh_cpu_frequency();
  int clean_up_invalid_tables();
  int init_ctas_clean_up_task(); //Regularly clean up the residuals related to querying and building tables and temporary tables
  int init_redef_heart_beat_task();
  int init_ddl_heart_beat_task_container();
  int init_refresh_cpu_frequency();
  int set_running_mode();
  int initialize_server_runtime();
  int wait_for_server_runtime();
  int check_if_schema_ready();
  int check_if_timezone_usable();
  int parse_mode();
  void deinit_fts();

public:
  static int get_network_speed_from_config_file(int64_t &network_speed);
public:
  volatile bool need_ctas_cleanup_; //true: ObCTASCleanUpTask should traverse all table schemas to find the one need be dropped
private:
  class StandbyHostAdapter;

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
  bool need_bootstrap_;
  volatile bool has_stopped_;
  bool has_destroy_;
  int clients_fd_ = -1;
#ifdef _WIN32
  HANDLE clients_h_ = INVALID_HANDLE_VALUE;
#endif
  // The network framework in OceanBase is all defined at ObServerNetworkFrame.
  ObSrvNetworkFrame net_frame_;


  common::ObMySQLProxy sql_proxy_;
  common::ObMySQLProxy ddl_sql_proxy_;

  // The OceanBase configuration relating to.
  common::ObServerConfig &config_;
  ObServerReloadConfig reload_config_;
  common::ObConfigManager config_mgr_;
  omt::ObTimezoneMgr &timezone_mgr_;

  // The Oceanbase schema relating to.
  share::schema::ObSchemaServiceSQLImpl *schema_service_sql_impl_;
  share::schema::ObMultiVersionSchemaService &schema_service_;
  share::schema::ObSchemaPublishSignal schema_publish_signal_;
  ObSchemaRefreshSchedulerAdapter *schema_refresh_scheduler_;
  rootserver::ObMaxIdCacheAdapter *max_id_cache_adapter_;

  // The SQL Engine
  sql::ObSql sql_engine_;

  // The PL Engine
  pl::ObPL pl_engine_;

  // Shared SQLite connection pool for meta database (config and tablet_meta tables)
  share::ObSQLiteConnectionPool meta_db_pool_;

  // Optional plugin runtime implementation is kept opaque from this header.
  std::unique_ptr<ObServerPluginRuntime> plugin_runtime_;

  // The Oceanbase partition table relating to
  share::ObTabletTableOperator tablet_operator_;

  // storage related
  common::ObInOutBandwidthThrottle bandwidth_throttle_;
  int64_t sys_bkgd_net_percentage_;
  int64_t ethernet_speed_;
  uint64_t cpu_frequency_;

  // sql session_mgr
  sql::ObSQLSessionMgr session_mgr_;

  // Process-local schema, DDL, job, freeze and recycle-bin management.
  rootserver::ObLocalManagementService local_management_service_;
  StandbyHostAdapter *standby_host_;
  standby::StandbyModule *standby_module_;
  // All operations and processing logic relating to ob server is
  // defined in oceanbase_service_.
  ObService ob_service_;
  rootserver::ObDebugSyncBroadcasterAdapter debug_sync_broadcaster_;

  omt::ObServerRuntimeController server_runtime_controller_;

  // virtual table related
  ObVirtualDataAccessService vt_data_service_;
  // Weakly Consistent Read Service
  //observer start time
  int64_t start_time_;
  int64_t warm_up_start_time_;
  obmysql::ObDiag diag_;
  common::ObMysqlRandom scramble_rand_;
  common::ObTimer server_gtimer_;
  common::ObTimer sql_mem_timer_;
  common::ObTimer ctas_clean_up_timer_;
  ObServerDutyTask duty_task_;
  ObSqlMemoryTimerTask sql_mem_task_;
  ObCTASCleanUpTask ctas_clean_up_task_;     // repeat & no retry
  ObRedefTableHeartBeatTask redef_table_heart_beat_task_;
  ObRefreshCpuFreqTimeTask refresh_cpu_frequency_task_;
  blocksstable::ObStorageEnv storage_env_;
  share::ObSchemaStatusProxy schema_status_proxy_;

  bool is_log_dir_empty_;
  sql::ObConnectResourceMgr conn_res_mgr_;
  ObDiskUsageReportTask disk_usage_report_task_;

  logservice::ObServerLogBlockMgr log_block_mgr_;

  // This handler is used to process tasks during startup. it can speed up the startup process.
  // If you have tasks that need to be processed in parallel, you can use this handler,
  // but please note that this handler will be destroyed after observer startup.
  storage::ObStartupAccelTaskHandler startup_accel_handler_;

public:
  // ===== Observer-owned services bound into the single-server runtime =====
  // Module accessors.
  share::ObISharedTimer * shared_timer() { return mods_shared_timer_; }
  blocksstable::ObSharedMacroBlockMgr * shared_macro_block_mgr() { return mods_shared_macro_block_mgr_; }
  storage::ObStorageMetaMemMgr * storage_meta_mem_mgr() { return mods_storage_meta_mem_mgr_; }
  ObTableScanIteratorObjPool * table_scan_iterator_obj_pool() { return mods_table_scan_iterator_obj_pool_; }
  common::ObIOService * io_service() { return mods_io_service_; }
  storage::mds::ObMdsService * mds_service() { return mods_mds_service_; }
  share::ObSharedMemAllocMgr * shared_mem_alloc_mgr() { return mods_shared_mem_alloc_mgr_; }
  int set_memstore_threshold() override;
  int get_server_cpu(double &min_cpu, double &max_cpu)
  {
    return server_runtime_controller_.get_server_cpu(min_cpu, max_cpu);
  }
  transaction::ObTransService * trans_service() { return mods_trans_service_; }
  logservice::ObLogService * log_service() { return mods_log_service_; }
  storage::ObLSService * ls_service() { return mods_ls_service_; }
  storage::ObILSRuntimeAdapter * ls_runtime_adapter() { return &ls_runtime_adapter_; }
  storage::ObLocalStorageMetaService * local_storage_meta_service() { return mods_local_storage_meta_service_; }
  common::ObMySQLProxy * sql_proxy() { return &sql_proxy_; }
  tmp_file::ObTmpFileManager * tmp_file_manager() { return mods_tmp_file_manager_; }
  compaction::ObCompactionProgressMgr * compaction_progress_mgr() { return mods_compaction_progress_mgr_; }
  compaction::ObServerCompactionEventHistory * server_compaction_event_history() { return mods_server_compaction_event_history_; }
  storage::ObTabletStatMgr * tablet_stat_mgr() { return mods_tablet_stat_mgr_; }
  memtable::ObLockWaitMgr * lock_wait_mgr() { return mods_lock_wait_mgr_; }
  data_plane::ObILockWaitService * lock_wait_service() { return this; }
  data_plane::ObIMajorFreezeCoordinator * major_freeze_coordinator()
  {
    return &major_freeze_coordinator_adapter_;
  }
  void reset_current_wait() override;
  int repost_lock_wait_request(void *request) override
  {
    return net_frame_.get_deliver().repost(request);
  }
  int get_or_insert_schedule_info(
      int64_t task_id,
      common::ObIAllocator &allocator,
      common::Ob2DArray<sql::ObPxTabletRange> &part_ranges,
      bool &is_idempotent_mode) override;
  transaction::tablelock::ObTableLockService * table_lock_service() { return mods_table_lock_service_; }
  rootserver::ObPrimaryMajorFreezeService * primary_major_freeze_service() { return mods_primary_major_freeze_service_; }
  rootserver::ObRestoreMajorFreezeService * restore_major_freeze_service() { return mods_restore_major_freeze_service_; }
  observer::ObTabletRuntimeMetaUpdater * tablet_runtime_meta_updater() { return mods_tablet_runtime_meta_updater_; }
  storage::ObSSTableMergeInfoMgr * sstable_merge_info_mgr() { return mods_sstable_merge_info_mgr_; }
  share::ObDagWarningHistoryManager * dag_warning_history_manager() { return mods_dag_warning_history_manager_; }
  compaction::ObScheduleSuspectInfoMgr * schedule_suspect_info_mgr() { return mods_schedule_suspect_info_mgr_; }
  compaction::ObCompactionSuggestionMgr * compaction_suggestion_mgr() { return mods_compaction_suggestion_mgr_; }
  compaction::ObDiagnoseTabletMgr * diagnose_tablet_mgr() { return mods_diagnose_tablet_mgr_; }
  storage::ObLobManager * lob_manager() { return mods_lob_manager_; }
  common::ObILobReadService * lob_read_service();
  int get_lower_bound_freeze_info(const int64_t snapshot_version, share::ObFreezeInfo &freeze_info);
  share::detector::ObDeadLockDetectorMgr * dead_lock_detector_mgr() { return mods_dead_lock_detector_mgr_; }
  transaction::ObTimestampService * timestamp_service() { return mods_timestamp_service_; }
  transaction::ObTimestampAccess * timestamp_access() { return mods_timestamp_access_; }
  transaction::ObTransIDService * trans_id_service() { return mods_trans_id_service_; }
  transaction::ObUniqueIDService * unique_id_service() { return mods_unique_id_service_; }
  sql::ObPsCache * ps_cache() { return mods_ps_cache_; }
  sql::ObPlanCache * plan_cache() { return mods_plan_cache_; }
  sql::dtl::ObDfc * dfc_manager() { return mods_dfc_; }
  omt::ObPxPools * px_pools() { return mods_px_pools_; }
  sql::ObSqlMemoryManager * sql_memory_manager() { return mods_sql_memory_manager_; }
  sql::dtl::ObDTLIntermResultManager * dtl_interm_result_manager() { return mods_dtl_interm_result_manager_; }
  sql::ObDataAccessService * data_access_service() { return mods_data_access_service_; }
  share::schema::ObSchemaRuntimeService * schema_runtime_service() { return mods_schema_runtime_service_; }
  storage::ObMemstoreFreezer * memstore_freezer() { return mods_memstore_freezer_; }
  storage::checkpoint::ObCheckPointService * check_point_service() { return mods_check_point_service_; }
  storage::checkpoint::ObTabletGCService * tablet_gc_service() { return mods_tablet_gc_service_; }
  compaction::ObTabletScheduler * tablet_scheduler() { return mods_tablet_scheduler_; }
  compaction::ObMediumChecker * medium_checker() { return mods_medium_checker_; }
  storage::ObCompactionMemPool * compaction_mem_pool() { return mods_compaction_mem_pool_; }
  share::ObDagScheduler * dag_scheduler() { return mods_dag_scheduler_; }
  storage::ObFreezeInfoMgr * freeze_info_mgr() { return mods_freeze_info_mgr_; }
  transaction::ObTxLoopWorker * tx_loop_worker() { return mods_tx_loop_worker_; }
  storage::ObAccessService * access_service() { return mods_access_service_; }
  data_plane::ObIOptimizerStorageService * optimizer_storage_service()
  {
    return mods_access_service_;
  }
  data_plane::ObIStorageEstimator * storage_estimator();
  data_plane::ObIReadTimestampService * read_timestamp_service();
  data_plane::ObIRangeService * range_service();
  data_plane::ObIWriteContextService * write_context_service();
  common::ObITabletScan * tablet_scan_service();
  data_plane::ObIDmlService * dml_service();
  data_plane::ObIMemoryPressureService * memory_pressure_service()
  {
    return this;
  }
  query::ObIVectorIndexService * vector_index_service();
  int get_memstore_condition(
      int64_t &active_memstore_used,
      int64_t &total_memstore_used,
      int64_t &memstore_freeze_trigger,
      int64_t &memstore_limit,
      int64_t &freeze_count) override;
  void enter_access() override;
  void leave_access() override;
  void check_current_thread() override;
  int get_global_safe_timestamp(int64_t &safe_timestamp) const override;
  uint64_t cpu_frequency_khz() override { return get_cpu_frequency_khz(); }
  int64_t network_speed_bytes_per_second() const override
  {
    return get_network_speed();
  }
  bool server_stopped() override { return is_stopped(); }
  bool server_has_tenant() const override
  {
    return server_runtime_controller_.has_runtime();
  }
  void request_ctas_cleanup() override;
  int check_current_tenant_available() const override;
  int get_current_tenant_cpu(double &min_cpu, double &max_cpu) const override;
  int get_current_tenant_min_worker_count(int64_t &worker_count) const override;
  int get_current_worker_unit_min_cpu(double &min_cpu) const override;
  int64_t current_query_start_time() const override;
  int submit_current_tenant_request(rpc::ObRequest &request) const override;
  int submit_px_task(
      int64_t group_id,
      const std::function<void(bool)> &task) const override;
  int create_virtual_table_factory(
      common::ObIAllocator &allocator,
      sql::ObIVirtualTableIteratorFactory *&factory) override;
  void destroy_virtual_table_factory(
      sql::ObIVirtualTableIteratorFactory *factory) override;
  share::ObITabletAutoincrementService * tablet_autoincrement_service();
  share::ObITabletAutoincrementAdmin * tablet_autoincrement_admin();
  query::ObIAiEndpointResolver * ai_endpoint_resolver() { return this; }
  int create_endpoint(
      common::ObArenaAllocator &allocator,
      const common::ObString &endpoint_name,
      const common::ObIJsonBase &definition) override;
  int alter_endpoint(
      common::ObArenaAllocator &allocator,
      const common::ObString &endpoint_name,
      const common::ObIJsonBase &definition) override;
  int drop_endpoint(const common::ObString &endpoint_name) override;
  int resolve_by_model_name(
      const common::ObString &model_name,
      common::ObIAllocator &allocator,
      share::ObAiModelEndpointInfo &endpoint,
      bool check_access = true) const override;
  int try_acquire_ddl_execution(int64_t cpu_quota_concurrency) override;
  void release_ddl_execution() override;
  concurrency_control::ObMultiVersionGarbageCollector * multi_version_garbage_collector() { return mods_multi_version_garbage_collector_; }
  storage::ObEmptyReadBucket * empty_read_bucket() { return mods_empty_read_bucket_; }
  transaction::tablelock::ObIInnerConnectionLockRuntime *
      inner_connection_lock_runtime();
  rootserver::ObDBMSSchedService * dbms_sched_service() { return mods_dbms_sched_service_; }
  query::ObISchedulerService * scheduler_service();
  query::ObIActiveSnapshotService * active_snapshot_service()
  {
    return &session_mgr_;
  }
  oceanbase::common::ObOptStatMonitorManager * opt_stat_monitor_manager() { return mods_opt_stat_monitor_manager_; }
  omt::ObSrsService * srs_service() { return mods_srs_service_; }
  logservice::ObILocalLogHandler * internal_table_refresh_handler()
  {
    return &internal_table_refresh_adapter_;
  }
  storage::ObTabletMemtableMgrPool * tablet_memtable_mgr_pool() { return mods_tablet_memtable_mgr_pool_; }
  storage::ObIServerRuntime * server_runtime_service() { return &server_runtime_controller_; }
  share::ObResourceLimitCalculator * resource_limit_calculator() { return mods_resource_limit_calculator_; }
  storage::ObGlobalIteratorPool * global_iterator_pool() { return mods_global_iterator_pool_; }
  common::ObRbMemMgr * rb_mem_mgr() { return mods_rb_mem_mgr_; }
  share::ObPluginVectorIndexService * plugin_vector_index_service() { return mods_plugin_vector_index_service_; }
  storage::ObIVectorIndexRuntime * vector_index_runtime();
  rootserver::ObDDLServiceLauncher * ddl_service_launcher() { return mods_ddl_service_launcher_; }
  rootserver::ObSystemPackageLoadService * system_package_load_service() { return mods_system_package_load_service_; }
  rootserver::ObDDLScheduler * ddl_scheduler() { return mods_ddl_scheduler_; }
  rootserver::ObIRootserverLocalRuntime * rootserver_local_runtime()
  {
    return &ob_service_;
  }
  omt::ObAiService * ai_service() { return mods_ai_service_; }
  share::ObChangeStreamMgr * change_stream_mgr() { return mods_change_stream_mgr_; }
  int execute_plugin_function(
      const char *service_id,
      uint32_t abi_major,
      uint32_t required_minor,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) override;
  int execute_plugin_extension(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) override;
  int resolve_plugin_sql_object(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      const char *const *argument_type_ids,
      uint32_t argument_count,
      seekdb_plugin_sql_binding_v1_t *binding) override;
  int execute_bound_plugin_function(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count) override;
  int describe_plugin_sql_column(
      const seekdb_plugin_sql_binding_v1_t *binding,
      uint32_t column_index,
      seekdb_plugin_sql_column_v1_t *column) override;
  int open_bound_plugin_table_function(
      const seekdb_plugin_sql_binding_v1_t *binding,
      const seekdb_plugin_table_execution_context_v1_t *context,
      const seekdb_plugin_execution_value_v1_t *arguments,
      uint32_t argument_count,
      std::unique_ptr<share::IPluginTableCursor> &cursor) override;
  int mutate_plugin_type_dependency(
      common::ObISQLClient &sql_client,
      const seekdb_plugin_sql_binding_v1_t &binding,
      uint64_t table_id,
      uint64_t column_id,
      bool add) override;
  // Explicit module lifecycle (ObServer owns modules; defined in ob_server_runtime_controller.cpp).
  int obs_construct_modules();
  int obs_init_modules();
  int obs_start_modules();
  void obs_stop_modules();
  void obs_wait_modules();
  void obs_destroy_modules();

private:
  // ===== module instances (ObServer is the sole owner; created by
  // obs_construct_modules() at boot and bound into runtime service slots) =====
  omt::ObSharedTimer * mods_shared_timer_ = nullptr;
  blocksstable::ObSharedMacroBlockMgr * mods_shared_macro_block_mgr_ = nullptr;
  storage::ObStorageMetaMemMgr * mods_storage_meta_mem_mgr_ = nullptr;
  ObTableScanIteratorObjPool * mods_table_scan_iterator_obj_pool_ = nullptr;
  common::ObIOService * mods_io_service_ = nullptr;
  storage::mds::ObMdsService * mods_mds_service_ = nullptr;
  share::ObSharedMemAllocMgr * mods_shared_mem_alloc_mgr_ = nullptr;
  transaction::ObTransService * mods_trans_service_ = nullptr;
  storage::ObLogStorageAdapter * mods_log_storage_adapter_ = nullptr;
  logservice::ObLogService * mods_log_service_ = nullptr;
  storage::ObLSService * mods_ls_service_ = nullptr;
  storage::ObLocalStorageMetaService * mods_local_storage_meta_service_ = nullptr;
  tmp_file::ObTmpFileManager * mods_tmp_file_manager_ = nullptr;
  compaction::ObCompactionProgressMgr * mods_compaction_progress_mgr_ = nullptr;
  compaction::ObServerCompactionEventHistory * mods_server_compaction_event_history_ = nullptr;
  storage::ObTabletStatMgr * mods_tablet_stat_mgr_ = nullptr;
  memtable::ObLockWaitMgr * mods_lock_wait_mgr_ = nullptr;
  transaction::tablelock::ObTableLockService * mods_table_lock_service_ = nullptr;
  rootserver::ObMajorFreezeCoordinatorAdapter major_freeze_coordinator_adapter_;
  rootserver::ObPrimaryMajorFreezeService * mods_primary_major_freeze_service_ = nullptr;
  rootserver::ObRestoreMajorFreezeService * mods_restore_major_freeze_service_ = nullptr;
  observer::ObTabletRuntimeMetaUpdater * mods_tablet_runtime_meta_updater_ = nullptr;
  storage::ObSSTableMergeInfoMgr * mods_sstable_merge_info_mgr_ = nullptr;
  share::ObDagWarningHistoryManager * mods_dag_warning_history_manager_ = nullptr;
  compaction::ObScheduleSuspectInfoMgr * mods_schedule_suspect_info_mgr_ = nullptr;
  compaction::ObCompactionSuggestionMgr * mods_compaction_suggestion_mgr_ = nullptr;
  compaction::ObDiagnoseTabletMgr * mods_diagnose_tablet_mgr_ = nullptr;
  storage::ObLobManager * mods_lob_manager_ = nullptr;
  share::detector::ObDeadLockDetectorMgr * mods_dead_lock_detector_mgr_ = nullptr;
  transaction::ObTimestampService * mods_timestamp_service_ = nullptr;
  transaction::ObTimestampAccess * mods_timestamp_access_ = nullptr;
  transaction::ObTransIDService * mods_trans_id_service_ = nullptr;
  transaction::ObUniqueIDService * mods_unique_id_service_ = nullptr;
  sql::ObPsCache * mods_ps_cache_ = nullptr;
  sql::ObPlanCache * mods_plan_cache_ = nullptr;
  sql::dtl::ObDfc * mods_dfc_ = nullptr;
  omt::ObPxPools * mods_px_pools_ = nullptr;
  sql::ObSqlMemoryManager * mods_sql_memory_manager_ = nullptr;
  sql::dtl::ObDTLIntermResultManager * mods_dtl_interm_result_manager_ = nullptr;
  sql::ObDataAccessService * mods_data_access_service_ = nullptr;
  share::schema::ObSchemaRuntimeService * mods_schema_runtime_service_ = nullptr;
  storage::ObMemstoreFreezer * mods_memstore_freezer_ = nullptr;
  storage::checkpoint::ObCheckPointService * mods_check_point_service_ = nullptr;
  storage::checkpoint::ObTabletGCService * mods_tablet_gc_service_ = nullptr;
  compaction::ObTabletScheduler * mods_tablet_scheduler_ = nullptr;
  compaction::ObMediumChecker * mods_medium_checker_ = nullptr;
  storage::ObCompactionMemPool * mods_compaction_mem_pool_ = nullptr;
  share::ObDagScheduler * mods_dag_scheduler_ = nullptr;
  storage::ObFreezeInfoMgr * mods_freeze_info_mgr_ = nullptr;
  transaction::ObTxLoopWorker * mods_tx_loop_worker_ = nullptr;
  storage::ObAccessService * mods_access_service_ = nullptr;
  concurrency_control::ObMultiVersionGarbageCollector * mods_multi_version_garbage_collector_ = nullptr;
  storage::ObEmptyReadBucket * mods_empty_read_bucket_ = nullptr;
  rootserver::ObDBMSSchedService * mods_dbms_sched_service_ = nullptr;
  oceanbase::common::ObOptStatMonitorManager * mods_opt_stat_monitor_manager_ = nullptr;
  omt::ObSrsService * mods_srs_service_ = nullptr;
  ObInternalTableRefreshAdapter internal_table_refresh_adapter_;
  storage::ObTabletMemtableMgrPool * mods_tablet_memtable_mgr_pool_ = nullptr;
  share::ObResourceLimitCalculator * mods_resource_limit_calculator_ = nullptr;
  storage::ObGlobalIteratorPool * mods_global_iterator_pool_ = nullptr;
  common::ObRbMemMgr * mods_rb_mem_mgr_ = nullptr;
  share::ObPluginVectorIndexService * mods_plugin_vector_index_service_ = nullptr;
  rootserver::ObDDLServiceLauncher * mods_ddl_service_launcher_ = nullptr;
  rootserver::ObSystemPackageLoadService * mods_system_package_load_service_ = nullptr;
  rootserver::ObDDLScheduler * mods_ddl_scheduler_ = nullptr;
  omt::ObAiService * mods_ai_service_ = nullptr;
  share::ObChangeStreamMgr * mods_change_stream_mgr_ = nullptr;
  ObLSRuntimeAdapter ls_runtime_adapter_;
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
