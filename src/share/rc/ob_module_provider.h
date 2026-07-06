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

#ifndef OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
#define OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_

// Low-layer module-access facade. ObServer owns the module
// instances and implements ObIModuleProvider; the global g_mp (set to &OBSERVER at
// boot) lets low-layer code (storage/share/lib) reach modules WITHOUT including
// observer/ob_server.h (no reverse dependency). Accessors return pointers so a
// module can later be exposed via a base-class pointer without touching call sites.
// The accessor declarations mirror the former MTL_MEMBERS module set.
//
// Include ob_tenant_base.h for the module forward declarations, the
// ObjPool `using` aliases, ObTestModule and lib::Worker::CompatMode.
#include "share/rc/ob_tenant_base.h"

namespace oceanbase
{
namespace share
{
// Single global module set in single-tenant seekdb. Implemented by ObServer.
struct ObFreezeInfo;
class ObIModuleProvider
{
public:
  virtual ~ObIModuleProvider() {}
  // ===== Module accessors (order == MTL_MEMBERS) =====
  virtual omt::ObSharedTimer * shared_timer() { return nullptr; }
  virtual oceanbase::sql::ObTenantSQLSessionMgr * tenant_sql_session_mgr() { return nullptr; }
  virtual storage::ObTenantMetaMemMgr * tenant_meta_mem_mgr() { return nullptr; }
  virtual storage::ObTenantFTPluginMgr * tenant_ft_plugin_mgr() { return nullptr; }
  virtual ObPartTransCtxObjPool * part_trans_ctx_obj_pool() { return nullptr; }
  virtual ObTableScanIteratorObjPool * table_scan_iterator_obj_pool() { return nullptr; }
  virtual common::ObTenantIOManager * tenant_io_manager() { return nullptr; }
  virtual storage::mds::ObTenantMdsService * tenant_mds_service() { return nullptr; }
  virtual blocksstable::ObSharedMacroBlockMgr * shared_macro_block_mgr() { return nullptr; }
  virtual share::ObSharedMemAllocMgr * shared_mem_alloc_mgr() { return nullptr; }
  virtual transaction::ObTransService * trans_service() { return nullptr; }
  virtual logservice::ObLogService * log_service() { return nullptr; }
  virtual storage::ObLSService * ls_service() { return nullptr; }
  virtual storage::ObTenantStorageMetaService * tenant_storage_meta_service() { return nullptr; }
  virtual tmp_file::ObTenantTmpFileManager * tenant_tmp_file_manager() { return nullptr; }
  virtual compaction::ObTenantCompactionProgressMgr * tenant_compaction_progress_mgr() { return nullptr; }
  virtual compaction::ObServerCompactionEventHistory * server_compaction_event_history() { return nullptr; }
  virtual storage::ObTenantTabletStatMgr * tenant_tablet_stat_mgr() { return nullptr; }
  virtual memtable::ObLockWaitMgr * lock_wait_mgr() { return nullptr; }
  virtual transaction::tablelock::ObTableLockService * table_lock_service() { return nullptr; }
  virtual rootserver::ObPrimaryMajorFreezeService * primary_major_freeze_service() { return nullptr; }
  virtual rootserver::ObRestoreMajorFreezeService * restore_major_freeze_service() { return nullptr; }
  virtual observer::ObTenantMetaChecker * tenant_meta_checker() { return nullptr; }
  virtual observer::ObTabletTableUpdater * tablet_table_updater() { return nullptr; }
  virtual storage::ObStorageHAHandlerService * storage_ha_handler_service() { return nullptr; }
  virtual storage::ObTenantSSTableMergeInfoMgr * tenant_ss_table_merge_info_mgr() { return nullptr; }
  virtual share::ObDagWarningHistoryManager * dag_warning_history_manager() { return nullptr; }
  virtual compaction::ObScheduleSuspectInfoMgr * schedule_suspect_info_mgr() { return nullptr; }
  virtual compaction::ObCompactionSuggestionMgr * compaction_suggestion_mgr() { return nullptr; }
  virtual compaction::ObDiagnoseTabletMgr * diagnose_tablet_mgr() { return nullptr; }
  virtual storage::ObLobManager * lob_manager() { return nullptr; }
  virtual common::ObILobReadService * lob_read_service() { return nullptr; }
  virtual int get_lower_bound_freeze_info(const int64_t snapshot_version, ObFreezeInfo &freeze_info) { return common::OB_NOT_SUPPORTED; }
  virtual share::ObGlobalAutoIncService * global_auto_inc_service() { return nullptr; }
  virtual share::detector::ObDeadLockDetectorMgr * dead_lock_detector_mgr() { return nullptr; }
  virtual transaction::ObTimestampService * timestamp_service() { return nullptr; }
  virtual transaction::ObTimestampAccess * timestamp_access() { return nullptr; }
  virtual transaction::ObTransIDService * trans_id_service() { return nullptr; }
  virtual transaction::ObUniqueIDService * unique_id_service() { return nullptr; }
  virtual sql::ObPlanBaselineMgr * plan_baseline_mgr() { return nullptr; }
  virtual sql::ObPsCache * ps_cache() { return nullptr; }
  virtual sql::ObPlanCache * plan_cache() { return nullptr; }
  virtual sql::dtl::ObTenantDfc * tenant_dfc() { return nullptr; }
  virtual omt::ObPxPools * px_pools() { return nullptr; }
  virtual lib::Worker::CompatMode compat_mode() { return lib::Worker::CompatMode::MYSQL; }
  virtual sql::ObTenantSqlMemoryManager * tenant_sql_memory_manager() { return nullptr; }
  virtual sql::dtl::ObDTLIntermResultManager * dtl_interm_result_manager() { return nullptr; }
  virtual sql::ObPlanMonitorNodeList * plan_monitor_node_list() { return nullptr; }
  virtual sql::ObDataAccessService * data_access_service() { return nullptr; }
  virtual sql::ObDASIDService * dasid_service() { return nullptr; }
  virtual share::schema::ObTenantSchemaService * tenant_schema_service() { return nullptr; }
  virtual storage::ObTenantFreezer * tenant_freezer() { return nullptr; }
  virtual storage::checkpoint::ObCheckPointService * check_point_service() { return nullptr; }
  virtual storage::checkpoint::ObTabletGCService * tablet_gc_service() { return nullptr; }
  virtual compaction::ObTenantTabletScheduler * tenant_tablet_scheduler() { return nullptr; }
  virtual compaction::ObTenantMediumChecker * tenant_medium_checker() { return nullptr; }
  virtual storage::ObTenantCompactionMemPool * tenant_compaction_mem_pool() { return nullptr; }
  virtual storage::ObDDLMergeBucketLock * ddl_merge_bucket_lock() { return nullptr; }
  virtual storage::ObTenantDirectLoadMgr * tenant_direct_load_mgr() { return nullptr; }
  virtual share::ObTenantDagScheduler * tenant_dag_scheduler() { return nullptr; }
  virtual storage::ObStorageHAService * storage_ha_service() { return nullptr; }
  virtual storage::ObTenantFreezeInfoMgr * tenant_freeze_info_mgr() { return nullptr; }
  virtual transaction::ObTxLoopWorker * tx_loop_worker() { return nullptr; }
  virtual storage::ObAccessService * access_service() { return nullptr; }
  virtual datadict::ObDataDictService * data_dict_service() { return nullptr; }
  virtual observer::ObTableLoadService * table_load_service() { return nullptr; }
  virtual observer::ObTableLoadResourceService * table_load_resource_service() { return nullptr; }
  virtual concurrency_control::ObMultiVersionGarbageCollector * multi_version_garbage_collector() { return nullptr; }
  virtual sql::ObFLTSpanMgr * flt_span_mgr() { return nullptr; }
  virtual storage::ObTenantCGReadInfoMgr * tenant_cg_read_info_mgr() { return nullptr; }
  virtual ObTestModule * test_module() { return nullptr; }
  virtual storage::ObEmptyReadBucket * empty_read_bucket() { return nullptr; }
  virtual rootserver::ObDBMSSchedService * dbms_sched_service() { return nullptr; }
  virtual oceanbase::common::ObOptStatMonitorManager * opt_stat_monitor_manager() { return nullptr; }
  virtual omt::ObTenantSrs * tenant_srs() { return nullptr; }
  virtual table::ObHTableLockMgr * h_table_lock_mgr() { return nullptr; }
  virtual table::ObTTLService * ttl_service() { return nullptr; }
  virtual table::ObTableObjectPoolMgr * table_object_pool_mgr() { return nullptr; }
  virtual share::ObIndexUsageInfoMgr * index_usage_info_mgr() { return nullptr; }
  virtual storage::ObTabletMemtableMgrPool * tablet_memtable_mgr_pool() { return nullptr; }
  virtual rootserver::ObMViewMaintenanceService * m_view_maintenance_service() { return nullptr; }
  virtual share::ObResourceLimitCalculator * resource_limit_calculator() { return nullptr; }
  virtual storage::ObGlobalIteratorPool * global_iterator_pool() { return nullptr; }
  virtual common::ObRbMemMgr * rb_mem_mgr() { return nullptr; }
  virtual share::ObPluginVectorIndexService * plugin_vector_index_service() { return nullptr; }
  virtual share::ObAutoSplitTaskCache * auto_split_task_cache() { return nullptr; }
  virtual observer::ObTenantQueryRespTimeCollector * tenant_query_resp_time_collector() { return nullptr; }
  virtual table::ObTableGroupCommitMgr * table_group_commit_mgr() { return nullptr; }
  virtual observer::ObTableQueryASyncMgr * table_query_a_sync_mgr() { return nullptr; }
  virtual table::ObTableClientInfoMgr * table_client_info_mgr() { return nullptr; }
  virtual table::ObHTableRowkeyMgr * h_table_rowkey_mgr() { return nullptr; }
  virtual rootserver::ObDDLServiceLauncher * ddl_service_launcher() { return nullptr; }
  virtual rootserver::ObSysTenantLoadSysPackageService * sys_tenant_load_sys_package_service() { return nullptr; }
  virtual rootserver::ObDDLScheduler * ddl_scheduler() { return nullptr; }
  virtual sql::ObSQLCCLRuleManager * sqlccl_rule_manager() { return nullptr; }
  virtual omt::ObTenantAiService * tenant_ai_service() { return nullptr; }
  virtual share::ObChangeStreamMgr * change_stream_mgr() { return nullptr; }
};

// Set to &OBSERVER once the single sys tenant's modules are created (boot), before
// any consumer thread reads a module. Low-layer code uses g_mp->xxx().
extern ObIModuleProvider *g_mp;

// Typed bridges so MTL_WITH_CHECK / mtl_sop resolve through g_mp instead
// of the (removed) ObTenantEnv::mtl<T> slot path. One specialization per type actually used.
template <class T> T mtl_checked();
template <> inline storage::ObLSService *mtl_checked<storage::ObLSService *>()
{ return g_mp->ls_service(); }
template <> inline memtable::ObLockWaitMgr *mtl_checked<memtable::ObLockWaitMgr *>()
{ return g_mp->lock_wait_mgr(); }
template <> inline transaction::ObTransService *mtl_checked<transaction::ObTransService *>()
{ return g_mp->trans_service(); }
template <> inline transaction::tablelock::ObTableLockService *mtl_checked<transaction::tablelock::ObTableLockService *>()
{ return g_mp->table_lock_service(); }

template <class T> common::ObServerObjectPool<T> *mtl_obj_pool();
template <> inline common::ObServerObjectPool<transaction::ObPartTransCtx> *mtl_obj_pool<transaction::ObPartTransCtx>()
{ return g_mp->part_trans_ctx_obj_pool(); }
template <> inline common::ObServerObjectPool<oceanbase::storage::ObTableScanIterator> *mtl_obj_pool<oceanbase::storage::ObTableScanIterator>()
{ return g_mp->table_scan_iterator_obj_pool(); }

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
