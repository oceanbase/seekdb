// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real SHOW iterator/printer/session journal; committed schema and transaction
// descriptor are controlled inputs, not a live virtual-table scan or commit.
#ifndef SEEKDB_TEST_SHOW_ROUTINE_CATALOG_FIXTURE_H_
#define SEEKDB_TEST_SHOW_ROUTINE_CATALOG_FIXTURE_H_
#include "session_catalog_view_fixture.h"
#include "observer/virtual_table/ob_show_create_procedure.h"

namespace show_routine_catalog_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using oceanbase::transaction::ObTxDesc;
using oceanbase::transaction::ObTxSEQ;
using oceanbase::transaction::SessionCatalogTestAccess;

class Iterator final : public oceanbase::observer::ObShowCreateProcedure {
public:
  explicit Iterator(ObIAllocator &allocator) {
    set_allocator(&allocator);
    void *storage = allocator.alloc(sizeof(ObObj));
    CHECK(storage);
    cur_row_.cells_ = new (storage) ObObj();
    cur_row_.count_ = 1;
    CHECK(output_column_ids_.push_back(OB_APP_MIN_COLUMN_ID + 2) == OB_SUCCESS);
  }
};

inline void run()
{
  for (auto type : {ROUTINE_FUNCTION_TYPE, ROUTINE_PROCEDURE_TYPE}) {
    ObArenaAllocator arena;
    auto service = std::make_unique<MockSchemaService>();
    auto manager = std::make_unique<ObSchemaMgr>();
    CHECK(manager->init() == OB_SUCCESS);
    ObSQLSessionInfo writer, observer, denied;
    for (auto *session : {&writer, &observer, &denied}) {
      CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
      CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
      CHECK(session->set_user(ObString::make_string(session == &denied ? "reader" : "fixture"),
          ObString::make_string("localhost"), session == &denied ? 124 : 123) == OB_SUCCESS);
      session->set_priv_user_id(session == &denied ? 124 : 123);
      session->set_user_priv_set(0);
      session->set_database_id(100);
      CHECK(session->set_default_database(ObString::make_string("fixture_db")) == OB_SUCCESS);
    }
    ObDatabaseSchema database;
    database.set_database_id(100); database.set_schema_version(42);
    CHECK(database.set_database_name("fixture_db") == OB_SUCCESS);
    ObUserInfo reader;
    reader.set_user_id(124); reader.set_schema_version(42); reader.set_priv_set(0);
    CHECK(reader.set_user_name("reader") == OB_SUCCESS && reader.set_host("localhost") == OB_SUCCESS);
    // The guard-local full schema below supplies role lookup by user ID; this
    // fixture does not initialize the server runtime or its global user index.
    ObRoutineInfo committed;
    committed.set_database_id(100); committed.set_routine_id(9001); committed.set_owner_id(123);
    committed.set_schema_version(42); committed.set_package_id(OB_INVALID_ID);
    committed.set_overload(0); committed.set_subprogram_id(0); committed.set_routine_type(type);
    CHECK(committed.set_routine_name("cycle_value") == OB_SUCCESS);
    CHECK(committed.set_priv_user("fixture@localhost") == OB_SUCCESS);
    CHECK(committed.set_routine_body(type == ROUTINE_FUNCTION_TYPE ? "RETURN 7" : "BEGIN SELECT 7; END") == OB_SUCCESS);
    CHECK(committed.set_comment("committed property") == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, 100, "cycle_value", 9001, type, 42) == OB_SUCCESS);
    char environment[OB_MAX_PROC_ENV_LENGTH]; int64_t pos = 0;
    CHECK(ObExecEnv::gen_exec_env(writer, environment, sizeof(environment), pos) == OB_SUCCESS);
    CHECK(committed.set_exec_env(ObString(pos, environment)) == OB_SUCCESS);
    if (type == ROUTINE_FUNCTION_TYPE) {
      ObRoutineParam result;
      result.set_param_position(0); result.set_param_type(ObIntType);
      CHECK(committed.add_routine_param(result) == OB_SUCCESS);
    }
    ObTxDesc tx;
    SessionCatalogTestAccess::active(tx);
    writer.get_tx_desc() = &tx;
    struct Binding {
      ObSQLSessionInfo &session;
      ~Binding() { session.discard_plugin_catalog_transaction(); session.get_tx_desc() = nullptr; }
    } binding{writer};
    std::shared_ptr<RoutineSchemaOverlay> schema;
    std::shared_ptr<RoutinePrivilegeOverlay> privileges;
    std::shared_ptr<RoutineCatalogTransaction> journal;
    CHECK(writer.prepare_plugin_catalog_view(ObTxSEQ(10, 0), schema, privileges, journal) == OB_SUCCESS);
    ObRoutineInfo altered;
    CHECK(altered.assign(committed) == OB_SUCCESS);
    altered.set_schema_version(43);
    CHECK(altered.set_comment("private change") == OB_SUCCESS);
    CHECK(schema->stage(altered) == OB_SUCCESS);

    // Mimic the factory's separate committed guard, deliberately NOT bound to
    // the statement view. The production iterator must perform that binding.
    const auto show = [&](ObSQLSessionInfo &session, uint64_t id, int expected, const char *property) {
      ObSchemaGetterGuard guard;
      CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
      CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
      CHECK(MockSchemaService::cache_user(guard, reader) == OB_SUCCESS);
      CHECK(MockSchemaService::cache_routine(guard, committed) == OB_SUCCESS);
      CHECK(!guard.has_routine_overlay());
      Iterator iterator(arena);
      iterator.set_schema_guard(&guard); iterator.set_session(&session);
      CHECK(session.get_session_priv_info(iterator.get_session_priv()) == OB_SUCCESS);
      ObObj key; key.set_int(id);
      ObNewRange range;
      range.start_key_.assign(&key, 1); range.end_key_.assign(&key, 1);
      ObSEArray<ObNewRange, 1> ranges;
      CHECK(ranges.push_back(range) == OB_SUCCESS && iterator.set_key_ranges(ranges) == OB_SUCCESS);
      ObNewRow *row = nullptr;
      const int ret = iterator.inner_get_next_row(row);
      if (ret != expected) std::cerr << "SHOW routine type=" << type << " id=" << id
          << " writer=" << (&session == &writer) << " ret=" << ret << " expected=" << expected << std::endl;
      CHECK(ret == expected);
      if (ret == OB_SUCCESS) {
        CHECK(row && row->count_ == 1);
        if (!property) CHECK(row->cells_[0].is_null());
        else {
          CHECK(!row->cells_[0].is_null());
          const auto text = row->cells_[0].get_string();
          CHECK(std::string(text.ptr(), text.length()).find(property) != std::string::npos);
        }
        CHECK(iterator.inner_get_next_row(row) == OB_ITER_END);
      }
    };
    show(writer, 9001, OB_SUCCESS, "private change");
    show(observer, 9001, OB_SUCCESS, "committed property");
    show(denied, 9001, OB_SUCCESS, nullptr); // Binding must not bypass SHOW privileges.
    CHECK(schema->erase(100, committed.get_routine_name(), type, 9001) == OB_SUCCESS);
    show(writer, 9001, OB_ERR_SP_DOES_NOT_EXIST, nullptr);
    show(observer, 9001, OB_SUCCESS, "committed property");
    altered.set_routine_id(9002); // New identity exists only in the caller view.
    CHECK(schema->stage(altered) == OB_SUCCESS);
    show(writer, 9002, OB_SUCCESS, "private change");
    CHECK(writer.rollback_plugin_catalog_view(771, ObTxSEQ(10, 0)) == OB_SUCCESS);
    show(writer, 9001, OB_SUCCESS, "committed property");
    SessionCatalogTestAccess::active(tx, 772);
    show(writer, 9001, OB_TRANS_INVALID_STATE, nullptr); // No silent committed fallback.
  }
}
}
#endif
