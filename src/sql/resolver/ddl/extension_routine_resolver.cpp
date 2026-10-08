/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "sql/resolver/ddl/catalog_routine_lookup.h"
#include "sql/resolver/ddl/extension_script.h"
#include "sql/resolver/ddl/extension_statement_diagnostics.h"
#include "sql/resolver/ddl/native_routine_dcl_request.h"
#include "sql/resolver/ddl/ob_create_routine_stmt.h"
#include "sql/resolver/ddl/ob_drop_routine_stmt.h"
#include "sql/resolver/ob_resolver.h"
#include "sql/resolver/ob_schema_checker.h"
#include "sql/resolver/expr/ob_raw_expr.h"
#include "sql/privilege_check/ob_privilege_check.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/ob_physical_plan_ctx.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_sql_context.h"
#include "sql/ob_sql.h"
#include "share/ob_server_struct.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/pl_ddl/routine_catalog_writer.h"
#include "rootserver/pl_ddl/routine_id_reservation.h"
#include "rootserver/pl_ddl/routine_version_reservation.h"
#include "share/schema/routine_schema_overlay.h"
#include "share/schema/routine_privilege_overlay.h"
#include "share/schema/routine_catalog_transaction.h"
#include "share/schema/catalog_operation_recorder.h"
#include "sql/pl/ob_pl_package_guard.h"
#include "share/plugin/extension_install.h"
#include "query/command/ob_root_command_service.h"
#include "lib/charset/ob_charset.h"
#include <new>
#include <vector>
#include <thread>

