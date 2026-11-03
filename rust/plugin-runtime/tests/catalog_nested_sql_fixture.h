// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real connection opt-in, execution-context linking and statement-close logic;
// the transaction service is controlled, so this does not prove live SQL effects.
#ifndef SEEKDB_TEST_CATALOG_NESTED_SQL_FIXTURE_H_
#define SEEKDB_TEST_CATALOG_NESTED_SQL_FIXTURE_H_
#include "observer/ob_inner_sql_connection.h"
#include "sql/ob_sql_utils.h"
#include "sql/ob_sql_trans_control.h"

namespace oceanbase { namespace observer {
class PluginCatalogConnectionTestAccess {
public:
  static int init_context(ObInnerSQLConnection &connection, sql::ObExecContext &context) {
    return connection.init_plugin_catalog_context(context);
  }
  static bool enabled(const ObInnerSQLConnection &connection) { return connection.plugin_catalog_sql_; }
  static int check_mds(const ObInnerSQLConnection &connection, transaction::ObTxDataSourceType type) {
    return connection.check_mds_transaction(type);
  }
};
} }

namespace catalog_nested_sql_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::transaction;
using oceanbase::observer::ObInnerSQLConnection;
using oceanbase::observer::PluginCatalogConnectionTestAccess;

inline void run()
{
  struct Service final : ObTransService {
    int rollbacks = 0;
    int rollback_to_implicit_savepoint(ObTxDesc &tx, ObTxSEQ, int64_t,
        bool, ObTxCleanPolicy) override {
      ++rollbacks;
      CHECK(oceanbase::data_plane::tx_desc_id(&tx).get_id() == 771);
      return OB_SUCCESS;
    }
  };
  auto service = std::make_unique<Service>();
  ObArenaAllocator arena;
  ObSQLSessionInfo session;
  CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
  session.set_database_id(100); session.set_priv_user_id(123);
  ObSQLSessionMgr sessions;
  session.set_session_manager(&sessions);
  ObTxDesc tx;
  SessionCatalogTestAccess::idle(tx);
  struct Binding {
    ObSQLSessionInfo &session;
    ObTransService *previous = oceanbase::share::server_service<ObTransService>();
    Binding(ObSQLSessionInfo &session, ObTxDesc &tx, ObTransService &service) : session(session) {
      session.get_tx_desc() = &tx;
      oceanbase::share::bind_server_service<ObTransService>(&service);
    }
    ~Binding() {
      session.get_tx_desc() = nullptr;
      session.set_session_manager(nullptr);
      ObSQLSessionInfo::ExecCtxSessionRegister registration(session, nullptr);
      oceanbase::share::bind_server_service<ObTransService>(previous);
    }
  } binding(session, tx, *service);
  auto management = std::make_unique<oceanbase::rootserver::ObLocalManagementService>();
  ObAddr address;
  oceanbase::observer::ObVTIterCreator vt(*management, address);
  auto engine = std::make_unique<ObSql>(); // No SQL/storage is executed by this fixture.
  ObInnerSQLConnection connection;
  const auto signal = ObTxDataSourceType::DDL_TRANS;
  const auto other = static_cast<ObTxDataSourceType>(-1);
  CHECK(connection.register_multi_data_source(signal, nullptr, 0) == OB_NOT_INIT);
  CHECK(connection.enable_plugin_catalog_sql() == OB_TRANS_INVALID_STATE);
  CHECK(connection.init(engine.get(), &vt, &session) == OB_SUCCESS);
  CHECK(connection.register_multi_data_source(signal, nullptr, 0) == OB_ERR_UNEXPECTED);
  connection.set_is_in_trans(true);
  CHECK(PluginCatalogConnectionTestAccess::check_mds(connection, signal) == OB_SUCCESS);
  connection.set_is_in_trans(false);
  connection.set_spi_connection(true);
  CHECK(connection.enable_plugin_catalog_sql() == OB_TRANS_INVALID_STATE);
  ObExecContext parent(arena);
  parent.set_my_session(&session);
  // Parent-backed expression execution and parentless COMMIT preparation.
  for (bool with_parent : {true, false}) {
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, with_parent ? &parent : nullptr);
    CHECK(session.set_autocommit(true) == OB_SUCCESS);
    session.set_trans_type(ObTxClass::USER);
    CHECK(session.set_start_stmt() == OB_SUCCESS);
    ObSQLSessionInfo::StmtSavedValue saved;
    ObIInnerSQLConnection::SavedValue connection_saved;
    CHECK(connection.begin_nested_session(saved, connection_saved, false) == OB_SUCCESS);
    CHECK(session.get_nested_count() == 1);
    CHECK(connection.enable_plugin_catalog_sql() == OB_SUCCESS);
    CHECK(connection.register_multi_data_source(signal, nullptr, 0) == OB_TRANS_INVALID_STATE);
    SessionCatalogTestAccess::active(tx);
    CHECK(PluginCatalogConnectionTestAccess::check_mds(connection, signal) == OB_SUCCESS);
    CHECK(connection.register_multi_data_source(other, nullptr, 0) == OB_NOT_SUPPORTED);
    SessionCatalogTestAccess::active(tx, 771, 1000, true);
    CHECK(connection.register_multi_data_source(signal, nullptr, 0) == OB_ERR_READ_ONLY_TRANSACTION);
    SessionCatalogTestAccess::active(tx);
    SessionCatalogTestAccess::committed(tx);
    CHECK(connection.register_multi_data_source(signal, nullptr, 0) == OB_TRANS_INVALID_STATE);
    CHECK(!connection.is_in_trans() && session.get_tx_desc() == &tx);
    SessionCatalogTestAccess::idle(tx);
    for (auto type : {stmt::T_SELECT, stmt::T_INSERT, stmt::T_UPDATE, stmt::T_DELETE}) {
      for (bool rollback : {false, true}) {
        ObSqlCtx sql; sql.session_info_ = &session;
        ObExecContext child(arena);
        child.set_my_session(&session); child.set_sql_ctx(&sql);
        CHECK(child.create_physical_plan_ctx() == OB_SUCCESS);
        ObPhysicalPlan physical;
        physical.set_stmt_type(type);
        child.get_physical_plan_ctx()->set_phy_plan(&physical);
        child.get_physical_plan_ctx()->set_timeout_timestamp(INT64_MAX);
        if (type == stmt::T_SELECT && !rollback) {
          // Reproduce the original mismatch before opting in. A parent alone
          // must not change ordinary internal SQL's transaction ownership.
          LinkExecCtxGuard link(session, child);
          CHECK(!ObSQLUtils::is_nested_sql(&child));
          CHECK(ObSqlTransControl::end_stmt(child, false, false) == OB_ERR_UNEXPECTED);
          CHECK(session.get_nested_count() == 1);
        }
        CHECK(PluginCatalogConnectionTestAccess::init_context(connection, child) == OB_SUCCESS);
        CHECK(child.is_plugin_sql());
        // Engine planning/open and result close link independently. A read
        // result may be consumed later, so the marker must survive unlinking.
        {
          LinkExecCtxGuard link(session, child);
          CHECK(ObSQLUtils::is_nested_sql(&child) && child.get_nested_level() == 1);
          CHECK(child.get_parent_ctx() == (with_parent ? &parent : nullptr));
          bool autocommit = true;
          CHECK(session.get_autocommit(autocommit) == OB_SUCCESS && !autocommit);
        }
        CHECK(session.get_cur_exec_ctx() == (with_parent ? &parent : nullptr));
        {
          LinkExecCtxGuard link(session, child);
          CHECK(ObSqlTransControl::end_stmt(child, rollback, false) == OB_SUCCESS);
          CHECK(session.get_nested_count() == 1 && session.get_tx_desc() == &tx);
        }
        CHECK(session.get_database_id() == 100 && session.get_priv_user_id() == 123);
        CHECK(session.get_trans_type() == ObTxClass::USER);
      }
    }
    CHECK(connection.end_nested_session(saved, connection_saved) == OB_SUCCESS);
    CHECK(session.get_nested_count() == 0);
    CHECK(connection.register_multi_data_source(signal, nullptr, 0) == OB_TRANS_INVALID_STATE);
    bool autocommit = false;
    CHECK(session.get_autocommit(autocommit) == OB_SUCCESS && autocommit);
    ObExecContext stale(arena);
    stale.set_my_session(&session);
    CHECK(PluginCatalogConnectionTestAccess::init_context(connection, stale) == OB_TRANS_INVALID_STATE);
    CHECK(!stale.is_plugin_sql());
    CHECK(session.set_end_stmt() == OB_SUCCESS);
  }
  CHECK(service->rollbacks == 6); // Only the three writing statement kinds, twice.
  CHECK(connection.destroy() == OB_SUCCESS);
  CHECK(!PluginCatalogConnectionTestAccess::enabled(connection));
  // Reuse must not opt ordinary internal SQL into plugin semantics.
  CHECK(connection.init(engine.get(), &vt, &session) == OB_SUCCESS);
  ObExecContext ordinary(arena);
  ordinary.set_my_session(&session);
  CHECK(PluginCatalogConnectionTestAccess::init_context(connection, ordinary) == OB_SUCCESS);
  CHECK(!ordinary.is_plugin_sql() && !ObSQLUtils::is_nested_sql(&ordinary));
  CHECK(connection.destroy() == OB_SUCCESS);
}
}
#endif
