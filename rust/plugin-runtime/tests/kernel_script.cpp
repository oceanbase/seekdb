// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Links the real kernel parser, package adapter, and Rust archive. No SQL server.
#include "sql/resolver/ddl/extension_script.h"
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "sql/resolver/ddl/extension_routine_batch.h"
#include "sql/resolver/cmd/create_extension_resolver.h"
#include "sql/resolver/cmd/create_extension_stmt.h"
#include "sql/resolver/cmd/drop_extension_resolver.h"
#include "sql/resolver/cmd/drop_extension_stmt.h"
#include "sql/resolver/cmd/alter_extension_resolver.h"
#include "sql/resolver/cmd/alter_extension_stmt.h"
#include "sql/engine/cmd/alter_extension_executor.h"
#include "sql/engine/cmd/create_extension_executor.h"
#include "sql/engine/cmd/drop_extension_executor.h"
#include "sql/resolver/ob_resolver_utils.h"
#include "sql/privilege_check/ob_privilege_check.h"
#include "sql/ob_sql_utils.h"
#include "sql/resolver/ob_resolver_define.h"
#include "sql/ob_sql_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/pl/pl_cache/ob_pl_cache_mgr.h"
#include "sql/pl/ob_pl.h"
#include "sql/engine/ob_physical_plan.h"
#include "sql/ob_spi.h"
#include "share/plugin/extension_install.h"
#include "share/plugin/ob_plugin_catalog.h"
#include "share/plugin/ob_plugin_sql_catalog.h"
#include "share/schema/ob_priv_sql_service.h"
#include "share/ob_ddl_common.h"
#include "rootserver/ob_local_management_service.h"
#include "rootserver/pl_ddl/ob_pl_ddl_service.h"
#include "rootserver/ob_ddl_service.h"
#include "observer/ob_server_plugin_runtime.h"
#include "observer/ob_command_line_parser.h"
#include <getopt.h>
#include "observer/ob_server_options.h"
#include "lib/charset/ob_charset.h"
#include <cstdlib>
#include <iostream>
#include <new>
#include <vector>
#include "catalog_version_fixture.h"
#include "routine_overlay_guard_fixture.h"

#define CHECK(expr) do { if (!(expr)) { std::cerr << __LINE__ << ": " << #expr << std::endl; std::abort(); } } while (false)
#include "extension_requires_fixture.h"

#include "routine_id_reservation_fixture.h"
#include "routine_version_reservation_fixture.h"
#include "routine_sequence_fixture.h"
#include "routine_privilege_fixture.h"
#include "routine_create_fixture.h"
#include "routine_statement_fixture.h"
#include "plugin_expression_fixture.h"
#include "plugin_projection_fixture.h"
#include "rust_sql_expression_fixture.h"
#include "sql/ob_sql_init.h"
#include "plan_cache_eviction_fixture.h"
#include "routine_transaction_privileges_fixture.h"
#include "session_catalog_view_fixture.h"
#include "show_routine_catalog_fixture.h"
#include "query_operation_fixture.h"
#include "caller_catalog_operation_fixture.h"
#include "caller_routine_mutation_fixture.h"
#include "plugin_memory_table_fixture.h"

// Real results enter/leave an access region on construction/destruction even
// without a cache lookup. Count that balance; do not model real server epochs.
class CountingPlanCacheAccess final : public oceanbase::query::ObIPlanCacheAccessService
{
public:
  int enters_ = 0, leaves_ = 0, depth_ = 0;
  void enter_access() override { CHECK(depth_ == 0); ++depth_; ++enters_; }
  void leave_access() override { CHECK(depth_ == 1); --depth_; ++leaves_; }
  void check_current_thread() override { CHECK(false); }
  int get_global_safe_timestamp(int64_t &) const override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
};

