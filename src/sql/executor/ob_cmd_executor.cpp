/*
 * Copyright (c) 2025 OceanBase.
 *
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

#define USING_LOG_PREFIX SQL_EXE

#include "lib/stat/ob_diagnostic_info_guard.h"
#include "ob_cmd_executor.h"
#include "observer/scheduler/ob_ddl_count_guard.h"  // ObDDLCountGuard (relocated L9)
#include "share/ob_version_parser.h"
#include "sql/resolver/ddl/ob_alter_table_stmt.h"
#include "sql/resolver/ddl/ob_create_table_stmt.h"
#include "sql/resolver/ddl/ob_drop_index_stmt.h"
#include "sql/resolver/ddl/ob_drop_table_stmt.h"
#include "sql/resolver/ddl/ob_drop_index_stmt.h"
#include "sql/resolver/ddl/ob_alter_database_stmt.h"
#include "sql/resolver/ddl/ob_drop_database_stmt.h"
#include "sql/resolver/ddl/ob_create_database_stmt.h"
#include "sql/resolver/ddl/ob_use_database_stmt.h"
#include "sql/resolver/ddl/ob_create_outline_stmt.h"
#include "sql/resolver/ddl/ob_alter_outline_stmt.h"
#include "sql/resolver/ddl/ob_drop_outline_stmt.h"
#include "sql/resolver/ddl/ob_drop_routine_stmt.h"
#include "sql/resolver/ddl/ob_alter_routine_stmt.h"
#include "sql/resolver/ddl/ob_create_package_stmt.h"
#include "sql/resolver/ddl/ob_drop_package_stmt.h"
#include "sql/resolver/ddl/ob_trigger_stmt.h"
#include "sql/resolver/ddl/ob_rename_table_stmt.h"
#include "sql/resolver/ddl/ob_truncate_table_stmt.h"
#include "sql/resolver/ddl/ob_create_table_like_stmt.h"
#include "sql/resolver/ddl/ob_fork_table_stmt.h"
#include "sql/resolver/ddl/ob_fork_database_stmt.h"
#include "sql/resolver/ddl/ob_purge_stmt.h"
#include "sql/resolver/ddl/ob_lock_table_stmt.h"
#include "sql/resolver/dcl/ob_create_user_stmt.h"
#include "sql/resolver/dcl/ob_drop_user_stmt.h"
#include "sql/resolver/dcl/ob_rename_user_stmt.h"
#include "sql/resolver/dcl/ob_lock_user_stmt.h"
#include "sql/resolver/dcl/ob_set_password_stmt.h"
#include "sql/resolver/dcl/ob_grant_stmt.h"
#include "sql/resolver/dcl/ob_revoke_stmt.h"
#include "sql/resolver/dcl/ob_create_role_stmt.h"
#include "sql/resolver/dcl/ob_drop_role_stmt.h"
#include "sql/resolver/dcl/ob_alter_user_role_stmt.h"
#include "sql/resolver/tcl/ob_start_trans_stmt.h"
#include "sql/resolver/tcl/ob_end_trans_stmt.h"
#include "sql/resolver/tcl/ob_savepoint_stmt.h"
#include "sql/resolver/cmd/ob_kill_stmt.h"
#include "sql/resolver/cmd/ob_empty_query_stmt.h"
#include "sql/resolver/cmd/ob_call_procedure_stmt.h"
#include "sql/resolver/cmd/ob_anonymous_block_stmt.h"
#include "sql/resolver/prepare/ob_prepare_stmt.h"
#include "sql/resolver/prepare/ob_execute_stmt.h"
#include "sql/resolver/prepare/ob_deallocate_stmt.h"
#include "sql/resolver/ddl/ob_rename_table_stmt.h"
#include "sql/resolver/ddl/ob_truncate_table_stmt.h"
#include "sql/resolver/ddl/ob_create_table_like_stmt.h"
#include "sql/resolver/ddl/ob_recyclebin_restore_stmt.h"
#include "sql/resolver/ddl/ob_purge_stmt.h"
#include "sql/resolver/ddl/ob_optimize_stmt.h"
#include "sql/engine/ob_exec_context.h"
#include "sql/engine/cmd/ob_empty_query_executor.h"
#include "sql/engine/cmd/ob_dcl_executor.h"
#include "sql/engine/cmd/ob_tcl_executor.h"
#include "sql/engine/cmd/ob_recyclebin_executor.h"
#include "sql/engine/cmd/ob_set_names_executor.h"
#include "sql/engine/cmd/ob_alter_system_executor.h"
#include "sql/engine/cmd/ob_set_password_executor.h"
#include "sql/engine/cmd/ob_database_executor.h"
#include "sql/engine/cmd/ob_table_executor.h"
#include "sql/engine/cmd/ob_index_executor.h"
#include "sql/engine/cmd/ob_kill_executor.h"
#include "sql/engine/cmd/ob_user_cmd_executor.h"
#include "sql/engine/cmd/ob_outline_executor.h"
#include "sql/engine/cmd/ob_routine_executor.h"
#include "sql/engine/cmd/ob_package_executor.h"
#include "sql/engine/cmd/ob_trigger_executor.h"
#include "sql/engine/cmd/ob_analyze_executor.h"
#include "sql/engine/cmd/ob_load_data_executor.h"
#include "sql/engine/cmd/ob_role_cmd_executor.h"
#include "sql/engine/cmd/ob_get_diagnostics_executor.h"
#include "sql/engine/cmd/ob_lock_table_executor.h"
#include "sql/engine/prepare/ob_prepare_executor.h"
#include "sql/engine/prepare/ob_execute_executor.h"
#include "sql/engine/prepare/ob_deallocate_executor.h"
#include "share/ob_structured_event_logger.h"
#include "observer/omt/ob_server_runtime.h"
#include "sql/resolver/dcl/ob_alter_role_stmt.h"
#include "sql/resolver/cmd/ob_merge_table_stmt.h"
#include "sql/engine/cmd/ob_merge_table_executor.h"

namespace oceanbase
{
using namespace common;
namespace sql
{

// Do not inline the executor call to make sure ObCmdExecutor::execute() stack usage under 8K.
#define DEFINE_EXECUTE_CMD(Statement, Executor) \
        [&]() __attribute__((noinline)) { \
        Statement &stmt = *(static_cast<Statement*>(&cmd)); \
        Executor executor; \
        sql_text = (stmt.get_query_ctx() == NULL) ? "" : stmt.get_query_ctx()->get_sql_stmt();         \
        ret = executor.execute(ctx, stmt); \
        }()

int ObCmdExecutor::execute(ObExecContext &ctx, ObICmd &cmd)
{
  int ret = OB_SUCCESS;
  ObString sql_text;
  ObSQLSessionInfo *my_session = ctx.get_my_session();
  bool is_ddl_or_dcl_stmt = false;
  int64_t ori_query_timeout;
  int64_t ori_trx_timeout;
  if (ObStmt::is_ddl_stmt(static_cast<stmt::StmtType>(cmd.get_cmd_type()), true)
      || ObStmt::is_dcl_stmt(static_cast<stmt::StmtType>(cmd.get_cmd_type()))) {
    if (OB_ISNULL(my_session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is null", K(ret));
    } else if (stmt::T_VARIABLE_SET == static_cast<stmt::StmtType>(cmd.get_cmd_type())
        && !static_cast<ObVariableSetStmt*>(&cmd)->has_global_variable()) {
      // Only set global variable is DDL operation, session level variable change is not DDL
      // do nothing
    } else {
      my_session->get_query_timeout(ori_query_timeout);
      my_session->get_tx_timeout(ori_trx_timeout);
      ObObj val;
      val.set_int(GCONF._ob_ddl_timeout);
      is_ddl_or_dcl_stmt = true;
      if (OB_FAIL(my_session->update_sys_variable(
                         share::SYS_VAR_OB_QUERY_TIMEOUT, val))) {
        LOG_WARN("set sys variable failed", K(ret), K(val.get_int()));
      } else if (OB_FAIL(my_session->update_sys_variable(
                         share::SYS_VAR_OB_TRX_TIMEOUT, val))) {
        LOG_WARN("set sys variable failed", K(ret), K(val.get_int()));
      } else {
        ctx.get_physical_plan_ctx()->set_timeout_timestamp(
            my_session->get_query_start_time() + GCONF._ob_ddl_timeout);
        THIS_WORKER.set_timeout_ts(
            my_session->get_query_start_time() + GCONF._ob_ddl_timeout);
      }
      if (OB_SUCC(ret)) {
        // DDL release the specific version of schema_mgr held before sending rpc to RS
        // Avoid the DDL in the queue always occupying slots causing the DDL being processed by RS to have no new slots available and resulting in a deadlock issue
        if (stmt::T_CREATE_OUTLINE == static_cast<stmt::StmtType>(cmd.get_cmd_type())
            || stmt::T_ALTER_OUTLINE == static_cast<stmt::StmtType>(cmd.get_cmd_type())
          // create outline and alter outline will continue to use schema guard to generate logical plan at execute
          // reset delay to ObCreateOutlineExecutor::execute and ObAlterOutlineExecutor::execute
            || (stmt::T_CREATE_TABLE == static_cast<stmt::StmtType>(cmd.get_cmd_type()))
          // ctas needs to use ObSelectStmt in execute_ctas to construct a statement similar to insert into select
          // During the SQL assembly process, the members of ObSelectStmt need to continue relying on the schema obtained from a specific version schema guard
          // reset delay to ObCreateTableExecutor::execute and ObCreateTableExecutor::execute_cta inside
        ) {
        } else if (OB_FAIL(ctx.get_sql_ctx()->schema_guard_->reset())){
          LOG_WARN("schema_guard reset failed", K(ret));
        }
      }
    }
  }
  
  if (OB_FAIL(ret)) {
  } else if (OB_ISNULL(my_session)) {
    ret = OB_ERR_UNEXPECTED;
    LOG_WARN("session is null", KR(ret));
  } else {
  }
  ObDDLCountGuard ddl_guard{};
  if (OB_SUCC(ret)) {
    if (true && GCONF._enable_ddl_worker_isolation
        && ObStmt::is_ddl_stmt(static_cast<stmt::StmtType>(cmd.get_cmd_type()), true)) {
      if (OB_FAIL(ddl_guard.try_inc_ddl_count(GCONF.cpu_quota_concurrency))) {
        LOG_WARN("fail to increment server DDL count", KR(ret));
      }
    }
  }

  if (OB_SUCC(ret)) {
    switch (cmd.get_cmd_type()) {
      case stmt::T_CREATE_VIEW: // fall through
      case stmt::T_CREATE_TABLE: {
        DEFINE_EXECUTE_CMD(ObCreateTableStmt, ObCreateTableExecutor);
        break;
      }
      case stmt::T_ALTER_TABLE: {
        DEFINE_EXECUTE_CMD(ObAlterTableStmt, ObAlterTableExecutor);
        break;
      }
      case stmt::T_START_TRANS: {
        DEFINE_EXECUTE_CMD(ObStartTransStmt, ObStartTransExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_END_TRANS: {
        DEFINE_EXECUTE_CMD(ObEndTransStmt, ObEndTransExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_CREATE_SAVEPOINT: {
        DEFINE_EXECUTE_CMD(ObCreateSavePointStmt, ObCreateSavePointExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_ROLLBACK_SAVEPOINT: {
        DEFINE_EXECUTE_CMD(ObRollbackSavePointStmt, ObRollbackSavePointExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_RELEASE_SAVEPOINT: {
        DEFINE_EXECUTE_CMD(ObReleaseSavePointStmt, ObReleaseSavePointExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_DROP_VIEW: // fall through
      case stmt::T_DROP_TABLE: {
        DEFINE_EXECUTE_CMD(ObDropTableStmt, ObDropTableExecutor);
        break;
      }
      case stmt::T_RENAME_TABLE: {
        DEFINE_EXECUTE_CMD(ObRenameTableStmt, ObRenameTableExecutor);
        break;
      }
      case stmt::T_TRUNCATE_TABLE: {
        DEFINE_EXECUTE_CMD(ObTruncateTableStmt, ObTruncateTableExecutor);
        break;
      }
      case stmt::T_VARIABLE_SET: {
        DEFINE_EXECUTE_CMD(ObVariableSetStmt, ObVariableSetExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_DIAGNOSTICS: {
        DEFINE_EXECUTE_CMD(ObGetDiagnosticsStmt, ObGetDiagnosticsExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_CREATE_DATABASE: {
        DEFINE_EXECUTE_CMD(ObCreateDatabaseStmt, ObCreateDatabaseExecutor);
        break;
      }
      case stmt::T_USE_DATABASE: {
        DEFINE_EXECUTE_CMD(ObUseDatabaseStmt, ObUseDatabaseExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_ALTER_DATABASE: {
        DEFINE_EXECUTE_CMD(ObAlterDatabaseStmt, ObAlterDatabaseExecutor);
        break;
      }
      case stmt::T_DROP_DATABASE: {
        DEFINE_EXECUTE_CMD(ObDropDatabaseStmt, ObDropDatabaseExecutor);
        break;
      }
      case stmt::T_CREATE_INDEX: {
        DEFINE_EXECUTE_CMD(ObCreateIndexStmt, ObCreateIndexExecutor);
        break;
      }
      case stmt::T_DROP_INDEX: {
        DEFINE_EXECUTE_CMD(ObDropIndexStmt, ObDropIndexExecutor);
        break;
      }
      case stmt::T_ALTER_VIEW: {
        break;
      }
      case stmt::T_CREATE_TABLE_LIKE: {
        DEFINE_EXECUTE_CMD(ObCreateTableLikeStmt, ObCreateTableLikeExecutor);
        break;
      }
      case stmt::T_FORK_TABLE: {
        DEFINE_EXECUTE_CMD(ObForkTableStmt, ObForkTableExecutor);
        break;
      }
      case stmt::T_FORK_DATABASE: {
        DEFINE_EXECUTE_CMD(ObForkDatabaseStmt, ObForkDatabaseExecutor);
        break;
      }
      case stmt::T_MERGE_TABLE: {
        DEFINE_EXECUTE_CMD(ObMergeTableStmt, ObMergeTableExecutor);
        break;
      }
      case stmt::T_RECYCLEBIN_RESTORE_TABLE: {
        DEFINE_EXECUTE_CMD(ObRecyclebinRestoreTableStmt, ObRecyclebinRestoreTableExecutor);
        break;
      }
      case stmt::T_RECYCLEBIN_RESTORE_DATABASE: {
        DEFINE_EXECUTE_CMD(ObRecyclebinRestoreDatabaseStmt, ObRecyclebinRestoreDatabaseExecutor);
        break;
      }
      case stmt::T_PURGE_TABLE: {
        DEFINE_EXECUTE_CMD(ObPurgeTableStmt, ObPurgeTableExecutor);
        break;
      }
      case stmt::T_PURGE_INDEX: {
        DEFINE_EXECUTE_CMD(ObPurgeIndexStmt, ObPurgeIndexExecutor);
        break;
      }
      case stmt::T_PURGE_DATABASE: {
        DEFINE_EXECUTE_CMD(ObPurgeDatabaseStmt, ObPurgeDatabaseExecutor);
        break;
      }
      case stmt::T_PURGE_RECYCLEBIN: {
        DEFINE_EXECUTE_CMD(ObPurgeRecycleBinStmt, ObPurgeRecycleBinExecutor);
        break;
      }
      case stmt::T_OPTIMIZE_TABLE: {
        DEFINE_EXECUTE_CMD(ObOptimizeTableStmt, ObOptimizeTableExecutor);
        break;
      }
      case stmt::T_CREATE_USER: {
        DEFINE_EXECUTE_CMD(ObCreateUserStmt, ObCreateUserExecutor);
        break;
      }
      case stmt::T_ALTER_USER_ROLE:
      case stmt::T_ALTER_USER: {
        DEFINE_EXECUTE_CMD(ObAlterUserRoleStmt, ObAlterUserRoleExecutor);
        break;
      }

      case stmt::T_DROP_USER: {
        DEFINE_EXECUTE_CMD(ObDropUserStmt, ObDropUserExecutor);
        break;
      }
      case stmt::T_RENAME_USER: {
        DEFINE_EXECUTE_CMD(ObRenameUserStmt, ObRenameUserExecutor);
        break;
      }
      case stmt::T_SET_PASSWORD: {
        DEFINE_EXECUTE_CMD(ObSetPasswordStmt, ObSetPasswordExecutor);
        break;
      }
      case stmt::T_LOCK_USER: {
        DEFINE_EXECUTE_CMD(ObLockUserStmt, ObLockUserExecutor);
        break;
      }
      case stmt::T_SYSTEM_GRANT:
      case stmt::T_GRANT_ROLE:
      case stmt::T_GRANT: {
        DEFINE_EXECUTE_CMD(ObGrantStmt, ObGrantExecutor);
        break;
      }
      case stmt::T_SYSTEM_REVOKE:
      case stmt::T_REVOKE_ROLE:
      case stmt::T_REVOKE: {
        DEFINE_EXECUTE_CMD(ObRevokeStmt, ObRevokeExecutor);
        break;
      }
      case stmt::T_PREPARE: {
        DEFINE_EXECUTE_CMD(ObPrepareStmt, ObPrepareExecutor);
        break;
      }
      case stmt::T_EXECUTE: {
        // only call procedure run this logic, text ps mode execute call procedure,
        // if procedure has out param, it should return result to argument
        ObExecuteStmt &stmt = *(static_cast<ObExecuteStmt*>(&cmd));
        if (stmt::T_CALL_PROCEDURE != stmt.get_prepare_type()) {
          ret = OB_ERR_UNEXPECTED;
          LOG_WARN("ps text shoudle be handled as normal query, not cmd", K(ret));
        } else {
          DEFINE_EXECUTE_CMD(ObExecuteStmt, ObExecuteExecutor);
        }
        break;
      }
      case stmt::T_DEALLOCATE: {
        DEFINE_EXECUTE_CMD(ObDeallocateStmt, ObDeallocateExecutor);
        break;
      }
      case stmt::T_FREEZE: {
        DEFINE_EXECUTE_CMD(ObFreezeStmt, ObFreezeExecutor);
        break;
      }
      case stmt::T_FLUSH_CACHE: {
        DEFINE_EXECUTE_CMD(ObFlushCacheStmt, ObFlushCacheExecutor);
        break;
      }
      case stmt::T_FLUSH_KVCACHE: {
        DEFINE_EXECUTE_CMD(ObFlushKVCacheStmt, ObFlushKVCacheExecutor);
        break;
      }
      case stmt::T_FLUSH_ILOGCACHE: {
        DEFINE_EXECUTE_CMD(ObFlushIlogCacheStmt, ObFlushIlogCacheExecutor);
        break;
      }
      case stmt::T_FLUSH_DAG_WARNINGS: {
        DEFINE_EXECUTE_CMD(ObFlushDagWarningsStmt, ObFlushDagWarningsExecutor);
        break;
      }
      case stmt::T_ADMIN_MERGE: {
        DEFINE_EXECUTE_CMD(ObAdminMergeStmt, ObAdminMergeExecutor);
        break;
      }
      case stmt::T_REFRESH_MEMORY_STAT: {
        DEFINE_EXECUTE_CMD(ObRefreshMemStatStmt, ObRefreshMemStatExecutor);
        break;
      }
      case stmt::T_REFRESH_IO_CALIBRATION: {
        DEFINE_EXECUTE_CMD(ObRefreshIOCalibraitonStmt, ObRefreshIOCalibraitonExecutor);
        break;
      }
      case stmt::T_ALTER_SYSTEM_SET_PARAMETER: {
        DEFINE_EXECUTE_CMD(ObSetConfigStmt, ObSetConfigExecutor);
        break;
      }
      case stmt::T_ALTER_SYSTEM_SETTP: {
        DEFINE_EXECUTE_CMD(ObSetTPStmt, ObSetTPExecutor);
        break;
      }
      case stmt::T_CLEAR_MERGE_ERROR: {
        DEFINE_EXECUTE_CMD(ObClearMergeErrorStmt, ObClearMergeErrorExecutor);
        break;
      }
      case stmt::T_CANCEL_TASK: {
        DEFINE_EXECUTE_CMD(ObCancelTaskStmt, ObCancelTaskExecutor);
        break;
      }
      case stmt::T_SET_NAMES: {
        DEFINE_EXECUTE_CMD(ObSetNamesStmt, ObSetNamesExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_LOAD_DATA: {
        DEFINE_EXECUTE_CMD(ObLoadDataStmt, ObLoadDataExecutor);
        break;
      }
      case stmt::T_KILL: {
        DEFINE_EXECUTE_CMD(ObKillStmt, ObKillExecutor);
        break;
      }
      case stmt::T_EMPTY_QUERY: {
        DEFINE_EXECUTE_CMD(ObEmptyQueryStmt, ObEmptyQueryExecutor);
        break;
      }
      case stmt::T_LOCK_TABLE: {
        DEFINE_EXECUTE_CMD(ObLockTableStmt, ObLockTableExecutor);
        break;
      }
      case stmt::T_CREATE_OUTLINE: {
        DEFINE_EXECUTE_CMD(ObCreateOutlineStmt, ObCreateOutlineExecutor);
        break;
      }
      case stmt::T_ALTER_OUTLINE: {
        DEFINE_EXECUTE_CMD(ObAlterOutlineStmt, ObAlterOutlineExecutor);
        break;
      }
      case stmt::T_DROP_OUTLINE: {
        DEFINE_EXECUTE_CMD(ObDropOutlineStmt, ObDropOutlineExecutor);
        break;
      }
      case stmt::T_CREATE_ROUTINE: {
        DEFINE_EXECUTE_CMD(ObCreateRoutineStmt, ObCreateRoutineExecutor);
        break;
      }
      case stmt::T_ALTER_ROUTINE: {
        DEFINE_EXECUTE_CMD(ObAlterRoutineStmt, ObAlterRoutineExecutor);
        break;
      }
      case stmt::T_DROP_ROUTINE: {
        DEFINE_EXECUTE_CMD(ObDropRoutineStmt, ObDropRoutineExecutor);
        break;
      }
      case stmt::T_CALL_PROCEDURE: {
        DEFINE_EXECUTE_CMD(ObCallProcedureStmt, ObCallProcedureExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_ANONYMOUS_BLOCK: {
        DEFINE_EXECUTE_CMD(ObAnonymousBlockStmt, ObAnonymousBlockExecutor);
        sql_text = ObString::make_empty_string();  // do not record
        break;
      }
      case stmt::T_CREATE_PACKAGE: {
        DEFINE_EXECUTE_CMD(ObCreatePackageStmt, ObCreatePackageExecutor);
        break;
      }
      case stmt::T_DROP_PACKAGE: {
        DEFINE_EXECUTE_CMD(ObDropPackageStmt, ObDropPackageExecutor);
        break;
      }
      case stmt::T_CREATE_TRIGGER: {
        DEFINE_EXECUTE_CMD(ObCreateTriggerStmt, ObCreateTriggerExecutor);
        break;
      }
      case stmt::T_DROP_TRIGGER: {
        DEFINE_EXECUTE_CMD(ObDropTriggerStmt, ObDropTriggerExecutor);
        break;
      }
      case stmt::T_ALTER_TRIGGER: {
        DEFINE_EXECUTE_CMD(ObAlterTriggerStmt, ObAlterTriggerExecutor);
        break;
      }
      case stmt::T_ANALYZE: {
        DEFINE_EXECUTE_CMD(ObAnalyzeStmt, ObAnalyzeExecutor);
        break;
      }
      case stmt::T_SET_TABLE_COMMENT:
      case stmt::T_SET_COLUMN_COMMENT: {
        ObAlterTableStmt &stmt = *(static_cast<ObAlterTableStmt*>(&cmd));
        
        bool is_parallel_ddl = true;
        if (OB_FAIL(ObParallelDDLControlMode::is_parallel_ddl_enable(
                           ObParallelDDLControlMode::SET_COMMENT, is_parallel_ddl))) {
          LOG_WARN("fail to get whether is parallel set comment", KR(ret));
        } else if (!is_parallel_ddl) {
          DEFINE_EXECUTE_CMD(ObAlterTableStmt, ObAlterTableExecutor);
        } else {
          DEFINE_EXECUTE_CMD(ObAlterTableStmt, ObCommentExecutor);
        }
        break;
      }
      case stmt::T_CREATE_ROLE: {
        DEFINE_EXECUTE_CMD(ObCreateRoleStmt, ObCreateRoleExecutor);
        break;
      }
      case stmt::T_DROP_ROLE: {
        DEFINE_EXECUTE_CMD(ObDropRoleStmt, ObDropRoleExecutor);
        break;
      }
      case stmt::T_ALTER_ROLE: {
        DEFINE_EXECUTE_CMD(ObAlterRoleStmt, ObAlterRoleExecutor);
        break;
      }
      case stmt::T_ALTER_SYSTEM_RESET_PARAMETER: {
        DEFINE_EXECUTE_CMD(ObResetConfigStmt, ObResetConfigExecutor);
        break;
      }
      case stmt::T_TABLET_CMD:
      case stmt::T_DROP_MEMTABLE:
      case stmt::T_CLEAR_MEMTABLE:
      case stmt::T_CHECK_ROOT_TABLE:
      default: {
        ret = OB_ERR_UNEXPECTED;
        LOG_WARN("unknow cmd type", "cmd_type", cmd.get_cmd_type(), "T_MAX", T_MAX);
        break;
      }
    }
  }
  if (!sql_text.empty()) {
    SERVER_EVENT_ADD("sql", "execute_cmd",
                     "cmd_type", cmd.get_cmd_type(),
                     "sql_text", ObHexEscapeSqlStr(ctx.get_sql_ctx()->is_sensitive_ ?
                                                   ObString(OB_MASKED_STR) : sql_text),
                     "return_code", ret);
  }

  if (is_ddl_or_dcl_stmt) {
    // ddl/dcl execution process modified the session's query_timeout and trx_timeout, need to restore after execution
    int tmp_ret = ret;
    ObObj ori_query_timeout_obj;
    ObObj ori_trx_timeout_obj;
    ori_query_timeout_obj.set_int(ori_query_timeout);
    ori_trx_timeout_obj.set_int(ori_trx_timeout);
    if (OB_ISNULL(my_session)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("session is null", K(ret));
    } else if (OB_ISNULL(ctx.get_task_exec_ctx().schema_service_)) {
      ret = OB_ERR_UNEXPECTED;
      LOG_WARN("schema_service_ is null", K(ret));
    } else if (OB_FAIL(my_session->update_sys_variable(
                       share::SYS_VAR_OB_QUERY_TIMEOUT,
                       ori_query_timeout_obj))) {
      LOG_WARN("set sys variable failed", K(ret),
                                          K(ori_query_timeout_obj.get_int()));
    } else if (OB_FAIL(my_session->update_sys_variable(
                       share::SYS_VAR_OB_TRX_TIMEOUT,
                       ori_trx_timeout_obj))) {
      LOG_WARN("set sys variable failed", K(ret),
                                          K(ori_trx_timeout_obj.get_int()));
    } else if (OB_FAIL(ctx.get_task_exec_ctx().schema_service_->get_runtime_schema_guard(
                       *(ctx.get_sql_ctx()->schema_guard_)))) {
      LOG_WARN("failed to get schema guard", K(ret));
    }
    if (OB_FAIL(tmp_ret)) {
      // overwrite ret
      ret = tmp_ret;
    }
  }

  return ret;
}

#undef DEFINE_EXECUTE_CMD

}/* ns sql*/
}/* ns oceanbase */
