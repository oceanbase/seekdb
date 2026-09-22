// Throwaway V14 SQL-only composition. Included by ob_server.cpp so the prototype
// can reuse the existing composition owner without a second server object graph.
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/plan_cache/ob_ps_cache.h"
#include "sql/engine/ob_sql_memory_manager.h"
#include "sql/engine/expr/ob_lob_result_materializer.h"
#include "sql/ob_result_set.h"
#include "observer/omt/ob_srs_service.h"
#include "observer/omt/ob_server_module_lifecycle.h"
#include "sql/optimizer/stat/ob_opt_stat_monitor_manager.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/das/ob_das_context.h"
#include "sql/das/ob_data_access_service.h"
#include "sql/dtl/ob_dtl_interm_result_manager.h"
#include "share/ob_autoincrement_service.h"
#include "rootserver/ddl_task/ob_ddl_scheduler.h"
#include "rootserver/ob_ddl_service_launcher.h"
#include "rootserver/ob_max_id_cache_adapter.h"
#include "observer/schema/ob_schema_service_sql_impl.h"
#include <memory>
#include "namespace/namespace.h"
#include "sql/resolver/cmd/ob_variable_set_stmt.h"
#include "sql/resolver/ddl/ob_use_database_stmt.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "rpc/obmysql/ob_sql_nio_server.h"
#include <limits.h>
#include <string.h>
#include <unistd.h>

