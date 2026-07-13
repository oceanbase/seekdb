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

#ifndef OB_TENANT_BASE_H_
#define OB_TENANT_BASE_H_

#include "lib/ob_define.h"
#include "lib/ob_errno.h"
#include "lib/utility/ob_macro_utils.h"
#include "lib/worker.h"
#include "lib/ob_running_mode.h"
#include "lib/thread/threads.h"
#include "lib/allocator/ob_malloc.h"
#include "lib/task/ob_timer_service.h" // ObTimerService
#include "share/ob_tenant_role.h"//ObTenantRole
#include "common/mysqlclient/ob_tenant_oci_envs.h"
namespace oceanbase
{
namespace common {
  class ObTenantIOManager;
  template<typename T> class ObServerObjectPool;
  class ObOptStatMonitorManager;
  class ObRbMemMgr;
  class ObILobReadService;
}
namespace omt {
 class ObPxPools;
 class ObTenant;
 class ObSharedTimer;
 class ObTenantSrs;
 class ObTenantAiService;
}
namespace obmysql {
  class ObSqlNioServer;
}
namespace sql {
  namespace dtl {
    class ObTenantDfc;
    class ObDTLIntermResultManager;
  }
  class ObTenantSQLSessionMgr;
  class ObTenantSqlMemoryManager;
  class ObPlanMonitorNodeList;
  class ObPlanBaselineMgr;
  class ObDataAccessService;
  class ObPlanCache;
  class ObPsCache;
  class ObSQLCCLRuleManager;
}
namespace blocksstable {
  class ObSharedMacroBlockMgr;
}
namespace tmp_file {
  class ObTenantTmpFileManager;
}
namespace storage {
namespace mds {
class ObTenantMdsService;
}
  class ObLSService;
  class ObAccessService;
  class ObTenantFreezer;
  class ObTenantMetaMemMgr;
  class ObTenantStorageMetaService;
  class ObTenantFTPluginMgr;
  class ObTenantFreezeInfoMgr;
  class ObStorageHAService;
  class ObStorageHAHandlerService;
  class ObTenantSSTableMergeInfoMgr;
  class ObTenantTabletStatMgr;
  class ObTenantCompactionMemPool;
  namespace checkpoint {
    class ObCheckPointService;
    class ObTabletGCService;
  }
  class ObLobManager;
  class ObTableScanIterator;
  struct ObDDLMergeBucketLock;
  class ObTenantDirectLoadMgr;
  class ObEmptyReadBucket;
  class ObTabletMemtableMgrPool;

