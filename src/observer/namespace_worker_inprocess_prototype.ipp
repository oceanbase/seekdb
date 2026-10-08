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
#include "rootserver/fork_table/namespace_schema_publication.h"
#include "query/tablelock/ob_table_lock_runtime.h"
#include "storage/tablelock/ob_table_lock_service.h"
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
std::vector<uint64_t> &namespace_schema_load_stack()
{
  static thread_local std::vector<uint64_t> stack;
  return stack;
}
bool namespace_schema_loading(uint64_t id)
{
  const auto &stack = namespace_schema_load_stack();
  return std::find(stack.begin(), stack.end(), id) != stack.end();
}
class NamespaceSchemaLoadScope final
{
public:
  explicit NamespaceSchemaLoadScope(uint64_t id) { namespace_schema_load_stack().push_back(id); }
  ~NamespaceSchemaLoadScope() { namespace_schema_load_stack().pop_back(); }
};
// Read the published directory and SQL schema at one retained MVCC boundary.
// Following replicas must never repair an in-progress primary publication.
int load_committed_namespace_schema(uint64_t namespace_id,
                                   ObMultiVersionSchemaService &schema)
{
  // Schema loading issues inner SQL in this same namespace. Its nested
  // sessions use the installed static schemas while the outer load finishes.
  if (namespace_schema_loading(namespace_id)) { return OB_SUCCESS; }
  NamespaceSchemaLoadScope scope(namespace_id);
  InProcessServingScope serving(namespace_id);
  auto *access = share::server_service<storage::ObAccessService>();
  if (access == nullptr) { return OB_NOT_INIT; }
  auto &store = access->instance_meta_store();
  storage::InstanceMetaStore::Transaction tx;
  int ret = store.begin(tx, ObTimeUtility::current_time() + 30 * 1000 * 1000, true);
  if (OB_SUCC(ret)) {
    rootserver::InstanceNamespaceMetadata metadata(store, tx);
    rootserver::InstanceNamespaceRecord record;
    if (OB_FAIL(metadata.get_namespace(namespace_id, record))) {
    } else if (record.roots.state != 0) {
      ret = OB_ENTRY_NOT_EXIST;
    } else if (record.roots.active_schema_changes != 0
               || record.roots.pending_schema_version != 0) {
      // Keep an already installed publication usable during the next DDL.
      // In particular progress/management reads must keep serving while a
      // table changes. A cold runtime still waits for a complete publication.
      int64_t installed = OB_INVALID_VERSION;
      ret = schema.get_runtime_refreshed_schema_version(installed);
      if (OB_SUCC(ret) && (!schema.is_runtime_schema_ready()
                          || installed < record.roots.schema_version)) {
        ret = OB_SCHEMA_EAGAIN;
      }
    } else {
      ObRefreshSchemaStatus status;
      status.snapshot_timestamp_ = tx.snapshot_version().get_val_for_tx();
      status.readable_schema_version_ = record.roots.schema_version;
      ret = schema.refresh_runtime_schema_from_static_system(
          status, record.roots.schema_version);
    }
    const int cleanup = store.rollback(tx);
    if (OB_SUCC(ret)) { ret = cleanup; }
  }
  return ret;
}
int recover_namespace_schema_publication(uint64_t namespace_id,
                                        ObMultiVersionSchemaService &schema)
{
  bool needed = false;
  int ret = storage::NamespaceForkKernelPrototype::begin_schema_recovery(namespace_id, needed);
  int64_t published = OB_INVALID_VERSION;
  int64_t installed = OB_INVALID_VERSION;
  if (OB_SUCC(ret) && needed) {
    if (OB_FAIL(storage::NamespaceForkKernelPrototype::namespace_schema_version(namespace_id, published))) {
    } else if (OB_FAIL(schema.refresh_runtime_schema_from_static_system())) {
    } else if (OB_FAIL(schema.get_runtime_refreshed_schema_version(installed))) {
    } else if (installed < published) {
      ret = OB_STATE_NOT_MATCH;
    } else if (installed > published) {
      ret = sync_namespace_schema_delta(namespace_id, schema, installed);
    }
    if (OB_SUCC(ret)) {
      ret = storage::NamespaceForkKernelPrototype::finish_schema_recovery(namespace_id, installed);
    }
  }
  return ret;
}
class NamespaceSchemaLifecycle final : public INamespaceSchemaLifecycle
{
public:
  explicit NamespaceSchemaLifecycle(uint64_t ns, bool load_on_access = true,
                                    bool bootstrap = false)
      : ns_(ns), load_on_access_(load_on_access), bootstrap_(bootstrap) {}
  void complete_bootstrap() { bootstrap_.store(false, std::memory_order_release); }
  bool is_bootstrapping() const override { return bootstrap_.load(std::memory_order_acquire); }
  int acquire_read_view(const std::function<int(share::SCN &)> &acquire,
      ns::NamespaceCatalogViews::Handle &view,
      const ns::NamespaceCatalogViews::Handle &previous) override
  {
    if (is_bootstrapping()) {
      share::SCN snapshot;
      return acquire(snapshot);
    }
    return storage::NamespaceForkKernelPrototype::acquire_read_view(ns_, acquire, view, previous);
  }
  int find_read_view(int64_t snapshot, ns::NamespaceCatalogViews::Handle &view) override
  {
    view.reset();
    if (is_bootstrapping()) { return OB_SUCCESS; }
    view = ns::namespace_registry().catalog_views().find(ns_, snapshot);
    return view ? OB_SUCCESS : OB_STATE_NOT_MATCH;
  }
  int refresh() override
  {
    if (bootstrap_.load(std::memory_order_acquire)) { return OB_SUCCESS; }
    if (namespace_schema_loading(ns_)) { return OB_SUCCESS; }
    if (share::server_is_recovery_mode()) {
      primary_recovery_pending_.store(true, std::memory_order_release);
      auto *schema = namespace_schema_service(ns_);
      return schema == nullptr ? OB_NOT_INIT : load_committed_namespace_schema(ns_, *schema);
    }
    if (primary_recovery_pending_.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> guard(recovery_mutex_);
      if (primary_recovery_pending_.load(std::memory_order_acquire)) {
        NamespaceSchemaLoadScope scope(ns_);
        auto *schema = namespace_schema_service(ns_);
        const int ret = schema == nullptr ? OB_NOT_INIT
            : load_on_access_ ? inprocess_refresh_schema(ns_)
                              : recover_namespace_schema_publication(ns_, *schema);
        if (ret != OB_SUCCESS) { return ret; }
        primary_recovery_pending_.store(false, std::memory_order_release);
      }
    }
    return load_on_access_ ? inprocess_refresh_schema(ns_) : OB_SUCCESS;
  }
  int fetch_version(bool published, bool core_version, int64_t &version) override
  {
    if (bootstrap_.load(std::memory_order_acquire)) {
      auto *service = namespace_schema_service(ns_);
      if (service == nullptr) { return OB_NOT_INIT; }
      return published ? service->get_published_schema_version(version, core_version)
          : service->get_runtime_refreshed_schema_version(version, core_version);
    }
    return storage::NamespaceForkKernelPrototype::namespace_schema_version(ns_, version);
  }
  int begin_change() override
  {
    return bootstrap_.load(std::memory_order_acquire) ? OB_SUCCESS
        : storage::NamespaceForkKernelPrototype::begin_schema_change(ns_);
  }
  int finish_change(int64_t committed_schema_version) override
  {
    return bootstrap_.load(std::memory_order_acquire) ? OB_SUCCESS
        : storage::NamespaceForkKernelPrototype::finish_schema_change(ns_, committed_schema_version);
  }
  int stage_publication(common::ObMySQLTransaction &sql, ObMultiVersionSchemaService &schema,
      int64_t version, std::unique_ptr<rootserver::NamespaceSchemaPublication> &publication) override
  {
    if (bootstrap_.load(std::memory_order_acquire)) { return OB_SUCCESS; }
    auto *access = share::server_service<storage::ObAccessService>();
    if (access == nullptr) { return OB_NOT_INIT; }
    NamespaceSchemaLoadScope scope(ns_);
    publication.reset(new rootserver::NamespaceSchemaPublication(access->instance_meta_store(), ns_));
    return publication->stage(sql, schema, version);
  }
  int publish(ObMultiVersionSchemaService &schema_service,
              int64_t &published_schema_version) override
  {
    if (bootstrap_.load(std::memory_order_acquire)) {
      return schema_service.get_runtime_refreshed_schema_version(published_schema_version);
    }
    if (const char *delay_text = std::getenv("SEEKDB_NAMESPACE_DDL_PUBLISH_DELAY_US")) {
      char *end = nullptr;
      const int64_t delay_us = std::strtoll(delay_text, &end, 10);
      if (*delay_text && end && !*end && delay_us > 0 && delay_us <= 2000000) {
        ob_usleep(delay_us);
      }
    }
    return sync_namespace_schema_delta(ns_, schema_service, published_schema_version);
  }
private:
  uint64_t ns_;
  bool load_on_access_;
  std::atomic<bool> bootstrap_;
  std::atomic<bool> primary_recovery_pending_{false};
  std::mutex recovery_mutex_;
};
NamespaceSchemaLifecycle &bootstrap_schema_lifecycle()
{
  // Installed explicitly by instance bootstrap; normal namespaces use the
  // same lifecycle without a bootstrap phase.
  static NamespaceSchemaLifecycle lifecycle(1, false, true);
  return lifecycle;
}
int complete_namespace_schema_bootstrap(ObMultiVersionSchemaService &service)
{
  bootstrap_schema_lifecycle().complete_bootstrap();
  // A replica loads the publication already committed by its primary.
  // Persistent recovery is only permitted after local writes are admitted.
  if (!share::server_is_write_enabled()) { return OB_SUCCESS; }
  bool needed = false;
  int ret = storage::NamespaceForkKernelPrototype::begin_schema_recovery(1, needed);
  int64_t version = OB_INVALID_VERSION;
  if (OB_SUCC(ret)) { ret = sync_namespace_schema_delta(1, service, version); }
  if (OB_SUCC(ret) && needed) {
    ret = storage::NamespaceForkKernelPrototype::finish_schema_recovery(1, version);
  }
  return ret;
}
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
    int64_t &published_schema_version)
{
  auto *lifecycle = namespace_schema_lifecycle(namespace_id);
  return lifecycle == nullptr ? OB_NOT_INIT
      : lifecycle->publish(schema_service, published_schema_version);
}
int stage_namespace_schema_publication(uint64_t namespace_id, common::ObMySQLTransaction &sql,
    ObMultiVersionSchemaService &schema, int64_t version,
    std::unique_ptr<rootserver::NamespaceSchemaPublication> &publication)
{
  auto *lifecycle = namespace_schema_lifecycle(namespace_id);
  return lifecycle == nullptr ? OB_NOT_INIT : lifecycle->stage_publication(sql, schema, version, publication);
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
InProcessDmlService inprocess_dml;
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
common::ObIVirtualTableScan *effective_virtual_table_scan(sql::ObSQLSessionInfo *session)
{
  ns::NamespaceRuntime *runtime = session ? session->ns_runtime() : nullptr;
  return runtime ? static_cast<common::ObIVirtualTableScan *>(
      runtime->service(ns::NamespaceRuntime::VIRTUAL_TABLE_SCAN_SERVICE)) : nullptr;
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
  auto &schema_lifecycle = bootstrap_schema_lifecycle();
  static RootTableLockTabletRouter table_lock_tablet_router;
  runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_SERVICE, &direct_insert);
  runtime.set_service(ns::NamespaceRuntime::DML_SERVICE, &inprocess_dml);
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
    common::ObTabletID storage_tablet;
    ns::TabletAccess access;
    if (OB_SUCC(ret) && storage_space.is_namespace()
        && !has_logical_schema && !is_inner_table(logical_table_id)) {
      ret = OB_INVALID_ARGUMENT;
    }
    data_plane::ObNamespaceAccessMode mode;
    if (OB_FAIL(ret)) {
    } else if (OB_FAIL(storage_access_mode(storage_space, mode))) {
    } else if (OB_FAIL(access.prepare_current_read(storage_space.tablet_namespace_id(),
        logical_table_id, logical_tablet, mode))) {
    } else {
      storage_tablet = access.tablet();
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
  NamespaceSchemaLifecycle schema_lifecycle;
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
std::mutex namespace_dbms_scheduler_mutex;
bool namespace_dbms_scheduler_started = false;
bool namespace_dbms_scheduler_leader = false;
std::mutex namespace_table_lock_service_mutex;
bool namespace_table_lock_services_started = false;
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
std::vector<rootserver::ObDBMSSchedService *> namespace_dbms_schedulers()
{
  std::vector<ns::NamespaceRuntime *> runtimes;
  ns::namespace_registry().list_retained_runtimes(runtimes);
  std::vector<rootserver::ObDBMSSchedService *> schedulers;
  for (auto *runtime : runtimes) {
    auto *service = static_cast<query::ObISchedulerService *>(
        runtime->service(ns::NamespaceRuntime::DBMS_SCHEDULER));
    if (service != nullptr) {
      schedulers.push_back(static_cast<rootserver::ObDBMSSchedService *>(service));
    }
  }
  return schedulers;
}
int init_namespace_dbms_scheduler(ns::NamespaceRuntime &runtime,
    common::ObMySQLProxy &sql_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(namespace_dbms_scheduler_mutex);
  if (runtime.service(ns::NamespaceRuntime::DBMS_SCHEDULER) != nullptr) {
    ret = OB_INIT_TWICE;
  } else {
    auto scheduler = std::make_unique<rootserver::ObDBMSSchedService>();
    bool start_attempted = false;
    if (OB_FAIL(scheduler->init(sql_proxy, schema_service))) {
    } else {
      if (namespace_dbms_scheduler_started) {
        start_attempted = true;
        ret = scheduler->start();
      }
      if (ret == OB_SUCCESS && namespace_dbms_scheduler_started
          && namespace_dbms_scheduler_leader) {
        ret = scheduler->activate();
      }
      if (ret == OB_SUCCESS) {
        runtime.set_owned_service<query::ObISchedulerService>(
            ns::NamespaceRuntime::DBMS_SCHEDULER, std::move(scheduler));
      }
    }
    if (ret != OB_SUCCESS && start_attempted) {
      scheduler->stop();
      scheduler->wait();
    }
  }
  return ret;
}
int start_namespace_dbms_schedulers()
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(namespace_dbms_scheduler_mutex);
  auto schedulers = namespace_dbms_schedulers();
  for (auto *scheduler : schedulers) {
    if (OB_SUCCESS != (ret = scheduler->start())) { break; }
    if (namespace_dbms_scheduler_leader
        && OB_SUCCESS != (ret = scheduler->activate())) { break; }
  }
  if (ret != OB_SUCCESS) {
    for (auto *scheduler : schedulers) { scheduler->deactivate(); }
    for (auto *scheduler : schedulers) { scheduler->stop(); }
  } else {
    namespace_dbms_scheduler_started = true;
  }
  return ret;
}
int update_namespace_dbms_scheduler_role(bool leader)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(namespace_dbms_scheduler_mutex);
  namespace_dbms_scheduler_leader = leader;
  if (namespace_dbms_scheduler_started) {
    auto schedulers = namespace_dbms_schedulers();
    for (auto *scheduler : schedulers) {
      if (leader) {
        if (OB_SUCCESS != (ret = scheduler->activate())) { break; }
      } else {
        scheduler->deactivate();
      }
    }
    if (ret != OB_SUCCESS) {
      for (auto *scheduler : schedulers) { scheduler->deactivate(); }
      namespace_dbms_scheduler_leader = false;
    }
  }
  return ret;
}
void stop_namespace_dbms_schedulers()
{
  std::lock_guard<std::mutex> guard(namespace_dbms_scheduler_mutex);
  namespace_dbms_scheduler_started = false;
  namespace_dbms_scheduler_leader = false;
  for (auto *scheduler : namespace_dbms_schedulers()) { scheduler->stop(); }
}
void wait_namespace_dbms_schedulers()
{
  std::vector<rootserver::ObDBMSSchedService *> schedulers;
  {
    std::lock_guard<std::mutex> guard(namespace_dbms_scheduler_mutex);
    schedulers = namespace_dbms_schedulers();
  }
  for (auto *scheduler : schedulers) { scheduler->wait(); }
}
void destroy_namespace_dbms_schedulers()
{
  std::lock_guard<std::mutex> guard(namespace_dbms_scheduler_mutex);
  std::vector<ns::NamespaceRuntime *> runtimes;
  ns::namespace_registry().list_retained_runtimes(runtimes);
  for (auto *runtime : runtimes) {
    runtime->clear_service(ns::NamespaceRuntime::DBMS_SCHEDULER);
  }
}
std::vector<transaction::tablelock::ObTableLockService *> namespace_table_lock_services()
{
  std::vector<ns::NamespaceRuntime *> runtimes;
  ns::namespace_registry().list_retained_runtimes(runtimes);
  std::vector<transaction::tablelock::ObTableLockService *> services;
  for (auto *runtime : runtimes) {
    auto *service = static_cast<transaction::tablelock::ObTableLockService *>(
        runtime->service(ns::NamespaceRuntime::TABLE_LOCK_SERVICE));
    if (service != nullptr) { services.push_back(service); }
  }
  return services;
}
int init_namespace_table_lock_service(ns::NamespaceRuntime &runtime,
    common::ObMySQLProxy &sql_proxy,
    share::schema::ObMultiVersionSchemaService &schema_service,
    query::ObIDeadlockSessionService &session_service)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(namespace_table_lock_service_mutex);
  if (runtime.service(ns::NamespaceRuntime::TABLE_LOCK_SERVICE) != nullptr) {
    ret = OB_INIT_TWICE;
  } else {
    auto service = std::make_unique<transaction::tablelock::ObTableLockService>();
    auto cleanup = [&runtime](const transaction::tablelock::ObTableLockOwnerID &owner) {
      return query::release_locks_for_dead_owner(owner.type(), owner.id(), runtime);
    };
    if (OB_FAIL(service->init(sql_proxy, schema_service, session_service, cleanup))) {
    } else if (namespace_table_lock_services_started && OB_FAIL(service->start())) {
      service->stop();
      service->wait();
      service->destroy();
    } else {
      runtime.set_owned_service<transaction::tablelock::ObTableLockService>(
          ns::NamespaceRuntime::TABLE_LOCK_SERVICE, std::move(service));
    }
  }
  return ret;
}
int start_namespace_table_lock_services()
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(namespace_table_lock_service_mutex);
  auto services = namespace_table_lock_services();
  for (auto *service : services) {
    if (OB_SUCCESS != (ret = service->start())) { break; }
  }
  if (ret == OB_SUCCESS) {
    namespace_table_lock_services_started = true;
  } else {
    for (auto *service : services) { service->stop(); }
    for (auto *service : services) { service->wait(); }
  }
  return ret;
}
void stop_namespace_table_lock_services()
{
  std::lock_guard<std::mutex> guard(namespace_table_lock_service_mutex);
  namespace_table_lock_services_started = false;
  for (auto *service : namespace_table_lock_services()) { service->stop(); }
}
void wait_namespace_table_lock_services()
{
  std::lock_guard<std::mutex> guard(namespace_table_lock_service_mutex);
  for (auto *service : namespace_table_lock_services()) { service->wait(); }
}
void destroy_namespace_table_lock_services()
{
  std::lock_guard<std::mutex> guard(namespace_table_lock_service_mutex);
  std::vector<ns::NamespaceRuntime *> runtimes;
  ns::namespace_registry().list_retained_runtimes(runtimes);
  for (auto *runtime : runtimes) {
    auto *service = static_cast<transaction::tablelock::ObTableLockService *>(
        runtime->service(ns::NamespaceRuntime::TABLE_LOCK_SERVICE));
    if (service != nullptr) {
      service->destroy();
      runtime->clear_service(ns::NamespaceRuntime::TABLE_LOCK_SERVICE);
    }
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
  common::ObMySQLProxy *root_sql_proxy = namespace_sql_proxy(1);
  auto *root_schema_service = namespace_schema_service(1);
  auto *root_schema_status_proxy = root_schema_service != nullptr
      ? root_schema_service->get_schema_status_proxy() : nullptr;
  rootserver::ObLocalManagementService *root_commands =
      namespace_local_management_service(1);
  auto services = std::make_unique<InProcessNamespaceServices>(ns);
  char suffix[32];
  snprintf(suffix, sizeof(suffix), "ns%llu", static_cast<unsigned long long>(ns));
  const char *stage = "alloc";
  if (OB_ISNULL(root_sql_proxy) || OB_ISNULL(root_schema_status_proxy)
      || OB_ISNULL(root_commands)) {
    ret = OB_NOT_INIT;
  } else if (OB_ISNULL(services->sql_proxy = OB_NEW(NamespaceRoutingSqlProxy,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->ddl_proxy = OB_NEW(NamespaceRoutingSqlProxy,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->signal = OB_NEW(share::schema::ObSchemaPublishSignal,
          ObModIds::OB_SCHEMA_SERVICE))
      || OB_ISNULL(services->max_id = OB_NEW(rootserver::ObMaxIdCacheAdapter,
          ObModIds::OB_SCHEMA_SERVICE, *root_commands))
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
      services->sql_proxy, &GCONF, *root_schema_status_proxy,
      GCTX.status_, GCTX.in_bootstrap_, OB_MAX_VERSION_COUNT, *services->backend,
      *services->scheduler, *services->signal, suffix))) {
  } else if (FALSE_IT(stage = "ddl_sequence")) {
  } else if (OB_FAIL(([&] {
      // The global launcher initializes only ns1's schema backend. Child DDL
      // also publishes a sequence id, so seed it from the current leader epoch.
      auto *root_schema_service = namespace_schema_service(1);
      auto *root_backend = root_schema_service != nullptr
          ? root_schema_service->get_schema_service() : nullptr;
      const auto sequence = root_backend != nullptr
          ? root_backend->get_sequence_id() : ObDDLSequenceID();
      return root_backend == nullptr || !sequence.is_valid()
          ? OB_NOT_INIT
          : services->backend->init_sequence_id_by_sys_leader_epoch(
                sequence.get_sys_leader_epoch());
    })())) {
  } else if (FALSE_IT(stage = "baseline")) {
  } else if (OB_FAIL(([&] {
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
    })())) {
  } else if (FALSE_IT(stage = "plan_cache")) {
  } else if (OB_ISNULL(services->plan_cache = OB_NEW(sql::ObPlanCache,
          ObModIds::OB_SQL_PLAN_CACHE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(services->plan_cache->init(common::OB_PLAN_CACHE_BUCKET_NUMBER,
          server, *services->schema_service))) {
  } else if (FALSE_IT(services->opt_stat_manager.bind_plan_cache(*services->plan_cache))) {
  } else if (FALSE_IT(stage = "ps_cache")) {
  } else if (OB_ISNULL(services->ps_cache = OB_NEW(sql::ObPsCache,
          ObModIds::OB_SQL_PS_CACHE))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (OB_FAIL(sql::ObPsCache::server_module_init(
          services->ps_cache, *services->schema_service))) {
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
          *root_sql_proxy,
          *services->schema_service, services->autoincrement))) {
  } else if (FALSE_IT(stage = "virtual_table_scan")) {
  } else if (FALSE_IT(services->address = server.get_self())) {
  } else if (OB_ISNULL(services->virtual_table_scan = OB_NEW(
          ObVirtualDataAccessService, ObModIds::OB_SCHEMA_SERVICE,
          services->address, &GCONF))) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } else if (FALSE_IT(stage = "opt_stat_monitor")) {
  } else if (OB_FAIL(services->opt_stat_monitor_manager.init(
          services->sql_proxy, services->schema_service,
          &services->opt_stat_manager))) {
  } else if (OB_FAIL(([&] {
      auto *monitor = &services->opt_stat_monitor_manager;
      return common::ObOptStatMonitorManager::server_module_start(monitor);
    }()))) {
  } else if (OB_FAIL(services->root_commands->schedule_load_ddl_task())) {
  } else if (FALSE_IT(stage = "dbms_scheduler")) {
  } else if (OB_FAIL(init_namespace_dbms_scheduler(
          runtime, *services->sql_proxy, *services->schema_service))) {
  } else if (FALSE_IT(stage = "table_lock_service")) {
  } else if (OB_FAIL(init_namespace_table_lock_service(runtime,
          *services->sql_proxy, *services->schema_service,
          server.get_sql_session_mgr()))) {
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
    runtime.set_service(ns::NamespaceRuntime::DML_SERVICE, &inprocess_dml);
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
    runtime.set_service(ns::NamespaceRuntime::DDL_SQL_PROXY, services->ddl_proxy);
    runtime.set_service(ns::NamespaceRuntime::VECTOR_TASK_SQL_PROXY,
        namespace_sql_proxy(1));
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
  if (share::server_is_recovery_mode()) {
    return load_committed_namespace_schema(ns, *services.schema_service);
  }
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
      // All runtime services are installed before this load. Recovery uses
      // inner SQL, which needs the same fully bound runtime as normal reads.
      // Keep schema_loaded false until both schema and directory are current.
      bool recovery_needed = false;
      int64_t directory_schema_version = OB_INVALID_VERSION;
      int64_t local_schema_version = OB_INVALID_VERSION;
      int64_t published_schema_version = OB_INVALID_VERSION;
      if (OB_SUCC(ret)) {
        ret = storage::NamespaceForkKernelPrototype::begin_schema_recovery(
            ns, recovery_needed);
      }
      if (OB_SUCC(ret) && recovery_needed) {
        ret = services.schema_lifecycle.fetch_version(
            false, false, directory_schema_version);
      }
      if (OB_SUCC(ret)) {
        ret = services.schema_service->refresh_runtime_schema_from_static_system();
      }
      if (OB_SUCC(ret) && recovery_needed) {
        if (OB_FAIL(services.schema_service->get_runtime_refreshed_schema_version(
                local_schema_version))) {
        } else if (local_schema_version < directory_schema_version) {
          ret = OB_STATE_NOT_MATCH;
        } else if (local_schema_version > directory_schema_version) {
          ret = sync_namespace_schema_delta(
              ns, *services.schema_service, published_schema_version);
        } else {
          published_schema_version = local_schema_version;
        }
        if (OB_SUCC(ret)) {
          ret = storage::NamespaceForkKernelPrototype::finish_schema_recovery(
              ns, published_schema_version);
        }
      }
      load_guard.lock();
      services.schema_loaded.store(ret == OB_SUCCESS, std::memory_order_release);
      services.schema_loading_thread = std::thread::id();
      load_guard.unlock();
      services.schema_load_cv.notify_all();
    }
  }
  bool expected = false;
  if (!ret && share::server_is_write_enabled()
      && services.recovery_loaded.compare_exchange_strong(expected, true)) {
    rootserver::ObDDLTaskContext context;
    context.namespace_id_ = ns;
    context.local_build_mode_ = rootserver::ObDDLTaskContext::LocalBuildMode::RESTARTABLE_SQL;
    context.recovery_mode_ = rootserver::ObDDLTaskContext::RecoveryMode::RETRY_UNTIL_CONSISTENT;
    context.sql_proxy_ = services.sql_proxy;
    context.session_sql_proxy_ = namespace_sql_proxy(1);
    context.freeze_info_sql_proxy_ = namespace_sql_proxy(1);
    context.ddl_proxy_ = services.ddl_proxy;
    context.schema_service_ = services.schema_service;
    context.autoincrement_service_ = &services.autoincrement;
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
common::ObMySQLProxy *namespace_ddl_sql_proxy(uint64_t ns)
{
  ns::NamespaceRuntime *runtime = nullptr;
  if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    return nullptr;
  }
  return static_cast<common::ObMySQLProxy *>(
      runtime->service(ns::NamespaceRuntime::DDL_SQL_PROXY));
}
rootserver::ObLocalManagementService *namespace_local_management_service(uint64_t ns)
{
  ns::NamespaceRuntime *runtime = nullptr;
  if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    return nullptr;
  }
  return static_cast<rootserver::ObLocalManagementService *>(
      runtime->service(ns::NamespaceRuntime::ROOT_COMMAND_SERVICE));
}
} } }
