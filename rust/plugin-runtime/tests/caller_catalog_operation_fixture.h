// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real query frame preflight/prepare-failure boundaries, not live SQL effects.
#ifndef SEEKDB_TEST_CALLER_CATALOG_OPERATION_FIXTURE_H_
#define SEEKDB_TEST_CALLER_CATALOG_OPERATION_FIXTURE_H_
#include "sql/engine/expr/caller_catalog_transaction.h"
#include "share/schema/routine_catalog_transaction.h"
#include "share/ob_server_struct.h"
#include "common/mysqlclient/ob_mysql_proxy.h"
#include "data_plane/transaction/ob_i_transaction_service.h"
#include "storage/tx/ob_trans_service.h"
#include "observer/ob_inner_sql_connection.h"
#include "observer/virtual_table/ob_virtual_table_iterator_factory.h"
#include "query/session/ob_inner_sql_connection_access.h"
#include "storage/tablelock/ob_lock_inner_connection_util.h"
#include "sql/ob_sql.h"
#include "catalog_nested_sql_fixture.h"
#include "catalog_preparing_rollback_fixture.h"

namespace caller_catalog_operation_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;

inline void check_lock_connection()
{
  using namespace oceanbase::transaction;
  using namespace oceanbase::transaction::tablelock;
  using oceanbase::observer::ObInnerSQLConnection;
  using oceanbase::query::ObInnerSQLConnectionAccess;
  using Connection = oceanbase::common::sqlclient::ObISQLConnection;
  struct Runtime final : ObIInnerConnectionLockRuntime {
    int calls = 0, result = OB_SUCCESS;
    bool throws = false;
    ObTxDesc *expected = nullptr;
    int lock_obj(const ObLockObjRequest &, Connection *connection) override {
      ++calls;
      auto *native = dynamic_cast<ObInnerSQLConnection *>(connection);
      CHECK(native != nullptr);
      // The same gate as the production Root lock adapter. No storage effects.
      if (!native->is_in_trans()) return OB_ERR_UNEXPECTED;
      CHECK(native->is_extern_session() && native->get_session().get_tx_desc() == expected);
      CHECK(oceanbase::data_plane::tx_desc_id(expected).get_id() == 771);
      CHECK(!oceanbase::data_plane::tx_desc_is_explicit(expected));
      if (throws) throw std::bad_alloc();
      return result;
    }
    int process_lock_rpc(const oceanbase::obcall::ObInnerSQLTransmitArg &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int lock_table(uint64_t, ObTableLockMode, int64_t, Connection *, ObTableLockOwnerID, ObTableLockPriority) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int lock_table(const ObLockTableRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int unlock_table(const ObUnLockTableRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int lock_tablet(uint64_t, ObTabletID, ObTableLockMode, int64_t, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int lock_tablet(uint64_t, const ObIArray<ObTabletID> &, ObTableLockMode, int64_t, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int lock_tablet(const ObLockAloneTabletRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int unlock_tablet(const ObUnLockAloneTabletRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int unlock_obj(const ObUnLockObjRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int lock_obj(const ObLockObjsRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int unlock_obj(const ObUnLockObjsRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int replace_lock(const ObReplaceLockRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int replace_lock(const ObReplaceAllLocksRequest &, Connection *) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int execute_write_sql(Connection *, const ObSqlString &, int64_t &) override { CHECK(false); return OB_NOT_SUPPORTED; }
    int execute_read_sql(Connection *, const ObSqlString &, ObISQLClient::ReadResult &) override { CHECK(false); return OB_NOT_SUPPORTED; }
  } runtime;
  struct RuntimeBinding {
    ObIInnerConnectionLockRuntime *previous = oceanbase::share::server_service<ObIInnerConnectionLockRuntime>();
    explicit RuntimeBinding(ObIInnerConnectionLockRuntime &runtime) {
      oceanbase::share::bind_server_service<ObIInnerConnectionLockRuntime>(&runtime);
    }
    ~RuntimeBinding() { oceanbase::share::bind_server_service<ObIInnerConnectionLockRuntime>(previous); }
  } runtime_binding(runtime);
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
  ObTxDesc tx;
  SessionCatalogTestAccess::idle(tx);
  runtime.expected = &tx;
  struct TransactionBinding {
    ObSQLSessionInfo &session;
    TransactionBinding(ObSQLSessionInfo &session, ObTxDesc &tx) : session(session) { session.get_tx_desc() = &tx; }
    ~TransactionBinding() { session.get_tx_desc() = nullptr; }
  } transaction_binding(session, tx);
  auto management = std::make_unique<oceanbase::rootserver::ObLocalManagementService>();
  ObAddr address;
  oceanbase::observer::ObVTIterCreator vt(*management, address);
  auto engine = std::make_unique<ObSql>(); // Uninitialized: never executes SQL.
  struct BorrowedConnection final : ObInnerSQLConnection {
    int transaction_control_calls = 0;
    int start_transaction(bool) override { ++transaction_control_calls; return OB_NOT_SUPPORTED; }
    int commit() override { ++transaction_control_calls; return OB_NOT_SUPPORTED; }
    int rollback() override { ++transaction_control_calls; return OB_NOT_SUPPORTED; }
  } connection;
  CHECK(connection.init(engine.get(), &vt, &session) == OB_SUCCESS);
  CHECK(connection.is_extern_session() && !connection.is_in_trans());
  ObLockObjRequest request;
  request.op_type_ = IN_TRANS_COMMON_LOCK;
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, nullptr) == OB_INVALID_ARGUMENT);
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_TRANS_INVALID_STATE);
  CHECK(runtime.calls == 0);
  CHECK(session.set_start_stmt() == OB_SUCCESS);
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_TRANS_INVALID_STATE);
  ObSQLSessionInfo::StmtSavedValue saved;
  CHECK(session.begin_nested_session(saved, false) == OB_SUCCESS);
  for (int result : {OB_SUCCESS, OB_TIMEOUT}) {
    runtime.result = result;
    CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == result);
    CHECK(!connection.is_in_trans() && session.get_nested_count() == 1);
    CHECK(session.get_tx_desc() == &tx && !oceanbase::data_plane::tx_desc_is_explicit(&tx));
  }
  runtime.throws = true;
  bool caught = false;
  try { (void)ObInnerSQLConnectionAccess::lock_obj(request, &connection); }
  catch (const std::bad_alloc &) { caught = true; }
  CHECK(caught && !connection.is_in_trans());
  runtime.throws = false; runtime.result = OB_SUCCESS;
  // Preserve a pre-existing marker as well, including on an error.
  connection.set_is_in_trans(true); runtime.result = OB_TIMEOUT;
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_TIMEOUT);
  CHECK(connection.is_in_trans());
  connection.set_is_in_trans(false);
  const int calls = runtime.calls;
  session.set_tx_read_only(true);
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_ERR_READ_ONLY_TRANSACTION);
  session.set_tx_read_only(false);
  request.op_type_ = OUT_TRANS_LOCK;
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_NOT_SUPPORTED);
  request.op_type_ = IN_TRANS_COMMON_LOCK;
  SessionCatalogTestAccess::committed(tx);
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_TRANS_INVALID_STATE);
  SessionCatalogTestAccess::idle(tx, false);
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &connection) == OB_TRANS_INVALID_STATE);
  CHECK(runtime.calls == calls && !connection.is_in_trans());
  CHECK(session.end_nested_session(saved) == OB_SUCCESS);
  CHECK(session.set_end_stmt() == OB_SUCCESS);
  CHECK(connection.destroy() == OB_SUCCESS && !connection.is_in_trans());
  CHECK(connection.transaction_control_calls == 0);
  // An unprepared owned connection retains the legacy rejection.
  ObInnerSQLConnection owned;
  CHECK(ObInnerSQLConnectionAccess::lock_obj(request, &owned) == OB_ERR_UNEXPECTED);
  CHECK(!owned.is_in_trans());
}

