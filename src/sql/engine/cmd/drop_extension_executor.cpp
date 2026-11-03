/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#define USING_LOG_PREFIX SQL_EXE
#include "sql/engine/cmd/drop_extension_executor.h"
#include "sql/resolver/cmd/drop_extension_stmt.h"
#include "sql/engine/ob_exec_context.h"
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
#include "share/plugin/extension_install.h"
#include "query/api/query/command/ob_root_command_service.h"
#include "sql/privilege_check/ob_privilege_check.h"
#include <new>
#include <cstdio>
#endif
namespace oceanbase { namespace sql {
using namespace common;
int DropExtensionExecutor::execute(ObExecContext &ctx, const DropExtensionStmt &statement)
{
  int ret = OB_NOT_SUPPORTED;
#if defined(SEEKDB_WITH_EXPERIMENTAL_PLUGINS)
  auto *session = ctx.get_my_session();
  auto *context = ctx.get_sql_ctx();
  uint64_t extension_id = 0;
  int publication_status = OB_NOT_INIT;
  std::string error;
  try {
    if (nullptr == session || nullptr == context || nullptr == context->schema_guard_ ||
        nullptr == ctx.get_root_command_service() || nullptr == ctx.get_physical_plan_ctx()) {
      ret = OB_NOT_INIT;
    } else if (context->session_info_ != session || context->disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL) {
      ret = OB_ERR_NO_PRIVILEGE;
    } else if (session->is_in_transaction() || session->is_inner() || session->is_nested_session()) {
      ret = OB_NOT_SUPPORTED;
      error = "Extension removal requires a top-level statement without an active transaction";
    } else if (statement.database_id() != session->get_database_id()) {
      ret = OB_STATE_NOT_MATCH;
    } else if (statement.name().empty() || nullptr == statement.name().ptr()) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      share::schema::ObStmtNeedPrivs privileges(ctx.get_allocator());
      share::plugin::ExtensionDropRequest request;
      request.tenant_id_ = 1;
      request.database_id_ = statement.database_id();
      request.name_.assign(statement.name().ptr(), statement.name().length());
      request.cascade_ = statement.cascade();
      if (OB_FAIL(ObPrivilegeCheck::check_privilege_new(*context, &statement, privileges))) {
      } else if (OB_FAIL(ObPrivilegeCheck::check_password_expired(*context, statement.get_stmt_type()))) {
      } else if (OB_FAIL(context->schema_guard_->reset())) {
      } else {
        // Root refreshes schema and checks ownership/member privileges against
        // its locked installation snapshot. No package discovery or native load.
        ret = ctx.get_root_command_service()->drop_extension_routines(request, *session,
            extension_id, publication_status, error);
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (extension_id != 0) {
    if (ret != OB_SUCCESS) publication_status = ret;
    ret = OB_SUCCESS;
    ctx.get_physical_plan_ctx()->set_affected_rows(1);
    if (publication_status != OB_SUCCESS) {
      char warning[192];
      std::snprintf(warning, sizeof(warning),
          "Extension %llu removal committed; schema publication requires recovery (status %d). Do not retry removal.",
          static_cast<unsigned long long>(extension_id), publication_status);
      session->get_warnings_buffer().append_warning(warning, publication_status);
      LOG_WARN("Extension removal committed with schema publication pending", K(extension_id), K(publication_status));
    }
  } else if (ret == OB_SUCCESS) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret) && !error.empty()) {
    LOG_WARN("Extension removal failed", K(ret), KCSTRING(error.c_str()));
    if (ret == OB_NOT_SUPPORTED) LOG_USER_ERROR(OB_NOT_SUPPORTED, error.c_str());
  }
#else
  UNUSED(ctx);
  UNUSED(statement);
  LOG_USER_ERROR(OB_NOT_SUPPORTED, "DROP EXTENSION without the experimental plugin runtime");
#endif
  return ret;
}
} }