  class ObGlobalIteratorPool;
} // namespace storage

namespace transaction {
  class ObTransService;          // transaction service
  class ObTimestampService;
  class ObTimestampAccess;
  class ObTransIDService;
  class ObUniqueIDService;
  class ObTxLoopWorker;
  class ObTxCtx;
  namespace tablelock {
    class ObTableLockService;
  }
}
namespace concurrency_control {
  class ObMultiVersionGarbageCollector; // MVCC GC
}
namespace table
{
  class ObTTLService;
  class ObHTableLockMgr;
  class ObTableObjectPoolMgr;
  class ObTableGroupCommitMgr;
  class ObHTableRowkeyMgr;
  class ObTableClientInfoMgr;
}
namespace logservice
{
  class ObLogService;
}
namespace datadict
{
  class ObDataDictService;
}
namespace compaction
{
  class ObTenantCompactionProgressMgr;
  class ObServerCompactionEventHistory;
  class ObScheduleSuspectInfoMgr;
  class ObCompactionSuggestionMgr;
  class ObDiagnoseTabletMgr;
  class ObTenantMediumChecker;
  class ObTenantTabletScheduler;
  class ObTenantCompactionObjMgr;
}
namespace memtable
{
  class ObLockWaitMgr;
}
namespace rootserver
{
  class ObPrimaryMajorFreezeService;
  class ObRestoreMajorFreezeService;
  class ObDBMSSchedService;
  class ObDDLScheduler;
  class ObDDLServiceLauncher;
  class ObSysTenantLoadSysPackageService;
}
namespace observer
{
  class ObTenantMetaChecker;
  class ObTableLoadService;
  class ObTableLoadResourceService;
  class ObStartupAccelTaskHandler;
  class ObTabletTableUpdater;
  class ObTenantQueryRespTimeCollector;
  class ObTableQueryASyncMgr;
}
// for ObTenantSwitchGuard temporary use>>>>>>>>
namespace observer
{
  class ObAllVirtualTabletInfo;
  class ObAllVirtualTransCheckpointInfo;
  class ObAllVirtualTabletEncryptInfo;
  class ObAllVirtualTabletSSTableMacroInfo;
  class ObAllVirtualObjLock;
  class ObAllVirtualMemstoreInfo;
}
namespace storage {
  class MockTenantModuleEnv;
}

namespace share
{
class ObTestModule;
class ObTenantDagScheduler;
class ObTenantModuleInitCtx;
class ObGlobalAutoIncService;
class ObDagWarningHistoryManager;
class ObTenantErrsimModuleMgr;
class ObTenantErrsimEventMgr;
class ObSharedMemAllocMgr;
class ObIndexUsageInfoMgr;
class ObResourceLimitCalculator;

class ObPluginVectorIndexService;
class ObChangeStreamMgr;
namespace schema
{
  class ObTenantSchemaService;
}
namespace detector
{
  class ObDeadLockDetectorMgr;
}

#ifdef ERRSIM
#define TenantErrsimModule share::ObTenantErrsimModuleMgr*,
#define TenantErrsimEvent share::ObTenantErrsimEventMgr*,
#else
#define TenantErrsimModule
#define TenantErrsimEvent
#endif

#define TenantDiskSpaceManager
#define TenantFileManager
#define SSMicroCachePrewarmService
#define SSMicroCache
#define TenantCompactionObjMgr
#define PublicBlockGCService
#define StorageCachePolicyService
// List the types of tenant-local variables that need to be added here, the tenant will create an instance for each type.
// The lifecycle of each instance is driven by ObServer's explicit obs_* routines.
// Use the MTL interface to obtain an instance.
using ObTxCtxObjPool = common::ObServerObjectPool<transaction::ObTxCtx>;
using ObTableScanIteratorObjPool = common::ObServerObjectPool<oceanbase::storage::ObTableScanIterator>;
#define MTL_MEMBERS                                  \
  MTL_LIST(                                          \
      omt::ObSharedTimer*,                           \
      oceanbase::sql::ObTenantSQLSessionMgr*,        \
      storage::ObTenantMetaMemMgr*,                  \
      storage::ObTenantFTPluginMgr*,                 \
      ObTxCtxObjPool*,                        \
      ObTableScanIteratorObjPool*,                   \
      common::ObTenantIOManager*,                    \
      storage::mds::ObTenantMdsService*,             \
      blocksstable::ObSharedMacroBlockMgr*,          \
      share::ObSharedMemAllocMgr*,                   \
      transaction::ObTransService*,                  \
      logservice::ObLogService*,                     \
      TenantDiskSpaceManager                         \
      TenantFileManager                              \
      SSMicroCache                                   \
      SSMicroCachePrewarmService                     \
      StorageCachePolicyService                      \
      storage::ObLSService*,                         \
      storage::ObTenantStorageMetaService*,          \
      tmp_file::ObTenantTmpFileManager*,             \
      compaction::ObTenantCompactionProgressMgr*,    \
      compaction::ObServerCompactionEventHistory*,   \
      storage::ObTenantTabletStatMgr*,               \
      memtable::ObLockWaitMgr*,                      \
      transaction::tablelock::ObTableLockService*,   \
      rootserver::ObPrimaryMajorFreezeService*,      \
      rootserver::ObRestoreMajorFreezeService*,      \
      observer::ObTenantMetaChecker*,                \
      observer::ObTabletTableUpdater*,               \
      storage::ObStorageHAHandlerService*,           \
      storage::ObTenantSSTableMergeInfoMgr*,         \
      share::ObDagWarningHistoryManager*,            \
      compaction::ObScheduleSuspectInfoMgr*,         \
      compaction::ObCompactionSuggestionMgr*,        \
      compaction::ObDiagnoseTabletMgr *,             \
      storage::ObLobManager*,                        \
      common::ObILobReadService*,                    \
      share::ObGlobalAutoIncService*,                \
      share::detector::ObDeadLockDetectorMgr*,       \
      transaction::ObTimestampService*,              \
      transaction::ObTimestampAccess*,               \
      transaction::ObTransIDService*,                \
      transaction::ObUniqueIDService*,               \
      sql::ObPlanBaselineMgr*,                       \
      sql::ObPsCache*,                               \
      sql::ObPlanCache*,                             \
      sql::dtl::ObTenantDfc*,                        \
      omt::ObPxPools*,                               \
      lib::Worker::CompatMode,                       \
      sql::ObTenantSqlMemoryManager*,                \
      sql::dtl::ObDTLIntermResultManager*,           \
      sql::ObPlanMonitorNodeList*,                   \
      sql::ObDataAccessService*,                     \
      share::schema::ObTenantSchemaService*,         \
      storage::ObTenantFreezer*,                     \
      storage::checkpoint::ObCheckPointService *,    \
      storage::checkpoint::ObTabletGCService *,      \
      compaction::ObTenantTabletScheduler*,          \
      compaction::ObTenantMediumChecker*,            \
      storage::ObTenantCompactionMemPool*,           \
      TenantCompactionObjMgr                         \
      storage::ObDDLMergeBucketLock*,                \
      storage::ObTenantDirectLoadMgr*,               \
      share::ObTenantDagScheduler*,                  \
      storage::ObStorageHAService*,                  \
      storage::ObTenantFreezeInfoMgr*,               \
      transaction::ObTxLoopWorker *,                 \
      storage::ObAccessService*,                     \
      datadict::ObDataDictService*,                  \
      observer::ObTableLoadService*,                 \
      observer::ObTableLoadResourceService*,         \
      concurrency_control::ObMultiVersionGarbageCollector*, \
      ObTestModule*,                                 \
      storage::ObEmptyReadBucket*,                  \
      rootserver::ObDBMSSchedService*,              \
      TenantErrsimModule                            \
      TenantErrsimEvent                             \
      oceanbase::common::ObOptStatMonitorManager*,  \
      omt::ObTenantSrs*,                            \
      table::ObHTableLockMgr*,                      \
      table::ObTTLService*,                         \
      table::ObTableObjectPoolMgr*,                \
      share::ObIndexUsageInfoMgr*,                  \
      storage::ObTabletMemtableMgrPool*,            \
      PublicBlockGCService                          \
      share::ObResourceLimitCalculator*,            \
      storage::ObGlobalIteratorPool*,                \
      common::ObRbMemMgr*,                           \
      share::ObPluginVectorIndexService*,            \
      observer::ObTenantQueryRespTimeCollector*,     \
      table::ObTableGroupCommitMgr*,                 \
      observer::ObTableQueryASyncMgr*,               \
      table::ObTableClientInfoMgr*,                  \
      table::ObHTableRowkeyMgr*,                     \
      rootserver::ObDDLServiceLauncher*,             \
      rootserver::ObSysTenantLoadSysPackageService*, \
      rootserver::ObDDLScheduler*,                   \
      sql::ObSQLCCLRuleManager*              ,       \
      omt::ObTenantAiService*,                       \
      share::ObChangeStreamMgr*                        \
  )
// Get tenant epoch id
#define MTL_EPOCH_ID() share::ObTenantEnv::get_tenant_local()->get_epoch()
// tenant switchover epoch
#define MTL_GET_SWITCHOVER_EPOCH() share::ObTenantEnv::get_tenant()->get_switchover_epoch()
#define MTL_SET_SWITCHOVER_EPOCH(switchover_epoch) share::ObTenantEnv::get_tenant()->set_switchover_epoch(switchover_epoch)
// Get whether it is the primary tenant
#define MTL_TENANT_ROLE_CACHE_IS_PRIMARY() share::ObTenantEnv::get_tenant()->is_primary_tenant()
//Since the previous tenant was default as the primary database, this is a compatibility writing method
// Tenant role is initialized successfully, not invalid
#define MTL_TENANT_ROLE_CACHE_IS_INVALID() share::ObTenantEnv::get_tenant()->is_invalid_tenant()
// Is the tenant in the process of recovery
// Update tenant role
#define MTL_SET_TENANT_ROLE_CACHE(tenant_role) share::ObTenantEnv::get_tenant()->set_tenant_role(tenant_role)
// get tenant role
#define MTL_GET_TENANT_ROLE_CACHE() share::ObTenantEnv::get_tenant()->get_tenant_role()
// Get tenant module
#define MTL_CTX() (share::ObTenantEnv::get_tenant())
// Get tenant initialization parameters, used only during initialization
#define MTL_INIT_CTX() (share::ObTenantEnv::get_tenant_local()->get_mtl_init_ctx())
// Get tenant module (single sys tenant)
#define MTL_WITH_CHECK(TYPE) ::oceanbase::share::mtl_checked<TYPE>()
#define MTL_IS_MINI_MODE() share::ObTenantEnv::get_tenant()->is_mini_mode()
#define MTL_CPU_COUNT() share::ObTenantEnv::get_tenant()->unit_max_cpu()
#define MTL_MEM_SIZE() share::ObTenantEnv::get_tenant()->unit_memory_size()
// Set tenant prepare gc state
#define MTL_SET_TENANT_PREPARE_GC_STATE() share::ObTenantEnv::get_tenant()->set_prepare_unit_gc()
// Get tenant prepare gc status
#define MTL_GET_TENANT_PREPARE_GC_STATE() share::ObTenantEnv::get_tenant()->is_prepare_unit_gc()
// Per-module bind + lifecycle iteration machinery removed. ObServer owns the modules
// and brings them up via explicit ordered obs_construct/init/start/stop/wait/destroy
// routines (omt/ob_multi_tenant.cpp). The slot below is now a transitional read-alias only.
// Get the tenant-local instance
//
// Need to be used in conjunction with the tenant context to obtain the specified type of tenant local instance.
// For example, MTL(ObPxPools*) can be used to obtain the current tenant's PX pool.
// MTL(TYPE) read macro removed; low-layer code uses share::g_mp->xxx().
// Helper function
#define MTL_LIST(...) __VA_ARGS__

//======================================================================//
// Expose the Tenant class to various modules, place the interfaces to be exposed here (tenant-level service, mgr, etc.)
class ObTenantBase : public lib::IRunWrapper
{
// get_tenant when omt internally adds a read lock to the tenant,
// ObTenantSpaceFetcher destructor needs to unlock,
// Therefore expose the unlock interface to ObTenantSpaceFetcher
friend class ObTenantSpaceFetcher;
friend class omt::ObTenant;
friend class ObTenantEnv;

