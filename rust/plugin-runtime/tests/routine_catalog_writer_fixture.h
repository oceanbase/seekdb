// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real writer/operator/schema/privilege SQL and Rust operation recording. Rows,
// version allocation and invalidation sink are controlled, not a live DB test.
#ifndef SEEKDB_TEST_ROUTINE_CATALOG_WRITER_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_CATALOG_WRITER_FIXTURE_H_
#include "rootserver/pl_ddl/routine_catalog_writer.h"
#include "rootserver/pl_ddl/native_routine_acl_version_reservation.h"
#include "borrowed_sql_transaction_fixture.h"
#include "catalog_sql_namespace_fixture.h"
#include "plugin_expression_fixture.h"

namespace routine_catalog_writer_test {
using namespace oceanbase::common;
using namespace oceanbase::share::schema;
using namespace oceanbase::rootserver;
using oceanbase::transaction::ObTxSEQ;

inline void run()
{
  catalog_sql_namespace_test::run();
  for (bool overloaded : {false, true}) for (int scenario = 0; scenario < 68; ++scenario) {
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
    const bool auto_grants = scenario == 1 || scenario == 3 || scenario == 16 || (scenario >= 48 && scenario < 57) ||
        (scenario >= 58 && scenario != 61);
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
    bool stale_acl = scenario == 17 || scenario == 18;
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
    if (scenario >= 19) {
      ObRoutineParam result;
      result.set_routine_id(311234); result.set_schema_version(42);
      result.set_sequence(0); result.set_subprogram_id(0); result.set_param_position(0); result.set_param_level(0);
      result.set_param_type(ObDoubleType);
      CHECK(routine.add_routine_param(result) == OB_SUCCESS);
    }
    ObRoutineInfo old;
    if (overloaded && scenario >= 19) routine.set_overload(7);
    CHECK(old.assign(routine) == OB_SUCCESS);
    const bool native = scenario >= 19;
    if (native && scenario != 25)
      CHECK(routine.set_native_binding(ObString::make_string("org.seekdb.gis"),
          ObString::make_string(scenario == 24 || scenario == 27 || scenario == 28 ?
              "org.seekdb.gis.replacement" : "org.seekdb.gis.function.st_area"), 1) == OB_SUCCESS);
    if ((scenario >= 23 && scenario <= 28 && scenario != 26) || scenario == 34 || scenario == 35)
      CHECK(old.set_native_binding(ObString::make_string("org.seekdb.gis"),
          ObString::make_string("org.seekdb.gis.function.st_area"), 1) == OB_SUCCESS);
    const bool drop = (scenario >= 11 && scenario <= 13) || scenario == 18 || scenario == 21 || scenario == 22 ||
        (scenario >= 38 && scenario < 48);
    const bool replace = scenario == 14 || (scenario >= 23 && scenario <= 28) || scenario == 34 || scenario == 35;
    if (scenario == 34) routine.get_routine_params().at(0)->set_param_type(ObIntType);
    if (scenario == 35) routine.set_deterministic();
    if (scenario == 30 || scenario == 31) rows.on_read = [scenario](ExtensionVersionRows &r) {
      if (r.sql.find("SELECT native_module_id") == 0 &&
          (scenario == 30 || r.sql.find("__all_routine_history") != std::string::npos)) r.read_status = OB_ERR_BAD_FIELD_ERROR;
    };
    const bool reserve = scenario != 25 && scenario != 26 && ((scenario >= 2 && scenario <= 4) || scenario == 8 || scenario == 9 ||
                         drop || replace || scenario == 15 || scenario == 17 || native);
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
    NativeRoutineAclVersionReservation owner_grant;
    if (scenario >= 58 && scenario != 62) {
      ObRoutineInfo target;
      CHECK(target.assign(routine) == OB_SUCCESS);
      if (scenario == 60) target.set_overload(target.get_overload() + 1);
      CHECK(NativeRoutineAclVersionReservation::reserve_create_owner(*service, transaction,
          target, owner_grant) == OB_SUCCESS);
      CHECK(owner_grant.count() == 1 && owner_grant.version_at(0) > routine.get_schema_version());
    }
    const int64_t owner_grant_version = owner_grant.version_at(0);
    if (scenario == 59) {
      // A following script operation allocates a version before CREATE runs.
      // The automatic owner grant must retain its earlier reserved version.
      int64_t later = 0;
      CHECK(service->gen_new_schema_version(later) == OB_SUCCESS && later > owner_grant_version);
    }
    const int reserved_allocations = allocator.calls_;
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
    plugin_expression_test::Provider provider;
    std::vector<std::string> native_calls;
    int native_resolves = 0;
    provider.native_resolution_ = [&](const char *module, const char *implementation,
                                     const char *const *arguments, uint32_t count, seekdb_plugin_sql_binding_v1_t *binding) {
      ++native_resolves;
      CHECK(native && !arguments && count == 0 && binding);
      CHECK(std::string(module) == "org.seekdb.gis");
      CHECK(std::string(implementation) == routine.get_native_implementation_id().ptr());
      *binding = {};
      binding->struct_size = sizeof(*binding); binding->kind = SEEKDB_PLUGIN_EXTENSION_FUNCTION;
      binding->owner_generation = scenario == 32 ? 0 : 7;
      binding->catalog_epoch = 11;
      std::strcpy(binding->owner_plugin_id, module);
      std::strcpy(binding->object_id, implementation);
      std::strcpy(binding->result_type_id, scenario == 33 ? "core.type.int64" : "core.type.float64");
      return scenario == 37 ? OB_ENTRY_NOT_EXIST : OB_SUCCESS;
    };
    provider.native_dependency_ = [&](ObISQLClient &client, const ObString &module,
                                     const ObString &implementation, uint64_t routine_id, bool add, uint64_t expected_generation) {
      CHECK(native && &client == &transaction && transaction.is_started());
      CHECK(expected_generation == (add ? 7 : 0));
      CHECK(module == ObString::make_string("org.seekdb.gis"));
      CHECK(routine_id == routine.get_routine_id());
      native_calls.emplace_back((add ? "+" : "-") + std::string(implementation.ptr(), implementation.length()));
      if (scenario == 36) return OB_STATE_NOT_MATCH;
      return ((add && (scenario == 20 || scenario == 27)) ||
              (!add && (scenario == 22 || scenario == 28))) ? OB_TIMEOUT : OB_SUCCESS;
    };
    if (scenario == 29) oceanbase::share::g_mp = nullptr;
    if (scenario == 19) {
      const int reads = rows.reads, writes = rows.writes;
      ObRoutineInfo other_owner; CHECK(other_owner.assign(routine) == OB_SUCCESS); other_owner.set_owner_id(124);
      RoutineCatalogWriter rejected_owner(*service, proxy, view, transaction, true);
      CHECK(rejected_owner.create(other_owner, &routine, errors, dependencies, nullptr) == OB_NOT_SUPPORTED);
      CHECK(rows.reads == reads && rows.writes == writes && native_calls.empty() && native_resolves == 0);
      std::cout << "PASS: native owner transfer refused before catalog/module mutation" << std::endl;
    }
    if (native && drop) {
      rows.on_read = [&](ExtensionVersionRows &transport) {
        transport.rows.clear();
        if (transport.sql.find("SELECT database_id,owner_id,overload,schema_version,routine_type,native_abi_version") == 0) {
          ExtensionVersionRows::Row row;
          row.integers = {{0, 100}, {1, 123}, {2, routine.get_overload()}, {3, scenario == 38 ? 41 : 42},
              {4, ROUTINE_FUNCTION_TYPE}, {5, 1}};
          row.strings = {{6, "writer_value"}, {7, "org.seekdb.gis"}, {8, "org.seekdb.gis.function.st_area"}};
          transport.rows.push_back(row);
        } else if (transport.sql.find("SELECT grantee_id,grantor_id,col_id,priv_id,priv_option") == 0) {
          CHECK(transport.sql.find("WHERE obj_id=311234 AND objtype=" + std::to_string(uint64_t(ObObjectType::FUNCTION))) != std::string::npos);
          const auto add = [&](int grantee, int grantor, int right, int option) {
            ExtensionVersionRows::Row row;
            row.integers = {{0, grantee}, {1, grantor}, {2, OBJ_LEVEL_FOR_TAB_PRIV}, {3, right}, {4, option}};
            transport.rows.push_back(row);
          };
          // Deliberately absent from the committed guard's ACL cache.
          add(123, 124, OBJ_PRIV_ID_ALTER, 0);
          add(123, 124, OBJ_PRIV_ID_EXECUTE, scenario == 47 ? 2 : 1);
          add(125, 126, OBJ_PRIV_ID_EXECUTE, 0);
          if (scenario == 39) transport.read_status = OB_TIMEOUT;
          if (scenario == 40) transport.close_status = OB_TIMEOUT;
          if (scenario == 46) transport.rows.push_back(transport.rows.back());
        }
      };
      if (scenario == 41) allocator.fail_at_ = allocator.calls_ + 1;
      if (scenario == 44) allocator.fail_at_ = allocator.calls_ + 2;
      if (scenario == 42) rows.affected_rows = [](const std::string &sql) {
        return int64_t(sql.find("DELETE FROM oceanbase.__all_objauth ") == 0 ? 0 : 1);
      };
      if (scenario == 43) rows.fail_write_at = rows.writes + 2;
    }
    size_t automatic_acl_start = size_t(-1);
    if (native && !drop && !replace) {
      const auto previous = rows.on_read;
      rows.on_read = [&, previous](ExtensionVersionRows &transport) {
        if (previous) previous(transport);
        if (transport.sql.find("SELECT database_id,owner_id,overload,schema_version,routine_type,native_abi_version") == 0) {
          transport.rows.clear();
          ExtensionVersionRows::Row row;
          row.integers = {{0, 100}, {1, 123}, {2, routine.get_overload()}, {3, routine.get_schema_version()},
              {4, ROUTINE_FUNCTION_TYPE}, {5, 1}};
          row.strings = {{6, "writer_value"}, {7, "org.seekdb.gis"}, {8, "org.seekdb.gis.function.st_area"}};
          transport.rows.push_back(row);
        } else if (transport.sql.find("SELECT grantee_id,grantor_id,col_id,priv_id,priv_option") == 0) {
          transport.rows.clear();
          if (scenario == 49 || scenario == 57) {
            ExtensionVersionRows::Row row;
            row.integers = {{0, 125}, {1, 126}, {2, OBJ_LEVEL_FOR_TAB_PRIV}, {3, OBJ_PRIV_ID_EXECUTE}, {4, 0}};
            transport.rows.push_back(row);
          }
          if (scenario == 51) transport.close_status = OB_TIMEOUT;
        } else if (transport.sql.find("SELECT priv_id,priv_option") == 0) {
          transport.rows.clear();
          automatic_acl_start = rows.written.size();
          CHECK(transport.sql.find("grantor_id=123 AND grantee_id=123 ") != std::string::npos);
          if (scenario >= 52 && scenario <= 56) rows.fail_write_at = rows.writes + scenario - 51;
          if (scenario >= 63) rows.fail_write_at = rows.writes + scenario - 62;
        }
      };
      if (scenario == 50) allocator.fail_at_ = allocator.calls_ + 1;
    }
    const int64_t reserved_version = version.version(); // take_drop consumes the reservation.
    RoutineCatalogWriter writer(*service, proxy, view, transaction, scenario >= 2 && scenario != 8);
    int ret = drop ? writer.drop(routine, errors, nullptr, invalidation, &version)
        : scenario == 10 ? writer.alter(routine, errors, nullptr)
        : writer.create(routine, replace || scenario == 9 ? &old : nullptr, errors, dependencies,
                        nullptr, reserve && !replace ? &id : nullptr, reserve ? &version : nullptr,
                        scenario >= 58 ? &owner_grant : nullptr);
    const int expected[] = {OB_SUCCESS, OB_SUCCESS, OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT,
      OB_STATE_NOT_MATCH, OB_NOT_INIT, OB_ERR_UNEXPECTED, OB_INVALID_ARGUMENT, OB_INVALID_ARGUMENT,
      OB_SUCCESS, OB_SUCCESS, OB_TIMEOUT, OB_TIMEOUT, OB_SUCCESS, OB_TIMEOUT, OB_TIMEOUT,
      OB_SUCCESS, OB_SUCCESS,
      OB_SUCCESS, OB_TIMEOUT, OB_SUCCESS, OB_TIMEOUT, OB_SUCCESS, OB_SUCCESS,
      OB_NOT_SUPPORTED, OB_NOT_SUPPORTED, OB_TIMEOUT, OB_TIMEOUT, OB_NOT_INIT,
      OB_ERR_BAD_FIELD_ERROR, OB_ERR_BAD_FIELD_ERROR, OB_STATE_NOT_MATCH, OB_INVALID_ARGUMENT,
      OB_INVALID_ARGUMENT, OB_INVALID_ARGUMENT, OB_STATE_NOT_MATCH, OB_ENTRY_NOT_EXIST,
      OB_STATE_NOT_MATCH, OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT, OB_SEARCH_NOT_FOUND,
      OB_TIMEOUT, OB_TIMEOUT, OB_SUCCESS, OB_INVALID_DATA, OB_INVALID_DATA,
      OB_SUCCESS, OB_STATE_NOT_MATCH, OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT,
      OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT, OB_STATE_NOT_MATCH,
      OB_SUCCESS, OB_SUCCESS, OB_STATE_NOT_MATCH, OB_STATE_NOT_MATCH, OB_STATE_NOT_MATCH,
      OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT, OB_TIMEOUT};
    if (ret != expected[scenario]) {
      std::cerr << "routine writer scenario=" << scenario << " ret=" << ret
                << " reads=" << rows.reads << " writes=" << rows.writes
                << " versions=" << allocator.calls_ << std::endl;
    }
    CHECK(ret == expected[scenario]);
    if (scenario >= 58) {
      CHECK(owner_grant.count() == 0 && allocator.calls_ == reserved_allocations);
      if (scenario <= 59) {
        uint64_t high = 0, operations = 0;
        CHECK(journal.schema_state(9981, high, operations) == OB_SUCCESS && high == uint64_t(owner_grant_version));
      } else if (scenario <= 62) CHECK(rows.writes == 0);
      else CHECK(automatic_acl_start != size_t(-1) && rows.written.size() - automatic_acl_start == size_t(scenario - 62));
    }
    if (scenario >= 52 && scenario <= 56)
      CHECK(automatic_acl_start != size_t(-1) && rows.written.size() - automatic_acl_start == size_t(scenario - 51));
    CHECK(rows.starts == 0 && rows.ends == 0); // Even failure never takes data ownership.
    CHECK(invalidation.calls == (scenario == 11 || scenario == 12 || scenario == 18 ||
                                scenario == 21 || scenario == 22 || scenario == 45 ? 1 : 0));
    uint64_t pending = 99;
    CHECK(journal.invalidation_count(9981, pending) == OB_SUCCESS);
    CHECK(pending == (scenario == 11 || scenario == 18 || scenario == 21 || scenario == 22 || scenario == 45 ? 1 : 0));
    if (scenario == 17 || scenario == 18) CHECK(!stale_acl);
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
      CHECK(grant_written == ((!native && auto_grants) || scenario == 17 || scenario == 18));
      if (native && !drop && !replace) {
        int current = 0, history = 0;
        const auto normalize = [](std::string sql) {
          sql.erase(std::remove_if(sql.begin(), sql.end(),
              [](char c) { return c == ' ' || c == '\n' || c == '\t' || c == '`'; }), sql.end());
          return sql;
        };
        const std::string identity = "VALUES(" + std::to_string(routine.get_routine_id()) + "," +
            std::to_string(uint64_t(ObObjectType::FUNCTION)) + "," + std::to_string(OBJ_LEVEL_FOR_TAB_PRIV) + ",123,123,";
        for (const auto &sql : rows.written) {
          if (sql.find("REPLACE INTO oceanbase.__all_objauth ") == 0) {
            ++current;
            const auto compact = normalize(sql);
            CHECK(compact.find(identity + std::to_string(OBJ_PRIV_ID_EXECUTE) + ",0,") != std::string::npos ||
                compact.find(identity + std::to_string(OBJ_PRIV_ID_ALTER) + ",0,") != std::string::npos);
          }
          if (sql.find("INSERT INTO oceanbase.__all_objauth_history ") == 0) {
            ++history;
            const auto compact = normalize(sql);
            CHECK(compact.find(identity) != std::string::npos &&
                compact.find("," + std::to_string(last) + ",0)") != std::string::npos);
          }
        }
        CHECK(current == (auto_grants ? 2 : 0) && history == current);
        if (auto_grants) CHECK(last > uint64_t(routine.get_schema_version()));
      }
    }
    if (native) {
      const std::string area = "org.seekdb.gis.function.st_area";
      const std::string replacement = "org.seekdb.gis.replacement";
      const std::vector<std::string> expected_calls = scenario == 23 || scenario == 25 || scenario == 26 || scenario == 29 ||
          (scenario >= 30 && scenario <= 35) || scenario == 37 || (scenario >= 38 && scenario < 48 && scenario != 45) ||
          (scenario >= 60 && scenario <= 62) ? std::vector<std::string>{} :
          scenario == 24 || scenario == 28 ? std::vector<std::string>{"+" + replacement, "-" + area} :
          scenario == 27 ? std::vector<std::string>{"+" + replacement} :
          std::vector<std::string>{(drop || scenario == 25 ? "-" : "+") + area};
      CHECK(native_calls == expected_calls);
      CHECK(native_resolves == (drop || scenario == 23 || scenario == 25 || scenario == 26 || scenario == 29 ||
                               scenario == 30 || scenario == 31 ? 0 : 1));
      CHECK(rows.active);
      if ((scenario >= 29 && scenario <= 35) || scenario == 37) CHECK(rows.writes == 0);
      if (drop) {
        int deletes = 0, histories = 0;
        int64_t last_acl = -1, routine_delete = -1;
        for (size_t i = 0; i < rows.written.size(); ++i) {
          const auto &sql = rows.written[i];
          if (sql.find("DELETE FROM oceanbase.__all_objauth ") == 0) {
            ++deletes; last_acl = i;
            auto compact = sql;
            compact.erase(std::remove_if(compact.begin(), compact.end(),
                [](char c) { return c == ' ' || c == '`'; }), compact.end());
            CHECK(compact.find("obj_id=311234ANDobjtype=" + std::to_string(uint64_t(ObObjectType::FUNCTION)) + "AND") != std::string::npos);
          }
          if (sql.find("INSERT INTO oceanbase.__all_objauth_history ") == 0) { ++histories; last_acl = i; }
          if (sql.find("DELETE FROM oceanbase.__all_routine ") == 0) routine_delete = i;
        }
        if (scenario == 21 || scenario == 22 || scenario == 45) {
          CHECK(deletes == 3 && histories == 3 && routine_delete > last_acl);
          uint64_t high = 0, operations = 0;
          CHECK(journal.schema_state(9981, high, operations) == OB_SUCCESS && high > uint64_t(reserved_version));
        } else if (scenario >= 38) CHECK(routine_delete == -1);
      }
      // The outer caller's rollback notification discards any pending schema
      // state/invalidation after a module edge error. No hidden commit occurred.
      if (ret != OB_SUCCESS) {
        CHECK(journal.rollback(9981, ObTxSEQ(10, 0)) == OB_SUCCESS);
        CHECK(journal.invalidation_count(9981, pending) == OB_SUCCESS && pending == 0);
      }
    } else CHECK(native_calls.empty());
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
