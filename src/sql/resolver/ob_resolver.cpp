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

#define USING_LOG_PREFIX SQL_RESV
#include "sql/resolver/ob_resolver.h"
#include "lib/utility/ob_smart_call.h"
#include "lib/stat/ob_diagnostic_info_guard.h"
#include "sql/resolver/cmd/ob_alter_system_resolver.h"
#include "sql/resolver/dml/ob_insert_resolver.h"
#include "sql/resolver/dml/ob_update_resolver.h"
#include "sql/resolver/dml/ob_delete_resolver.h"
#include "sql/resolver/tcl/transaction_resolver.h"
#include "sql/resolver/prepare/prepared_command_resolver.h"
#include "sql/resolver/dcl/access_control_resolver.h"
#include "sql/syntax.h"
#include "sql/resolver/ddl/ob_create_table_resolver.h"
#include "sql/resolver/ddl/ob_rename_table_resolver.h"
#include "sql/resolver/ddl/ob_truncate_table_resolver.h"
#include "sql/resolver/ddl/ob_fork_table_resolver.h"
#include "sql/resolver/ddl/ob_fork_database_resolver.h"
#include "sql/resolver/cmd/merge_table_resolver.h"
#include "sql/resolver/cmd/diff_table_resolver.h"
#include "sql/resolver/ddl/ob_create_table_like_resolver.h"
#include "sql/resolver/ddl/ob_alter_table_resolver.h"
#include "sql/resolver/ddl/alter_database_resolver.h"
#include "sql/resolver/ddl/ob_drop_table_resolver.h"
#include "sql/resolver/ddl/ob_create_index_resolver.h"
#include "sql/resolver/ddl/ob_drop_index_resolver.h"
#include "sql/resolver/ddl/ob_create_database_resolver.h"
#include "sql/resolver/ddl/ob_use_database_resolver.h"
#include "sql/resolver/ddl/ob_drop_database_resolver.h"
#include "sql/resolver/ddl/ob_create_view_resolver.h"
#include "sql/resolver/ddl/outline_resolver.h"
#include "sql/resolver/ddl/lock_table_resolver.h"
#include "sql/resolver/ddl/ob_explain_resolver.h"
#include "sql/resolver/ddl/ob_drop_routine_resolver.h"
#include "sql/resolver/ddl/ob_trigger_resolver.h"
#include "sql/resolver/ddl/ob_optimize_resolver.h"
#include "ddl/ob_drop_routine_resolver.h"
#include "ddl/ob_alter_routine_resolver.h"
#include "sql/resolver/ddl/ob_create_package_resolver.h"
#include "sql/resolver/ddl/ob_drop_package_resolver.h"
#include "sql/resolver/ddl/ob_purge_resolver.h"
#include "sql/resolver/ddl/restore_table_resolver.h"
#include "sql/resolver/ddl/restore_database_resolver.h"
#include "sql/resolver/ddl/ob_analyze_stmt_resolver.h"
#include "sql/resolver/cmd/ob_variable_set_resolver.h"
#include "sql/resolver/cmd/ob_show_resolver.h"
#include "sql/resolver/cmd/ob_kill_resolver.h"
#include "sql/resolver/cmd/load_data_resolver.h"
#include "sql/resolver/cmd/ob_set_names_resolver.h"
#include "sql/resolver/cmd/ob_set_transaction_resolver.h"
#include "sql/resolver/cmd/ob_empty_query_resolver.h"
#include "sql/resolver/cmd/ob_anonymous_block_resolver.h"
#include "sql/resolver/cmd/ob_call_procedure_resolver.h"
#include "sql/resolver/expr/ob_raw_expr_wrap_enum_set.h"
#include "sql/resolver/cmd/ob_get_diagnostics_resolver.h"
#include "sql/pl/ob_pl_package.h"