	template<class T> struct Identity {};

public:
  virtual int pre_run() override;
  virtual int end_run() override;

  double unit_max_cpu() const { return unit_max_cpu_; }
  double unit_min_cpu() const { return unit_min_cpu_; }
  int64_t set_unit_memory_size(int64_t memory_size)
  {
    int64_t orig_size = unit_memory_size_;
    unit_memory_size_ = memory_size;
    return orig_size;
  }
  int64_t unit_memory_size() const { return unit_memory_size_; }
  bool is_mini_mode() const { return lib::is_mini_mode(); }
  void set_prepare_unit_gc()
  {
    // only set marked_prepare_gc_ts_ once
    if (marked_prepare_gc_ts_ <= 0) {
      marked_prepare_gc_ts_ = ObTimeUtility::current_time();
    }
  }
  void clear_prepare_unit_gc()
  {
    marked_prepare_gc_ts_ = 0;
  }
  bool is_prepare_unit_gc() const { return marked_prepare_gc_ts_ > 0; }
  int64_t get_prepare_unit_gc_ts() const { return marked_prepare_gc_ts_; }
  int64_t get_max_session_num(const int64_t rl_max_session_num);

public:
  ObTenantBase(const int64_t epoch = 0);
  ObTenantBase &operator=(const ObTenantBase &ctx);
  int init();
  void destroy();
  virtual inline uint64_t id() const override { return 1; }
  OB_INLINE int64_t get_epoch() const { return epoch_; }
  const ObTenantModuleInitCtx *get_mtl_init_ctx() const { return mtl_init_ctx_; }

