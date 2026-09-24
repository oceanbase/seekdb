/*
 * Copyright (c) 2026 OceanBase.
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once
#include "routine_overlay_guard_fixture.h"
#include "sql/engine/expr/ob_expr_udf.h"
#include "sql/resolver/dml/ob_select_resolver.h"
#include "sql/resolver/ddl/ob_create_routine_resolver.h"
#include "sql/resolver/ddl/ob_create_routine_stmt.h"
#include "sql/resolver/ddl/ob_alter_routine_resolver.h"
#include "sql/resolver/ddl/ob_alter_routine_stmt.h"
#include "sql/resolver/ddl/ob_drop_routine_resolver.h"
#include "sql/resolver/ddl/ob_drop_routine_stmt.h"
#include "sql/resolver/dcl/ob_grant_resolver.h"
#include "sql/resolver/dcl/ob_revoke_resolver.h"
#include "sql/engine/cmd/ob_dcl_executor.h"
#include "rootserver/ob_local_management_service.h"
#include "query/command/ob_root_service_serialization.h"
#include <type_traits>
#include "sql/privilege_check/ob_privilege_check.h"
#include "sql/printer/ob_schema_printer.h"
#include "sql/resolver/ddl/native_function_declaration.h"
#include "share/schema/native_routine_signature.h"
#include "share/schema/routine_catalog_savepoint.h"
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "sql/resolver/ddl/extension_script.h"
#include "sql/plan_cache/ob_plan_cache.h"
#include "sql/pl/ob_pl.h"
#include "sql/pl/ob_pl_router.h"
#include "sql/ob_sql.h"
#include "sql/resolver/expr/plugin_expr_type.h"
#include "gis_declarations_fixture.h"
#include "extension_dcl_batch_fixture.h"

// Real SELECT/PL name resolution, UDF codegen, runtime ACL, invocation scope and
// GIS DSO. Native declarations are resolved from SQL, but catalog storage,
// credentials/grants/column rows are supplied, not installed on a server.
// The PL caller is analyzed, not executed as bytecode.
namespace native_routine_call_test {
using namespace oceanbase::share::schema;

struct Input {
  ObString geometry;
  bool null = false;
  int calls = 0;
};
inline thread_local Input *active_input = nullptr;

inline int evaluate_input(const ObExpr &expr, ObEvalCtx &ctx, ObDatum &result)
{
  CHECK(active_input);
  const auto *session = ctx.exec_ctx_.get_my_session();
  CHECK(session && session->get_priv_user_id() == 123 && session->get_database_id() == 101);
  ++active_input->calls;
  if (active_input->null || (expr.is_batch_result() && ctx.get_batch_idx() == 2)) result.set_null();
  else result.set_string(active_input->geometry);
  return OB_SUCCESS;
}

inline void run(GisProvider &provider, bool definer, bool batch, const std::string &package_root)
{
  constexpr int rows = 6;
  ObArenaAllocator arena;
  auto session = std::make_unique<ObSQLSessionInfo>();
  CHECK(session->test_init(1, 1, &arena) == OB_SUCCESS);
  CHECK(session->load_default_sys_variable(false, false) == OB_SUCCESS);
  CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
  CHECK(session->set_default_database(ObString::make_string("caller_db")) == OB_SUCCESS);
  session->set_database_id(101); session->set_priv_user_id(123);
  session->set_user_priv_set(OB_PRIV_INSERT); session->set_db_priv_set(OB_PRIV_SELECT);
  auto manager = std::make_unique<ObSchemaMgr>();
  auto service = std::make_unique<MockSchemaService>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSimpleServerRuntimeSchema runtime;
  runtime.set_schema_version(42);
  runtime.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  runtime.set_status(SERVER_RUNTIME_STATUS_NORMAL);
  CHECK(runtime.set_runtime_name(ObString::make_string("native_fixture")) == OB_SUCCESS);
  CHECK(manager->add_runtime_schema(runtime) == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  ObDatabaseSchema database, caller_database, empty_database;
  database.set_database_id(100); database.set_schema_version(42);
  caller_database.set_database_id(101); caller_database.set_schema_version(42);
  empty_database.set_database_id(102); empty_database.set_schema_version(42);
  CHECK(database.set_database_name("native_db") == OB_SUCCESS);
  CHECK(caller_database.set_database_name("caller_db") == OB_SUCCESS);
  CHECK(empty_database.set_database_name("empty_db") == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, caller_database) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, empty_database) == OB_SUCCESS);
  for (const auto *db : {&database, &caller_database, &empty_database}) {
    ObSimpleDatabaseSchema simple;
    simple.set_database_id(db->get_database_id()); simple.set_schema_version(42);
    CHECK(simple.set_database_name(db->get_database_name_str()) == OB_SUCCESS);
    CHECK(manager->add_database(simple) == OB_SUCCESS);
  }
  ObUserInfo caller, owner;
  for (auto *user : {&caller, &owner}) {
    const bool is_owner = user == &owner;
    user->set_user_id(is_owner ? 124 : 123); user->set_schema_version(42);
    CHECK(user->set_user_name(is_owner ? "owner" : "caller") == OB_SUCCESS);
    CHECK(user->set_host("localhost") == OB_SUCCESS);
    if (is_owner) user->set_priv_set(OB_PRIV_SELECT);
    ObSimpleUserSchema simple;
    simple.set_user_id(user->get_user_id()); simple.set_schema_version(42);
    CHECK(simple.set_user_name(user->get_user_name_str()) == OB_SUCCESS);
    CHECK(simple.set_host(user->get_host_name_str()) == OB_SUCCESS);
    CHECK(manager->add_user(simple) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_user(guard, *user) == OB_SUCCESS);
  }
  ObRoutineInfo routine;
  {
    ObSchemaChecker checker; CHECK(checker.init(guard) == OB_SUCCESS);
    ObStmtFactory statements(arena);
    ObRawExprFactory expressions(arena);
    ObMySQLProxy proxy;
    ObResolverParams params;
    params.allocator_ = &arena; params.session_info_ = session.get(); params.schema_checker_ = &checker;
    params.stmt_factory_ = &statements; params.expr_factory_ = &expressions;
    params.query_ctx_ = statements.get_query_ctx(); params.sql_proxy_ = &proxy;
    const std::string prefix = "CREATE FUNCTION native_db.area_alias(geometry GEOMETRY) RETURNS ";
    const std::string suffix = std::string(" SQL SECURITY ") + (definer ? "DEFINER" : "INVOKER") +
        " AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_area' LANGUAGE C";
    const auto create = [&](const std::string &text, int expected, bool copy) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
      ObCreateFunctionResolver resolver(params);
      const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
      std::cout << "native CREATE resolve: status=" << status << std::endl;
      CHECK(status == expected);
      CHECK(session->get_database_id() == 101 && session->get_database_name() == ObString::make_string("caller_db"));
      if (status == OB_SUCCESS) {
        auto *statement = dynamic_cast<ObCreateRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
        const auto &info = statement->get_routine_arg().routine_info_;
        CHECK(info.is_native() && info.get_param_count() == 1 && info.get_owner_id() == 124);
        CHECK(info.get_route_sql().empty()); // No compiled PL forwarding body.
        ObSessionPrivInfo privilege;
        CHECK(session->get_session_priv_info(privilege) == OB_SUCCESS);
        ObSEArray<ObNeedPriv, 2> needs;
        CHECK(ObPrivilegeCheck::get_stmt_need_privs(privilege, statement, needs) == OB_SUCCESS);
        CHECK(needs.count() == 2 && needs.at(0).priv_set_ == OB_PRIV_CREATE_ROUTINE &&
              needs.at(1).priv_level_ == OB_PRIV_USER_LEVEL && needs.at(1).priv_set_ == OB_PRIV_SUPER);
        if (copy) CHECK(routine.assign(info) == OB_SUCCESS);
      }
    };
    create(prefix + "DOUBLE" + suffix, OB_ERR_NO_PRIVILEGE, false);
    CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 124) == OB_SUCCESS);
    session->set_priv_user_id(124); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    create(prefix + "BIGINT" + suffix, OB_INVALID_ARGUMENT, false);
    create(prefix + "DOUBLE" + suffix, OB_SUCCESS, true);
    CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_INSERT);
  }
  CHECK(routine.get_database_id() == 100 && routine.get_priv_user() == ObString::make_string("owner@localhost"));
  routine.set_routine_id(310001); routine.set_schema_version(42); // Controlled catalog allocation.
  for (int64_t i = 0; i < routine.get_routine_params().count(); ++i) {
    auto *parameter = routine.get_routine_params().at(i);
    parameter->set_routine_id(310001); parameter->set_schema_version(42);
  }
  seekdb_plugin_sql_binding_v1_t binding{};
  std::vector<std::string> argument_types;
  CHECK(PluginFunctionExpr::resolve_native_binding(routine, binding, argument_types) == OB_SUCCESS);
  CHECK(argument_types.size() == 1 && argument_types[0] == "core.type.geometry");
  ObRoutineInfo invalid;
  CHECK(invalid.assign(routine) == OB_SUCCESS);
  invalid.get_routine_params().at(0)->set_param_type(ObIntType);
  CHECK(PluginFunctionExpr::resolve_native_binding(invalid, binding, argument_types) == OB_INVALID_ARGUMENT);
  CHECK(binding.struct_size == 0 && argument_types.empty());
  CHECK(invalid.assign(routine) == OB_SUCCESS);
  CHECK(invalid.set_native_binding(ObString::make_string("org.seekdb.other"),
      ObString::make_string("org.seekdb.gis.function.st_area"), 1) == OB_SUCCESS);
  CHECK(PluginFunctionExpr::resolve_native_binding(invalid, binding, argument_types) != OB_SUCCESS);
  CHECK(binding.struct_size == 0 && argument_types.empty());
  auto native_privileges = std::make_shared<RoutinePrivilegeOverlay>();
  auto overlay = std::make_shared<RoutineSchemaOverlay>(native_privileges);
  CHECK(overlay->stage(routine) == OB_SUCCESS);
  CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  if (!definer && !batch) verify_extension_dcl_batch(routine);
  {
    // Exercise the same printer as SHOW CREATE FUNCTION, including reparsing
    // persisted source with the PL parser. Merely retaining AS text is not
    // sufficient if the printer truncates or rewrites the native declaration.
    ObRoutineInfo original_source;
    CHECK(original_source.assign(routine) == OB_SUCCESS);
    // Simulate diagnostic text retained across a rebind. The dump must use
    // the persisted module/implementation, not restore these obsolete IDs.
    CHECK(original_source.set_routine_body(ObString::make_string(
        "FUNCTION area_alias(geometry GEOMETRY) RETURNS DOUBLE "
        "AS 'org.example.old', 'org.example.old.area' LANGUAGE C")) == OB_SUCCESS);
    CHECK(overlay->stage(original_source) == OB_SUCCESS);
    ObSchemaPrinter printer(guard);
    ObExecEnv environment; CHECK(environment.init(routine.get_exec_env()) == OB_SUCCESS);
    char text[8192]{}; int64_t position = 0;
    CHECK(printer.print_routine_definition(routine.get_routine_id(), environment,
        text, sizeof(text), position, TZ_INFO(session.get())) == OB_SUCCESS);
    ObParser parser(arena, environment.get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString(position, text), parsed) == OB_SUCCESS);
    const auto *create = parsed.result_tree_->children_[0];
    CHECK(create && create->type_ == T_SF_CREATE);
    NativeFunctionDeclaration declaration;
    CHECK(NativeFunctionDeclaration::read(create->children_[5], declaration) == OB_SUCCESS);
    CHECK(declaration.module_id_ == "org.seekdb.gis" &&
          declaration.implementation_id_ == "org.seekdb.gis.function.st_area");
    CHECK(original_source.set_native_binding(ObString(), ObString(), 0) == OB_SUCCESS);
    CHECK(overlay->stage(original_source) == OB_SUCCESS);
    position = 0;
    CHECK(printer.print_routine_definition(routine.get_routine_id(), environment,
        text, sizeof(text), position, TZ_INFO(session.get())) == OB_INVALID_DATA);
    CHECK(overlay->stage(routine) == OB_SUCCESS);
    std::cout << "PASS: native SHOW CREATE declaration roundtrip" << std::endl;
  }
  ObRawExprFactory factory(arena);
  ObColumnRefRawExpr *column = nullptr;
  CHECK(factory.create_raw_expr(T_REF_COLUMN, column) == OB_SUCCESS);
  ObColumnSchemaV2 schema;
  schema.set_table_id(555); schema.set_column_id(16); schema.set_data_type(ObGeometryType);
  schema.set_collation_type(CS_TYPE_BINARY);
  CHECK(schema.set_column_name("geometry") == OB_SUCCESS);
  CHECK(ObRawExprUtils::init_column_expr(schema, nullptr, *column) == OB_SUCCESS);
  column->set_ref_id(555, 16);
  ObUDFRawExpr *raw = nullptr;
  CHECK(factory.create_raw_expr(T_FUN_UDF, raw) == OB_SUCCESS);
  raw->set_udf_id(310001); raw->set_pkg_id(OB_INVALID_ID); raw->set_udf_schema_version(42);
  ObExprResType return_type;
  return_type.set_double();
  raw->set_result_type(return_type);
  raw->set_func_name(ObString::make_string("area_alias"));
  CHECK(raw->init_param_exprs(1) == OB_SUCCESS);
  CHECK(raw->add_param_expr(column) == OB_SUCCESS);
  CHECK(raw->get_params_type().push_back(column->get_result_type()) == OB_SUCCESS);
  CHECK(raw->add_param_desc(ObUDFParamDesc()) == OB_SUCCESS);
  CHECK(raw->formalize(session.get()) == OB_SUCCESS);
  ObExprCGCtx cg(arena, session.get(), &guard);
  ObExprUDF udf(arena);
  ObExpr stale;
  raw->set_udf_schema_version(41);
  CHECK(udf.cg_expr(cg, *raw, stale) == OB_SCHEMA_EAGAIN);
  raw->set_udf_schema_version(42);
  ObExprResType wrong_type;
  wrong_type.set_int(); raw->set_result_type(wrong_type);
  CHECK(udf.cg_expr(cg, *raw, stale) == OB_INVALID_DATA);
  raw->set_result_type(return_type);
  for (const char *id : {"org.example.geometry", "org.example.int64", "core.type.geometry.extra"}) {
    PluginExprType opaque;
    opaque.logical_id_ = ObString::make_string(id);
    opaque.physical_type_ = ObGeometryType; opaque.catalog_epoch_ = 1;
    CHECK(column->set_plugin_type(opaque) == OB_SUCCESS);
    CHECK(udf.cg_expr(cg, *raw, stale) == OB_NOT_SUPPORTED);
  }
  {
    PluginExprType stored;
    stored.logical_id_ = ObString::make_string("core.type.geometry");
    stored.physical_type_ = ObGeometryType; stored.stored_ = true;
    stored.sql_name_ = ObString::make_string("stored_geometry");
    stored.owner_ = ObString::make_string("org.example.geometry");
    stored.format_ = ObString::make_string("org.example.geometry.storage"); stored.format_version_ = 1;
    CHECK(column->set_plugin_type(stored) == OB_SUCCESS);
    CHECK(udf.cg_expr(cg, *raw, stale) == OB_NOT_SUPPORTED);
  }
  column->clear_plugin_type();
  ObMySQLProxy proxy;
  auto cache = std::make_unique<ObPlanCache>();
  auto engine = std::make_unique<oceanbase::pl::ObPL>();
  ObSql runtime_sql;
  LobService lob;
  ObSqlCtx sql; sql.session_info_ = session.get(); sql.schema_guard_ = &guard;
  ObExecContext execution(arena);
  execution.set_my_session(session.get()); execution.set_sql_ctx(&sql);
  execution.set_lob_read_service(&lob);
  CHECK(execution.create_physical_plan_ctx() == OB_SUCCESS);
  ObSQLSessionInfo::ExecCtxSessionRegister registration(*session, &execution);
  // Resolve actual SQL through the normal catalog/PL name-resolution route,
  // not by manufacturing a typed UDF node. Only schema storage is supplied.
  execution.set_sql_proxy(&proxy); execution.set_plan_cache(cache.get());
  execution.set_pl_engine(engine.get()); execution.set_pl_sql_runtime(&runtime_sql);
  ObSchemaChecker checker; CHECK(checker.init(guard) == OB_SUCCESS);
  ObStmtFactory statements(arena);
  ObResolverParams resolver_params;
  resolver_params.allocator_ = &arena; resolver_params.expr_factory_ = &factory;
  resolver_params.stmt_factory_ = &statements; resolver_params.query_ctx_ = statements.get_query_ctx();
  resolver_params.session_info_ = session.get(); resolver_params.schema_checker_ = &checker;
  resolver_params.sql_proxy_ = &proxy; resolver_params.plan_cache_ = cache.get();
  resolver_params.pl_engine_ = engine.get(); resolver_params.pl_sql_runtime_ = &runtime_sql;
  // Helpers such as POINT/ST_GeomFromText are ordinary database routines too.
  // Loading GIS alone must never make these visible through global SQL names.
  CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 124) == OB_SUCCESS);
  session->set_priv_user_id(124); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
  stage_gis_catalog(package_root, resolver_params, guard, *manager, *overlay, 123);
  CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
  session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_INSERT);
  {
    // Root's sequential SQL-package resolver must keep the native binding in
    // its owned wire operation. This does not simulate a committed install.
    using namespace oceanbase::share::plugin;
    ExtensionPackageSource source;
    source.name_ = "gis_binding_test"; source.version_ = "1.0";
    source.native_module_ = "org.seekdb.gis";
    source.scripts_.push_back({"", "1.0",
        "CREATE FUNCTION packaged_area(geometry GEOMETRY) RETURNS DOUBLE SQL SECURITY INVOKER "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_area' LANGUAGE C;"
        "GRANT EXECUTE ON FUNCTION packaged_area(GEOMETRY) TO 'caller'@'localhost';"
        "REVOKE EXECUTE ON FUNCTION packaged_area(GEOMETRY) FROM 'caller'@'localhost';"});
    ExtensionScript script; std::string error;
    CHECK(script.load_source(source, session->get_sql_mode(), error) == OB_SUCCESS);
    CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 124) == OB_SUCCESS);
    session->set_priv_user_id(124); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    CHECK(session->set_default_database(ObString::make_string("native_db")) == OB_SUCCESS);
    session->set_database_id(100);
    ExtensionInstallSpec spec;
    spec.tenant_id_ = 1; spec.database_id_ = 100; spec.owner_id_ = 124;
    spec.name_ = source.name_; spec.version_ = source.version_; spec.native_module_id_ = source.native_module_;
    int update_index = 0;
    for (const char *text : {
        "GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'caller'@'localhost'",
        "REVOKE EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) FROM 'caller'@'localhost'",
        "GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'caller'@'localhost', 'owner'@'localhost' WITH GRANT OPTION",
        "REVOKE GRANT OPTION FOR EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) FROM 'owner'@'localhost', 'caller'@'localhost' CASCADE"}) {
      auto dcl_source = source;
      dcl_source.scripts_ = {{"", "1.0", text}};
      ExtensionScript dcl_script;
      CHECK(dcl_script.load_source(dcl_source, session->get_sql_mode(), error) == OB_SUCCESS);
      ExtensionRoutineScriptResolver dcl_sequence(dcl_script, spec, resolver_params, sql);
      CHECK(dcl_sequence.preflight_install(spec, error) == OB_SUCCESS);
      const ExtensionRoutineUpdateOperation *sequential = nullptr;
      const int sequential_status = dcl_sequence.resolve(0, guard, sequential, error);
      std::cout << "package native DCL sequence: status=" << sequential_status << " error=" << error << std::endl;
      CHECK(sequential_status == OB_SUCCESS && sequential);
      ExtensionUpdatePlan update_plan;
      const std::string update_name = "dcl_update_" + std::to_string(update_index++);
      CHECK(update_plan.load(package_root, 1, 100, update_name, {91,124,"1","org.seekdb.gis"},
          "2", session->get_sql_mode(), error) == OB_SUCCESS);
      ExtensionRoutineScriptResolver update_sequence(update_plan, resolver_params, sql);
      CHECK(update_sequence.preflight(update_plan.request(), error) == OB_SUCCESS);
      const ExtensionRoutineUpdateOperation *updated = nullptr;
      CHECK(update_sequence.resolve(0, guard, updated, error) == OB_SUCCESS && updated);
      CHECK(updated->kind_ == sequential->kind_);
      ExtensionRoutineUpdateBatch dcl_output;
      const int dcl_status = ExtensionRoutineResolver::resolve_statement(dcl_script, 0, resolver_params, sql,
          100, dcl_output, error);
      std::cout << "package native DCL resolve: status=" << dcl_status << " error=" << error << std::endl;
      CHECK(dcl_status == OB_SUCCESS && dcl_output.operations().count() == 1);
      dcl_script.reset();
      const auto &dcl_op = dcl_output.operations().at(0);
      CHECK(sequential->kind_ == dcl_op.kind_ && !sequential->is_schema_change());
      const auto &target = dcl_op.grant_arg_ ? dcl_op.grant_arg_->native_target_ : dcl_op.revoke_arg_->native_target_;
      CHECK(target.resolved_ && target.actor_id_ == 124 && target.routine_.get_routine_id() == 310001);
      CHECK(target.routine_.get_schema_version() == 42 && target.signature_qualified_);
      const bool multiple = std::strstr(text, "'owner'") != nullptr;
      if (dcl_op.grant_arg_) {
        CHECK(dcl_op.grant_arg_->hosts_.count() == (multiple ? 2 : 1));
        CHECK(dcl_op.grant_arg_->users_passwd_.count() == (multiple ? 4 : 2));
        CHECK(dcl_op.grant_arg_->users_passwd_.at(0) == ObString::make_string("caller"));
        CHECK(dcl_op.grant_arg_->users_passwd_.at(1).empty());
        CHECK(!dcl_op.grant_arg_->need_create_user_ && !dcl_op.grant_arg_->is_inner_);
        CHECK(bool(dcl_op.grant_arg_->priv_set_ & OB_PRIV_GRANT) == multiple ||
              (multiple && dcl_op.grant_arg_->option_ == GRANT_OPTION));
      } else {
        CHECK(dcl_op.revoke_arg_ && dcl_op.revoke_arg_->native_grantees_.count() == (multiple ? 2 : 1));
        CHECK(dcl_op.revoke_arg_->native_grantees_.at(0) == 123 && dcl_op.revoke_arg_->user_id_ == OB_INVALID_ID);
        CHECK(dcl_op.revoke_arg_->grant_option_only_ == multiple);
        if (multiple) CHECK(dcl_op.revoke_arg_->revoke_behavior_ == oceanbase::obcall::ObRevokeRoutineArg::REVOKE_CASCADE);
      }
    }
    for (const auto &test : {
        std::make_pair("GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'missing_dcl_fixture'@'localhost'", OB_USER_NOT_EXIST),
        std::make_pair("GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'caller'@'localhost' IDENTIFIED BY 'fixture_password'", OB_NOT_SUPPORTED),
        std::make_pair("GRANT EXECUTE ON FUNCTION native_db.area_alias(GEOMETRY) TO 'caller'@'localhost' IDENTIFIED BY ''", OB_NOT_SUPPORTED),
        std::make_pair("GRANT EXECUTE ON FUNCTION caller_db.point(DOUBLE,DOUBLE) TO 'caller'@'localhost'", OB_ERR_BAD_DATABASE)}) {
      auto dcl_source = source; dcl_source.scripts_ = {{"", "1.0", test.first}};
      ExtensionScript dcl_script;
      CHECK(dcl_script.load_source(dcl_source, session->get_sql_mode(), error) == OB_SUCCESS);
      ExtensionRoutineUpdateBatch output;
      const int status = ExtensionRoutineResolver::resolve_statement(dcl_script, 0, resolver_params, sql, 100, output, error);
      std::cout << "package native DCL rejection: status=" << status << " expected=" << test.second << std::endl;
      CHECK(status == test.second && output.operations().empty());
    }
    {
      session->set_user_priv_set(OB_PRIV_CREATE_ROUTINE);
      ExtensionRoutineScriptResolver restricted(script, spec, resolver_params, sql);
      CHECK(restricted.preflight_install(spec, error) == OB_ERR_NO_PRIVILEGE);
      auto invoker_source = source;
      invoker_source.requires_superuser_ = false;
      auto invoker_spec = spec;
      invoker_spec.requires_superuser_ = false;
      ExtensionScript invoker_script;
      CHECK(invoker_script.load_source(invoker_source, session->get_sql_mode(), error) == OB_SUCCESS);
      ExtensionRoutineScriptResolver denied(invoker_script, invoker_spec, resolver_params, sql);
      CHECK(denied.preflight_install(invoker_spec, error) == OB_SUCCESS);
      const ExtensionRoutineUpdateOperation *operation = nullptr;
      CHECK(denied.resolve(0, guard, operation, error) == OB_ERR_NO_PRIVILEGE);
      CHECK(operation == nullptr && session->get_database_id() == 100 && session->get_priv_user_id() == 124);
      // A failed package statement cannot be retried with a newly elevated
      // identity inside the same sequence.
      session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
      CHECK(denied.resolve(0, guard, operation, error) == OB_STATE_NOT_MATCH && operation == nullptr);
    }
    ExtensionRoutineScriptResolver sequence(script, spec, resolver_params, sql);
    CHECK(sequence.preflight_install(spec, error) == OB_SUCCESS);
    CHECK(sequence.validate_view(guard, error) == OB_SUCCESS);
    const ExtensionRoutineUpdateOperation *operation = nullptr;
    const int status = sequence.resolve(0, guard, operation, error);
    std::cout << "native package resolve: status=" << status << " error=" << error << std::endl;
    CHECK(status == OB_SUCCESS && operation && operation->create_arg_);
    const auto &packaged = operation->create_arg_->routine_info_;
    CHECK(packaged.is_native() && packaged.is_native_binding_valid());
    CHECK(packaged.get_native_module_id() == routine.get_native_module_id() &&
          packaged.get_native_implementation_id() == routine.get_native_implementation_id());
    CHECK(packaged.get_database_id() == 100 && packaged.get_owner_id() == 124 && packaged.get_route_sql().empty());
    CHECK(packaged.get_routine_name() == ObString::make_string("packaged_area"));
    ObRoutineInfo installed;
    CHECK(installed.assign(packaged) == OB_SUCCESS);
    installed.set_routine_id(310002); installed.set_schema_version(42);
    for (int64_t i = 0; i < installed.get_routine_params().count(); ++i) {
      installed.get_routine_params().at(i)->set_routine_id(310002);
      installed.get_routine_params().at(i)->set_schema_version(42);
    }
    CHECK(overlay->stage(installed) == OB_SUCCESS); // Controlled storage, not a commit.
    // Subsequent DCL must bind the CREATE's private identity, not a global
    // implementation or an earlier statement's temporary resolver allocation.
    // This exercises sequential semantic resolution, not ACL SQL execution.
    const ExtensionRoutineUpdateOperation *grant_operation = nullptr, *revoke_operation = nullptr;
    CHECK(sequence.resolve(1, guard, grant_operation, error) == OB_SUCCESS && grant_operation);
    CHECK(sequence.resolve(2, guard, revoke_operation, error) == OB_SUCCESS && revoke_operation);
    CHECK(grant_operation->kind_ == ExtensionRoutineUpdateOperation::Kind::GRANT &&
          revoke_operation->kind_ == ExtensionRoutineUpdateOperation::Kind::REVOKE);
    script.reset(); // All three operations must own their source strings.
    CHECK(packaged.get_routine_name() == ObString::make_string("packaged_area"));
    CHECK(grant_operation->grant_arg_->native_target_.routine_.get_routine_id() == 310002 &&
          revoke_operation->revoke_arg_->native_target_.routine_.get_routine_id() == 310002);
    CHECK(grant_operation->grant_arg_->native_target_.routine_.get_schema_version() == 42 &&
          revoke_operation->revoke_arg_->native_target_.routine_.get_schema_version() == 42);
    CHECK(grant_operation->grant_arg_->users_passwd_.at(0) == ObString::make_string("caller") &&
          revoke_operation->revoke_arg_->native_grantees_.at(0) == 123);
    CHECK(session->get_database_id() == 100 && session->get_priv_user_id() == 124);
    CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_INSERT);
    CHECK(session->set_default_database(ObString::make_string("caller_db")) == OB_SUCCESS);
    session->set_database_id(101);
  }
  const auto resolve = [&](const char *text, int expected, uint64_t identity = 310001) -> ObUDFRawExpr * {
    ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
    CHECK(parser.parse(ObString::make_string(text), parsed) == OB_SUCCESS);
    ObSelectResolver resolver(resolver_params);
    const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
    std::cout << "native SQL resolve: status=" << status << " sql=" << text << std::endl;
    CHECK(status == expected);
    if (status != OB_SUCCESS) return nullptr;
    auto *statement = resolver.get_select_stmt(); CHECK(statement && statement->get_select_item_size() == 1);
    auto *udf = dynamic_cast<ObUDFRawExpr *>(statement->get_select_item(0).expr_);
    CHECK(udf && udf->get_udf_id() == identity && udf->get_udf_version() == 42);
    ObSEArray<ObSchemaObjVersion, 2> dependencies;
    CHECK(udf->get_schema_object_version(guard, dependencies) == OB_SUCCESS);
    CHECK(dependencies.count() == 1 && dependencies.at(0).object_id_ == identity &&
          dependencies.at(0).object_type_ == DEPENDENCY_FUNCTION);
    return udf;
  };
  raw = resolve("SELECT native_db.area_alias(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'))", OB_SUCCESS);
  auto *packaged_raw = resolve(
      "SELECT native_db.packaged_area(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'))", OB_SUCCESS, 310002);
  resolve("SELECT packaged_area(NULL)", OB_ERR_FUNCTION_UNKNOWN);
  resolve("SELECT area_alias(NULL)", OB_ERR_FUNCTION_UNKNOWN); // Not installed in caller_db.
  CHECK(session->set_default_database(ObString::make_string("native_db")) == OB_SUCCESS);
  session->set_database_id(100);
  resolve("SELECT area_alias(NULL)", OB_SUCCESS);
  resolve("SELECT area_alias()", OB_ERR_SP_WRONG_ARG_NUM);
  CHECK(session->set_default_database(ObString::make_string("caller_db")) == OB_SUCCESS);
  session->set_database_id(101);
  if (!definer && !batch) {
    oceanbase::obcall::ObCreateRoutineArg argument;
    CHECK(argument.routine_info_.assign(routine) == OB_SUCCESS);
    argument.routine_info_.set_routine_id(OB_INVALID_ID);
    argument.routine_info_.set_schema_version(OB_INVALID_VERSION);
    for (int64_t i = 0; i < argument.routine_info_.get_routine_params().count(); ++i) {
      auto *parameter = argument.routine_info_.get_routine_params().at(i);
      parameter->set_routine_id(OB_INVALID_ID);
      parameter->set_schema_version(OB_INVALID_VERSION);
    }
    CHECK(argument.routine_info_.set_native_binding(ObString(), ObString(), 0) == OB_SUCCESS);
    CHECK(argument.routine_info_.set_routine_name(ObString::make_string("native_wrapper")) == OB_SUCCESS);
    CHECK(argument.routine_info_.set_routine_body(ObString::make_string(
        "RETURN native_db.area_alias(ST_GeomFromText('POLYGON((0 0,4 0,4 3,0 3,0 0))'))")) == OB_SUCCESS);
    oceanbase::pl::ObPLRouter router(argument.routine_info_, *session, guard, proxy,
                                     &runtime_sql, engine.get(), true);
    ObString route;
    const int status = router.analyze(route, argument.dependency_infos_, argument.routine_info_, &argument);
    std::cout << "native PL caller resolve: status=" << status << std::endl;
    CHECK(status == OB_SUCCESS && !argument.routine_info_.is_native());
    bool depends = false;
    for (const auto &dependency : argument.dependency_infos_) {
      if (dependency.get_ref_obj_id() == 310001) depends = true;
    }
    CHECK(depends);
  }
  // Also execute the unchanged parsed expression, including nested GIS argument
  // evaluation. The later column variant isolates batching and security checks.
  const auto grant_native = [&](const char *name, bool execute) {
    ObSEArray<const ObRoutineInfo *, 4> family;
    CHECK(guard.get_standalone_function_infos(100, ObString::make_string(name), family) == OB_SUCCESS && !family.empty());
    oceanbase::share::ObPackedObjPriv bits = 0;
    CHECK(oceanbase::share::ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION,
        OBJ_PRIV_ID_EXECUTE, bits) == OB_SUCCESS);
    for (const auto *member : family) {
      CHECK(member && member->is_native());
      CHECK(MockSchemaService::grant_object(*manager, member->get_routine_id(), member->get_owner_id(),
          123, execute ? bits : 0) == OB_SUCCESS);
    }
  };
  grant_native("area_alias", true); grant_native("packaged_area", true);
  if (!definer && !batch) {
    // Real CREATE -> owned schema -> normal SQL argument completion -> GIS DSO.
    // A missing argument must become its default, never PL's NULL placeholder.
    CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 124) == OB_SUCCESS);
    session->set_priv_user_id(124); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    const std::string prefix = "CREATE FUNCTION native_db.default_point(";
    verify_gis_declarations(package_root, resolver_params, *session);
    const std::string suffix = ") RETURNS GEOMETRY DETERMINISTIC NO SQL SQL SECURITY INVOKER "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_point' LANGUAGE C";
    ObRoutineInfo point;
    const auto create_default = [&](const std::string &text, int expected) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
      ObCreateFunctionResolver resolver(resolver_params);
      const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
      std::cout << "native default CREATE: status=" << status << std::endl;
      CHECK(status == expected);
      if (status == OB_SUCCESS) {
        auto *statement = dynamic_cast<ObCreateRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
        CHECK(point.assign(statement->get_routine_arg().routine_info_) == OB_SUCCESS);
      }
    };
    create_default(prefix + "x DOUBLE DEFAULT 3, y DOUBLE" + suffix, OB_INVALID_ARGUMENT);
    create_default(prefix + "x DOUBLE DEFAULT ABS(-3), y DOUBLE DEFAULT 4" + suffix, OB_NOT_SUPPORTED);
    create_default("CREATE FUNCTION native_db.ordinary_default(x DOUBLE DEFAULT 3) RETURNS DOUBLE RETURN x", OB_NOT_SUPPORTED);
    create_default("CREATE FUNCTION native_db.default_area(g GEOMETRY DEFAULT 1) RETURNS DOUBLE "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_area' LANGUAGE C", OB_INVALID_ARGUMENT);
    create_default("CREATE FUNCTION native_db.default_area(g GEOMETRY DEFAULT NULL) RETURNS DOUBLE "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_area' LANGUAGE C", OB_SUCCESS);
    create_default(prefix + "x DOUBLE DEFAULT TRUE, y DOUBLE DEFAULT NULL" + suffix, OB_SUCCESS);
    create_default(prefix + "x DOUBLE DEFAULT -3.5, y DOUBLE DEFAULT 4e0" + suffix, OB_SUCCESS);
    point.set_routine_id(310003); point.set_schema_version(42);
    for (int64_t i = 0; i < point.get_routine_params().count(); ++i) {
      auto *parameter = point.get_routine_params().at(i);
      parameter->set_routine_id(310003); parameter->set_schema_version(42);
    }
    ObRoutineParam *x = nullptr, *y = nullptr;
    CHECK(point.get_routine_param(0, x) == OB_SUCCESS && point.get_routine_param(1, y) == OB_SUCCESS);
    CHECK(x->get_default_value() == ObString::make_string("-3.5"));
    CHECK(y->get_default_value() == ObString::make_string("4e0"));
    // Recheck untrusted wire/catalog defaults even when CREATE was bypassed.
    CHECK(y->set_default_value(ObString()) == OB_SUCCESS);
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types) == OB_INVALID_ARGUMENT);
    CHECK(y->set_default_value(ObString::make_string("4e0")) == OB_SUCCESS);
    CHECK(x->set_default_value(ObString::make_string("caller_function()")) == OB_SUCCESS);
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types) == OB_NOT_SUPPORTED);
    CHECK(x->set_default_value(ObString::make_string("-3.5")) == OB_SUCCESS);
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types) == OB_SUCCESS);
    std::vector<char> wire(point.get_serialize_size()); int64_t position = 0;
    CHECK(point.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
    ObRoutineInfo decoded; position = 0;
    CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS);
    // DDL wire does not carry schema versions: Root assigns them at catalog
    // admission. Supply that controlled step after the wire roundtrip as well.
    decoded.set_schema_version(42);
    for (int64_t i = 0; i < decoded.get_routine_params().count(); ++i)
      decoded.get_routine_params().at(i)->set_schema_version(42);
    CHECK(overlay->stage(decoded) == OB_SUCCESS);
    std::fill(wire.begin(), wire.end(), '\0');
    const ObRoutineInfo *owned_point = nullptr;
    CHECK(guard.get_routine_info(310003, owned_point) == OB_SUCCESS && owned_point);
    ObExecEnv environment; CHECK(environment.init(owned_point->get_exec_env()) == OB_SUCCESS);
    ObSchemaPrinter printer(guard); char printed[8192]{}; position = 0;
    CHECK(printer.print_routine_definition(310003, environment, printed, sizeof(printed), position,
        TZ_INFO(session.get())) == OB_SUCCESS);
    const std::string definition(printed, position);
    CHECK(definition.find("DEFAULT -3.5") != std::string::npos && definition.find("DEFAULT 4e0") != std::string::npos);
    ObParser dump_parser(arena, environment.get_sql_mode()); ParseResult dump{};
    CHECK(dump_parser.parse(ObString(position, printed), dump) == OB_SUCCESS);
    const std::string line_suffix = ") RETURNS GEOMETRY DETERMINISTIC NO SQL SQL SECURITY INVOKER "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_linestring' LANGUAGE C";
    create_default("CREATE FUNCTION native_db.native_line(VARIADIC points GEOMETRY[], x DOUBLE" + line_suffix,
        OB_INVALID_ARGUMENT);
    create_default("CREATE FUNCTION native_db.native_line(x DOUBLE DEFAULT 1, VARIADIC points GEOMETRY[]" + line_suffix,
        OB_INVALID_ARGUMENT);
    create_default("CREATE FUNCTION native_db.native_area(VARIADIC points GEOMETRY[]) RETURNS DOUBLE "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_area' LANGUAGE C", OB_INVALID_ARGUMENT);
    create_default("CREATE FUNCTION native_db.pl_variadic(VARIADIC x DOUBLE[]) RETURNS DOUBLE RETURN 1", OB_NOT_SUPPORTED);
    provider.change_native_epoch_on_expansion_ = true;
    create_default("CREATE FUNCTION native_db.native_line(VARIADIC points GEOMETRY[]" + line_suffix, OB_STATE_NOT_MATCH);
    provider.change_native_epoch_on_expansion_ = false;
    create_default("CREATE FUNCTION native_db.native_line(VARIADIC points GEOMETRY[]" + line_suffix, OB_SUCCESS);
    CHECK(point.get_param_count() == 1 && NativeRoutineSignature::variadic(point));
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types) == OB_SUCCESS);
    CHECK(argument_types.size() == 1 && binding.maximum_arity == 64);
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types, 3) == OB_SUCCESS);
    CHECK(argument_types == std::vector<std::string>(3, "core.type.geometry"));
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types, 0) == OB_ERR_SP_WRONG_ARG_NUM);
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types, 65) == OB_ENTRY_NOT_EXIST);
    CHECK(PluginFunctionExpr::resolve_native_binding(point, binding, argument_types, 1025) == OB_ERR_SP_WRONG_ARG_NUM);
    point.set_routine_id(310004);
    wire.resize(point.get_serialize_size()); position = 0;
    CHECK(point.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
    position = 0; CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS);
    decoded.set_schema_version(42);
    for (int64_t i = 0; i < decoded.get_routine_params().count(); ++i) {
      decoded.get_routine_params().at(i)->set_routine_id(310004);
      decoded.get_routine_params().at(i)->set_schema_version(42);
    }
    CHECK(NativeRoutineSignature::variadic(decoded));
    CHECK(overlay->stage(decoded) == OB_SUCCESS);
    std::fill(wire.begin(), wire.end(), '\0');
    position = 0;
    CHECK(printer.print_routine_definition(310004, environment, printed, sizeof(printed), position,
        TZ_INFO(session.get())) == OB_SUCCESS);
    CHECK(std::string(printed, position).find("VARIADIC") != std::string::npos);
    CHECK(std::string(printed, position).find("[]") != std::string::npos);
    ParseResult line_dump{};
    CHECK(dump_parser.parse(ObString(position, printed), line_dump) == OB_SUCCESS);
    // A fixed prefix followed by a repeating native element type.
    create_default("CREATE FUNCTION native_db.native_point(x DOUBLE, VARIADIC remaining DOUBLE[]) RETURNS GEOMETRY "
        "DETERMINISTIC NO SQL SQL SECURITY INVOKER "
        "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_makepoint' LANGUAGE C", OB_SUCCESS);
    point.set_routine_id(310005); point.set_schema_version(42);
    for (int64_t i = 0; i < point.get_routine_params().count(); ++i) {
      point.get_routine_params().at(i)->set_routine_id(310005);
      point.get_routine_params().at(i)->set_schema_version(42);
    }
    CHECK(overlay->stage(point) == OB_SUCCESS);
    CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_INSERT);
    grant_native("default_point", true); grant_native("native_line", true); grant_native("native_point", true);
    CHECK(resolve("SELECT native_db.default_point()", OB_SUCCESS, 310003)->get_param_count() == 2);
    resolve("SELECT native_db.default_point(1,2,3)", OB_ERR_SP_WRONG_ARG_NUM, 310003);
    CHECK(resolve("SELECT native_db.native_line(POINT(0,0), POINT(3,4))", OB_SUCCESS, 310004)->get_param_count() == 2);
    resolve("SELECT native_db.native_line()", OB_ERR_SP_WRONG_ARG_NUM, 310004);
    resolve("SELECT native_db.native_point(7)", OB_ERR_SP_WRONG_ARG_NUM, 310005);
    std::string large = "native_db.native_line(";
    for (int i = 0; i < 64; ++i) {
      if (i) large += ',';
      large += "POINT(" + std::to_string(i) + ",0)";
    }
    const std::string excessive = "SELECT " + large + ",POINT(64,0))";
    auto *too_many = resolve(excessive.c_str(), OB_SUCCESS, 310004);
    ObExpr rejected_variadic;
    CHECK(udf.cg_expr(cg, *too_many, rejected_variadic) == OB_ENTRY_NOT_EXIST);
    large += ')';
    for (const auto &test : std::vector<std::pair<std::string, double>>{
        {"ST_X(native_db.default_point())", -3.5},
        {"ST_Y(native_db.default_point())", 4},
        {"ST_X(native_db.default_point(7))", 7},
        {"ST_Y(native_db.default_point(7))", 4},
        {"ST_Y(native_db.default_point(7,9))", 9},
        {"ST_X(native_db.default_point(NULL))", 0},
        {"ST_Length(native_db.native_line(POINT(0,0),POINT(3,4)))", 5},
        {"ST_Length(native_db.native_line(POINT(0,0),POINT(3,4),POINT(6,8)))", 10},
        {"ST_Length(native_db.native_line(POINT(0,0),NULL))", 0},
        {"ST_Y(native_db.native_point(7,8))", 8},
        {"ST_X(native_db.native_point(7,8,9))", 7},
        {"ST_Length(" + large + ")", 63}}) {
      const std::string text = std::string("SELECT ") + test.first;
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
      ObSelectResolver resolver(resolver_params);
      const int resolved = resolver.resolve(*parsed.result_tree_->children_[0]);
      std::cout << "native default SELECT: status=" << resolved << " sql=" << text << std::endl;
      CHECK(resolved == OB_SUCCESS);
      auto *expression = resolver.get_select_stmt()->get_select_item(0).expr_;
      ObExecContext context(arena); context.set_my_session(session.get()); context.set_sql_ctx(&sql);
      context.set_sql_proxy(&proxy); context.set_runtime_services(execution.get_runtime_services());
      CHECK(context.create_physical_plan_ctx() == OB_SUCCESS);
      ObSQLSessionInfo::ExecCtxSessionRegister enter(*session, &context);
      ObRawExprUniqueSet roots(false); CHECK(roots.append(expression) == OB_SUCCESS);
      ObStaticEngineExprCG generator(arena, session.get(), &guard, 0, 0);
      ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
      CHECK(context.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
      CHECK(frame.pre_alloc_exec_memory(context) == OB_SUCCESS);
      ObExpr *root = nullptr; ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*expression, outputs, root) == OB_SUCCESS);
      ObEvalCtx eval(context); ObDatum *value = nullptr;
      const int evaluated = root->eval(eval, value);
      std::cout << "native default execution: status=" << evaluated << std::endl;
      CHECK(evaluated == OB_SUCCESS && value);
      if (text.find("NULL") != std::string::npos) CHECK(value->is_null());
      else CHECK(!value->is_null() && value->get_double() == test.second);
      CHECK(session->get_database_id() == 101 && session->get_priv_user_id() == 123);
      ObSQLSessionInfo::ExecCtxSessionRegister leave(*session, &execution);
    }
    // Expanded arguments must also retain the ordinary vectorized path. Use a
    // controlled geometry column to observe skips, NULL rows and evaluation
    // count while parsing/binding/generating the native call normally.
    auto *line = resolve("SELECT native_db.native_line(NULL,POINT(3,4))", OB_SUCCESS, 310004);
    CHECK(line->replace_param_expr(0, column) == OB_SUCCESS);
    CHECK(line->formalize(session.get()) == OB_SUCCESS);
    ObExecContext vector_execution(arena);
    vector_execution.set_my_session(session.get()); vector_execution.set_sql_ctx(&sql);
    vector_execution.set_sql_proxy(&proxy);
    vector_execution.set_runtime_services(execution.get_runtime_services());
    CHECK(vector_execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister enter_vector(*session, &vector_execution);
    ObRawExprUniqueSet vector_roots(false); CHECK(vector_roots.append(line) == OB_SUCCESS);
    ObStaticEngineExprCG vector_generator(arena, session.get(), &guard, 0, 0);
    vector_generator.set_batch_size(rows);
    ObExprFrameInfo vector_frame(arena);
    CHECK(vector_generator.generate(vector_roots, vector_frame) == OB_SUCCESS);
    CHECK(vector_execution.init_expr_op(vector_frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(vector_frame.pre_alloc_exec_memory(vector_execution) == OB_SUCCESS);
    ObExpr *vector_root = nullptr, *vector_input = nullptr;
    ObSEArray<ObRawExpr *, 1> vector_outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*line, vector_outputs, vector_root) == OB_SUCCESS);
    CHECK(vector_root->type_ == T_FUN_UDF && vector_root->arg_cnt_ == 2);
    for (auto &expr : vector_frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) vector_input = &expr;
    CHECK(vector_input);
    std::vector<char> point_geometry(sizeof(ObLobCommon) + 26, 0);
    auto *point_data = (new (point_geometry.data()) ObLobCommon())->buffer_;
    point_data[4] = 1; point_data[5] = 1; point_data[6] = 1; // POINT(0,0).
    Input vector_argument{ObString(point_geometry.size(), point_geometry.data())};
    active_input = &vector_argument;
    vector_input->eval_func_ = evaluate_input;
    vector_input->eval_batch_func_ = expr_default_eval_batch_func;
    ObEvalCtx vector_eval(vector_execution);
    auto *vector_skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(rows)));
    CHECK(vector_skip); vector_skip->reset(rows); vector_skip->set(1);
    CHECK(vector_root->eval_batch(vector_eval, *vector_skip, rows) == OB_SUCCESS);
    CHECK(vector_argument.calls == rows - 1);
    std::vector<char> expected_line(46, 0);
    expected_line[4] = 1; expected_line[5] = 1; expected_line[6] = 2; expected_line[10] = 2;
    const double line_coordinates[] = {0, 0, 3, 4};
    std::memcpy(expected_line.data() + 14, line_coordinates, sizeof(line_coordinates));
    for (int i = 0; i < rows; ++i) {
      if (vector_skip->at(i)) continue;
      const auto &value = vector_root->locate_batch_datums(vector_eval)[i];
      if (i == 2) CHECK(value.is_null());
      else {
        CHECK(!value.is_null());
        ObString bytes;
        CHECK(read_plugin_expr_bytes(*vector_root, vector_eval, value, arena, bytes) == OB_SUCCESS);
        CHECK(bytes.length() == expected_line.size() &&
              std::memcmp(bytes.ptr(), expected_line.data(), expected_line.size()) == 0);
      }
    }
    CHECK(vector_root->eval_batch(vector_eval, *vector_skip, rows) == OB_SUCCESS);
    CHECK(vector_argument.calls == rows - 1); // Cached rows are not evaluated twice.
    CHECK(session->get_database_id() == 101 && session->get_priv_user_id() == 123);
    active_input = nullptr;
    ObSQLSessionInfo::ExecCtxSessionRegister leave_vector(*session, &execution);
    std::cout << "PASS: native defaults/expanded variadic scalar and batch calls, fixed prefixes, bounds, wire/SHOW CREATE and NULL arguments" << std::endl;

    // Real native declarations with the same database/name, all at sparse
    // nonzero slots. The host still controls IDs; no persistent DDL/ACL claim.
    CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 124) == OB_SUCCESS);
    session->set_priv_user_id(124); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    const auto declare_overload = [&](const char *name, const char *parameters, const char *implementation,
                                      uint64_t id, int64_t slot) {
      create_default(std::string("CREATE FUNCTION native_db.") + name + "(" + parameters +
          ") RETURNS GEOMETRY DETERMINISTIC NO SQL SQL SECURITY INVOKER AS 'org.seekdb.gis', '" +
          implementation + "' LANGUAGE C", OB_SUCCESS);
      point.set_routine_id(id); point.set_schema_version(42); point.set_overload(slot);
      for (int64_t j = 0; j < point.get_routine_params().count(); ++j) {
        auto *parameter = point.get_routine_params().at(j);
        parameter->set_routine_id(id); parameter->set_schema_version(42);
      }
      CHECK(overlay->stage(point) == OB_SUCCESS);
    };
    declare_overload("over_geo", "x DOUBLE,y DOUBLE", "org.seekdb.gis.function.st_point", 312001, 7);
    declare_overload("over_geo", "VARIADIC points GEOMETRY[]", "org.seekdb.gis.function.st_linestring", 312002, 900);
    declare_overload("over_geo", "x GEOMETRY,y GEOMETRY", "org.seekdb.gis.function.st_linestring", 312003, 55);
    declare_overload("over_defaults", "x DOUBLE,y DOUBLE DEFAULT 4", "org.seekdb.gis.function.st_makepoint", 312004, 4);
    declare_overload("over_defaults", "x DOUBLE,y DOUBLE DEFAULT 4,z DOUBLE DEFAULT 5", "org.seekdb.gis.function.st_makepoint", 312005, 19);
    declare_overload("grant_only", "x DOUBLE, y DOUBLE", "org.seekdb.gis.function.st_point", 312006, 77);
    CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_INSERT);
    CHECK(MockSchemaService::grant(*manager, 123, "over_geo", OB_PRIV_EXECUTE, "native_db") == OB_SUCCESS);
    CHECK(MockSchemaService::grant(*manager, 123, "over_defaults", OB_PRIV_EXECUTE, "native_db") == OB_SUCCESS);
    const auto alter_signature = [&](const char *text, int expected, uint64_t identity = OB_INVALID_ID,
                                      int64_t slot = 0) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(text), parsed) == OB_SUCCESS);
      ObAlterFunctionResolver resolver(resolver_params);
      const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
      std::cout << "native typed ALTER: status=" << status << " sql=" << text << std::endl;
      CHECK(status == expected);
      if (status == OB_SUCCESS) {
        const auto *statement = dynamic_cast<ObAlterRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
        const auto &arg = statement->get_routine_arg();
        CHECK(arg.is_need_alter_ && arg.routine_info_.get_routine_id() == identity &&
              arg.routine_info_.get_overload() == slot && arg.routine_info_.get_schema_version() == 42);
        CHECK(arg.routine_info_.get_comment() == ObString::make_string("signature change"));
        bool pinned = false;
        for (int64_t i = 0; i < arg.based_schema_object_infos_.count(); ++i) {
          const auto &ref = arg.based_schema_object_infos_.at(i);
          pinned |= ref.schema_type_ == ROUTINE_SCHEMA && ref.schema_id_ == identity && ref.schema_version_ == 42;
        }
        CHECK(pinned);
        std::vector<char> wire(arg.get_serialize_size()); int64_t position = 0;
        CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
        oceanbase::obcall::ObCreateRoutineArg decoded; position = 0;
        CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS);
        CHECK(decoded.routine_info_.get_routine_id() == identity && decoded.routine_info_.get_overload() == slot);
        std::string before, after;
        CHECK(NativeRoutineSignature::input_identity(arg.routine_info_, before) == OB_SUCCESS);
        CHECK(NativeRoutineSignature::input_identity(decoded.routine_info_, after) == OB_SUCCESS && before == after);
      }
    };
    alter_signature("ALTER FUNCTION native_db.over_geo COMMENT 'signature change'", OB_ERR_FUNC_DUP);
    alter_signature("ALTER FUNCTION native_db.over_geo(DOUBLE,DOUBLE) COMMENT 'signature change'", OB_SUCCESS, 312001, 7);
    alter_signature("ALTER FUNCTION native_db.over_geo(GEOMETRY,GEOMETRY) COMMENT 'signature change'", OB_SUCCESS, 312003, 55);
    alter_signature("ALTER FUNCTION native_db.over_geo(GEOMETRY[]) COMMENT 'signature change'", OB_SUCCESS, 312002, 900);
    alter_signature("ALTER FUNCTION native_db.over_geo(BIGINT,BIGINT) COMMENT 'signature change'", OB_ERR_SP_DOES_NOT_EXIST);
    alter_signature("ALTER FUNCTION native_db.over_geo(GEOMETRY) COMMENT 'signature change'", OB_ERR_SP_DOES_NOT_EXIST);
    alter_signature("ALTER FUNCTION native_db.over_defaults(DOUBLE) COMMENT 'signature change'", OB_ERR_SP_DOES_NOT_EXIST);
    alter_signature("ALTER FUNCTION native_db.over_defaults() COMMENT 'signature change'", OB_ERR_SP_DOES_NOT_EXIST);
    alter_signature("ALTER FUNCTION native_db.over_defaults(DOUBLE,DOUBLE) COMMENT 'signature change'", OB_SUCCESS, 312004, 4);
    alter_signature("ALTER FUNCTION native_db.area_alias(GEOMETRY) COMMENT 'signature change'", OB_SUCCESS, 310001);
    alter_signature("ALTER FUNCTION native_db.area_alias COMMENT 'signature change'", OB_SUCCESS, 310001);
    alter_signature("ALTER FUNCTION native_db.over_geo(GEOMETRY[],GEOMETRY) COMMENT 'signature change'", OB_NOT_SUPPORTED);
    alter_signature("ALTER FUNCTION native_db.over_geo() COMMENT 'signature change'", OB_ERR_SP_DOES_NOT_EXIST);
    std::cout << "PASS: native ALTER signature grammar, exact type/array/arity identity and version-pinned owned wire; no Root mutation claims" << std::endl;
    const auto drop_signature = [&](const char *text, int expected, uint64_t identity = OB_INVALID_ID,
                                     int64_t slot = 0) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(text), parsed) == OB_SUCCESS);
      ObDropFunctionResolver resolver(resolver_params);
      const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
      std::cout << "native typed DROP: status=" << status << " sql=" << text << std::endl;
      CHECK(status == expected);
      if (status != OB_SUCCESS) return;
      const auto *statement = dynamic_cast<ObDropRoutineStmt *>(resolver.get_basic_stmt()); CHECK(statement);
      const auto &arg = statement->get_routine_arg();
      CHECK(arg.is_valid() && arg.native_target_resolved_ && arg.native_target_.get_routine_id() == identity);
      std::vector<char> wire(arg.get_serialize_size()); int64_t position = 0;
      CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
      oceanbase::obcall::ObDropRoutineArg decoded; position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.is_valid());
      CHECK(decoded.native_target_resolved_ && decoded.native_target_.get_routine_id() == identity);
      ExtensionRoutineUpdateBatch batch;
      ObSEArray<ExtensionRoutineUpdateBatch::Operation, 1> operations;
      CHECK(operations.push_back({ExtensionRoutineUpdateBatch::Operation::Kind::DROP, nullptr, &arg}) == OB_SUCCESS);
      CHECK(batch.assign(operations) == OB_SUCCESS && batch.operations().count() == 1);
      const auto *owned = batch.operations().at(0).drop_arg_;
      CHECK(owned && owned->is_valid() && owned->native_target_resolved_ &&
            owned->native_target_.get_routine_id() == identity);
      if (identity == OB_INVALID_ID) {
        CHECK(decoded.if_exist_ && decoded.check_native_target(nullptr, 100) == OB_SUCCESS);
        CHECK(decoded.check_native_target(&arg.native_target_, 100) == OB_STATE_NOT_MATCH);
        return;
      }
      CHECK(decoded.native_target_.get_overload() == slot && decoded.native_target_.get_schema_version() == 42);
      CHECK(owned->native_target_.get_schema_version() == 42 &&
            owned->check_native_target(&arg.native_target_, 100) == OB_SUCCESS);
      CHECK(decoded.check_native_target(&arg.native_target_, 100) == OB_SUCCESS);
      CHECK(decoded.check_native_target(&arg.native_target_, 101) == OB_STATE_NOT_MATCH);
      CHECK(decoded.check_native_target(nullptr, 100) == (decoded.if_exist_ ? OB_SUCCESS : OB_ERR_SP_DOES_NOT_EXIST));
      bool pinned = false;
      for (int64_t i = 0; i < decoded.based_schema_object_infos_.count(); ++i) {
        const auto &ref = decoded.based_schema_object_infos_.at(i);
        pinned |= ref.schema_type_ == ROUTINE_SCHEMA && ref.schema_id_ == identity && ref.schema_version_ == 42;
      }
      CHECK(pinned);
      for (int mutation = 0; mutation < 9; ++mutation) {
        ObRoutineInfo changed; CHECK(changed.assign(arg.native_target_) == OB_SUCCESS);
        if (mutation == 0) changed.set_routine_id(identity + 1);
        else if (mutation == 1) changed.set_overload(slot + 1);
        else if (mutation == 2) changed.set_schema_version(43);
        else if (mutation == 3) changed.set_owner_id(changed.get_owner_id() + 1);
        else if (mutation == 4) CHECK(changed.set_routine_name(ObString::make_string("other")) == OB_SUCCESS);
        else if (mutation == 5) CHECK(changed.set_native_binding(changed.get_native_module_id(),
            ObString::make_string("org.seekdb.gis.function.other"), 1) == OB_SUCCESS);
        else {
          auto type = changed.get_routine_params().at(0)->get_param_type();
          if (mutation == 6) type.set_obj_type(ObIntType);
          else if (mutation == 7) type.set_collation_type(CS_TYPE_UTF8MB4_GENERAL_CI);
          else type.set_scale(type.get_scale() + 1);
          changed.get_routine_params().at(0)->set_param_type(type);
        }
        CHECK(decoded.check_native_target(&changed, 100) == OB_STATE_NOT_MATCH);
      }
      std::string before, after;
      CHECK(NativeRoutineSignature::input_identity(arg.native_target_, before) == OB_SUCCESS);
      std::fill(wire.begin(), wire.end(), '\0');
      CHECK(NativeRoutineSignature::input_identity(decoded.native_target_, after) == OB_SUCCESS && before == after);
      CHECK(decoded.native_target_.get_routine_name() == arg.native_target_.get_routine_name());
    };
    drop_signature("DROP FUNCTION native_db.over_geo", OB_ERR_FUNC_DUP);
    drop_signature("DROP FUNCTION IF EXISTS native_db.over_geo", OB_ERR_FUNC_DUP);
    drop_signature("DROP FUNCTION native_db.over_geo(DOUBLE,DOUBLE)", OB_SUCCESS, 312001, 7);
    drop_signature("DROP FUNCTION native_db.over_geo(GEOMETRY,GEOMETRY)", OB_SUCCESS, 312003, 55);
    drop_signature("DROP FUNCTION native_db.over_geo(GEOMETRY[])", OB_SUCCESS, 312002, 900);
    drop_signature("DROP FUNCTION IF EXISTS native_db.over_geo(GEOMETRY[])", OB_SUCCESS, 312002, 900);
    drop_signature("DROP FUNCTION native_db.over_geo(BIGINT,BIGINT)", OB_ERR_SP_DOES_NOT_EXIST);
    drop_signature("DROP FUNCTION IF EXISTS native_db.over_geo(BIGINT,BIGINT)", OB_SUCCESS);
    drop_signature("DROP FUNCTION native_db.over_defaults(DOUBLE)", OB_ERR_SP_DOES_NOT_EXIST);
    drop_signature("DROP FUNCTION IF EXISTS native_db.over_geo()", OB_SUCCESS);
    drop_signature("DROP FUNCTION native_db.over_geo(GEOMETRY[],GEOMETRY)", OB_NOT_SUPPORTED);
    drop_signature("DROP FUNCTION native_db.area_alias", OB_SUCCESS, 310001);
    drop_signature("DROP FUNCTION native_db.area_alias(GEOMETRY)", OB_SUCCESS, 310001);
    drop_signature("DROP FUNCTION native_db.AREA_ALIAS(GEOMETRY)", OB_SUCCESS, 310001);

    const auto verify_dcl_wire = [&](auto &arg, uint64_t identity, int64_t slot) {
      using Arg = typename std::remove_reference<decltype(arg)>::type;
      CHECK(arg.is_valid() && arg.native_target_.resolved_);
      CHECK(arg.native_target_.routine_.get_routine_id() == identity);
      CHECK(arg.native_target_.routine_.get_overload() == slot);
      ObSEArray<uint64_t, 4> actor_roles;
      for (uint64_t role : {125, 124, 125}) CHECK(actor_roles.push_back(role) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(123, actor_roles) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(123, arg.native_target_.enabled_roles_) == OB_SUCCESS);
      arg.grantor_id_ = 123;
      CHECK(arg.is_valid() && arg.native_target_.enabled_roles_.count() == 2);
      arg.grantor_id_ = 124; CHECK(!arg.is_valid()); arg.grantor_id_ = 123;
      std::vector<char> wire(arg.get_serialize_size()); int64_t position = 0;
      CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
      Arg decoded; position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.is_valid());
      std::fill(wire.begin(), wire.end(), '\x5a');
      const auto &target = decoded.native_target_;
      CHECK(target.signature_qualified_ == arg.native_target_.signature_qualified_);
      CHECK(target.actor_id_ == 123 && target.enabled_roles_.count() == 2 &&
          target.enabled_roles_.at(0) == 124 && target.enabled_roles_.at(1) == 125);
      CHECK(target.resolved_ && target.routine_.get_schema_version() == 42);
      CHECK(target.check(&arg.native_target_.routine_) == OB_SUCCESS);
      CHECK(target.check(nullptr) == OB_ERR_SP_DOES_NOT_EXIST);
      CHECK(target.admit(guard, ObString::make_string("native_db"),
          target.routine_.get_routine_name(), identity) == OB_NOT_SUPPORTED);
      CHECK(target.admit(guard, ObString::make_string("caller_db"),
          target.routine_.get_routine_name(), identity) == OB_STATE_NOT_MATCH);
      CHECK(target.admit(guard, ObString::make_string("native_db"),
          target.routine_.get_routine_name(), identity + 1) == OB_STATE_NOT_MATCH);
      for (int mutation = 0; mutation < 6; ++mutation) {
        ObRoutineInfo changed; CHECK(changed.assign(arg.native_target_.routine_) == OB_SUCCESS);
        switch (mutation) {
          case 0: changed.set_schema_version(43); break;
          case 1: changed.set_routine_id(identity + 1); break;
          case 2: changed.set_overload(slot + 1); break;
          case 3: changed.set_owner_id(999); break;
          case 4: CHECK(changed.set_routine_name("rebound") == OB_SUCCESS); break;
          case 5: CHECK(changed.set_native_binding(ObString::make_string("org.seekdb.other"),
              ObString::make_string("org.seekdb.gis.function.st_point"), 1) == OB_SUCCESS); break;
        }
        CHECK(target.check(&changed) == OB_STATE_NOT_MATCH);
      }
      oceanbase::obcall::NativeRoutinePrivilegeTarget unpinned;
      ObSEArray<const ObRoutineInfo *, 4> dcl_family;
      CHECK(guard.get_standalone_function_infos(100, target.routine_.get_routine_name(), dcl_family) == OB_SUCCESS);
      CHECK(unpinned.admit(guard, ObString::make_string("native_db"),
          target.routine_.get_routine_name(), identity) ==
          (dcl_family.count() > 1 ? OB_ERR_FUNC_DUP : OB_SCHEMA_EAGAIN));
      CHECK(unpinned.admit(guard, ObString::make_string("native_db"),
          ObString::make_string("over_geo"), OB_INVALID_ID) == OB_ERR_FUNC_DUP);
      CHECK(unpinned.admit(guard, ObString::make_string("missing_db"),
          ObString::make_string("old_function"), OB_INVALID_ID) == OB_SUCCESS);
      CHECK(target.admit(guard, ObString::make_string("missing_db"),
          target.routine_.get_routine_name(), identity) == OB_STATE_NOT_MATCH);
      // Reusing a request decoder for legacy DCL must not retain an earlier target.
      const bool qualified = arg.native_target_.signature_qualified_;
      arg.native_target_.clear_actor();
      arg.native_target_.resolved_ = false;
      arg.native_target_.signature_qualified_ = false;
      wire.resize(arg.get_serialize_size()); position = 0;
      CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
      position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.is_valid());
      CHECK(!decoded.native_target_.resolved_ && !decoded.native_target_.signature_qualified_ &&
          decoded.native_target_.routine_.get_routine_id() == OB_INVALID_ID &&
          decoded.native_target_.actor_id_ == OB_INVALID_ID && decoded.native_target_.enabled_roles_.empty());
      arg.native_target_.resolved_ = true;
      arg.native_target_.signature_qualified_ = qualified;
      CHECK(arg.native_target_.bind_actor(123, actor_roles) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(0, actor_roles) == OB_INVALID_ARGUMENT);
      CHECK(arg.native_target_.actor_id_ == OB_INVALID_ID && arg.native_target_.enabled_roles_.empty());
      CHECK(arg.native_target_.bind_actor(OB_INVALID_ID, actor_roles) == OB_INVALID_ARGUMENT);
      CHECK(actor_roles.push_back(0) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(123, actor_roles) == OB_INVALID_ARGUMENT);
      actor_roles.reset();
      for (int i = 0; i < 16385; ++i) CHECK(actor_roles.push_back(124) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(123, actor_roles) == OB_INVALID_ARGUMENT);
      actor_roles.reset(); CHECK(actor_roles.push_back(124) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(123, actor_roles) == OB_SUCCESS);
      // A malformed request payload must clear a previously populated target.
      // Test the payload entry: generic UNIS framing rejects a bad envelope
      // before entering any request-specific deserializer.
      wire.resize(arg.get_serialize_size()); position = 0;
      CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS);
      position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS);
      position = 0;
      const char malformed[] = {'\x80'};
      CHECK(decoded.deserialize_(malformed, sizeof(malformed), position) != OB_SUCCESS);
      CHECK(!decoded.native_target_.resolved_ && decoded.native_target_.actor_id_ == OB_INVALID_ID &&
          decoded.native_target_.enabled_roles_.empty());
      const auto bad_roles = [&](uint64_t actor, int64_t count, std::initializer_list<uint64_t> roles) {
        std::vector<char> payload(arg.native_target_.get_serialize_size() + 128);
        char *buf = payload.data(); const int64_t buf_len = payload.size(); int64_t pos = 0;
        int ret = OB_SUCCESS;
        const bool resolved = true;
        const int64_t version = arg.native_target_.routine_.get_schema_version();
        LST_DO_CODE(OB_UNIS_ENCODE, resolved, arg.native_target_.routine_, version, qualified, actor, count);
        for (uint64_t role : roles) OB_UNIS_ENCODE(role);
        CHECK(ret == OB_SUCCESS);
        auto &reused = decoded.native_target_;
        CHECK(reused.assign(arg.native_target_.routine_, qualified) == OB_SUCCESS);
        CHECK(reused.bind_actor(123, actor_roles) == OB_SUCCESS);
        int64_t cursor = 0;
        CHECK(reused.deserialize_(buf, pos, cursor) != OB_SUCCESS);
        CHECK(!reused.resolved_ && reused.actor_id_ == OB_INVALID_ID && reused.enabled_roles_.empty());
      };
      bad_roles(123, -1, {});
      bad_roles(123, 16385, {}); // Reject before reading or allocating advertised entries.
      bad_roles(OB_INVALID_ID, 1, {124});
      bad_roles(0, 1, {124});
      bad_roles(123, 2, {124, 124});
      bad_roles(123, 2, {125, 124});
      bad_roles(123, 1, {0});
      bad_roles(123, 1, {OB_INVALID_ID});
      bad_roles(123, 2, {124});
      auto &reused = decoded.native_target_;
      CHECK(reused.bind_actor(123, actor_roles) == OB_INVALID_ARGUMENT); // No resolved target after failure.
      CHECK(reused.assign(arg.native_target_.routine_, qualified) == OB_SUCCESS);
      CHECK(reused.bind_actor(123, actor_roles) == OB_SUCCESS);
      CHECK(reused.assign(arg.native_target_.routine_, qualified) == OB_SUCCESS);
      CHECK(reused.is_valid() && reused.resolved_ && reused.actor_id_ == OB_INVALID_ID && reused.enabled_roles_.empty());
    };
    const auto verify_dcl_authority = [&](const ObStmt &statement, uint64_t identity) {
      using namespace oceanbase::share;
      ObSessionPrivInfo privilege;
      CHECK(session->get_session_priv_info(privilege) == OB_SUCCESS);
      ObSEArray<ObNeedPriv, 2> needs;
      CHECK(ObPrivilegeCheck::get_stmt_need_privs(privilege, &statement, needs) == OB_SUCCESS);
      CHECK(needs.count() == 1 && needs.at(0).native_routine_id_ == identity &&
          needs.at(0).native_routine_version_ == 42 && needs.at(0).priv_set_ == (OB_PRIV_EXECUTE | OB_PRIV_GRANT));
      ObStmtNeedPrivs required(arena), cached(arena);
      CHECK(required.need_privs_.assign(needs) == OB_SUCCESS);
      CHECK(cached.deep_copy(required, arena) == OB_SUCCESS);
      CHECK(cached.need_privs_.at(0).native_routine_id_ == identity && cached.need_privs_.at(0).native_routine_version_ == 42);
      const auto check = [&] { return ObPrivilegeCheck::check_privilege(sql, cached); };
      const ObPrivSet session_bits = session->get_user_priv_set();
      session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_EXECUTE | OB_PRIV_GRANT);
      CHECK(check() == OB_ERR_NO_ROUTINE_PRIVILEGE); // Ignore stale broad session bits and name grants.
      session->set_user_priv_set(session_bits);
      ObPackedObjPriv execute = 0, execute_option = 0, alter = 0, alter_option = 0;
      CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_EXECUTE, execute) == OB_SUCCESS);
      CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, OBJ_PRIV_ID_EXECUTE, execute_option) == OB_SUCCESS);
      CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(NO_OPTION, OBJ_PRIV_ID_ALTER, alter) == OB_SUCCESS);
      CHECK(ObPrivPacker::raw_obj_priv_to_packed_info(GRANT_OPTION, OBJ_PRIV_ID_ALTER, alter_option) == OB_SUCCESS);
      CHECK(MockSchemaService::grant_object(*manager, identity, 124, 123, execute) == OB_SUCCESS);
      CHECK(check() == OB_ERR_NO_ROUTINE_PRIVILEGE); // EXECUTE alone cannot delegate.
      CHECK(MockSchemaService::grant_object(*manager, identity, 124, 123, execute_option) == OB_SUCCESS);
      CHECK(check() == OB_SUCCESS);
      auto &need = cached.need_privs_.at(0);
      need.priv_set_ |= OB_PRIV_ALTER_ROUTINE;
      CHECK(MockSchemaService::grant_object(*manager, identity, 124, 123, execute_option | alter) == OB_SUCCESS);
      CHECK(check() == OB_ERR_NO_ROUTINE_PRIVILEGE);
      CHECK(MockSchemaService::grant_object(*manager, identity, 124, 123, execute_option | alter_option) == OB_SUCCESS);
      CHECK(check() == OB_SUCCESS);
      need.priv_set_ = OB_PRIV_EXECUTE | OB_PRIV_GRANT;
      CHECK(MockSchemaService::grant_object(*manager, identity, 124, 123, execute_option) == OB_SUCCESS);
      const ObRoutineInfo *routine = nullptr;
      CHECK(guard.get_routine_info(identity, routine) == OB_SUCCESS && routine);
      {
        RoutineCatalogSavepoint private_revoke(overlay, native_privileges); CHECK(private_revoke.valid());
        CHECK(native_privileges->record_object_change(*routine, 124, 123, 43, execute_option, execute) == OB_SUCCESS);
        CHECK(check() == OB_ERR_NO_ROUTINE_PRIVILEGE);
      }
      CHECK(check() == OB_SUCCESS);
      need.native_routine_version_ = 43; CHECK(check() == OB_SCHEMA_EAGAIN); need.native_routine_version_ = 42;
      need.native_routine_id_ = OB_INVALID_ID; CHECK(check() == OB_INVALID_ARGUMENT); need.native_routine_id_ = identity;
      need.priv_level_ = OB_PRIV_TABLE_LEVEL; CHECK(check() == OB_INVALID_ARGUMENT); need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
      need.db_ = ObString::make_string("caller_db"); CHECK(check() == OB_SCHEMA_EAGAIN);
      need.db_ = ObString::make_string("native_db");
      CHECK(MockSchemaService::revoke_object(*manager, identity, 124, 123) == OB_SUCCESS);
      CHECK(check() == OB_ERR_NO_ROUTINE_PRIVILEGE); // Same copied requirement rechecks revocation.
    };
    const auto dcl = [&](const char *name, int expected, uint64_t identity = OB_INVALID_ID, int64_t slot = 0) {
      for (bool grant : {true, false}) {
        const std::string text = std::string(grant ? "GRANT EXECUTE ON FUNCTION native_db." :
            "REVOKE EXECUTE ON FUNCTION native_db.") + name +
            (grant ? " TO 'caller'@'localhost'" : " FROM 'caller'@'localhost'");
        CHECK(session->store_query_string(ObString(text.size(), text.data())) == OB_SUCCESS);
        ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
        CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
        if (grant) {
          ObGrantResolver resolver(resolver_params);
          const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
          std::cout << "native GRANT resolve: status=" << status << " sql=" << text << std::endl;
          CHECK(status == expected);
          if (status != OB_SUCCESS) continue;
          auto *statement = dynamic_cast<ObGrantStmt *>(resolver.get_basic_stmt()); CHECK(statement);
          auto &arg = static_cast<oceanbase::obcall::ObGrantArg &>(statement->get_ddl_arg());
          arg.db_ = statement->get_database_name(); arg.table_ = statement->get_table_name();
          arg.object_id_ = statement->get_object_id(); arg.object_type_ = statement->get_object_type();
          arg.priv_level_ = statement->get_grant_level(); arg.priv_set_ = statement->get_priv_set();
          CHECK(arg.native_target_.signature_qualified_ == (std::strchr(name, '(') != nullptr));
          verify_dcl_wire(arg, identity, slot);
          verify_dcl_authority(*statement, identity);
          arg.object_id_ += 1; CHECK(!arg.is_valid());
        } else {
          ObRevokeResolver resolver(resolver_params);
          const int status = resolver.resolve(*parsed.result_tree_->children_[0]);
          std::cout << "native REVOKE resolve: status=" << status << " sql=" << text << std::endl;
          CHECK(status == expected);
          if (status != OB_SUCCESS) continue;
          auto *statement = dynamic_cast<ObRevokeStmt *>(resolver.get_basic_stmt()); CHECK(statement);
          auto &arg = static_cast<oceanbase::obcall::ObRevokeRoutineArg &>(statement->get_ddl_arg());
          CHECK(statement->get_users().count() == 1);
          arg.user_id_ = statement->get_users().at(0);
          arg.db_ = statement->get_database_name(); arg.routine_ = statement->get_table_name();
          arg.obj_id_ = statement->get_object_id(); arg.obj_type_ = uint64_t(statement->get_object_type());
          arg.priv_set_ = statement->get_priv_set();
          CHECK(arg.native_target_.signature_qualified_ == (std::strchr(name, '(') != nullptr));
          verify_dcl_wire(arg, identity, slot);
          verify_dcl_authority(*statement, identity);
          arg.obj_type_ = uint64_t(ObObjectType::PROCEDURE); CHECK(!arg.is_valid());
        }
      }
    };
    dcl("over_geo", OB_ERR_FUNC_DUP);
    dcl("grant_only", OB_SUCCESS, 312006, 77);
    dcl("area_alias", OB_SUCCESS, 310001);
    dcl("AREA_ALIAS", OB_SUCCESS, 310001);
    dcl("over_geo(DOUBLE,DOUBLE)", OB_SUCCESS, 312001, 7);
    dcl("over_geo(GEOMETRY,GEOMETRY)", OB_SUCCESS, 312003, 55);
    dcl("over_geo(GEOMETRY[])", OB_SUCCESS, 312002, 900);
    dcl("over_geo(BIGINT,BIGINT)", OB_ERR_SP_DOES_NOT_EXIST);
    dcl("over_geo()", OB_ERR_SP_DOES_NOT_EXIST);
    dcl("over_geo(GEOMETRY[],GEOMETRY)", OB_NOT_SUPPORTED);
    dcl("over_geo(GEOMETRY[][])", OB_NOT_SUPPORTED);
    dcl("over_geo(VECTOR(3))", OB_NOT_SUPPORTED);
    dcl("over_defaults(DOUBLE)", OB_ERR_SP_DOES_NOT_EXIST);
    dcl("over_defaults(DOUBLE,DOUBLE)", OB_SUCCESS, 312004, 4);
    dcl("over_defaults(DOUBLE,DOUBLE,DOUBLE)", OB_SUCCESS, 312005, 19);
    dcl("area_alias(GEOMETRY)", OB_SUCCESS, 310001);
    dcl("AREA_ALIAS(GEOMETRY)", OB_SUCCESS, 310001);
    // New REVOKE syntax is preserved end-to-end as request intent, but the
    // Root admission gate still refuses the legacy name-keyed write path.
    for (bool typed : {false, true}) for (bool option_only : {false, true}) for (int behavior = 0; behavior < 3; ++behavior) {
      using Arg = oceanbase::obcall::ObRevokeRoutineArg;
      const char *name = typed ? "over_geo(DOUBLE,DOUBLE)" : "area_alias";
      const uint64_t identity = typed ? 312001 : 310001;
      const bool options = option_only || behavior != 0;
      const std::string text = std::string("REVOKE ") + (option_only ? "GRANT OPTION FOR " : "") +
          "EXECUTE ON FUNCTION native_db." + name + " FROM 'caller'@'localhost', 'owner'@'localhost'" +
          (behavior == 1 ? " RESTRICT" : (behavior == 2 ? " CASCADE" : ""));
      CHECK(session->store_query_string(ObString(text.size(), text.data())) == OB_SUCCESS);
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
      auto &tree = *parsed.result_tree_->children_[0];
      CHECK(tree.type_ == T_REVOKE && tree.num_child_ == 7 && tree.value_ == 0 && tree.children_[6]);
      std::cout << "native REVOKE options: flags=" << tree.children_[6]->value_ << " sql=" << text << std::endl;
      CHECK(tree.children_[6]->type_ == T_INT && tree.children_[6]->value_ == (int(option_only) | (behavior << 1)));
      ObRevokeResolver resolver(resolver_params);
      CHECK(resolver.resolve(tree) == OB_SUCCESS);
      auto *statement = dynamic_cast<ObRevokeStmt *>(resolver.get_basic_stmt()); CHECK(statement);
      CHECK(statement->get_users().count() == 2 && statement->has_native_revoke_options() == options);
      auto &arg = static_cast<Arg &>(statement->get_ddl_arg());
      arg.user_id_ = statement->get_users().at(0);
      arg.db_ = statement->get_database_name(); arg.routine_ = statement->get_table_name();
      arg.obj_id_ = statement->get_object_id(); arg.obj_type_ = uint64_t(statement->get_object_type());
      arg.priv_set_ = statement->get_priv_set(); arg.grantor_id_ = 123;
      ObSEArray<uint64_t, 4> roles; CHECK(roles.push_back(124) == OB_SUCCESS);
      CHECK(arg.native_target_.bind_actor(123, roles) == OB_SUCCESS);
      CHECK(arg.is_valid() && arg.grant_option_only_ == option_only && int(arg.revoke_behavior_) == behavior);
      CHECK(arg.admit_native_target(guard) == OB_NOT_SUPPORTED);
      CHECK(arg.native_target_.revalidate(guard, arg.db_, arg.routine_, arg.obj_id_) == OB_SUCCESS);
      verify_dcl_authority(*statement, identity);
      std::vector<char> wire(arg.get_serialize_size()); int64_t position = 0;
      CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS && position == int64_t(wire.size()));
      Arg decoded; position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.is_valid());
      CHECK(decoded.grant_option_only_ == option_only && int(decoded.revoke_behavior_) == behavior);
      CHECK(decoded.native_target_.actor_id_ == 123 && decoded.native_target_.enabled_roles_.count() == 1 &&
          decoded.native_target_.enabled_roles_.at(0) == 124 && decoded.native_target_.routine_.get_routine_id() == identity);
      CHECK(decoded.admit_native_target(guard) == OB_NOT_SUPPORTED);
      decoded.db_ = ObString::make_string("caller_db");
      CHECK(decoded.admit_native_target(guard) == OB_STATE_NOT_MATCH); // Identity checks precede the feature gate.

      // Reused decoders must not retain native options after a legacy request.
      Arg legacy; legacy.user_id_ = 123; legacy.db_ = ObString::make_string("native_db");
      legacy.routine_ = ObString::make_string("legacy_procedure"); legacy.obj_type_ = uint64_t(ObObjectType::PROCEDURE);
      wire.resize(legacy.get_serialize_size()); position = 0;
      CHECK(legacy.serialize(wire.data(), wire.size(), position) == OB_SUCCESS); position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.is_valid());
      CHECK(!decoded.has_native_revoke_options() && !decoded.native_target_.resolved_);

      for (int mutation = 0; mutation < 7; ++mutation) {
        const auto saved_behavior = arg.revoke_behavior_;
        const bool saved_option = arg.grant_option_only_;
        arg.grant_option_only_ = true;
        if (mutation == 0) arg.revoke_behavior_ = static_cast<Arg::NativeRevokeBehavior>(-1);
        if (mutation == 1) arg.revoke_behavior_ = static_cast<Arg::NativeRevokeBehavior>(3);
        if (mutation == 2) arg.priv_set_ = OB_PRIV_GRANT;
        if (mutation == 3) arg.priv_set_ = 0;
        if (mutation == 4) arg.obj_type_ = uint64_t(ObObjectType::PROCEDURE);
        if (mutation == 5) arg.grantor_id_ = 124;
        if (mutation == 6) {
          arg.native_target_.clear_actor(); arg.native_target_.resolved_ = false;
          arg.native_target_.signature_qualified_ = false;
        }
        CHECK(!arg.is_valid() && arg.admit_native_target(guard) == OB_INVALID_ARGUMENT);
        wire.resize(arg.get_serialize_size()); position = 0;
        CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS); position = 0;
        CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_INVALID_DATA);
        CHECK(!decoded.is_valid() && !decoded.has_native_revoke_options() && !decoded.native_target_.resolved_ &&
            decoded.native_target_.actor_id_ == OB_INVALID_ID && decoded.native_target_.enabled_roles_.empty());
        arg.grant_option_only_ = saved_option; arg.revoke_behavior_ = saved_behavior;
        arg.priv_set_ = OB_PRIV_EXECUTE; arg.obj_type_ = uint64_t(ObObjectType::FUNCTION); arg.grantor_id_ = 123;
        arg.native_target_.resolved_ = true; arg.native_target_.signature_qualified_ = typed;
        CHECK(arg.native_target_.bind_actor(123, roles) == OB_SUCCESS);
      }
      tree.children_[6]->value_ = 6;
      // The executor sends the entire recipient set once. Owned canonical
      // recipients survive the wire; neither a decoder error nor a legacy
      // request may reuse a previous batch or fall back to its first user.
      ObSEArray<uint64_t, 4> recipients;
      CHECK(recipients.push_back(124) == OB_SUCCESS && recipients.push_back(123) == OB_SUCCESS &&
          recipients.push_back(124) == OB_SUCCESS);
      CHECK(arg.set_native_grantees(recipients) == OB_SUCCESS && arg.is_valid());
      CHECK(arg.user_id_ == OB_INVALID_ID && arg.native_grantees_.count() == 2 &&
          arg.native_grantees_.at(0) == 123 && arg.native_grantees_.at(1) == 124);
      CHECK(arg.set_native_grantees(arg.native_grantees_) == OB_SUCCESS && arg.is_valid());
      wire.resize(arg.get_serialize_size()); position = 0;
      CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS && position == int64_t(wire.size()));
      position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.is_valid());
      CHECK(decoded.user_id_ == OB_INVALID_ID && decoded.native_grantees_.count() == 2 &&
          decoded.native_grantees_.at(0) == 123 && decoded.native_grantees_.at(1) == 124);
      CHECK(decoded.native_target_.revalidate(guard, decoded.db_, decoded.routine_, decoded.obj_id_) == OB_SUCCESS);
      for (int mutation = 0; mutation < 6; ++mutation) {
        if (mutation == 0) arg.user_id_ = 123; // Two representations cannot compete.
        if (mutation == 1) arg.native_grantees_.at(1) = 123; // Duplicate.
        if (mutation == 2) arg.native_grantees_.at(0) = 125; // Unsorted.
        if (mutation == 3) arg.native_grantees_.at(0) = 0;
        if (mutation == 4) arg.native_grantees_.at(1) = OB_INVALID_ID;
        if (mutation == 5) arg.native_target_.clear_actor();
        CHECK(!arg.is_valid());
        wire.resize(arg.get_serialize_size()); position = 0;
        CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS); position = 0;
        CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_INVALID_DATA);
        CHECK(!decoded.is_valid() && decoded.native_grantees_.empty() && decoded.user_id_ == OB_INVALID_ID);
        CHECK(arg.native_target_.bind_actor(123, roles) == OB_SUCCESS);
        CHECK(arg.set_native_grantees(recipients) == OB_SUCCESS && arg.is_valid());
      }
      wire.resize(legacy.get_serialize_size()); position = 0;
      CHECK(legacy.serialize(wire.data(), wire.size(), position) == OB_SUCCESS); position = 0;
      CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_SUCCESS && decoded.native_grantees_.empty());
      recipients.reset();
      CHECK(arg.set_native_grantees(recipients) == OB_INVALID_ARGUMENT && !arg.is_valid());
      if (!typed && !option_only && behavior == 0) {
        for (uint64_t id = 1; id <= 16385; ++id) CHECK(recipients.push_back(id) == OB_SUCCESS);
        CHECK(arg.set_native_grantees(recipients) == OB_INVALID_ARGUMENT && !arg.is_valid());
        CHECK(arg.native_grantees_.assign(recipients) == OB_SUCCESS);
        wire.resize(arg.get_serialize_size()); position = 0;
        CHECK(arg.serialize(wire.data(), wire.size(), position) == OB_SUCCESS); position = 0;
        CHECK(decoded.deserialize(wire.data(), wire.size(), position) == OB_INVALID_DATA && decoded.native_grantees_.empty());
      }
      {
        // Execute the real Query-to-Root dispatch. Root storage is deliberately
        // substituted here; this proves one call, not successful SQL commit.
        class ActorService final : public MockSchemaService {
        public:
          ActorService(ObSchemaMgr &manager, const ObUserInfo &actor) : manager_(manager), actor_(actor) {}
          int get_runtime_schema_guard(ObSchemaGetterGuard &g,
              int64_t = OB_INVALID_VERSION, RefreshSchemaMode = RefreshSchemaMode::NORMAL) override {
            int ret = bind(g, *this, manager_);
            return ret == OB_SUCCESS ? cache_user(g, actor_) : ret;
          }
          ObSchemaMgr &manager_;
          const ObUserInfo &actor_;
        } actor_service(*manager, caller);
        class Commands final : public oceanbase::rootserver::ObLocalManagementService {
        public:
          int calls = 0, status = OB_SUCCESS;
          int revoke_routine(const oceanbase::obcall::ObRevokeRoutineArg &request) override {
            ++calls;
            CHECK(oceanbase::query::root_service_serial_active());
            CHECK(request.is_valid() && request.user_id_ == OB_INVALID_ID && request.native_grantees_.count() == 2);
            CHECK(request.native_grantees_.at(0) == 123 && request.native_grantees_.at(1) == 124);
            CHECK(request.native_target_.actor_id_ == 123 && request.grantor_id_ == 123 && !request.ddl_stmt_str_.empty());
            return status;
          }
        } commands;
        auto *saved_service = GCTX.schema_service_;
        GCTX.schema_service_ = &actor_service;
        execution.set_command_services(&commands, nullptr, nullptr, nullptr, nullptr);
        ObString command_sql;
        CHECK(ob_write_string(arena, ObString(text.size(), text.data()), command_sql) == OB_SUCCESS);
        statement->get_query_ctx()->set_sql_stmt(command_sql);
        ObRevokeExecutor executor;
        for (int status : {OB_SUCCESS, OB_TIMEOUT}) {
          commands.status = status; commands.calls = 0;
          CHECK(executor.execute(execution, *statement) == status && commands.calls == 1);
          CHECK(!oceanbase::query::root_service_serial_active());
        }
        execution.set_command_services(nullptr, nullptr, nullptr, nullptr, nullptr);
        GCTX.schema_service_ = saved_service;
      }
      ObRevokeResolver malformed(resolver_params);
      CHECK(malformed.resolve(tree) == OB_ERR_PARSE_SQL);
    }
    {
      const char *text = "REVOKE GRANT OPTION FOR EXECUTE, ALTER ROUTINE ON FUNCTION native_db.area_alias "
          "FROM 'caller'@'localhost' CASCADE";
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(text), parsed) == OB_SUCCESS);
      ObRevokeResolver resolver(resolver_params);
      CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
      auto *statement = dynamic_cast<ObRevokeStmt *>(resolver.get_basic_stmt()); CHECK(statement);
      CHECK(statement->has_native_revoke_options() &&
          statement->get_priv_set() == (OB_PRIV_EXECUTE | OB_PRIV_ALTER_ROUTINE));
    }
    for (const char *text : {
        "REVOKE GRANT OPTION FOR EXECUTE ON PROCEDURE native_db.area_alias FROM 'caller'@'localhost'",
        "REVOKE SELECT ON TABLE native_db.missing FROM 'caller'@'localhost' CASCADE",
        "REVOKE GRANT OPTION FOR EXECUTE ON FUNCTION native_db.missing FROM 'caller'@'localhost'",
        "REVOKE IF EXISTS GRANT OPTION FOR EXECUTE ON PROCEDURE native_db.missing FROM 'missing'@'localhost' IGNORE UNKNOWN USER CASCADE"}) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(text), parsed) == OB_SUCCESS);
      ObRevokeResolver resolver(resolver_params);
      CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) != OB_SUCCESS);
    }
    for (const char *text : {
        "REVOKE GRANT OPTION EXECUTE ON FUNCTION native_db.area_alias FROM 'caller'@'localhost'",
        "REVOKE GRANT OPTION FOR EXECUTE ON FUNCTION native_db.area_alias FROM 'caller'@'localhost' CASCADE RESTRICT",
        "REVOKE EXECUTE ON FUNCTION native_db.area_alias CASCADE FROM 'caller'@'localhost'",
        "REVOKE GRANT OPTION FOR EXECUTE ON native_db.area_alias FROM 'caller'@'localhost'"}) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(text), parsed) != OB_SUCCESS);
    }
    for (const char *text : {
        "REVOKE SELECT ON native_db.t FROM 'caller'@'localhost'",
        "REVOKE EXECUTE ON PROCEDURE native_db.p FROM 'caller'@'localhost'",
        "REVOKE ALL PRIVILEGES, GRANT OPTION FROM 'caller'@'localhost'",
        "REVOKE 'reader' FROM 'caller'@'localhost'",
        "REVOKE IF EXISTS SELECT ON native_db.t FROM 'caller'@'localhost' IGNORE UNKNOWN USER"}) {
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString::make_string(text), parsed) == OB_SUCCESS);
      const auto *tree = parsed.result_tree_->children_[0]; CHECK(tree);
      if (tree->type_ == T_REVOKE) {
        CHECK(tree->value_ == 0 && tree->num_child_ == 7);
        CHECK(tree->children_[6] == nullptr || tree->children_[6]->value_ == 0);
      }
    }
    std::cout << "PASS: 12 native REVOKE option/behavior/signature combinations, object authority, owned request wire, 84 invalid payloads, decoder reuse and Root admission rejection; no SQL mutation claims" << std::endl;
    {
      const ObRoutineInfo *original = nullptr;
      CHECK(guard.get_routine_info(312001, original) == OB_SUCCESS && original);
      oceanbase::obcall::NativeRoutinePrivilegeTarget pinned;
      CHECK(pinned.assign(*original, true) == OB_SUCCESS);
      RoutineCatalogSavepoint duplicate_scope(overlay, native_privileges);
      CHECK(duplicate_scope.valid());
      ObRoutineInfo duplicate;
      CHECK(duplicate.assign(*original) == OB_SUCCESS);
      duplicate.set_routine_id(313001); duplicate.set_overload(99);
      for (int64_t i = 0; i < duplicate.get_routine_params().count(); ++i) {
        auto *parameter = duplicate.get_routine_params().at(i);
        CHECK(parameter); parameter->set_routine_id(313001);
      }
      // The private view itself refuses a duplicate signature. Replacing the
      // object under the same name/signature must not retarget a pinned request.
      CHECK(overlay->stage(duplicate) == OB_STATE_NOT_MATCH);
      CHECK(overlay->erase(100, original->get_routine_name(), ROUTINE_FUNCTION_TYPE, 312001, 7) == OB_SUCCESS);
      CHECK(overlay->stage(duplicate) == OB_SUCCESS);
      CHECK(pinned.admit(guard, ObString::make_string("native_db"),
          ObString::make_string("over_geo"), 312001) == OB_ERR_SP_DOES_NOT_EXIST);
      dcl("over_geo(DOUBLE,DOUBLE)", OB_SUCCESS, 313001, 99);
    }
    dcl("over_geo(DOUBLE,DOUBLE)", OB_SUCCESS, 312001, 7);
    for (const char *object : {"TABLE", "PROCEDURE"}) for (bool grant : {true, false}) {
      const std::string text = std::string(grant ? "GRANT EXECUTE ON " : "REVOKE EXECUTE ON ") +
          object + " native_db.area_alias(GEOMETRY)" +
          (grant ? " TO 'caller'@'localhost'" : " FROM 'caller'@'localhost'");
      CHECK(session->store_query_string(ObString(text.size(), text.data())) == OB_SUCCESS);
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
      if (grant) {
        ObGrantResolver resolver(resolver_params);
        CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_INVALID_ARGUMENT);
      } else {
        ObRevokeResolver resolver(resolver_params);
        CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_INVALID_ARGUMENT);
      }
    }
    for (const char *signature : {"(1,2)", "(DOUBLE DEFAULT 1)", "(x DOUBLE)", "(DOUBLE + DOUBLE)"}) {
      const std::string text = std::string("GRANT EXECUTE ON FUNCTION native_db.over_geo") +
          signature + " TO 'caller'@'localhost'";
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) != OB_SUCCESS);
    }
    CHECK(session->store_query_string(ObString()) == OB_SUCCESS);
    std::cout << "PASS: native name-only and exact-signature GRANT/REVOKE resolution, owned wire targets, stale identity checks and typed/nonzero ACL write gate (no durable grant claim)" << std::endl;
    std::cout << "PASS: parsed native DCL uses copied object/version privilege requirements, per-right grant options, current ACL, private revoke/savepoint and stale-target rejection; no durable DCL claims" << std::endl;
    std::cout << "PASS: native DROP exact signatures, IF EXISTS misses, owned target wire and stale/rebound identity rejection; no Root deletion claims" << std::endl;
    resolve("SELECT native_db.over_geo(1,2)", OB_SUCCESS, 312001);
    resolve("SELECT native_db.over_geo(POINT(0,0),POINT(3,4))", OB_SUCCESS, 312003);
    resolve("SELECT native_db.over_geo(POINT(0,0),POINT(3,4),POINT(6,8))", OB_SUCCESS, 312002);
    resolve("SELECT native_db.over_geo(NULL,NULL)", OB_ERR_FUNC_DUP);
    resolve("SELECT native_db.over_defaults(1)", OB_ERR_FUNC_DUP);
    resolve("SELECT native_db.over_defaults(1,2)", OB_ERR_FUNC_DUP);
    resolve("SELECT native_db.over_defaults(1,2,3)", OB_SUCCESS, 312005);
    resolve("SELECT over_geo(1,2)", OB_ERR_FUNCTION_UNKNOWN);
    {
      ParamStore placeholders((ObWrapperAllocator(arena)));
      for (int i = 0; i < 3; ++i) {
        ObObjParam value; value.set_null(); value.set_param_meta();
        CHECK(placeholders.push_back(value) == OB_SUCCESS);
      }
      auto *saved_parameters = resolver_params.param_list_;
      resolver_params.param_list_ = &placeholders;
      resolver_params.is_prepare_protocol_ = true;
      resolve("SELECT native_db.over_geo(?,?)", OB_ERR_FUNC_DUP);
      resolve("SELECT native_db.over_geo(?,POINT(3,4))", OB_SUCCESS, 312003);
      resolver_params.is_prepare_protocol_ = false;
      resolver_params.param_list_ = saved_parameters;
    }
    struct NativeOverloadCall { std::string expression; double value; uint64_t id; };
    oceanbase::share::ObPackedObjPriv native_execute = 0;
    CHECK(oceanbase::share::ObPrivPacker::raw_obj_priv_to_packed_info(
        NO_OPTION, OBJ_PRIV_ID_EXECUTE, native_execute) == OB_SUCCESS);
    // A grant on another overload must not authorize any of the calls below.
    CHECK(MockSchemaService::grant_object(*manager, 312004, 124, 123, native_execute) == OB_SUCCESS);
    for (const auto &test : std::vector<NativeOverloadCall>{
        {"ST_X(native_db.over_geo(1,2))", 1, 312001},
        {"ST_Length(native_db.over_geo(POINT(0,0),POINT(3,4)))", 5, 312003},
        {"ST_Length(native_db.over_geo(POINT(0,0),POINT(3,4),POINT(6,8)))", 10, 312002},
        {"ST_Y(native_db.over_defaults(1,2,3))", 2, 312005}}) {
      const auto text = "SELECT " + test.expression;
      ObParser parser(arena, session->get_sql_mode()); ParseResult parsed{};
      CHECK(parser.parse(ObString(text.size(), text.data()), parsed) == OB_SUCCESS);
      ObSelectResolver resolver(resolver_params);
      CHECK(resolver.resolve(*parsed.result_tree_->children_[0]) == OB_SUCCESS);
      auto *expression = resolver.get_select_stmt()->get_select_item(0).expr_;
      ObExecContext context(arena); context.set_my_session(session.get()); context.set_sql_ctx(&sql);
      context.set_sql_proxy(&proxy); context.set_runtime_services(execution.get_runtime_services());
      CHECK(context.create_physical_plan_ctx() == OB_SUCCESS);
      ObSQLSessionInfo::ExecCtxSessionRegister enter(*session, &context);
      ObRawExprUniqueSet roots(false); CHECK(roots.append(expression) == OB_SUCCESS);
      ObStaticEngineExprCG generator(arena, session.get(), &guard, 0, 0);
      ObExprFrameInfo frame(arena); CHECK(generator.generate(roots, frame) == OB_SUCCESS);
      CHECK(context.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
      CHECK(frame.pre_alloc_exec_memory(context) == OB_SUCCESS);
      ObExpr *root = nullptr; ObSEArray<ObRawExpr *, 1> outputs;
      CHECK(ObStaticEngineExprCG::generate_rt_expr(*expression, outputs, root) == OB_SUCCESS);
      ObEvalCtx eval(context); ObDatum *value = nullptr;
      CHECK(root->eval(eval, value) == OB_ERR_NO_ROUTINE_PRIVILEGE);
      CHECK(MockSchemaService::grant_object(*manager, test.id, 124, 123, native_execute) == OB_SUCCESS);
      for (auto &expr : frame.rt_exprs_) expr.get_eval_info(eval).evaluated_ = false;
      CHECK(root->eval(eval, value) == OB_SUCCESS && value && !value->is_null() && value->get_double() == test.value);
      const ObRoutineInfo *target = nullptr;
      CHECK(guard.get_routine_info(test.id, target) == OB_SUCCESS && target);
      {
        RoutineCatalogSavepoint private_revoke(overlay, native_privileges); CHECK(private_revoke.valid());
        CHECK(native_privileges->record_object_change(*target, 124, 123, 43, native_execute, 0) == OB_SUCCESS);
        for (auto &expr : frame.rt_exprs_) expr.get_eval_info(eval).evaluated_ = false;
        CHECK(root->eval(eval, value) == OB_ERR_NO_ROUTINE_PRIVILEGE);
        CHECK(private_revoke.rollback() == OB_SUCCESS);
        for (auto &expr : frame.rt_exprs_) expr.get_eval_info(eval).evaluated_ = false;
        CHECK(root->eval(eval, value) == OB_SUCCESS && value->get_double() == test.value);
      }
      CHECK(MockSchemaService::revoke_object(*manager, test.id, 124, 123) == OB_SUCCESS);
      {
        RoutineCatalogSavepoint private_grant(overlay, native_privileges); CHECK(private_grant.valid());
        CHECK(native_privileges->record_object_change(*target, 124, 123, 44, 0, native_execute) == OB_SUCCESS);
        for (auto &expr : frame.rt_exprs_) expr.get_eval_info(eval).evaluated_ = false;
        CHECK(root->eval(eval, value) == OB_SUCCESS && value->get_double() == test.value);
      }
      for (auto &expr : frame.rt_exprs_) expr.get_eval_info(eval).evaluated_ = false;
      CHECK(root->eval(eval, value) == OB_ERR_NO_ROUTINE_PRIVILEGE);
      ObSQLSessionInfo::ExecCtxSessionRegister leave(*session, &execution);
    }
    std::cout << "PASS: nonzero native SQL execution requires exact object grant, rejects shared-name/other-overload grants and rechecks revoke on cached expression; controlled ACL cache, no SQL GRANT claims" << std::endl;
    std::cout << "PASS: private object grant/revoke and savepoint rollback affect reused native SQL expressions through the real GIS DSO; controlled views, no commit claims" << std::endl;
    CHECK(overlay->erase(100, ObString::make_string("over_geo"), ROUTINE_FUNCTION_TYPE, 312003, 55) == OB_SUCCESS);
    resolve("SELECT native_db.over_geo(POINT(0,0),POINT(3,4))", OB_SUCCESS, 312002);
    CHECK(overlay->erase(100, ObString::make_string("over_defaults"), ROUTINE_FUNCTION_TYPE, 312005, 19) == OB_SUCCESS);
    CHECK(resolve("SELECT native_db.over_defaults(1)", OB_SUCCESS, 312004)->get_param_count() == 2);
    std::cout << "PASS: real SQL native overload identity/selection/execution, sparse slots, defaults ambiguity, fixed-vs-variadic and independent drop; no persistent installation or object ACL claims" << std::endl;

    // The actual implementation-only module publishes no SQL function names.
    // A separate database deliberately has no declarations.
    CHECK(session->set_user(ObString::make_string("owner"), ObString::make_string("localhost"), 124) == OB_SUCCESS);
    session->set_priv_user_id(124); session->set_user_priv_set(OB_PRIV_SUPER | OB_PRIV_CREATE_ROUTINE);
    const char *constructors[] = {"point", "linestring", "polygon", "multipoint",
        "multilinestring", "multipolygon", "geometrycollection", "geomcollection"};
    for (int i = 0; i < 8; ++i) {
      const std::string name = constructors[i];
      create_default("CREATE FUNCTION native_db.`" + name + "`(" +
          (i == 0 ? "x DOUBLE, y DOUBLE" : "VARIADIC elements GEOMETRY[]") +
          ") RETURNS GEOMETRY DETERMINISTIC NO SQL SQL SECURITY INVOKER "
          "AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_" + name + "' LANGUAGE C", OB_SUCCESS);
      point.set_routine_id(311000 + i); point.set_schema_version(42);
      for (int64_t j = 0; j < point.get_routine_params().count(); ++j) {
        auto *parameter = point.get_routine_params().at(j);
        parameter->set_routine_id(311000 + i); parameter->set_schema_version(42);
      }
      CHECK(overlay->stage(point) == OB_SUCCESS);
      grant_native(constructors[i], true);
    }
    create_default("CREATE FUNCTION native_db.ST_Length(g GEOMETRY) RETURNS DOUBLE DETERMINISTIC "
        "NO SQL SQL SECURITY INVOKER AS 'org.seekdb.gis', 'org.seekdb.gis.function.st_length' LANGUAGE C", OB_SUCCESS);
    point.set_routine_id(311008); point.set_schema_version(42);
    for (int64_t j = 0; j < point.get_routine_params().count(); ++j) {
      auto *parameter = point.get_routine_params().at(j);
      parameter->set_routine_id(311008); parameter->set_schema_version(42);
    }
    CHECK(overlay->stage(point) == OB_SUCCESS);
    grant_native("st_length", true);
    CHECK(session->set_user(ObString::make_string("caller"), ObString::make_string("localhost"), 123) == OB_SUCCESS);
    session->set_priv_user_id(123); session->set_user_priv_set(OB_PRIV_INSERT);
    CHECK(session->set_default_database(ObString::make_string("empty_db")) == OB_SUCCESS);
    session->set_database_id(102);
    for (int i = 0; i < 8; ++i) {
      const std::string text = std::string("SELECT ") + constructors[i] + (i == 0 ? "(0,0)" : "(NULL)");
      resolve(text.c_str(), OB_ERR_FUNCTION_UNKNOWN, 311000 + i);
    }
    resolve("SELECT native_db.point(0,0)", OB_SUCCESS, 311000);
    CHECK(session->set_default_database(ObString::make_string("native_db")) == OB_SUCCESS);
    session->set_database_id(100);
    for (int i = 0; i < 8; ++i) {
      const std::string text = std::string("SELECT ") + constructors[i] + (i == 0 ? "(0,0)" : "(NULL)");
      resolve(text.c_str(), OB_SUCCESS, 311000 + i);
    }
    resolve("SELECT GEOMETRYCOLLECTION()", OB_ERR_SP_WRONG_ARG_NUM, 311006);
    resolve("SELECT GEOMCOLLECTION()", OB_ERR_SP_WRONG_ARG_NUM, 311007);
    auto *keyword_call = resolve("SELECT ST_Length(LINESTRING(POINT(0,0),POINT(3,4)))", OB_SUCCESS, 311008);
    ObExecContext keyword_execution(arena);
    keyword_execution.set_my_session(session.get()); keyword_execution.set_sql_ctx(&sql);
    keyword_execution.set_sql_proxy(&proxy);
    keyword_execution.set_runtime_services(execution.get_runtime_services());
    CHECK(keyword_execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister enter_keyword(*session, &keyword_execution);
    ObRawExprUniqueSet keyword_roots(false); CHECK(keyword_roots.append(keyword_call) == OB_SUCCESS);
    ObStaticEngineExprCG keyword_generator(arena, session.get(), &guard, 0, 0);
    ObExprFrameInfo keyword_frame(arena);
    CHECK(keyword_generator.generate(keyword_roots, keyword_frame) == OB_SUCCESS);
    CHECK(keyword_execution.init_expr_op(keyword_frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(keyword_frame.pre_alloc_exec_memory(keyword_execution) == OB_SUCCESS);
    ObExpr *keyword_root = nullptr; ObSEArray<ObRawExpr *, 1> keyword_outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*keyword_call, keyword_outputs, keyword_root) == OB_SUCCESS);
    ObEvalCtx keyword_eval(keyword_execution); ObDatum *keyword_value = nullptr;
    CHECK(keyword_root->eval(keyword_eval, keyword_value) == OB_SUCCESS);
    CHECK(keyword_value && !keyword_value->is_null() && keyword_value->get_double() == 5);
    CHECK(session->get_database_id() == 100 && session->get_priv_user_id() == 123);
    // A cached expression must still enforce EXECUTE on the nested constructor.
    grant_native("linestring", false);
    for (auto &expr : keyword_frame.rt_exprs_) expr.get_eval_info(keyword_eval).evaluated_ = false;
    CHECK(keyword_root->eval(keyword_eval, keyword_value) == OB_ERR_NO_ROUTINE_PRIVILEGE);
    CHECK(overlay->erase(100, ObString::make_string("point"), ROUTINE_FUNCTION_TYPE, 311000) == OB_SUCCESS);
    resolve("SELECT POINT(0,0)", OB_ERR_FUNCTION_UNKNOWN, 311000);
    CHECK(overlay->erase(100, ObString::make_string("geometrycollection"), ROUTINE_FUNCTION_TYPE, 311006) == OB_SUCCESS);
    resolve("SELECT GEOMETRYCOLLECTION(NULL)", OB_ERR_FUNCTION_UNKNOWN, 311006);
    resolve("SELECT GEOMCOLLECTION(NULL)", OB_SUCCESS, 311007);
    CHECK(session->set_default_database(ObString::make_string("caller_db")) == OB_SUCCESS);
    session->set_database_id(101);
    ObSQLSessionInfo::ExecCtxSessionRegister leave_keyword(*session, &execution);
    std::cout << "PASS: GIS keyword constructors use database routine identity, visibility, execution and ACL without module SQL names" << std::endl;
  }
  grant_native("area_alias", true); grant_native("packaged_area", true);
  for (auto *scalar_raw : {raw, packaged_raw}) {
    ObExecContext scalar_execution(arena);
    scalar_execution.set_my_session(session.get()); scalar_execution.set_sql_ctx(&sql);
    scalar_execution.set_sql_proxy(&proxy);
    scalar_execution.set_runtime_services(execution.get_runtime_services());
    CHECK(scalar_execution.create_physical_plan_ctx() == OB_SUCCESS);
    ObSQLSessionInfo::ExecCtxSessionRegister enter(*session, &scalar_execution);
    ObRawExprUniqueSet scalar_roots(false); CHECK(scalar_roots.append(scalar_raw) == OB_SUCCESS);
    ObStaticEngineExprCG scalar_generator(arena, session.get(), &guard, 0, 0);
    ObExprFrameInfo scalar_frame(arena);
    CHECK(scalar_generator.generate(scalar_roots, scalar_frame) == OB_SUCCESS);
    CHECK(scalar_execution.init_expr_op(scalar_frame.need_ctx_cnt_) == OB_SUCCESS);
    CHECK(scalar_frame.pre_alloc_exec_memory(scalar_execution) == OB_SUCCESS);
    ObExpr *scalar_root = nullptr;
    ObSEArray<ObRawExpr *, 1> outputs;
    CHECK(ObStaticEngineExprCG::generate_rt_expr(*scalar_raw, outputs, scalar_root) == OB_SUCCESS);
    ObEvalCtx scalar_eval(scalar_execution); ObDatum *result = nullptr;
    const int status = scalar_root->eval(scalar_eval, result);
    std::cout << "native parsed SQL execution: status=" << status << std::endl;
    CHECK(status == OB_SUCCESS);
    CHECK(result && !result->is_null() && result->get_double() == 12);
    CHECK(session->get_database_id() == 101 && session->get_priv_user_id() == 123);
    ObSQLSessionInfo::ExecCtxSessionRegister leave(*session, &execution);
  }
  grant_native("area_alias", false); grant_native("packaged_area", false);
  raw = resolve("SELECT native_db.area_alias(NULL)", OB_SUCCESS);
  // Use controlled column input below to inspect exactly-once argument
  // evaluation and caller identity, retaining the resolved catalog signature.
  CHECK(raw->replace_param_expr(0, column) == OB_SUCCESS);
  CHECK(raw->formalize(session.get()) == OB_SUCCESS);
  ObRawExprUniqueSet roots(false); CHECK(roots.append(raw) == OB_SUCCESS);
  ObStaticEngineExprCG generator(arena, session.get(), &guard, 0, 0);
  if (batch) generator.set_batch_size(rows);
  ObExprFrameInfo frame(arena);
  CHECK(generator.generate(roots, frame) == OB_SUCCESS);
  CHECK(execution.init_expr_op(frame.need_ctx_cnt_) == OB_SUCCESS);
  CHECK(frame.pre_alloc_exec_memory(execution) == OB_SUCCESS);
  ObExpr *root = nullptr, *input = nullptr;
  ObSEArray<ObRawExpr *, 1> outputs;
  CHECK(ObStaticEngineExprCG::generate_rt_expr(*raw, outputs, root) == OB_SUCCESS);
  CHECK(root->type_ == T_FUN_UDF && root->arg_cnt_ == 1); // No dispatch-name / RETURN wrapper.
  for (auto &expr : frame.rt_exprs_) if (expr.type_ == T_REF_COLUMN) input = &expr;
  CHECK(input);
  auto *metadata = dynamic_cast<ObExprUDFInfo *>(root->extra_info_);
  CHECK(metadata && metadata->native_ && metadata->native_schema_version_ == 42);
  // Cached/distributed expression data must own all binding identifiers.
  std::vector<char> wire(metadata->get_serialize_size()); int64_t pos = 0;
  CHECK(metadata->serialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == wire.size());
  ObExprUDFInfo decoded(arena, T_FUN_UDF); pos = 0;
  CHECK(decoded.deserialize(wire.data(), wire.size(), pos) == OB_SUCCESS && pos == wire.size());
  std::fill(wire.begin(), wire.end(), '\0');
  ObIExprExtraInfo *copied = nullptr;
  CHECK(decoded.deep_copy(arena, T_FUN_UDF, copied) == OB_SUCCESS);
  root->extra_info_ = copied;
  ObEvalCtx eval(execution);
  auto *skip = to_bit_vector(arena.alloc(ObBitVector::memory_size(rows))); CHECK(skip); skip->reset(rows);
  if (batch) skip->set(1);
  std::vector<char> geometry(sizeof(ObLobCommon) + 98, 0);
  auto *data = (new (geometry.data()) ObLobCommon())->buffer_;
  data[4] = 1; data[5] = 1; data[6] = 3; data[10] = 1; data[14] = 5;
  const double coordinates[] = {0, 0, 4, 0, 4, 3, 0, 3, 0, 0};
  std::memcpy(data + 18, coordinates, sizeof(coordinates));
  Input argument{ObString(geometry.size(), geometry.data())};
  active_input = &argument;
  input->eval_func_ = evaluate_input;
  if (batch) input->eval_batch_func_ = expr_default_eval_batch_func;
  const auto invoke = [&](bool null_value = false) {
    argument.null = null_value;
    const int old_calls = argument.calls;
    root->get_eval_info(eval).evaluated_ = false;
    if (batch) root->get_evaluated_flags(eval).reset(rows);
    if (batch) input->get_evaluated_flags(eval).reset(rows);
    input->get_eval_info(eval).evaluated_ = input->get_eval_info(eval).projected_ = false;
    ObDatum *value = nullptr;
    const int status = batch ? root->eval_batch(eval, *skip, rows) : root->eval(eval, value);
    std::cout << "native invocation: definer=" << definer << " batch=" << batch
              << " null=" << null_value << " status=" << status << std::endl;
    CHECK(session->get_database_id() == 101 && session->get_database_name() == ObString::make_string("caller_db"));
    CHECK(session->get_priv_user_id() == 123 && session->get_user_priv_set() == OB_PRIV_INSERT);
    CHECK(session->get_db_priv_set() == OB_PRIV_SELECT && session->get_local_autocommit());
    if (status == OB_SUCCESS || status == OB_TIMEOUT) CHECK(argument.calls - old_calls == (batch ? rows - 1 : 1));
    else CHECK(argument.calls == old_calls);
    if (status == OB_SUCCESS) for (int i = 0; i < (batch ? rows : 1); ++i) {
      if (batch && skip->at(i)) continue;
      const auto &result = batch ? root->locate_batch_datums(eval)[i] : *value;
      if (null_value || (batch && i == 2)) CHECK(result.is_null());
      else CHECK(!result.is_null() && result.get_double() == 12);
    }
    return status;
  };
  provider.native_session_ = session.get(); provider.native_user_ = definer ? 124 : 123;
  const int before = provider.native_calls_;
  CHECK(invoke() == OB_ERR_NO_ROUTINE_PRIVILEGE && provider.native_calls_ == before);
  CHECK(MockSchemaService::grant(*manager, 123, "area_alias", OB_PRIV_EXECUTE, "native_db") == OB_SUCCESS);
  CHECK(invoke() == OB_ERR_NO_ROUTINE_PRIVILEGE); // A historical name grant cannot authorize native slot zero.
  grant_native("area_alias", true);
  CHECK(invoke() == OB_SUCCESS && provider.native_calls_ > before);
  CHECK(invoke(true) == OB_SUCCESS);
  provider.native_failure_ = OB_TIMEOUT;
  CHECK(invoke() == OB_TIMEOUT);
  provider.native_failure_ = OB_SUCCESS;
  routine.set_schema_version(43); CHECK(overlay->stage(routine) == OB_SUCCESS);
  CHECK(invoke() == OB_SCHEMA_EAGAIN);
  routine.set_schema_version(42); CHECK(overlay->stage(routine) == OB_SUCCESS);
  grant_native("area_alias", false);
  CHECK(invoke(true) == OB_ERR_NO_ROUTINE_PRIVILEGE); // NULL cannot bypass ACL.
  provider.native_session_ = nullptr;
  active_input = nullptr;
  CHECK(provider.legacy_calls_ == 0);
  std::cout << "PASS: native UDF " << (batch ? "batch" : "scalar") << " "
            << (definer ? "definer" : "invoker") << ", catalog identity, ACL and scope restoration" << std::endl;
}
}
