// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
#ifndef SEEKDB_TEST_QUERY_CATALOG_FIXTURE_H_
#define SEEKDB_TEST_QUERY_CATALOG_FIXTURE_H_
#include "routine_overlay_guard_fixture.h"
#include "routine_catalog_savepoint_fixture.h"
#include "routine_catalog_transaction_fixture.h"
#include "borrowed_sql_transaction_fixture.h"
#include "catalog_commit_preparation_fixture.h"
#include "catalog_visibility_fixture.h"
#include "routine_overlay_lifetime_fixture.h"
#include "routine_catalog_writer_fixture.h"
#include "sql/engine/expr/plugin_sql_context.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/ob_sql_context.h"
#include "common/ob_timeout_ctx.h"
#include "lib/worker.h"

namespace query_catalog_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using namespace oceanbase::share::plugin;

inline void run(ObPluginLoader &loader)
{
  routine_catalog_savepoint_test::run();
  routine_overlay_lifetime_test::run();
  routine_catalog_transaction_test::run();
  borrowed_sql_transaction_test::run();
  catalog_commit_preparation_test::run();
  catalog_visibility_test::run();
  routine_catalog_writer_test::run();
  for (bool fail_current : {false, true}) {
    // Populate table bookkeeping through the public restore path, then fail
    // one real snapshot allocation. This is not a data-transaction fixture.
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    ObPhysicalPlan physical;
    ObSQLSessionInfo::StmtSavedValue seed;
    seed.cur_phy_plan_ = &physical;
    for (uint64_t id = 1; id <= 5; ++id)
      CHECK(seed.total_stmt_tables_.push_back(ObBasicSessionInfo::TableStmtType(id, stmt::T_SELECT)) == OB_SUCCESS);
    for (uint64_t id = 4; id <= 6; ++id)
      CHECK(seed.cur_stmt_tables_.push_back(ObBasicSessionInfo::TableStmtType(id, stmt::T_UPDATE)) == OB_SUCCESS);
    CHECK(session.restore_basic_session(seed) == OB_SUCCESS);
    const std::string outer(2048, 'q');
    CHECK(session.store_query_string(ObString(outer.size(), outer.data())) == OB_SUCCESS);
    CHECK(session.set_autocommit(true) == OB_SUCCESS);
    CHECK(session.set_default_database(ObString::make_string("allocation_fixture")) == OB_SUCCESS);
    session.set_query_start_time(321); session.set_stmt_type(stmt::T_SELECT);
    session.get_raw_audit_record().try_cnt_ = 11;
    CHECK(session.set_start_stmt() == OB_SUCCESS);
    struct FailAllocation final : ObIAllocator {
      int calls = 0;
      void *alloc(int64_t) override { ++calls; return nullptr; }
      void *alloc(int64_t size, const ObMemAttr &) override { return alloc(size); }
      void free(void *ptr) override { CHECK(!ptr); }
    } allocation;
    ObSQLSessionInfo::StmtSavedValue failed;
    if (fail_current) failed.cur_stmt_tables_.set_block_allocator(ModulePageAllocator(allocation));
    else failed.total_stmt_tables_.set_block_allocator(ModulePageAllocator(allocation));
    CHECK(session.begin_nested_session(failed, false) == OB_ALLOCATE_MEMORY_FAILED);
    CHECK(allocation.calls == 1);
    CHECK(session.get_nested_count() == 0 && !session.is_inner());
    CHECK(session.get_current_query_string() == ObString(outer.size(), outer.data()));
    CHECK(session.get_query_start_time() == 321 && session.get_stmt_type() == stmt::T_SELECT);
    CHECK(session.get_raw_audit_record().try_cnt_ == 11);
    bool autocommit = false;
    CHECK(session.get_autocommit(autocommit) == OB_SUCCESS && autocommit);
    // A fresh successful snapshot proves the live plan and both source arrays
    // survived; checking only the failed snapshot could hide partial mutation.
    ObSQLSessionInfo::StmtSavedValue probe;
    CHECK(session.save_session(probe) == OB_SUCCESS);
    CHECK(probe.cur_phy_plan_ == &physical && probe.total_stmt_tables_.count() == 5 && probe.cur_stmt_tables_.count() == 3);
    for (int64_t i = 0; i < probe.total_stmt_tables_.count(); ++i)
      CHECK(probe.total_stmt_tables_.at(i) == seed.total_stmt_tables_.at(i) && probe.total_stmt_tables_.at(i).get_stmt_type() == stmt::T_SELECT);
    for (int64_t i = 0; i < probe.cur_stmt_tables_.count(); ++i)
      CHECK(probe.cur_stmt_tables_.at(i) == seed.cur_stmt_tables_.at(i) && probe.cur_stmt_tables_.at(i).get_stmt_type() == stmt::T_UPDATE);
    CHECK(session.restore_session(probe) == OB_SUCCESS);
    CHECK(session.set_end_stmt() == OB_SUCCESS);
    session.reset_cur_phy_plan_to_null();
  }
  {
    // Real session baseline: the participant remains lazy and cannot be
    // attached without an actual active caller transaction. This does not
    // simulate a data transaction or prove commit/callback handoff behavior.
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    auto privileges = std::make_shared<RoutinePrivilegeOverlay>(100, 123);
    auto schema = std::make_shared<RoutineSchemaOverlay>(privileges);
    CHECK(!session.has_plugin_catalog_transaction());
    CHECK(session.record_plugin_catalog_schema_version(oceanbase::transaction::ObTxSEQ(1, 0), 42)
        == OB_TRANS_INVALID_STATE);
    oceanbase::sql::ObExecContext missing_frame(arena);
    oceanbase::sql::CallerCatalogTransaction borrowed;
    CHECK(borrowed.invalidation_sink() == nullptr);
    CHECK(borrowed.open(missing_frame) == OB_STATE_NOT_MATCH);
    CHECK(borrowed.transaction() == nullptr && borrowed.close() == OB_STATE_NOT_MATCH);
    int cleanup_result = OB_ERR_UNEXPECTED;
    CHECK(borrowed.close(cleanup_result) == OB_STATE_NOT_MATCH && cleanup_result == OB_SUCCESS);
    CHECK(borrowed.status() == OB_STATE_NOT_MATCH);
    CHECK(borrowed.open(missing_frame) == OB_INIT_TWICE);
    using oceanbase::sql::CallerCatalogTransaction;
    {
      // Explicit COMMIT has no physical plan. It must fail on missing actual
      // transaction/ownership, not create a transaction or require a fake plan.
      RoutineCatalogTransaction foreign(123);
      CHECK(!session.owns_plugin_catalog_transaction(&foreign, 123, 0));
      CHECK(!session.owns_plugin_catalog_transaction(nullptr, 0, 0));
      CHECK(!oceanbase::data_plane::tx_desc_is_active(session.get_tx_desc()));
      const int64_t deadline = ObTimeUtility::current_time() + 1000000;
      const auto *timeout = &ObTimeoutCtx::get_ctx();
      const auto worker_timeout = THIS_WORKER.get_timeout_ts();
      for (int scenario = 0; scenario < 5; ++scenario) {
        CallerCatalogTransaction commit;
        const auto id = scenario == 1 ? 0 : 123;
        const auto base = scenario == 2 ? -1 : 0;
        const auto expire = scenario == 3 ? 0 : deadline;
        ObExecContext frame(arena); frame.set_my_session(&session);
        std::unique_ptr<ObSQLSessionInfo::ExecCtxSessionRegister> current;
        if (scenario == 4) current = std::make_unique<ObSQLSessionInfo::ExecCtxSessionRegister>(session, &frame);
        const int expected = scenario == 0 || scenario == 4 ? OB_TRANS_INVALID_STATE : OB_INVALID_ARGUMENT;
        CHECK(commit.open_for_commit(session, foreign, id, base, expire) == expected);
        CHECK(commit.transaction() == nullptr && commit.invalidation_sink() == nullptr && commit.close() == expected);
        CHECK(commit.invalidation_sink() == nullptr);
        CHECK(commit.open_for_commit(session, foreign, id, base, expire) == OB_INIT_TWICE);
        CHECK(session.get_nested_count() == -1 && !session.is_inner() && !session.get_tx_desc());
        CHECK(&ObTimeoutCtx::get_ctx() == timeout && THIS_WORKER.get_timeout_ts() == worker_timeout);
      }
    }
    CHECK(CallerCatalogTransaction::validate_statement(session, "SELECT 'a;b'", false) == OB_SUCCESS);
    CHECK(CallerCatalogTransaction::validate_statement(session, "INSERT INTO t VALUES ('a;b')", true) == OB_SUCCESS);
    CHECK(CallerCatalogTransaction::validate_statement(session, "UPDATE t SET c=1", true) == OB_SUCCESS);
    CHECK(CallerCatalogTransaction::validate_statement(session, "DELETE FROM t WHERE c=1", true) == OB_SUCCESS);
    const char *denied[] = {"COMMIT", "ROLLBACK", "START TRANSACTION", "SAVEPOINT x",
      "CREATE TABLE t(c INT)", "DROP TABLE t", "CALL f()", "SET autocommit=1", "TRUNCATE TABLE t",
      "INSERT INTO t VALUES (1); COMMIT", "SELECT 1; SELECT 2"};
    for (const auto *text : denied) {
      CHECK(CallerCatalogTransaction::validate_statement(session, text, true) == OB_NOT_SUPPORTED);
      CHECK(CallerCatalogTransaction::validate_statement(session, text, false) == OB_NOT_SUPPORTED);
    }
    CHECK(CallerCatalogTransaction::validate_statement(session, "SELECT 1", true) == OB_NOT_SUPPORTED);
    CHECK(CallerCatalogTransaction::validate_statement(session, "INSERT INTO t VALUES (1)", false) == OB_NOT_SUPPORTED);
    CHECK(CallerCatalogTransaction::validate_statement(session, "INSERT INTO t VALUES ('unfinished", true) != OB_SUCCESS);
    CHECK(CallerCatalogTransaction::validate_statement(session, nullptr, true) == OB_INVALID_ARGUMENT);
    {
      ObExecContext no_transaction(arena); no_transaction.set_my_session(&session);
      CHECK(no_transaction.create_physical_plan_ctx() == OB_SUCCESS);
      ObPhysicalPlan physical; no_transaction.get_physical_plan_ctx()->set_phy_plan(&physical);
      ObSQLSessionInfo::ExecCtxSessionRegister current(session, &no_transaction);
      CallerCatalogTransaction client;
      CHECK(client.open(no_transaction) == OB_TRANS_INVALID_STATE && client.transaction() == nullptr);
      CHECK(!session.get_tx_desc()); // Opening a borrowed transport never starts one.
    }
    {
      const std::string outer(2048, 'x');
      CHECK(session.store_query_string(ObString(outer.size(), outer.data())) == OB_SUCCESS);
      CHECK(session.set_autocommit(true) == OB_SUCCESS);
      session.set_query_start_time(321); session.set_stmt_type(stmt::T_SELECT);
      CHECK(session.set_default_database(ObString::make_string("outer_database")) == OB_SUCCESS);
      ObSQLSessionInfo::StmtSavedValue saved;
      CHECK(session.begin_nested_session(saved, false) == OB_ERR_UNEXPECTED);
      CHECK(session.get_current_query_string() == ObString(outer.size(), outer.data()));
      CHECK(!session.is_inner() && session.get_nested_count() == -1);
      CHECK(session.set_start_stmt() == OB_SUCCESS);
      CHECK(session.begin_nested_session(saved, false) == OB_SUCCESS);
      CHECK(session.is_inner() && session.get_nested_count() == 1);
      CHECK(saved.cur_query_len_ == outer.size() && session.get_current_query_string().empty());
      bool autocommit = true; CHECK(session.get_autocommit(autocommit) == OB_SUCCESS && !autocommit);
      CHECK(session.store_query_string(ObString::make_string("inner")) == OB_SUCCESS);
      CHECK(session.set_default_database(ObString::make_string("inner_database")) == OB_SUCCESS);
      session.set_query_start_time(456); session.set_stmt_type(stmt::T_INSERT);
      CHECK(session.end_nested_session(saved) == OB_SUCCESS);
      CHECK(session.get_current_query_string() == ObString(outer.size(), outer.data()));
      CHECK(session.get_query_start_time() == 321 && session.get_stmt_type() == stmt::T_SELECT);
      CHECK(session.get_autocommit(autocommit) == OB_SUCCESS && autocommit);
      CHECK(session.get_database_name() == ObString::make_string("outer_database"));
      CHECK(!session.is_inner() && session.get_nested_count() == 0);
      CHECK(session.set_end_stmt() == OB_SUCCESS);
    }
    CHECK(session.record_plugin_catalog_view(oceanbase::transaction::ObTxSEQ(1, 0), schema, privileges)
        == OB_TRANS_INVALID_STATE);
    CHECK(!session.has_plugin_catalog_transaction());
    CHECK(session.prepare_plugin_catalog_commit() == OB_SUCCESS);
    CHECK(session.get_last_ddl_schema_version() == 0);
    session.set_last_ddl_schema_version(601);
    session.set_last_ddl_schema_version(600);
    session.set_last_ddl_schema_version(0);
    CHECK(session.get_last_ddl_schema_version() == 601);
    session.set_last_ddl_schema_version(602);
    CHECK(session.get_last_ddl_schema_version() == 602);
    CHECK(session.rollback_plugin_catalog_view(123, oceanbase::transaction::ObTxSEQ(1, 0)) == OB_SUCCESS);
    CHECK(session.complete_plugin_catalog_transaction(123, OB_TIMEOUT, false) == OB_SUCCESS);
    session.reset_tx_variable(false);
    CHECK(!session.has_plugin_catalog_transaction());
    CHECK(session.get_last_ddl_schema_version() == 602); // Data tx reset must retain the read fence.
  }
  const char *type = "core.type.bytes";
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_routine_id", &type, 1, binding) == OB_SUCCESS);
  // Only schema storage is controlled. Visibility, schema-view dispatch,
  // execution-context admission, C ABI, loader and Rust handler are real.
  for (int scenario = 0; scenario < 14; ++scenario) {
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    CHECK(session.set_user(ObString::make_string("fixture"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session.set_priv_user_id(123); session.set_user_priv_set(scenario == 4 ? 0 : OB_PRIV_CREATE_ROUTINE);
    session.set_db_priv_set(0); session.set_database_id(OB_SYS_DATABASE_ID);
    CHECK(session.set_default_database(ObString::make_string(OB_SYS_DATABASE_NAME)) == OB_SUCCESS);
    auto service = std::make_unique<MockSchemaService>();
    auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
    CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
    ObSimpleServerRuntimeSchema runtime;
    runtime.set_schema_version(42); runtime.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
    runtime.set_status(SERVER_RUNTIME_STATUS_NORMAL);
    CHECK(runtime.set_runtime_name(ObString::make_string("fixture")) == OB_SUCCESS);
    CHECK(manager->add_runtime_schema(runtime) == OB_SUCCESS);
    ObUserInfo user; user.set_user_id(123); user.set_schema_version(42);
    CHECK(user.set_user_name("fixture") == OB_SUCCESS && user.set_host("localhost") == OB_SUCCESS);
    ObSimpleUserSchema simple_user; simple_user.set_user_id(123); simple_user.set_schema_version(42);
    CHECK(simple_user.set_user_name("fixture") == OB_SUCCESS && simple_user.set_host("localhost") == OB_SUCCESS);
    CHECK(manager->add_user(simple_user) == OB_SUCCESS);
    ObDatabaseSchema database; database.set_database_id(OB_SYS_DATABASE_ID); database.set_schema_version(42);
    CHECK(database.set_database_name(OB_SYS_DATABASE_NAME) == OB_SUCCESS);
    ObSchemaGetterGuard view;
    CHECK(MockSchemaService::bind(view, *service, *manager) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(view, user) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_database(view, database) == OB_SUCCESS);
    ObRoutineInfo routine; routine.set_database_id(OB_SYS_DATABASE_ID); routine.set_routine_id(7777011);
    routine.set_schema_version(42); routine.set_owner_id(123); routine.set_package_id(OB_INVALID_ID);
    routine.set_overload(0); routine.set_routine_type(scenario == 2 ? ROUTINE_PROCEDURE_TYPE : ROUTINE_FUNCTION_TYPE);
    CHECK(routine.set_routine_name(ObString::make_string("catalog_query")) == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, OB_SYS_DATABASE_ID, "catalog_query", 7777011, routine.get_routine_type(), 42) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_routine(view, routine) == OB_SUCCESS);
    auto overlay = std::make_shared<RoutineSchemaOverlay>();
    if (scenario == 10 || scenario == 11) {
      CHECK(overlay->erase(OB_SYS_DATABASE_ID, routine.get_routine_name(), ROUTINE_FUNCTION_TYPE, 7777011) == OB_SUCCESS);
      if (scenario == 10) { routine.set_routine_id(7777022); CHECK(overlay->stage(routine) == OB_SUCCESS); }
      CHECK(view.attach_routine_overlay(overlay) == OB_SUCCESS);
    }
    ObSqlCtx sql_context; sql_context.session_info_ = &session; sql_context.schema_guard_ = &view;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(scenario == 8 ? nullptr : &sql_context);
    CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObPhysicalPlan physical;
    execution.get_physical_plan_ctx()->set_phy_plan(&physical);
    execution.get_physical_plan_ctx()->set_timeout_timestamp(scenario == 7 ? 1 : 0);
    ObSQLSessionInfo::ExecCtxSessionRegister register_execution(session, scenario == 9 ? nullptr : &execution);
    struct Sink {
      bool emitted = false, is_null = false; int64_t id = 0;
      static seekdb_plugin_status_t emit(seekdb_plugin_host_handle_t *opaque, const seekdb_plugin_execution_result_v1_t *value) {
        auto &sink = *reinterpret_cast<Sink *>(opaque); sink.emitted = true; sink.is_null = value->is_null;
        if (!sink.is_null) { CHECK(value->data_size == sizeof(int64_t)); std::memcpy(&sink.id, value->data, sizeof(sink.id)); }
        return SEEKDB_PLUGIN_STATUS_OK;
      }
    } sink;
    PluginSqlContext query(execution);
    seekdb_plugin_execution_context_v2_t context{}; context.v1.struct_size = sizeof(context);
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink); context.v1.emit_result = Sink::emit;
    query.attach(context);
    const auto &api = *reinterpret_cast<const seekdb_plugin_sql_api_v3_t *>(context.sql_api);
    const std::string name = scenario == 1 ? "CATALOG_QUERY" : scenario == 3 ? "absent" : scenario == 6 ? std::string(1, char(0xff)) : "catalog_query";
    seekdb_plugin_routine_lookup_result_v1_t output{}; output.struct_size = sizeof(output);
    if (scenario == 12) {
      output.object_id = 99;
      std::thread wrong([&]() { CHECK(api.lookup_routine(context.sql_context, 1, name.data(), name.size(), &output) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION); });
      wrong.join(); CHECK(query.error() == OB_SUCCESS && output.object_id == 99);
    }
    class Reenter final : public ObIExtraStatusCheck {
    public:
      const seekdb_plugin_sql_api_v3_t &api_; seekdb_plugin_sql_context_handle_t *context_;
      Reenter(const seekdb_plugin_sql_api_v3_t &api, seekdb_plugin_sql_context_handle_t *context) : api_(api), context_(context) {}
      const char *name() const override { return "catalog-query-reentry"; }
      int check() const override {
        seekdb_plugin_routine_lookup_result_v1_t result{}; result.struct_size = sizeof(result);
        CHECK(api_.lookup_routine(context_, 1, "catalog_query", 13, &result) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
        CHECK(result.database_error == OB_STATE_NOT_MATCH && result.object_id == 0);
        return OB_SUCCESS; // Ignoring the nested failure must not clear it.
      }
    } reentry(api, context.sql_context);
    seekdb_plugin_status_t status;
    if (scenario == 13) {
      ObIExtraStatusCheck::Guard extra(execution, reentry);
      status = api.lookup_routine(context.sql_context, 1, name.data(), name.size(), &output);
    } else {
      status = api.lookup_routine(context.sql_context, scenario == 5 ? 99 : scenario == 2 ? 2 : 1, name.data(), name.size(), &output);
    }
    const bool succeeds = scenario < 4 || scenario == 10 || scenario == 11 || scenario == 12;
    const uint64_t expected_id = scenario == 3 || scenario == 11 ? 0 : scenario == 10 ? 7777022 : 7777011;
    CHECK((status == SEEKDB_PLUGIN_STATUS_OK) == succeeds);
    CHECK((query.error() == OB_SUCCESS) == succeeds && output.database_error == query.error());
    if (scenario == 4) CHECK(query.error() == OB_ERR_NO_PRIVILEGE);
    if (scenario == 5) CHECK(query.error() == OB_INVALID_ARGUMENT);
    if (scenario == 7) CHECK(query.error() == OB_TIMEOUT);
    if (scenario == 8 || scenario == 9 || scenario == 13) CHECK(query.error() == OB_STATE_NOT_MATCH);
    CHECK(output.object_id == (succeeds ? expected_id : 0));
    if (!succeeds) {
      const int error = query.error();
      seekdb_plugin_query_status_v1_t polled{}; polled.struct_size = sizeof(polled);
      CHECK(api.v2.poll_query(context.sql_context, &polled) != SEEKDB_PLUGIN_STATUS_OK && polled.database_error == error);
      seekdb_plugin_sql_result_v1_t result{}; result.struct_size = sizeof(result);
      CHECK(api.v2.v1.execute(context.sql_context, nullptr, 0, nullptr, 0, 0, nullptr, nullptr, &result) != SEEKDB_PLUGIN_STATUS_OK);
      CHECK(result.database_error == error);
    }
    if (scenario == 0 || scenario == 1 || scenario == 3 || scenario == 4 || scenario == 7 || scenario == 10 || scenario == 11) {
      // Same real host view through the actual Rust DSO, not a substitute SQL transport.
      PluginSqlContext native_query(execution); native_query.attach(context);
      seekdb_plugin_execution_value_v1_t argument{}; argument.struct_size = sizeof(argument); argument.type_id = type;
      argument.data = reinterpret_cast<const uint8_t *>(name.data()); argument.data_size = name.size();
      const int code = loader.execute_bound_function(binding, &context.v1, &argument, 1);
      CHECK((code == OB_SUCCESS) == succeeds && sink.emitted == succeeds);
      if (succeeds) CHECK(sink.is_null == (expected_id == 0) && (sink.is_null || sink.id == expected_id));
      CHECK((native_query.error() == OB_SUCCESS) == succeeds);
      ObPluginStatusSnapshot status; CHECK(loader.get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 0);
    }
    // Metadata-only lookup never starts a data transaction or SELECT savepoint.
    CHECK(!execution.has_plugin_sql_savepoint() && !session.is_in_transaction());
    ObSQLSessionInfo::ExecCtxSessionRegister unregister_execution(session, nullptr);
  }
}
}
#endif