namespace oceanbase
{
using namespace common;
using namespace share;
namespace sql
{
ObResolver::ObResolver(ObResolverParams &params)
    : params_(params)
{

}

ObResolver::~ObResolver()
{
}

template <typename ResolverType, typename Syntax>
int ObResolver::stmt_resolver_func(ObResolverParams &params, const Syntax &parse_tree, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  HEAP_VAR(ResolverType, stmt_resolver, params) {
    if (OB_FAIL(SMART_CALL(stmt_resolver.resolve(parse_tree)))) {
    }
    stmt = stmt_resolver.get_basic_stmt();
  }
  return ret;
}

template <typename SelectResolverType>
int ObResolver::select_stmt_resolver_func(ObResolverParams &params, const peg::Node &parse_tree, ObStmt *&stmt)
{
  int ret = OB_SUCCESS;
  HEAP_VAR(SelectResolverType, stmt_resolver, params) {
    stmt_resolver.set_calc_found_rows(true);
    stmt_resolver.set_has_top_limit(true);
    if (OB_FAIL(SMART_CALL(stmt_resolver.resolve(parse_tree)))) {
    }
    stmt = stmt_resolver.get_basic_stmt();
  }
  return ret;
}

int ObResolver::check_parameters(const ParsedSql &parsed)
{
  const auto &metadata = parsed.metadata();
  const auto &parameters = metadata.question_mark_ctx_;
  if (parameters.by_ordinal_ && parameters.by_name_) {
    _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "Ordinal and named parameters cannot be combined");
    return OB_INVALID_ARGUMENT;
  }
  if (parameters.count_ == 0 || metadata.parse_mode_ != peg::ParseMode::Sql || metadata.pl_context_) {
    return OB_SUCCESS;
  }
  using Kind = peg::NodeKind;
  switch (parsed.statement()->kind()) {
    case Kind::Select: case Kind::Insert: case Kind::Update: case Kind::Delete:
    case Kind::Explain: case Kind::CreateOutline: case Kind::AlterOutline:
    case Kind::Prepare: case Kind::VariableSet: case Kind::Kill: case Kind::Diagnostics:
    case Kind::CreateView:
      return OB_SUCCESS;
    case Kind::CreateTable:
      if (peg::table_definition(*parsed.statement()).select_mode != peg::TableSelectMode::None) return OB_SUCCESS;
      break;
    default:
      break;
  }
  _LOG_USER_MSG(USER_ERROR, OB_INVALID_ARGUMENT, "Statement does not accept parameter markers");
  return OB_INVALID_ARGUMENT;
}

int ObResolver::resolve(const ParsedSql &parsed, ObStmt *&stmt)
{
  if (parsed.statement() == nullptr) { return OB_ERR_UNEXPECTED; }
  const int ret = check_parameters(parsed);
  if (ret != OB_SUCCESS) return ret;
  params_.syntax_ = &parsed;
  return resolve(*parsed.statement(), stmt);
}

int ObResolver::resolve_anonymous(const ParsedSql &parsed, ObStmt *&stmt)
{
  const int checked = check_parameters(parsed);
  if (checked != OB_SUCCESS) return checked;
  params_.resolver_scope_stmt_type_ = T_SP_ANONYMOUS_BLOCK;
  params_.session_info_->set_stmt_type(stmt::T_ANONYMOUS_BLOCK);
  params_.syntax_ = &parsed;
  const int ret = stmt_resolver_func<ObAnonymousBlockResolver>(params_, parsed, stmt);
  return ret == OB_SUCCESS ? finish_resolution(stmt) : ret;
}

int ObResolver::resolve(const peg::Node &statement, ObStmt *&stmt)
{
  ACTIVE_SESSION_FLAG_SETTER_GUARD(in_resolve);
  int ret = OB_SUCCESS;
  stmt::StmtType type = stmt::T_NONE;
  if (OB_ISNULL(params_.allocator_) || OB_ISNULL(params_.schema_checker_)
      || OB_ISNULL(params_.session_info_) || OB_ISNULL(params_.expr_factory_)
      || OB_ISNULL(params_.query_ctx_)) {
    ret = OB_NOT_INIT;
  } else if (OB_FAIL(statement_type(statement, type))) {
  } else {
    params_.session_info_->set_stmt_type(type);
    ObExecContext *exec_ctx = params_.session_info_->get_cur_exec_ctx();
    if (exec_ctx != nullptr && exec_ctx->get_sql_ctx() != nullptr) {
      exec_ctx->get_sql_ctx()->stmt_type_ = type;
    }
    switch (statement.kind()) {
      case peg::NodeKind::PlBlock:
      case peg::NodeKind::PlLabeledBlock:
        params_.resolver_scope_stmt_type_ = T_SP_ANONYMOUS_BLOCK;
        ret = stmt_resolver_func<ObAnonymousBlockResolver>(params_, *params_.syntax_, stmt);
        break;
      case peg::NodeKind::Select:
        params_.resolver_scope_stmt_type_ = T_SELECT;
        ret = select_stmt_resolver_func<ObSelectResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Insert:
      case peg::NodeKind::Replace:
        params_.resolver_scope_stmt_type_ = T_INSERT;
        ret = stmt_resolver_func<ObInsertResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Update:
        params_.resolver_scope_stmt_type_ = T_UPDATE;
        ret = stmt_resolver_func<ObUpdateResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Delete:
        params_.resolver_scope_stmt_type_ = T_DELETE;
        ret = stmt_resolver_func<ObDeleteResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Explain:
        params_.resolver_scope_stmt_type_ = T_EXPLAIN;
        ret = stmt_resolver_func<ObExplainResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateView:
        params_.resolver_scope_stmt_type_ = T_CREATE_VIEW;
        ret = stmt_resolver_func<ObCreateViewResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateOutline:
      case peg::NodeKind::AlterOutline:
      case peg::NodeKind::DropOutline:
        params_.resolver_scope_stmt_type_ = statement.kind() == peg::NodeKind::CreateOutline
            ? T_CREATE_OUTLINE : statement.kind() == peg::NodeKind::AlterOutline
            ? T_ALTER_OUTLINE : T_DROP_OUTLINE;
        ret = stmt_resolver_func<OutlineResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::VariableSet:
        params_.resolver_scope_stmt_type_ = T_VARIABLE_SET;
        ret = stmt_resolver_func<ObVariableSetResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SetNames:
      case peg::NodeKind::SetCharset:
        params_.resolver_scope_stmt_type_ = T_SET_NAMES;
        ret = stmt_resolver_func<ObSetNamesResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::LockTables:
      case peg::NodeKind::UnlockTables:
        params_.resolver_scope_stmt_type_ = T_LOCK_TABLE;
        ret = stmt_resolver_func<LockTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::LoadData:
        params_.resolver_scope_stmt_type_ = T_LOAD_DATA;
        ret = stmt_resolver_func<LoadDataResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Kill:
        params_.resolver_scope_stmt_type_ = T_KILL;
        ret = stmt_resolver_func<ObKillResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Diagnostics:
        params_.resolver_scope_stmt_type_ = T_DIAGNOSTICS;
        ret = stmt_resolver_func<ObGetDiagnosticsResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateTable:
        params_.resolver_scope_stmt_type_ = T_CREATE_TABLE;
        ret = stmt_resolver_func<ObCreateTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::AlterTable:
        params_.resolver_scope_stmt_type_ = T_ALTER_TABLE;
        ret = stmt_resolver_func<ObAlterTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateIndex:
        params_.resolver_scope_stmt_type_ = T_CREATE_INDEX;
        ret = stmt_resolver_func<ObCreateIndexResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropView:
        params_.resolver_scope_stmt_type_ = T_DROP_VIEW;
        ret = stmt_resolver_func<ObDropTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropTable:
        params_.resolver_scope_stmt_type_ = T_DROP_TABLE;
        ret = stmt_resolver_func<ObDropTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropIndex:
        params_.resolver_scope_stmt_type_ = T_DROP_INDEX;
        ret = stmt_resolver_func<ObDropIndexResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateTableLike:
        params_.resolver_scope_stmt_type_ = T_CREATE_TABLE_LIKE;
        ret = stmt_resolver_func<ObCreateTableLikeResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ForkTable:
        params_.resolver_scope_stmt_type_ = T_FORK_TABLE;
        ret = stmt_resolver_func<ObForkTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ForkDatabase:
        params_.resolver_scope_stmt_type_ = T_FORK_DATABASE;
        ret = stmt_resolver_func<ObForkDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DiffTable:
        params_.resolver_scope_stmt_type_ = T_DIFF_TABLE;
        ret = select_stmt_resolver_func<DiffTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::MergeTable:
        params_.resolver_scope_stmt_type_ = T_MERGE_TABLE;
        ret = stmt_resolver_func<MergeTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Freeze:
        params_.resolver_scope_stmt_type_ = T_FREEZE;
        ret = stmt_resolver_func<ObFreezeResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SuspendMerge:
      case peg::NodeKind::ResumeMerge:
        params_.resolver_scope_stmt_type_ = T_MERGE_CONTROL;
        ret = stmt_resolver_func<MergeControlResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ClearMergeError:
        params_.resolver_scope_stmt_type_ = T_CLEAR_MERGE_ERROR;
        ret = stmt_resolver_func<ClearMergeErrorResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CancelTask:
        params_.resolver_scope_stmt_type_ = T_CANCEL_TASK;
        ret = stmt_resolver_func<CancelTaskResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::FlushCache:
        params_.resolver_scope_stmt_type_ = T_FLUSH_CACHE;
        ret = stmt_resolver_func<ObFlushCacheResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::FlushKvCache:
        params_.resolver_scope_stmt_type_ = T_FLUSH_KVCACHE;
        ret = stmt_resolver_func<ObFlushKVCacheResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::FlushDagWarnings:
        params_.resolver_scope_stmt_type_ = T_FLUSH_DAG_WARNINGS;
        ret = stmt_resolver_func<FlushDagWarningsResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::RefreshIoCalibration:
        params_.resolver_scope_stmt_type_ = T_REFRESH_IO_CALIBRATION;
        ret = stmt_resolver_func<RefreshIoCalibrationResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SwitchoverToPrimary:
        params_.resolver_scope_stmt_type_ = T_SWITCHOVER_TO_PRIMARY;
        ret = stmt_resolver_func<SwitchRoleResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SwitchoverToStandby:
        params_.resolver_scope_stmt_type_ = T_SWITCHOVER_TO_STANDBY;
        ret = stmt_resolver_func<SwitchRoleResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ActivateStandby:
        params_.resolver_scope_stmt_type_ = T_ACTIVATE_STANDBY;
        ret = stmt_resolver_func<SwitchRoleResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SetConfig:
        params_.resolver_scope_stmt_type_ = T_ALTER_SYSTEM_SET_PARAMETER;
        ret = stmt_resolver_func<ObSetConfigResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SetTracepoint:
        params_.resolver_scope_stmt_type_ = T_ALTER_SYSTEM_SETTP;
        ret = stmt_resolver_func<ObSetTPResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateDatabase:
        params_.resolver_scope_stmt_type_ = T_CREATE_DATABASE;
        ret = stmt_resolver_func<ObCreateDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::AlterDatabase:
        params_.resolver_scope_stmt_type_ = T_ALTER_DATABASE;
        ret = stmt_resolver_func<AlterDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::UseDatabase:
        params_.resolver_scope_stmt_type_ = T_USE_DATABASE;
        ret = stmt_resolver_func<ObUseDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropDatabase:
        params_.resolver_scope_stmt_type_ = T_DROP_DATABASE;
        ret = stmt_resolver_func<ObDropDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::RenameTables:
        params_.resolver_scope_stmt_type_ = T_RENAME_TABLE;
        ret = stmt_resolver_func<ObRenameTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::TruncateTable:
        params_.resolver_scope_stmt_type_ = T_TRUNCATE_TABLE;
        ret = stmt_resolver_func<ObTruncateTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::RestoreTable:
        params_.resolver_scope_stmt_type_ = T_RECYCLEBIN_RESTORE_TABLE;
        ret = stmt_resolver_func<RestoreTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::RestoreDatabase:
        params_.resolver_scope_stmt_type_ = T_RECYCLEBIN_RESTORE_DATABASE;
        ret = stmt_resolver_func<RestoreDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::PurgeTable:
        params_.resolver_scope_stmt_type_ = T_PURGE_TABLE;
        ret = stmt_resolver_func<ObPurgeTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::PurgeIndex:
        params_.resolver_scope_stmt_type_ = T_PURGE_INDEX;
        ret = stmt_resolver_func<ObPurgeIndexResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::PurgeDatabase:
        params_.resolver_scope_stmt_type_ = T_PURGE_DATABASE;
        ret = stmt_resolver_func<ObPurgeDatabaseResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::PurgeRecyclebin:
        params_.resolver_scope_stmt_type_ = T_PURGE_RECYCLEBIN;
        ret = stmt_resolver_func<ObPurgeRecycleBinResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::OptimizeTable:
        params_.resolver_scope_stmt_type_ = T_OPTIMIZE_TABLE;
        ret = stmt_resolver_func<ObOptimizeTableResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::SetTransaction:
        params_.resolver_scope_stmt_type_ = T_TRANSACTION;
        ret = stmt_resolver_func<ObSetTransactionResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowTables:
        params_.resolver_scope_stmt_type_ = T_SHOW_TABLES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowTableStatus:
        params_.resolver_scope_stmt_type_ = T_SHOW_TABLE_STATUS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowProcedureStatus:
        params_.resolver_scope_stmt_type_ = T_SHOW_PROCEDURE_STATUS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowFunctionStatus:
        params_.resolver_scope_stmt_type_ = T_SHOW_FUNCTION_STATUS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowTriggers:
        params_.resolver_scope_stmt_type_ = T_SHOW_TRIGGERS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowProfile:
        params_.resolver_scope_stmt_type_ = T_SHOW_PROFILE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CheckTable:
        params_.resolver_scope_stmt_type_ = T_SHOW_CHECK_TABLE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowDatabases:
        params_.resolver_scope_stmt_type_ = T_SHOW_DATABASES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowVariables:
        params_.resolver_scope_stmt_type_ = T_SHOW_VARIABLES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowStatus:
        params_.resolver_scope_stmt_type_ = T_SHOW_STATUS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowColumns:
        params_.resolver_scope_stmt_type_ = T_SHOW_COLUMNS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateDatabase:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_DATABASE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateUser:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_USER;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateTable:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_TABLE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateView:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_VIEW;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateProcedure:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_PROCEDURE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateFunction:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_FUNCTION;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowParameters:
        params_.resolver_scope_stmt_type_ = T_SHOW_PARAMETERS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowTrace:
        params_.resolver_scope_stmt_type_ = T_SHOW_TRACE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowIndexes:
        params_.resolver_scope_stmt_type_ = T_SHOW_INDEXES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowProcesslist:
        params_.resolver_scope_stmt_type_ = T_SHOW_PROCESSLIST;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowWarnings:
        params_.resolver_scope_stmt_type_ = T_SHOW_WARNINGS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowEngines:
        params_.resolver_scope_stmt_type_ = T_SHOW_ENGINES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowEngine:
        params_.resolver_scope_stmt_type_ = T_SHOW_ENGINE;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowOpenTables:
        params_.resolver_scope_stmt_type_ = T_SHOW_OPEN_TABLES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowPrivileges:
        params_.resolver_scope_stmt_type_ = T_SHOW_PRIVILEGES;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowRecyclebin:
        params_.resolver_scope_stmt_type_ = T_SHOW_RECYCLEBIN;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowErrors:
        params_.resolver_scope_stmt_type_ = T_SHOW_ERRORS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCharset:
        params_.resolver_scope_stmt_type_ = T_SHOW_CHARSET;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCollation:
        params_.resolver_scope_stmt_type_ = T_SHOW_COLLATION;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowGrants:
        params_.resolver_scope_stmt_type_ = T_SHOW_GRANTS;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ShowCreateTrigger:
        params_.resolver_scope_stmt_type_ = T_SHOW_CREATE_TRIGGER;
        ret = stmt_resolver_func<ObShowResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::EmptyQuery:
      case peg::NodeKind::FlushPrivileges:
        params_.resolver_scope_stmt_type_ = T_EMPTY_QUERY;
        ret = stmt_resolver_func<ObEmptyQueryResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateProcedure:
        params_.resolver_scope_stmt_type_ = T_SP_CREATE;
        ret = stmt_resolver_func<ObCreateRoutineResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::AlterProcedure:
        params_.resolver_scope_stmt_type_ = T_SP_ALTER;
        ret = stmt_resolver_func<ObAlterProcedureResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::PlCall:
        params_.resolver_scope_stmt_type_ = T_SP_CALL_STMT;
        ret = stmt_resolver_func<ObCallProcedureResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateFunction:
        params_.resolver_scope_stmt_type_ = T_SF_CREATE;
        ret = stmt_resolver_func<ObCreateRoutineResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::AlterFunction:
        params_.resolver_scope_stmt_type_ = T_SF_ALTER;
        ret = stmt_resolver_func<ObAlterFunctionResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropProcedure:
        params_.resolver_scope_stmt_type_ = T_SP_DROP;
        ret = stmt_resolver_func<ObDropProcedureResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropFunction:
        params_.resolver_scope_stmt_type_ = T_SF_DROP;
        ret = stmt_resolver_func<ObDropFunctionResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreatePackage:
        params_.resolver_scope_stmt_type_ = T_PACKAGE_CREATE;
        ret = stmt_resolver_func<ObCreatePackageResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreatePackageBody:
        params_.resolver_scope_stmt_type_ = T_PACKAGE_CREATE_BODY;
        ret = stmt_resolver_func<ObCreatePackageBodyResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropPackage:
        params_.resolver_scope_stmt_type_ = T_PACKAGE_DROP;
        ret = stmt_resolver_func<ObDropPackageResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Analyze:
        params_.resolver_scope_stmt_type_ = T_MYSQL_ANALYZE;
        ret = stmt_resolver_func<ObAnalyzeStmtResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::CreateTrigger:
        params_.resolver_scope_stmt_type_ = T_TG_CREATE;
        ret = stmt_resolver_func<ObTriggerResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::DropTrigger:
        params_.resolver_scope_stmt_type_ = T_TG_DROP;
        ret = stmt_resolver_func<ObTriggerResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::ResetConfig:
        params_.resolver_scope_stmt_type_ = T_ALTER_SYSTEM_RESET_PARAMETER;
        ret = stmt_resolver_func<ObResetConfigResolver>(params_, statement, stmt);
        break;
      case peg::NodeKind::Prepare:
      case peg::NodeKind::Execute:
      case peg::NodeKind::Deallocate:
        return resolve_prepared_command(statement, params_, stmt);
      case peg::NodeKind::CreateUser:
      case peg::NodeKind::DropUser:
      case peg::NodeKind::LockUser:
      case peg::NodeKind::RenameUser:
      case peg::NodeKind::SetPassword:
      case peg::NodeKind::CreateRole:
      case peg::NodeKind::DropRole:
      case peg::NodeKind::Grant:
      case peg::NodeKind::Revoke:
      case peg::NodeKind::RevokeAll:
      case peg::NodeKind::GrantRole:
      case peg::NodeKind::RevokeRole:
      case peg::NodeKind::SetRole:
      case peg::NodeKind::SetDefaultRole:
      case peg::NodeKind::AlterUserDefaultRole:
        return resolve_access_control(statement, params_, stmt);
      default:
        return resolve_transaction(statement, params_, stmt);
    }
  }
  if (OB_SUCC(ret)) ret = finish_resolution(stmt);
  return ret;
}

int ObResolver::finish_resolution(ObStmt *stmt)
{
  int ret = OB_SUCCESS;
  if (OB_SUCC(ret) && stmt->is_dml_stmt() && !params_.session_info_->is_varparams_sql_prepare()) {
    ObDMLStmt *dml_stmt = static_cast<ObDMLStmt*>(stmt);
    ObRawExprWrapEnumSet enum_set_wrapper(*params_.expr_factory_, params_.session_info_);
    if (OB_FAIL(enum_set_wrapper.wrap_enum_set(*dml_stmt))) {
    }
  }

  if (OB_SUCC(ret) && stmt->is_dml_stmt() && !stmt->is_explain_stmt()) {
    if (OB_FAIL(params_.query_ctx_->query_hint_.init_query_hint(params_.allocator_,
                                                                params_.session_info_,
                                                                static_cast<ObDMLStmt*>(stmt)))) {
    } else if (OB_FAIL(params_.query_ctx_->query_hint_.check_and_set_params_from_hint(params_,
                                                       *static_cast<ObDMLStmt*>(stmt)))) {
    }
  }

  if (OB_SUCC(ret) && stmt->is_dml_stmt() && !stmt->is_explain_stmt()) {
    bool is_contain_inner_table = false;
    bool is_contain_select_for_update = false;
    ObDMLStmt *dml_stmt = static_cast<ObDMLStmt*>(stmt);
    if (OB_FAIL(dml_stmt->check_if_contain_inner_table(is_contain_inner_table))) {
    } else if (OB_FAIL(dml_stmt->check_if_contain_select_for_update(
                is_contain_select_for_update))) {
    } else {
      params_.query_ctx_->is_contain_inner_table_ = is_contain_inner_table;
      params_.query_ctx_->is_contain_select_for_update_ = is_contain_select_for_update;
      params_.query_ctx_->has_dml_write_stmt_ = dml_stmt->is_dml_write_stmt();
    }

  }
  if (OB_SUCC(ret)) {
    stmt::StmtType stmt_type = stmt->get_stmt_type();
    if (ObStmt::is_ddl_stmt(stmt_type, stmt->has_global_variable()) || ObStmt::is_dcl_stmt(stmt_type)) {
      ObDDLStmt *ddl_stmt = static_cast<ObDDLStmt*>(stmt);
      obcall::ObDDLArg &ddl_arg = ddl_stmt->get_ddl_arg();

      if (OB_ISNULL(params_.query_ctx_)) {
        ret = OB_ERR_UNEXPECTED;
      } else {
        ddl_arg.ddl_stmt_str_ = params_.query_ctx_->get_sql_stmt();
      }
    }
  }
  return ret;
}

}  // namespace sql
}  // namespace oceanbase
