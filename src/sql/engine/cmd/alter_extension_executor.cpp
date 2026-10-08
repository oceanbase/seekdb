/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#define USING_LOG_PREFIX SQL_EXE
#include "sql/engine/cmd/alter_extension_executor.h"
#include "sql/resolver/cmd/alter_extension_stmt.h"
#include "sql/engine/ob_exec_context.h"
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
#include "sql/resolver/ddl/extension_script.h"
#include "sql/resolver/ddl/extension_routine_resolver.h"
#include "sql/privilege_check/ob_privilege_check.h"
#include "sql/ob_sql.h"
#include "observer/ob_server_plugin_runtime.h"
#include "share/ob_server_struct.h"
#include <new>
#include <cstdio>
#endif
namespace oceanbase { namespace sql {
using namespace common;
int AlterExtensionExecutor::execute(ObExecContext &ctx, const AlterExtensionStmt &statement)
{
  int ret = OB_NOT_SUPPORTED;
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  auto *session = ctx.get_my_session();
  auto *context = ctx.get_sql_ctx();
  auto *sql = dynamic_cast<ObSql *>(ctx.get_pl_sql_runtime());
  uint64_t extension_id = 0;
  bool changed = false;
  int publication_status = OB_NOT_INIT;
  std::string error;
  try {
    if (session == nullptr || context == nullptr || context->schema_guard_ == nullptr || sql == nullptr ||
        ctx.get_root_command_service() == nullptr || ctx.get_physical_plan_ctx() == nullptr || GCTX.plugin_runtime_ == nullptr) {
      ret = OB_NOT_INIT;
    } else if (context->session_info_ != session || context->disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL) {
      ret = OB_ERR_NO_PRIVILEGE;
    } else if (session->is_in_transaction() || session->is_inner() || session->is_nested_session()) {
      ret = OB_NOT_SUPPORTED;
      error = "Extension update requires a top-level statement without an active transaction";
    } else if (statement.database_id() != session->get_database_id()) {
      ret = OB_STATE_NOT_MATCH;
    } else if (statement.name().empty() || statement.name().ptr() == nullptr || statement.version().length() < 0 ||
               (!statement.version().empty() && statement.version().ptr() == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      share::schema::ObStmtNeedPrivs privileges(ctx.get_allocator());
      std::string root;
      ExtensionUpdatePlan plan;
      ObResolverParams services;
      sql->bind_resolver_runtime_services(services);
      services.session_info_ = session;
      services.sql_proxy_ = ctx.get_sql_proxy();
      if (OB_FAIL(ObPrivilegeCheck::check_privilege_new(*context, &statement, privileges))) {}
      else if (OB_FAIL(ObPrivilegeCheck::check_password_expired(*context, statement.get_stmt_type()))) {}
      else if (OB_FAIL(GCTX.plugin_runtime_->extension_package_root(root))) {
        if (ret == OB_NOT_SUPPORTED) error = "Extension source discovery requires administrator startup option --extension-dir";
      } else if (OB_FAIL(plan.prepare(root, std::string(statement.name().ptr(), statement.name().length()),
          statement.version().empty() ? std::string() : std::string(statement.version().ptr(), statement.version().length()),
          *context, *ctx.get_root_command_service(), error))) {}
      else {
        // prepare has released the old guard and bound an authenticated source
        // ID/version. Root rechecks both under lock, resolving each statement
        // against its transaction-local view. Do not rerun the base script or
        // silently replan if another update won the race.
        ret = ExtensionRoutineResolver::update(plan, services, *context, extension_id, changed,
                                              publication_status, error);
      }
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED; }
  catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (extension_id != 0) {
    if (ret != OB_SUCCESS) publication_status = ret;
    ret = OB_SUCCESS; // Includes a confirmed same-version no-op; never retry a committed update.
    ctx.get_physical_plan_ctx()->set_affected_rows(changed ? 1 : 0);
    if (publication_status != OB_SUCCESS) {
      char warning[192];
      std::snprintf(warning, sizeof(warning),
          "Extension %llu update committed; schema publication requires recovery (status %d). Do not retry update.",
          static_cast<unsigned long long>(extension_id), publication_status);
      session->get_warnings_buffer().append_warning(warning, publication_status);
      LOG_WARN("Extension update completed with schema publication pending", K(extension_id), K(publication_status));
    }
  } else if (ret == OB_SUCCESS) ret = OB_ERR_UNEXPECTED;
  if (OB_FAIL(ret) && !error.empty()) {
    LOG_WARN("Extension update failed", K(ret), KCSTRING(error.c_str()));
    if (ret == OB_NOT_SUPPORTED) LOG_USER_ERROR(OB_NOT_SUPPORTED, error.c_str());
  }
#else
  UNUSED(ctx);
  UNUSED(statement);
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "ALTER EXTENSION without the experimental plugin runtime");
#endif
  return ret;
}
} }