namespace oceanbase { namespace observer {
int ObServer::namespace_sql_worker_prototype(const char *query)
{
  namespace_worker_prototype::worker_process = true;
  namespace_worker_prototype::worker_bootstrapping = true;
  scramble_rand_.init(static_cast<uint64_t>(start_time_), static_cast<uint64_t>(start_time_ / 2));
  using namespace sql;
  using namespace common;
  using namespace share;
  int ret = OB_SUCCESS;
  bool namespace_schema_recovered = false;
  namespace_worker_prototype::Frame bootstrap;
  ret = namespace_worker_prototype::worker_read_wire(bootstrap);
  if (ret || bootstrap.type() != 'B') { return ret ? ret : OB_INVALID_ARGUMENT; }
  const uint64_t logical_port = bootstrap.number();
  const ObString logical_ip = bootstrap.string();
  const uint64_t configured_memory_budget = bootstrap.number();
  const uint64_t configured_tls = bootstrap.number();
  const ObString configured_min_tls = bootstrap.string();
  const ObString configured_invited_common_names = bootstrap.string();
  const uint64_t configured_system_package_ready = bootstrap.number();
  const std::string logical_host(logical_ip.ptr(), logical_ip.length());
  const std::string min_tls_version(
      configured_min_tls.ptr(), configured_min_tls.length());
  const std::string invited_common_names(
      configured_invited_common_names.ptr(), configured_invited_common_names.length());
  if (!bootstrap.consumed() || logical_port == 0 || logical_port > UINT16_MAX
      || configured_memory_budget < 256L * 1024 * 1024
      || configured_memory_budget > static_cast<uint64_t>(INT64_MAX)
      || configured_tls > 1 || min_tls_version.empty()
      || configured_system_package_ready > 1
      || invited_common_names.empty()
      || !self_addr_.set_ip_addr(logical_host.c_str(), static_cast<int>(logical_port))) { return OB_INVALID_ARGUMENT; }
  if (query[0] != '@') { return OB_NOT_SUPPORTED; }
  char *namespace_end = nullptr;
  namespace_worker_prototype::worker_namespace = std::strtoull(query + 1, &namespace_end, 10);
  if (!namespace_end || *namespace_end || namespace_worker_prototype::worker_namespace == 0
      || namespace_worker_prototype::worker_namespace >= (1ULL << 30)) { return OB_INVALID_ARGUMENT; }
  // Boundary-layer skeleton (Phase 1a): this worker process owns exactly one
  // Namespace/Runtime pair. The name stays empty until the spawn protocol
  // carries it; name-routed logins arrive pre-validated via the proxy entry.
  if (0 != ns::namespace_registry().add(namespace_worker_prototype::worker_namespace, "")) {
    return OB_ERR_UNEXPECTED;
  }
  namespace_worker_prototype::RemoteTabletScan remote_scan;
  namespace_worker_prototype::RemoteLobReadService remote_lob_read;
  int64_t concurrency = 2;
  if (const char *value = std::getenv("SEEKDB_NAMESPACE_SQL_WORKER_THREADS")) {
    char *end = nullptr; concurrency = std::strtol(value, &end, 10);
    if (!*value || !end || *end || concurrency < 1 || concurrency > 8) { return OB_INVALID_ARGUMENT; }
  }
  lib::Worker worker;
  lib::Worker::set_worker_to_thread_local(&worker);
  ObPLogWriterCfg log_cfg;
  OB_LOGGER.init(log_cfg);
  OB_LOGGER.set_file_name("worker-prototype.log", true, false);
  OB_LOGGER.set_log_level("WARN");
  OB_LOGGER.set_enable_async_log(false);
  const int64_t budget = static_cast<int64_t>(configured_memory_budget);
  // The shared process owns the resource policy and passes the effective
  // per-worker budget at spawn. reload_config derives cache sizing from it;
  // memory_limit is ignored.
  config_.memory_budget = budget;
  config_.cpu_count.set_value(std::to_string(concurrency).c_str());
  config_.enable_async_syslog.set_value("false");
  config_.ssl_client_authentication.set_value(configured_tls ? "true" : "false");
  config_.sql_protocol_min_tls_version.set_value(min_tls_version.c_str());
  config_.ob_ssl_invited_common_names.set_value(invited_common_names.c_str());
  config_._pushdown_storage_level.set_value("0");
  config_._rowsets_max_rows.set_value("32");
  config_.enable_sql_operator_dump.set_value("false");
  // These are the seekdb bootstrap baseline. Configuration persistence stays
  // in the shared process, while its validation SQL reads the worker-local
  // parameter virtual table.
  config_.enable_record_trace_log.set_value("false");
  config_._enable_dbms_job_package.set_value("false");
  config_._bloom_filter_ratio.set_value("3");
  // Client endpoints differ; these processes execute on one logical storage
  // server. Native transaction routing compares this identity with its owner.
  config_.self_addr_ = self_addr_;
  lib::update_mini_mode(budget, concurrency);
  set_memory_budget(budget);
  g_bootstrap_server_runtime.init();
  g_bootstrap_server_runtime.set_memory_size(budget);
  g_bootstrap_server_runtime.set_min_cpu(concurrency);
  g_bootstrap_server_runtime.set_max_cpu(concurrency);
  g_bootstrap_server_runtime.set_role(ObServerRole::PRIMARY_ROLE);
  fprintf(stderr,
      "PROTOTYPE_NAMESPACE_WORKER_RESOURCES ns=%llu memory_budget=%lld threads=%lld tls=%llu min_tls=%s\n",
      static_cast<unsigned long long>(namespace_worker_prototype::worker_namespace),
      static_cast<long long>(budget), static_cast<long long>(concurrency),
      static_cast<unsigned long long>(configured_tls), min_tls_version.c_str());
  // Each step is reported outside the protocol while bootstrapping is proved.
#define WORKER_STEP(expr) do { if (OB_SUCC(ret)) { ret = (expr); \
  fprintf(stderr, "worker bootstrap: %s => %d\n", #expr, ret); } } while (0)
  WORKER_STEP(GMEMCONF.reload_config(config_));
  WORKER_STEP(init_pre_setting());
  WORKER_STEP(init_global_context());
  if (OB_SUCC(ret)) {
    ATOMIC_STORE(&GCTX.sys_package_ready_, configured_system_package_ready != 0);
  }
  WORKER_STEP(init_interrupt());
  WORKER_STEP(ObTimerService::get_instance().start());
  WORKER_STEP(init_config_module(""));
  WORKER_STEP(init_tz_info_mgr());
  WORKER_STEP(ObQueryRetryCtrl::init());
  WORKER_STEP(sql::init_sql_factories());
  WORKER_STEP(sql::init_sql_executor_singletons());
  WORKER_STEP(sql::init_sql_expr_static_var());
  WORKER_STEP(ObPreProcessSysVars::init_sys_var(ObServerOptions::KeyValueArray()));
  WORKER_STEP(ObBasicSessionInfo::init_sys_vars_cache_base_values());
  WORKER_STEP(init_global_kvcache());
  WORKER_STEP(init_sql_proxy());
  // AUTO_INCREMENT allocation is SQL-side state. Its durable sequence rows
  // are read and written through this worker's routed SQL proxy, so they stay
  // in the namespace selected by the worker/storage IPC channel.
  WORKER_STEP(ObAutoincrementService::get_instance().init(&sql_proxy_));
  WORKER_STEP(schema_status_proxy_.init());
  WORKER_STEP(init_schema());
  if (OB_SUCC(ret) && namespace_worker_prototype::worker_namespace != 0) {
    // Static system-table definitions are identical in every namespace. Seed
    // them locally so incremental refresh can query this namespace's
    // __all_ddl_operation without first scanning every schema history table.
    ObArenaAllocator allocator("NsSysSchema");
    ObSArray<share::schema::ObTableSchema> system_schemas;
    WORKER_STEP(share::schema::ObSchemaUtils::construct_inner_table_schemas(
        system_schemas, allocator, true));
    WORKER_STEP(share::schema::ObSchemaUtils::generate_hard_code_schema_version(system_schemas));
    const int64_t core_schema_version =
        share::schema::ObSchemaUtils::get_inner_table_core_schema_version(system_schemas);
    const int64_t system_schema_version =
        share::schema::ObSchemaUtils::get_inner_table_sys_schema_version(system_schemas);
    fprintf(stderr, "PROTOTYPE_NATIVE_SCHEMA_BASELINE core=%lld sys=%lld count=%lld\n",
        (long long)core_schema_version, (long long)system_schema_version,
        (long long)system_schemas.count());
    WORKER_STEP(schema_service_.broadcast_runtime_schema(
        system_schemas, system_schema_version));
  }
  // DDL belongs to the namespace worker together with parsing, schema and
  // inner SQL.  The local management service writes this worker's native
  // system tables through sql_proxy_; tablet MDS is attached to the same
  // remote transaction by the data-plane transaction service.
  local_management_service_.set_local_command_service(ob_service_);
  WORKER_STEP(local_management_service_.init_sql_worker(
      config_, config_mgr_, self_addr_, sql_proxy_, schema_service_));
  gctx_.in_bootstrap_ = false;
  bind_server_service<rootserver::ObLocalManagementService>(&local_management_service_);
  vt_data_service_.get_vt_iter_factory().get_vt_iter_creator().set_schema_service(schema_service_);
  WORKER_STEP(session_mgr_.init());
  WORKER_STEP(server_module_new_default(mods_plan_cache_));
  WORKER_STEP(server_module_new_default(mods_ps_cache_));
  WORKER_STEP(server_module_new_default(mods_sql_memory_manager_));
  WORKER_STEP(server_module_new_default(mods_srs_service_));
  WORKER_STEP(server_module_new_default(mods_opt_stat_monitor_manager_));
  WORKER_STEP(server_module_new_default(mods_data_access_service_));
  WORKER_STEP(server_module_new_default(mods_shared_timer_));
  // Long-running DDL is SQL-side namespace state.  Give every namespace
  // worker its own native launcher/scheduler instead of falling back to the
  // storage process when CREATE INDEX or a database DROP creates a DDL task.
  WORKER_STEP(server_module_new_default(mods_ddl_service_launcher_));
  WORKER_STEP(server_module_new_default(mods_ddl_scheduler_));
  WORKER_STEP(dtl::ObDfc::server_module_new(mods_dfc_));
  WORKER_STEP(server_module_new_default(mods_px_pools_));
  WORKER_STEP(server_module_new_default(mods_dtl_interm_result_manager_));
  WORKER_STEP(storage::ObLobManager::server_module_new(mods_lob_manager_));
  remote_lob_read.set_local(mods_lob_manager_);
  bind_server_service<ObSQLSessionMgr>(&session_mgr_);
  bind_server_service<ObVTIterCreator>(&vt_data_service_.get_vt_iter_factory().get_vt_iter_creator());
  bind_server_service<ObPlanCache>(mods_plan_cache_);
  bind_server_service<ObPsCache>(mods_ps_cache_);
  bind_server_service<ObSqlMemoryManager>(mods_sql_memory_manager_);
  bind_server_service<ObOptStatMonitorManager>(mods_opt_stat_monitor_manager_);
  bind_server_service<ObDataAccessService>(mods_data_access_service_);
  bind_server_service<common::ObILobReadService>(mods_lob_manager_);
  bind_server_service<share::ObISharedTimer>(mods_shared_timer_);
  bind_server_service<dtl::ObDfc>(mods_dfc_);
  bind_server_service<omt::ObPxPools>(mods_px_pools_);
  bind_server_service<dtl::ObDTLIntermResultManager>(mods_dtl_interm_result_manager_);
  bind_server_service<rootserver::ObDDLServiceLauncher>(mods_ddl_service_launcher_);
  bind_server_service<rootserver::ObDDLScheduler>(mods_ddl_scheduler_);
  WORKER_STEP(omt::ObSharedTimer::server_module_init(mods_shared_timer_));
  WORKER_STEP(omt::ObSharedTimer::server_module_start(mods_shared_timer_));
  WORKER_STEP(DTL.init());
  WORKER_STEP(dtl::ObDfc::server_module_init(mods_dfc_));
  WORKER_STEP(omt::ObPxPools::server_module_init(mods_px_pools_));
  WORKER_STEP(dtl::ObDTLIntermResultManager::server_module_init(mods_dtl_interm_result_manager_));
  WORKER_STEP(dtl::ObDTLIntermResultManager::server_module_start(mods_dtl_interm_result_manager_));
  WORKER_STEP(ObOptStatMonitorManager::server_module_init(mods_opt_stat_monitor_manager_));
  WORKER_STEP(ObPlanCache::server_module_init(mods_plan_cache_, *this));
  WORKER_STEP(server_module_init_default(mods_lob_manager_));
  WORKER_STEP(server_module_start_default(mods_lob_manager_));
  WORKER_STEP(ObPsCache::server_module_init(mods_ps_cache_));
  WORKER_STEP(ObSqlMemoryManager::server_module_init(mods_sql_memory_manager_));
  WORKER_STEP(rootserver::ObDDLServiceLauncher::server_module_init(
      mods_ddl_service_launcher_));
  WORKER_STEP(rootserver::ObDDLScheduler::server_module_init(mods_ddl_scheduler_));
  WORKER_STEP(ObOptStatManager::get_instance().init(&sql_proxy_, &config_));
  bind_server_service<common::ObILobReadService>(&remote_lob_read);
  WORKER_STEP(sql_engine_.init(&ObOptStatManager::get_instance(), &remote_scan,
      self_addr_, *mods_plan_cache_, *mods_ps_cache_, pl_engine_, *this, *this,
      local_management_service_, ob_service_, *this, *this, *this,
      *mods_srs_service_, remote_lob_read));
  gctx_.status_ = SS_SERVING;
  g_server_modules_ready = OB_SUCC(ret);
  using namespace namespace_worker_prototype;
  if (ret != OB_SUCCESS) { return ret; }
  RemoteTransactionService remote_transactions;
  RemoteRootserverLocalRuntime remote_rootserver_runtime;
  rootserver::ObDebugSyncBroadcasterAdapter remote_debug_sync_broadcaster(
      remote_rootserver_runtime);
  RemoteInnerConnectionLockRuntime remote_inner_locks;
  RemoteDmlService remote_dml;
  RemoteWriteContext remote_write_context;
  RemoteRangeService remote_ranges;
  RemoteDirectInsertService remote_direct_insert;
  RemoteTabletAutoincrementAdmin remote_tablet_autoincrement_admin;
  {
    bind_server_service<ObITabletScan>(&remote_scan);
    // Virtual tables are SQL/session/schema views owned by this worker. Only
    // physical tablet access crosses the storage IPC boundary.
    bind_server_service<ObIVirtualTableScan>(&vt_data_service_);
    bind_server_service<data_plane::ObITransactionService>(&remote_transactions);
    bind_server_service<rootserver::ObIRootserverLocalRuntime>(
        &remote_rootserver_runtime);
    bind_server_service<transaction::tablelock::ObIInnerConnectionLockRuntime>(
        &remote_inner_locks);
    bind_server_service<data_plane::ObIRangeService>(&remote_ranges);
    bind_server_service<data_plane::IDirectInsertService>(&remote_direct_insert);
    // The native slice store persists scheduling metadata through inner SQL,
    // whose storage path is already remote in this worker.
    sql::register_ddl_slice_store(this);
    bind_server_service<data_plane::ObIDmlService>(&remote_dml);
    bind_server_service<data_plane::ObIWriteContextService>(&remote_write_context);
    bind_server_service<share::ObITabletAutoincrementAdmin>(
        &remote_tablet_autoincrement_admin);
    worker_catalog_fetch = fetch_catalog;
  }
  // Activation happens only after every data-plane service points at the
  // shared-storage gateway.  DDL executor threads therefore run in this
  // namespace worker while all physical reads/writes still cross IPC.
  WORKER_STEP(mods_ddl_service_launcher_->activate());
  WORKER_STEP(mods_ddl_scheduler_->activate());
  struct SessionOwner {
    ObArenaAllocator allocator{ObMemAttr("NsSQLSession")};
    ObSQLSessionInfo session; // Destroyed before its allocator.
    std::atomic<bool> running{false};
    common::sqlclient::ObISQLConnectionGuard inner;
  };
  struct SessionSlot {
    std::shared_ptr<SessionOwner> owner;
    uint64_t generation = 1;
  };
  std::mutex namespace_schema_mutex;
  std::atomic<bool> namespace_schema_loaded{namespace_schema_recovered};
  auto refresh_namespace_schema = [&]() -> int {
    int result = OB_SUCCESS;
    if (!namespace_schema_loaded.load(std::memory_order_acquire)) {
      std::lock_guard<std::mutex> lock(namespace_schema_mutex);
      if (!namespace_schema_loaded.load(std::memory_order_relaxed)) {
        result = schema_service_.refresh_runtime_schema_from_static_system();
        if (!result) {
          namespace_schema_loaded.store(true, std::memory_order_release);
        }
      }
    } else {
      result = schema_service_.refresh_and_add_schema(false);
    }
    return result;
  };
  std::mutex sessions_mutex;
  std::vector<SessionSlot> slots;
  std::vector<uint64_t> free_slots;
  uint64_t active_sessions = 0;
  auto initialize = [&](SessionOwner &owner, uint32_t sid, uint32_t capabilities, Frame *state, bool internal) -> int {
    int ret = OB_SUCCESS;
    ObSQLSessionInfo &session = owner.session;
    if (OB_FAIL(session.test_init(1, sid, &owner.allocator))) {
    } else if (OB_FAIL(session.load_default_sys_variable(false, false))) {
    } else if (OB_FAIL(session.set_user(
                   ObString::make_string("root"),
                   ObString::make_string("%"),
                   OB_SYS_USER_ID))) {
    }
    session.set_capability(obmysql::ObMySQLCapabilityFlags(capabilities));
    session.set_user_priv_set(OB_PRIV_SELECT | OB_PRIV_INSERT | OB_PRIV_UPDATE | OB_PRIV_DELETE);
    if (worker_namespace == 1) { session.set_user_priv_set(OB_PRIV_ALL | OB_PRIV_GRANT); }
    session.set_session_manager(&session_mgr_);
    if (!ret && internal) { ret = ObInnerSQLConnection::init_session_info(&session, false, false); }
    if (!ret && state) {
      ret = apply_session_state(session, *state);
      if (!ret && !state->consumed()) {
        const uint64_t user_id = state->number();
        const ObString user_name = state->string();
        const ObString host_name = state->string();
        if (!state->consumed() || state->ret || user_id > UINT64_MAX) {
          ret = OB_INVALID_ARGUMENT;
        } else {
          ret = session.set_user(user_name, host_name, user_id);
          if (!ret) {
            session.set_user_priv_set(user_id == OB_SYS_USER_ID
                ? OB_PRIV_ALL | OB_PRIV_GRANT
                : 0);
          }
        }
      }
      if (!ret && !internal && !session.get_database_name().empty()) {
        uint64_t database_id = OB_INVALID_ID;
        share::schema::ObSchemaGetterGuard guard;
        if (OB_FAIL(refresh_namespace_schema())) {
        } else if (OB_FAIL(schema_service_.get_runtime_schema_guard(guard))) {
        } else if (OB_FAIL(guard.get_database_id(
                       session.get_database_name(), database_id))) {
        } else if (database_id == OB_INVALID_ID) {
          ret = OB_ERR_BAD_DATABASE;
        } else {
          session.set_database_id(database_id);
        }
      }
      const uint64_t db = session.get_database_id();
      if (!ret && !internal && db != OB_INVALID_ID) {
        if (worker_namespace == 1) {
          // Namespace 1 owns the original physical objects, whose ids are
          // already namespace-local. The process binding is authoritative.
          if (storage::NamespaceForkKernelPrototype::is_encoded_id(db)) {
            ret = OB_INVALID_ARGUMENT;
          }
        } else {
          // Namespace is carried by the process and storage IPC route. Keep
          // schema identities local so the native schema service can consume
          // this namespace's system tables without reverse-routing object ids.
          uint64_t local_db = OB_INVALID_ID;
          ret = storage::NamespaceForkKernelPrototype::local_object_id(
              worker_namespace, db, local_db);
          if (!ret) { session.set_database_id(local_db); }
        }
      }
    }
    // Namespace workers generate plans from their private, version-pinned
    // schema cache.  Do not retain a second process-local plan-object cache;
    // the plan belongs to this request and is released with its result set.
    if (!ret && owns_namespace_schema()) { session.set_local_ob_enable_plan_cache(false); }
    if (!ret && internal) { ret = ObInnerSQLConnection::create_connection_with_external_session(&session, owner.inner); }
    return ret;
  };
  // Native expression/operator checkpoints dispatch through THIS_WORKER. The
  // base lib::Worker used by a plain thread does not check SQL timeout/cancel.
  class RequestWorker final : public lib::Worker {
  public:
    int check_status() override {
      int ret = worker_request ? worker_request->status() : OB_SUCCESS;
      if (!ret && get_session()) { get_session()->is_terminate(ret); }
      if (!ret && is_timeout()) { ret = OB_TIMEOUT; }
      return ret ? ret : lib::Worker::check_status();
    }
  };
  auto execute_inner = [&](SessionOwner &owner, Frame &input) -> int {
    const bool previous_inner_sql_execution = worker_inner_sql_execution;
    worker_inner_sql_execution = true;
    struct InnerSqlExecutionScope {
      bool previous;
      ~InnerSqlExecutionScope() { worker_inner_sql_execution = previous; }
    } inner_sql_execution_scope{previous_inner_sql_execution};
    auto *connection = static_cast<ObInnerSQLConnection *>(owner.inner.get_ptr());
    if (!connection) { return OB_INVALID_ARGUMENT; }
    THIS_WORKER.set_session(&owner.session);
    struct SessionScope { ~SessionScope() { THIS_WORKER.set_session(nullptr); } } scope;
    const int64_t deadline = input.number();
    THIS_WORKER.set_timeout_ts(deadline);
    const bool previous_shared_bootstrap_request = worker_shared_bootstrap_request;
    worker_shared_bootstrap_request = input.number() != 0;
    struct SharedBootstrapRequestScope {
      bool previous;
      ~SharedBootstrapRequestScope() { worker_shared_bootstrap_request = previous; }
    } shared_bootstrap_request_scope{previous_shared_bootstrap_request};
    ObSessionDDLInfo ddl; input.read(ddl); owner.session.set_ddl_info(ddl);
    const uint64_t operation = input.number();
    int ret = input.ret;
    int64_t affected = 0;
    if (!ret && (operation == 'R' || operation == 'W')) {
      const bool user_sql = input.number() != 0;
      const ObString text = input.string();
      if (!input.consumed()) { return OB_INVALID_ARGUMENT; }
      if (operation == 'W') { ret = connection->execute_write(text, affected, user_sql); }
      else {
        ObISQLClient::ReadResult result;
        ret = connection->execute_read(text, result, user_sql);
        if (!ret) {
          auto *native = static_cast<ObInnerSQLResult *>(result.get_result());
          const auto *fields = native->result_set().get_field_columns();
          if (!fields) { return OB_ERR_UNEXPECTED; }
          Frame metadata('m'); metadata.number(fields->count());
          for (int64_t i = 0; i < fields->count(); ++i) { metadata.string(fields->at(i).cname_); }
          ret = worker_send(metadata);
          ObArenaAllocator row_allocator(ObMemAttr("NsInnerResult"));
          while (!ret && !(ret = native->next())) {
            row_allocator.reuse();
            const ObNewRow *row = native->get_row();
            if (!row) { ret = OB_ERR_UNEXPECTED; break; }
            Frame values('r'); values.number(row->get_count());
            for (int64_t i = 0; !ret && i < row->get_count(); ++i) {
              ObObj value = row->get_cell(i);
              ret = materialize_lob_result(
                  value, &row_allocator, owner.session, &remote_lob_read);
              if (!ret) {
                values.write_object(value);
                ret = values.ret;
              }
            }
            if (!ret) { ret = worker_send(values); }
          }
          if (ret == OB_ITER_END) { ret = OB_SUCCESS; }
          const int close_ret = native->close();
          if (!ret) { ret = close_ret; }
        }
      }
    } else if (!ret && operation == 'B') {
      const bool snapshot = input.number() != 0;
      ret = input.consumed() ? connection->start_transaction(snapshot) : OB_INVALID_ARGUMENT;
    } else if (!ret && operation == 'C') {
      ret = input.consumed() ? connection->commit() : OB_INVALID_ARGUMENT;
    } else if (!ret && operation == 'X') {
      ret = input.consumed() ? connection->rollback() : OB_INVALID_ARGUMENT;
    } else if (!ret && operation == 'S') {
      const ObString name = input.string(); ObObj value; input.read(value);
      if (!input.consumed()) { ret = OB_INVALID_ARGUMENT; }
      else { ret = value.is_int() ? connection->set_session_variable(name, value.get_int())
                                 : connection->set_session_variable(name, value.get_string()); }
    } else if (!ret) { ret = OB_NOT_SUPPORTED; }
    if (!ret && operation != 'R') { Frame result('o'); result.number(affected); ret = worker_send(result); }
    fprintf(stderr, "PROTOTYPE_V18_INNER_EXECUTE session=%u operation=%c ret=%d\n", owner.session.get_server_sid(), char(operation), ret);
    return ret;
  };
  struct Job {
    Frame input;
    std::shared_ptr<PendingRequest> request;
    std::shared_ptr<SessionOwner> owner;
  };
  RequestRoutes requests;
  std::mutex jobs_mutex;
  std::condition_variable jobs_changed;
  std::deque<Job> jobs;
  bool stopping = false;
  auto complete = [&](const std::shared_ptr<PendingRequest> &request, int result, SessionOwner *owner = nullptr) {
    if (owner && !result) { result = request->status(); }
    Frame done('D'); done.tag(request->tag); done.number(result);
    if (owner) {
      if (append_session_state(owner->session, done)) { std::_Exit(1); }
      owner->running = false;
    }
    // Release before publishing D: the peer may immediately reuse this slot.
    requests.release(request->tag);
    fprintf(stderr, "PROTOTYPE_V13_DONE request=%llu generation=%llu ret=%d\n",
        (unsigned long long)request->tag.slot, (unsigned long long)request->tag.generation, result);
    if (worker_send_wire(std::move(done))) { std::_Exit(1); }
  };
  // Phase 1b spike (ticket 04): prove a second ObMultiVersionSchemaService
  // instance can init and refresh inside this process, next to THE_ONE that
  // the whole composition is wired to. Enabled by SEEKDB_NS_SCHEMA_SPIKE=1;
  // the prototype never runs it in production. Spike objects intentionally
  // leak: the process exits when the test finishes.
  auto run_schema_spike = [&]() {
    if (nullptr == std::getenv("SEEKDB_NS_SCHEMA_SPIKE")) { return; }
    // Refresh issues inner SQL whose storage scans must carry this worker's
    // routing identity; take a direct-request binding like the V19 probe.
    SessionBinding *spike_binding = nullptr;
    if (OB_SUCCESS != begin_direct_request(0, spike_binding, true)) { return; }
    {
    int spike_ret = OB_SUCCESS;
    // The ctor is protected to enforce the THE_ONE singleton; per-ns
    // instances need that opened up (see the spike report).
    class SpikeSchemaService final : public share::schema::ObMultiVersionSchemaService {};
    SpikeSchemaService *spike_service = nullptr;
    share::schema::ObSchemaPublishSignal *spike_signal = nullptr;
    rootserver::ObMaxIdCacheAdapter *spike_max_id = nullptr;
    share::schema::ObSchemaServiceSQLImpl *spike_backend = nullptr;
    ObSchemaRefreshSchedulerAdapter *spike_scheduler = nullptr;
    int64_t the_one_version = OB_INVALID_VERSION;
    int64_t spike_version = OB_INVALID_VERSION;
    const char *stage = "alloc";
    if (OB_ISNULL(spike_service = OB_NEW(SpikeSchemaService,
        ObModIds::OB_SCHEMA_SERVICE))
        || OB_ISNULL(spike_signal = OB_NEW(share::schema::ObSchemaPublishSignal,
        ObModIds::OB_SCHEMA_SERVICE))
        || OB_ISNULL(spike_max_id = OB_NEW(rootserver::ObMaxIdCacheAdapter,
        ObModIds::OB_SCHEMA_SERVICE, local_management_service_))
        || OB_ISNULL(spike_backend = OB_NEW(share::schema::ObSchemaServiceSQLImpl,
        ObModIds::OB_SCHEMA_SERVICE, spike_max_id, ddl_sql_proxy_, *spike_service))
        || OB_ISNULL(spike_scheduler = OB_NEW(ObSchemaRefreshSchedulerAdapter,
        ObModIds::OB_SCHEMA_SERVICE, ob_service_, *spike_service))) {
      spike_ret = OB_ALLOCATE_MEMORY_FAILED;
    } else if (FALSE_IT(stage = "signal_init")) {
    } else if (OB_SUCCESS != (spike_ret = spike_signal->init())) {
    } else if (FALSE_IT(stage = "service_init")) {
    } else if (OB_SUCCESS != (spike_ret = spike_service->init(
        &sql_proxy_, &config_, schema_status_proxy_, gctx_.status_,
        gctx_.in_bootstrap_, OB_MAX_VERSION_COUNT, *spike_backend,
        *spike_scheduler, *spike_signal, "spike"))) {
    } else if (FALSE_IT(stage = "broadcast")) {
    } else if (FALSE_IT([&] {
        // Inner-SQL refresh needs a session-bound storage routing context
        // (ticket 08 territory), so the spike pushes versions directly, the
        // same way this worker seeds its own baseline above.
        ObArenaAllocator allocator("NsSpike");
        ObSArray<share::schema::ObTableSchema> system_schemas;
        if (OB_SUCCESS != (spike_ret = share::schema::ObSchemaUtils::construct_inner_table_schemas(
            system_schemas, allocator, true))) {
        } else if (OB_SUCCESS != (spike_ret = share::schema::ObSchemaUtils::generate_hard_code_schema_version(
            system_schemas))) {
        } else {
          const int64_t sys_version =
              share::schema::ObSchemaUtils::get_inner_table_sys_schema_version(system_schemas);
          spike_ret = spike_service->broadcast_runtime_schema(system_schemas, sys_version);
        }
      }())) {
    } else if (FALSE_IT(stage = "version_second")) {
    } else if (OB_SUCCESS != (spike_ret =
        spike_service->get_runtime_refreshed_schema_version(spike_version))) {
    } else if (FALSE_IT(stage = "version_the_one")) {
    } else if (OB_SUCCESS != (spike_ret =
        schema_service_.get_runtime_refreshed_schema_version(the_one_version))) {
    } else {
      stage = "done";
    }
    fprintf(stderr,
        "PROTOTYPE_SCHEMA_SPIKE stage=%s ret=%d the_one_version=%lld second_version=%lld\n",
        stage, spike_ret, static_cast<long long>(the_one_version),
        static_cast<long long>(spike_version));
  }
    finish_direct_request();
    close_session(spike_binding);
  };
  bool spike_ran = false;
  auto execute_job = [&](Job &job) -> int {
    if (!job.request->call_trace.is_valid()) { job.request->call_trace.init(self_addr_); }
    ObTraceIdGuard trace_guard(job.request->call_trace);
    struct CallTraceScope {
      const ObCurTraceId::TraceId *previous = worker_call_trace;
      explicit CallTraceScope(const ObCurTraceId::TraceId &trace) { worker_call_trace = &trace; }
      ~CallTraceScope() { worker_call_trace = previous; }
    } call_trace_scope(job.request->call_trace);
    lib::Worker *previous_worker = &THIS_WORKER;
    PendingRequest *previous_request = worker_request;
    RequestWorker request_worker;
    lib::Worker::set_worker_to_thread_local(&request_worker);
      worker_request = job.request.get();
      worker_request->sql_session = job.owner ? &job.owner->session : nullptr;
      THIS_WORKER.set_timeout_ts(job.request->deadline);
      Frame &input = job.input;
      int result = OB_SUCCESS;
      if (input.type() == 'V') {
        obcall::ObAdminSetConfigArg arg;
        input.read(arg);
        int64_t applied = 0;
        int64_t restart_required = 0;
        result = input.consumed() && arg.is_valid()
            ? apply_dynamic_worker_config(arg, applied, restart_required)
            : OB_INVALID_ARGUMENT;
        fprintf(stderr,
            "PROTOTYPE_NAMESPACE_WORKER_CONFIG_APPLIED ns=%llu applied=%lld restart=%lld ret=%d\n",
            static_cast<unsigned long long>(worker_namespace),
            static_cast<long long>(applied),
            static_cast<long long>(restart_required), result);
      } else if (input.type() == 'E') {
        const uint64_t ready = input.number();
        if (!input.consumed() || ready > 1) {
          result = OB_INVALID_ARGUMENT;
        } else {
          ATOMIC_STORE(&GCTX.sys_package_ready_, ready != 0);
        }
        fprintf(stderr,
            "PROTOTYPE_NAMESPACE_WORKER_SYSTEM_PACKAGE_READY ns=%llu ready=%llu ret=%d\n",
            static_cast<unsigned long long>(worker_namespace),
            static_cast<unsigned long long>(ready), result);
      } else if (input.type() == 'H') {
        const uint64_t refresh_control_schema = input.number();
        if (!input.consumed() || refresh_control_schema > 1) {
          result = OB_INVALID_ARGUMENT;
        }
        // Namespace 1 owns the GLOBAL control schema.  Load that schema in its
        // private SchemaService before the shared process publishes this
        // Channel; endpoint publication is itself SQL against a GLOBAL table.
        // Child namespaces stay lazy so ordinary fork activation remains O(1).
        // Initial cluster bootstrap cannot do this yet because its physical
        // system tablets are created after the first Worker is started.
        if (!result && refresh_control_schema) {
          result = worker_namespace == 1
              ? refresh_namespace_schema() : OB_INVALID_ARGUMENT;
        }
        // Exercise a worker-originated route with no gateway SQL exchange
        // serving its storage frames.  Avoid a shared SchemaService lookup
        // here: a cache miss would recursively open this not-yet-published
        // namespace 1 Channel.  The sid-less internal route additionally skips
        // default variable loading, which can recurse into this still
        // initializing Worker.
        SessionBinding *binding = nullptr;
        if (!result) { result = begin_direct_request(0, binding, true); }
        const int finish_ret = finish_direct_request();
        if (!result) { result = finish_ret; }
        close_session(binding);
        worker_request = job.request.get();
        fprintf(stderr, "PROTOTYPE_V19_DIRECT_STORAGE_PROBE ns=%llu ret=%d\n", (unsigned long long)worker_namespace, result);
      } else if (input.type() == 'A' || input.type() == 'a') {
        const uint64_t sid = input.number(), capabilities = input.number();
        auto owner = std::make_shared<SessionOwner>();
        result = input.ret || sid == 0 || sid > UINT32_MAX || capabilities > UINT32_MAX
            ? OB_INVALID_ARGUMENT : initialize(*owner, sid, capabilities, &input, input.type() == 'a');
        if (!result) {
          Frame opened('a');
          {
            std::lock_guard<std::mutex> guard(sessions_mutex);
            uint64_t index;
            if (free_slots.empty()) { index = slots.size(); slots.emplace_back(); }
            else { index = free_slots.back(); free_slots.pop_back(); }
            slots[index].owner = owner; ++active_sessions;
            opened.number(index); opened.number(slots[index].generation);
            fprintf(stderr, "PROTOTYPE_V11_SESSION_OPEN ns=%llu slot=%llu generation=%llu active=%llu slots=%zu capacity=%zu\n",
                (unsigned long long)worker_namespace, (unsigned long long)index,
                (unsigned long long)slots[index].generation, (unsigned long long)active_sessions, slots.size(), slots.capacity());
          }
          result = worker_send(opened);
          if (!result && !spike_ran) { spike_ran = true; run_schema_spike(); }
        }
      } else if (input.type() == 'I') {
        result = execute_inner(*job.owner, input);
      } else {
        result = OB_NOT_SUPPORTED;
      }
      complete(job.request, result, job.owner.get());
    worker_request = previous_request;
    lib::Worker::set_worker_to_thread_local(previous_worker);
    return OB_SUCCESS;
  };
  auto run_job = [&](Job &job) {
    // A cooperative nested job can enter from deep inside native SQL. Reuse
    // native stack extension before constructing another request context.
    const int error = SMART_CALL_LARGE(execute_job(job));
    if (error) { complete(job.request, error, job.owner.get()); }
  };
  PrototypeThreads executors(concurrency, [&] {
    lib::set_thread_name("NsSQLExecute");
    int depth = 0;
    if (worker_namespace == 1) {
      // Native internal callers can start another query before closing a streamed
      // result. Only nest work from that call chain: unrelated SQL may need a lock
      // held by the suspended caller and prevent its ready reply from being read.
      // The pipe reader still only dispatches; no polling or extra thread.
      worker_wait = [&](PendingRequest &waiting, bool credit, bool draining) {
        auto ready = [&] {
          std::lock_guard<std::mutex> guard(waiting.mutex);
          return waiting.error || (credit ? waiting.credit : bool(waiting.incoming || waiting.terminal))
              || (!draining && waiting.status());
        };
        std::unique_lock<std::mutex> lock(jobs_mutex);
        while (!stopping && !ready()) {
          auto internal = std::find_if(jobs.begin(), jobs.end(), [](const Job &job) {
            return (job.input.type() == 'a' || job.input.type() == 'I')
                && worker_call_trace && job.request->call_trace == *worker_call_trace;
          });
          if (internal != jobs.end()) {
            {
              Job nested = std::move(*internal); jobs.erase(internal);
              lock.unlock();
              if (depth >= 8) { complete(nested.request, OB_SIZE_OVERFLOW, nested.owner.get()); }
              else { ++depth; run_job(nested); --depth; }
              // Releasing the last session owner can issue storage RPCs and
              // reenter this wait loop. Keep destruction outside jobs_mutex.
            }
            lock.lock();
          } else if (draining || waiting.deadline == INT64_MAX) {
            jobs_changed.wait(lock);
          } else {
            jobs_changed.wait_until(lock,
                std::chrono::system_clock::time_point(std::chrono::microseconds(waiting.deadline)));
          }
        }
      };
    }
    for (;;) {
      Job job;
      {
        std::unique_lock<std::mutex> lock(jobs_mutex);
        jobs_changed.wait(lock, [&] { return stopping || !jobs.empty(); });
        if (stopping) { break; }
        job = std::move(jobs.front()); jobs.pop_front();
      }
      run_job(job);
    }
    worker_wait = {};
  });
  if ((ret = executors.start())) { return ret; }
  fprintf(stderr, "PROTOTYPE_V12_EXECUTORS count=%lld max_requests=%zu\n", (long long)concurrency, MAX_REQUESTS);
  WORKER_STEP(server_runtime_controller_.init_sql_worker_runtime());
  bind_server_service<omt::ObServerRuntimeController>(&server_runtime_controller_);
  WORKER_STEP(conn_res_mgr_.init(schema_service_, server_gtimer_));
  session_mgr_.bind_lifecycle_services(
      *mods_ps_cache_, remote_debug_sync_broadcaster, conn_res_mgr_);
  WORKER_STEP(server_gtimer_.schedule(session_mgr_, ObSQLSessionMgr::SCHEDULE_PERIOD, true));
  // Clients reach this worker through its Unix socket only: the shared entry
  // proxies client connections to it, diagnostics may connect directly.
  config_.mysql_port_mode.set_value("disabled");
  config_.sql_net_thread_count.set_value("1");
  WORKER_STEP(net_frame_.init());
  WORKER_STEP(net_frame_.start());
  if (ret) { std::_Exit(1); }
  Frame ready('Y'); ready.number(worker_namespace);
  // The NIO layer binds run/sql.sock under this worker's base directory.
  // Publish it relative to the instance base (the shared process working
  // directory): an absolute path can exceed the AF_UNIX sun_path limit for
  // deep deployment directories.
  char cwd[PATH_MAX];
  if (!getcwd(cwd, sizeof(cwd))) { return OB_ERR_UNEXPECTED; }
  const char *dir = strrchr(cwd, '/');
  char client_endpoint[PATH_MAX];
  snprintf(client_endpoint, sizeof(client_endpoint),
           "run/%s/run/sql.sock", dir ? dir + 1 : cwd);
  ready.string(ObString::make_string(client_endpoint));
  // Publish the same native serving state as ObServer::start(). SQL executors
  // use it to distinguish an asynchronous DDL wait from server shutdown.
  prepare_stop_ = false;
  stop_ = false;
  has_stopped_ = false;
  if (OB_SUCC(ret) && worker_namespace > 1) {
    // A dead Worker may have committed native all_* rows after it marked the
    // namespace dirty but before it published the matching directory delta.
    // Reconcile before opening the client listener, then atomically clear the
    // persistent fork fence. No user session can race this startup repair.
    IndependentStorageScope storage_scope;
    int64_t directory_schema_version = OB_INVALID_VERSION;
    int64_t worker_schema_version = OB_INVALID_VERSION;
    int64_t published_schema_version = OB_INVALID_VERSION;
    bool recovery_needed = false;
    WORKER_STEP(storage_scope.error());
    WORKER_STEP(begin_namespace_schema_recovery(recovery_needed));
    if (OB_SUCC(ret) && recovery_needed) {
      WORKER_STEP(fetch_schema_version(false, false, directory_schema_version));
      WORKER_STEP(schema_service_.refresh_runtime_schema_from_static_system());
      WORKER_STEP(schema_service_.get_runtime_refreshed_schema_version(
          worker_schema_version));
      if (OB_SUCC(ret) && worker_schema_version < directory_schema_version) {
        ret = OB_STATE_NOT_MATCH;
      }
      if (OB_SUCC(ret) && worker_schema_version > directory_schema_version) {
        WORKER_STEP(sync_namespace_schema_delta(
            worker_namespace, directory_schema_version, published_schema_version));
      } else if (OB_SUCC(ret)) {
        published_schema_version = worker_schema_version;
      }
      WORKER_STEP(finish_namespace_schema_recovery(published_schema_version));
      namespace_schema_recovered = OB_SUCC(ret);
    }
  }
  // worker_read keeps its bootstrap wire-pump until the recovery round trips
  // above are done; the posted-reply path needs the main read loop, which
  // starts only after the ready frame goes out below.
  worker_bootstrapping = false;
  ret = worker_send_wire(ready);
  while (!ret) {
    Frame input;
    if ((ret = worker_read_wire(input))) { break; }
    const RequestTag tag = input.tag();
    if (tag.slot & WORKER_REQUEST) {
      auto request = worker_storage_routes.find(tag);
      if (!request || (input.type() != 'K' && input.type() != 'l' && input.type() != 'c'
          && input.type() != 's' && input.type() != 'w' && input.type() != 'g'
          && input.type() != 'r')) { ret = OB_INVALID_ARGUMENT; break; }
      ret = request->post(std::move(input));
      if (worker_namespace == 1) { std::lock_guard<std::mutex> guard(jobs_mutex); jobs_changed.notify_all(); }
      continue;
    }
    if (input.ret || !tag.generation || tag.slot >= MAX_REQUESTS) { ret = OB_INVALID_ARGUMENT; break; }
    if (input.type() == 'Z') {
      const int reason = static_cast<int>(input.number());
      if (!input.consumed() || (reason != OB_TIMEOUT && reason != OB_ERR_QUERY_INTERRUPTED)) {
        ret = OB_INVALID_ARGUMENT; break;
      }
      auto request = requests.find(tag);
      if (!request || !request->cancellable) { continue; }
      request->cancel(reason);
      std::optional<Job> queued;
      {
        std::lock_guard<std::mutex> guard(jobs_mutex);
        for (auto it = jobs.begin(); it != jobs.end(); ++it) {
          if (it->request == request) { queued = std::move(*it); jobs.erase(it); break; }
        }
      }
      // Queued cancellation must not wait for an execution thread to become free.
      if (queued) { complete(request, reason, queued->owner.get()); }
      jobs_changed.notify_all();
      continue;
    }
    if (input.type() == 'K' || input.type() == 'c' || input.type() == 's' || input.type() == 'w'
        || input.type() == 'g' || input.type() == 'r') {
      auto request = requests.find(tag);
      if (request) { ret = request->post(std::move(input)); }
      if (worker_namespace == 1) { std::lock_guard<std::mutex> guard(jobs_mutex); jobs_changed.notify_all(); }
      continue;
    }
    auto request = requests.accept(tag);
    if (!request) { ret = OB_INVALID_ARGUMENT; break; }
    if (input.type() == 'a' || input.type() == 'I') {
      input.read(request->call_trace);
      if (input.ret || !request->call_trace.is_valid()) { ret = OB_INVALID_ARGUMENT; break; }
    }
    if (input.type() == 'P') { complete(request, input.consumed() ? OB_SUCCESS : OB_INVALID_ARGUMENT); continue; }
    int result = OB_SUCCESS;
    std::shared_ptr<SessionOwner> owner;
    if (input.type() == 'C' || input.type() == 'I') {
      const uint64_t index = input.number(), generation = input.number();
      {
        std::lock_guard<std::mutex> guard(sessions_mutex);
        if (input.ret || index >= slots.size() || generation != slots[index].generation || !slots[index].owner) {
          result = OB_ERR_SESSION_INTERRUPTED;
        } else if (input.type() == 'C') {
          if (!input.consumed()) { result = OB_INVALID_ARGUMENT; }
          else {
            owner = std::move(slots[index].owner); --active_sessions;
            if (slots[index].generation != UINT64_MAX) { ++slots[index].generation; free_slots.push_back(index); }
            fprintf(stderr, "PROTOTYPE_V11_SESSION_CLOSE ns=%llu slot=%llu generation=%llu active=%llu slots=%zu\n",
                (unsigned long long)worker_namespace, (unsigned long long)index,
                (unsigned long long)generation, (unsigned long long)active_sessions, slots.size());
          }
        } else if (slots[index].owner->running.exchange(true)) { result = OB_EAGAIN; }
        else { owner = slots[index].owner; }
      }
      if (input.type() == 'C') {
        // An admitted query owns a reference; closing/reusing its slot cannot
        // destroy the old session until that query has finished using it.
        owner.reset(); complete(request, result); continue;
      }
    } else if (input.type() != 'A' && input.type() != 'a'
               && input.type() != 'H' && input.type() != 'V'
               && input.type() != 'E') {
      result = OB_NOT_SUPPORTED;
    }
    if (!result && owner) {
      const int64_t saved = input.pos;
      const uint64_t deadline = input.number();
      if (input.ret || !deadline || deadline > INT64_MAX) { result = OB_INVALID_ARGUMENT; }
      else { request->deadline = static_cast<int64_t>(deadline); request->cancellable = true; }
      input.pos = saved;
      if (!result) { result = request->status(); }
    }
    if (result) { complete(request, result, owner.get()); continue; }
    {
      std::lock_guard<std::mutex> guard(jobs_mutex);
      jobs.push_back(Job{std::move(input), request, owner});
    }
    jobs_changed.notify_all();
  }
  prepare_stop_ = true;
  stop_ = true;
  requests.fail();
  worker_storage_routes.fail();
  // The worker is a process lifetime boundary. Stop it before unwinding the
  // SQL/storage adapters that its native network tasks can still reference.
  std::_Exit(ret == OB_SUCCESS ? 0 : 1);

#undef WORKER_STEP
  return ret;
}
} }
