// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real writer/operator/schema/privilege SQL and Rust operation recording. Rows,
// version allocation and invalidation sink are controlled, not a live DB test.
#ifndef SEEKDB_TEST_ROUTINE_CATALOG_WRITER_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_CATALOG_WRITER_FIXTURE_H_
#include "rootserver/pl_ddl/routine_catalog_writer.h"
#include "borrowed_sql_transaction_fixture.h"
#include "catalog_sql_namespace_fixture.h"

namespace routine_catalog_writer_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;
using oceanbase::transaction::ObTxSEQ;

inline void run()
{
  catalog_sql_namespace_test::run();
  for (int scenario = 0; scenario < 19; ++scenario) {
    auto manager = std::make_unique<ObSchemaMgr>();
    CHECK(manager->init() == OB_SUCCESS);
    CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
    ObSimpleServerRuntimeSchema runtime;
    runtime.set_schema_version(42);
    runtime.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
    runtime.set_status(SERVER_RUNTIME_STATUS_NORMAL);
    CHECK(runtime.set_runtime_name(ObString::make_string("writer_fixture")) == OB_SUCCESS);
    CHECK(manager->add_runtime_schema(runtime) == OB_SUCCESS);
    ObUserInfo user;
    user.set_user_id(123); user.set_schema_version(42);
    CHECK(user.set_user_name("writer_user") == OB_SUCCESS && user.set_host("localhost") == OB_SUCCESS);
    ObSimpleUserSchema simple_user;
    simple_user.set_user_id(123); simple_user.set_schema_version(42);
    CHECK(simple_user.set_user_name(user.get_user_name_str()) == OB_SUCCESS);
    CHECK(simple_user.set_host(user.get_host_name_str()) == OB_SUCCESS);
    CHECK(manager->add_user(simple_user) == OB_SUCCESS);
    ObDatabaseSchema database;
    database.set_database_id(100); database.set_schema_version(42);
    CHECK(database.set_database_name("writer_db") == OB_SUCCESS);
    ObSysVariableSchema variables;
    variables.set_schema_version(42);
    CHECK(variables.load_default_system_variable() == OB_SUCCESS);
    int64_t variable_index = OB_INVALID_INDEX;
    CHECK(oceanbase::share::ObSysVarMeta::calc_sys_var_store_idx(
        oceanbase::share::SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable_index) == OB_SUCCESS);
    ObSysVarSchema *automatic = variables.get_sysvar_schema(variable_index);
    CHECK(automatic);
    const bool auto_grants = scenario == 1 || scenario == 3 || scenario == 16;
    CHECK(automatic->set_value(ObString::make_string(auto_grants ? "1" : "0")) == OB_SUCCESS);
    struct Service final : MockSchemaService {
      ObSchemaMgr &manager;
      ObUserInfo &user;
      ObDatabaseSchema &database;
      ObSysVariableSchema &variables;
      int guard_error = OB_SUCCESS;
      Service(ObSchemaMgr &manager, ObUserInfo &user, ObDatabaseSchema &database, ObSysVariableSchema &variables)
          : manager(manager), user(user), database(database), variables(variables) {}
      ~Service() override { schema_service_ = nullptr; }
      void bind_sql(ObSchemaService &sql) { schema_service_ = &sql; }
      int get_runtime_refreshed_schema_version(int64_t &version, bool = false) const override
      { version = 42; return OB_SUCCESS; }
      int get_runtime_schema_guard(ObSchemaGetterGuard &guard, int64_t = OB_INVALID_VERSION,
                                  RefreshSchemaMode = NORMAL) override {
        if (guard_error != OB_SUCCESS) return guard_error;
        int ret = MockSchemaService::bind(guard, *this, manager);
        if (ret == OB_SUCCESS) ret = cache_user(guard, user);
        if (ret == OB_SUCCESS) ret = cache_database(guard, database);
        if (ret == OB_SUCCESS) ret = cache_variables(guard, variables);
        return ret;
      }
    };
    auto service = std::make_unique<Service>(*manager, user, database, variables);
    ObMySQLProxy proxy;
    routine_version_test::Allocator allocator(proxy, *service);
    if (scenario != 7) service->bind_sql(allocator);
    ObSchemaGetterGuard view;
    if (scenario != 6) CHECK(service->get_runtime_schema_guard(view) == OB_SUCCESS);
    ExtensionVersionRows rows;
    rows.rows.clear(); rows.active = true; rows.write_status = OB_SUCCESS;
    bool stale_acl = scenario >= 17;
    if (stale_acl) {
      rows.routine_name_column = true;
      rows.on_read = [&](ExtensionVersionRows &r) {
        r.rows.clear();
        if (stale_acl && (r.sql.find("SELECT all_priv FROM oceanbase.__all_routine_privilege") == 0 ||
                         r.sql.find("SELECT routine_name FROM oceanbase.__all_routine_privilege") == 0)) {
          ExtensionVersionRows::Row row;
          row.id = 7; // EXECUTE + ALTER ROUTINE + GRANT, not an object ID in this query.
          row.dependency = "WRITER_VALUE"; // Actual stored spelling must be used for deletion.
          r.rows.push_back(row);
        }
      };
      rows.affected_rows = [&](const std::string &sql) {
        if (sql.find("DELETE FROM oceanbase.__all_routine_privilege ") == 0) {
          CHECK(stale_acl);
          CHECK(sql.find("5752495445525F56414C5545") != std::string::npos ||
                sql.find("5752495445525f56414c5545") != std::string::npos);
          stale_acl = false;
        }
        return int64_t{1};
      };
    }
    if (scenario == 4) rows.fail_write_at = 1;
    if (scenario == 13) rows.read_status = OB_TIMEOUT;
    if (scenario == 15) rows.on_read = [](ExtensionVersionRows &r) {
      if (r.sql.find("__all_routine_privilege") != std::string::npos) r.read_status = OB_TIMEOUT;
    };
    if (scenario == 16) service->guard_error = OB_TIMEOUT;
    borrowed_sql_transaction_test::Guard identity_guard;
    RoutineCatalogTransaction journal(9981);
    CHECK(journal.admit_ddl(9981, ObTxSEQ(10, 0), 7) == OB_SUCCESS);
    struct Recorder final : ICatalogOperationRecorder {
      RoutineCatalogTransaction &journal;
      explicit Recorder(RoutineCatalogTransaction &journal) : journal(journal) {}
      int check_schema_operation() const override { return journal.check_ddl_write(9981, ObTxSEQ(10, 0)); }
      int finish_schema_operation(int64_t version, int result) override {
        return result != OB_SUCCESS ? result : journal.record_schema_version(9981, ObTxSEQ(10, 0), version);
      }
    } recorder(journal);
    BorrowedSQLTransaction transaction(rows, identity_guard, &recorder);
    ObRoutineInfo routine;
    routine.set_database_id(100); routine.set_owner_id(123); routine.set_routine_id(311234);
    routine.set_schema_version(42); routine.set_package_id(OB_INVALID_ID); routine.set_overload(0);
    routine.set_subprogram_id(0);
    routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
    CHECK(routine.set_routine_name(ObString::make_string("writer_value")) == OB_SUCCESS);
    CHECK(routine.set_routine_body(ObString::make_string("RETURN 1")) == OB_SUCCESS);
    CHECK(routine.is_valid());
    ObRoutineInfo old;
    CHECK(old.assign(routine) == OB_SUCCESS);
    const bool drop = (scenario >= 11 && scenario <= 13) || scenario == 18;
    const bool replace = scenario == 14;
    const bool reserve = (scenario >= 2 && scenario <= 4) || scenario == 8 || scenario == 9 ||
                         drop || replace || scenario == 15 || scenario == 17;
    RoutineIdReservation id;
    RoutineVersionReservation version;
    if (reserve) {
      if (drop) CHECK(RoutineVersionReservation::reserve_drop(*service, transaction, routine, version) == OB_SUCCESS);
      else {
        if (!replace) {
          CHECK(RoutineIdReservation::reserve(allocator, routine, id) == OB_SUCCESS);
          routine.set_routine_id(id.id());
        }
        CHECK(RoutineVersionReservation::reserve(*service, transaction, routine, replace ? &old : nullptr, version) == OB_SUCCESS);
        routine.set_schema_version(version.version());
      }
    }
    if (scenario == 5) identity_guard.error = OB_TRANS_INVALID_STATE;
    struct Invalidation final : IRoutineCacheInvalidation {
      RoutineCatalogTransaction &journal;
      explicit Invalidation(RoutineCatalogTransaction &journal) : journal(journal) {}
      int calls = 0, result = OB_SUCCESS;
      int on_drop(uint64_t id, uint64_t db) override {
        CHECK(id == 311234 && db == 100); ++calls;
        return result != OB_SUCCESS ? result : journal.record_invalidation(9981, ObTxSEQ(10, 0), db, id);
      }
    } invalidation(journal);
    if (scenario == 12) invalidation.result = OB_TIMEOUT;
    ObErrorInfo errors;
    ObSEArray<ObDependencyInfo, 1> dependencies;
    RoutineCatalogWriter writer(*service, proxy, view, transaction, scenario >= 2 && scenario != 8);
    int ret = drop ? writer.drop(routine, errors, nullptr, invalidation, &version)
        : scenario == 10 ? writer.alter(routine, errors, nullptr)
        : writer.create(routine, replace || scenario == 9 ? &old : nullptr, errors, dependencies,
                        nullptr, reserve && !replace ? &id : nullptr, reserve ? &version : nullptr);
    const int expected[] = {OB_SUCCESS, OB_SUCCESS, OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT,
      OB_STATE_NOT_MATCH, OB_NOT_INIT, OB_ERR_UNEXPECTED, OB_INVALID_ARGUMENT, OB_INVALID_ARGUMENT,
      OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT, OB_TIMEOUT, OB_SUCCESS, OB_TIMEOUT, OB_TIMEOUT,
      OB_SUCCESS, OB_SUCCESS};
    if (ret != expected[scenario]) {
      std::cerr << "routine writer scenario=" << scenario << " ret=" << ret
                << " reads=" << rows.reads << " writes=" << rows.writes
                << " versions=" << allocator.calls_ << std::endl;
    }
    CHECK(ret == expected[scenario]);
    CHECK(rows.starts == 0 && rows.ends == 0); // Even failure never takes data ownership.
    CHECK(invalidation.calls == (scenario == 11 || scenario == 12 || scenario == 18 ? 1 : 0));
    uint64_t pending = 99;
    CHECK(journal.invalidation_count(9981, pending) == OB_SUCCESS);
    CHECK(pending == (scenario == 11 || scenario == 18 ? 1 : 0));
    if (scenario >= 17) CHECK(!stale_acl);
    if (scenario >= 5 && scenario <= 9) CHECK(rows.reads == 0 && rows.writes == 0);
    const int calls = rows.reads + rows.writes;
    CHECK(writer.alter(routine, errors, nullptr) == OB_INIT_TWICE);
    CHECK(rows.reads + rows.writes == calls);
    for (const auto &sql : rows.written) {
      catalog_sql_namespace_test::check(sql);
      CHECK(sql.find("alter system") == std::string::npos);
      CHECK(sql.find("normal_schema_version") == std::string::npos);
    }
    for (const auto &sql : rows.queries) catalog_sql_namespace_test::check(sql);
    if (ret == OB_SUCCESS && scenario != 10) {
      uint64_t last = 0, operations = 0;
      CHECK(journal.schema_state(9981, last, operations) == OB_SUCCESS && last > 42 && operations > 0);
      bool grant_written = false;
      for (const auto &sql : rows.written) grant_written |= sql.find("__all_routine_privilege") != std::string::npos;
      CHECK(grant_written == (auto_grants || scenario >= 17));
    }
    if (scenario == 11) {
      // Controlled writer effects, followed by data-rollback notification. No
      // cache operation is invoked; the corresponding private request vanishes.
      CHECK(journal.rollback(9981, ObTxSEQ(10, 0)) == OB_SUCCESS);
      CHECK(journal.invalidation_count(9981, pending) == OB_SUCCESS && pending == 0);
    } else if (scenario == 18) {
      RoutineInvalidationQueue delivery(1, 1);
      CHECK(delivery.valid());
      uint64_t last = 0, operations = 0, ticket = 99, db = 99, id = 99;
      CHECK(journal.peek_invalidation(9981, ticket, db, id) == OB_STATE_NOT_MATCH);
      CHECK(ticket == 0 && db == 0 && id == 0);
      CHECK(journal.begin_prepare(9981, last, operations) == OB_SUCCESS);
      CHECK(delivery.reserve(journal, 9981) == OB_SUCCESS);
      CHECK(journal.record_end_sign(9981, last + 1) == OB_SUCCESS);
      CHECK(journal.complete_prepare(9981, last + 1, OB_SUCCESS) == OB_SUCCESS);
      CHECK(journal.finish(9981, true) == OB_SUCCESS); // Fixture notification, not real durable commit.
      CHECK(journal.peek_invalidation(9981, ticket, db, id) == OB_SUCCESS);
      CHECK(ticket == 0 && db == 0 && id == 0); // Ownership moved before session can discard journal.
      CHECK(journal.invalidation_count(9981, pending) == OB_SUCCESS && pending == 0);
      struct Evictor final : IRoutineCacheEvictor {
        int calls = 0;
        int check_schema_version(int64_t version) override { CHECK(version > 42); return OB_SUCCESS; }
        int evict(uint64_t db, uint64_t id) override {
          CHECK(db == 100 && id == 311234); ++calls; return OB_SUCCESS;
        }
      } evictor;
      uint32_t processed = 0;
      CHECK(delivery.process(evictor, 1, processed) == OB_SUCCESS && processed == 1 && evictor.calls == 1);
    }
  }
}
}
#endif