class UncalledInstaller final : public oceanbase::share::plugin::IExtensionCatalogInstaller,
                                public oceanbase::share::plugin::IExtensionCatalogDropper,
                                public oceanbase::share::plugin::IExtensionCatalogUpdater
{
public:
  int read_update_source(uint64_t, uint64_t, const std::string &,
      oceanbase::share::plugin::ExtensionVersionSnapshot &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
  int install_extension(const oceanbase::share::plugin::ExtensionInstallSpec &,
                        oceanbase::share::plugin::IExtensionSchemaInstaller &,
                        uint64_t &, std::string &, oceanbase::common::ObMySQLTransaction *, int64_t) override
  {
    CHECK(false); // An uninitialized management service must never call us.
    return oceanbase::common::OB_ERR_UNEXPECTED;
  }
  int drop_extension(const oceanbase::share::plugin::ExtensionDropRequest &,
                     oceanbase::share::plugin::IExtensionSchemaDropper &,
                     uint64_t &, std::string &, oceanbase::common::ObMySQLTransaction *, int64_t) override
  {
    CHECK(false);
    return oceanbase::common::OB_ERR_UNEXPECTED;
  }
  int update_extension(const oceanbase::share::plugin::ExtensionUpdateRequest &,
                       oceanbase::share::plugin::IExtensionSchemaUpdater &,
                       uint64_t &, bool &, std::string &, oceanbase::common::ObMySQLTransaction *, int64_t) override
  {
    CHECK(false);
    return oceanbase::common::OB_ERR_UNEXPECTED;
  }
};

class UncalledDropper final : public oceanbase::share::plugin::IExtensionSchemaDropper
{
public:
  int preflight(const oceanbase::share::plugin::ExtensionDropRequest &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
  int admit(oceanbase::share::ObPluginSqlConnection &,
            const oceanbase::share::plugin::ExtensionDropRequest &,
            const oceanbase::share::plugin::ExtensionDropSnapshot &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
  int apply(oceanbase::share::ObPluginSqlConnection &,
            const oceanbase::share::plugin::ExtensionDropSnapshot &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
};

class UncalledUpdater final : public oceanbase::share::plugin::IExtensionSchemaUpdater
{
public:
  int preflight(const oceanbase::share::plugin::ExtensionUpdateRequest &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
  int admit(oceanbase::share::ObPluginSqlConnection &,
            const oceanbase::share::plugin::ExtensionUpdateRequest &,
            const oceanbase::share::plugin::ExtensionUpdateSnapshot &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
  int apply(oceanbase::share::ObPluginSqlConnection &,
            const oceanbase::share::plugin::ExtensionUpdateRequest &,
            const oceanbase::share::plugin::ExtensionUpdateSnapshot &,
            std::vector<oceanbase::share::plugin::ExtensionMemberIdentity> &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
};

class UncalledCatalogUpdater final : public oceanbase::share::plugin::IExtensionCatalogUpdater
{
public:
  int read_update_source(uint64_t, uint64_t, const std::string &,
      oceanbase::share::plugin::ExtensionVersionSnapshot &, std::string &) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
  int update_extension(const oceanbase::share::plugin::ExtensionUpdateRequest &,
                       oceanbase::share::plugin::IExtensionSchemaUpdater &,
                       uint64_t &, bool &, std::string &, oceanbase::common::ObMySQLTransaction *, int64_t) override
  { CHECK(false); return oceanbase::common::OB_ERR_UNEXPECTED; }
};

// Faulting transport only: exercise the real privilege SQL builder/reader's
// failure handling, not successful SQL execution or transaction visibility.
class FailedPrivilegeRead final : public oceanbase::common::ObMySQLTransaction
{
public:
  bool is_started() const override { return true; }
  int read(ReadResult &, const char *sql, const int32_t) override {
    query_ = sql;
    ++reads_;
    return oceanbase::common::OB_TIMEOUT;
  }
  int reads_ = 0;
  std::string query_;
};

// Only the Root observation boundary is substituted. This exercises the real
// planning/package/parser path, not Root authentication or database visibility.
class UpdateSourceCommand final : public oceanbase::rootserver::ObLocalManagementService
{
public:
  int read_extension_update_source(uint64_t tenant, uint64_t database, const std::string &name,
      oceanbase::sql::ObSQLSessionInfo &session,
      oceanbase::share::plugin::ExtensionVersionSnapshot &output, std::string &error) override
  {
    CHECK(tenant == 1 && database == 100 && name == "text_ops");
    ++reads_;
    output = source_;
    if (throw_allocation_) throw std::bad_alloc();
    error = status_ == oceanbase::common::OB_SUCCESS ? "" : "observation failed";
    if (change_database_) session.set_database_id(200);
    if (change_mode_) session.set_sql_mode(session.get_sql_mode() ^ SMO_ANSI_QUOTES);
    return status_;
  }
  oceanbase::share::plugin::ExtensionVersionSnapshot source_{91, 123, "1.0", ""};
  int reads_ = 0;
  int status_ = oceanbase::common::OB_SUCCESS;
  bool change_database_ = false;
  bool change_mode_ = false;
  bool throw_allocation_ = false;
};

int main(int argc, char **argv)
{
  CHECK(argc == 5);
  OB_LOGGER.set_file_name("kernel_script.log", true);
  OB_LOGGER.set_enable_async_log(false);
  OB_LOGGER.set_log_level("WARN");
  using namespace oceanbase::common;
  using namespace oceanbase::sql;
  CHECK(ObCharset::init_charset() == OB_SUCCESS);
  CHECK(init_sql_factories() == OB_SUCCESS);
  // Shared session prerequisites belong to the entrypoint, not to whichever
  // resolver fixture happens to run before other users of default variables.
  CHECK(oceanbase::share::ObSysVariables::init_default_values() == OB_SUCCESS);
  CHECK(ObBasicSessionInfo::init_sys_vars_cache_base_values() == OB_SUCCESS);
  PlanCacheEvictionTestAccess::run();
  routine_transaction_privileges_test::run();
  session_catalog_view_test::run();
  show_routine_catalog_test::run();
  query_operation_test::run();
  caller_catalog_operation_test::run();
  caller_routine_mutation_test::run();
  extension_requires_test::run(argv[1]);
  plugin_expression_test::run();
  routine_reservation_test::run();
  routine_version_test::run();
  routine_statement_test::run(argv[1]);
  plugin_projection_test::run();
  rust_sql_expression_test::run(argv[3], argv[2], argv[4]);
  plugin_memory_table_test::run(argv[3]);
  {
    // Real guard + schema manager methods, with an explicitly controlled binding
    // instead of a live schema service. No SQL, lock, or permission claims.
    using namespace oceanbase::share::schema;
    auto schema_service = std::make_unique<MockSchemaService>();
    auto manager = std::make_unique<ObSchemaMgr>();
    CHECK(manager->init() == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, 100, "ext_value", 1001, ROUTINE_FUNCTION_TYPE, 42) == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, 100, "untouched", 2001, ROUTINE_FUNCTION_TYPE, 42) == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, 100, "ext_value", 3001, ROUTINE_PROCEDURE_TYPE, 42) == OB_SUCCESS);
    ObSchemaGetterGuard guard, independent, table_guard, child, uninitialized;
    auto overlay = std::make_shared<RoutineSchemaOverlay>();
    CHECK(!guard.has_routine_overlay());
    CHECK(guard.attach_routine_overlay(overlay) == OB_INNER_STAT_ERROR);
    CHECK(MockSchemaService::bind(guard, *schema_service, *manager) == OB_SUCCESS);
    CHECK(MockSchemaService::bind(independent, *schema_service, *manager) == OB_SUCCESS);
    CHECK(MockSchemaService::bind(child, *schema_service, *manager) == OB_SUCCESS);
    CHECK(MockSchemaService::bind(table_guard, *schema_service, *manager,
                                  ObSchemaGetterGuard::TABLE_SCHEMA_GUARD) == OB_SUCCESS);
    CHECK(table_guard.attach_routine_overlay(overlay) == OB_NOT_SUPPORTED && !table_guard.has_routine_overlay());
    CHECK(guard.attach_routine_overlay(nullptr) == OB_INVALID_ARGUMENT && !guard.has_routine_overlay());
    CHECK(uninitialized.inherit_routine_overlay(guard) == OB_INNER_STAT_ERROR);
    CHECK(child.inherit_routine_overlay(uninitialized) == OB_INNER_STAT_ERROR);
    CHECK(child.inherit_routine_overlay(table_guard) == OB_NOT_SUPPORTED);
    CHECK(table_guard.inherit_routine_overlay(guard) == OB_NOT_SUPPORTED);
    CHECK(child.inherit_routine_overlay(child) == OB_INIT_TWICE);
    CHECK(child.inherit_routine_overlay(independent) == OB_SUCCESS && !child.has_routine_overlay());
    {
      auto other_service = std::make_unique<MockSchemaService>();
      ObSchemaGetterGuard other;
      CHECK(MockSchemaService::bind(other, *other_service, *manager) == OB_SUCCESS);
      CHECK(child.inherit_routine_overlay(other) == OB_STATE_NOT_MATCH && !child.has_routine_overlay());
    }
    const ObString name = ObString::make_string("EXT_VALUE");
    uint64_t id = OB_INVALID_ID;
    int64_t version = OB_INVALID_VERSION;
    CHECK(guard.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1001);
    CHECK(guard.get_schema_version(ROUTINE_SCHEMA, 1001, version) == OB_SUCCESS && version == 42);
    ObRoutineInfo routine;
    routine.set_database_id(100);
    routine.set_package_id(OB_INVALID_ID);
    routine.set_routine_id(1001);
    routine.set_owner_id(123);
    routine.set_routine_type(ROUTINE_FUNCTION_TYPE);
    routine.set_overload(0);
    routine.set_subprogram_id(0);
    routine.set_schema_version(43);
    CHECK(routine.set_routine_name(ObString::make_string("ext_value")) == OB_SUCCESS);
    CHECK(routine.set_routine_body(ObString::make_string("RETURN 43")) == OB_SUCCESS);
    CHECK(overlay->stage(routine) == OB_SUCCESS);
    CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS && guard.has_routine_overlay());
    CHECK(guard.attach_routine_overlay(overlay) == OB_INIT_TWICE);
    CHECK(guard.attach_routine_overlay(std::make_shared<RoutineSchemaOverlay>()) == OB_INIT_TWICE);
    CHECK(child.inherit_routine_overlay(guard) == OB_SUCCESS && child.has_routine_overlay());
    CHECK(child.inherit_routine_overlay(guard) == OB_INIT_TWICE);
    CHECK(child.inherit_routine_overlay(independent) == OB_INIT_TWICE && child.has_routine_overlay());
    {
      auto session = std::make_unique<ObSQLSessionInfo>();
      ObSPIService::PLPrepareCtx ordinary(*session, nullptr, false, false);
      CHECK(ordinary.parent_schema_guard_ == nullptr);
      ObSPIService::PLPrepareCtx nested(*session, nullptr, true, false, true, &child);
      CHECK(nested.parent_schema_guard_ == &child && nested.is_dynamic_sql_ && nested.is_parser_dynamic_sql_);
    }
    {
      // Actual PL cache entry points: no cache/session initialization is needed
      // or allowed on the provisional path. Sentinel keys must not be adjusted.
      using namespace oceanbase::pl;
      namespace lib = oceanbase::lib;
      auto plan_cache = std::make_unique<ObPlanCache>();
      RoutineCatalogTransaction undeliverable(771);
      CHECK(plan_cache->reserve_plugin_invalidations(undeliverable, 771) == OB_NOT_INIT);
      ObPLCacheCtx cache_ctx(*plan_cache);
      cache_ctx.schema_guard_ = &child;
      cache_ctx.key_.key_id_ = 1001;
      cache_ctx.key_.db_id_ = 777;
      cache_ctx.key_.sys_vars_str_ = ObString::make_string("unchanged");
      oceanbase::lib::MemoryContext memory;
      oceanbase::lib::ContextParam param;
      param.set_mem_attr(ObModIds::OB_PL_TEMP, ObCtxIds::DEFAULT_CTX_ID);
      CHECK(CURRENT_CONTEXT->CREATE_CONTEXT(memory, param) == OB_SUCCESS);
      {
        ObPLCacheObject object(NS_PRCR, memory);
        object.get_stat_for_update().db_id_ = 999;
        object.get_stat_for_update().hit_count_ = 17;
        object.get_stat_for_update().last_active_time_ = 23;
        for (const auto ns : {NS_PRCR, NS_SFC, NS_ANON, NS_PKG, NS_CALLSTMT}) {
          cache_ctx.key_.namespace_ = ns;
          object.set_ns(ns);
          ObCacheObjGuard output;
          CHECK(ObPLCacheMgr::get_pl_cache(nullptr, output, cache_ctx) == OB_SQL_PC_NOT_EXIST);
          CHECK(output.get_cache_obj() == nullptr);
          CHECK(ObPLCacheMgr::get_pl_cache(plan_cache.get(), output, cache_ctx) == OB_SQL_PC_NOT_EXIST);
          CHECK(ObPLCacheMgr::add_pl_cache(plan_cache.get(), &object, cache_ctx) == OB_SUCCESS);
          CHECK(cache_ctx.key_.namespace_ == ns && cache_ctx.key_.key_id_ == 1001);
          CHECK(cache_ctx.key_.db_id_ == 777 && cache_ctx.key_.sys_vars_str_ == "unchanged");
          CHECK(!object.added_lc() && object.get_ref_count() == 0);
          CHECK(object.get_stat().db_id_ == 999 && object.get_stat().hit_count_ == 17
                && object.get_stat().last_active_time_ == 23);
        }
        CHECK(ObPLCacheMgr::add_pl_cache(plan_cache.get(), nullptr, cache_ctx) == OB_INVALID_ARGUMENT);
        CHECK(ObPLCacheMgr::add_pl_cache(nullptr, &object, cache_ctx) == OB_ERR_UNEXPECTED);
        // Ordinary guards must not inherit the provisional bypass. With no
        // cache/session, the normal anonymous lookup is still an error.
        cache_ctx.schema_guard_ = &independent;
        cache_ctx.key_.namespace_ = NS_ANON;
        ObCacheObjGuard output;
        CHECK(ObPLCacheMgr::get_pl_cache(nullptr, output, cache_ctx) == OB_ERR_UNEXPECTED);
        CHECK(output.get_cache_obj() == nullptr);
      }
      {
        // Both text and prepared/PL SQL plan entry points must bypass cache
        // access even without session, physical context, or cache services.
        ObArenaAllocator allocator;
        ObSqlCtx sql_context;
        sql_context.schema_guard_ = &child;
        ObExecContext execution(allocator);
        ObPhysicalPlan physical(memory);
        ObPLFunction routine_object(memory);
        for (const auto mode : {PC_TEXT_MODE, PC_PS_MODE, PC_PL_MODE}) {
          ObPlanCacheCtx sql_cache_ctx(ObString::make_string("SELECT ext_value()"),
                                      mode, allocator, sql_context, execution);
          sql_cache_ctx.fp_result_.pc_key_.key_id_ = 1234;
          sql_cache_ctx.fp_result_.pc_key_.db_id_ = 777;
          sql_cache_ctx.fp_result_.pc_key_.name_ = ObString::make_string("unchanged");
          sql_cache_ctx.regenerating_expired_plan_ = true;
          sql_cache_ctx.need_destroy_node_ = true;
          ObCacheObjGuard output;
          CHECK(plan_cache->get_plan(allocator, sql_cache_ctx, output) == OB_SQL_PC_NOT_EXIST);
          CHECK(plan_cache->get_ps_plan(output, 1234, sql_cache_ctx) == OB_SQL_PC_NOT_EXIST);
          CHECK(output.get_cache_obj() == nullptr);
          CHECK(plan_cache->add_plan(&physical, sql_cache_ctx) == OB_NOT_SUPPORTED);
          CHECK(plan_cache->add_ps_plan(&physical, sql_cache_ctx) == OB_NOT_SUPPORTED);
          CHECK(plan_cache->add_ps_plan(&routine_object, sql_cache_ctx) == OB_NOT_SUPPORTED);
          CHECK(plan_cache->add_plan(nullptr, sql_cache_ctx) == OB_INVALID_ARGUMENT);
          CHECK(plan_cache->add_ps_plan<ObPhysicalPlan>(nullptr, sql_cache_ctx) == OB_INVALID_ARGUMENT);
          CHECK(plan_cache->add_ps_plan<ObPLFunction>(nullptr, sql_cache_ctx) == OB_INVALID_ARGUMENT);
          CHECK(sql_cache_ctx.fp_result_.pc_key_.key_id_ == 1234);
          CHECK(sql_cache_ctx.fp_result_.pc_key_.db_id_ == 777);
          CHECK(sql_cache_ctx.fp_result_.pc_key_.name_ == "unchanged");
          CHECK(sql_cache_ctx.need_destroy_node_ && sql_cache_ctx.regenerating_expired_plan_);
          CHECK(!physical.added_lc() && physical.get_ref_count() == 0);
          CHECK(!routine_object.added_lc() && routine_object.get_ref_count() == 0);
          CHECK(!sql_context.plan_cache_hit_);
        }
      }
      DESTROY_CONTEXT(memory);
    }
    const ObRoutineInfo *by_name = nullptr;
    const ObRoutineInfo *by_id = nullptr;
    CHECK(guard.get_standalone_function_info(100, name, by_name) == OB_SUCCESS && by_name != nullptr);
    CHECK(guard.get_routine_info(1001, by_id) == OB_SUCCESS && by_name == by_id);
    CHECK(by_id->get_routine_body() == "RETURN 43");
    uint64_t database = OB_INVALID_ID;
    CHECK(guard.get_schema_version(ROUTINE_SCHEMA, 1001, version, &database) == OB_SUCCESS
          && version == 43 && database == 100);
    bool exists = false;
    CHECK(guard.check_standalone_function_exist(100, name, exists) == OB_SUCCESS && exists);
    CHECK(guard.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1001);
    // Uncovered keys still consult the real in-memory base manager.
    CHECK(guard.get_standalone_function_id(100, ObString::make_string("untouched"), id) == OB_SUCCESS && id == 2001);
    CHECK(guard.get_standalone_procedure_id(100, name, id) == OB_SUCCESS && id == 3001);
    CHECK(overlay->erase(100, name, ROUTINE_FUNCTION_TYPE, 1001) == OB_SUCCESS);
    const ObRoutineInfo *deleted = by_id;
    CHECK(guard.get_standalone_function_info(100, name, deleted) == OB_SUCCESS && deleted == nullptr);
    deleted = by_id;
    CHECK(guard.get_routine_info(1001, deleted) == OB_SUCCESS && deleted == nullptr);
    CHECK(guard.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == OB_INVALID_ID);
    CHECK(guard.check_standalone_function_exist(100, name, exists) == OB_SUCCESS && !exists);
    CHECK(guard.get_schema_version(ROUTINE_SCHEMA, 1001, version) == OB_SUCCESS && version == OB_INVALID_VERSION);
    CHECK(by_id->get_routine_body() == "RETURN 43"); // old borrowed view survives DROP
    CHECK(independent.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1001);
    CHECK(independent.get_schema_version(ROUTINE_SCHEMA, 1001, version) == OB_SUCCESS && version == 42);
    // A new ID replaces the name, but cannot make the deleted old ID reappear.
    routine.set_routine_id(1002);
    routine.set_schema_version(44);
    CHECK(routine.set_routine_body(ObString::make_string("RETURN 44")) == OB_SUCCESS);
    CHECK(overlay->stage(routine) == OB_SUCCESS);
    CHECK(guard.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1002);
    CHECK(guard.get_standalone_function_info(100, name, by_name) == OB_SUCCESS
          && by_name->get_routine_body() == "RETURN 44");
    CHECK(guard.get_schema_version(ROUTINE_SCHEMA, 1002, version, &database) == OB_SUCCESS && version == 44 && database == 100);
    CHECK(guard.get_routine_info(1001, deleted) == OB_SUCCESS && deleted == nullptr);
    CHECK(by_id->get_routine_body() == "RETURN 43");
    deleted = by_id;
    CHECK(guard.get_standalone_function_info(100, ObString::make_string("missing"), deleted) == OB_SUCCESS && deleted == nullptr);
    deleted = by_id;
    CHECK(guard.get_routine_info(OB_INVALID_ID, deleted) == OB_INVALID_ARGUMENT && deleted == nullptr);
    std::weak_ptr<const RoutineSchemaOverlay> lifetime = overlay;
    overlay.reset();
    CHECK(!lifetime.expired() && guard.has_routine_overlay());
    CHECK(guard.get_standalone_function_info(100, name, by_name) == OB_SUCCESS && by_name->get_routine_id() == 1002);
    CHECK(guard.reset() == OB_SUCCESS && !guard.has_routine_overlay() && !lifetime.expired());
    // The child owns the provisional view, not a borrowed pointer to its parent.
    CHECK(child.get_standalone_function_info(100, name, by_name) == OB_SUCCESS
          && by_name != nullptr && by_name->get_routine_id() == 1002);
    CHECK(child.get_routine_info(1001, deleted) == OB_SUCCESS && deleted == nullptr);
    CHECK(child.get_schema_version(ROUTINE_SCHEMA, 1002, version) == OB_SUCCESS && version == 44);
    CHECK(child.get_standalone_function_id(100, ObString::make_string("untouched"), id) == OB_SUCCESS && id == 2001);
    ObSchemaGetterGuard grandchild;
    CHECK(MockSchemaService::bind(grandchild, *schema_service, *manager) == OB_SUCCESS);
    CHECK(grandchild.inherit_routine_overlay(child) == OB_SUCCESS);
    CHECK(child.reset() == OB_SUCCESS && !child.has_routine_overlay() && !lifetime.expired());
    CHECK(grandchild.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1002);
    CHECK(grandchild.reset() == OB_SUCCESS && lifetime.expired());
    // Borrowed pointers are deliberately not used after the final owner resets.
    CHECK(MockSchemaService::bind(guard, *schema_service, *manager) == OB_SUCCESS);
    CHECK(guard.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1001);
    CHECK(guard.get_schema_version(ROUTINE_SCHEMA, 1001, version) == OB_SUCCESS && version == 42);
    {
      // Retry/cursor owner survives the original guard and repeated refreshes.
      auto retry_overlay = std::make_shared<RoutineSchemaOverlay>();
      CHECK(retry_overlay->erase(100, name, ROUTINE_FUNCTION_TYPE, 1001) == OB_SUCCESS);
      CHECK(retry_overlay->stage(routine) == OB_SUCCESS);
      std::weak_ptr<const RoutineSchemaOverlay> retained = retry_overlay;
      ObSchemaGetterGuard parent;
      CHECK(MockSchemaService::bind(parent, *schema_service, *manager) == OB_SUCCESS);
      CHECK(parent.attach_routine_overlay(retry_overlay) == OB_SUCCESS);
      std::shared_ptr<const RoutineSchemaOverlay> captured = retry_overlay;
      CHECK(uninitialized.capture_routine_overlay(captured) == OB_INNER_STAT_ERROR && captured == nullptr);
      captured = retry_overlay;
      CHECK(table_guard.capture_routine_overlay(captured) == OB_NOT_SUPPORTED && captured == nullptr);
      CHECK(independent.capture_routine_overlay(captured) == OB_SUCCESS && captured == nullptr);
      CountingPlanCacheAccess access;
      auto retry_session = std::make_unique<ObSQLSessionInfo>();
      auto spi = std::make_unique<ObSPIResultSet>();
      CHECK(spi->init(*retry_session, access) == OB_SUCCESS);
      CHECK(access.depth_ == 1 && access.enters_ == 1 && access.leaves_ == 0);
      CHECK(spi->restore_routine_overlay() == OB_STATE_NOT_MATCH);
      CHECK(spi->capture_routine_overlay(&parent) == OB_SUCCESS);
      CHECK(spi->capture_routine_overlay(nullptr) == OB_INIT_TWICE);
      CHECK(parent.reset() == OB_SUCCESS);
      retry_overlay.reset();
      CHECK(!retained.expired());
      for (int attempt = 0; attempt < 3; ++attempt) {
        spi->get_sql_ctx().schema_guard_ = &independent;
        spi->reset_member_for_retry(*retry_session);
        CHECK(access.depth_ == 1 && access.enters_ == attempt + 2 && access.leaves_ == attempt + 1);
        CHECK(spi->get_sql_ctx().schema_guard_ == nullptr && !retained.expired());
        auto &fresh = spi->get_scheme_guard();
        CHECK(fresh.reset() == OB_SUCCESS);
        CHECK(MockSchemaService::bind(fresh, *schema_service, *manager) == OB_SUCCESS);
        CHECK(!fresh.has_routine_overlay());
        CHECK(spi->restore_routine_overlay() == OB_SUCCESS && fresh.has_routine_overlay());
        CHECK(spi->restore_routine_overlay() == OB_INIT_TWICE);
        CHECK(fresh.get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1002);
        CHECK(fresh.get_routine_info(1001, deleted) == OB_SUCCESS && deleted == nullptr);
        CHECK(fresh.get_schema_version(ROUTINE_SCHEMA, 1002, version) == OB_SUCCESS && version == 44);
      }
      spi->reset();
      CHECK(access.depth_ == 0 && access.enters_ == 4 && access.leaves_ == 4);
      CHECK(retained.expired() && !spi->get_scheme_guard().has_routine_overlay());
      CHECK(spi->restore_routine_overlay() == OB_STATE_NOT_MATCH);
      CHECK(spi->capture_routine_overlay(&independent) == OB_SUCCESS);
      CHECK(MockSchemaService::bind(spi->get_scheme_guard(), *schema_service, *manager) == OB_SUCCESS);
      CHECK(spi->restore_routine_overlay() == OB_SUCCESS && !spi->get_scheme_guard().has_routine_overlay());
      CHECK(spi->get_scheme_guard().get_standalone_function_id(100, name, id) == OB_SUCCESS && id == 1001);
      CHECK(spi->capture_routine_overlay(&guard) == OB_INIT_TWICE);
    }
  }
  {
    oceanbase::share::ObPluginSqlRowReader reader;
    int64_t integer = 99;
    ObString text = ObString::make_string("stale");
    CHECK(reader.read_int64(0, integer) == OB_NOT_INIT && integer == 0);
    CHECK(reader.read_text(0, text) == OB_NOT_INIT && text.empty());
    oceanbase::share::plugin::ObPluginCatalog catalog;
    oceanbase::share::plugin::ExtensionDropRequest request;
    UncalledDropper dropper;
    uint64_t removed = 99;
    std::string error = "stale";
    CHECK(catalog.drop_extension(request, dropper, removed, error, nullptr, 0) == OB_NOT_INIT);
    CHECK(removed == 0 && error.empty());
    UncalledUpdater updater;
    oceanbase::share::plugin::ExtensionUpdateRequest update;
    bool changed = true;
    removed = 99;
    error = "stale";
    CHECK(catalog.update_extension(update, updater, removed, changed, error, nullptr, 0) == OB_NOT_INIT);
    CHECK(removed == 0 && !changed && error.empty());
    oceanbase::share::plugin::ExtensionVersionSnapshot version{99, 88, "stale", "stale"};
    error = "stale";
    CHECK(catalog.read_update_source(1, 2, "text_ops", version, error) == OB_NOT_INIT);
    CHECK(version.extension_id_ == 0 && version.owner_id_ == 0 && version.version_.empty()
          && version.native_module_id_.empty() && error.empty());
  }
  {
    // Production catalog + SQL row adapter + Rust validation, with a controlled
    // transport/result fixture. No DDL, transactions, packages, or native loads.
    using oceanbase::share::plugin::ExtensionVersionSnapshot;
    auto check_read = [](ExtensionVersionRows &fixture, int expected) {
      oceanbase::share::plugin::ObPluginCatalog catalog;
      CHECK(catalog.init(&fixture) == OB_SUCCESS && fixture.reads == 0);
      ExtensionVersionSnapshot version{99, 88, "stale", "stale"};
      std::string error = "stale";
      CHECK(catalog.read_update_source(1, 2, "text_ops", version, error) == expected);
      CHECK(fixture.starts == 0 && fixture.ends == 0 && fixture.writes == 0);
      if (expected == OB_SUCCESS) {
        CHECK(version.extension_id_ == 91 && version.owner_id_ == 123);
        CHECK(version.version_ == fixture.rows[0].version && version.native_module_id_ == fixture.rows[0].module);
        CHECK(error.empty());
        CHECK(fixture.reads == 1 && fixture.closes == 1);
        CHECK(fixture.sql.find("FROM __all_extension_instance") != std::string::npos);
        CHECK(fixture.sql.find("tenant_id=1 AND database_id=2 AND extension_name='text_ops'") != std::string::npos);
        CHECK(fixture.sql.find("FOR UPDATE") == std::string::npos);
        auto retained = version.version_;
        fixture.rows[0].version.assign("overwritten transport storage");
        CHECK(version.version_ == retained);
      } else {
        CHECK(version.extension_id_ == 0 && version.owner_id_ == 0 && version.version_.empty()
              && version.native_module_id_.empty());
      }
    };
    { ExtensionVersionRows f; check_read(f, OB_SUCCESS); }
    { ExtensionVersionRows f; f.rows[0].version = "release / candidate";
      f.rows[0].module = "seekdb.native"; check_read(f, OB_SUCCESS); }
    { ExtensionVersionRows f; f.rows.clear(); check_read(f, OB_ENTRY_NOT_EXIST); }
    { ExtensionVersionRows f; f.rows.push_back({}); check_read(f, OB_INVALID_DATA); }
    for (int64_t invalid : {int64_t{0}, int64_t{-1}}) {
      ExtensionVersionRows f; f.rows[0].id = invalid; check_read(f, OB_INVALID_DATA);
      ExtensionVersionRows g; g.rows[0].owner = invalid; check_read(g, OB_INVALID_DATA);
    }
    for (const auto &text : {std::string{}, std::string(256, 'v'), std::string("bad\0label", 9),
                            std::string("\xff", 1), std::string("bad\nlabel")}) {
      ExtensionVersionRows f; f.rows[0].version = text; check_read(f, OB_INVALID_DATA);
    }
    for (const auto &text : {std::string(256, 'm'), std::string("bad\0module", 10), std::string("\xff", 1)}) {
      ExtensionVersionRows f; f.rows[0].module = text; check_read(f, OB_INVALID_DATA);
    }
    { ExtensionVersionRows f; f.read_status = OB_TIMEOUT; check_read(f, OB_TIMEOUT); }
    { ExtensionVersionRows f; f.close_status = OB_TIMEOUT; check_read(f, OB_TIMEOUT); }
    for (int position : {0, 1}) {
      ExtensionVersionRows f; f.fail_next_at = position; check_read(f, OB_TIMEOUT);
    }
    for (int column = 0; column < 4; ++column) {
      ExtensionVersionRows f; f.fail_field = column; check_read(f, OB_ERR_NULL_VALUE);
    }
    { ExtensionVersionRows f; f.active = true; check_read(f, OB_STATE_NOT_MATCH); CHECK(f.reads == 0); }
    ExtensionVersionRows fixture;
    oceanbase::share::plugin::ObPluginCatalog catalog;
    CHECK(catalog.init(&fixture) == OB_SUCCESS);
    for (const auto &name : {std::string{}, std::string(256, 'n'), std::string("bad\0name", 8),
                             std::string("\xff", 1)}) {
      ExtensionVersionSnapshot output{99, 88, "stale", "stale"};
      std::string error;
      CHECK(catalog.read_update_source(1, 2, name, output, error) == OB_INVALID_ARGUMENT);
      CHECK(output.extension_id_ == 0 && fixture.reads == 0);
    }
    for (uint64_t invalid_id : {uint64_t{0}, uint64_t{1} << 63}) {
      ExtensionVersionSnapshot output{99, 88, "stale", "stale"};
      std::string error;
      CHECK(catalog.read_update_source(invalid_id, 2, "text_ops", output, error) == OB_INVALID_ARGUMENT);
      CHECK(output.extension_id_ == 0 && fixture.reads == 0);
      CHECK(catalog.read_update_source(1, invalid_id, "text_ops", output, error) == OB_INVALID_ARGUMENT);
      CHECK(output.extension_id_ == 0 && fixture.reads == 0);
    }
  }
  ExtensionScript script;
  std::string error;
  {
    std::string query = "CREATE PROCEDURE query_probe() BEGIN SELECT 'a;b'; SELECT 2; END; -- tail";
    CHECK(script.load_routine_statement(query, 0, error) == OB_SUCCESS);
    CHECK(error.empty() && script.statements().count() == 1 && script.statements().at(0).node_->type_ == T_SP_CREATE);
    CHECK(script.source().name_.empty() && script.source().version_.empty() && script.source().scripts_.empty());
    CHECK(script.sql_bytes() == query.size());
    oceanbase::share::plugin::ExtensionInstallSpec spec;
    ObResolverParams query_services;
    ObSqlCtx query_context;
    ExtensionRoutineScriptResolver not_a_package(script, spec, query_services, query_context);
    CHECK(not_a_package.preflight_install(spec, error) == OB_STATE_NOT_MATCH);
    query.assign("changed");
    CHECK(script.statements().at(0).sql_.prefix_match("CREATE PROCEDURE"));
    CHECK(script.append_catalog_declarations({}, error) == OB_STATE_NOT_MATCH);
    CHECK(script.statements().empty());
    for (const char *text : {"ALTER FUNCTION f COMMENT 'query';", "DROP FUNCTION IF EXISTS f;",
                            "CREATE FUNCTION f() RETURNS INT RETURN 1;", "DROP PROCEDURE p;"}) {
      CHECK(script.load_routine_statement(text, 0, error) == OB_SUCCESS);
      CHECK(script.statements().count() == 1 && script.source().name_.empty());
    }
    for (const std::string &text : std::vector<std::string>{"", "-- only comment", "SELECT 1;",
        "CREATE TABLE t(a INT);", "BEGIN;", "DROP FUNCTION f; DROP FUNCTION g;",
        "DROP FUNCTION f; SELECT 'unfinished", std::string("DROP FUNCTION f;\0SELECT 1", 25),
        std::string("DROP FUNCTION ") + char(0xff), std::string(4 * 1024 * 1024 + 1, 'x')}) {
      CHECK(script.load_routine_statement(text, 0, error) != OB_SUCCESS);
      CHECK(script.statements().empty() && script.sql_bytes() == 0 && !error.empty());
    }
  }
  {
    // No package directory/control file: declarations use the same real parser.
    oceanbase::share::plugin::ExtensionPackageSource memory;
    memory.name_ = "memory_ops";
    memory.version_ = "2";
    memory.scripts_ = {{"", "1", "CREATE FUNCTION memory_count(v TEXT) RETURNS BIGINT RETURN CHAR_LENGTH(v); -- tail"},
                       {"1", "2", "CREATE PROCEDURE memory_probe() BEGIN SELECT 'a;b'; SELECT 2; END;"}};
    CHECK(script.load_source(memory, 0, error) == OB_SUCCESS);
    CHECK(script.statements().count() == 2 && script.statements().at(0).node_->type_ == T_SF_CREATE);
    CHECK(script.statements().at(1).node_->type_ == T_SP_CREATE);
    memory.scripts_[0].sql_ = "caller changed storage";
    CHECK(script.source().scripts_[0].sql_.find("memory_count") != std::string::npos);
    CHECK(script.load_source(script.source(), 0, error) == OB_SUCCESS); // self-alias safe copy
    CHECK(script.statements().count() == 2);
    memory = script.source();
    memory.scripts_[1].sql_ = "CREATE FUNCTION broken(";
    CHECK(script.load_source(memory, 0, error) == OB_ERR_PARSE_SQL);
    CHECK(script.statements().empty() && script.source().name_.empty());
    memory.scripts_[0].sql_ = "SELECT 'unfinished";
    memory.scripts_[1].sql_ = "'; SELECT 2;";
    CHECK(script.load_source(memory, 0, error) == OB_ERR_PARSE_SQL);
    CHECK(script.statements().empty() && script.source().scripts_.empty());
    memory.from_version_ = "2";
    memory.scripts_.clear();
    CHECK(script.load_source(memory, 0, error) == OB_SUCCESS);
    CHECK(script.statements().empty() && script.source().from_version_ == "2");
    memory.version_ = "3";
    memory.scripts_ = {{"2", "3", ""}};
    CHECK(script.load_source(memory, 0, error) == OB_SUCCESS);
    CHECK(script.statements().empty() && script.source().scripts_.size() == 1);
    memory.scripts_[0].from_version_ = "wrong";
    CHECK(script.load_source(memory, 0, error) == OB_INVALID_ARGUMENT);
    CHECK(script.statements().empty() && script.source().name_.empty());
    memory.from_version_.clear(); memory.version_ = "1";
    memory.scripts_ = {{"", "1", "-- comments only"}};
    CHECK(script.load_source(memory, 0, error) == OB_ERR_EMPTY_QUERY);
    memory.scripts_[0].sql_.clear();
    for (int i = 0; i < 4096; ++i) memory.scripts_[0].sql_ += "SELECT 1;\n";
    CHECK(script.load_source(memory, 0, error) == OB_SUCCESS && script.statements().count() == 4096);
    memory.scripts_[0].sql_ += "SELECT 2;\n";
    CHECK(script.load_source(memory, 0, error) == OB_SIZE_OVERFLOW && script.statements().empty());
  }
  CHECK(script.load(argv[1], "routines", "", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 2);
  CHECK(script.statements().at(0).node_->type_ == T_SF_CREATE);
  CHECK(script.statements().at(1).node_->type_ == T_SP_CREATE);
  CHECK(script.sql_mode() == 0);
  CHECK(script.statements().at(0).sql_.length() > 0);
  {
    CHECK(script.append_catalog_declarations({"CREATE FUNCTION catalog_added() RETURNS INT RETURN 1; -- tail",
        "CREATE PROCEDURE catalog_probe() BEGIN SELECT 'a;b'; END;"}, error) == OB_SUCCESS);
    CHECK(script.statements().count() == 4);
    CHECK(script.statements().at(2).node_->type_ == T_SF_CREATE && script.statements().at(3).node_->type_ == T_SP_CREATE);
    CHECK(script.append_catalog_declarations({}, error) == OB_STATE_NOT_MATCH && script.statements().empty());
    CHECK(script.load(argv[1], "routines", "", 0, error) == OB_SUCCESS);
    CHECK(script.append_catalog_declarations({"CREATE FUNCTION broken("}, error) == OB_ERR_PARSE_SQL);
    CHECK(script.statements().empty() && script.source().name_.empty());
    CHECK(script.load(argv[1], "routines", "", 0, error) == OB_SUCCESS);
    CHECK(script.append_catalog_declarations({"SELECT 'unfinished", "'; SELECT 2;"}, error) == OB_ERR_PARSE_SQL);
    CHECK(script.statements().empty());
    CHECK(script.load(argv[1], "routines", "", 0, error) == OB_SUCCESS);
  }
  {
    auto native = script.source();
    native.native_install_ = true; native.native_module_ = "org.test"; native.scripts_.clear();
    CHECK(script.load_source(native, 0, error) == OB_SUCCESS && script.statements().empty());
    CHECK(script.append_catalog_declarations({}, error) == OB_ERR_EMPTY_QUERY && script.source().name_.empty());
    CHECK(script.load_source(native, 0, error) == OB_SUCCESS);
    CHECK(script.append_catalog_declarations({"-- no objects"}, error) == OB_ERR_EMPTY_QUERY && script.statements().empty());
    CHECK(script.load_source(native, 0, error) == OB_SUCCESS);
    CHECK(script.append_catalog_declarations({"CREATE FUNCTION native_only() RETURNS INT RETURN 1;"}, error) == OB_SUCCESS);
    CHECK(script.statements().count() == 1 && script.source().scripts_.empty() && script.source().native_install_);
    CHECK(script.load(argv[1], "routines", "", 0, error) == OB_SUCCESS);
  }
  const auto first_sql = script.statements().at(0).sql_;
  CHECK(std::string(first_sql.ptr(), first_sql.length()).find("CHAR_LENGTH('a;b')") != std::string::npos);
  // A failure after a valid prefix cannot expose partial statements or source.
  CHECK(script.load(argv[1], "bad_tail", "", 0, error) == OB_ERR_PARSE_SQL);
  CHECK(script.statements().empty() && script.source().scripts_.empty());
  CHECK(!error.empty());
  CHECK(script.load(argv[1], "directives", "", 0, error) == OB_ERR_PARSE_SQL);
  CHECK(script.statements().empty());
  const int comments_status = script.load(argv[1], "comments", "", 0, error);
  if (comments_status != OB_ERR_EMPTY_QUERY) {
    std::cerr << "comment-only status=" << comments_status << ", " << error << std::endl;
  }
  CHECK(comments_status == OB_ERR_EMPTY_QUERY);
  CHECK(script.statements().empty());
  CHECK(script.load(argv[1], "many", "", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 4096);
  CHECK(script.statements().at(0).node_->type_ == T_SELECT);
  CHECK(script.statements().at(4095).node_->type_ == T_SELECT);
  CHECK(script.load(argv[1], "too_many", "", 0, error) == OB_SIZE_OVERFLOW);
  CHECK(script.statements().empty() && script.source().name_.empty());
  // Parsing is general; permission/transaction support is a separate preflight.
  CHECK(script.load(argv[1], "general", "", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 2);
  CHECK(script.statements().at(0).node_->type_ == T_CREATE_TABLE);
  CHECK(script.statements().at(1).node_->type_ == T_SELECT);
  CHECK(script.load(argv[1], "chain", "2", 0, error) == OB_SUCCESS);
  CHECK(script.source().version_ == "2" && script.source().scripts_.size() == 2);
  CHECK(script.statements().count() == 2);
  CHECK(script.statements().at(0).node_->type_ == T_SF_CREATE);
  CHECK(script.statements().at(1).node_->type_ == T_SF_CREATE);
  for (const char *name : {"chain_bad", "chain_tokens"}) {
    CHECK(script.load(argv[1], name, "2", 0, error) == OB_ERR_PARSE_SQL);
    CHECK(script.statements().empty() && script.source().scripts_.empty());
  }
  CHECK(script.load(argv[1], "chain_many", "2", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 4096);
  CHECK(script.load(argv[1], "chain_many", "3", 0, error) == OB_SIZE_OVERFLOW);
  CHECK(script.statements().empty() && script.source().scripts_.empty());
  CHECK(script.load(argv[2], "text_ops", "1.1", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 3 && script.source().version_ == "1.1");
  CHECK(script.statements().at(2).node_->type_ == T_SF_CREATE);
  CHECK(script.load_update(argv[2], "text_ops", "1.0", "1.1", 0, error) == OB_SUCCESS);
  CHECK(script.source().from_version_ == "1.0" && script.source().version_ == "1.1");
  CHECK(script.statements().count() == 1 && script.statements().at(0).node_->type_ == T_SF_CREATE);
  CHECK(script.load_update(argv[1], "update_general", "1", "2", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 2);
  CHECK(script.statements().at(0).node_->type_ == T_SF_DROP);
  CHECK(script.statements().at(1).node_->type_ == T_SF_CREATE);
  for (const char *name : {"update_bad", "update_tokens"}) {
    CHECK(script.load_update(argv[1], name, "1", "3", 0, error) == OB_ERR_PARSE_SQL);
    CHECK(script.source().from_version_.empty() && script.source().scripts_.empty() && script.statements().empty());
  }
  CHECK(script.load_update(argv[1], "update_many", "1", "3", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 4096);
  CHECK(script.load_update(argv[1], "update_many", "1", "4", 0, error) == OB_SIZE_OVERFLOW);
  CHECK(script.source().from_version_.empty() && script.statements().empty());
  CHECK(script.load_update(argv[1], "update_empty", "1", "4", 0, error) == OB_SUCCESS);
  CHECK(script.source().scripts_.size() == 3 && script.statements().empty());
  CHECK(script.source().from_version_ == "1" && script.source().version_ == "4");
  CHECK(script.load_update(argv[1], "update_empty", "4", "4", 0, error) == OB_SUCCESS);
  CHECK(script.source().scripts_.empty() && script.statements().empty());
  CHECK(script.source().from_version_ == "4" && script.source().version_ == "4");
  {
    using oceanbase::share::plugin::ExtensionVersionSnapshot;
    ExtensionUpdatePlan plan;
    const auto check_empty = [&]() {
      CHECK(!plan.ready());
      CHECK(plan.request().expected_extension_id_ == 0 && plan.request().tenant_id_ == 0
            && plan.request().database_id_ == 0 && plan.request().name_.empty()
            && plan.request().from_version_.empty() && plan.request().to_version_.empty());
      CHECK(plan.observed().extension_id_ == 0 && plan.observed().owner_id_ == 0
            && plan.observed().version_.empty() && plan.observed().native_module_id_.empty());
      CHECK(plan.script().source().name_.empty() && plan.script().source().scripts_.empty()
            && plan.script().statements().empty() && plan.script().sql_mode() == 0);
    };
    check_empty();
    ExtensionVersionSnapshot observed{91, 123, "1.0", ""};
    std::string name = "text_ops", target = "1.1";
    CHECK(plan.load(argv[2], 1, 100, name, observed, target, SMO_ANSI_QUOTES, error) == OB_SUCCESS);
    CHECK(plan.ready() && error.empty());
    CHECK(plan.request().tenant_id_ == 1 && plan.request().database_id_ == 100
          && plan.request().expected_extension_id_ == 91);
    CHECK(plan.script().statements().count() == 1 && plan.script().sql_mode() == SMO_ANSI_QUOTES);
    name.assign("changed"); target.assign("changed"); observed = {999, 888, "changed", "changed"};
    CHECK(plan.request().name_ == "text_ops" && plan.request().from_version_ == "1.0"
          && plan.request().to_version_ == "1.1");
    CHECK(plan.observed().extension_id_ == 91 && plan.observed().owner_id_ == 123
          && plan.observed().version_ == "1.0" && plan.observed().native_module_id_.empty());
    CHECK(plan.script().source().from_version_ == "1.0" && plan.script().source().version_ == "1.1");
    // Control-default resolution is recorded in the request, including no-op.
    observed = {91, 123, "1.0", ""};
    CHECK(plan.load(argv[2], 1, 100, "text_ops", observed, "", 0, error) == OB_SUCCESS);
    CHECK(plan.ready() && plan.request().to_version_ == "1.0"
          && plan.script().source().scripts_.empty() && plan.script().statements().empty());
    observed.version_ = "1";
    CHECK(plan.load(argv[1], 1, 100, "update_empty", observed, "4", 0, error) == OB_SUCCESS);
    CHECK(plan.ready() && plan.request().from_version_ == "1" && plan.request().to_version_ == "4"
          && plan.script().source().scripts_.size() == 3 && plan.script().statements().empty());
    CHECK(plan.load(argv[1], 1, 100, "update_general", observed, "2", 0, error) == OB_SUCCESS);
    CHECK(plan.script().statements().count() == 2
          && plan.script().statements().at(0).node_->type_ == T_SF_DROP
          && plan.script().statements().at(1).node_->type_ == T_SF_CREATE);
    // A later bad edge cannot leave an earlier ready plan or parsed prefix.
    CHECK(plan.load(argv[1], 1, 100, "update_bad", observed, "3", 0, error) == OB_ERR_PARSE_SQL);
    check_empty();
    CHECK(plan.load(argv[1], 1, 100, "update_general", observed, "missing", 0, error) != OB_SUCCESS);
    check_empty();
    // A SQL update may retain, but not silently replace, its native identity.
    observed.native_module_id_ = "seekdb.native";
    CHECK(plan.load(argv[1], 1, 100, "native", observed, "1", 0, error) == OB_SUCCESS);
    CHECK(plan.ready() && plan.observed().native_module_id_ == "seekdb.native");
    observed.native_module_id_ = "another.module";
    CHECK(plan.load(argv[1], 1, 100, "native", observed, "1", 0, error) == OB_STATE_NOT_MATCH);
    CHECK(!error.empty());
    check_empty();
    observed.native_module_id_.clear();
    CHECK(plan.load(argv[1], 1, 100, "native", observed, "1", 0, error) == OB_STATE_NOT_MATCH);
    check_empty();
    for (uint64_t invalid : {uint64_t{0}, uint64_t{1} << 63}) {
      observed = {91, 123, "1", ""};
      CHECK(plan.load(argv[1], invalid, 100, "update_empty", observed, "4", 0, error) == OB_INVALID_ARGUMENT);
      check_empty();
      CHECK(plan.load(argv[1], 1, invalid, "update_empty", observed, "4", 0, error) == OB_INVALID_ARGUMENT);
      check_empty();
      observed.extension_id_ = invalid;
      CHECK(plan.load(argv[1], 1, 100, "update_empty", observed, "4", 0, error) == OB_INVALID_ARGUMENT);
      check_empty();
      observed.extension_id_ = 91; observed.owner_id_ = invalid;
      CHECK(plan.load(argv[1], 1, 100, "update_empty", observed, "4", 0, error) == OB_INVALID_ARGUMENT);
      check_empty();
    }
    for (const std::string &version : {std::string{}, std::string(256, 'v')}) {
      observed = {91, 123, version, ""};
      CHECK(plan.load(argv[1], 1, 100, "update_empty", observed, "4", 0, error) == OB_INVALID_ARGUMENT);
      check_empty();
    }
    observed = {91, 123, "1", std::string(256, 'm')};
    CHECK(plan.load(argv[1], 1, 100, "update_empty", observed, "4", 0, error) == OB_INVALID_ARGUMENT);
    check_empty();
    // prepare uses the authenticated command interface before touching files.
    auto commands = std::make_unique<UpdateSourceCommand>();
    auto planning_session = std::make_unique<ObSQLSessionInfo>();
    oceanbase::share::schema::ObSchemaGetterGuard guard;
    ObSqlCtx planning_context;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_NOT_INIT);
    check_empty();
    planning_context.session_info_ = planning_session.get();
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_NOT_INIT);
    planning_context.schema_guard_ = &guard;
    planning_session->set_database_id(100);
    planning_context.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_ERR_NO_PRIVILEGE);
    planning_context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
    planning_session->set_nested_count(1);
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_NOT_SUPPORTED);
    planning_session->set_nested_count(0);
    for (uint64_t no_database : {uint64_t{0}, OB_INVALID_ID}) {
      planning_session->set_database_id(no_database);
      CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_ERR_NO_DB_SELECTED);
    }
    CHECK(commands->reads_ == 0);
    planning_session->set_database_id(100);
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_SUCCESS);
    CHECK(commands->reads_ == 1 && plan.ready() && plan.request().expected_extension_id_ == 91);
    commands->source_.version_.assign("replaced source");
    CHECK(plan.request().from_version_ == "1.0" && plan.observed().version_ == "1.0");
    commands->status_ = OB_TIMEOUT;
    CHECK(plan.prepare("/missing-extension-root", "text_ops", "1.1", planning_context, *commands, error) == OB_TIMEOUT);
    CHECK(commands->reads_ == 2 && error == "observation failed");
    check_empty();
    commands->source_ = {91, 123, "1.0", ""};
    commands->status_ = OB_SUCCESS;
    commands->change_database_ = true;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_STATE_NOT_MATCH);
    check_empty();
    planning_session->set_database_id(100);
    commands->change_database_ = false;
    commands->change_mode_ = true;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_STATE_NOT_MATCH);
    check_empty();
    CHECK(commands->reads_ == 4); // no silent reread/rebase of the installation
    commands->change_mode_ = false;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_SUCCESS);
    commands->throw_allocation_ = true;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_ALLOCATE_MEMORY_FAILED);
    CHECK(commands->reads_ == 6);
    check_empty();
    commands->throw_allocation_ = false;
    CHECK(plan.prepare(argv[2], "text_ops", "1.1", planning_context, *commands, error) == OB_SUCCESS);
    plan.reset();
    check_empty();
  }
  CHECK(script.load(argv[1], "general", "", 0, error) == OB_SUCCESS);
  ExtensionRoutineResolver resolved;
  ObResolverParams services;
  ObSqlCtx context;
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_NOT_SUPPORTED);
  CHECK(resolved.args().empty());
  CHECK(script.load(argv[2], "text_ops", "", 0, error) == OB_SUCCESS);
  CHECK(script.statements().count() == 2);
  CHECK(script.statements().at(0).node_->type_ == T_SF_CREATE);
  CHECK(script.statements().at(1).node_->type_ == T_SF_CREATE);
  // Real bridge admission checks; NOT a positive database resolver test.
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_NOT_INIT);
  CHECK(resolved.args().empty());
  services.disable_privilege_check_ = true;
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_ERR_NO_PRIVILEGE);
  services.disable_privilege_check_ = false;
  context.disable_privilege_check_ = PRIV_CHECK_FLAG_DISABLE;
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_ERR_NO_PRIVILEGE);
  CHECK(resolved.args().empty());
  context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  CHECK(script.load(argv[1], "ignore_conflict", "", 0, error) == OB_SUCCESS);
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_NOT_SUPPORTED);
  CHECK(script.load(argv[1], "native", "", 0, error) == OB_SUCCESS);
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_NOT_SUPPORTED);
  CHECK(resolved.args().empty());
  script.reset();
  CHECK(script.statements().empty() && script.source().name_.empty());
  CHECK(script.sql_mode() == 0);
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_INVALID_ARGUMENT);
  CHECK(resolved.args().empty());
  uint64_t extension_id = 42;
  int publication_status = OB_SUCCESS;
  CHECK(resolved.install(script, services, context, 1, extension_id, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && publication_status == OB_NOT_INIT);

  // Real composition/command admission, still no initialized database service.
  auto management = std::make_unique<oceanbase::rootserver::ObLocalManagementService>();
  auto session = std::make_unique<ObSQLSessionInfo>();
  oceanbase::share::plugin::ExtensionInstallSpec spec;
  oceanbase::query::ObIRootCommandService &command = *management;
  extension_id = 42;
  publication_status = OB_SUCCESS;
  CHECK(command.install_extension_routines(spec, resolved.args(), *session,
      extension_id, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && publication_status == OB_NOT_INIT && error.empty());
  oceanbase::share::plugin::ExtensionDropRequest drop_request;
  extension_id = 42;
  publication_status = OB_SUCCESS;
  error = "stale";
  CHECK(command.drop_extension_routines(drop_request, *session,
      extension_id, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && publication_status == OB_NOT_INIT && error.empty());
  oceanbase::share::plugin::ExtensionUpdateRequest update_request;
  ExtensionRoutineUpdateBatch update_batch;
  bool changed = true;
  extension_id = 42;
  publication_status = OB_SUCCESS;
  error = "stale";
  CHECK(command.update_extension_routines(update_request, update_batch.operations(), *session,
      extension_id, changed, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && !changed && publication_status == OB_NOT_INIT && error.empty());
  oceanbase::share::plugin::ExtensionVersionSnapshot update_source{99, 88, "stale", "stale"};
  error = "stale";
  CHECK(command.read_extension_update_source(1, 2, "text_ops", *session, update_source, error) == OB_NOT_INIT);
  CHECK(update_source.extension_id_ == 0 && update_source.owner_id_ == 0 && update_source.version_.empty()
        && update_source.native_module_id_.empty() && error.empty());
  std::weak_ptr<oceanbase::share::plugin::IExtensionCatalogInstaller> weak;
  std::weak_ptr<oceanbase::share::plugin::IExtensionCatalogDropper> weak_dropper;
  std::weak_ptr<oceanbase::share::plugin::IExtensionCatalogUpdater> weak_updater;
  {
    auto provider = std::make_shared<UncalledInstaller>();
    weak = provider;
    management->set_extension_catalog_installer(provider);
    management->set_extension_catalog_dropper(provider);
    management->set_extension_catalog_updater(provider);
    weak_dropper = provider;
    weak_updater = provider;
  }
  CHECK(!weak.expired());
  CHECK(!weak_dropper.expired());
  CHECK(!weak.owner_before(weak_dropper) && !weak_dropper.owner_before(weak));
  CHECK(!weak_updater.expired());
  CHECK(!weak.owner_before(weak_updater) && !weak_updater.owner_before(weak));
  CHECK(command.install_extension_routines(spec, resolved.args(), *session,
      extension_id, publication_status, error) == OB_NOT_INIT);
  extension_id = 42;
  publication_status = OB_SUCCESS;
  error = "stale";
  CHECK(command.drop_extension_routines(drop_request, *session,
      extension_id, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && publication_status == OB_NOT_INIT && error.empty());
  extension_id = 42;
  changed = true;
  publication_status = OB_SUCCESS;
  error = "stale";
  CHECK(command.update_extension_routines(update_request, update_batch.operations(), *session,
      extension_id, changed, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && !changed && publication_status == OB_NOT_INIT && error.empty());
  management->set_extension_catalog_installer(nullptr);
  CHECK(!weak.expired()); // Drop capability still pins the very same catalog.
  management->set_extension_catalog_dropper(nullptr);
  CHECK(!weak.expired() && !weak_dropper.expired() && !weak_updater.expired());
  // The third capability owns the exact same catalog, not a parallel registry.
  // An admitted command's local strong reference similarly outlives revocation.
  auto admitted_update_owner = weak_updater.lock();
  management->set_extension_catalog_updater(nullptr);
  CHECK(!weak.expired());
  admitted_update_owner.reset();
  CHECK(weak.expired());
  CHECK(weak_dropper.expired());
  CHECK(weak_updater.expired());
  CHECK(script.load(argv[1], "requires", "", 0, error) == OB_SUCCESS);
  services.root_command_service_ = management.get();
  services.session_info_ = session.get();
  context.session_info_ = session.get();
  CHECK(resolved.install(script, services, context, 1, extension_id, publication_status, error) == OB_NOT_INIT);
  CHECK(extension_id == 0 && publication_status == OB_NOT_INIT && resolved.args().empty());
  CHECK(script.source().requires_ == (std::vector<std::string>{"other"}));
  CHECK(script.load_update(argv[2], "text_ops", "1.0", "1.1", 0, error) == OB_SUCCESS);
  CHECK(resolved.resolve(script, services, context, 1, error) == OB_NOT_SUPPORTED);
  CHECK(resolved.args().empty() && error.find("update") != std::string::npos);
  CHECK(resolved.install(script, services, context, 1, extension_id, publication_status, error) == OB_NOT_SUPPORTED);
  CHECK(extension_id == 0 && publication_status == OB_NOT_INIT && resolved.args().empty());
  CHECK(error.find("update") != std::string::npos);
  // The SQL DDL retry classifier must not turn an unknown transaction outcome
  // from the Rust coordinator into an automatic second installation attempt.
  CHECK(!oceanbase::share::is_ddl_stmt_packet_retry_err(OB_TRANS_UNKNOWN));
  oceanbase::observer::ObServerPluginRuntime runtime;
  std::vector<oceanbase::share::plugin::ObPluginStatusSnapshot> memory_status(1);
  CHECK(runtime.list_plugin_status(memory_status) == OB_NOT_INIT && memory_status.empty());
  CHECK(!runtime.extension_catalog_installer());
  CHECK(!runtime.extension_catalog_dropper());
  CHECK(!runtime.extension_catalog_updater());
  std::string package_root = "stale";
  CHECK(runtime.extension_package_root(package_root) == OB_NOT_INIT);
  CHECK(package_root.empty());
  {
    // Real Observer runtime/catalog composition, not a second mock provider.
    // init binds the SQL client but does not read/bootstrap catalog tables or
    // recover/load installed modules. The transport must stay unused here.
    FailedPrivilegeRead unused_sql;
    oceanbase::observer::ObServerPluginRuntime composed;
    CHECK(composed.init(&unused_sql, argv[1], std::string("bad\0root", 8)) == OB_INVALID_ARGUMENT);
    CHECK(!composed.extension_catalog_installer() && !composed.extension_catalog_dropper()
          && !composed.extension_catalog_updater());
    CHECK(composed.init(&unused_sql, argv[1], argv[2]) == OB_SUCCESS);
    CHECK(composed.list_plugin_status(memory_status) == OB_SUCCESS && memory_status.empty());
    auto installer = composed.extension_catalog_installer();
    auto dropper = composed.extension_catalog_dropper();
    auto updater = composed.extension_catalog_updater();
    CHECK(installer && dropper && updater);
    CHECK(!installer.owner_before(dropper) && !dropper.owner_before(installer));
    CHECK(!installer.owner_before(updater) && !updater.owner_before(installer));
    std::weak_ptr<oceanbase::share::plugin::IExtensionCatalogUpdater> pending = updater;
    CHECK(composed.init(&unused_sql, argv[1], argv[2]) == OB_INIT_TWICE);
    composed.destroy();
    memory_status.emplace_back();
    CHECK(composed.list_plugin_status(memory_status) == OB_NOT_INIT && memory_status.empty());
    CHECK(!composed.extension_catalog_installer() && !composed.extension_catalog_dropper()
          && !composed.extension_catalog_updater());
    installer.reset();
    dropper.reset();
    CHECK(!pending.expired()); // the command-held updater alone keeps catalog alive
    updater.reset();
    CHECK(pending.expired());
    composed.destroy();
    CHECK(unused_sql.reads_ == 0);
  }

  // Actual command-line parser, no listener or SQL engine. Resolve relative
  // package paths before the server's later chdir into its base directory.
  oceanbase::observer::ObServerOptions options;
  oceanbase::observer::ObCommandLineParser command_line;
  char program[] = "seekdb";
  char option[] = "--extension-dir";
  char directory[] = "packages"; // runner's cwd is the temporary fixture parent
  char *arguments[] = {program, option, directory, nullptr};
  CHECK(command_line.parse_args(3, arguments, options) == OB_SUCCESS);
  CHECK(options.extension_dir_.string() == ObString::make_string(argv[1]));
  CHECK(options.plugin_memory_limit_ == UINT64_MAX && options.plugin_allocation_limit_ == UINT64_MAX);
  // The production parser invokes Rust, not a duplicate test-only grammar.
  const auto parse_limits = [&](std::vector<std::string> text,
                                oceanbase::observer::ObServerOptions &opts) {
    std::vector<char *> args;
    for (auto &word : text) args.push_back(word.data());
    args.push_back(nullptr);
    optind = 0; // reset GNU getopt between independent invocations in this process
    return command_line.parse_args(static_cast<int>(text.size()), args.data(), opts);
  };
  CHECK(parse_limits({"seekdb", "--plugin-memory-limit=64MiB", "--plugin-allocation-limit", "4096"}, options)
        == OB_SUCCESS);
  CHECK(options.plugin_memory_limit_ == 64ULL * 1024 * 1024 && options.plugin_allocation_limit_ == 4096);
  {
    FailedPrivilegeRead unused_sql;
    oceanbase::observer::ObServerPluginRuntime configured;
    CHECK(configured.init(&unused_sql, argv[1], argv[2],
          options.plugin_memory_limit_, options.plugin_allocation_limit_) == OB_SUCCESS);
    configured.destroy();
    CHECK(unused_sql.reads_ == 0);
  }
  for (const std::string flag : {"--plugin-memory-limit", "--plugin-allocation-limit"}) {
    for (const std::string invalid : {"", "-1", "+1", " 1", "1 ", "1.5", "1KB",
                                    "18446744073709551616", "18446744073709551615TiB"}) {
      CHECK(parse_limits({"seekdb", flag + "=" + invalid}, options) == OB_INVALID_ARGUMENT);
      CHECK(options.plugin_memory_limit_ == 64ULL * 1024 * 1024 && options.plugin_allocation_limit_ == 4096);
    }
    CHECK(parse_limits({"seekdb", flag + "=" + std::string(65, '0')}, options) == OB_INVALID_ARGUMENT);
  }
  CHECK(parse_limits({"seekdb", "--plugin-allocation-limit=1KiB"}, options) == OB_INVALID_ARGUMENT);
  CHECK(options.plugin_memory_limit_ == 64ULL * 1024 * 1024 && options.plugin_allocation_limit_ == 4096);
  CHECK(parse_limits({"seekdb", "--plugin-memory-limit=0", "--plugin-allocation-limit=0"}, options) == OB_SUCCESS);
  CHECK(options.plugin_memory_limit_ == 0 && options.plugin_allocation_limit_ == 0);
  CHECK(parse_limits({"seekdb", "--plugin-memory-limit=unlimited", "--plugin-allocation-limit=unlimited"}, options)
        == OB_SUCCESS);
  CHECK(options.plugin_memory_limit_ == UINT64_MAX && options.plugin_allocation_limit_ == UINT64_MAX);

  // The existing DDL wire codec, not a mock copier. Releasing every source
  // object/buffer must leave all base and derived argument fields unchanged.
  ExtensionRoutineBatch batch;
  auto encode = [](const auto &arg) {
    const auto size = arg.get_serialize_size();
    CHECK(size > 0);
    std::vector<char> bytes(size);
    int64_t position = 0;
    CHECK(arg.serialize(bytes.data(), size, position) == OB_SUCCESS && position == size);
    return bytes;
  };
  std::vector<char> expected;
  {
    oceanbase::obcall::ObCreateRoutineArg original;
    std::string db = "owning_database";
    std::string audit = "CREATE FUNCTION owning_function() RETURNS INT RETURN 123";
    original.db_name_ = ObString(db.size(), db.data());
    original.ddl_stmt_str_ = ObString(audit.size(), audit.data());
    original.sync_from_primary_ = true;
    original.parallelism_ = 7;
    original.task_id_ = 91;
    original.is_parallel_ = true;
    CHECK(original.based_schema_object_infos_.push_back(oceanbase::share::schema::ObBasedSchemaObjectInfo(
        100, oceanbase::share::schema::DATABASE_SCHEMA, 23)) == OB_SUCCESS);
    original.is_or_replace_ = true;
    original.is_need_alter_ = true;
    original.with_if_not_exist_ = true;
    original.routine_info_.set_database_id(100);
    original.routine_info_.set_routine_id(1234);
    original.routine_info_.set_routine_type(oceanbase::share::schema::ROUTINE_FUNCTION_TYPE);
    CHECK(original.routine_info_.set_routine_name("owning_function") == OB_SUCCESS);
    CHECK(original.routine_info_.set_routine_body("RETURN 123") == OB_SUCCESS);
    ObString error_text = ObString::make_string("example compiler diagnostic");
    CHECK(original.error_info_.set_text(error_text) == OB_SUCCESS);
    oceanbase::share::schema::ObDependencyInfo dependency;
    CHECK(dependency.set_ref_obj_name("referenced_object") == OB_SUCCESS);
    CHECK(dependency.set_dep_reason("example dependency") == OB_SUCCESS);
    CHECK(original.dependency_infos_.push_back(dependency) == OB_SUCCESS);
    ObSEArray<const oceanbase::obcall::ObCreateRoutineArg *, 16> source;
    CHECK(source.push_back(&original) == OB_SUCCESS);
    expected = encode(original);
    CHECK(batch.assign(source) == OB_SUCCESS && batch.args().count() == 1);
    std::fill(db.begin(), db.end(), 'x');
    std::fill(audit.begin(), audit.end(), 'x');
  }
  CHECK(batch.args().at(0)->db_name_ == "owning_database");
  CHECK(batch.args().at(0)->routine_info_.get_routine_body() == "RETURN 123");
  CHECK(encode(*batch.args().at(0)) == expected);
  {
    oceanbase::obcall::ObCreateRoutineArg first;
    ObSEArray<const oceanbase::obcall::ObCreateRoutineArg *, 16> invalid;
    CHECK(invalid.push_back(&first) == OB_SUCCESS);
    CHECK(invalid.push_back(nullptr) == OB_SUCCESS);
    CHECK(batch.assign(invalid) == OB_INVALID_ARGUMENT && batch.args().empty());
    invalid.reset();
    CHECK(batch.assign(invalid) == OB_INVALID_ARGUMENT && batch.args().empty());
    for (int i = 0; i < 4097; ++i) CHECK(invalid.push_back(&first) == OB_SUCCESS);
    CHECK(batch.assign(invalid) == OB_INVALID_ARGUMENT && batch.args().empty());
  }

  // Real mixed DDL wire ownership. A DROP/CREATE of the same name must remain
  // ordered, not collapsed to a name-keyed map; ALTER retains its complete RPC.
  ExtensionRoutineUpdateBatch updates;
  using UpdateOperation = ExtensionRoutineUpdateBatch::Operation;
  using UpdateKind = UpdateOperation::Kind;
  std::vector<std::vector<char>> expected_updates;
  {
    oceanbase::obcall::ObCreateRoutineArg create;
    oceanbase::obcall::ObCreateRoutineArg alter;
    oceanbase::obcall::ObDropRoutineArg drop;
    std::string database = "updating_database";
    std::string name = "same_routine";
    std::string audit = "DROP FUNCTION IF EXISTS same_routine";
    drop.db_name_ = ObString(database.size(), database.data());
    drop.routine_name_ = ObString(name.size(), name.data());
    drop.ddl_stmt_str_ = ObString(audit.size(), audit.data());
    drop.routine_type_ = oceanbase::share::schema::ROUTINE_FUNCTION_TYPE;
    drop.if_exist_ = true;
    drop.task_id_ = 987;
    drop.sync_from_primary_ = true;
    CHECK(drop.based_schema_object_infos_.push_back(oceanbase::share::schema::ObBasedSchemaObjectInfo(
        345, oceanbase::share::schema::ROUTINE_SCHEMA, 67)) == OB_SUCCESS);
    ObString drop_diagnostic = ObString::make_string("drop diagnostic");
    CHECK(drop.error_info_.set_text(drop_diagnostic) == OB_SUCCESS);
    create.db_name_ = drop.db_name_;
    create.ddl_stmt_str_ = ObString::make_string("CREATE FUNCTION same_routine() RETURNS INT RETURN 1");
    create.routine_info_.set_database_id(123);
    create.routine_info_.set_routine_type(oceanbase::share::schema::ROUTINE_FUNCTION_TYPE);
    CHECK(create.routine_info_.set_routine_name(drop.routine_name_) == OB_SUCCESS);
    CHECK(create.routine_info_.set_routine_body("RETURN 1") == OB_SUCCESS);
    alter.db_name_ = drop.db_name_;
    alter.is_need_alter_ = true;
    alter.routine_info_.set_routine_id(345);
    alter.ddl_stmt_str_ = ObString::make_string("ALTER FUNCTION same_routine COMMENT 'changed'");
    CHECK(alter.routine_info_.set_routine_name(drop.routine_name_) == OB_SUCCESS);
    oceanbase::share::schema::ObDependencyInfo dependency;
    CHECK(dependency.set_ref_obj_name("retained_dependency") == OB_SUCCESS);
    CHECK(alter.dependency_infos_.push_back(dependency) == OB_SUCCESS);
    ObSEArray<UpdateOperation, 16> input;
    CHECK(input.push_back({UpdateKind::DROP, nullptr, &drop}) == OB_SUCCESS);
    CHECK(input.push_back({UpdateKind::CREATE, &create, nullptr}) == OB_SUCCESS);
    CHECK(input.push_back({UpdateKind::ALTER, &alter, nullptr}) == OB_SUCCESS);
    CHECK(input.push_back({UpdateKind::DROP, nullptr, &drop}) == OB_SUCCESS);
    expected_updates = {encode(drop), encode(create), encode(alter), encode(drop)};
    CHECK(updates.assign(input) == OB_SUCCESS);
    std::fill(database.begin(), database.end(), 'x');
    std::fill(name.begin(), name.end(), 'x');
    std::fill(audit.begin(), audit.end(), 'x');
  }
  for (int pass = 0; pass < 2; ++pass) {
    CHECK(updates.operations().count() == 4);
    const UpdateKind kinds[] = {UpdateKind::DROP, UpdateKind::CREATE, UpdateKind::ALTER, UpdateKind::DROP};
    for (int64_t i = 0; i < 4; ++i) {
      const auto &op = updates.operations().at(i);
      CHECK(op.has_valid_shape() && op.kind_ == kinds[i]);
      CHECK((op.drop_arg_ ? encode(*op.drop_arg_) : encode(*op.create_arg_)) == expected_updates[i]);
    }
    CHECK(updates.assign(updates.operations()) == OB_SUCCESS); // self-borrowing assignment
  }
  {
    oceanbase::obcall::ObCreateRoutineArg create;
    oceanbase::obcall::ObDropRoutineArg drop;
    for (const auto &bad : {UpdateOperation{},
         UpdateOperation{UpdateKind::CREATE, nullptr, nullptr},
         UpdateOperation{UpdateKind::DROP, &create, nullptr},
         UpdateOperation{UpdateKind::ALTER, nullptr, &drop},
         UpdateOperation{UpdateKind::CREATE, &create, &drop},
         UpdateOperation{static_cast<UpdateKind>(255), &create, nullptr}}) {
      ObSEArray<UpdateOperation, 16> input;
      CHECK(input.push_back({UpdateKind::CREATE, &create, nullptr}) == OB_SUCCESS);
      CHECK(updates.assign(input) == OB_SUCCESS); // repopulate before every failure
      CHECK(input.push_back(bad) == OB_SUCCESS);
      CHECK(updates.assign(input) == OB_INVALID_ARGUMENT && updates.operations().empty());
    }
    ObSEArray<UpdateOperation, 16> input;
    CHECK(updates.assign(input) == OB_SUCCESS && updates.operations().empty());
    for (int64_t i = 0; i < ExtensionRoutineUpdateBatch::MAX_OPERATIONS; ++i) {
      CHECK(input.push_back({UpdateKind::DROP, nullptr, &drop}) == OB_SUCCESS);
    }
    CHECK(updates.assign(input) == OB_SUCCESS);
    CHECK(updates.operations().count() == ExtensionRoutineUpdateBatch::MAX_OPERATIONS);
    CHECK(input.push_back({UpdateKind::DROP, nullptr, &drop}) == OB_SUCCESS);
    CHECK(updates.assign(input) == OB_INVALID_ARGUMENT && updates.operations().empty());
    // Bound the aggregate wire allocation, not just individual payloads. Two
    // legal-size RPCs can exceed the batch limit; failure leaves no first RPC.
    input.reset();
    std::string large(ExtensionRoutineUpdateBatch::MAX_WIRE_BYTES / 2, 'x');
    drop.ddl_stmt_str_ = ObString(large.size(), large.data());
    CHECK(input.push_back({UpdateKind::DROP, nullptr, &drop}) == OB_SUCCESS);
    CHECK(input.push_back({UpdateKind::DROP, nullptr, &drop}) == OB_SUCCESS);
    CHECK(updates.assign(input) == OB_SIZE_OVERFLOW && updates.operations().empty());
    updates.reset();
    updates.reset();
  }
  {
    // Admission at the actual ordinary ALTER bridge. An unstarted caller
    // transaction must not be started/ended/published by either ALTER variant.
    oceanbase::rootserver::ObDDLService ddl_service;
    oceanbase::rootserver::ObDDLSQLTransaction transaction(nullptr);
    oceanbase::obcall::ObCreateRoutineArg alter;
    for (const bool via_replacement : {false, true}) {
      alter.is_need_alter_ = via_replacement;
      CHECK(oceanbase::rootserver::ObPLDDLService::alter_routine(alter, ddl_service, &transaction)
            == OB_STATE_NOT_MATCH);
      CHECK(!transaction.is_started());
    }
    // Actual transaction-visible privilege reader rejects absent transactions,
    // never retains caller output or falls back to a stale schema-cache value.
    oceanbase::share::schema::ObRoutinePrivSortKey key(
        123, ObString::make_string("db"), ObString::make_string("f"),
        oceanbase::share::schema::ROUTINE_FUNCTION_TYPE);
    ObPrivSet privileges = OB_PRIV_EXECUTE;
    CHECK(oceanbase::share::schema::ObPrivSqlService::get_routine_priv_in_transaction(
        key, transaction, privileges) == OB_STATE_NOT_MATCH);
    CHECK(privileges == OB_PRIV_SET_EMPTY && !transaction.is_started());
    FailedPrivilegeRead failed_read;
    std::string odd_database = "db'quoted";
    std::string odd_routine = "f'quoted";
    oceanbase::share::schema::ObRoutinePrivSortKey odd_key(
        123, ObString(odd_database.size(), odd_database.data()),
        ObString(odd_routine.size(), odd_routine.data()), oceanbase::share::schema::ROUTINE_FUNCTION_TYPE);
    privileges = OB_PRIV_EXECUTE;
    CHECK(oceanbase::share::schema::ObPrivSqlService::get_routine_priv_in_transaction(
        odd_key, failed_read, privileges) == OB_TIMEOUT);
    CHECK(privileges == OB_PRIV_SET_EMPTY && failed_read.reads_ == 1);
    CHECK(failed_read.query_.find("SELECT all_priv FROM oceanbase.__all_routine_privilege") == 0);
    CHECK(failed_read.query_.find("FOR UPDATE") != std::string::npos);
    CHECK(failed_read.query_.find(odd_database) == std::string::npos);
    CHECK(failed_read.query_.find(odd_routine) == std::string::npos);
    UncalledCatalogUpdater updater;
    oceanbase::share::plugin::ExtensionUpdateRequest update_request;
    ObSEArray<UpdateOperation, 16> input;
    oceanbase::share::schema::ObSessionPrivInfo session_priv;
    ObSEArray<uint64_t, 4> roles;
    uint64_t updated_id = 99;
    bool changed = true;
    int published = OB_SUCCESS;
    std::string diagnostic = "stale";
    CHECK(oceanbase::rootserver::ObPLDDLService::update_routines_extension(
        update_request, input, session_priv, roles, updater, ddl_service,
        updated_id, changed, published, diagnostic) == OB_NOT_INIT);
    CHECK(updated_id == 0 && !changed && published == OB_NOT_INIT && diagnostic.empty());
  }

  // A real, distinct management grammar. New non-reserved keywords must not
  // break existing identifiers or VERSION(), nor turn the command into DML.
  for (const char *sql : {"CREATE EXTENSION text_ops", "CREATE EXTENSION text_ops VERSION '1.0'",
                         "CREATE EXTENSION `Mixed-Case` VERSION '2.0'"}) {
    ObArenaAllocator arena("ExtCommandTest");
    ObParser parser(arena, 0);
    ParseResult parsed{};
    CHECK(parser.parse(ObString::make_string(sql), parsed) == OB_SUCCESS);
    auto *node = parsed.result_tree_->children_[0];
    CHECK(node->type_ == T_CREATE_EXTENSION && node->num_child_ == 2);
    CHECK(node->children_[0]->str_len_ > 0);
    CHECK(!ObSQLUtils::cause_implicit_commit(parsed));
    CHECK(ObSQLUtils::is_mysql_ps_not_support_stmt(parsed));
    stmt::StmtType type = stmt::T_NONE;
    CHECK(ObResolverUtils::resolve_stmt_type(parsed, type) == OB_SUCCESS);
    CHECK(type == stmt::T_CREATE_EXTENSION);
    CHECK(ObStmt::is_ddl_stmt(type, false) && ObStmt::is_write_stmt(type, false));
    CHECK(!CreateExtensionStmt().cause_implicit_commit());

    ObResolverParams command_params;
    CreateExtensionResolver resolver(command_params);
    CHECK(resolver.resolve(*node) == OB_NOT_INIT);
    command_params.allocator_ = &arena;
    command_params.session_info_ = session.get();
    command_params.disable_privilege_check_ = true;
    CHECK(resolver.resolve(*node) == OB_ERR_NO_PRIVILEGE);
    command_params.disable_privilege_check_ = false;
    command_params.is_prepare_protocol_ = true;
    CHECK(resolver.resolve(*node) == OB_NOT_SUPPORTED);
    command_params.is_prepare_protocol_ = false;
    CHECK(resolver.resolve(*node) == OB_ERR_NO_DB_SELECTED);
    CHECK(resolver.get_basic_stmt() == nullptr);
  }
  for (const char *sql : {"SELECT VERSION()", "SELECT extension()", "SELECT db.version(), db.extension()",
                         "SELECT @@version", "SELECT VERSION() AS extension",
                         "SELECT extension, version FROM t",
                         "CREATE TABLE extension(version INT)"}) {
    ObArenaAllocator arena("ExtCommandTest");
    ObParser parser(arena, 0);
    ParseResult parsed{};
    const int parse_status = parser.parse(ObString::make_string(sql), parsed);
    if (parse_status != OB_SUCCESS) std::cerr << "compatibility SQL: " << sql << ", status=" << parse_status << std::endl;
    CHECK(parse_status == OB_SUCCESS);
    CHECK(parsed.result_tree_->children_[0]->type_ != T_CREATE_EXTENSION);
  }
  for (const char *sql : {"DROP EXTENSION text_ops", "DROP EXTENSION text_ops RESTRICT",
                         "DROP EXTENSION `Mixed-Case` CASCADE"}) {
    ObArenaAllocator arena("ExtDropParse");
    ObParser parser(arena, 0);
    ParseResult parsed{};
    CHECK(parser.parse(ObString::make_string(sql), parsed) == OB_SUCCESS);
    auto *node = parsed.result_tree_->children_[0];
    CHECK(node->type_ == T_DROP_EXTENSION && node->num_child_ == 2);
    CHECK(node->children_[0]->str_len_ > 0 && node->children_[1]->type_ == T_INT);
    CHECK(node->children_[1]->value_ == (std::string(sql).find("CASCADE") != std::string::npos ? 1 : 0));
    CHECK(!ObSQLUtils::cause_implicit_commit(parsed));
    CHECK(ObSQLUtils::is_mysql_ps_not_support_stmt(parsed));
    stmt::StmtType type = stmt::T_NONE;
    CHECK(ObResolverUtils::resolve_stmt_type(parsed, type) == OB_SUCCESS);
    CHECK(type == stmt::T_DROP_EXTENSION);
    CHECK(ObStmt::is_ddl_stmt(type, false) && ObStmt::is_write_stmt(type, false));
    CHECK(!DropExtensionStmt().cause_implicit_commit());
    ObResolverParams params;
    DropExtensionResolver resolver(params);
    CHECK(resolver.resolve(*node) == OB_NOT_INIT);
    params.allocator_ = &arena;
    params.session_info_ = session.get();
    params.disable_privilege_check_ = true;
    CHECK(resolver.resolve(*node) == OB_ERR_NO_PRIVILEGE);
    params.disable_privilege_check_ = false;
    params.is_prepare_protocol_ = true;
    CHECK(resolver.resolve(*node) == OB_NOT_SUPPORTED);
    params.is_prepare_protocol_ = false;
    CHECK(resolver.resolve(*node) == OB_ERR_NO_DB_SELECTED);
    CHECK(resolver.get_basic_stmt() == nullptr);
  }
  for (const char *sql : {"ALTER EXTENSION text_ops UPDATE", "ALTER EXTENSION text_ops UPDATE TO '1.1'",
                         "ALTER EXTENSION `Mixed-Case` UPDATE TO '2.0'"}) {
    ObArenaAllocator arena("ExtUpdateParse");
    ObParser parser(arena, 0);
    ParseResult parsed{};
    CHECK(parser.parse(ObString::make_string(sql), parsed) == OB_SUCCESS);
    auto *node = parsed.result_tree_->children_[0];
    CHECK(node->type_ == T_ALTER_EXTENSION && node->num_child_ == 2);
    CHECK(node->children_[0]->str_len_ > 0);
    CHECK(!ObSQLUtils::cause_implicit_commit(parsed));
    CHECK(ObSQLUtils::is_mysql_ps_not_support_stmt(parsed));
    stmt::StmtType type = stmt::T_NONE;
    CHECK(ObResolverUtils::resolve_stmt_type(parsed, type) == OB_SUCCESS && type == stmt::T_ALTER_EXTENSION);
    CHECK(ObStmt::is_ddl_stmt(type, false) && ObStmt::is_write_stmt(type, false));
    CHECK(!AlterExtensionStmt().cause_implicit_commit());
    ObResolverParams params;
    AlterExtensionResolver resolver(params);
    CHECK(resolver.resolve(*node) == OB_NOT_INIT);
    params.allocator_ = &arena;
    params.session_info_ = session.get();
    params.disable_privilege_check_ = true;
    CHECK(resolver.resolve(*node) == OB_ERR_NO_PRIVILEGE);
    params.disable_privilege_check_ = false;
    params.is_prepare_protocol_ = true;
    CHECK(resolver.resolve(*node) == OB_NOT_SUPPORTED);
    params.is_prepare_protocol_ = false;
    CHECK(resolver.resolve(*node) == OB_ERR_NO_DB_SELECTED && resolver.get_basic_stmt() == nullptr);
  }
  for (const char *sql : {"CREATE EXTENSION", "CREATE EXTENSION text_ops VERSION",
                         "CREATE EXTENSION text_ops VERSION ?", "CREATE EXTENSION text_ops VERSION '1' VERSION '2'",
                         "DROP EXTENSION", "DROP EXTENSION text_ops VERSION '1'",
                         "DROP EXTENSION text_ops RESTRICT CASCADE", "DROP EXTENSION text_ops, other",
                         "DROP EXTENSION ?", "ALTER EXTENSION text_ops", "ALTER EXTENSION text_ops TO '1.1'",
                         "ALTER EXTENSION text_ops UPDATE TO", "ALTER EXTENSION text_ops UPDATE TO ?",
                         "ALTER EXTENSION text_ops UPDATE VERSION '1.1'", "ALTER EXTENSION text_ops UPDATE TO '1' TO '2'",
                         "ALTER EXTENSION text_ops UPDATE CASCADE"}) {
    ObArenaAllocator arena("ExtCommandTest");
    ObParser parser(arena, 0);
    ParseResult parsed{};
    CHECK(parser.parse(ObString::make_string(sql), parsed) != OB_SUCCESS);
  }
  // Validate privilege extraction, not authorization against a live catalog.
  CreateExtensionStmt command_stmt;
  command_stmt.set_database(ObString::make_string("target_db"), 100);
  oceanbase::share::schema::ObSessionPrivInfo session_priv;
  ObSEArray<oceanbase::share::schema::ObNeedPriv, 4> need_privs;
  CHECK(ObPrivilegeCheck::get_stmt_need_privs(session_priv, &command_stmt, need_privs) == OB_ERR_UNEXPECTED);
  CHECK(need_privs.empty());
  session_priv.user_id_ = 1; // Valid identity, no privileges granted by this fixture.
  CHECK(ObPrivilegeCheck::get_stmt_need_privs(session_priv, &command_stmt, need_privs) == OB_SUCCESS);
  CHECK(need_privs.count() == 1);
  CHECK(need_privs.at(0).db_ == "target_db");
  CHECK(need_privs.at(0).priv_level_ == oceanbase::share::schema::OB_PRIV_DB_LEVEL);
  CHECK(need_privs.at(0).priv_set_ == OB_PRIV_CREATE_ROUTINE);
  DropExtensionStmt drop_stmt;
  need_privs.reset();
  CHECK(ObPrivilegeCheck::get_stmt_need_privs(session_priv, &drop_stmt, need_privs) == OB_INVALID_ARGUMENT);
  drop_stmt.set_database(ObString::make_string("target_db"), 100);
  CHECK(ObPrivilegeCheck::get_stmt_need_privs(session_priv, &drop_stmt, need_privs) == OB_SUCCESS);
  CHECK(need_privs.empty()); // Ownership/member grants are checked under Root's lock, not here.
  AlterExtensionStmt alter_stmt;
  CHECK(ObPrivilegeCheck::get_stmt_need_privs(session_priv, &alter_stmt, need_privs) == OB_INVALID_ARGUMENT);
  alter_stmt.set_database(ObString::make_string("target_db"), 100);
  CHECK(ObPrivilegeCheck::get_stmt_need_privs(session_priv, &alter_stmt, need_privs) == OB_SUCCESS);
  CHECK(need_privs.empty()); // No blanket ALTER/CREATE grant; authenticated Root checks remain mandatory.
  {
    // Execute the real entry's admission check without any server composition.
    ObArenaAllocator arena;
    ObExecContext exec(arena);
    CHECK(AlterExtensionExecutor().execute(exec, alter_stmt) == OB_NOT_INIT);
    CHECK(CreateExtensionExecutor().execute(exec, command_stmt) == OB_NOT_INIT);
    CHECK(DropExtensionExecutor().execute(exec, drop_stmt) == OB_NOT_INIT);
  }

  // Positive command construction using the real parser/factory/session data.
  // This verifies command ownership, NOT database existence or authorization.
  for (bool explicit_version : {false, true}) {
    ObArenaAllocator statement_arena("ExtCommandOwn");
    ObStmtFactory factory(statement_arena);
    ObResolverParams params;
    params.allocator_ = &statement_arena;
    params.stmt_factory_ = &factory;
    params.query_ctx_ = factory.get_query_ctx();
    CHECK(params.query_ctx_ != nullptr);
    params.session_info_ = session.get();
    CHECK(session->set_default_database(ObString::make_string("package_db")) == OB_SUCCESS);
    session->set_database_id(100);
    CreateExtensionResolver resolver(params);
    {
      ObArenaAllocator parser_arena("ExtCommandParse");
      ObParser parser(parser_arena, 0);
      ParseResult parsed{};
      const char *sql = explicit_version ? "CREATE EXTENSION pkg VERSION '1.1'" : "CREATE EXTENSION pkg";
      CHECK(parser.parse(ObString::make_string(sql), parsed) == OB_SUCCESS);
      CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
      auto *statement = dynamic_cast<CreateExtensionStmt *>(resolver.get_basic_stmt());
      CHECK(statement != nullptr && statement->name().ptr() != parsed.result_tree_->children_[0]->children_[0]->str_value_);
    }
    CHECK(session->set_default_database(ObString::make_string("changed_db")) == OB_SUCCESS);
    session->set_database_id(200);
    auto *statement = dynamic_cast<CreateExtensionStmt *>(resolver.get_basic_stmt());
    CHECK(statement != nullptr && statement->name() == "pkg");
    CHECK(statement->version() == (explicit_version ? "1.1" : ""));
    CHECK(statement->database_name() == "package_db" && statement->database_id() == 100);
  }
  for (bool explicit_version : {false, true}) {
    ObArenaAllocator statement_arena("ExtUpdateOwn");
    ObStmtFactory factory(statement_arena);
    ObResolverParams params;
    params.allocator_ = &statement_arena;
    params.stmt_factory_ = &factory;
    params.query_ctx_ = factory.get_query_ctx();
    params.session_info_ = session.get();
    CHECK(session->set_default_database(ObString::make_string("package_db")) == OB_SUCCESS);
    session->set_database_id(100);
    AlterExtensionResolver resolver(params);
    {
      ObArenaAllocator parser_arena("ExtUpdateParse");
      ObParser parser(parser_arena, 0);
      ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(explicit_version ?
          "ALTER EXTENSION pkg UPDATE TO '1.1'" : "ALTER EXTENSION pkg UPDATE"), parsed) == OB_SUCCESS);
      CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
      auto *statement = dynamic_cast<AlterExtensionStmt *>(resolver.get_basic_stmt());
      CHECK(statement && statement->name().ptr() != parsed.result_tree_->children_[0]->children_[0]->str_value_);
    }
    CHECK(session->set_default_database(ObString::make_string("changed_db")) == OB_SUCCESS);
    session->set_database_id(200);
    auto *statement = dynamic_cast<AlterExtensionStmt *>(resolver.get_basic_stmt());
    CHECK(statement && statement->name() == "pkg" && statement->version() == (explicit_version ? "1.1" : ""));
    CHECK(statement->database_name() == "package_db" && statement->database_id() == 100);
    ParseNode invalid{};
    CHECK(resolver.resolve(invalid) == OB_INVALID_ARGUMENT && resolver.get_basic_stmt() == nullptr);
  }
  for (bool cascade : {false, true}) {
    ObArenaAllocator statement_arena("ExtDropOwn");
    ObStmtFactory factory(statement_arena);
    ObResolverParams params;
    params.allocator_ = &statement_arena;
    params.stmt_factory_ = &factory;
    params.query_ctx_ = factory.get_query_ctx();
    CHECK(params.query_ctx_ != nullptr);
    params.session_info_ = session.get();
    CHECK(session->set_default_database(ObString::make_string("package_db")) == OB_SUCCESS);
    session->set_database_id(100);
    DropExtensionResolver resolver(params);
    {
      ObArenaAllocator parser_arena("ExtDropParse");
      ObParser parser(parser_arena, 0);
      ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(cascade ? "DROP EXTENSION pkg CASCADE" : "DROP EXTENSION pkg"), parsed) == OB_SUCCESS);
      CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
      auto *statement = dynamic_cast<DropExtensionStmt *>(resolver.get_basic_stmt());
      CHECK(statement != nullptr && statement->name().ptr() != parsed.result_tree_->children_[0]->children_[0]->str_value_);
    }
    CHECK(session->set_default_database(ObString::make_string("changed_db")) == OB_SUCCESS);
    session->set_database_id(200);
    auto *statement = dynamic_cast<DropExtensionStmt *>(resolver.get_basic_stmt());
    CHECK(statement != nullptr && statement->name() == "pkg" && statement->cascade() == cascade);
    CHECK(statement->database_name() == "package_db" && statement->database_id() == 100);
  }
}