  void set_tenant_role(const share::ObTenantRole::Role tenant_role_value)
  {
    if (get_tenant_role() != tenant_role_value) {
      SHARE_LOG(INFO, "set tenant role", K(tenant_role_value), K(tenant_role_value_));
    }
    (void)ATOMIC_STORE(&tenant_role_value_, tenant_role_value);
  }

  share::ObTenantRole::Role get_tenant_role() const
  {
    return ATOMIC_LOAD(&tenant_role_value_);
  }

  bool is_primary_tenant()
  {
    return share::is_primary_tenant(ATOMIC_LOAD(&tenant_role_value_));
  }


  bool is_restore_tenant()
  {
    return share::is_restore_tenant(ATOMIC_LOAD(&tenant_role_value_));
  }

  bool is_invalid_tenant()
  {
    return share::is_invalid_tenant(ATOMIC_LOAD(&tenant_role_value_));
  }

  void set_switchover_epoch(const int64_t switchover_epoch)
  {
    int64_t cached_switchover_epoch = get_switchover_epoch();
    if (OB_INVALID_VERSION != switchover_epoch && cached_switchover_epoch < switchover_epoch) {
      SHARE_LOG(INFO, "try set switchover_epoch", K(switchover_epoch), K(cached_switchover_epoch));
      ATOMIC_BCAS(&switchover_epoch_, cached_switchover_epoch, switchover_epoch);
    }
  }

