// Copyright (c) 2026 OceanBase. SPDX-License-Identifier: Apache-2.0
// Host composition of query-owned routine mutations and Root catalog writers.
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "sql/ob_sql_context.h"
#include "share/ob_server_struct.h"
#include "share/schema/routine_schema_overlay.h"
#include "share/schema/catalog_operation_recorder.h"
#include "rootserver/ob_ddl_service.h"
#include "rootserver/pl_ddl/routine_catalog_writer.h"
#include "rootserver/pl_ddl/routine_id_reservation.h"
#include "rootserver/pl_ddl/routine_version_reservation.h"
namespace oceanbase { namespace sql {
using namespace common;
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


} }