inline void run()
{
  catalog_nested_sql_test::run();
  catalog_preparing_rollback_test::run();
  check_lock_connection();
  struct Mutation final : ICallerCatalogMutation {
    int calls = 0;
    int status = OB_ERR_NO_PRIVILEGE;
    bool throws = false;
    oceanbase::transaction::ObTxDesc *terminate_during_preflight = nullptr;
    int preflight(ObExecContext &) override {
      ++calls;
      if (throws) throw std::bad_alloc();
      if (terminate_during_preflight)
        oceanbase::transaction::SessionCatalogTestAccess::committed(*terminate_during_preflight);
      return status;
    }
    int apply(ObExecContext &, ObMySQLTransaction &, RoutineSchemaOverlay &,
        RoutinePrivilegeOverlay &, oceanbase::rootserver::IRoutineCacheInvalidation &) override {
      CHECK(false); return OB_ERR_UNEXPECTED;
    }
  } mutation;
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
  session.set_database_id(100); session.set_priv_user_id(123);
  auto service = std::make_unique<MockSchemaService>();
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  ObSqlCtx sql; sql.session_info_ = &session; sql.schema_guard_ = &guard;
  ObExecContext execution(arena);
  execution.set_my_session(&session); execution.set_sql_ctx(&sql);
  struct Current {
    ObSQLSessionInfo &session;
    explicit Current(ObSQLSessionInfo &session, ObExecContext &execution) : session(session) {
      ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
    }
    ~Current() { ObSQLSessionInfo::ExecCtxSessionRegister registration(session, nullptr); }
  } current(session, execution);
  CatalogOperationResult result;
  // No plan: cannot reach even the action's semantic preflight or data layer.
  CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_NOT_INIT);
  CHECK(mutation.calls == 0 && result.outcome_ == CatalogOperationResult::Outcome::NOT_STARTED);
  uint64_t object_id = 999;
  std::string error;
  CHECK(ExtensionRoutineResolver::mutate(execution, "DROP FUNCTION query_value;", object_id, result, error) == OB_NOT_INIT);
  CHECK(object_id == 0 && result.outcome_ == CatalogOperationResult::Outcome::NOT_STARTED);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObPhysicalPlan physical;
  execution.get_physical_plan_ctx()->set_phy_plan(&physical);
  execution.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
  ObMySQLProxy proxy; // Uninitialized on purpose: this fixture must never do SQL.
  struct Services {
    ObMultiVersionSchemaService *schema = GCTX.schema_service_;
    ObMySQLProxy *sql = GCTX.sql_proxy_;
    Services(ObMultiVersionSchemaService &schema, ObMySQLProxy &sql) {
      GCTX.schema_service_ = &schema; GCTX.sql_proxy_ = &sql;
    }
    ~Services() { GCTX.schema_service_ = schema; GCTX.sql_proxy_ = sql; }
  } services(*service, proxy);
  sql.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
  CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_ERR_NO_PRIVILEGE);
  CHECK(mutation.calls == 0);
  sql.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  execution.get_physical_plan_ctx()->set_timeout_timestamp(1);
  CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_TIMEOUT && mutation.calls == 0);
  execution.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
  CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_ERR_NO_PRIVILEGE);
  CHECK(mutation.calls == 1 && result.outcome_ == CatalogOperationResult::Outcome::NOT_STARTED);
  mutation.throws = true;
  CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_ALLOCATE_MEMORY_FAILED);
  CHECK(mutation.calls == 2 && !session.get_tx_desc() && !session.has_plugin_catalog_transaction());
  mutation.throws = false;
  mutation.status = OB_SUCCESS;
  // No transaction service is bound by this kernel fixture. Exercise the real
  // prepare_plugin_sql failure, close and identity check; no mock data success.
  CHECK(oceanbase::data_plane::query_transaction_service() == nullptr);
  CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_ERR_UNEXPECTED);
  CHECK(mutation.calls == 3 && result.failed_phase_ == 2);
  CHECK(result.outcome_ == CatalogOperationResult::Outcome::ROLLED_BACK);
  CHECK(result.close_error_ == 0 && result.identity_error_ == 0 &&
      result.data_rollback_error_ == 0 && result.view_rollback_error_ == 0);
  CHECK(!session.get_tx_desc() && !session.has_plugin_catalog_transaction() && !guard.has_routine_overlay());
  {
    // Preflight must pin an already prepared IDLE identity too. Semantic
    // admission cannot terminate it and silently let prepare acquire another.
    oceanbase::transaction::ObTxDesc tx;
    struct Binding {
      ObSQLSessionInfo &session;
      Binding(ObSQLSessionInfo &session, oceanbase::transaction::ObTxDesc &tx) : session(session) {
        session.get_tx_desc() = &tx;
      }
      ~Binding() { session.discard_plugin_catalog_transaction(); session.get_tx_desc() = nullptr; }
    } binding(session, tx);
    oceanbase::transaction::SessionCatalogTestAccess::idle(tx);
    mutation.terminate_during_preflight = &tx;
    CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_TRANS_INVALID_STATE);
    CHECK(result.failed_phase_ == 1 && result.outcome_ == CatalogOperationResult::Outcome::NOT_STARTED);
    mutation.terminate_during_preflight = nullptr;
    oceanbase::transaction::SessionCatalogTestAccess::idle(tx);
    // The valid IDLE owner reaches the real preparation dependency, which is
    // deliberately absent here, rather than failing the transaction guard.
    CHECK(run_caller_catalog_operation(execution, mutation, result) == OB_ERR_UNEXPECTED);
    CHECK(result.failed_phase_ == 2 && result.identity_error_ == OB_SUCCESS);
    CHECK(oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
    CHECK(!oceanbase::data_plane::tx_desc_is_active(&tx));
    CHECK(!session.has_plugin_catalog_transaction());
  }
  {
    // Real SQL preparation with only the storage savepoint effect controlled.
    // No transaction is started, no SQL is executed, and no service is inited.
    using oceanbase::transaction::ObTransService;
    using oceanbase::transaction::ObTxDesc;
    using oceanbase::transaction::ObTxParam;
    using oceanbase::transaction::ObTxSEQ;
    using oceanbase::transaction::SessionCatalogTestAccess;
    struct Savepoints final : ObTransService {
      int calls = 0, error = OB_SUCCESS;
      int create_implicit_savepoint(ObTxDesc &tx, const ObTxParam &parameters,
          ObTxSEQ &point, bool release) override {
        ++calls;
        CHECK(parameters.is_valid() && release);
        if (error != OB_SUCCESS) return error;
        point = ObTxSEQ(2, 0);
        tx.add_implicit_savepoint(point);
        CHECK(!oceanbase::data_plane::tx_desc_is_active(&tx));
        return OB_SUCCESS;
      }
    };
    auto savepoints = std::make_unique<Savepoints>();
    ObTxDesc tx;
    struct Binding {
      ObSQLSessionInfo &session;
      ObTransService *saved = oceanbase::share::server_service<ObTransService>();
      Binding(ObSQLSessionInfo &session, ObTxDesc &tx, ObTransService &service) : session(session) {
        session.get_tx_desc() = &tx;
        oceanbase::share::bind_server_service<ObTransService>(&service);
      }
      ~Binding() {
        session.get_tx_desc() = nullptr;
        oceanbase::share::bind_server_service<ObTransService>(saved);
      }
    } binding(session, tx, *savepoints);
    physical.set_stmt_type(stmt::T_SELECT);
    CHECK(physical.is_plain_select() && !session.has_start_stmt());
    for (int scenario = 0; scenario < 3; ++scenario) {
      execution.set_plugin_sql_savepoint(false);
      execution.set_plugin_sql_tx_id(0);
      SessionCatalogTestAccess::idle(tx, false);
      savepoints->calls = 0;
      savepoints->error = scenario == 1 ? OB_TIMEOUT : OB_SUCCESS;
      if (scenario == 2) CHECK(session.set_start_stmt() == OB_SUCCESS);
      CHECK(ObSqlTransControl::prepare_plugin_sql(execution) == savepoints->error);
      CHECK(savepoints->calls == 1);
      if (scenario == 1) {
        CHECK(!execution.has_plugin_sql_savepoint() && !session.has_start_stmt());
      } else {
        CHECK(execution.has_plugin_sql_savepoint() && execution.get_plugin_sql_tx_id() == 771);
        CHECK(session.has_start_stmt() && session.get_nested_count() == 0);
        CHECK(oceanbase::data_plane::tx_desc_is_statement_ready(&tx));
        CHECK(!oceanbase::data_plane::tx_desc_is_explicit(&tx));
        CHECK(ObSqlTransControl::prepare_plugin_sql(execution) == OB_SUCCESS);
        CHECK(savepoints->calls == 1 && session.get_nested_count() == 0);
        ObSQLSessionInfo::StmtSavedValue saved;
        CHECK(session.begin_nested_session(saved, false) == OB_SUCCESS);
        CHECK(session.get_nested_count() == 1);
        CHECK(session.end_nested_session(saved) == OB_SUCCESS);
        CHECK(session.get_nested_count() == 0);
        CHECK(session.set_end_stmt() == OB_SUCCESS && !session.has_start_stmt());
      }
    }
    execution.set_plugin_sql_savepoint(false);
    execution.set_plugin_sql_tx_id(0);
  }
}
}
#endif