  int64_t get_switchover_epoch() const
  {
    return ATOMIC_LOAD(&switchover_epoch_);
  }

  /// Called after publish_schema() for this tenant. Modules override in
  /// ObTenant to hook schema publish events (e.g., wake CSFetcher from IDLE).
  virtual void on_schema_publish() {}

  // Typed slot get<T>/set<T> removed (no slot storage anymore).

private:
  // Per-tenant module lifecycle (create/init/start/stop/wait/destroy_mtl_module)
  // removed; ObServer owns the modules and drives them via explicit obs_* routines.
  // The slot below survives only as a transitional read-alias for the not-yet-migrated
  // MTL(T*) readers + the oblib get_di_container bridge (removed in the next wave).

  // Per-tenant typed module slots removed (ObServer owns the modules).

protected:
  virtual int unlock()
  {
    return OB_SUCCESS;
  }

protected:
  int64_t epoch_;
  bool inited_;
  bool created_;
  share::ObTenantModuleInitCtx *mtl_init_ctx_;
  share::ObTenantRole::Role tenant_role_value_;
  // max/min cpu read from unit
  double unit_max_cpu_;
  double unit_min_cpu_;
  int64_t unit_memory_size_;
  int64_t switchover_epoch_;

private:
  int64_t marked_prepare_gc_ts_;
};

using ReleaseCbFunc = std::function<int ()>;
extern int get_tenant_base_with_lock(ObTenantBase *&ctx, ReleaseCbFunc &release_cb);

// g_tenant_ctx is a dummy to avoid nullptr deref before create_tenant_module().
// Once g_tenant_ptr = this (after create_mtl_module), all MTL reads go directly
// to the real ObTenant — no copies, no dual objects.
extern ObTenantBase g_tenant_ctx;
inline ObTenantBase *g_tenant_ptr = &g_tenant_ctx;
// Non-tenant readiness flag, set true once the (single) sys tenant's MTL
// modules are created. Replaces MTL_SWITCH's switch_to() readiness guard with a
// de-tenanted run-once guard (MOD_SCOPE; readiness-by-construction).
inline bool g_modules_ready = false;

class ObTenantEnv
{
public:
  static void set_tenant(ObTenantBase *ctx);
  static inline ObTenantBase *get_tenant()
  {
    return g_tenant_ptr;
  }
  static inline ObTenantBase *get_tenant_local()
  {
    return g_tenant_ptr;
  }
  // ObTenantEnv::mtl<T> slot accessors removed (use share::g_mp->xxx()).
};

class ObTenantSwitchGuard
{
friend class omt::ObTenant;
friend class storage::MockTenantModuleEnv;

friend ObTenantSwitchGuard _make_tenant_switch_guard();
private:
  ObTenantSwitchGuard() { reset(); }
public:
  ObTenantSwitchGuard(ObTenantBase *ctx);
  // just for make guard
  ObTenantSwitchGuard(const ObTenantSwitchGuard &other) {
    UNUSED(other);
    reset();
  }
  ~ObTenantSwitchGuard()
  {
    release();
  }
  int switch_to(bool need_check_allow = true);
  int switch_to(ObTenantBase *ctx);
  void release();
  void reset()
  {
    loop_num_ = 0;
    on_switch_ = false;
    stash_tenant_ = nullptr;
    release_cb_ = nullptr;
  }
  // for MTL_SWITCH
  int loop_num_;
private:
  bool on_switch_;
  ObTenantBase *stash_tenant_;
  ReleaseCbFunc release_cb_;
};

inline ObTenantSwitchGuard _make_tenant_switch_guard()
{
  static ObTenantSwitchGuard _guard;
  return _guard;
}

#define MAKE_TENANT_SWITCH_SCOPE_GUARD(guard) \
  share::ObTenantSwitchGuard guard = share::_make_tenant_switch_guard()

// De-tenanted run-once readiness guard (was MTL_SWITCH(tenant) -> switch_to).
// Keeps the for-once structure so any break/continue in the body behaves exactly as
// before; gates on the non-tenant g_modules_ready instead of a tenant switch.
#define MOD_SCOPE \
  for (int64_t _mod_loop = 0; _mod_loop == 0; ++_mod_loop) \
    if (OB_LIKELY(::oceanbase::share::g_modules_ready))

