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
// Include ob_server_runtime.h for module forward declarations, object-pool
// aliases, ObTestModule and lib::Worker::CompatMode.
#include "share/rc/ob_server_runtime.h"

struct seekdb_plugin_execution_context_v1;
struct seekdb_plugin_execution_value_v1;
typedef int32_t seekdb_plugin_extension_kind_t;

namespace oceanbase
{
namespace common
{
class ObIOService;
class ObOptStatMonitorManager;
class ObRbMemMgr;
class ObILobReadService;
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
}
namespace sql
{
class ObSqlMemoryManager;
class ObPlanBaselineMgr;
class ObDataAccessService;
class ObPlanCache;
class ObPsCache;
namespace dtl
{
class ObDfc;
class ObDTLIntermResultManager;
}
}
namespace storage
{
class ObStorageMetaMemMgr;
class ObLSService;
class ObLocalStorageMetaService;
class ObTabletStatMgr;
class ObSSTableMergeInfoMgr;
class ObLobManager;
class ObMemstoreFreezer;
class ObCompactionMemPool;
class ObDirectLoadMgr;
class ObFreezeInfoMgr;
class ObAccessService;
class ObEmptyReadBucket;
class ObTabletMemtableMgrPool;
class ObGlobalIteratorPool;
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
namespace tablelock { class ObTableLockService; }
}
namespace logservice { class ObLogService; }
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
namespace rootserver
{
class ObPrimaryMajorFreezeService;
class ObRestoreMajorFreezeService;
class ObDBMSSchedService;
class ObDDLServiceLauncher;
class ObSystemPackageLoadService;
class ObDDLScheduler;
}
namespace observer
{
class ObTabletRuntimeMetaUpdater;
}
namespace concurrency_control { class ObMultiVersionGarbageCollector; }
namespace share
{
class ObTestModule;
class ObDagScheduler;
class ObDagWarningHistoryManager;
class ObSharedMemAllocMgr;
class ObResourceLimitCalculator;
class ObPluginVectorIndexService;
class ObChangeStreamMgr;
class ObErrsimModuleMgr;
namespace schema { class ObSchemaRuntimeService; }
namespace detector { class ObDeadLockDetectorMgr; }
// Single global module set in seekdb. Implemented by ObServer.
struct ObFreezeInfo;
class ObIModuleProvider
{
public:
  virtual ~ObIModuleProvider() {}
  // ===== Module accessors =====
  virtual omt::ObSharedTimer * shared_timer() { return nullptr; }
  virtual blocksstable::ObSharedMacroBlockMgr * shared_macro_block_mgr() { return nullptr; }
  virtual storage::ObStorageMetaMemMgr * storage_meta_mem_mgr() { return nullptr; }
  virtual ObTableScanIteratorObjPool * table_scan_iterator_obj_pool() { return nullptr; }
  virtual common::ObIOService * io_service() { return nullptr; }
  virtual storage::mds::ObMdsService * mds_service() { return nullptr; }
  virtual share::ObSharedMemAllocMgr * shared_mem_alloc_mgr() { return nullptr; }
  virtual share::ObErrsimModuleMgr * errsim_module_mgr() { return nullptr; }
  virtual transaction::ObTransService * trans_service() { return nullptr; }
  virtual logservice::ObLogService * log_service() { return nullptr; }
  virtual storage::ObLSService * ls_service() { return nullptr; }
  virtual storage::ObLocalStorageMetaService * local_storage_meta_service() { return nullptr; }
  virtual tmp_file::ObTmpFileManager * tmp_file_manager() { return nullptr; }
  virtual compaction::ObCompactionProgressMgr * compaction_progress_mgr() { return nullptr; }
  virtual compaction::ObServerCompactionEventHistory * server_compaction_event_history() { return nullptr; }
  virtual storage::ObTabletStatMgr * tablet_stat_mgr() { return nullptr; }
  virtual memtable::ObLockWaitMgr * lock_wait_mgr() { return nullptr; }
  virtual transaction::tablelock::ObTableLockService * table_lock_service() { return nullptr; }
  virtual rootserver::ObPrimaryMajorFreezeService * primary_major_freeze_service() { return nullptr; }
  virtual rootserver::ObRestoreMajorFreezeService * restore_major_freeze_service() { return nullptr; }
  virtual observer::ObTabletRuntimeMetaUpdater * tablet_runtime_meta_updater() { return nullptr; }
  virtual storage::ObSSTableMergeInfoMgr * sstable_merge_info_mgr() { return nullptr; }
  virtual share::ObDagWarningHistoryManager * dag_warning_history_manager() { return nullptr; }
  virtual compaction::ObScheduleSuspectInfoMgr * schedule_suspect_info_mgr() { return nullptr; }
  virtual compaction::ObCompactionSuggestionMgr * compaction_suggestion_mgr() { return nullptr; }
  virtual compaction::ObDiagnoseTabletMgr * diagnose_tablet_mgr() { return nullptr; }
  virtual storage::ObLobManager * lob_manager() { return nullptr; }
  virtual common::ObILobReadService * lob_read_service() { return nullptr; }
  virtual int get_lower_bound_freeze_info(const int64_t snapshot_version, ObFreezeInfo &freeze_info) { return common::OB_NOT_SUPPORTED; }
  virtual share::detector::ObDeadLockDetectorMgr * dead_lock_detector_mgr() { return nullptr; }
  virtual transaction::ObTimestampService * timestamp_service() { return nullptr; }
  virtual transaction::ObTimestampAccess * timestamp_access() { return nullptr; }
  virtual transaction::ObTransIDService * trans_id_service() { return nullptr; }
  virtual transaction::ObUniqueIDService * unique_id_service() { return nullptr; }
  virtual sql::ObPsCache * ps_cache() { return nullptr; }
  virtual sql::ObPlanCache * plan_cache() { return nullptr; }
  virtual sql::dtl::ObDfc * dfc_manager() { return nullptr; }
  virtual omt::ObPxPools * px_pools() { return nullptr; }
  virtual sql::ObSqlMemoryManager * sql_memory_manager() { return nullptr; }
  virtual sql::dtl::ObDTLIntermResultManager * dtl_interm_result_manager() { return nullptr; }
  virtual sql::ObDataAccessService * data_access_service() { return nullptr; }
  virtual share::schema::ObSchemaRuntimeService * schema_runtime_service() { return nullptr; }
  virtual storage::ObMemstoreFreezer * memstore_freezer() { return nullptr; }
  virtual storage::checkpoint::ObCheckPointService * check_point_service() { return nullptr; }
  virtual storage::checkpoint::ObTabletGCService * tablet_gc_service() { return nullptr; }
  virtual compaction::ObTabletScheduler * tablet_scheduler() { return nullptr; }
  virtual compaction::ObMediumChecker * medium_checker() { return nullptr; }
  virtual storage::ObCompactionMemPool * compaction_mem_pool() { return nullptr; }
  virtual storage::ObDirectLoadMgr * direct_load_mgr() { return nullptr; }
  virtual share::ObDagScheduler * dag_scheduler() { return nullptr; }
  virtual storage::ObFreezeInfoMgr * freeze_info_mgr() { return nullptr; }
  virtual transaction::ObTxLoopWorker * tx_loop_worker() { return nullptr; }
  virtual storage::ObAccessService * access_service() { return nullptr; }
  virtual concurrency_control::ObMultiVersionGarbageCollector * multi_version_garbage_collector() { return nullptr; }
  virtual ObTestModule * test_module() { return nullptr; }
  virtual storage::ObEmptyReadBucket * empty_read_bucket() { return nullptr; }
  virtual rootserver::ObDBMSSchedService * dbms_sched_service() { return nullptr; }
  virtual oceanbase::common::ObOptStatMonitorManager * opt_stat_monitor_manager() { return nullptr; }
  virtual omt::ObSrsService * srs_service() { return nullptr; }
  virtual storage::ObTabletMemtableMgrPool * tablet_memtable_mgr_pool() { return nullptr; }
  virtual share::ObResourceLimitCalculator * resource_limit_calculator() { return nullptr; }
  virtual storage::ObGlobalIteratorPool * global_iterator_pool() { return nullptr; }
  virtual common::ObRbMemMgr * rb_mem_mgr() { return nullptr; }
  virtual share::ObPluginVectorIndexService * plugin_vector_index_service() { return nullptr; }
  virtual rootserver::ObDDLServiceLauncher * ddl_service_launcher() { return nullptr; }
  virtual rootserver::ObSystemPackageLoadService * system_package_load_service() { return nullptr; }
  virtual rootserver::ObDDLScheduler * ddl_scheduler() { return nullptr; }
  virtual omt::ObAiService * ai_service() { return nullptr; }
  virtual share::ObChangeStreamMgr * change_stream_mgr() { return nullptr; }
  // Generic byte-oriented plugin execution bridge. The default implementation
  // keeps core-only builds independent from the optional plugin loader.
  virtual int execute_plugin_function(
      const char *service_id,
      uint32_t abi_major,
      uint32_t required_minor,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count)
  {
    UNUSED(service_id);
    UNUSED(abi_major);
    UNUSED(required_minor);
    UNUSED(context);
    UNUSED(arguments);
    UNUSED(argument_count);
    return common::OB_NOT_SUPPORTED;
  }
  virtual int execute_plugin_extension(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      const seekdb_plugin_execution_context_v1 *context,
      const seekdb_plugin_execution_value_v1 *arguments,
      uint32_t argument_count)
  {
    UNUSED(kind);
    UNUSED(sql_name);
    UNUSED(context);
    UNUSED(arguments);
    UNUSED(argument_count);
    return common::OB_NOT_SUPPORTED;
  }
};

// Set to &OBSERVER once the server modules are created (boot), before
// any consumer thread reads a module. Low-layer code uses g_mp->xxx().
extern ObIModuleProvider *g_mp;

// Typed bridges for the remaining low-layer module access helpers.
template <class T> T server_module();
template <> inline storage::ObLSService *server_module<storage::ObLSService *>()
{ return g_mp->ls_service(); }
template <> inline memtable::ObLockWaitMgr *server_module<memtable::ObLockWaitMgr *>()
{ return g_mp->lock_wait_mgr(); }
template <> inline transaction::ObTransService *server_module<transaction::ObTransService *>()
{ return g_mp->trans_service(); }
template <> inline transaction::tablelock::ObTableLockService *server_module<transaction::tablelock::ObTableLockService *>()
{ return g_mp->table_lock_service(); }

template <class T> common::ObServerObjectPool<T> *server_obj_pool();
template <> inline common::ObServerObjectPool<oceanbase::storage::ObTableScanIterator> *server_obj_pool<oceanbase::storage::ObTableScanIterator>()
{ return g_mp->table_scan_iterator_obj_pool(); }

template <class T> inline T *borrow_server_object()
{
  return server_obj_pool<T>()->borrow_object();
}

template <class T> inline void return_server_object(T *object)
{
  server_obj_pool<T>()->return_object(object);
}

} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_RC_OB_MODULE_PROVIDER_H_