namespace oceanbase { namespace sql {
using namespace common;

int CallerRoutineMutation::preflight(ObExecContext &context)
{
  if (attempted_) return OB_INIT_TWICE;
  attempted_ = true;
  error_.clear();
  auto *session = context.get_my_session();
  auto *sql_context = context.get_sql_ctx();
  auto *runtime = dynamic_cast<ObSql *>(context.get_pl_sql_runtime());
  if (!session || !sql_context || !sql_context->schema_guard_ || !runtime || !context.get_sql_proxy())
    return OB_NOT_INIT;
  if (sql_context->session_info_ != session || session->get_cur_exec_ctx() != &context)
    return OB_STATE_NOT_MATCH;
  ExtensionScript script;
  ObResolverParams services;
  runtime->bind_resolver_runtime_services(services);
  services.session_info_ = session;
  services.sql_proxy_ = context.get_sql_proxy();
  int ret = script.load_routine_statement(sql_, session->get_sql_mode(), error_);
  if (OB_SUCC(ret)) ret = ExtensionRoutineResolver::resolve_statement(script, 0, services,
      *sql_context, session->get_database_id(), batch_, error_);
  if (OB_SUCC(ret)) {
    ready_ = true;
    context_ = &context;
    database_id_ = session->get_database_id();
    principal_id_ = session->get_priv_user_id();
    sql_mode_ = session->get_sql_mode();
  }
  return ret;
}

int CallerRoutineMutation::apply(ObExecContext &context, ObMySQLTransaction &transaction,
    share::schema::RoutineSchemaOverlay &schema, share::schema::RoutinePrivilegeOverlay &privileges,
    rootserver::IRoutineCacheInvalidation &invalidation)
{
  using namespace share::schema;
  using namespace rootserver;
  using Kind = share::plugin::ExtensionRoutineUpdateOperation::Kind;
  const char *phase = "context validation";
  const auto execute = [&]() -> int {
    if (!ready_) return OB_STATE_NOT_MATCH;
    ready_ = false;
    auto *session = context.get_my_session();
    auto *sql_context = context.get_sql_ctx();
    auto *service = GCTX.schema_service_;
    if (!session || !sql_context || !sql_context->schema_guard_ || !service || !context.get_sql_proxy())
      return OB_NOT_INIT;
    if (&context != context_ || sql_context->session_info_ != session || session->get_cur_exec_ctx() != &context ||
        session->get_database_id() != database_id_ || session->get_priv_user_id() != principal_id_ ||
        session->get_sql_mode() != sql_mode_ || !transaction.is_started() ||
        schema.is_retired() || schema.privileges() != &privileges) return OB_STATE_NOT_MATCH;
    if (sql_context->disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL) return OB_ERR_NO_PRIVILEGE;
    auto &guard = *sql_context->schema_guard_;
    phase = "view/admission validation";
    std::shared_ptr<const RoutineSchemaOverlay> bound;
    int ret = guard.capture_routine_overlay(bound);
    if (OB_FAIL(ret)) return ret;
    if (bound.get() != &schema || batch_.operations().count() != 1) return OB_STATE_NOT_MATCH;
    auto *recorder = dynamic_cast<ICatalogOperationRecorder *>(&transaction);
    if (!recorder) return OB_STATE_NOT_MATCH;
    if (OB_FAIL(recorder->check_schema_operation())) return ret;
    const auto &op = batch_.operations().at(0);
    if (!op.has_valid_shape()) return OB_INVALID_ARGUMENT;
    if (!op.is_schema_change()) return OB_NOT_SUPPORTED;
    const bool drop = op.kind_ == Kind::DROP;
    if (drop ? !op.drop_arg_->is_valid() : !op.create_arg_->is_valid()) return OB_INVALID_ARGUMENT;
    const auto &db_name = drop ? op.drop_arg_->db_name_ : op.create_arg_->db_name_;
    const auto &name = drop ? op.drop_arg_->routine_name_ : op.create_arg_->routine_info_.get_routine_name();
    const auto type = drop ? op.drop_arg_->routine_type_ : op.create_arg_->routine_info_.get_routine_type();
    if (type != ROUTINE_FUNCTION_TYPE && type != ROUTINE_PROCEDURE_TYPE) return OB_NOT_SUPPORTED;
    const ObDatabaseSchema *database = nullptr;
    phase = "database lookup";
    if (OB_FAIL(guard.get_database_schema(db_name, database))) return ret;
    if (!database || database->get_database_id() != database_id_) return OB_ERR_BAD_DATABASE;
    if (database->is_in_recyclebin()) return OB_OP_NOT_ALLOW;
    phase = "dependency version validation";
    ret = drop ? ObDDLService::check_parallel_ddl_conflict(guard, *op.drop_arg_)
               : ObDDLService::check_parallel_ddl_conflict(guard, *op.create_arg_);
    if (OB_FAIL(ret)) return ret;
    ObSessionPrivInfo session_priv;
    phase = "privilege validation";
    ObArenaAllocator allocator;
    ObStmtNeedPrivs needs(allocator);
    ObNeedPriv need;
    need.db_ = db_name; need.table_ = name;
    need.obj_type_ = type == ROUTINE_FUNCTION_TYPE ? ObObjectType::FUNCTION : ObObjectType::PROCEDURE;
    need.priv_level_ = OB_PRIV_ROUTINE_LEVEL;
    need.priv_set_ = op.kind_ == Kind::CREATE ? OB_PRIV_CREATE_ROUTINE : OB_PRIV_ALTER_ROUTINE;
    if (OB_FAIL(needs.need_privs_.reserve(1))) return ret;
    if (OB_FAIL(needs.need_privs_.push_back(need))) return ret;
    if (OB_FAIL(session->get_session_priv_info(session_priv))) return ret;
    if (op.kind_ == Kind::CREATE && op.create_arg_->routine_info_.is_native() &&
        !(session_priv.user_priv_set_ & OB_PRIV_SUPER)) return OB_ERR_NO_PRIVILEGE;
    if (!(session_priv.user_priv_set_ & OB_PRIV_SUPER) && OB_FAIL(guard.verify_read_only(needs))) return ret;
    if (OB_FAIL(guard.check_priv(session_priv, session->get_enable_role_array(), needs))) return ret;
    const ObRoutineInfo *before = nullptr;
    phase = "routine lookup";
    if (drop && op.drop_arg_->native_target_resolved_) {
      if (op.drop_arg_->native_target_.get_routine_id() != OB_INVALID_ID) {
        ret = guard.get_routine_info(op.drop_arg_->native_target_.get_routine_id(), before);
      }
      if (OB_SUCC(ret)) ret = op.drop_arg_->check_native_target(before, database_id_);
    } else if (op.kind_ == Kind::ALTER && op.create_arg_->routine_info_.is_native()) {
      ret = guard.get_routine_info(op.create_arg_->routine_info_.get_routine_id(), before);
    } else {
      ret = type == ROUTINE_FUNCTION_TYPE ? guard.get_standalone_function_info(database_id_, name, before)
          : guard.get_standalone_procedure_info(database_id_, name, before);
    }
    if (OB_FAIL(ret)) return ret;
    RoutineCatalogWriter writer(*service, *context.get_sql_proxy(), guard, transaction, true);
    RoutineVersionReservation version;
    if (drop) {
      if (!before) return op.drop_arg_->if_exist_ ? OB_SUCCESS : OB_ERR_SP_DOES_NOT_EXIST;
      phase = "drop version reservation";
      if (OB_FAIL(RoutineVersionReservation::reserve_drop(*service, transaction, *before, version))) return ret;
      ObErrorInfo errors = op.drop_arg_->error_info_;
      // The normal writer checks Extension membership on this transaction, and
      // routes dependency changes and invalidation through the caller journal.
      phase = "drop writer";
      if (OB_FAIL(writer.drop(*before, errors, &op.drop_arg_->ddl_stmt_str_, invalidation, &version))) return ret;
      phase = "drop view update";
      if (OB_FAIL(schema.erase(database_id_, name, type, before->get_routine_id(), before->get_overload()))) return ret;
      if (OB_FAIL(privileges.record_drop(*before))) return ret;
      object_id_ = before->get_routine_id();
    } else {
      phase = "create/alter admission";
      const auto &arg = *op.create_arg_;
      const bool create = op.kind_ == Kind::CREATE;
      if (arg.is_or_replace_ || arg.with_if_not_exist_ || arg.is_need_alter_ != !create) return OB_NOT_SUPPORTED;
      if (create && arg.error_info_.get_error_status() == ERROR_STATUS_HAS_ERROR) return OB_ERR_RESOLVE_SQL;
      if (create && before) return OB_ERR_SP_ALREADY_EXISTS;
      if (!create && !before) return OB_ERR_SP_DOES_NOT_EXIST;
      ObRoutineInfo after;
      ObSArray<ObDependencyInfo> dependencies;
      RoutineIdReservation identity;
      if (OB_FAIL(after.assign(arg.routine_info_))) return ret;
      if (create) {
        phase = "create identity reservation";
        if (OB_FAIL(dependencies.assign(arg.dependency_infos_))) return ret;
        auto *ids = service->get_schema_service();
        if (!ids) return OB_NOT_INIT;
        after.set_database_id(database_id_);
        after.set_routine_id(OB_INVALID_ID);
        if (OB_FAIL(RoutineIdReservation::reserve(*ids, after, identity))) return ret;
        after.set_routine_id(identity.id());
      } else {
        phase = "alter dependency preservation";
        if (after.get_database_id() != database_id_ || after.get_routine_id() != before->get_routine_id() ||
            after.get_owner_id() != before->get_owner_id() || after.get_schema_version() != before->get_schema_version()
            || after.get_overload() != before->get_overload() || after.get_routine_name() != before->get_routine_name())
          return OB_STATE_NOT_MATCH;
        // Attribute-only ALTER must retain committed AND earlier caller writes'
        // dependencies, not replace them with the resolver's empty array.
        if (OB_FAIL(ObDependencyInfo::collect_ref_infos(before->get_routine_id(), transaction, dependencies))) return ret;
      }
      bool automatic = false;
      phase = "automatic privilege policy";
      if (create) {
        const ObSysVarSchema *variable = nullptr;
        ObObj value;
        if (OB_FAIL(guard.get_system_variable(share::SYS_VAR_AUTOMATIC_SP_PRIVILEGES, variable))) return ret;
        if (!variable) return OB_ERR_UNEXPECTED;
        if (OB_FAIL(variable->get_value(&allocator, nullptr, value))) return ret;
        automatic = value.get_bool();
      }
      phase = "create/alter version reservation";
      if (OB_FAIL(RoutineVersionReservation::reserve(*service, transaction, after, before, version))) return ret;
      after.set_schema_version(version.version());
      auto &parameters = after.get_routine_params();
      for (int64_t i = 0; i < parameters.count(); ++i) {
        if (!parameters.at(i)) return OB_ERR_UNEXPECTED;
        parameters.at(i)->set_routine_id(after.get_routine_id());
        parameters.at(i)->set_schema_version(after.get_schema_version());
      }
      ObErrorInfo errors = arg.error_info_;
      phase = "create/alter writer";
      if (OB_FAIL(writer.create(after, before, errors, dependencies, &arg.ddl_stmt_str_,
          create ? &identity : nullptr, &version))) return ret;
      phase = "create/alter view update";
      if (OB_FAIL(schema.stage(after))) return ret;
      if (create && OB_FAIL(privileges.record_create(after, automatic))) return ret;
      object_id_ = after.get_routine_id();
    }
    return OB_SUCCESS;
  };
  const int ret = execute();
  if (ret != OB_SUCCESS) {
    // Diagnostics must not replace a writer/rollback-relevant primary error.
    try { error_ = std::string("query routine ") + phase + " failed"; } catch (...) {}
  }
  return ret;
}

int ExtensionRoutineResolver::mutate(ObExecContext &context, const std::string &sql, uint64_t &object_id,
    share::schema::CatalogOperationResult &result, std::string &error)
{
  using share::schema::CatalogOperationResult;
  object_id = 0;
  result = CatalogOperationResult{};
  error.clear();
  try {
    CallerRoutineMutation mutation(sql, error);
    const int ret = run_caller_catalog_operation(context, mutation, result);
    if (ret == OB_SUCCESS && result.outcome_ == CatalogOperationResult::Outcome::APPLIED)
      object_id = mutation.object_id();
    return ret;
  } catch (const std::bad_alloc &) { result.operation_error_ = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { result.operation_error_ = OB_ERR_UNEXPECTED; }
  return result.operation_error_;
}

struct ExtensionRoutineScriptResolver::Impl
{
  const ExtensionUpdatePlan *plan_ = nullptr;
  const ExtensionScript &script_;
  const share::plugin::ExtensionInstallSpec install_;
  const uint64_t database_id_;
  const ObResolverParams &services_;
  const ObSqlCtx &context_;
  std::vector<std::unique_ptr<ExtensionRoutineUpdateBatch>> statements_;
  share::plugin::ICatalogBuildProgram *program_ = nullptr;
  int64_t bytes_ = 0;
  bool build_started_ = false;
  bool failed_ = false;
  Impl(const ExtensionUpdatePlan &plan, const ObResolverParams &services, const ObSqlCtx &context)
      : plan_(&plan), script_(plan.script()), database_id_(plan.request().database_id_),
        services_(services), context_(context) {}
  Impl(const ExtensionScript &script, const share::plugin::ExtensionInstallSpec &spec,
      const ObResolverParams &services, const ObSqlCtx &context, share::plugin::ICatalogBuildProgram *program)
      : script_(script), install_(spec), database_id_(spec.database_id_), services_(services), context_(context),
        program_(program) {}
};

ExtensionRoutineScriptResolver::ExtensionRoutineScriptResolver(const ExtensionUpdatePlan &plan,
    const ObResolverParams &services, const ObSqlCtx &context)
    : impl_(std::make_unique<Impl>(plan, services, context)) {}
ExtensionRoutineScriptResolver::ExtensionRoutineScriptResolver(const ExtensionScript &script,
    const share::plugin::ExtensionInstallSpec &spec, const ObResolverParams &services, const ObSqlCtx &context,
    share::plugin::ICatalogBuildProgram *program)
    : impl_(std::make_unique<Impl>(script, spec, services, context, program)) {}
ExtensionRoutineScriptResolver::~ExtensionRoutineScriptResolver() = default;
int64_t ExtensionRoutineScriptResolver::statement_count() const
{ return impl_->script_.statements().count(); }

int ExtensionRoutineScriptResolver::preflight(const share::plugin::ExtensionUpdateRequest &request,
    std::string &error)
{
  const auto &state = *impl_;
  if (state.plan_ == nullptr) return OB_STATE_NOT_MATCH;
  const auto &plan = *state.plan_;
  const auto &bound = plan.request();
  if (state.failed_ || !plan.ready()) return OB_STATE_NOT_MATCH;
  if (request.tenant_id_ != bound.tenant_id_ || request.database_id_ != bound.database_id_ ||
      request.expected_extension_id_ != bound.expected_extension_id_ || request.name_ != bound.name_ ||
      request.from_version_ != bound.from_version_ || request.to_version_ != bound.to_version_ ||
      request.requires_ != bound.requires_ || request.prerequisites_ != bound.prerequisites_ ||
      request.requires_superuser_ != bound.requires_superuser_) return OB_STATE_NOT_MATCH;
  return preflight_common(error);
}

int ExtensionRoutineScriptResolver::preflight_install(const share::plugin::ExtensionInstallSpec &spec,
    std::string &error)
{
  const auto &state = *impl_;
  const auto &bound = state.install_;
  const auto &source = state.script_.source();
  if (state.plan_ != nullptr || state.failed_ || source.name_.empty() || source.version_.empty())
    return OB_STATE_NOT_MATCH;
  if (spec.tenant_id_ != 1 || spec.database_id_ != bound.database_id_ || spec.owner_id_ != bound.owner_id_ ||
      spec.name_ != bound.name_ || spec.version_ != bound.version_ || spec.native_module_id_ != bound.native_module_id_ ||
      spec.requires_ != bound.requires_ || spec.prerequisites_ != bound.prerequisites_ ||
      spec.requires_superuser_ != bound.requires_superuser_ ||
      !spec.members_.empty() || !bound.members_.empty()) return OB_INVALID_ARGUMENT;
  if (!source.from_version_.empty() || source.name_ != spec.name_ || source.version_ != spec.version_ ||
      source.native_module_ != spec.native_module_id_ || source.requires_ != spec.requires_ || source.prerequisites_ != spec.prerequisites_ ||
      source.requires_superuser_ != spec.requires_superuser_)
    return OB_STATE_NOT_MATCH;
  if (statement_count() == 0 && state.program_ == nullptr) return OB_INVALID_ARGUMENT;
  if (state.context_.session_info_ == nullptr) return OB_NOT_INIT;
  if (state.context_.session_info_->get_priv_user_id() != spec.owner_id_) return OB_ERR_NO_PRIVILEGE;
  const int ret = preflight_common(error);
  return ret == OB_SUCCESS && state.program_ != nullptr ? state.program_->preflight(spec, error) : ret;
}

int ExtensionRoutineScriptResolver::preflight_common(std::string &error)
{
  UNUSED(error);
  const auto &state = *impl_;
  const auto &script = state.script_;
  const auto &services = state.services_;
  const auto &context = state.context_;
  if (state.failed_) return OB_STATE_NOT_MATCH;
  if (context.disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL || services.disable_privilege_check_)
    return OB_ERR_NO_PRIVILEGE;
  if (context.session_info_ == nullptr || services.session_info_ != context.session_info_ ||
      services.sql_proxy_ == nullptr) return OB_NOT_INIT;
  share::schema::ObSessionPrivInfo actor;
  const int privilege_ret = context.session_info_->get_session_priv_info(actor);
  if (privilege_ret != OB_SUCCESS) return privilege_ret;
  if (script.source().requires_superuser_ && !(actor.user_priv_set_ & OB_PRIV_SUPER))
    return OB_ERR_NO_PRIVILEGE;
  if (state.database_id_ == 0 || state.database_id_ == OB_INVALID_ID ||
      state.database_id_ != context.session_info_->get_database_id() ||
      script.sql_mode() != context.session_info_->get_sql_mode()) return OB_STATE_NOT_MATCH;
  if (statement_count() < 0 || statement_count() > ExtensionRoutineUpdateBatch::MAX_OPERATIONS) return OB_SIZE_OVERFLOW;
  for (int64_t i = 0; i < statement_count(); ++i) {
    const auto *node = script.statements().at(i).node_;
    if (node == nullptr) return OB_ERR_UNEXPECTED;
    if (node->type_ == T_SF_CREATE || node->type_ == T_SP_CREATE) {
      if (node->value_ != 0) return OB_NOT_SUPPORTED;
      if (services.pl_sql_runtime_ == nullptr || services.pl_engine_ == nullptr) return OB_NOT_INIT;
    } else if (node->type_ == T_GRANT || node->type_ == T_REVOKE) {
      // Native routine scope/recipients are checked by the semantic resolver.
    } else if (state.plan_ == nullptr || (node->type_ != T_SF_ALTER && node->type_ != T_SP_ALTER &&
               node->type_ != T_SF_DROP && node->type_ != T_SP_DROP)) return OB_NOT_SUPPORTED;
  }
  return OB_SUCCESS;
}

int ExtensionRoutineScriptResolver::validate_view(share::schema::ObSchemaGetterGuard &view, std::string &error)
{
  int ret = impl_->plan_ ? preflight(impl_->plan_->request(), error) : preflight_install(impl_->install_, error);
  const auto &name = impl_->script_.source().schema_;
  if (OB_SUCC(ret) && !name.empty()) {
    uint64_t database = OB_INVALID_ID;
    ret = view.get_database_id(ObString(name.size(), name.data()), database);
    if (OB_SUCC(ret) && database != impl_->database_id_) ret = OB_ERR_BAD_DATABASE;
  }
  return ret;
}

int ExtensionRoutineScriptResolver::resolve(int64_t index, share::schema::ObSchemaGetterGuard &view,
    const share::plugin::ExtensionRoutineUpdateOperation *&operation, std::string &error)
{
  operation = nullptr;
  int ret = OB_SUCCESS;
  auto &state = *impl_;
  try {
    ret = state.plan_ ? preflight(state.plan_->request(), error) : preflight_install(state.install_, error);
    if (OB_SUCC(ret) && (index < 0 || index >= statement_count() ||
        static_cast<size_t>(index) != state.statements_.size())) ret = OB_STATE_NOT_MATCH;
    if (OB_SUCC(ret)) {
      ObSqlCtx context;
      context.session_info_ = state.context_.session_info_;
      context.schema_guard_ = &view;
      context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
      auto owned = std::make_unique<ExtensionRoutineUpdateBatch>();
      ret = ExtensionRoutineResolver::resolve_statement(state.script_, index, state.services_, context,
          state.database_id_, *owned, error);
      if (OB_SUCC(ret)) {
        const auto &op = owned->operations().at(0);
        const int64_t size = op.create_arg_ ? op.create_arg_->get_serialize_size()
            : op.drop_arg_ ? op.drop_arg_->get_serialize_size()
            : op.grant_arg_ ? op.grant_arg_->get_serialize_size() : op.revoke_arg_->get_serialize_size();
        if (size <= 0 || size > ExtensionRoutineUpdateBatch::MAX_WIRE_BYTES - state.bytes_) ret = OB_SIZE_OVERFLOW;
        else {
          state.statements_.push_back(std::move(owned));
          state.bytes_ += size;
          operation = &state.statements_.back()->operations().at(0);
        }
      }
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (OB_FAIL(ret)) state.failed_ = true;
  return ret;
}

bool ExtensionRoutineScriptResolver::has_builder() const
{ return impl_->program_ != nullptr; }

int ExtensionRoutineScriptResolver::build(share::schema::ObSchemaGetterGuard &view,
    const StageRoutine &stage, std::string &error)
{
  auto &state = *impl_;
  if (state.program_ == nullptr) return OB_SUCCESS;
  if (state.plan_ != nullptr || state.failed_ || state.build_started_ || !stage ||
      state.statements_.size() != static_cast<size_t>(statement_count())) return OB_STATE_NOT_MATCH;
  state.build_started_ = true; // Never rerun a program after any outcome.
  int ret = OB_SUCCESS;
  try {
    ret = validate_view(view, error);
    class Builder final : public share::plugin::ICatalogRoutineBuilder {
    public:
      Builder(Impl &state, share::schema::ObSchemaGetterGuard &view, const StageRoutine &stage)
          : state_(state), view_(view), stage_(stage), sql_bytes_(state.script_.sql_bytes()) {}
      int lookup_routine(share::plugin::CatalogRoutineKind kind, const std::string &name,
                         uint64_t &object_id, std::string &error) override
      {
        UNUSED(error);
        using namespace share::schema;
        using share::plugin::CatalogRoutineKind;
        object_id = 0;
        if (thread_ != std::this_thread::get_id()) return OB_STATE_NOT_MATCH;
        if (status_ != OB_SUCCESS) return status_;
        try {
          ObSessionPrivInfo privileges;
          auto &session = *state_.context_.session_info_;
          status_ = session.get_session_priv_info(privileges);
          if (status_ == OB_SUCCESS && privileges.user_id_ != state_.install_.owner_id_) status_ = OB_ERR_NO_PRIVILEGE;
          if (status_ == OB_SUCCESS) status_ = lookup_catalog_routine(session, view_, state_.database_id_, kind, name, object_id);
        } catch (const std::bad_alloc &) { status_ = OB_ALLOCATE_MEMORY_FAILED;
        } catch (...) { status_ = OB_ERR_UNEXPECTED; }
        if (status_ != OB_SUCCESS) object_id = 0;
        return status_;
      }
      int create_routine(const std::string &sql, uint64_t &object_id, std::string &error) override
      {
        object_id = 0;
        if (thread_ != std::this_thread::get_id()) return OB_STATE_NOT_MATCH;
        if (status_ != OB_SUCCESS) return status_;
        try {
          if (sql_bytes_ > 4 * 1024 * 1024 || sql.size() > 4 * 1024 * 1024 - sql_bytes_ ||
              state_.statements_.size() >= ExtensionRoutineUpdateBatch::MAX_OPERATIONS) {
            status_ = OB_SIZE_OVERFLOW;
          } else {
            sql_bytes_ += sql.size();
            auto input = state_.script_.source();
            input.native_install_ = false;
            input.scripts_ = {{"", input.version_, sql}};
            ExtensionScript parsed;
            status_ = parsed.load_source(input, state_.script_.sql_mode(), error);
            if (status_ == OB_SUCCESS && parsed.statements().count() != 1) status_ = OB_INVALID_ARGUMENT;
            if (status_ == OB_SUCCESS) {
              const auto *node = parsed.statements().at(0).node_;
              if (node == nullptr || (node->type_ != T_SF_CREATE && node->type_ != T_SP_CREATE) || node->value_ != 0)
                status_ = OB_NOT_SUPPORTED;
            }
            if (status_ == OB_SUCCESS) {
              ObSqlCtx context;
              context.session_info_ = state_.context_.session_info_;
              context.schema_guard_ = &view_;
              context.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
              auto owned = std::make_unique<ExtensionRoutineUpdateBatch>();
              status_ = ExtensionRoutineResolver::resolve_statement(parsed, 0, state_.services_, context,
                  state_.database_id_, *owned, error);
              if (status_ == OB_SUCCESS) {
                const auto &op = owned->operations().at(0);
                if (!op.has_valid_shape() || op.kind_ != share::plugin::ExtensionRoutineUpdateOperation::Kind::CREATE) {
                  status_ = OB_ERR_UNEXPECTED;
                } else {
                  const int64_t bytes = op.create_arg_->get_serialize_size();
                  if (bytes <= 0 || bytes > ExtensionRoutineUpdateBatch::MAX_WIRE_BYTES - state_.bytes_) {
                    status_ = OB_SIZE_OVERFLOW;
                  } else {
                    // Root retains argument pointers until schema persistence;
                    // keep wire ownership before invoking its staging callback.
                    state_.statements_.push_back(std::move(owned));
                    state_.bytes_ += bytes;
                    status_ = stage_(state_.statements_.back()->operations().at(0), object_id);
                    if (status_ == OB_SUCCESS && (object_id == 0 || object_id > INT64_MAX)) status_ = OB_ERR_UNEXPECTED;
                  }
                }
              }
            }
          }
        } catch (const std::bad_alloc &) { status_ = OB_ALLOCATE_MEMORY_FAILED;
        } catch (...) { status_ = OB_ERR_UNEXPECTED; }
        if (status_ != OB_SUCCESS) object_id = 0;
        return status_;
      }
      int status() const { return status_; }
    private:
      Impl &state_;
      share::schema::ObSchemaGetterGuard &view_;
      const StageRoutine &stage_;
      const std::thread::id thread_ = std::this_thread::get_id();
      size_t sql_bytes_;
      int status_ = OB_SUCCESS;
    } builder(state, view, stage);
    if (OB_SUCC(ret)) {
      ret = state.program_->build(builder, error);
      if (builder.status() != OB_SUCCESS) ret = builder.status(); // Cannot swallow construction failure.
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (OB_FAIL(ret)) state.failed_ = true;
  return ret;
}

int ExtensionRoutineResolver::update(const ExtensionUpdatePlan &plan, const ObResolverParams &services,
    const ObSqlCtx &context, uint64_t &extension_id, bool &changed,
    int &publication_status, std::string &error)
{
  extension_id = 0;
  changed = false;
  publication_status = OB_NOT_INIT;
  error.clear();
  int ret = OB_SUCCESS;
  try {
    ExtensionRoutineScriptResolver script(plan, services, context);
    if (OB_FAIL(script.preflight(plan.request(), error))) {
    } else if (context.schema_guard_ == nullptr || services.root_command_service_ == nullptr) {
      ret = OB_NOT_INIT;
    } else if (context.session_info_->is_in_transaction() || context.session_info_->is_inner() ||
               context.session_info_->is_nested_session()) {
      ret = OB_NOT_SUPPORTED;
    } else if (OB_FAIL(context.schema_guard_->reset())) {
    } else {
      ObSEArray<share::plugin::ExtensionRoutineUpdateOperation, 1> no_pre_resolved;
      ret = services.root_command_service_->update_extension_routines(plan.request(), no_pre_resolved,
          *context.session_info_, extension_id, changed, publication_status, error, &script);
    }
  } catch (const std::bad_alloc &) {
    if (extension_id != 0) publication_status = OB_ALLOCATE_MEMORY_FAILED;
    else ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (extension_id != 0) publication_status = OB_ERR_UNEXPECTED;
    else ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

struct ExtensionRoutineResolver::Impl
{
  struct Statement
  {
    Statement() : arena_("ExtResolve"), statements_(arena_), expressions_(arena_), physical_(arena_) {}
    ~Statement() { expressions_.destory(); }
    ObArenaAllocator arena_;
    ObStmtFactory statements_;
    ObRawExprFactory expressions_;
    ObPhysicalPlanCtx physical_;
    ObSchemaChecker checker_;
    pl::ObPLPackageGuard packages_;
    ObSqlCtx context_;
  };
};

int ExtensionRoutineResolver::resolve_statement(const ExtensionScript &script, int64_t index,
    const ObResolverParams &services, const ObSqlCtx &context, uint64_t database_id,
    ExtensionRoutineUpdateBatch &output, std::string &error)
{
  using Operation = share::plugin::ExtensionRoutineUpdateOperation;
  using Kind = Operation::Kind;
  output.reset();
  error.clear();
  int ret = OB_SUCCESS;
  const char *phase = "context validation";
  try {
    ExtensionStatementDiagnostics diagnostics(ret);
    if (context.disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL || services.disable_privilege_check_) {
      ret = OB_ERR_NO_PRIVILEGE;
    } else if (index < 0 || index >= script.statements().count() ||
               script.statements().count() > ExtensionRoutineUpdateBatch::MAX_OPERATIONS ||
               database_id == 0 || database_id > static_cast<uint64_t>(INT64_MAX)) {
      ret = OB_INVALID_ARGUMENT;
    }
    Kind kind = Kind::INVALID;
    if (OB_SUCC(ret)) {
      const auto *node = script.statements().at(index).node_;
      if (node != nullptr) {
        if ((node->type_ == T_SF_CREATE || node->type_ == T_SP_CREATE) && node->value_ == 0) kind = Kind::CREATE;
        else if (node->type_ == T_SF_ALTER || node->type_ == T_SP_ALTER) kind = Kind::ALTER;
        else if (node->type_ == T_SF_DROP || node->type_ == T_SP_DROP) kind = Kind::DROP;
        else if (node->type_ == T_GRANT) kind = Kind::GRANT;
        else if (node->type_ == T_REVOKE) kind = Kind::REVOKE;
      }
      if (kind == Kind::INVALID) {
        ret = OB_NOT_SUPPORTED;
        error = "extension statement requires routine CREATE, ALTER, DROP or native routine DCL; CREATE IF NOT EXISTS is not supported";
      }
    }
    if (OB_FAIL(ret)) {
    } else if (context.session_info_ == nullptr || context.schema_guard_ == nullptr ||
               services.session_info_ != context.session_info_ || services.sql_proxy_ == nullptr ||
               (kind == Kind::CREATE && (services.pl_sql_runtime_ == nullptr || services.pl_engine_ == nullptr))) {
      ret = OB_NOT_INIT;
    } else if (script.sql_mode() != context.session_info_->get_sql_mode() ||
               database_id != context.session_info_->get_database_id()) {
      ret = OB_STATE_NOT_MATCH;
    }
    if (OB_SUCC(ret) && !script.source().schema_.empty()) {
      uint64_t fixed_database = OB_INVALID_ID;
      const auto &name = script.source().schema_;
      ret = context.schema_guard_->get_database_id(ObString(name.size(), name.data()), fixed_database);
      if (OB_SUCC(ret) && fixed_database != database_id) ret = OB_ERR_BAD_DATABASE;
    }
    if (OB_SUCC(ret)) {
      auto owned = std::make_unique<Impl::Statement>();
      auto &state = *owned;
      auto &session = *context.session_info_;
      auto *exec = session.get_cur_exec_ctx();
      if (exec != nullptr && (exec->get_my_session() != &session || exec->get_sql_ctx() == nullptr)) {
        ret = OB_NOT_INIT;
      } else if (OB_FAIL(state.packages_.init())) {
      } else {
        // Don't shallow-copy ObSqlCtx (it owns containers) or inherit outer
        // prepared parameters, PL namespace, package cache, or query constants.
        state.context_.session_info_ = &session;
        state.context_.schema_guard_ = context.schema_guard_;
        state.context_.disable_privilege_check_ = PRIV_CHECK_FLAG_NORMAL;
        struct ContextGuard {
          ObSQLSessionInfo &session_;
          ObExecContext *exec_;
          ObSqlCtx *sql_;
          pl::ObPLPackageGuard *packages_;
          ObStmtFactory *statements_;
          ObRawExprFactory *expressions_;
          ObPhysicalPlanCtx *physical_;
          stmt::StmtType type_;
          ~ContextGuard() {
            session_.set_stmt_type(type_);
            if (exec_) {
              exec_->swap_resolver_factories(statements_, expressions_);
              exec_->set_package_guard(packages_);
              exec_->set_sql_ctx(sql_);
              exec_->set_physical_plan_ctx(physical_);
            }
          }
        } restore{session, exec, exec ? exec->get_sql_ctx() : nullptr,
                  exec ? exec->peek_package_guard() : nullptr, &state.statements_, &state.expressions_,
                  exec ? exec->get_physical_plan_ctx() : nullptr,
                  session.get_stmt_type()};
        if (exec) {
          exec->swap_resolver_factories(restore.statements_, restore.expressions_);
          exec->set_sql_ctx(&state.context_);
          exec->set_package_guard(&state.packages_);
          // ENUM/SET and other subschema resolution use the current execution
          // context's type map. Never borrow or mutate the outer query's map.
          state.physical_.set_exec_ctx(exec);
          exec->set_physical_plan_ctx(&state.physical_);
        }
        ObResolverParams params;
        params.session_info_ = &session;
        params.require_complete_routine_dependencies_ = true;
        params.allocator_ = &state.arena_;
        params.stmt_factory_ = &state.statements_;
        params.expr_factory_ = &state.expressions_;
        params.schema_checker_ = &state.checker_;
        params.query_ctx_ = state.statements_.get_query_ctx();
        params.package_guard_ = &state.packages_;
        params.database_id_ = database_id;
        params.sql_proxy_ = services.sql_proxy_;
        params.plan_cache_ = services.plan_cache_;
        params.pl_sql_runtime_ = services.pl_sql_runtime_;
        params.pl_engine_ = services.pl_engine_;
        params.dependency_info_queue_ = services.dependency_info_queue_;
        params.root_command_service_ = services.root_command_service_;
        params.srs_provider_ = services.srs_provider_;
        params.lob_read_service_ = services.lob_read_service_;
        if (params.query_ctx_ == nullptr) ret = OB_ALLOCATE_MEMORY_FAILED;
        else {
          params.query_ctx_->sql_schema_guard_.set_schema_guard(context.schema_guard_);
          state.expressions_.set_query_ctx(params.query_ctx_);
          const uint64_t session_id = session.get_session_type() == ObSQLSessionInfo::INNER_SESSION
              ? OB_INVALID_ID : session.get_sessid_for_table();
          ret = state.checker_.init(params.query_ctx_->sql_schema_guard_, session_id);
        }
        const ObCharsets4Parser charsets = session.get_charsets4parser();
        const auto collation = charsets.string_collation_;
        ObString sql;
        if (OB_SUCC(ret) && !ObCharset::is_valid_collation(collation)) ret = OB_INVALID_ARGUMENT;
        if (OB_SUCC(ret)) ret = ObCharset::charset_convert(state.arena_, script.statements().at(index).sql_,
            CS_TYPE_UTF8MB4_GENERAL_CI, collation, sql, ObCharset::COPY_STRING_ON_SAME_CHARSET);
        ParseResult parsed{};
        if (OB_SUCC(ret)) {
          ObParser parser(state.arena_, session.get_sql_mode(), charsets);
          ret = parser.parse(sql, parsed);
        }
        if (OB_SUCC(ret) && (parsed.result_tree_ == nullptr || parsed.result_tree_->num_child_ != 1 ||
            parsed.result_tree_->children_ == nullptr || parsed.result_tree_->children_[0] == nullptr ||
            parsed.result_tree_->children_[0]->type_ != script.statements().at(index).node_->type_)) ret = OB_ERR_PARSE_SQL;
        ObStmt *statement = nullptr;
        if (OB_SUCC(ret)) {
          params.cur_sql_ = sql;
          params.query_ctx_->set_sql_stmt(sql);
          state.context_.cur_sql_ = sql;
          ObResolver resolver(params);
          phase = "semantic resolution";
          ret = resolver.resolve(ObResolver::IS_NOT_PREPARED_STMT, *parsed.result_tree_->children_[0], statement);
        }
        if (OB_SUCC(ret)) {
          share::schema::ObStmtNeedPrivs privileges(state.arena_);
          phase = "privilege check";
          ret = ObPrivilegeCheck::check_privilege_new(state.context_, statement, privileges);
          if (OB_SUCC(ret)) ret = ObPrivilegeCheck::check_password_expired(state.context_, statement->get_stmt_type());
        }
        Operation operation;
        obcall::ObDDLArg *ddl = nullptr;
        if (OB_SUCC(ret)) phase = "operation preparation";
        if (OB_SUCC(ret) && (kind == Kind::GRANT || kind == Kind::REVOKE)) {
          const obcall::NativeRoutinePrivilegeTarget *target = nullptr;
          if (kind == Kind::GRANT) {
            auto *grant = dynamic_cast<ObGrantStmt *>(statement);
            if (!grant) ret = OB_ERR_UNEXPECTED;
            else if (OB_FAIL(NativeRoutineDclRequest::prepare(*grant, session, *context.schema_guard_))) {
            } else {
              auto &arg = static_cast<obcall::ObGrantArg &>(grant->get_ddl_arg());
              operation = {kind, nullptr, nullptr, &arg}; ddl = &arg; target = &arg.native_target_;
            }
          } else {
            auto *revoke = dynamic_cast<ObRevokeStmt *>(statement);
            if (!revoke) ret = OB_ERR_UNEXPECTED;
            else if (OB_FAIL(NativeRoutineDclRequest::prepare(*revoke, session, *context.schema_guard_))) {
            } else {
              auto &arg = static_cast<obcall::ObRevokeRoutineArg &>(revoke->get_ddl_arg());
              operation = {kind, nullptr, nullptr, nullptr, &arg}; ddl = &arg; target = &arg.native_target_;
            }
          }
          if (OB_SUCC(ret) && (!target || target->routine_.get_database_id() != database_id)) ret = OB_ERR_BAD_DATABASE;
        } else if (OB_SUCC(ret) && kind == Kind::DROP) {
          auto *drop = dynamic_cast<ObDropRoutineStmt *>(statement);
          if (drop == nullptr) ret = OB_ERR_UNEXPECTED;
          else {
            auto &arg = drop->get_routine_arg();
            uint64_t target_database = OB_INVALID_ID;
            ret = context.schema_guard_->get_database_id(arg.db_name_, target_database);
            if (OB_SUCC(ret) && target_database != database_id) ret = OB_ERR_BAD_DATABASE;
            operation = {kind, nullptr, &arg};
            ddl = &arg;
          }
        } else if (OB_SUCC(ret)) {
          auto *routine = dynamic_cast<ObCreateRoutineStmt *>(statement);
          if (routine == nullptr) ret = OB_ERR_UNEXPECTED;
          else {
            auto &arg = routine->get_routine_arg();
            // The legacy collector marks ANY retained warning as HAS_ERROR.
            // Our isolated diagnostics start with OB_MAX_ERROR_CODE, so a
            // warning-only result must not become a broken schema object.
            if (arg.error_info_.get_error_status() == share::schema::ERROR_STATUS_HAS_ERROR &&
                arg.error_info_.get_error_number() == static_cast<uint64_t>(OB_MAX_ERROR_CODE)) {
              arg.error_info_.reset();
            }
            // MySQL routine resolution may deliberately return success with a
            // deferred body error. Such a result has no complete dependency
            // set and must not be staged/committed as an Extension CREATE.
            // Ordinary CREATE ROUTINE retains its existing deferred semantics.
            if (kind == Kind::CREATE && arg.error_info_.get_error_status() == share::schema::ERROR_STATUS_HAS_ERROR) {
              ret = OB_ERR_RESOLVE_SQL;
              error = "extension routine body contains unresolved compilation errors";
            } else if (arg.routine_info_.get_database_id() != database_id) ret = OB_ERR_BAD_DATABASE;
            else if (arg.is_or_replace_ || arg.with_if_not_exist_ || arg.is_need_alter_ != (kind == Kind::ALTER))
              ret = OB_NOT_SUPPORTED;
            operation = {kind, &arg, nullptr};
            ddl = &arg;
          }
        }
        // Wire-copy before all parser, package and schema-borrowing state dies.
        if (OB_SUCC(ret)) phase = "owned snapshot";
        if (OB_SUCC(ret)) ret = ObCharset::charset_convert(state.arena_, sql, collation,
            ObCharset::get_system_collation(), ddl->ddl_stmt_str_, ObCharset::COPY_STRING_ON_SAME_CHARSET);
        ObSEArray<Operation, 1> one;
        if (OB_SUCC(ret)) ret = one.push_back(operation);
        if (OB_SUCC(ret)) ret = output.assign(one);
      }
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (OB_FAIL(ret)) {
    output.reset();
    if (error.empty()) {
      try {
        error = "extension routine resolution failed at statement " +
            std::to_string(index >= 0 && index < script.statements().count() ? index + 1 : 0) + " (" + phase + ")";
      }
      catch (...) {} // Preserve the original status even if diagnostics run out of memory.
    }
  }
  return ret;
}

ExtensionRoutineResolver::ExtensionRoutineResolver() = default;
ExtensionRoutineResolver::~ExtensionRoutineResolver() = default;
void ExtensionRoutineResolver::reset()
{
  batch_.reset();
}

int ExtensionRoutineResolver::install(const ExtensionScript &script, const ObResolverParams &services,
                                     const ObSqlCtx &context, uint64_t database_id,
                                     uint64_t &extension_id, int &publication_status, std::string &error,
                                     share::plugin::ICatalogBuildProgram *program)
{
  extension_id = 0;
  publication_status = OB_NOT_INIT;
  error.clear();
  reset();
  if (nullptr == context.session_info_ || nullptr == services.root_command_service_) return OB_NOT_INIT;
  int ret = OB_SUCCESS;
  try {
    share::schema::ObSessionPrivInfo privileges;
    share::plugin::ExtensionInstallSpec spec;
    if (script.source().name_.empty() || script.source().version_.empty()) {
      ret = OB_INVALID_ARGUMENT;
      error = "query catalog statements are not Extension installation packages";
    } else if (!script.source().from_version_.empty()) {
      ret = OB_NOT_SUPPORTED;
      error = "Extension update plans require update coordination, not new installation";
    } else if (context.session_info_->is_in_transaction() || context.session_info_->is_inner() ||
               context.session_info_->is_nested_session()) {
      ret = OB_NOT_SUPPORTED;
      error = "Extension installation cannot join or implicitly commit an active transaction";
    } else if (OB_FAIL(context.session_info_->get_session_priv_info(privileges))) {
    } else if (context.schema_guard_ == nullptr) {
      ret = OB_NOT_INIT;
    } else {
      spec.tenant_id_ = 1;
      spec.database_id_ = database_id;
      spec.owner_id_ = privileges.user_id_;
      spec.name_ = script.source().name_;
      spec.version_ = script.source().version_;
      // An association with an already installed module, not permission to
      // load native code. Catalog locks/checks ACTIVE and records the durable
      // dependency in this same schema transaction before committing it.
      spec.native_module_id_ = script.source().native_module_;
      spec.requires_ = script.source().requires_;
      spec.prerequisites_ = script.source().prerequisites_;
      spec.requires_superuser_ = script.source().requires_superuser_;
      ExtensionRoutineScriptResolver sequence(script, spec, services, context, program);
      if (OB_FAIL(sequence.preflight_install(spec, error))) {
      } else if (OB_FAIL(context.schema_guard_->reset())) {
      } else {
        // Root supplies the view, reserves real IDs/versions and stages each
        // admitted object before the next callback. No pre-resolved batch.
        ret = services.root_command_service_->install_extension_routines(
            spec, batch_.args(), *context.session_info_, extension_id, publication_status, error, &sequence);
      }
    }
  } catch (const std::bad_alloc &) {
    // A provider must preserve committed identity before any post-commit work.
    // Never make a committed installation look retryable due to later failure.
    if (extension_id != 0) publication_status = OB_ALLOCATE_MEMORY_FAILED;
    else ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    if (extension_id != 0) publication_status = OB_ERR_UNEXPECTED;
    else ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ExtensionRoutineResolver::resolve(const ExtensionScript &script, const ObResolverParams &services,
                                     const ObSqlCtx &context, uint64_t database_id, std::string &error)
{
  reset();
  error.clear();
  int ret = OB_SUCCESS;
  try {
    ObSEArray<const obcall::ObCreateRoutineArg *, 16> resolved_args;
    // Never inherit an internal privilege bypass or unrelated prepared/PL state.
    if (context.disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL || services.disable_privilege_check_) {
      ret = OB_ERR_NO_PRIVILEGE;
    } else if (!script.source().from_version_.empty()) {
      ret = OB_NOT_SUPPORTED;
      error = "Extension update plans require update coordination, not new installation";
    } else if (script.statements().empty() || script.statements().count() > 4096 ||
               database_id == 0 || database_id == OB_INVALID_ID) {
      ret = OB_INVALID_ARGUMENT;
    } else if (!script.source().native_module_.empty()) {
      ret = OB_NOT_SUPPORTED;
      error = "routine installation requires a pure-SQL package";
    }
    // This is the first schema adapter's support set, not the parser's grammar.
    for (int64_t i = 0; OB_SUCC(ret) && i < script.statements().count(); ++i) {
      const auto *node = script.statements().at(i).node_;
      if (nullptr == node || (node->type_ != T_SF_CREATE && node->type_ != T_SP_CREATE) || node->value_ != 0) {
        ret = OB_NOT_SUPPORTED;
        error = "extension routine adapter requires new functions or procedures without IF NOT EXISTS";
      }
    }
    if (OB_FAIL(ret)) {
    } else if (nullptr == context.session_info_ || nullptr == context.schema_guard_ ||
               services.session_info_ != context.session_info_ || nullptr == services.sql_proxy_ ||
               nullptr == services.pl_sql_runtime_ || nullptr == services.pl_engine_) {
      ret = OB_NOT_INIT;
    } else if (script.sql_mode() != context.session_info_->get_sql_mode()) {
      ret = OB_STATE_NOT_MATCH;
      error = "extension script SQL mode differs from the resolving session";
    }
    if (OB_SUCC(ret) && !script.source().schema_.empty()) {
      uint64_t fixed_database = OB_INVALID_ID;
      const auto &schema = script.source().schema_;
      ret = context.schema_guard_->get_database_id(ObString(static_cast<int32_t>(schema.size()), schema.data()), fixed_database);
      if (OB_SUCC(ret) && fixed_database != database_id) {
        ret = OB_ERR_BAD_DATABASE;
        error = "extension fixed schema differs from the target database";
      }
    }
    if (OB_SUCC(ret)) {
      std::vector<std::unique_ptr<ExtensionRoutineUpdateBatch>> statements;
      statements.reserve(script.statements().count());
      int64_t wire_bytes = 0;
      for (int64_t i = 0; OB_SUCC(ret) && i < script.statements().count(); ++i) {
        auto statement = std::make_unique<ExtensionRoutineUpdateBatch>();
        ret = resolve_statement(script, i, services, context, database_id, *statement, error);
        if (OB_SUCC(ret)) {
          const auto &operation = statement->operations().at(0);
          const auto *arg = operation.create_arg_;
          if (operation.kind_ != share::plugin::ExtensionRoutineUpdateOperation::Kind::CREATE || arg == nullptr)
            ret = OB_ERR_UNEXPECTED;
          else if (arg->get_serialize_size() > ExtensionRoutineUpdateBatch::MAX_WIRE_BYTES - wire_bytes)
            ret = OB_SIZE_OVERFLOW;
          else {
            wire_bytes += arg->get_serialize_size();
            ret = resolved_args.push_back(arg);
            if (OB_SUCC(ret)) statements.push_back(std::move(statement));
          }
        }
      }
      if (OB_SUCC(ret)) ret = batch_.assign(resolved_args);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  // No parser/schema/PL context survives into the DDL command. Keep only the
  // owned wire snapshots, including strings originally borrowed from arenas.
  if (OB_FAIL(ret)) reset();
  return ret;
}

} }
