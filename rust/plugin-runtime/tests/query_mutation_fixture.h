// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Public SPI, concrete frame and actual Rust DSO. No transaction service is
// available here: test rejection/prepare failure, not successful data mutation.
#ifndef SEEKDB_TEST_QUERY_MUTATION_FIXTURE_H_
#define SEEKDB_TEST_QUERY_MUTATION_FIXTURE_H_
#include "sql/engine/expr/plugin_sql_context.h"
#include "sql/ob_sql.h"
#include "data_plane/transaction/ob_i_transaction_service.h"

namespace query_mutation_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using namespace oceanbase::share::plugin;
inline void run(ObPluginLoader &loader)
{
  const char *type = "core.type.bytes";
  seekdb_plugin_sql_binding_v1_t binding{};
  CHECK(loader.resolve_sql_extension(SEEKDB_PLUGIN_EXTENSION_FUNCTION, "seekdb_rust_routine_ddl", &type, 1, binding) == OB_SUCCESS);
  for (int scenario = 0; scenario < 13; ++scenario) {
    ObArenaAllocator arena;
    ObSQLSessionInfo session;
    CHECK(session.test_init(1, 1, &arena) == OB_SUCCESS);
    CHECK(session.load_default_sys_variable(false, false) == OB_SUCCESS);
    CHECK(session.set_user(ObString::make_string("fixture"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session.set_priv_user_id(123); session.set_database_id(OB_SYS_DATABASE_ID);
    session.set_user_priv_set(OB_PRIV_ALTER_ROUTINE);
    CHECK(session.set_default_database(ObString::make_string(OB_SYS_DATABASE_NAME)) == OB_SUCCESS);
    auto service = std::make_unique<MockSchemaService>();
    auto manager = std::make_unique<ObSchemaMgr>(); CHECK(manager->init() == OB_SUCCESS);
    CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
    ObSchemaGetterGuard guard;
    CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
    ObMySQLProxy proxy;
    struct Globals {
      ObMultiVersionSchemaService *schema = GCTX.schema_service_;
      ObMySQLProxy *proxy = GCTX.sql_proxy_;
      Globals(ObMultiVersionSchemaService &schema, ObMySQLProxy &proxy) {
        GCTX.schema_service_ = &schema; GCTX.sql_proxy_ = &proxy;
      }
      ~Globals() { GCTX.schema_service_ = schema; GCTX.sql_proxy_ = proxy; }
    } globals(*service, proxy);
    ObSqlCtx sql; sql.session_info_ = &session; sql.schema_guard_ = &guard;
    ObSql runtime;
    ObExecContext execution(arena); execution.set_my_session(&session); execution.set_sql_ctx(&sql);
    execution.set_pl_sql_runtime(&runtime);
    ObPhysicalPlan plan;
    if (scenario != 0) {
      CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
      execution.get_physical_plan_ctx()->set_phy_plan(&plan);
      execution.get_physical_plan_ctx()->set_timeout_timestamp(scenario == 11 ? 1 : INT64_MAX);
    }
    ObSQLSessionInfo::ExecCtxSessionRegister registration(session, &execution);
    struct Current {
      ObSQLSessionInfo &session;
      ~Current() { ObSQLSessionInfo::ExecCtxSessionRegister clear(session, nullptr); }
    } current{session};
    PluginSqlContext query(execution);
    seekdb_plugin_execution_context_v2_t context{}; context.v1.struct_size = sizeof(context);
    struct Sink {
      bool emitted = false;
      static seekdb_plugin_status_t emit(seekdb_plugin_host_handle_t *raw, const seekdb_plugin_execution_result_v1_t *) {
        reinterpret_cast<Sink *>(raw)->emitted = true; return SEEKDB_PLUGIN_STATUS_OK;
      }
    } sink;
    context.v1.host = reinterpret_cast<seekdb_plugin_host_handle_t *>(&sink);
    context.v1.emit_result = Sink::emit; query.attach(context);
    CHECK(context.sql_api && context.sql_api->struct_size == sizeof(seekdb_plugin_sql_api_v4_t));
    CHECK(context.sql_api->spi_major == 1 && context.sql_api->spi_minor == SEEKDB_PLUGIN_SQL_CATALOG_MUTATION_MINOR);
    const auto &api = *reinterpret_cast<const seekdb_plugin_sql_api_v4_t *>(context.sql_api);
    CHECK(api.v3.lookup_routine && api.v3.v2.poll_query && api.v3.v2.v1.execute && api.mutate_routine);
    const std::string statement = scenario == 3 ? std::string("a\0b", 3) : scenario == 5 ? "SELECT 1;" :
        scenario == 6 ? std::string(1, char(0xff)) : scenario == 12 ? "DROP FUNCTION f; SELECT 'unfinished" :
        "DROP FUNCTION IF EXISTS query_absent;";
    seekdb_plugin_routine_mutation_result_v1_t output{};
    output.struct_size = scenario == 9 ? 0 : sizeof(output); output.object_id = 999;
    if (scenario == 8) {
      std::thread wrong([&] { CHECK(api.mutate_routine(context.sql_context, statement.data(), statement.size(), &output)
          == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION); });
      wrong.join(); CHECK(output.object_id == 999 && query.error() == OB_SUCCESS);
    }
    struct Reenter final : ObIExtraStatusCheck {
      const seekdb_plugin_sql_api_v4_t &api; seekdb_plugin_sql_context_handle_t *context;
      Reenter(const seekdb_plugin_sql_api_v4_t &api, seekdb_plugin_sql_context_handle_t *context) : api(api), context(context) {}
      const char *name() const override { return "query-mutation-reentry"; }
      int check() const override {
        seekdb_plugin_routine_mutation_result_v1_t result{}; result.struct_size = sizeof(result);
        CHECK(api.mutate_routine(context, "DROP FUNCTION f;", 16, &result) == SEEKDB_PLUGIN_STATUS_FAILED_PRECONDITION);
        CHECK(result.database_error == OB_STATE_NOT_MATCH && result.object_id == 0);
        return OB_SUCCESS; // An ignored reentry must not reach prepare/write.
      }
    } reenter(api, context.sql_context);
    if (scenario == 10) sql.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
    seekdb_plugin_status_t status;
    CHECK(oceanbase::data_plane::query_transaction_service() == nullptr);
    if (scenario == 7) {
      ObIExtraStatusCheck::Guard extra(execution, reenter);
      status = api.mutate_routine(context.sql_context, statement.data(), statement.size(), &output);
    } else status = api.mutate_routine(context.sql_context, scenario == 2 ? nullptr : statement.data(),
        scenario == 4 ? 4ULL * 1024 * 1024 + 1 : statement.size(), &output);
    CHECK(status != SEEKDB_PLUGIN_STATUS_OK && query.error() != OB_SUCCESS);
    CHECK(!session.get_tx_desc() && !session.has_plugin_catalog_transaction());
    if (scenario == 9) CHECK(output.struct_size == 0 && output.object_id == 999);
    else {
      CHECK(output.struct_size == sizeof(output) && output.object_id == 0);
      CHECK(output.database_error == query.error());
      CHECK(output.close_error == 0 && output.identity_error == 0 && output.data_rollback_error == 0 &&
          output.view_rollback_error == 0 && output.poison_error == 0);
      CHECK(output.outcome == (scenario == 1 || scenario == 8 ? SEEKDB_PLUGIN_CATALOG_ROLLED_BACK : SEEKDB_PLUGIN_CATALOG_NOT_STARTED));
    }
    if (scenario == 0) CHECK(query.error() == OB_NOT_INIT);
    if (scenario == 1 || scenario == 8) CHECK(query.error() == OB_ERR_UNEXPECTED);
    if (scenario == 7) CHECK(query.error() == OB_STATE_NOT_MATCH);
    if (scenario == 10) CHECK(query.error() == OB_ERR_NO_PRIVILEGE);
    if (scenario == 11) CHECK(query.error() == OB_TIMEOUT);
    const auto original = query.error();
    output.struct_size = sizeof(output);
    CHECK(api.mutate_routine(context.sql_context, statement.data(), statement.size(), &output) != SEEKDB_PLUGIN_STATUS_OK);
    CHECK(output.database_error == original && output.object_id == 0 && output.outcome == SEEKDB_PLUGIN_CATALOG_NOT_STARTED);
    seekdb_plugin_query_status_v1_t poll{}; poll.struct_size = sizeof(poll);
    CHECK(api.v3.v2.poll_query(context.sql_context, &poll) != SEEKDB_PLUGIN_STATUS_OK && poll.database_error == original);
    seekdb_plugin_routine_lookup_result_v1_t lookup{}; lookup.struct_size = sizeof(lookup);
    CHECK(api.v3.lookup_routine(context.sql_context, 1, "f", 1, &lookup) != SEEKDB_PLUGIN_STATUS_OK && lookup.database_error == original);
    if (scenario == 0 || scenario == 1 || scenario == 3 || scenario == 5 || scenario == 11) {
      PluginSqlContext native_query(execution); native_query.attach(context);
      seekdb_plugin_execution_value_v1_t argument{}; argument.struct_size = sizeof(argument); argument.type_id = type;
      argument.data = reinterpret_cast<const uint8_t *>(statement.data()); argument.data_size = statement.size();
      CHECK(loader.execute_bound_function(binding, &context.v1, &argument, 1) != OB_SUCCESS);
      CHECK(native_query.error() == original && !sink.emitted);
      CHECK(!session.get_tx_desc() && !session.has_plugin_catalog_transaction());
    }
  }
}
}
#endif
