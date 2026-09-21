// Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0.
// Real CREATE/PL body semantic resolution; controlled schemas and publication.
// No server execution, authenticated connection, Root transaction or rollback.
#ifndef SEEKDB_TEST_ROUTINE_CREATE_FIXTURE_H_
#define SEEKDB_TEST_ROUTINE_CREATE_FIXTURE_H_
#include "sql/pl/ob_pl_router.h"

namespace routine_create_test {
using namespace oceanbase::common;
using namespace oceanbase::sql;
using namespace oceanbase::share::schema;
using namespace oceanbase::share::plugin;

class NoSqlExecution final : public ObIPLSqlRuntime
{
public:
  int calls_ = 0;
  int prepare_pl_sql(const ObString &, ObSPIService::PLPrepareCtx &,
                     ObSPIService::PLPrepareResult &, ParamStore *) override
  { ++calls_; return OB_NOT_SUPPORTED; }
  int execute_pl_sql(const ObString &, ObSQLSessionInfo &, ParamStore &, ObResultSet &,
                     ObSqlCtx &, bool, bool) override
  { ++calls_; return OB_NOT_SUPPORTED; }
};

inline void run(const char *root, const ObResolverParams &outer_services, const ObSqlCtx &outer,
                const char *native_package = nullptr, bool in_memory = false,
                ObPluginLoader *catalog_loader = nullptr, bool composed = false, bool dynamic_builder = false)
{
  auto &session = *outer.session_info_;
  ObExecContext *exec = session.get_cur_exec_ctx();
  CHECK(exec != nullptr);
  auto *original_physical = exec->get_physical_plan_ctx();
  struct RestoreSession {
    ObSQLSessionInfo &session_; ObPrivSet user_, db_; ObSQLMode mode_;
    ~RestoreSession() {
      session_.set_user_priv_set(user_); session_.set_db_priv_set(db_); session_.set_sql_mode(mode_);
    }
  } restore{session, session.get_user_priv_set(), session.get_db_priv_set(), session.get_sql_mode()};
  session.set_user_priv_set(OB_PRIV_CREATE_ROUTINE);
  session.set_db_priv_set(0);
  auto service = std::make_unique<routine_reservation_test::VersionService>();
  auto allocator = std::make_unique<routine_version_test::Allocator>(*outer_services.sql_proxy_, *service);
  service->bind_sql(*allocator);
  routine_version_test::Transaction transaction;
  auto manager = std::make_unique<ObSchemaMgr>();
  CHECK(manager->init() == OB_SUCCESS);
  CHECK(MockSchemaService::set_name_case_mode(*manager, OB_ORIGIN_AND_INSENSITIVE) == OB_SUCCESS);
  ObSimpleServerRuntimeSchema runtime_schema;
  runtime_schema.set_schema_version(42);
  runtime_schema.set_name_case_mode(OB_ORIGIN_AND_INSENSITIVE);
  runtime_schema.set_status(SERVER_RUNTIME_STATUS_NORMAL);
  CHECK(runtime_schema.set_runtime_name(ObString::make_string("fixture")) == OB_SUCCESS);
  CHECK(manager->add_runtime_schema(runtime_schema) == OB_SUCCESS);
  ObUserInfo user;
  user.set_user_id(123);
  user.set_schema_version(42);
  CHECK(user.set_user_name("fixture") == OB_SUCCESS && user.set_host("localhost") == OB_SUCCESS);
  ObSimpleUserSchema simple_user;
  simple_user.set_user_id(123);
  simple_user.set_schema_version(42);
  CHECK(simple_user.set_user_name(user.get_user_name_str()) == OB_SUCCESS);
  CHECK(simple_user.set_host(user.get_host_name_str()) == OB_SUCCESS);
  CHECK(manager->add_user(simple_user) == OB_SUCCESS);
  ObDatabaseSchema database;
  database.set_database_id(OB_SYS_DATABASE_ID);
  database.set_schema_version(42);
  CHECK(database.set_database_name(OB_SYS_DATABASE_NAME) == OB_SUCCESS);
  ObSchemaGetterGuard guard;
  CHECK(MockSchemaService::bind(guard, *service, *manager) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_user(guard, user) == OB_SUCCESS);
  CHECK(MockSchemaService::cache_database(guard, database) == OB_SUCCESS);
  auto privileges = std::make_shared<RoutinePrivilegeOverlay>(OB_SYS_DATABASE_ID, 123);
  auto overlay = std::make_shared<RoutineSchemaOverlay>(privileges);
  CHECK(guard.attach_routine_overlay(overlay) == OB_SUCCESS);
  NoSqlExecution runtime;
  auto engine = std::make_unique<oceanbase::pl::ObPL>();
  ObResolverParams services;
  services.session_info_ = outer.session_info_;
  services.sql_proxy_ = outer_services.sql_proxy_;
  services.pl_sql_runtime_ = &runtime;
  services.pl_engine_ = engine.get();
  ObSqlCtx context;
  context.session_info_ = outer.session_info_;
  context.schema_guard_ = &guard;
  context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
  ExtensionScript script;
  std::string error;
  if (dynamic_builder) {
    using oceanbase::rootserver::RoutineIdReservation;
    using oceanbase::rootserver::RoutineVersionReservation;
    // A pre-existing procedure in the base guard, not the transaction overlay.
    ObRoutineInfo existing_procedure;
    existing_procedure.set_database_id(OB_SYS_DATABASE_ID);
    existing_procedure.set_routine_id(7777001);
    existing_procedure.set_schema_version(42);
    existing_procedure.set_owner_id(123);
    existing_procedure.set_package_id(OB_INVALID_ID);
    existing_procedure.set_overload(0);
    existing_procedure.set_routine_type(ROUTINE_PROCEDURE_TYPE);
    CHECK(existing_procedure.set_routine_name(ObString::make_string("catalog_existing_procedure")) == OB_SUCCESS);
    CHECK(MockSchemaService::add(*manager, OB_SYS_DATABASE_ID, "catalog_existing_procedure", 7777001,
        ROUTINE_PROCEDURE_TYPE, 42) == OB_SUCCESS);
    CHECK(MockSchemaService::cache_routine(guard, existing_procedure) == OB_SUCCESS);
    ExtensionPackageSource source;
    source.name_ = "builder_ops"; source.version_ = "1";
    source.native_module_ = "org.seekdb.rust-text"; source.native_install_ = true;
    CHECK(script.load_source(source, session.get_sql_mode(), error) == OB_SUCCESS && script.statements().empty());
    if (catalog_loader) {
      CHECK(script.load(root, "rust_text_built", "1.0", session.get_sql_mode(), error) == OB_SUCCESS);
      source = script.source();
      CHECK(source.native_install_ && source.scripts_.empty() && script.statements().empty());
    }
    ExtensionInstallSpec spec{1, OB_SYS_DATABASE_ID, 123, source.name_, source.version_, source.native_module_, {}, {}};
    class Program final : public ICatalogBuildProgram {
    public:
      Program(int scenario, ObSQLSessionInfo &session) : scenario_(scenario), name_("builder_" + std::to_string(scenario)), session_(session) {}
      int scenario_, runs_ = 0;
      std::string name_;
      ObSQLSessionInfo &session_;
      uint64_t first_ = 0, second_ = 0;
      int preflight(const ExtensionInstallSpec &spec, std::string &) override {
        CHECK(spec.owner_id_ == 123 && spec.name_ == "builder_ops"); return OB_SUCCESS;
      }
      int build(ICatalogRoutineBuilder &builder, std::string &error) override {
        ++runs_;
        if (scenario_ == 0) {
          uint64_t found = 0;
          CHECK(builder.lookup_routine(CatalogRoutineKind::PROCEDURE, "CATALOG_EXISTING_PROCEDURE", found, error) == OB_SUCCESS && found == 7777001);
          CHECK(builder.lookup_routine(CatalogRoutineKind::FUNCTION, "catalog_existing_procedure", found, error) == OB_SUCCESS && found == 0);
        }
        if (scenario_ == 5) throw std::bad_alloc();
        const std::string first = "CREATE FUNCTION " + name_ + "() RETURNS INT RETURN 7;";
        if (scenario_ == 1 || scenario_ == 4 || scenario_ == 8) {
          const std::string invalid = scenario_ == 1 ? "SELECT 1;" : scenario_ == 4 ? first + first : std::string(4 * 1024 * 1024 + 1, 'x');
          uint64_t id = 999;
          const int ret = builder.create_routine(invalid, id, error);
          CHECK(ret != OB_SUCCESS && id == 0);
          id = 999;
          CHECK(builder.create_routine(first, id, error) == ret && id == 0);
          return OB_SUCCESS; // Host must not let a program swallow a build error.
        }
        int ret = builder.create_routine(first, first_, error);
        if (ret == OB_SUCCESS) {
          CHECK(first_ != 0);
          if (scenario_ == 0 || scenario_ == 6 || scenario_ >= 9) {
            uint64_t found = 999;
            CHECK(builder.lookup_routine(CatalogRoutineKind::FUNCTION,
                scenario_ == 0 ? "BUILDER_0" : name_, found, error) == OB_SUCCESS && found == first_);
            CHECK(builder.lookup_routine(CatalogRoutineKind::PROCEDURE, name_, found, error) == OB_SUCCESS && found == 0);
            CHECK(builder.lookup_routine(CatalogRoutineKind::FUNCTION, "missing_builder_routine", found, error) == OB_SUCCESS && found == 0);
          }
          // Branch/name construction depends on the actually reserved ID, and
          // the body resolves the just-created function in the local view.
          ret = builder.create_routine("CREATE FUNCTION built_after_" + std::to_string(first_) +
              "() RETURNS INT RETURN " + name_ + "();", second_, error);
        }
        if (ret == OB_SUCCESS) CHECK(second_ != 0 && second_ != first_);
        if (ret == OB_SUCCESS && scenario_ >= 9) {
          uint64_t found = 999;
          const std::string invalid = scenario_ == 9 ? "" : scenario_ == 10 ? std::string("f\0g", 3) :
              scenario_ == 11 ? std::string(1, char(0xff)) : scenario_ == 12 ? std::string(2049, 'x') : name_;
          if (scenario_ == 14) session_.set_user_priv_set(0);
          const auto kind = scenario_ == 13 ? static_cast<CatalogRoutineKind>(999) : CatalogRoutineKind::FUNCTION;
          ret = builder.lookup_routine(kind, invalid, found, error);
          CHECK(ret != OB_SUCCESS && found == 0);
          CHECK(builder.lookup_routine(CatalogRoutineKind::FUNCTION, name_, found, error) == ret && found == 0);
          CHECK(builder.create_routine(first, found, error) == ret && found == 0);
          return OB_SUCCESS; // Lookup errors must also poison construction.
        }
        return scenario_ == 6 ? OB_TIMEOUT : ret;
      }
    };
    for (int scenario = 0; scenario < (catalog_loader ? 1 : 15); ++scenario) {
      Program program(scenario, session);
      std::unique_ptr<ICatalogDeclarations> declarations;
      if (catalog_loader) {
        CHECK(catalog_loader->prepare_catalog_install(source, 1, OB_SYS_DATABASE_ID, 123, declarations) == OB_SUCCESS);
        CHECK(declarations && declarations->sql().empty() && declarations->program());
      }
      ExtensionRoutineScriptResolver sequence(script, spec, services, context,
          declarations ? declarations->program() : &program);
      CHECK(sequence.has_builder() && sequence.preflight_install(spec, error) == OB_SUCCESS);
      std::vector<std::unique_ptr<RoutineIdReservation>> ids;
      std::vector<std::unique_ptr<RoutineVersionReservation>> versions;
      std::vector<const ExtensionRoutineUpdateOperation *> operations;
      int stages = 0;
      const IExtensionRoutineScript::StageRoutine stage = [&](const ExtensionRoutineUpdateOperation &op, uint64_t &id) {
        CHECK(transaction.is_started() && id == 0);
        if (catalog_loader) {
          ObPluginStatusSnapshot status;
          CHECK(catalog_loader->get_status(source.native_module_, status) == OB_SUCCESS && status.lease_count_ == 1);
        }
        CHECK(op.kind_ == ExtensionRoutineUpdateOperation::Kind::CREATE && op.create_arg_);
        ++stages;
        if (scenario == 2) return OB_TIMEOUT;
        if (scenario == 3) return OB_SUCCESS; // Invalid stage: no reserved ID.
        const auto &arg = *op.create_arg_;
        CHECK(arg.routine_info_.get_owner_id() == 123 && arg.routine_info_.get_database_id() == OB_SYS_DATABASE_ID);
        if (!ids.empty()) {
          bool dependency = false, fence = false;
          for (int64_t i = 0; i < arg.dependency_infos_.count(); ++i)
            dependency |= arg.dependency_infos_.at(i).get_ref_obj_id() == ids[0]->id();
          for (int64_t i = 0; i < arg.based_schema_object_infos_.count(); ++i) {
            const auto &ref = arg.based_schema_object_infos_.at(i);
            fence |= ref.schema_id_ == ids[0]->id() && ref.schema_type_ == ROUTINE_SCHEMA &&
                     ref.schema_version_ == versions[0]->version();
          }
          CHECK(dependency && fence);
        }
        ObRoutineInfo staged;
        CHECK(staged.assign(arg.routine_info_) == OB_SUCCESS);
        auto identity = std::make_unique<RoutineIdReservation>();
        CHECK(RoutineIdReservation::reserve(*allocator, staged, *identity) == OB_SUCCESS);
        staged.set_routine_id(identity->id());
        auto version = std::make_unique<RoutineVersionReservation>();
        CHECK(RoutineVersionReservation::reserve(*service, transaction, staged, nullptr, *version) == OB_SUCCESS);
        staged.set_schema_version(version->version());
        for (int64_t i = 0; i < staged.get_routine_params().count(); ++i) {
          staged.get_routine_params().at(i)->set_routine_id(identity->id());
          staged.get_routine_params().at(i)->set_schema_version(version->version());
        }
        CHECK(overlay->stage(staged) == OB_SUCCESS && privileges->record_create(staged, true) == OB_SUCCESS);
        id = identity->id();
        ids.push_back(std::move(identity)); versions.push_back(std::move(version)); operations.push_back(&op);
        return OB_SUCCESS;
      };
      if (scenario == 7) session.set_user_priv_set(0);
      const int ret = oceanbase::query::serialize_root_service_call([&]() {
        int code = resolve_extension_routine_sequence(sequence, 0, guard,
            [](const ExtensionRoutineUpdateOperation &) { CHECK(false); return OB_ERR_UNEXPECTED; }, error);
        if (code == OB_SUCCESS) code = sequence.build(guard, stage, error);
        return code;
      });
      session.set_user_priv_set(OB_PRIV_CREATE_ROUTINE);
      const int expected = scenario == 0 ? OB_SUCCESS : scenario == 1 ? OB_NOT_SUPPORTED :
          scenario == 2 || scenario == 6 ? OB_TIMEOUT : scenario == 3 ? OB_ERR_UNEXPECTED :
          scenario == 4 ? OB_INVALID_ARGUMENT : scenario == 5 ? OB_ALLOCATE_MEMORY_FAILED :
          scenario == 8 ? OB_SIZE_OVERFLOW : scenario == 11 ? OB_ERR_INCORRECT_STRING_VALUE :
          scenario == 14 ? OB_ERR_NO_PRIVILEGE : scenario >= 9 ? OB_INVALID_ARGUMENT : ret;
      if (ret != expected) std::cerr << "builder scenario=" << scenario << " ret=" << ret << " " << error << std::endl;
      CHECK(ret == expected && ((scenario != 7 && scenario < 9) || ret != OB_SUCCESS));
      CHECK(program.runs_ == (catalog_loader ? 0 : 1));
      if (scenario == 0 || scenario == 6 || scenario >= 9) {
        CHECK(stages == 2 && operations.size() == 2);
        if (catalog_loader) {
          CHECK(operations[0]->create_arg_->routine_info_.get_routine_name() == "rust_built_length");
          CHECK(operations[1]->create_arg_->routine_info_.get_routine_name() == "rust_built_nonempty");
        } else {
          CHECK(operations[0]->create_arg_->routine_info_.get_routine_name() == ObString(program.name_.size(), program.name_.data()));
          CHECK(program.first_ == ids[0]->id() && program.second_ == ids[1]->id());
        }
      } else CHECK(stages == (scenario == 2 || scenario == 3 ? 1 : 0));
      CHECK(sequence.build(guard, stage, error) == OB_STATE_NOT_MATCH && program.runs_ == (catalog_loader ? 0 : 1));
    }
    CHECK(runtime.calls_ == 0 && transaction.writes_.empty());
    return; // Real reservation/PL view, not actual Root install/commit/rollback.
  }
  if (composed) {
    using oceanbase::rootserver::RoutineIdReservation;
    using oceanbase::rootserver::RoutineVersionReservation;
    std::vector<std::unique_ptr<RoutineIdReservation>> ids;
    std::vector<std::unique_ptr<RoutineVersionReservation>> versions;
    std::vector<std::unique_ptr<ExtensionRoutineUpdateBatch>> batches;
    auto stage = [&](const ExtensionRoutineUpdateOperation &operation, bool provider) {
      CHECK(operation.kind_ == ExtensionRoutineUpdateOperation::Kind::CREATE && operation.create_arg_);
      auto batch = std::make_unique<ExtensionRoutineUpdateBatch>();
      ObSEArray<ExtensionRoutineUpdateOperation, 1> snapshot;
      CHECK(snapshot.push_back(operation) == OB_SUCCESS && batch->assign(snapshot) == OB_SUCCESS);
      const auto &arg = *batch->operations().at(0).create_arg_;
      CHECK(arg.error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
      if (!provider) {
        CHECK(ids.size() >= 2);
        for (size_t i = 0; i < 2; ++i) {
          bool dependency = false, fence = false;
          for (int64_t j = 0; j < arg.dependency_infos_.count(); ++j) {
            const auto &reference = arg.dependency_infos_.at(j);
            dependency |= reference.get_ref_obj_id() == ids[i]->id() &&
                          reference.get_ref_obj_type() == ObObjectType::FUNCTION;
          }
          for (int64_t j = 0; j < arg.based_schema_object_infos_.count(); ++j) {
            const auto &reference = arg.based_schema_object_infos_.at(j);
            fence |= reference.schema_id_ == ids[i]->id() && reference.schema_type_ == ROUTINE_SCHEMA &&
                     reference.schema_version_ == versions[i]->version();
          }
          CHECK(dependency && fence);
        }
      }
      ObRoutineInfo staged;
      CHECK(staged.assign(arg.routine_info_) == OB_SUCCESS);
      auto identity = std::make_unique<RoutineIdReservation>();
      CHECK(RoutineIdReservation::reserve(*allocator, staged, *identity) == OB_SUCCESS);
      staged.set_routine_id(identity->id());
      auto version = std::make_unique<RoutineVersionReservation>();
      CHECK(RoutineVersionReservation::reserve(*service, transaction, staged, nullptr, *version) == OB_SUCCESS);
      staged.set_schema_version(version->version());
      for (int64_t j = 0; j < staged.get_routine_params().count(); ++j) {
        staged.get_routine_params().at(j)->set_routine_id(identity->id());
        staged.get_routine_params().at(j)->set_schema_version(version->version());
      }
      CHECK(overlay->stage(staged) == OB_SUCCESS && privileges->record_create(staged, true) == OB_SUCCESS);
      ids.push_back(std::move(identity)); versions.push_back(std::move(version)); batches.push_back(std::move(batch));
      return OB_SUCCESS;
    };
    for (bool provider : {true, false}) {
      const char *name = provider ? "text_ops" : "text_composed";
      ExtensionScript source;
      CHECK(source.load(root, name, "1.0", session.get_sql_mode(), error) == OB_SUCCESS);
      CHECK(source.source().native_module_.empty());
      CHECK(source.source().requires_ == (provider ? std::vector<std::string>{} : std::vector<std::string>{"text_ops"}));
      ExtensionInstallSpec spec{1, OB_SYS_DATABASE_ID, 123, name, "1.0", "", {}, source.source().requires_};
      ExtensionRoutineScriptResolver resolver(source, spec, services, context);
      CHECK(resolver.preflight_install(spec, error) == OB_SUCCESS);
      const int status = resolve_extension_routine_sequence(resolver, resolver.statement_count(), guard,
          [&](const ExtensionRoutineUpdateOperation &operation) { return stage(operation, provider); }, error);
      if (status != OB_SUCCESS) std::cerr << "composed install resolution=" << status << " " << error << std::endl;
      CHECK(status == OB_SUCCESS && ids.size() == (provider ? 2 : 3));
    }
    ExtensionUpdatePlan plan;
    CHECK(plan.load(root, 1, OB_SYS_DATABASE_ID, "text_composed", {91, 123, "1.0", ""},
        "1.1", session.get_sql_mode(), error) == OB_SUCCESS);
    CHECK(plan.request().requires_ == (std::vector<std::string>{"text_ops"}));
    ExtensionRoutineScriptResolver update(plan, services, context);
    CHECK(update.preflight(plan.request(), error) == OB_SUCCESS && update.statement_count() == 1);
    CHECK(resolve_extension_routine_sequence(update, update.statement_count(), guard,
        [&](const ExtensionRoutineUpdateOperation &operation) { return stage(operation, false); }, error) == OB_SUCCESS);
    CHECK(ids.size() == 4);
    ExtensionUpdatePlan same;
    CHECK(same.load(root, 1, OB_SYS_DATABASE_ID, "text_composed", {91, 123, "1.1", ""},
        "1.1", session.get_sql_mode(), error) == OB_SUCCESS);
    CHECK(same.request().requires_ == plan.request().requires_ && same.script().statements().empty());
    CHECK(runtime.calls_ == 0 && transaction.writes_.empty());
    return; // Controlled schemas only; no catalog install/commit is emulated.
  }
  if (native_package) {
    CHECK(script.load(root, native_package, "1.0", session.get_sql_mode(), error) == OB_SUCCESS);
    CHECK(script.source().native_module_ == "org.seekdb.rust-text");
    const bool native_only = script.source().native_install_;
    if (native_only) {
      CHECK(script.source().scripts_.empty() && script.statements().empty() && catalog_loader);
      ExtensionInstallSpec unprepared{1, OB_SYS_DATABASE_ID, 123, native_package, "1.0", script.source().native_module_, {}, {}};
      ExtensionRoutineScriptResolver empty_sequence(script, unprepared, services, context);
      CHECK(empty_sequence.preflight_install(unprepared, error) == OB_INVALID_ARGUMENT);
    }
    std::unique_ptr<ICatalogDeclarations> prepared_catalog;
    if (catalog_loader) {
      CHECK(catalog_loader->prepare_catalog_install(script.source(), 1, OB_SYS_DATABASE_ID, 123, prepared_catalog) == OB_SUCCESS);
      CHECK(prepared_catalog && prepared_catalog->sql().size() == 1);
      CHECK(script.append_catalog_declarations(prepared_catalog->sql(), error) == OB_SUCCESS);
    }
    if (in_memory) {
      auto declarations = script.source();
      declarations.requires_ = {"preinstalled"};
      declarations.prerequisites_ = {"migration_provider"};
      // Replace the file's SQL with runtime-built declarations. The native
      // module is already active; no file is rewritten or consulted by load_source.
      declarations.scripts_[0].sql_ =
          "CREATE FUNCTION runtime_native_count(v TEXT) RETURNS BIGINT DETERMINISTIC NO SQL SQL SECURITY INVOKER "
          "RETURN seekdb_rust_char_count(v);\n"
          "CREATE FUNCTION runtime_native_unicode(v TEXT) RETURNS BIGINT DETERMINISTIC NO SQL SQL SECURITY INVOKER "
          "RETURN seekdb_rust_char_count(seekdb_rust_text(v));";
      CHECK(script.load_source(declarations, session.get_sql_mode(), error) == OB_SUCCESS);
    }
    ExtensionInstallSpec spec{1, OB_SYS_DATABASE_ID, 123, native_package, "1.0", script.source().native_module_, {}, script.source().requires_};
    spec.prerequisites_ = script.source().prerequisites_;
    ExtensionRoutineScriptResolver sequence(script, spec, services, context);
    CHECK(sequence.preflight_install(spec, error) == OB_SUCCESS);
    if (in_memory) {
      auto missing_requirements = spec;
      missing_requirements.requires_.clear();
      CHECK(sequence.preflight_install(missing_requirements, error) == OB_INVALID_ARGUMENT);
      ExtensionRoutineScriptResolver missing_dependencies(script, missing_requirements, services, context);
      CHECK(missing_dependencies.preflight_install(missing_requirements, error) == OB_STATE_NOT_MATCH);
      auto missing_prerequisites = spec;
      missing_prerequisites.prerequisites_.clear();
      CHECK(sequence.preflight_install(missing_prerequisites, error) == OB_INVALID_ARGUMENT);
      ExtensionRoutineScriptResolver missing_temporary(script, missing_prerequisites, services, context);
      CHECK(missing_temporary.preflight_install(missing_prerequisites, error) == OB_STATE_NOT_MATCH);
    }
    auto mismatched = spec; mismatched.native_module_id_.clear();
    CHECK(sequence.preflight_install(mismatched, error) == OB_INVALID_ARGUMENT);
    ExtensionRoutineScriptResolver missing_association(script, mismatched, services, context);
    CHECK(missing_association.preflight_install(mismatched, error) == OB_STATE_NOT_MATCH);
    int statements = 0;
    const int ret = oceanbase::query::serialize_root_service_call([&]() {
      return resolve_extension_routine_sequence(sequence, sequence.statement_count(), guard,
          [&](const ExtensionRoutineUpdateOperation &operation) {
            CHECK(operation.kind_ == ExtensionRoutineUpdateOperation::Kind::CREATE && operation.create_arg_);
            const auto &routine = operation.create_arg_->routine_info_;
            CHECK(routine.get_database_id() == OB_SYS_DATABASE_ID && routine.get_owner_id() == 123);
            if (in_memory) CHECK(routine.get_routine_name() ==
                (statements == 0 ? "runtime_native_count" : "runtime_native_unicode"));
            if (catalog_loader) {
              ObPluginStatusSnapshot status;
              CHECK(catalog_loader->get_status("org.seekdb.rust-text", status) == OB_SUCCESS && status.lease_count_ == 1);
              if (native_only) CHECK(routine.get_routine_name() == "rust_native_length");
              else if (statements == 2) CHECK(routine.get_routine_name() == "rust_runtime_length");
            }
            const auto &body = routine.get_routine_body();
            CHECK(std::string(body.ptr(), body.length()).find("seekdb_rust_char_count") != std::string::npos);
            ++statements; return OB_SUCCESS;
          }, error);
    });
    if (ret != OB_SUCCESS) std::cerr << "native routine resolution=" << ret << " " << error << std::endl;
    CHECK(ret == OB_SUCCESS && statements == (native_only ? 1 : catalog_loader ? 3 : 2));
    ExtensionVersionSnapshot observed{91, 123, "1.0", "org.seekdb.rust-text"};
    ExtensionUpdatePlan plan;
    CHECK(plan.load(root, 1, OB_SYS_DATABASE_ID, native_package, observed, "1.1", session.get_sql_mode(), error) == OB_SUCCESS);
    ExtensionRoutineScriptResolver updater(plan, services, context);
    CHECK(updater.preflight(plan.request(), error) == OB_SUCCESS);
    int updates = 0;
    CHECK(oceanbase::query::serialize_root_service_call([&]() {
      return resolve_extension_routine_sequence(updater, updater.statement_count(), guard,
          [&](const ExtensionRoutineUpdateOperation &operation) {
            CHECK(operation.kind_ == ExtensionRoutineUpdateOperation::Kind::CREATE && operation.create_arg_);
            CHECK(operation.create_arg_->routine_info_.get_routine_name() ==
                (native_only ? "rust_native_nonempty" : "rust_text_nonempty"));
            ++updates; return OB_SUCCESS;
          }, error);
    }) == OB_SUCCESS);
    CHECK(updates == 1 && plan.observed().native_module_id_ == observed.native_module_id_);
    ExtensionUpdatePlan unchanged;
    CHECK(unchanged.load(root, 1, OB_SYS_DATABASE_ID, native_package, observed, "1.0", session.get_sql_mode(), error) == OB_SUCCESS);
    ExtensionRoutineScriptResolver noop(unchanged, services, context);
    CHECK(noop.preflight(unchanged.request(), error) == OB_SUCCESS && noop.statement_count() == 0);
    observed.native_module_id_ = "another.module";
    CHECK(plan.load(root, 1, OB_SYS_DATABASE_ID, native_package, observed, "1.1", session.get_sql_mode(), error) == OB_STATE_NOT_MATCH);
    CHECK(!plan.ready() && plan.script().statements().empty());
    CHECK(runtime.calls_ == 0 && transaction.writes_.empty());
    return;
  }
  CHECK(script.load(root, "create_chain", "1", context.session_info_->get_sql_mode(), error) == OB_SUCCESS);
  using oceanbase::rootserver::RoutineIdReservation;
  using oceanbase::rootserver::RoutineVersionReservation;
  std::vector<std::unique_ptr<RoutineIdReservation>> ids;
  std::vector<std::unique_ptr<RoutineVersionReservation>> versions;
  std::vector<std::unique_ptr<ExtensionRoutineUpdateBatch>> batches;
  ExtensionInstallSpec spec;
  spec.tenant_id_ = 1; spec.database_id_ = OB_SYS_DATABASE_ID; spec.owner_id_ = 123;
  spec.name_ = "create_chain"; spec.version_ = "1";
  ExtensionRoutineScriptResolver sequence(script, spec, services, context);
  CHECK(sequence.preflight_install(spec, error) == OB_SUCCESS);
  CHECK(sequence.preflight(ExtensionUpdateRequest{}, error) == OB_STATE_NOT_MATCH);
  for (int change = 0; change < 6; ++change) {
    auto changed = spec;
    if (change == 0) ++changed.database_id_;
    if (change == 1) ++changed.owner_id_;
    if (change == 2) changed.name_ = "other";
    if (change == 3) changed.version_ = "2";
    if (change == 4) changed.native_module_id_ = "native";
    if (change == 5) changed.members_.push_back({static_cast<uint32_t>(ROUTINE_SCHEMA), 99});
    CHECK(sequence.preflight_install(changed, error) == OB_INVALID_ARGUMENT);
  }
  const ExtensionRoutineUpdateOperation *first_view = nullptr;
  CHECK(resolve_extension_routine_sequence(sequence, sequence.statement_count(), guard,
      [&](const ExtensionRoutineUpdateOperation &operation) {
    const auto i = ids.size();
    if (i == 0) first_view = &operation;
    auto batch = std::make_unique<ExtensionRoutineUpdateBatch>();
    ObSEArray<ExtensionRoutineUpdateOperation, 1> snapshot;
    CHECK(snapshot.push_back(operation) == OB_SUCCESS && batch->assign(snapshot) == OB_SUCCESS);
    const auto &arg = *batch->operations().at(0).create_arg_;
    CHECK(arg.error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
    CHECK(runtime.calls_ == 0);
    if (i == 1) {
      bool refers_to_base = false;
      for (int64_t j = 0; j < arg.dependency_infos_.count(); ++j)
        refers_to_base |= arg.dependency_infos_.at(j).get_ref_obj_id() == ids.at(0)->id() &&
                         arg.dependency_infos_.at(j).get_ref_obj_type() == ObObjectType::FUNCTION;
      CHECK(refers_to_base);
      bool version_fenced = false;
      for (int64_t j = 0; j < arg.based_schema_object_infos_.count(); ++j) {
        const auto &reference = arg.based_schema_object_infos_.at(j);
        version_fenced |= reference.schema_id_ == ids.at(0)->id() && reference.schema_type_ == ROUTINE_SCHEMA &&
                          reference.schema_version_ == versions.at(0)->version();
      }
      CHECK(version_fenced);
    }
    ObRoutineInfo staged;
    CHECK(staged.assign(arg.routine_info_) == OB_SUCCESS);
    auto identity = std::make_unique<RoutineIdReservation>();
    CHECK(RoutineIdReservation::reserve(*allocator, staged, *identity) == OB_SUCCESS);
    staged.set_routine_id(identity->id());
    auto version = std::make_unique<RoutineVersionReservation>();
    CHECK(RoutineVersionReservation::reserve(*service, transaction, staged, nullptr, *version) == OB_SUCCESS);
    staged.set_schema_version(version->version());
    auto &params = staged.get_routine_params();
    for (int64_t j = 0; j < params.count(); ++j) {
      CHECK(params.at(j) != nullptr);
      params.at(j)->set_routine_id(identity->id());
      params.at(j)->set_schema_version(version->version());
    }
    CHECK(overlay->stage(staged) == OB_SUCCESS);
    CHECK(privileges->record_create(staged, true) == OB_SUCCESS);
    ids.push_back(std::move(identity));
    versions.push_back(std::move(version));
    batches.push_back(std::move(batch));
    return OB_SUCCESS;
  }, error) == OB_SUCCESS);
  CHECK(ids.size() == 2 && first_view != nullptr);
  CHECK(first_view->create_arg_->routine_info_.get_routine_name() == ObString::make_string("create_base"));
  const ExtensionRoutineUpdateOperation *repeated = first_view;
  CHECK(sequence.resolve(0, guard, repeated, error) == OB_STATE_NOT_MATCH && repeated == nullptr);
  CHECK(first_view->create_arg_->routine_info_.get_routine_name() == ObString::make_string("create_base"));
  CHECK(transaction.writes_.empty());
  {
    ExtensionScript wrong_arguments;
    std::string problem;
    CHECK(wrong_arguments.load(root, "create_wrong_arguments", "1", session.get_sql_mode(), problem) == OB_SUCCESS);
    ExtensionRoutineUpdateBatch rejected;
    const int wrong_status = ExtensionRoutineResolver::resolve_statement(wrong_arguments, 0, services, context,
        OB_SYS_DATABASE_ID, rejected, problem);
    // Current MySQL PL resolver does not mark RETURN when its call has wrong
    // arity, so its subsequent missing-RETURN diagnostic takes precedence.
    // Preserve that existing real error instead of manufacturing generic success.
    if (wrong_status != OB_ERR_NO_RETURN_IN_FUNCTION) std::cerr << "wrong arity status=" << wrong_status << std::endl;
    CHECK(wrong_status == OB_ERR_NO_RETURN_IN_FUNCTION);
    CHECK(rejected.operations().empty() && !problem.empty());
  }
  {
    // The controlled base manager was never mutated by schema staging. This
    // checks guard isolation, not real transaction/MVCC visibility.
    ObSchemaGetterGuard outside;
    CHECK(MockSchemaService::bind(outside, *service, *manager) == OB_SUCCESS);
    const ObRoutineInfo *invisible = nullptr;
    CHECK(outside.get_standalone_function_info(OB_SYS_DATABASE_ID, ObString::make_string("create_base"),
                                               invisible) == OB_SUCCESS && invisible == nullptr);
  }
  // A deleted provisional callee must not be resurrected from a cached AST.
  const ObRoutineInfo *base = nullptr;
  CHECK(guard.get_routine_info(ids.at(0)->id(), base) == OB_SUCCESS && base);
  CHECK(overlay->erase(base->get_database_id(), base->get_routine_name(), base->get_routine_type(),
                       base->get_routine_id()) == OB_SUCCESS);
  CHECK(privileges->record_drop(*base) == OB_SUCCESS);
  {
    // Ordinary MySQL routing still permits a deferred runtime SIGNAL. The
    // explicit Extension mode must reject that SAME body/dependency failure.
    ObWarningBuffer local;
    auto *previous = ob_get_tsi_warning_buffer();
    struct RestoreWarnings {
      ObWarningBuffer *previous_;
      ~RestoreWarnings() { ob_setup_tsi_warning_buffer(previous_); }
    } restore_warnings{previous};
    ob_setup_tsi_warning_buffer(&local);
    for (const bool complete : {false, true}) {
      oceanbase::obcall::ObCreateRoutineArg argument;
      CHECK(argument.routine_info_.assign(batches.at(1)->operations().at(0).create_arg_->routine_info_) == OB_SUCCESS);
      oceanbase::pl::ObPLRouter router(argument.routine_info_, session, guard, *services.sql_proxy_,
                                      &runtime, engine.get(), complete);
      ObString route;
      CHECK(router.analyze(route, argument.dependency_infos_, argument.routine_info_, &argument) ==
            (complete ? OB_ERR_RESOLVE_SQL : OB_SUCCESS));
      local.reset();
    }
  }
  ExtensionRoutineUpdateBatch failed;
  CHECK(failed.assign(batches.at(1)->operations()) == OB_SUCCESS);
  const int deleted_status = ExtensionRoutineResolver::resolve_statement(script, 1, services, context,
      OB_SYS_DATABASE_ID, failed, error);
  if (deleted_status != OB_ERR_RESOLVE_SQL) std::cerr << "deleted callee status=" << deleted_status << std::endl;
  CHECK(deleted_status == OB_ERR_RESOLVE_SQL && failed.operations().empty());
  CHECK(!error.empty() && transaction.writes_.empty());
  // Statement index 1 is never admitted after index 0's deferred body error.
  ExtensionUpdatePlan bad_plan;
  CHECK(bad_plan.load(root, 1, OB_SYS_DATABASE_ID, "create_missing", {91, 123, "1", ""},
      "2", context.session_info_->get_sql_mode(), error) == OB_SUCCESS);
  ExtensionRoutineScriptResolver bad_resolver(bad_plan, services, context);
  int admitted = 0;
  CHECK(resolve_extension_routine_sequence(bad_resolver, bad_resolver.statement_count(), guard,
      [&](const ExtensionRoutineUpdateOperation &) { ++admitted; return OB_SUCCESS; }, error) == OB_ERR_RESOLVE_SQL);
  CHECK(admitted == 0 && runtime.calls_ == 0 && transaction.writes_.empty());
  const ExtensionRoutineUpdateOperation *operation = &batches.at(0)->operations().at(0);
  CHECK(bad_resolver.resolve(1, guard, operation, error) == OB_STATE_NOT_MATCH && operation == nullptr);
  CHECK(bad_resolver.preflight_install(spec, error) == OB_STATE_NOT_MATCH);
  // Harmless MySQL warnings must remain warnings, not reject a valid package.
  const auto saved_mode = context.session_info_->get_sql_mode();
  context.session_info_->set_sql_mode(0);
  ExtensionScript warning_script;
  CHECK(warning_script.load(root, "create_warning", "1", 0, error) == OB_SUCCESS);
  ObWarningBuffer *warnings = ob_get_tsi_warning_buffer();
  CHECK(warnings != nullptr);
  const auto warning_count = warnings->get_total_warning_count();
  const int warning_status = ExtensionRoutineResolver::resolve_statement(warning_script, 0, services, context,
      OB_SYS_DATABASE_ID, failed, error);
  context.session_info_->set_sql_mode(saved_mode);
  if (warning_status != OB_SUCCESS) std::cerr << "warning-only CREATE status=" << warning_status << std::endl;
  CHECK(warning_status == OB_SUCCESS && failed.operations().count() == 1);
  CHECK(failed.operations().at(0).create_arg_->error_info_.get_error_status() == ERROR_STATUS_NO_ERROR);
  CHECK(warnings->get_total_warning_count() > warning_count && runtime.calls_ == 0);
  CHECK(exec->get_physical_plan_ctx() == original_physical);
  {
    ObArenaAllocator physical_arena;
    ObPhysicalPlanCtx outer_physical(physical_arena);
    outer_physical.set_exec_ctx(exec);
    struct RestorePhysical {
      ObExecContext &exec_; ObPhysicalPlanCtx *previous_;
      ~RestorePhysical() { exec_.set_physical_plan_ctx(previous_); }
    } restore_physical{*exec, original_physical};
    exec->set_physical_plan_ctx(&outer_physical);
    session.set_sql_mode(0);
    CHECK(ExtensionRoutineResolver::resolve_statement(warning_script, 0, services, context,
        OB_SYS_DATABASE_ID, failed, error) == OB_SUCCESS);
    session.set_sql_mode(saved_mode);
    CHECK(exec->get_physical_plan_ctx() == &outer_physical && !outer_physical.is_subschema_ctx_inited());
    CHECK(ExtensionRoutineResolver::resolve_statement(script, 1, services, context,
        OB_SYS_DATABASE_ID, failed, error) == OB_ERR_RESOLVE_SQL);
    CHECK(exec->get_physical_plan_ctx() == &outer_physical && !outer_physical.is_subschema_ctx_inited());
  }
  CHECK(exec->get_physical_plan_ctx() == original_physical);
  {
    // Actual install orchestration; only Root persistence is substituted. It
    // checks release of Query's old guard and exercises the real callback under
    // the Root serialization guard, then rejects admission before any writes.
    ObSchemaGetterGuard caller;
    CHECK(MockSchemaService::bind(caller, *service, *manager) == OB_SUCCESS);
    CHECK(caller.attach_routine_overlay(std::make_shared<RoutineSchemaOverlay>()) == OB_SUCCESS);
    class Command final : public oceanbase::rootserver::ObLocalManagementService {
    public:
      Command(ObSchemaGetterGuard &caller, ObSchemaGetterGuard &view) : caller_(caller), view_(view) {}
      int calls_ = 0, admissions_ = 0;
      std::string expected_module_;
      int install_extension_routines(const ExtensionInstallSpec &spec,
          const ObIArray<const oceanbase::obcall::ObCreateRoutineArg *> &args, ObSQLSessionInfo &,
          uint64_t &identity, int &publication, std::string &error, IExtensionRoutineScript *script) override {
        ++calls_;
        CHECK(spec.native_module_id_ == expected_module_);
        CHECK(args.empty() && script != nullptr && identity == 0 && publication == OB_NOT_INIT);
        std::shared_ptr<const RoutineSchemaOverlay> released;
        CHECK(caller_.capture_routine_overlay(released) == OB_INNER_STAT_ERROR && !released);
        return oceanbase::query::serialize_root_service_call([&]() {
          CHECK(script->preflight_install(spec, error) == OB_SUCCESS);
          return resolve_extension_routine_sequence(*script, script->statement_count(), view_,
              [&](const ExtensionRoutineUpdateOperation &) { ++admissions_; return OB_TIMEOUT; }, error);
        });
      }
    private:
      ObSchemaGetterGuard &caller_, &view_;
    } command(caller, guard);
    ObResolverParams bound_services = services;
    bound_services.root_command_service_ = &command;
    ObSqlCtx caller_context;
    caller_context.session_info_ = &session;
    caller_context.schema_guard_ = &caller;
    caller_context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
    ExtensionRoutineResolver installer;
    uint64_t installed = 99;
    int publication = OB_SUCCESS;
    ExtensionScript unsupported;
    CHECK(unsupported.load(root, "general", "1", session.get_sql_mode(), error) == OB_SUCCESS);
    CHECK(installer.install(unsupported, bound_services, caller_context, OB_SYS_DATABASE_ID,
        installed, publication, error) == OB_NOT_SUPPORTED);
    CHECK(command.calls_ == 0 && installed == 0 && publication == OB_NOT_INIT);
    std::shared_ptr<const RoutineSchemaOverlay> preserved;
    CHECK(caller.capture_routine_overlay(preserved) == OB_SUCCESS && preserved);
    CHECK(installer.install(script, bound_services, caller_context, OB_SYS_DATABASE_ID,
        installed, publication, error) == OB_TIMEOUT);
    CHECK(command.calls_ == 1 && command.admissions_ == 1 && installed == 0 && publication == OB_NOT_INIT);
    CHECK(installer.args().empty() && runtime.calls_ == 0 && transaction.writes_.empty());
    CHECK(MockSchemaService::bind(caller, *service, *manager) == OB_SUCCESS);
    CHECK(caller.attach_routine_overlay(std::make_shared<RoutineSchemaOverlay>()) == OB_SUCCESS);
    ExtensionScript native;
    CHECK(native.load(root, "native", "1", session.get_sql_mode(), error) == OB_SUCCESS);
    command.expected_module_ = "seekdb.native";
    CHECK(installer.install(native, bound_services, caller_context, OB_SYS_DATABASE_ID,
        installed, publication, error) == OB_TIMEOUT);
    CHECK(command.calls_ == 2 && command.admissions_ == 2 && installed == 0 && publication == OB_NOT_INIT);
    CHECK(installer.args().empty() && runtime.calls_ == 0 && transaction.writes_.empty());
  }
}
}
#endif
