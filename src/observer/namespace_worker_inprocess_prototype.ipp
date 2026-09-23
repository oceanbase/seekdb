// Ticket 05c: serve forked namespaces (ns>1) inside the shared process.
//
// The worker-mode storage boundary is reused verbatim: the same Remote* stub
// implementations serialize requests into Frames, and the same serve_storage
// handlers translate (ns, local) ids to storage ids. Only the transport
// changes: instead of a Unix socket round trip to a worker process, a bound
// session delivers each frame synchronously to the in-process storage
// context (InProcessStorage, the channel-free DirectStorageContext twin).
//
// Per-namespace services (schema service, plan cache) live in the namespace
// runtime's service slots and are constructed lazily on first use.
#include "namespace/namespace.h"
#include "observer/schema/ob_schema_service_sql_impl.h"
#include "rootserver/ob_max_id_cache_adapter.h"
#include "rootserver/ob_local_management_service.h"
#include "rootserver/ddl_task/ob_sys_ddl_util.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include <map>
#include <memory>
#include <mutex>
#include <shared_mutex>
namespace oceanbase { namespace observer { namespace namespace_worker_prototype {
bool shared_inner_sql_bounces(sql::ObSQLSessionInfo &session)
{
  const uint64_t ns = in_process_session_ns(&session);
  return !worker_process
      && !in_process_namespace_enabled(ns > 1 ? ns : resolve_shared_inner_sql_namespace());
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
RemoteTabletScan inprocess_scan;
RemoteLobReadService inprocess_lob_read;
RemoteRangeService inprocess_ranges;
RemoteDmlService inprocess_dml;
RemoteWriteContext inprocess_write_context;
RemoteTransactionService inprocess_transactions;
RemoteInnerConnectionLockRuntime inprocess_inner_locks;
transaction::tablelock::ObIInnerConnectionLockRuntime *inprocess_lock_runtime(
    common::sqlclient::ObISQLConnection *conn)
{
  auto *inner = static_cast<ObInnerSQLConnection *>(conn);
  return inner != nullptr && in_process_session_ns(&inner->get_session()) > 1
      ? static_cast<transaction::tablelock::ObIInnerConnectionLockRuntime *>(
            &inprocess_inner_locks)
      : share::server_service<transaction::tablelock::ObIInnerConnectionLockRuntime>();
}
common::ObITabletScan *effective_tablet_scan(sql::ObSQLSessionInfo *session,
                                             common::ObITabletScan *fallback)
{
  return in_process_session_ns(session) > 1
      ? static_cast<common::ObITabletScan *>(&inprocess_scan) : fallback;
}
common::ObILobReadService *effective_lob_read_service(sql::ObSQLSessionInfo *session,
                                                      common::ObILobReadService *fallback)
{
  return in_process_session_ns(session) > 1 ? &inprocess_lob_read : fallback;
}
data_plane::ObIRangeService *effective_range_service(sql::ObSQLSessionInfo *session,
                                                     data_plane::ObIRangeService *fallback)
{
  return in_process_session_ns(session) > 1 ? &inprocess_ranges : fallback;
}
data_plane::ObIDmlService *effective_dml_service(sql::ObSQLSessionInfo *session,
                                                 data_plane::ObIDmlService *fallback)
{
  return in_process_session_ns(session) > 1 ? &inprocess_dml : fallback;
}
data_plane::ObIWriteContextService *effective_write_context_service(
    sql::ObSQLSessionInfo *session, data_plane::ObIWriteContextService *fallback)
{
  return in_process_session_ns(session) > 1 ? &inprocess_write_context : fallback;
}
data_plane::ObITransactionService *effective_transaction_service(
    sql::ObSQLSessionInfo *session, data_plane::ObITransactionService *fallback)
{
  return in_process_session_ns(session) > 1 ? &inprocess_transactions : fallback;
}
sql::ObPlanCache *effective_plan_cache(sql::ObSQLSessionInfo *session,
                                       sql::ObPlanCache *fallback)
{
  if (in_process_session_ns(session) <= 1) { return fallback; }
  ns::NamespaceRuntime *runtime =
      session->ns_runtime();
  void *service = runtime ? runtime->service(ns::NamespaceRuntime::PLAN_CACHE) : nullptr;
  return service != nullptr ? static_cast<sql::ObPlanCache *>(service) : fallback;
}
query::ObIRootCommandService *effective_root_command_service(
    sql::ObSQLSessionInfo *session, query::ObIRootCommandService *fallback)
{
  if (in_process_session_ns(session) <= 1) { return fallback; }
  ns::NamespaceRuntime *runtime = session->ns_runtime();
  void *service = runtime ? runtime->service(ns::NamespaceRuntime::ROOT_COMMAND_SERVICE) : nullptr;
  return service != nullptr
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
  int read(ReadResult &res, const char *sql, const int32_t group_id) override
  {
    const int override_ret = push_inner_sql_namespace_override(ns_);
    const int ret = override_ret ? override_ret
        : ObCommonSqlProxy::read(res, sql, group_id);
    if (!override_ret) { pop_inner_sql_namespace_override(); }
    return ret;
  }
  int write(const char *sql, const int32_t group_id, int64_t &affected_rows) override
  {
    const int override_ret = push_inner_sql_namespace_override(ns_);
    const int ret = override_ret ? override_ret
        : ObCommonSqlProxy::write(sql, group_id, affected_rows);
    if (!override_ret) { pop_inner_sql_namespace_override(); }
    return ret;
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
struct InProcessNamespaceServices {
  NamespaceRoutingSqlProxy *sql_proxy = nullptr;
  NamespaceRoutingSqlProxy *ddl_proxy = nullptr;
  share::schema::ObMultiVersionSchemaService *schema_service = nullptr;
  share::schema::ObSchemaPublishSignal *signal = nullptr;
  rootserver::ObMaxIdCacheAdapter *max_id = nullptr;
  share::schema::ObSchemaServiceSQLImpl *backend = nullptr;
  InProcessSchemaRefreshScheduler *scheduler = nullptr;
  sql::ObPlanCache *plan_cache = nullptr;
  rootserver::ObLocalManagementService *root_commands = nullptr;
  RemoteRootserverLocalRuntime *local_runtime = nullptr;
  RemoteDirectInsertService direct_insert;
  RequestRoutes direct_insert_routes{WORKER_REQUEST};
  std::atomic<bool> schema_loaded{false};
  std::atomic<bool> recovery_loaded{false};
};
std::shared_mutex inprocess_services_mutex;
std::map<uint64_t, std::unique_ptr<InProcessNamespaceServices>> inprocess_services;
int activate_in_process_namespace(uint64_t ns, ns::NamespaceRuntime &runtime)
{
  int ret = OB_SUCCESS;
  ObServer &server = ObServer::get_instance();
  auto services = std::make_unique<InProcessNamespaceServices>();
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
  } else if (FALSE_IT(stage = "signal_init")) {
  } else if (OB_FAIL(services->signal->init())) {
  } else if (FALSE_IT(stage = "service_init")) {
  } else if (OB_FAIL(services->schema_service->init(
      services->sql_proxy, &GCONF, *GCTX.schema_status_proxy_,
      GCTX.status_, GCTX.in_bootstrap_, OB_MAX_VERSION_COUNT, *services->backend,
      *services->scheduler, *services->signal, suffix))) {
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
        recovery_ret = begin_namespace_schema_recovery(recovery_needed);
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
          recovery_ret = finish_namespace_schema_recovery(published_schema_version);
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
  } else if (FALSE_IT(stage = "root_commands")) {
  } else if (OB_ISNULL(services->local_runtime = OB_NEW(
          RemoteRootserverLocalRuntime, ObModIds::OB_SCHEMA_SERVICE, ns))) {
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
          *services->schema_service))) {
  } else {
    stage = "done";
  }
  fprintf(stderr,
      "PROTOTYPE_INPROCESS_NS_ACTIVATE ns=%llu stage=%s ret=%d\n",
      static_cast<unsigned long long>(ns), stage, ret);
  if (!ret) {
    runtime.set_service(ns::NamespaceRuntime::SCHEMA_SERVICE, services->schema_service);
    runtime.set_service(ns::NamespaceRuntime::PLAN_CACHE, services->plan_cache);
    runtime.set_service(ns::NamespaceRuntime::ROOT_COMMAND_SERVICE, services->root_commands);
    runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_SERVICE, &services->direct_insert);
    runtime.set_service(ns::NamespaceRuntime::DIRECT_INSERT_ROUTES, &services->direct_insert_routes);
    runtime.set_service(ns::NamespaceRuntime::SQL_PROXY, services->sql_proxy);
    inprocess_services.emplace(ns, std::move(services));
  }
  return ret;
}
int ensure_in_process_namespace(uint64_t ns)
{
  int ret = OB_SUCCESS;
  ns::NamespaceRuntime *runtime = nullptr;
  if (ns <= 1 || ns >= (1ULL << 30) || !forked_in_process()) {
    ret = OB_NOT_SUPPORTED;
  } else if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (!ret && runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE) == nullptr) {
    std::unique_lock<std::shared_mutex> guard(inprocess_services_mutex);
    if (runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE) == nullptr) {
      ret = activate_in_process_namespace(ns, *runtime);
    }
  }
  return ret;
}
// Per-command schema advance for an in-process forked namespace, mirroring
// the worker's refresh_namespace_schema: a one-time static full refresh,
// then incremental refresh on every command.
int inprocess_refresh_schema(uint64_t ns)
{
  std::shared_lock<std::shared_mutex> guard(inprocess_services_mutex);
  auto it = inprocess_services.find(ns);
  if (it == inprocess_services.end()) { return OB_NOT_INIT; }
  InProcessNamespaceServices &services = *it->second;
  int ret = OB_SUCCESS;
  if (!services.schema_loaded.load(std::memory_order_acquire)) {
    bool expected = false;
    if (!services.schema_loaded.compare_exchange_strong(expected, true)) {
      return OB_SUCCESS; // A concurrent first refresh drives the full load.
    }
    ret = services.schema_service->refresh_runtime_schema_from_static_system();
    if (ret) { services.schema_loaded.store(false, std::memory_order_release); }
  } else {
    ret = services.schema_service->refresh_and_add_schema(false);
  }
  guard.unlock();
  bool expected = false;
  if (!ret && services.recovery_loaded.compare_exchange_strong(expected, true)) {
    rootserver::ObDDLTaskContext context;
    context.namespace_id_ = ns;
    context.sql_proxy_ = services.sql_proxy;
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
  if (worker_process || ns <= 1) {
    return &share::schema::ObMultiVersionSchemaService::get_instance();
  }
  ns::NamespaceRuntime *runtime = nullptr;
  if (!ns::namespace_registry().get(ns, runtime) || runtime == nullptr) {
    return nullptr;
  }
  return static_cast<share::schema::ObMultiVersionSchemaService *>(
      runtime->service(ns::NamespaceRuntime::SCHEMA_SERVICE));
}
} } }
