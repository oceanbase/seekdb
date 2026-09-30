// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Concrete resolver -> reservation -> writer -> private view. SQL rows, schema
// allocation, principal and DDL admission are controlled; not a live database.
#ifndef SEEKDB_TEST_CALLER_ROUTINE_MUTATION_FIXTURE_H_
#define SEEKDB_TEST_CALLER_ROUTINE_MUTATION_FIXTURE_H_
#include "routine_overlay_lifetime_fixture.h"
#include "sql/ob_sql.h"
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "borrowed_sql_transaction_fixture.h"
#include "rootserver/pl_ddl/routine_cache_invalidation.h"

namespace test {
// Use ObSql's existing test friendship to supply the real PL collaborator.
// No resolver/writer replacement and no fake pointer to a server service. This
// does not initialize a complete server or enable data SQL execution.
class TestOptimizerUtils {
public:
  static void bind_catalog_fixture_pl(oceanbase::sql::ObSql &runtime, oceanbase::pl::ObPL &engine) {
    runtime.pl_engine_ = &engine;
  }
};
}

namespace caller_routine_mutation_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxSEQ;
inline void run()
{
  for (int scenario = 0; scenario < 15; ++scenario) {
    const bool create = scenario >= 10;
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    CHECK(session.set_user(ObString::make_string("fixture"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session.set_priv_user_id(123);
    session.set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE | OB_PRIV_ALTER_ROUTINE);
    session.set_database_id(OB_SYS_DATABASE_ID);
    CHECK(session.set_default_database(ObString::make_string(OB_SYS_DATABASE_NAME)) == OB_SUCCESS);
    struct Service final : MockSchemaService {
      ObSchemaMgr *manager = nullptr;
      const ObUserInfo *user = nullptr;
      const ObDatabaseSchema *database = nullptr;
      const ObSysVariableSchema *variables = nullptr;
      ~Service() override { schema_service_ = nullptr; }
      void bind_sql(ObSchemaService &sql) { schema_service_ = &sql; }
      int get_runtime_refreshed_schema_version(int64_t &version, bool = false) const override
      { version = 42; return OB_SUCCESS; }
      int get_runtime_schema_guard(ObSchemaGetterGuard &guard, int64_t = OB_INVALID_VERSION,
                                  RefreshSchemaMode = NORMAL) override {
        CHECK(manager && user && database && variables);
        int ret = MockSchemaService::bind(guard, *this, *manager);
        if (ret == OB_SUCCESS) ret = cache_user(guard, *user);
        if (ret == OB_SUCCESS) ret = cache_database(guard, *database);
        if (ret == OB_SUCCESS) ret = cache_variables(guard, *variables);
        return ret;
      }
    };
    auto service = std::make_unique<Service>();
    ObMySQLProxy proxy;
    routine_version_test::Allocator allocator(proxy, *service);
    service->bind_sql(allocator);
    struct GlobalServices {
      ObMultiVersionSchemaService *schema = GCTX.schema_service_;
      ObMySQLProxy *sql = GCTX.sql_proxy_;
      GlobalServices(ObMultiVersionSchemaService &schema, ObMySQLProxy &sql) {
        GCTX.schema_service_ = &schema; GCTX.sql_proxy_ = &sql;
      }
      ~GlobalServices() { GCTX.schema_service_ = schema; GCTX.sql_proxy_ = sql; }
    } global(*service, proxy);
    auto manager = std::make_unique<ObSchemaMgr>();
    CHECK(manager->init() == OB_SUCCESS);
    CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
    ObSimpleServerRuntimeSchema runtime_schema;
    runtime_schema.set_schema_version(42);
    runtime_schema.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
    runtime_schema.set_status(SERVER_RUNTIME_STATUS_NORMAL);
    CHECK(runtime_schema.set_runtime_name(ObString::make_string("caller_fixture")) == OB_SUCCESS);
    CHECK(manager->add_runtime_schema(runtime_schema) == OB_SUCCESS);
    ObUserInfo user;
    user.set_user_id(123); user.set_schema_version(42);
    CHECK(user.set_user_name("fixture") == OB_SUCCESS && user.set_host("localhost") == OB_SUCCESS);
    ObSimpleUserSchema simple_user;
    simple_user.set_user_id(123); simple_user.set_schema_version(42);
    CHECK(simple_user.set_user_name(user.get_user_name_str()) == OB_SUCCESS);
    CHECK(simple_user.set_host(user.get_host_name_str()) == OB_SUCCESS);
    CHECK(manager->add_user(simple_user) == OB_SUCCESS);
    ObDatabaseSchema database;
    database.set_database_id(OB_SYS_DATABASE_ID); database.set_schema_version(42);
    CHECK(database.set_database_name(OB_SYS_DATABASE_NAME) == OB_SUCCESS);
    ObSimpleDatabaseSchema simple_database;
    simple_database.set_database_id(OB_SYS_DATABASE_ID); simple_database.set_schema_version(42);
    CHECK(simple_database.set_database_name(OB_SYS_DATABASE_NAME) == OB_SUCCESS);
    CHECK(manager->add_database(simple_database) == OB_SUCCESS);
    ObSysVariableSchema variables;
    variables.set_schema_version(42);
    CHECK(variables.load_default_system_variable() == OB_SUCCESS);
    int64_t variable_index = OB_INVALID_INDEX;
    CHECK(oceanbase::share::ObSysVarMeta::calc_sys_var_store_idx(
        oceanbase::share::SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable_index) == OB_SUCCESS);
    CHECK(variables.get_sysvar_schema(variable_index)->set_value(ObString::make_string(scenario == 11 ? "1" : "0")) == OB_SUCCESS);
    service->manager = manager.get(); service->user = &user;
    service->database = &database; service->variables = &variables;
    ObSchemaGetterGuard guard;
    CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, user) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_variables(guard, variables) == OB_SUCCESS);
    auto privileges = std::make_shared<RoutinePrivilegeOverlay>();
    auto schema = std::make_shared<RoutineSchemaOverlay>(privileges);
    CHECK(guard.attach_routine_overlay(schema) == OB_SUCCESS);
    ObRoutineInfo original;
    original.set_database_id(OB_SYS_DATABASE_ID); original.set_owner_id(123);
    original.set_routine_id(311234); original.set_schema_version(42);
    original.set_package_id(OB_INVALID_ID); original.set_overload(0); original.set_subprogram_id(0);
    original.set_routine_type(ROUTINE_FUNCTION_TYPE);
    CHECK(original.set_routine_name(ObString::make_string("query_value")) == OB_SUCCESS);
    CHECK(original.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
    if (scenario != 8) CHECK(schema->stage(original) == OB_SUCCESS);
    ObSqlCtx sql; sql.session_info_ = &session; sql.schema_guard_ = &guard;
    auto engine = std::make_unique<oceanbase::pl::ObPL>();
    ObSql runtime;
    test::TestOptimizerUtils::bind_catalog_fixture_pl(runtime, *engine);
    ObExecContext execution(arena);
    execution.set_my_session(&session); execution.set_sql_ctx(&sql); execution.set_pl_sql_runtime(&runtime);
    struct Current {
      ObSQLSessionInfo &session;
      Current(ObSQLSessionInfo &session, ObExecContext &execution) : session(session) {
        ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
      }
      ~Current() { ObSQLSessionInfo::ExecCtxSessionRegister registration(session, nullptr); }
    } current(session, execution);
    ExtensionVersionRows rows;
    rows.rows.clear(); rows.active = true; rows.write_status = OB_SUCCESS;
    if (scenario == 3 || scenario == 12) rows.fail_write_at = 1;
    if (scenario == 4) rows.on_read = [](ExtensionVersionRows &r) {
      r.rows.clear();
      if (r.sql.find("__all_extension_member") != std::string::npos) r.rows.push_back({});
    };
    borrowed_sql_transaction_test::Guard identity;
    RoutineCatalogTransaction journal(9981);
    CHECK(journal.admit_ddl(9981, ObTxSEQ(10, 0), 7) == OB_SUCCESS);
    CHECK(journal.record(9981, ObTxSEQ(10, 0), schema, privileges) == OB_SUCCESS);
    struct Recorder final : ICatalogOperationRecorder {
      RoutineCatalogTransaction &journal;
      ObTxSEQ barrier{10, 0};
      explicit Recorder(RoutineCatalogTransaction &journal) : journal(journal) {}
      int check_schema_operation() const override { return journal.check_ddl_write(9981, barrier); }
      int finish_schema_operation(int64_t version, int result) override {
        return result == OB_SUCCESS ? journal.record_schema_version(9981, barrier, version) : result;
      }
    } recorder(journal);
    BorrowedSQLTransaction transaction(rows, identity, &recorder);
    struct Invalidation final : oceanbase::rootserver::IRoutineCacheInvalidation {
      int calls = 0;
      int on_drop(uint64_t id, uint64_t db) override {
        CHECK(id == 311234 && db == OB_SYS_DATABASE_ID); ++calls; return OB_SUCCESS;
      }
    } invalidation;
    const bool alter = scenario == 1 || scenario == 9;
    std::string error;
    CallerRoutineMutation mutation(scenario == 14 ? "CREATE FUNCTION query_created(v INT) RETURNS INT RETURN missing_callee(v);" :
        create ? "CREATE FUNCTION query_created(v INT) RETURNS INT RETURN v + 7;" :
        alter ? "ALTER FUNCTION query_value COMMENT 'changed';" :
        scenario == 8 ? "DROP FUNCTION IF EXISTS query_value;" : "DROP FUNCTION query_value;", error);
    CHECK(mutation.object_id() == 0);
    const int preflight = mutation.preflight(execution);
    if (scenario == 14) {
      // Strict body resolution must not turn an unresolved callee into a
      // deferred runtime SIGNAL and then write an incomplete dependency set.
      CHECK(preflight != OB_SUCCESS && preflight != OB_NOT_INIT);
      CHECK(rows.reads == 0 && rows.writes == 0 && allocator.ids_ == 0 && allocator.calls_ == 0);
      CHECK(mutation.object_id() == 0 && mutation.preflight(execution) == OB_INIT_TWICE);
      CHECK(mutation.apply(execution, transaction, *schema, *privileges, invalidation) == OB_STATE_NOT_MATCH);
      continue;
    }
    if (preflight != OB_SUCCESS) std::cerr << "caller routine preflight=" << preflight << " " << error << std::endl;
    CHECK(preflight == OB_SUCCESS);
    CHECK(mutation.preflight(execution) == OB_INIT_TWICE);
    CHECK(rows.reads == 0 && rows.writes == 0 && allocator.calls_ == 0);
    CHECK(GCTX.schema_service_ == service.get() && service->get_schema_service() == &allocator);
    CHECK(execution.get_sql_proxy() == &proxy);
    const ObDatabaseSchema *checked_database = nullptr;
    CHECK(guard.get_database_schema(ObString::make_string(OB_SYS_DATABASE_NAME), checked_database) == OB_SUCCESS);
    CHECK(checked_database == &database);
    ObSessionPrivInfo checked_privileges;
    CHECK(session.get_session_priv_info(checked_privileges) == OB_SUCCESS);
    CHECK(checked_privileges.user_priv_set_ & OB_PRIV_ALTER_ROUTINE);
    if (scenario == 2) session.set_user_priv_set(0);
    if (scenario == 5) session.set_database_id(100);
    if (scenario == 6) CHECK(journal.fail(9981, OB_TIMEOUT) == OB_SUCCESS);
    if (scenario == 7) sql.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
    if (scenario == 9) {
      ObRoutineInfo changed;
      CHECK(changed.assign(original) == OB_SUCCESS);
      changed.set_schema_version(43);
      CHECK(schema->stage(changed) == OB_SUCCESS);
    }
    if (scenario == 13) {
      ObRoutineInfo conflict;
      CHECK(conflict.assign(original) == OB_SUCCESS);
      conflict.set_routine_id(311235);
      CHECK(conflict.set_routine_name(ObString::make_string("query_created")) == OB_SUCCESS);
      CHECK(schema->stage(conflict) == OB_SUCCESS);
    }
    ObArenaAllocator creator_request;
    int ret = OB_SUCCESS;
    if (scenario == 11) {
      routine_overlay_lifetime_test::RequestScope request(creator_request);
      ret = mutation.apply(execution, transaction, *schema, *privileges, invalidation);
    } else {
      ret = mutation.apply(execution, transaction, *schema, *privileges, invalidation);
    }
    // The following dependent PL resolution runs after the creator request's
    // schema allocation arena has been released, as on a real client session.
    creator_request.reset();
    const int expected[] = {OB_SUCCESS, OB_SUCCESS, OB_ERR_NO_ROUTINE_PRIVILEGE, OB_TIMEOUT, OB_OP_NOT_ALLOW,
                           OB_STATE_NOT_MATCH, OB_TIMEOUT, OB_ERR_NO_PRIVILEGE, OB_SUCCESS, OB_ERR_PARALLEL_DDL_CONFLICT,
                           OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT, OB_ERR_SP_ALREADY_EXISTS};
    if (ret != expected[scenario]) std::cerr << "caller routine scenario=" << scenario << " ret=" << ret
        << " " << ob_strerror(ret) << " reads=" << rows.reads << " writes=" << rows.writes
        << " versions=" << allocator.calls_ << " " << error << std::endl;
    CHECK(ret == expected[scenario]);
    for (const auto &query : rows.written) catalog_sql_namespace_test::check(query);
    for (const auto &query : rows.queries) catalog_sql_namespace_test::check(query);
    CHECK(rows.starts == 0 && rows.ends == 0);
    CHECK(mutation.apply(execution, transaction, *schema, *privileges, invalidation) == OB_STATE_NOT_MATCH);
    CHECK(mutation.object_id() == (ret == OB_SUCCESS && scenario != 8 ? (create ? 400001 : 311234) : 0));
    if (create) {
      const ObRoutineInfo *created = nullptr;
      CHECK(guard.get_standalone_function_info(OB_SYS_DATABASE_ID, ObString::make_string("query_created"), created) == OB_SUCCESS);
      if (scenario == 12) CHECK(created == nullptr);
      else if (scenario == 13) CHECK(created && created->get_routine_id() == 311235);
      else {
        CHECK(created && created->get_routine_id() == mutation.object_id() && created->get_schema_version() == 1001);
        CHECK(created->get_owner_id() == 123 && created->get_routine_params().count() == 2);
        for (int64_t i = 0; i < created->get_routine_params().count(); ++i) {
          const auto *parameter = created->get_routine_params().at(i);
          CHECK(parameter && parameter->get_routine_id() == created->get_routine_id());
          CHECK(parameter->get_schema_version() == created->get_schema_version());
        }
        bool handled = false; ObPrivSet grants = 0;
        CHECK(privileges->lookup(OB_SYS_DATABASE_ID, created->get_routine_name(), ROUTINE_FUNCTION_TYPE,
            123, true, created, handled, grants) == OB_SUCCESS && handled);
        CHECK(grants == (scenario == 11 ? OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE : 0));
        // Automatic grants perform an additional normal schema operation; its
        // version must participate in commit preparation, not only the routine.
        CHECK(allocator.ids_ == 1 && allocator.calls_ == (scenario == 11 ? 2 : 1) && rows.writes > 0);
        uint64_t first_version = 0, first_operations = 0;
        CHECK(journal.schema_state(9981, first_version, first_operations) == OB_SUCCESS);
        CHECK(first_version == static_cast<uint64_t>(scenario == 11 ? 1002 : 1001) && first_operations > 0);
        CHECK(journal.record(9981, ObTxSEQ(20, 0), schema, privileges) == OB_SUCCESS);
        recorder.barrier = ObTxSEQ(20, 0);
        // The next normal PL resolver must see the new routine and produce a
        // dependency on its reserved identity; no publication or new guard.
        CallerRoutineMutation dependent("CREATE FUNCTION query_dependent(v INT) RETURNS INT RETURN query_created(v);", error);
        const int resolved = dependent.preflight(execution);
        if (resolved != OB_SUCCESS) std::cerr << "dependent preflight=" << resolved << " " << error << std::endl;
        CHECK(resolved == OB_SUCCESS);
        const int written = dependent.apply(execution, transaction, *schema, *privileges, invalidation);
        if (written != OB_SUCCESS) std::cerr << "dependent apply=" << written << " " << error << std::endl;
        CHECK(written == OB_SUCCESS && dependent.object_id() == 400002);
        for (const auto &query : rows.written) catalog_sql_namespace_test::check(query);
        for (const auto &query : rows.queries) catalog_sql_namespace_test::check(query);
        bool dependency_written = false;
        for (const auto &query : rows.written) if (query.find("__all_dependency") != std::string::npos &&
            query.find("400001") != std::string::npos && query.find("400002") != std::string::npos)
          dependency_written = true;
        CHECK(dependency_written && rows.starts == 0 && rows.ends == 0 && invalidation.calls == 0);
        // Model a confirmed DATA rollback to the second operation's barrier.
        // Only the real Rust journal/view undo is under test: the recording SQL
        // transport does not implement data rollback or transaction isolation.
        CHECK(journal.rollback(9981, ObTxSEQ(20, 0)) == OB_SUCCESS);
        const ObRoutineInfo *remaining = nullptr;
        CHECK(guard.get_standalone_function_info(OB_SYS_DATABASE_ID,
            ObString::make_string("query_dependent"), remaining) == OB_SUCCESS && !remaining);
        CHECK(guard.get_standalone_function_info(OB_SYS_DATABASE_ID,
            ObString::make_string("query_created"), remaining) == OB_SUCCESS && remaining);
        CHECK(remaining->get_routine_id() == 400001);
        uint64_t version = 0, operations = 0;
        CHECK(journal.schema_state(9981, version, operations) == OB_SUCCESS);
        CHECK(version == first_version && operations == first_operations);
        CHECK(privileges->lookup(OB_SYS_DATABASE_ID, remaining->get_routine_name(), ROUTINE_FUNCTION_TYPE,
            123, true, remaining, handled, grants) == OB_SUCCESS && handled);
        CHECK(grants == (scenario == 11 ? OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE : 0));
        CHECK(journal.rollback(9981, ObTxSEQ(10, 0)) == OB_SUCCESS);
        CHECK(guard.get_standalone_function_info(OB_SYS_DATABASE_ID,
            ObString::make_string("query_created"), remaining) == OB_SUCCESS && !remaining);
        CHECK(journal.schema_state(9981, version, operations) == OB_SUCCESS && version == 0 && operations == 0);
      }
    }
    const ObRoutineInfo *found = nullptr;
    CHECK(guard.get_standalone_function_info(OB_SYS_DATABASE_ID, original.get_routine_name(), found) == OB_SUCCESS);
    if (scenario == 0 || scenario == 8) CHECK(found == nullptr);
    else if (scenario == 1) {
      CHECK(found && found->get_comment() == "changed" && found->get_schema_version() > 42);
      CHECK(found->get_routine_id() == original.get_routine_id() && invalidation.calls == 0);
      bool dependencies_read = false;
      for (const auto &query : rows.queries) if (query.find(oceanbase::share::OB_ALL_DEPENDENCY_TNAME) != std::string::npos)
        dependencies_read = true;
      CHECK(dependencies_read);
    } else CHECK(found && found->get_comment().empty());
    if (scenario == 0) {
      CHECK(invalidation.calls == 1);
      bool handled = false; ObPrivSet grants = OB_PRIV_EXECUTE;
      CHECK(privileges->lookup(OB_SYS_DATABASE_ID, original.get_routine_name(), ROUTINE_FUNCTION_TYPE,
          123, true, nullptr, handled, grants) == OB_SUCCESS && handled && grants == 0);
    }
  }
}
}
#endif