  inline void *mtl_malloc(int64_t nbyte, const common::ObMemAttr &attr)
  {
    common::ObMemAttr inner_attr = attr;
    if (true &&
        nullptr != MTL_CTX()) {
      
    }
    return ob_malloc(nbyte, inner_attr);
  }

  inline void *mtl_malloc(int64_t nbyte, const lib::ObLabel &label)
  {
    common::ObMemAttr attr;
    attr.label_ = label;
    return mtl_malloc(nbyte, attr);
  }

  inline void mtl_free(void *ptr)
  {
    return ob_free(ptr);
  }

  inline void *mtl_malloc_align(int64_t alignment, int64_t nbyte, const common::ObMemAttr &attr)
  {
    common::ObMemAttr inner_attr = attr;
    if (true &&
        nullptr != MTL_CTX()) {
      
    }
    return ob_malloc_align(alignment, nbyte, inner_attr);
  }

  inline void *mtl_malloc_align(int64_t alignment , int64_t byte, const lib::ObLabel &label)
  {
    common::ObMemAttr attr;
    attr.label_ = label;
    return mtl_malloc_align(alignment, byte, attr);
  }

  inline void mtl_free_align(void *ptr)
  {
    return ob_free_align(ptr);
  }

  #define MTL_NEW(T, label, ...)                                \
  ({                                                            \
    T* ret = NULL;                                              \
    void *buf = oceanbase::share::mtl_malloc(sizeof(T), label); \
    if (OB_NOT_NULL(buf))                                       \
    {                                                           \
      ret = new(buf) T(__VA_ARGS__);                            \
    }                                                           \
    ret;                                                        \
  })

  #define MTL_DELETE(T, label, ptr)               \
    do{                                           \
      if (NULL != ptr)                            \
      {                                           \
        ptr->~T();                                \
        oceanbase::share::mtl_free(ptr);          \
        ptr = NULL;                               \
      }                                           \
    } while(0)


#define mtl_sop_borrow(type)                                                                                    \
  ({                                                                                                            \
    type *iter = ::oceanbase::share::mtl_obj_pool<type>()->borrow_object();                                       \
    (iter);                                                                                                     \
  })

#define mtl_sop_return(type, ptr)                                                                               \
  do {                                                                                                          \
    ::oceanbase::share::mtl_obj_pool<type>()->return_object(ptr);                                                 \
  } while (false)

#define mtl_sop_borrow_checked(type)                                                                                    \
  ({                                                                                                            \
    type *iter = ::oceanbase::share::mtl_obj_pool<type>()->borrow_object();                                       \
    (iter);                                                                                                     \
  })

#define mtl_sop_return_checked(type, iter)                                                                               \
  do {                                                                                                          \
    ::oceanbase::share::mtl_obj_pool<type>()->return_object(iter);                                                 \
  } while (false)

} // end of namespace share

} // end of namespace oceanbase


#endif // OB_TENANT_BASE_H_
