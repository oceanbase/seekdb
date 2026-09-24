/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#define USING_LOG_PREFIX SQL_EXE
#include "sql/engine/cmd/create_extension_executor.h"
#include "sql/resolver/cmd/create_extension_stmt.h"
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

int CreateExtensionExecutor::execute(ObExecContext &ctx, const CreateExtensionStmt &statement)
{
  int ret = OB_NOT_SUPPORTED;
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  auto *session = ctx.get_my_session();
  auto *context = ctx.get_sql_ctx();
  // This kernel entry requires the real host resolver-service binder, including
  // its dependency queue. It is not a service supplied by third-party plugins.
  auto *sql = dynamic_cast<ObSql *>(ctx.get_pl_sql_runtime());
  uint64_t extension_id = 0;
  int publication_status = OB_NOT_INIT;
  std::string error;
  try {
    if (nullptr == session || nullptr == context || nullptr == context->schema_guard_ ||
        nullptr == sql || nullptr == ctx.get_root_command_service() ||
        nullptr == ctx.get_physical_plan_ctx() || nullptr == GCTX.plugin_runtime_) {
      ret = OB_NOT_INIT;
    } else if (context->session_info_ != session || context->disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL) {
      ret = OB_ERR_NO_PRIVILEGE;
    } else if (session->is_in_transaction() || session->is_inner() || session->is_nested_session()) {
      ret = OB_NOT_SUPPORTED;
      error = "Extension installation currently requires a top-level statement without an active transaction";
    } else if (statement.database_id() != session->get_database_id()) {
      ret = OB_STATE_NOT_MATCH;
    } else if (statement.name().empty() || statement.name().ptr() == nullptr ||
               statement.version().length() < 0 ||
               (statement.version().length() > 0 && statement.version().ptr() == nullptr)) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      share::schema::ObStmtNeedPrivs privileges(ctx.get_allocator());
      share::schema::ObSessionPrivInfo actor;
      std::string root;
      ExtensionScript script;
      ExtensionRoutineResolver resolver;
      std::unique_ptr<share::plugin::ICatalogDeclarations> declarations;
      ObResolverParams services;
      sql->bind_resolver_runtime_services(services);
      services.session_info_ = session;
      services.sql_proxy_ = ctx.get_sql_proxy();
      if (OB_FAIL(ObPrivilegeCheck::check_privilege_new(*context, &statement, privileges))) {
      } else if (OB_FAIL(ObPrivilegeCheck::check_password_expired(*context, statement.get_stmt_type()))) {
      } else if (OB_FAIL(GCTX.plugin_runtime_->extension_package_root(root))) {
        if (ret == OB_NOT_SUPPORTED) error = "Extension source discovery requires administrator startup option --extension-dir";
      } else if (OB_FAIL(script.load(root, std::string(statement.name().ptr(), statement.name().length()),
          statement.version().empty() ? std::string() : std::string(statement.version().ptr(), statement.version().length()),
          session->get_sql_mode(), error))) {
      } else if (OB_FAIL(session->get_session_priv_info(actor))) {
      } else if (script.source().requires_superuser_ && !(actor.user_priv_set_ & OB_PRIV_SUPER)) {
        ret = OB_ERR_NO_PRIVILEGE;
        error = "Extension control requires SUPER; no installation callback has been invoked";
      } else {
        ret = GCTX.plugin_runtime_->prepare_catalog_install(script.source(), 1,
            statement.database_id(), session->get_priv_user_id(), declarations);
        if (OB_SUCC(ret) && declarations && !declarations->sql().empty())
          ret = script.append_catalog_declarations(declarations->sql(), error);
        // Own all argument bytes before releasing the old schema guard and
        // entering the internally serialized Root command. No ordinary DDL
        // executor or implicit commit is involved in the installation script.
        if (OB_SUCC(ret)) ret = resolver.install(script, services, *context, statement.database_id(),
                                                extension_id, publication_status, error,
                                                declarations ? declarations->program() : nullptr);
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (extension_id != 0) {
    if (ret != OB_SUCCESS) publication_status = ret;
    ret = OB_SUCCESS; // A committed identity must never be reported as retryable.
    ctx.get_physical_plan_ctx()->set_affected_rows(1);
    if (publication_status != OB_SUCCESS) {
      char warning[192];
      std::snprintf(warning, sizeof(warning),
          "Extension %llu committed; schema publication requires recovery (status %d). Do not retry installation.",
          static_cast<unsigned long long>(extension_id), publication_status);
      session->get_warnings_buffer().append_warning(warning, publication_status);
      LOG_WARN("Extension committed with schema publication pending", K(extension_id), K(publication_status));
    }
  } else if (ret == OB_SUCCESS) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret) && !error.empty()) {
    LOG_WARN("Extension installation failed", K(ret), KCSTRING(error.c_str()));
    if (ret == OB_NOT_SUPPORTED) LOG_USER_ERROR(OB_NOT_SUPPORTED, error.c_str());
  }
#else
  UNUSED(ctx);
  UNUSED(statement);
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "CREATE EXTENSION without the experimental plugin runtime");
#endif
  return ret;
}

} }
