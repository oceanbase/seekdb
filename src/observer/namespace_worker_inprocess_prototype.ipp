// Ticket 05c: serve forked namespaces (ns>1) inside the shared process.
//
// Query-side adapters bind each SQL session to its native storage context.
// The typed service calls route logical namespace ids at that boundary.
//
// Per-namespace services (schema service, plan cache) live in the namespace
// runtime's service slots and are constructed lazily on first use.
#include "namespace/namespace.h"
#include "data_plane/ob_i_range_service.h"
#include "observer/schema/ob_schema_service_sql_impl.h"
#include "rootserver/ob_max_id_cache_adapter.h"
#include "rootserver/ob_local_management_service.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h"
#include "rootserver/fork_table/namespace_fork_kernel_prototype.h"
#include "share/ob_autoincrement_service.h"
#include "share/schema/ob_schema_runtime_service.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "sql/optimizer/stat/ob_opt_stat_manager.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "observer/virtual_table/ob_virtual_data_access_service.h"
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
#include <shared_mutex>
#include <thread>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
class RootSchemaLifecycle final : public INamespaceSchemaLifecycle
{
public:
  int refresh() override { return OB_SUCCESS; }
  int fetch_version(bool published, bool core_version, int64_t &version) override
  {
    auto &service = ObMultiVersionSchemaService::get_instance();
    return published
        ? service.get_published_schema_version(version, core_version)
        : service.get_runtime_refreshed_schema_version(version, core_version);
  }
  int begin_change() override { return OB_SUCCESS; }
  int finish_change(int64_t) override { return OB_SUCCESS; }
  int publish(ObMultiVersionSchemaService &, int64_t base_schema_version,
              int64_t &published_schema_version) override
  {
    published_schema_version = base_schema_version;
    return OB_SUCCESS;
  }
};
class ForkSchemaLifecycle final : public INamespaceSchemaLifecycle
{
public:
  explicit ForkSchemaLifecycle(uint64_t ns) : ns_(ns) {}
  int refresh() override { return inprocess_refresh_schema(ns_); }
  int fetch_version(bool, bool, int64_t &version) override
  {
    return storage::NamespaceForkKernelPrototype::namespace_schema_version(ns_, version);
  }
  int begin_change() override
  {
    return storage::NamespaceForkKernelPrototype::begin_schema_change(ns_);
  }
  int finish_change(int64_t committed_schema_version) override
  {
    return storage::NamespaceForkKernelPrototype::finish_schema_change(
        ns_, committed_schema_version);
  }
  int publish(ObMultiVersionSchemaService &schema_service,
              int64_t base_schema_version, int64_t &published_schema_version) override
  {
    if (const char *delay_text = std::getenv("SEEKDB_NAMESPACE_DDL_PUBLISH_DELAY_US")) {
      char *end = nullptr;
      const int64_t delay_us = std::strtoll(delay_text, &end, 10);
      if (*delay_text && end && !*end && delay_us > 0 && delay_us <= 2000000) {
        ob_usleep(delay_us);
      }
    }
    return sync_namespace_schema_delta(
        ns_, schema_service, base_schema_version, published_schema_version);
  }
private:
  uint64_t ns_;
};
INamespaceSchemaLifecycle *namespace_schema_lifecycle(uint64_t namespace_id)
{
  ns::NamespaceRuntime *runtime = nullptr;
  return ns::namespace_registry().get(namespace_id, runtime) && runtime != nullptr
      ? static_cast<INamespaceSchemaLifecycle *>(
            runtime->service(ns::NamespaceRuntime::SCHEMA_LIFECYCLE))
      : nullptr;
}
int refresh_session_schema(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  if (runtime == nullptr) { return OB_NOT_INIT; }
  auto *lifecycle = static_cast<INamespaceSchemaLifecycle *>(
      runtime->service(ns::NamespaceRuntime::SCHEMA_LIFECYCLE));
  return lifecycle == nullptr ? OB_NOT_INIT : lifecycle->refresh();
}
int begin_namespace_schema_change(uint64_t namespace_id)
{
  auto *lifecycle = namespace_schema_lifecycle(namespace_id);
  return lifecycle == nullptr ? OB_NOT_INIT : lifecycle->begin_change();
}
int finish_namespace_schema_change(uint64_t namespace_id,
                                   int64_t committed_schema_version)
{
  auto *lifecycle = namespace_schema_lifecycle(namespace_id);
  return lifecycle == nullptr ? OB_NOT_INIT
      : lifecycle->finish_change(committed_schema_version);
}
int publish_namespace_schema_change(uint64_t namespace_id,
    ObMultiVersionSchemaService &schema_service,
    int64_t base_schema_version, int64_t &published_schema_version)
{
  auto *lifecycle = namespace_schema_lifecycle(namespace_id);
  return lifecycle == nullptr ? OB_NOT_INIT
      : lifecycle->publish(schema_service, base_schema_version, published_schema_version);
}
rootserver::ObIRootserverLocalRuntime *root_namespace_ddl_runtime()
{
  static InProcessRootserverLocalRuntime runtime(1);
  return &runtime;
}
// ---------------------------------------------------------------------------
// In-process storage context: the channel-free twin of DirectStorageContext.
// Owns the native storage session, open scans and write engine for one SQL
// session of one namespace. Lives on the session's SessionBinding.
// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// Storage service stubs: one stateless global set serves every in-process
// namespace; the serving namespace travels with the bound session context.
// ---------------------------------------------------------------------------
InProcessTabletScan inprocess_scan;
InProcessLobReadService inprocess_lob_read;
NativeWriteMaterialization native_write_materialization;
ForkWriteMaterialization fork_write_materialization;
InProcessDmlService native_inprocess_dml(native_write_materialization);
InProcessDmlService fork_inprocess_dml(fork_write_materialization);
InProcessWriteContext inprocess_write_context;
InProcessTransactionService inprocess_transactions;
InProcessInnerConnectionLockRuntime inprocess_inner_locks;
transaction::tablelock::ObIInnerConnectionLockRuntime *inprocess_lock_runtime(
    common::sqlclient::ObISQLConnection *conn)
{
  auto *inner = static_cast<ObInnerSQLConnection *>(conn);
  return inner != nullptr && in_process_session_ns(&inner->get_session()) > 0
      ? static_cast<transaction::tablelock::ObIInnerConnectionLockRuntime *>(
            &inprocess_inner_locks)
      : share::server_service<transaction::tablelock::ObIInnerConnectionLockRuntime>();
}
common::ObITabletScan *effective_tablet_scan(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<common::ObITabletScan *>(
      runtime->service(ns::NamespaceRuntime::TABLET_SCAN)) : nullptr;
}
common::ObILobReadService *effective_lob_read_service(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<common::ObILobReadService *>(
      runtime->service(ns::NamespaceRuntime::LOB_READ_SERVICE)) : nullptr;
}
data_plane::ObIDmlService *effective_dml_service(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime != nullptr
      ? static_cast<data_plane::ObIDmlService *>(
            runtime->service(ns::NamespaceRuntime::DML_SERVICE))
      : nullptr;
}
data_plane::ObIWriteContextService *effective_write_context_service(
    sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<data_plane::ObIWriteContextService *>(
      runtime->service(ns::NamespaceRuntime::WRITE_CONTEXT_SERVICE)) : nullptr;
}
data_plane::ObITransactionService *effective_transaction_service(
    sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<data_plane::ObITransactionService *>(
      runtime->service(ns::NamespaceRuntime::TRANSACTION_SERVICE)) : nullptr;
}
sql::ObPlanCache *effective_plan_cache(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<sql::ObPlanCache *>(
      runtime->service(ns::NamespaceRuntime::PLAN_CACHE)) : nullptr;
}
common::ObOptStatManager *effective_opt_stat_manager(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<common::ObOptStatManager *>(
      runtime->service(ns::NamespaceRuntime::OPT_STAT_MANAGER)) : nullptr;
}
common::ObOptStatMonitorManager *effective_opt_stat_monitor_manager(
    sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<common::ObOptStatMonitorManager *>(
      runtime->service(ns::NamespaceRuntime::OPT_STAT_MONITOR_MANAGER)) : nullptr;
}
query::ObIRootCommandService *effective_root_command_service(
    sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  void *service = runtime
      ? runtime->service(ns::NamespaceRuntime::ROOT_COMMAND_SERVICE) : nullptr;
  return runtime != nullptr
      ? static_cast<query::ObIRootCommandService *>(
            static_cast<rootserver::ObLocalManagementService *>(service))
      : nullptr;
}
// ---------------------------------------------------------------------------
// Per-namespace service group, constructed lazily on first use (ticket 05c).
// The composition mirrors the worker bootstrap: a routing sql proxy pins the
// namespace on inner SQL, the schema service instance gets the worker-style
// static system baseline, and recovery reconciles a possibly dirty fence.
// ---------------------------------------------------------------------------
class NamespaceRoutingSqlProxy final : public common::ObMySQLProxy
{
public:
  uint64_t target_namespace() const override { return ns_; }
  int init_routed(uint64_t ns, bool is_ddl)
  {
    ns_ = ns;
    return ObCommonSqlProxy::init(is_ddl);
  }
  int acquire_connection(common::sqlclient::ObISQLConnectionGuard &conn,
                         const int32_t group_id) override
  {
    int ret = ObCommonSqlProxy::acquire_connection(conn, group_id);
    ns::NamespaceRuntime *runtime = nullptr;
    if (OB_SUCC(ret) && (!ns::namespace_registry().get(ns_, runtime)
        || runtime == nullptr)) {
      ret = OB_ERR_UNEXPECTED;
    } else if (OB_SUCC(ret)) {
      static_cast<ObInnerSQLConnection *>(conn.get_ptr())->get_session().set_ns_runtime(runtime);
    }
    return ret;
  }
private:
  uint64_t ns_ = 0;
};
// The ctor is protected to enforce the THE_ONE singleton; per-ns instances
// subclass it open (same pattern as the ticket-04 spike).
class InProcessSchemaService final : public share::schema::ObMultiVersionSchemaService {};
// The shared ObService async refresh task refreshes THE_ONE only. A forked
// namespace instead refreshes its own instance synchronously on the calling
// thread, under the serving scope its ambient id translation needs.
class InProcessSchemaRefreshScheduler final : public share::schema::ObISchemaRefreshScheduler
{
public:
  InProcessSchemaRefreshScheduler(uint64_t ns,
                                  share::schema::ObMultiVersionSchemaService &schema)
      : ns_(ns), schema_(schema) {}
  int schedule_refresh_at_least(const int64_t schema_version) override
  {
    InProcessServingScope serving(ns_);
    return schema_.refresh_and_add_schema(false);
  }
private:
  uint64_t ns_;
  share::schema::ObMultiVersionSchemaService &schema_;
};
class InProcessTabletAutoincrementService final : public share::ObITabletAutoincrementService
{
public:
  explicit InProcessTabletAutoincrementService(uint64_t ns) : ns_(ns) {}
  int next_value(const common::ObTabletID &tablet_id, uint64_t &value) override
  {
    if (!tablet_id.is_valid()) { return OB_INVALID_ARGUMENT; }
    common::ObTabletID storage_tablet_id = tablet_id;
    int ret = route_tablet_id(ns_, storage_tablet_id);
    if (OB_SUCC(ret)) {
      auto *service = share::server_service<share::ObITabletAutoincrementService>();
      ret = service == nullptr ? OB_NOT_INIT : service->next_value(storage_tablet_id, value);
    }
    return ret;
  }
private:
  uint64_t ns_;
};
class InProcessRangeService;
extern InProcessRangeService native_inprocess_ranges;
void register_root_namespace_storage_services(ns::NamespaceRuntime &runtime)
{
  static InProcessDirectInsertService direct_insert;
  static DirectInsertRegistry direct_insert_registry;
  static InProcessTabletAutoincrementService tablet_autoincrement(1);
  static RootSchemaLifecycle schema_lifecycle;
  static RootTableLockTabletRouter table_lock_tablet_router;
  runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_SERVICE, &direct_insert);
  runtime.set_service(ns::NamespaceRuntime::DML_SERVICE, &native_inprocess_dml);
  runtime.set_service(ns::NamespaceRuntime::RANGE_SERVICE, &native_inprocess_ranges);
  runtime.set_service(ns::NamespaceRuntime::TABLET_SCAN, &inprocess_scan);
  runtime.set_service(ns::NamespaceRuntime::LOB_READ_SERVICE, &inprocess_lob_read);
  runtime.set_service(ns::NamespaceRuntime::WRITE_CONTEXT_SERVICE, &inprocess_write_context);
  runtime.set_service(ns::NamespaceRuntime::TRANSACTION_SERVICE, &inprocess_transactions);
  runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_REGISTRY,
      &direct_insert_registry);
  runtime.set_service(ns::NamespaceRuntime::TABLET_AUTOINCREMENT_SERVICE,
      &tablet_autoincrement);
  runtime.set_service(ns::NamespaceRuntime::SCHEMA_LIFECYCLE,
      &schema_lifecycle);
  runtime.set_service(ns::NamespaceRuntime::TABLE_LOCK_TABLET_ROUTER,
      &table_lock_tablet_router);
}
class IRangeSchemaPolicy
{
public:
  virtual ~IRangeSchemaPolicy() = default;
  virtual bool resolve_logical_schema(uint64_t table_id) const = 0;
};
class NativeRangeSchemaPolicy final : public IRangeSchemaPolicy
{
public:
  bool resolve_logical_schema(uint64_t table_id) const override
  {
    return !is_inner_table(table_id);
  }
};
class ForkRangeSchemaPolicy final : public IRangeSchemaPolicy
{
public:
  bool resolve_logical_schema(uint64_t) const override { return true; }
};
class InProcessRangeService final : public data_plane::ObIRangeService
{
public:
  explicit InProcessRangeService(const IRangeSchemaPolicy &schema_policy)
      : schema_policy_(schema_policy) {}
  int get_multi_ranges_cost(const common::ObTabletID &tablet, int64_t timeout,
      const common::ObIArray<common::ObStoreRange> &ranges, int64_t &size) override
  {
    return invoke('C', tablet, timeout, ranges, 0, nullptr, nullptr, size);
  }
  int split_multi_ranges(const common::ObTabletID &tablet, int64_t timeout,
      const common::ObIArray<common::ObStoreRange> &ranges, int64_t tasks,
      common::ObIAllocator &allocator,
      common::ObArrayArray<common::ObStoreRange> &split) override
  {
    int64_t unused_size = 0;
    return invoke('S', tablet, timeout, ranges, tasks, &allocator, &split, unused_size);
  }
private:
  int invoke(char operation, const common::ObTabletID &logical_tablet, int64_t timeout,
      const common::ObIArray<common::ObStoreRange> &ranges, int64_t tasks,
      common::ObIAllocator *allocator,
      common::ObArrayArray<common::ObStoreRange> *split, int64_t &size)
  {
    if (timeout <= 0) { return OB_TIMEOUT; }
    if (!logical_tablet.is_valid() || ranges.empty()
        || (operation == 'S' && (tasks <= 0 || allocator == nullptr || split == nullptr))) {
      return OB_INVALID_ARGUMENT;
    }
    auto *sql_session = THIS_WORKER.get_session();
    if (sql_session == nullptr || sql_session->ns_runtime() == nullptr) {
      return OB_NOT_INIT;
    }
    StorageSessionScope scope(sql_session);
    if (scope.error()) { return scope.error(); }
    const uint64_t logical_table_id = ranges.at(0).get_table_id();
    StorageSpaceHandle storage_space = active_worker_storage_space();
    ObSchemaGetterGuard guard;
    const ObTableSchema *logical_schema = nullptr;
    const bool has_logical_schema = serves_namespace_schema()
        && !storage::NamespaceForkKernelPrototype::is_encoded_id(logical_table_id)
        && schema_policy_.resolve_logical_schema(logical_table_id);
    int ret = OB_SUCCESS;
    if (has_logical_schema) {
      auto *schema_service = sql_session != nullptr
          ? sql_session->effective_schema_service()
          : nullptr;
      ret = schema_service == nullptr ? OB_NOT_INIT
          : schema_service->get_runtime_schema_guard(guard);
      if (OB_SUCC(ret)) { ret = guard.get_table_schema(logical_table_id, logical_schema); }
      if (OB_SUCC(ret) && (logical_schema == nullptr
          || logical_schema->get_table_id() != logical_table_id)) {
        ret = OB_SCHEMA_EAGAIN;
      }
      if (OB_SUCC(ret)) {
        ret = worker_storage_space_for_schema(*logical_schema, guard, storage_space);
      }
    }
    common::ObTabletID storage_tablet = logical_tablet;
    if (OB_SUCC(ret) && storage_space.is_namespace()) {
      if (has_logical_schema) {
        ret = route_tablet_id(storage_space, storage_tablet);
      } else if (!is_inner_table(logical_table_id)) {
        ret = OB_INVALID_ARGUMENT;
      }
    }
    common::ObSEArray<common::ObStoreRange, 4> storage_ranges;
    for (int64_t i = 0; OB_SUCC(ret) && i < ranges.count(); ++i) {
      if (ranges.at(i).get_table_id() != logical_table_id) {
        ret = OB_INVALID_ARGUMENT;
      } else {
        common::ObStoreRange range = ranges.at(i);
        ret = storage_ranges.push_back(range);
      }
    }
    if (OB_SUCC(ret)
        && storage::NamespaceForkKernelPrototype::is_encoded_id(storage_tablet.id())) {
      common::ObTabletID physical;
      int64_t redirect_cap = 0;
      ret = storage::NamespaceForkKernelPrototype::resolve_read_tablet(
          storage_tablet, physical, redirect_cap);
      if (OB_SUCC(ret)) { storage_tablet = physical; }
    }
    const int64_t now = ObTimeUtility::current_time();
    const int64_t deadline = timeout > INT64_MAX - now ? INT64_MAX : now + timeout;
    const int64_t remaining = std::min(deadline, THIS_WORKER.get_timeout_ts()) - now;
    if (OB_SUCC(ret) && remaining <= 0) { ret = OB_TIMEOUT; }
    auto *native = share::server_service<data_plane::ObIRangeService>();
    if (OB_SUCC(ret) && native == nullptr) { ret = OB_NOT_INIT; }
    if (OB_SUCC(ret)) {
      auto *previous_session = THIS_WORKER.get_session();
      THIS_WORKER.set_session(in_process_storage != nullptr
          ? &in_process_storage->session : previous_session);
      if (operation == 'C') {
        ret = native->get_multi_ranges_cost(
            storage_tablet, remaining, storage_ranges, size);
      } else {
        common::ObArrayArray<common::ObStoreRange> storage_split;
        ret = native->split_multi_ranges(storage_tablet, remaining,
            storage_ranges, tasks, *allocator, storage_split);
        split->reset();
        for (int64_t i = 0; OB_SUCC(ret) && i < storage_split.count(); ++i) {
          common::ObSEArray<common::ObStoreRange, 4> group;
          for (int64_t j = 0; OB_SUCC(ret) && j < storage_split.count(i); ++j) {
            common::ObStoreRange range = storage_split.at(i, j);
            ret = group.push_back(range);
          }
          if (OB_SUCC(ret)) { ret = split->push_back(group); }
        }
      }
      THIS_WORKER.set_session(previous_session);
    }
    return ret;
  }
  const IRangeSchemaPolicy &schema_policy_;
};
NativeRangeSchemaPolicy native_range_schema_policy;
ForkRangeSchemaPolicy fork_range_schema_policy;
InProcessRangeService native_inprocess_ranges(native_range_schema_policy);
InProcessRangeService fork_inprocess_ranges(fork_range_schema_policy);
data_plane::ObIRangeService *effective_range_service(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime != nullptr
      ? static_cast<data_plane::ObIRangeService *>(
            runtime->service(ns::NamespaceRuntime::RANGE_SERVICE))
      : nullptr;
}
struct InProcessNamespaceServices {
  explicit InProcessNamespaceServices(uint64_t ns)
      : tablet_autoincrement(ns), schema_lifecycle(ns) {}
  NamespaceRoutingSqlProxy *sql_proxy = nullptr;
  NamespaceRoutingSqlProxy *ddl_proxy = nullptr;
  share::schema::ObMultiVersionSchemaService *schema_service = nullptr;
  share::schema::ObSchemaPublishSignal *signal = nullptr;
  rootserver::ObMaxIdCacheAdapter *max_id = nullptr;
  share::schema::ObSchemaServiceSQLImpl *backend = nullptr;
  InProcessSchemaRefreshScheduler *scheduler = nullptr;
  sql::ObPlanCache *plan_cache = nullptr;
  sql::ObPsCache *ps_cache = nullptr;
  common::ObAddr address;
  ObVirtualDataAccessService *virtual_table_scan = nullptr;
  common::ObOptStatManager opt_stat_manager;
  common::ObOptStatMonitorManager opt_stat_monitor_manager;
  rootserver::ObLocalManagementService *root_commands = nullptr;
  InProcessRootserverLocalRuntime *local_runtime = nullptr;
  InProcessDirectInsertService direct_insert;
  InProcessTabletAutoincrementService tablet_autoincrement;
  ForkSchemaLifecycle schema_lifecycle;
  ForkTableLockTabletRouter table_lock_tablet_router;
  share::ObAutoincrementService autoincrement;
  DirectInsertRegistry direct_insert_registry;
  std::atomic<bool> schema_loaded{false};
  std::mutex schema_load_mutex;
  std::condition_variable schema_load_cv;
  std::thread::id schema_loading_thread;
  std::atomic<bool> recovery_loaded{false};
};
std::shared_mutex inprocess_services_mutex;
std::map<uint64_t, std::unique_ptr<InProcessNamespaceServices>> inprocess_services;
void stop_in_process_opt_stat_monitors()
{
  std::shared_lock<std::shared_mutex> guard(inprocess_services_mutex);
  for (auto &entry : inprocess_services) {
    auto *monitor = &entry.second->opt_stat_monitor_manager;
    common::ObOptStatMonitorManager::server_module_stop(monitor);
  }
}
void wait_in_process_opt_stat_monitors()
{
  std::shared_lock<std::shared_mutex> guard(inprocess_services_mutex);
  for (auto &entry : inprocess_services) {
    auto *monitor = &entry.second->opt_stat_monitor_manager;
    common::ObOptStatMonitorManager::server_module_wait(monitor);
  }
}
thread_local uint64_t activating_namespace = 0;
int resolve_inprocess_tablet_schema(uint64_t physical_tablet_id,
    share::schema::ObMultiVersionSchemaService *&schema_service,
    uint64_t &logical_tablet_id)
{
  const bool encoded = storage::NamespaceForkKernelPrototype::is_encoded_id(physical_tablet_id);
  const uint64_t owner_ns = encoded
      ? ::oceanbase::ns::NamespaceObjectKey::encoded_namespace(physical_tablet_id) : 1;
  schema_service = nullptr;
  logical_tablet_id = physical_tablet_id;
  int ret = encoded ? storage::NamespaceForkKernelPrototype::local_object_id(
      owner_ns, physical_tablet_id, logical_tablet_id) : OB_SUCCESS;
  ns::NamespaceRuntime *runtime = nullptr;
  if (ret != OB_SUCCESS) {
  } else if (!ns::namespace_registry().get(owner_ns, runtime) || runtime == nullptr) {
    ret = OB_NOT_INIT;
  } else if (runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE) == nullptr
             && (activating_namespace != 0
                 || OB_FAIL(ensure_in_process_namespace(owner_ns)))) {
    if (ret == OB_SUCCESS) { ret = OB_NOT_INIT; }
  } else if (OB_ISNULL(schema_service = namespace_schema_service(owner_ns))) {
    ret = OB_NOT_INIT;
  } else if (encoded && activating_namespace == 0) {
    std::shared_lock<std::shared_mutex> guard(inprocess_services_mutex);
    const auto it = inprocess_services.find(owner_ns);
    if (it != inprocess_services.end()
        && !it->second->schema_loaded.load(std::memory_order_acquire)) {
      guard.unlock();
      ret = inprocess_refresh_schema(owner_ns);
    }
  }
  return ret;
}
int activate_in_process_namespace(uint64_t ns, ns::NamespaceRuntime &runtime)
{
  int ret = OB_SUCCESS;
  ObServer &server = ObServer::get_instance();
  auto services = std::make_unique<InProcessNamespaceServices>(ns);
  char suffix[32];
  snprintf(suffix, sizeof(suffix), "ns%llu", static_cast<unsigned long long>(ns));
  const char *stage = "alloc";
  if (OB_ISNULL(services->sql_proxy = OB_NEW(NamespaceRoutingSqlProxy,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->ddl_proxy = OB_NEW(NamespaceRoutingSqlProxy,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->signal = OB_NEW(share::schema::ObSchemaPublishSignal,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->max_id = OB_NEW(rootserver::ObMaxIdCacheAdapter,
          ObModIds::OB_SCHEMA_SERVICE, server.get_local_management_service()))
      || OB_ISNULL(services->schema_service = OB_NEW(InProcessSchemaService,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->backend = OB_NEW(share::schema::ObSchemaServiceSQLImpl,
          ObModIds::OB_SCHEMA_SERVICE, services->max_id, *services->ddl_proxy,
          *services->schema_service))
      || OB_ISNULL(services->scheduler = OB_NEW(InProcessSchemaRefreshScheduler,
          ObModIds::OB_SCHEMA_SERVICE, ns, *services->schema_service))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(stage = "proxy_init")) {
  } else if (OB_FAIL(static_cast<NamespaceRoutingSqlProxy *>(
                 services->sql_proxy)->init_routed(ns, false))) {
  } else if (OB_FAIL(static_cast<NamespaceRoutingSqlProxy *>(
                 services->ddl_proxy)->init_routed(ns, true))) {
  } else if (OB_FAIL(services->autoincrement.init(services->sql_proxy))) {
  } else if (OB_FAIL(services->opt_stat_manager.init(services->sql_proxy, &GCONF, ns))) {
  } else if (FALSE_IT(stage = "signal_init")) {
  } else if (OB_FAIL(services->signal->init())) {
  } else if (FALSE_IT(stage = "service_init")) {
  } else if (OB_FAIL(services->schema_service->init(
      services->sql_proxy, &GCONF, *GCTX.schema_status_proxy_,
      GCTX.status_, GCTX.in_bootstrap_, OB_MAX_VERSION_COUNT, *services->backend,
      *services->scheduler, *services->signal, suffix))) {
  } else if (FALSE_IT(stage = "ddl_sequence")) {
  } else if (OB_FAIL(([&] {
      // The global launcher initializes only ns1's schema backend. Child DDL
      // also publishes a sequence id, so seed it from the current leader epoch.
      auto *root_backend = ObMultiVersionSchemaService::get_instance().get_schema_service();
      const auto sequence = root_backend != nullptr
          ? root_backend->get_sequence_id() : ObDDLSequenceID();
      return root_backend == nullptr || !sequence.is_valid()
          ? OB_NOT_INIT
          : services->backend->init_sequence_id_by_sys_leader_epoch(
                sequence.get_sys_leader_epoch());
    })())) {
  } else if (FALSE_IT(stage = "baseline")) {
  } else if (FALSE_IT([&] {
      // Static system-table definitions are identical in every namespace;
      // seed them locally exactly like the worker bootstrap does.
      ObArenaAllocator allocator("NsInProcBase");
      ObSArray<share::schema::ObTableSchema> system_schemas;
      int seed_ret = share::schema::ObSchemaUtils::construct_inner_table_schemas(
          system_schemas, allocator, true);
      if (!seed_ret) {
        seed_ret = share::schema::ObSchemaUtils::generate_hard_code_schema_version(
            system_schemas);
      }
      if (!seed_ret) {
        const int64_t system_schema_version =
            share::schema::ObSchemaUtils::get_inner_table_sys_schema_version(system_schemas);
        seed_ret = services->schema_service->broadcast_runtime_schema(
            system_schemas, system_schema_version);
      }
      return seed_ret;
    }())) {
  } else if (FALSE_IT(stage = "recovery")) {
  } else if (FALSE_IT([&] {
      // A previous serving instance may have committed native all_* rows
      // after it marked the namespace dirty. Reconcile before publishing,
      // mirroring the worker bootstrap recovery block.
      InProcessServingScope serving(ns);
      IndependentStorageScope storage_scope;
      int recovery_ret = storage_scope.error();
      int64_t directory_schema_version = OB_INVALID_VERSION;
      int64_t local_schema_version = OB_INVALID_VERSION;
      int64_t published_schema_version = OB_INVALID_VERSION;
      bool recovery_needed = false;
      if (!recovery_ret) {
        recovery_ret = storage::NamespaceForkKernelPrototype::begin_schema_recovery(
            ns, recovery_needed);
      }
      if (!recovery_ret && recovery_needed) {
        if (OB_SUCCESS != (recovery_ret = fetch_schema_version(
                false, false, directory_schema_version))) {
        } else if (OB_SUCCESS != (recovery_ret =
                services->schema_service->refresh_runtime_schema_from_static_system())) {
        } else if (OB_SUCCESS != (recovery_ret =
                services->schema_service->get_runtime_refreshed_schema_version(
                local_schema_version))) {
        } else if (local_schema_version < directory_schema_version) {
          recovery_ret = OB_STATE_NOT_MATCH;
        } else if (local_schema_version > directory_schema_version) {
          recovery_ret = sync_namespace_schema_delta(
              ns, *services->schema_service, directory_schema_version,
              published_schema_version);
        } else {
          published_schema_version = local_schema_version;
        }
        if (!recovery_ret) {
          recovery_ret = storage::NamespaceForkKernelPrototype::finish_schema_recovery(
              ns, published_schema_version);
        }
        services->schema_loaded.store(recovery_ret == OB_SUCCESS,
            std::memory_order_release);
      }
      return recovery_ret;
    }())) {
  } else if (FALSE_IT(stage = "plan_cache")) {
  } else if (OB_ISNULL(services->plan_cache = OB_NEW(sql::ObPlanCache,
          ObModIds::OB_SQL_PLAN_CACHE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(services->plan_cache->init(common::OB_PLAN_CACHE_BUCKET_NUMBER,
          server))) {
  } else if (FALSE_IT(services->opt_stat_manager.bind_plan_cache(*services->plan_cache))) {
  } else if (FALSE_IT(stage = "ps_cache")) {
  } else if (OB_ISNULL(services->ps_cache = OB_NEW(sql::ObPsCache,
          ObModIds::OB_SQL_PS_CACHE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(sql::ObPsCache::server_module_init(services->ps_cache))) {
  } else if (FALSE_IT(stage = "root_commands")) {
  } else if (OB_ISNULL(services->local_runtime = OB_NEW(
          InProcessRootserverLocalRuntime, ObModIds::OB_SCHEMA_SERVICE, ns))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_ISNULL(services->root_commands = OB_NEW(
          rootserver::ObLocalManagementService, ObModIds::OB_SCHEMA_SERVICE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(services->root_commands->set_ddl_local_runtime(
          services->local_runtime))) {
  } else if (FALSE_IT(services->root_commands->set_ddl_sql_proxy(
          services->ddl_proxy))) {
  } else if (FALSE_IT(services->root_commands->set_local_command_service(
          server.get_ob_service()))) {
  } else if (OB_FAIL(services->root_commands->init_sql_worker(
          GCONF, *GCTX.config_mgr_, server.get_self(), *services->sql_proxy,
          server.get_mysql_proxy(),
          *services->schema_service))) {
  } else if (FALSE_IT(stage = "virtual_table_scan")) {
  } else if (FALSE_IT(services->address = server.get_self())) {
  } else if (OB_ISNULL(services->virtual_table_scan = OB_NEW(
          ObVirtualDataAccessService, ObModIds::OB_SCHEMA_SERVICE,
          *services->root_commands, services->address, &GCONF))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(stage = "opt_stat_monitor")) {
  } else if (OB_FAIL(services->opt_stat_monitor_manager.init(
          services->sql_proxy, services->schema_service,
          &services->opt_stat_manager))) {
  } else if (OB_FAIL(([&] {
      auto *monitor = &services->opt_stat_monitor_manager;
      return common::ObOptStatMonitorManager::server_module_start(monitor);
    }()))) {
  } else {
    stage = "done";
  }
  fprintf(stderr,
      "PROTOTYPE_INPROCESS_NS_ACTIVATE ns=%llu stage=%s ret=%d\n",
      static_cast<unsigned long long>(ns), stage, ret);
  if (!ret) {
    runtime.set_service(ns::NamespaceRuntime::SCHEMA_SERVICE, services->schema_service);
    runtime.set_service(ns::NamespaceRuntime::PLAN_CACHE, services->plan_cache);
    runtime.set_service(ns::NamespaceRuntime::PS_CACHE, services->ps_cache);
    runtime.set_service(ns::NamespaceRuntime::OPT_STAT_MANAGER,
        &services->opt_stat_manager);
    runtime.set_service(ns::NamespaceRuntime::OPT_STAT_MONITOR_MANAGER,
        &services->opt_stat_monitor_manager);
    runtime.set_service(ns::NamespaceRuntime::VIRTUAL_TABLE_SCAN_SERVICE,
        services->virtual_table_scan);
    runtime.set_service(ns::NamespaceRuntime::ROOT_COMMAND_SERVICE, services->root_commands);
    runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_SERVICE, &services->direct_insert);
    runtime.set_service(ns::NamespaceRuntime::DML_SERVICE, &fork_inprocess_dml);
    runtime.set_service(ns::NamespaceRuntime::RANGE_SERVICE, &fork_inprocess_ranges);
    runtime.set_service(ns::NamespaceRuntime::TABLET_SCAN, &inprocess_scan);
    runtime.set_service(ns::NamespaceRuntime::LOB_READ_SERVICE, &inprocess_lob_read);
    runtime.set_service(ns::NamespaceRuntime::WRITE_CONTEXT_SERVICE, &inprocess_write_context);
    runtime.set_service(ns::NamespaceRuntime::TRANSACTION_SERVICE, &inprocess_transactions);
    runtime.set_service(ns::NamespaceRuntime::DDL_CHECKSUM_ERROR_VERIFIER,
        &rootserver::task_ddl_checksum_error_verifier());
    runtime.set_service(ns::NamespaceRuntime::TABLET_AUTOINCREMENT_SERVICE,
        &services->tablet_autoincrement);
    runtime.set_service(ns::NamespaceRuntime::AUTOINCREMENT_SERVICE,
        &services->autoincrement);
    runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_REGISTRY, &services->direct_insert_registry);
    runtime.set_service(ns::NamespaceRuntime::SQL_PROXY, services->sql_proxy);
    runtime.set_service(ns::NamespaceRuntime::VECTOR_TASK_SQL_PROXY, GCTX.sql_proxy_);
    runtime.set_service(ns::NamespaceRuntime::SCHEMA_LIFECYCLE,
        &services->schema_lifecycle);
    runtime.set_service(ns::NamespaceRuntime::TABLE_LOCK_TABLET_ROUTER,
        &services->table_lock_tablet_router);
    inprocess_services.emplace(ns, std::move(services));
  }
  return ret;
}
int ensure_in_process_namespace(uint64_t ns)
{
  int ret = OB_SUCCESS;
  ns::NamespaceRuntime *runtime = nullptr;
  if (ns <= 1 || ns >= ns::NamespaceObjectKey::NAMESPACE_LIMIT) {
    ret = OB_NOT_SUPPORTED;
  } else if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (!ret && runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE) == nullptr) {
    std::unique_lock<std::shared_mutex> guard(inprocess_services_mutex);
    if (runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE) == nullptr) {
      const uint64_t previous_activation = activating_namespace;
      activating_namespace = ns;
      ret = activate_in_process_namespace(ns, *runtime);
      activating_namespace = previous_activation;
    }
  }
  return ret;
}
int prepare_namespace_login(ns::NamespaceRuntime &runtime)
{
  auto *lifecycle = static_cast<INamespaceSchemaLifecycle *>(
      runtime.service(ns::NamespaceRuntime::SCHEMA_LIFECYCLE));
  int ret = OB_SUCCESS;
  if (lifecycle == nullptr) {
    ret = ensure_in_process_namespace(runtime.ns().id());
    if (OB_SUCC(ret)) {
      lifecycle = static_cast<INamespaceSchemaLifecycle *>(
          runtime.service(ns::NamespaceRuntime::SCHEMA_LIFECYCLE));
    }
  }
  return ret != OB_SUCCESS ? ret
      : lifecycle == nullptr ? OB_NOT_INIT : lifecycle->refresh();
}
// Initial schema load for an in-process namespace. DDL publishes later
// changes through InProcessSchemaRefreshScheduler in this same process.
int inprocess_refresh_schema(uint64_t ns)
{
  InProcessServingScope serving(ns);
  std::shared_lock<std::shared_mutex> guard(inprocess_services_mutex);
  auto it = inprocess_services.find(ns);
  if (it == inprocess_services.end()) { return OB_NOT_INIT; }
  InProcessNamespaceServices &services = *it->second;
  // Service entries are retained for the lifetime of the process. Release the
  // map lock before refresh, which can re-enter this path through storage.
  guard.unlock();
  int ret = OB_SUCCESS;
  if (!services.schema_loaded.load(std::memory_order_acquire)) {
    std::unique_lock<std::mutex> load_guard(services.schema_load_mutex);
    while (!services.schema_loaded.load(std::memory_order_acquire)
           && services.schema_loading_thread != std::thread::id()) {
      if (services.schema_loading_thread == std::this_thread::get_id()) {
        return OB_SUCCESS; // The refresh can re-enter through its own storage reads.
      }
      services.schema_load_cv.wait(load_guard);
    }
    if (!services.schema_loaded.load(std::memory_order_acquire)) {
      services.schema_loading_thread = std::this_thread::get_id();
      load_guard.unlock();
      ret = services.schema_service->refresh_runtime_schema_from_static_system();
      load_guard.lock();
      services.schema_loaded.store(ret == OB_SUCCESS, std::memory_order_release);
      services.schema_loading_thread = std::thread::id();
      load_guard.unlock();
      services.schema_load_cv.notify_all();
    }
  }
  bool expected = false;
  if (!ret && services.recovery_loaded.compare_exchange_strong(expected, true)) {
    rootserver::ObDDLTaskContext context;
    context.namespace_id_ = ns;
    context.local_build_mode_ = rootserver::ObDDLTaskContext::LocalBuildMode::RESTARTABLE_SQL;
    context.recovery_mode_ = rootserver::ObDDLTaskContext::RecoveryMode::RETRY_UNTIL_CONSISTENT;
    context.sql_proxy_ = services.sql_proxy;
    context.session_sql_proxy_ = GCTX.sql_proxy_;
    context.ddl_proxy_ = services.ddl_proxy;
    context.schema_service_ = services.schema_service;
    context.root_service_ = services.root_commands;
    context.local_runtime_ = services.local_runtime;
    int recover_ret = rootserver::ObSysDDLSchedulerUtil::recover_task(context);
    if (recover_ret) {
      services.recovery_loaded.store(false, std::memory_order_release);
      fprintf(stderr, "PROTOTYPE_INPROCESS_DDL_RECOVERY ns=%llu ret=%d\n",
          static_cast<unsigned long long>(ns), recover_ret);
    }
  }
  return ret;
}
share::schema::ObMultiVersionSchemaService *namespace_schema_service(uint64_t ns)
{
  ns::NamespaceRuntime *runtime = nullptr;
  if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    return nullptr;
  }
  return static_cast<share::schema::ObMultiVersionSchemaService *>(
      runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE));
}
common::ObMySQLProxy *namespace_sql_proxy(uint64_t ns)
{
  ns::NamespaceRuntime *runtime = nullptr;
  if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    return nullptr;
  }
  return static_cast<common::ObMySQLProxy *>(
      runtime->service(ns::NamespaceRuntime::SQL_PROXY));
}
} } }
