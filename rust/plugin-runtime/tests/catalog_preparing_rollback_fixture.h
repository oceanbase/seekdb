// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real SQL savepoint/session/Rust journal path; data rollback/abort is controlled.
#pragma once

namespace catalog_preparing_rollback_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::transaction;
using namespace oceanbase::share::schema;

inline void run()
{
  for (int scenario = 0; scenario < 11; ++scenario) {
    struct Service final : ObTransService {
      int error = OB_SUCCESS, rollbacks = 0, aborts = 0, points = 0;
      int create_branch_savepoint(ObTxDesc &, int16_t branch, ObTxSEQ &point) override {
        point = ObTxSEQ(21 + points++, branch); return OB_SUCCESS;
      }
      int rollback_to_implicit_savepoint(ObTxDesc &, ObTxSEQ, int64_t,
          bool, ObTxCleanPolicy) override { ++rollbacks; return error; }
      int abort_tx(ObTxDesc &, int) override { ++aborts; return OB_SUCCESS; }
    };
    auto service = std::make_unique<Service>();
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    session.set_database_id(100); session.set_priv_user_id(123);
    ObTxDesc tx;
    SessionCatalogTestAccess::active(tx);
    struct Binding {
      ObSQLSessionInfo &session;
      ObTransService *previous = oceanbase::share::server_service<ObTransService>();
      Binding(ObSQLSessionInfo &session, ObTxDesc &tx, ObTransService &service) : session(session) {
        session.get_tx_desc() = &tx;
        oceanbase::share::bind_server_service<ObTransService>(&service);
      }
      ~Binding() {
        session.discard_plugin_catalog_transaction(); session.get_tx_desc() = nullptr;
        oceanbase::share::bind_server_service<ObTransService>(previous);
      }
    } binding(session, tx, *service);
    std::shared_ptr<RoutineSchemaOverlay> schema;
    std::shared_ptr<RoutinePrivilegeOverlay> privileges;
    std::shared_ptr<RoutineCatalogTransaction> journal;
    CHECK(session.prepare_plugin_catalog_view(ObTxSEQ(10, 0), schema, privileges, journal) == OB_SUCCESS);
    CHECK(journal->admit_ddl(771, ObTxSEQ(10, 0), 7) == OB_SUCCESS);
    CHECK(journal->record_schema_version(771, ObTxSEQ(10, 0), 600) == OB_SUCCESS);
    uint64_t version = 0, operations = 0;
    CHECK(journal->begin_prepare(771, version, operations) == OB_SUCCESS);
    CHECK(version == 600 && operations == 1);
    CHECK(journal->record_end_sign(771, 601) == OB_SUCCESS);
    if (scenario == 9) CHECK(journal->complete_prepare(771, 601, OB_SUCCESS) == OB_SUCCESS);
    CHECK(session.set_start_stmt() == OB_SUCCESS);
    ObSQLSessionInfo::StmtSavedValue saved;
    if (scenario != 6) CHECK(session.begin_nested_session(saved, false) == OB_SUCCESS);
    ObSqlCtx sql; sql.session_info_ = &session;
    ObExecContext child(arena);
    child.set_my_session(&session); child.set_sql_ctx(&sql);
    child.set_is_plugin_sql(scenario != 5);
    CHECK(child.create_physical_plan_ctx() == OB_SUCCESS);
    ObPhysicalPlan physical; physical.set_stmt_type(stmt::T_INSERT);
    child.get_physical_plan_ctx()->set_phy_plan(&physical);
    child.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
    child.get_das_ctx().set_savepoint(scenario == 7 ? ObTxSEQ::INVL() : ObTxSEQ(20, 0));
    if (scenario == 1) service->error = OB_TIMEOUT;
    if (scenario == 8) SessionCatalogTestAccess::active(tx, 771, 1001);
    if (scenario == 10) SessionCatalogTestAccess::committed(tx);
    {
      LinkExecCtxGuard link(session, child);
      ObTxSEQ point;
      CHECK(ObSqlTransControl::create_anonymous_savepoint(child, point) == OB_SUCCESS);
      CHECK(point > ObTxSEQ(20, 0));
      if (scenario == 2) point = ObTxSEQ(20, 0);
      if (scenario == 3) point = ObTxSEQ(10, 0);
      if (scenario == 4) point = ObTxSEQ(21, 1);
      const int expected = scenario == 0 ? OB_SUCCESS : scenario == 1 ? OB_TIMEOUT
          : scenario == 4 ? OB_NOT_SUPPORTED : scenario == 8 ? OB_TRANS_INVALID_STATE : OB_STATE_NOT_MATCH;
      CHECK(ObSqlTransControl::rollback_savepoint(child, point) == expected);
      CHECK(service->rollbacks == 1);
      if (scenario <= 1) {
        CHECK(service->aborts == 0 && session.has_plugin_catalog_transaction());
        CHECK(!schema->is_retired() && !privileges->is_retired());
        CHECK(journal->check_preparing(771) == OB_SUCCESS);
        // No thaw, end-sign removal or permission to roll back the catalog.
        CHECK(journal->rollback(771, point) == OB_STATE_NOT_MATCH);
        CHECK(journal->record_schema_version(771, point, 602) == OB_STATE_NOT_MATCH);
        if (scenario == 0) {
          CHECK(ObSqlTransControl::create_anonymous_savepoint(child, point) == OB_SUCCESS);
          CHECK(ObSqlTransControl::rollback_savepoint(child, point) == OB_SUCCESS);
          CHECK(journal->complete_prepare(771, 601, OB_SUCCESS) == OB_SUCCESS);
          CHECK(journal->schema_state(771, version, operations) == OB_SUCCESS);
          CHECK(version == 601 && operations == 2);
        }
      } else {
        CHECK(!session.has_plugin_catalog_transaction() && schema->is_retired() && privileges->is_retired());
        CHECK(service->aborts == (scenario == 8 ? 0 : 1));
      }
    }
    if (scenario != 6) CHECK(session.end_nested_session(saved) == OB_SUCCESS);
    CHECK(session.set_end_stmt() == OB_SUCCESS);
  }
}
}
