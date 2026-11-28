// Copyright (c) 2026 OceanBase.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#define USING_LOG_PREFIX SQL_PARSER
#include "sql/syntax.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_smart_call.h"
#include "share/ob_errno.h"
#include <algorithm>

namespace oceanbase::sql {
using namespace common;
namespace {
using Kind = peg::NodeKind;

constexpr size_t ENTRY_RESERVE = 512 * 1024;
constexpr size_t PARSER_STACK_SIZE = (4UL << 20) - ACHUNK_PRESERVE_SIZE * 2;
constexpr size_t HOST_ADMISSION_RESERVE = 68 * 1024;

template <typename Operation>
int run_parser(Operation &&operation)
{
  bool stack_limit = false;
  auto invoke = [&]() -> int {
    stack_limit = false;
    void *address = nullptr;
    size_t size = 0;
    int ret = get_stackattr(address, size);
    if (ret != OB_SUCCESS) return ret;
    const uintptr_t low = reinterpret_cast<uintptr_t>(address);
    if (size == 0 || low > UINTPTR_MAX - size) return OB_ERR_UNEXPECTED;
    return operation(peg::StackBounds{low, low + size}, stack_limit);
  };
#if defined(__EMSCRIPTEN__)
  // Native stack switching cannot extend the Wasm execution stack.
  return invoke();
#elif defined(__x86_64__) || defined(__aarch64__)
  void *address = nullptr;
  size_t size = 0;
  int ret = get_stackattr(address, size);
  if (ret != OB_SUCCESS) return ret;
  const uintptr_t low = reinterpret_cast<uintptr_t>(address);
  if (size == 0 || low > UINTPTR_MAX - size) return OB_ERR_UNEXPECTED;
  uintptr_t current;
#if defined(__x86_64__)
  asm volatile("mov %%rsp, %0" : "=r"(current));
#else
  asm volatile("mov %0, sp" : "=r"(current));
#endif
  if (current < low || current > low + size) return OB_SIZE_OVERFLOW;
  const size_t remaining = current - low;
  if (remaining < HOST_ADMISSION_RESERVE) return OB_SIZE_OVERFLOW;
  auto extend = [&](size_t stack_size) -> int {
    void *stack = nullptr;
    int ret = alloc_stack(stack_size, stack);
    if (ret != OB_SUCCESS) return ret;
    DEFER(dealloc_stack(stack, stack_size));
    return CALL_WITH_NEW_STACK(invoke(), stack, stack_size);
  };
  ret = remaining >= ENTRY_RESERVE ? invoke() : extend(STACK_PER_EXTEND);
  return stack_limit && remaining < PARSER_STACK_SIZE ? extend(PARSER_STACK_SIZE) : ret;
#else
  return OB_NOT_SUPPORTED;
#endif
}

std::optional<stmt::StmtType> native_statement_type(Kind kind)
{
  switch (kind) {
    case Kind::PlBlock:
    case Kind::PlLabeledBlock: return stmt::T_ANONYMOUS_BLOCK;
    case Kind::Select: return stmt::T_SELECT;
    case Kind::Insert:
    case Kind::Replace: return stmt::T_INSERT;
    case Kind::Update: return stmt::T_UPDATE;
    case Kind::Delete: return stmt::T_DELETE;
    case Kind::Explain: return stmt::T_EXPLAIN;
    case Kind::Begin: return stmt::T_START_TRANS;
    case Kind::Commit: return stmt::T_END_TRANS;
    case Kind::Rollback: return stmt::T_END_TRANS;
    case Kind::LockTables:
    case Kind::UnlockTables: return stmt::T_LOCK_TABLE;
    case Kind::Savepoint: return stmt::T_CREATE_SAVEPOINT;
    case Kind::ReleaseSavepoint: return stmt::T_RELEASE_SAVEPOINT;
    case Kind::RollbackSavepoint: return stmt::T_ROLLBACK_SAVEPOINT;
    case Kind::CreateView: return stmt::T_CREATE_VIEW;
    case Kind::CreateOutline: return stmt::T_CREATE_OUTLINE;
    case Kind::AlterOutline: return stmt::T_ALTER_OUTLINE;
    case Kind::DropOutline: return stmt::T_DROP_OUTLINE;
    case Kind::VariableSet: return stmt::T_VARIABLE_SET;
    case Kind::SetNames:
    case Kind::SetCharset: return stmt::T_SET_NAMES;
    case Kind::Kill: return stmt::T_KILL;
    case Kind::LoadData: return stmt::T_LOAD_DATA;
    case Kind::Diagnostics: return stmt::T_DIAGNOSTICS;
    case Kind::Prepare: return stmt::T_PREPARE;
    case Kind::Execute: return stmt::T_EXECUTE;
    case Kind::Deallocate: return stmt::T_DEALLOCATE;
    case Kind::CreateUser: return stmt::T_CREATE_USER;
    case Kind::DropUser: return stmt::T_DROP_USER;
    case Kind::LockUser: return stmt::T_LOCK_USER;
    case Kind::RenameUser: return stmt::T_RENAME_USER;
    case Kind::SetPassword: return stmt::T_SET_PASSWORD;
    case Kind::CreateRole: return stmt::T_CREATE_ROLE;
    case Kind::DropRole: return stmt::T_DROP_ROLE;
    case Kind::Grant: return stmt::T_GRANT;
    case Kind::Revoke: return stmt::T_REVOKE;
    case Kind::RevokeAll: return stmt::T_REVOKE;
    case Kind::GrantRole: return stmt::T_GRANT_ROLE;
    case Kind::RevokeRole: return stmt::T_REVOKE_ROLE;
    case Kind::SetRole: return stmt::T_SET_ROLE;
    case Kind::SetDefaultRole: return stmt::T_ALTER_USER_ROLE;
    case Kind::AlterUserDefaultRole: return stmt::T_ALTER_USER_ROLE;
    case Kind::CreateTable: return stmt::T_CREATE_TABLE;
    case Kind::AlterTable: return stmt::T_ALTER_TABLE;
    case Kind::CreateIndex: return stmt::T_CREATE_INDEX;
    case Kind::DropView: return stmt::T_DROP_VIEW;
    case Kind::DropTable: return stmt::T_DROP_TABLE;
    case Kind::DropIndex: return stmt::T_DROP_INDEX;
    case Kind::CreateTableLike: return stmt::T_CREATE_TABLE_LIKE;
    case Kind::ForkTable: return stmt::T_FORK_TABLE;
    case Kind::ForkDatabase: return stmt::T_FORK_DATABASE;
    case Kind::MergeTable: return stmt::T_MERGE_TABLE;
    case Kind::DiffTable: return stmt::T_DIFF_TABLE;
    case Kind::FlushDagWarnings: return stmt::T_FLUSH_DAG_WARNINGS;
    case Kind::RefreshIoCalibration: return stmt::T_REFRESH_IO_CALIBRATION;
    case Kind::SwitchoverToPrimary: return stmt::T_SWITCHOVER_TO_PRIMARY;
    case Kind::SwitchoverToStandby: return stmt::T_SWITCHOVER_TO_STANDBY;
    case Kind::ActivateStandby: return stmt::T_ACTIVATE_STANDBY;
    case Kind::Freeze: return stmt::T_FREEZE;
    case Kind::SuspendMerge:
    case Kind::ResumeMerge: return stmt::T_ADMIN_MERGE;
    case Kind::ClearMergeError: return stmt::T_CLEAR_MERGE_ERROR;
    case Kind::CancelTask: return stmt::T_CANCEL_TASK;
    case Kind::FlushCache: return stmt::T_FLUSH_CACHE;
    case Kind::FlushKvCache: return stmt::T_FLUSH_KVCACHE;
    case Kind::SetConfig: return stmt::T_ALTER_SYSTEM_SET_PARAMETER;
    case Kind::SetTracepoint: return stmt::T_ALTER_SYSTEM_SETTP;
    case Kind::CreateDatabase: return stmt::T_CREATE_DATABASE;
    case Kind::AlterDatabase: return stmt::T_ALTER_DATABASE;
    case Kind::UseDatabase: return stmt::T_USE_DATABASE;
    case Kind::DropDatabase: return stmt::T_DROP_DATABASE;
    case Kind::RenameTables: return stmt::T_RENAME_TABLE;
    case Kind::TruncateTable: return stmt::T_TRUNCATE_TABLE;
    case Kind::PurgeTable: return stmt::T_PURGE_TABLE;
    case Kind::RestoreTable: return stmt::T_RECYCLEBIN_RESTORE_TABLE;
    case Kind::RestoreDatabase: return stmt::T_RECYCLEBIN_RESTORE_DATABASE;
    case Kind::PurgeIndex: return stmt::T_PURGE_INDEX;
    case Kind::PurgeDatabase: return stmt::T_PURGE_DATABASE;
    case Kind::PurgeRecyclebin: return stmt::T_PURGE_RECYCLEBIN;
    case Kind::OptimizeTable: return stmt::T_OPTIMIZE_TABLE;
    case Kind::SetTransaction: return stmt::T_VARIABLE_SET;
    case Kind::ShowTables: return stmt::T_SHOW_TABLES;
    case Kind::ShowTableStatus: return stmt::T_SHOW_TABLE_STATUS;
    case Kind::ShowProcedureStatus: return stmt::T_SHOW_PROCEDURE_STATUS;
    case Kind::ShowFunctionStatus: return stmt::T_SHOW_FUNCTION_STATUS;
    case Kind::ShowTriggers: return stmt::T_SHOW_TRIGGERS;
    case Kind::ShowProfile: return stmt::T_SHOW_PROFILE;
    case Kind::ShowEngines: return stmt::T_SHOW_ENGINES;
    case Kind::ShowEngine: return stmt::T_SHOW_ENGINE;
    case Kind::ShowOpenTables: return stmt::T_SHOW_OPEN_TABLES;
    case Kind::ShowPrivileges: return stmt::T_SHOW_PRIVILEGES;
    case Kind::ShowRecyclebin: return stmt::T_SHOW_RECYCLEBIN;
    case Kind::CheckTable: return stmt::T_SHOW_CHECK_TABLE;
    case Kind::ShowDatabases: return stmt::T_SHOW_DATABASES;
    case Kind::ShowVariables: return stmt::T_SHOW_VARIABLES;
    case Kind::ShowStatus: return stmt::T_SHOW_STATUS;
    case Kind::ShowColumns: return stmt::T_SHOW_COLUMNS;
    case Kind::ShowCreateDatabase: return stmt::T_SHOW_CREATE_DATABASE;
    case Kind::ShowCreateUser: return stmt::T_SHOW_CREATE_USER;
    case Kind::ShowCreateTable: return stmt::T_SHOW_CREATE_TABLE;
    case Kind::ShowCreateView: return stmt::T_SHOW_CREATE_VIEW;
    case Kind::ShowCreateProcedure: return stmt::T_SHOW_CREATE_PROCEDURE;
    case Kind::ShowCreateFunction: return stmt::T_SHOW_CREATE_FUNCTION;
    case Kind::ShowParameters: return stmt::T_SHOW_PARAMETERS;
    case Kind::ShowTrace: return stmt::T_SHOW_TRACE;
    case Kind::ShowIndexes: return stmt::T_SHOW_INDEXES;
    case Kind::ShowProcesslist: return stmt::T_SHOW_PROCESSLIST;
    case Kind::ShowWarnings: return stmt::T_SHOW_WARNINGS;
    case Kind::ShowErrors: return stmt::T_SHOW_ERRORS;
    case Kind::ShowCharset: return stmt::T_SHOW_CHARSET;
    case Kind::ShowCollation: return stmt::T_SHOW_COLLATION;
    case Kind::ShowGrants: return stmt::T_SHOW_GRANTS;
    case Kind::ShowCreateTrigger: return stmt::T_SHOW_CREATE_TRIGGER;
    case Kind::EmptyQuery: return stmt::T_EMPTY_QUERY;
    case Kind::FlushPrivileges: return stmt::T_EMPTY_QUERY;
    case Kind::CreateProcedure: return stmt::T_CREATE_ROUTINE;
    case Kind::AlterProcedure: return stmt::T_ALTER_ROUTINE;
    case Kind::PlCall: return stmt::T_CALL_PROCEDURE;
    case Kind::CreateFunction: return stmt::T_CREATE_ROUTINE;
    case Kind::AlterFunction: return stmt::T_ALTER_ROUTINE;
    case Kind::DropProcedure: return stmt::T_DROP_ROUTINE;
    case Kind::DropFunction: return stmt::T_DROP_ROUTINE;
    case Kind::CreatePackage: return stmt::T_CREATE_PACKAGE;
    case Kind::CreatePackageBody: return stmt::T_CREATE_PACKAGE_BODY;
    case Kind::DropPackage: return stmt::T_DROP_PACKAGE;
    case Kind::Analyze: return stmt::T_ANALYZE;
    case Kind::CreateTrigger: return stmt::T_CREATE_TRIGGER;
    case Kind::DropTrigger: return stmt::T_DROP_TRIGGER;
    case Kind::ResetConfig: return stmt::T_ALTER_SYSTEM_RESET_PARAMETER;
    default: return std::nullopt;
  }
}

rust::Slice<const uint8_t> sql_bytes(const ObString &sql)
{
  return {reinterpret_cast<const uint8_t *>(sql.ptr()), static_cast<size_t>(sql.length())};
}

peg::SqlMode parser_mode(ObSQLMode mode)
{
  return {bool(mode & SMO_ANSI_QUOTES), bool(mode & SMO_NO_BACKSLASH_ESCAPES),
          bool(mode & SMO_PIPES_AS_CONCAT), bool(mode & SMO_HIGH_NOT_PRECEDENCE),
          bool(mode & SMO_REAL_AS_FLOAT)};
}
}

int report_parse_error(const common::ObString &sql, const peg::ParseDiagnostic &diagnostic)
{
  const size_t offset = diagnostic.span.start;
  int line = 1;
  int column = 1;
  for (size_t i = 0; i < offset; ++i) {
    if (sql.ptr()[i] == '\n') {
      ++line;
      column = 1;
    } else if ((static_cast<unsigned char>(sql.ptr()[i]) & 0xc0) != 0x80) {
      ++column;
    }
  }
  size_t length = std::min<size_t>(80, sql.length() - offset);
  while (length > 0 && offset + length < sql.length()
         && (static_cast<unsigned char>(sql.ptr()[offset + length]) & 0xc0) == 0x80) {
    --length;
  }
  std::string description = ob_errpkt_strerror(OB_ERR_PARSER_SYNTAX);
  if (offset == static_cast<size_t>(sql.length())) description += "; unexpected end of input";
  if (offset < static_cast<size_t>(sql.length())) {
    description += " near '";
    for (size_t i = 0; i < length; ++i) {
      if (sql.ptr()[offset + i] == '\0') description += "\\0";
      else description.push_back(sql.ptr()[offset + i]);
    }
    description += "'";
  }
  description += " at line " + std::to_string(line);
  common::log_user_message(common::USER_ERROR, OB_ERR_PARSE_SQL, "%s", description.c_str());
  LOG_WARN_RET(OB_ERR_PARSE_SQL, "SQL syntax error", "span_start", offset,
      "span_end", diagnostic.span.end, "line", line, "column", column);
  return common::OB_ERR_PARSE_SQL;
}

int ObParser::parse_entry(const ObString &query, ParsedSql &owner, peg::ParseMode mode,
                          bool dynamic_sql)
{
  return run_parser([&](peg::StackBounds bounds, bool &stack_limit) -> int {
    owner.reset();
    ParseResult &result = owner.metadata_;
    result.contain_sensitive_data_ = true;
    ObString sql = query;
    if (mode == peg::ParseMode::Sql || mode == peg::ParseMode::Trigger
        || mode == peg::ParseMode::Statistics) {
      const int64_t length = peg::normalized_sql_length(sql_bytes(query));
      if (length == 0) return OB_ERR_EMPTY_QUERY;
      sql = ObString(length, query.ptr());
    }
    auto parsed = peg::parse_sql(sql_bytes(sql), parser_mode(sql_mode_), mode, bounds);
    stack_limit = parsed->error_kind() == peg::ParseErrorKind::StackLimit;
    if (stack_limit) return OB_SIZE_OVERFLOW;
    if (!parsed->error().empty()) {
      const auto expanded_sql = parsed->expanded_sql();
      const ObString source = expanded_sql.empty() ? sql
          : syntax_text(expanded_sql);
      return report_parse_error(source, parsed->diagnostic());
    }
    result.input_sql_ = sql.ptr();
    result.input_sql_len_ = sql.length();
    result.is_dynamic_sql_ = dynamic_sql;
    const auto kind = parsed->root().kind();
    result.contain_sensitive_data_ = kind == Kind::SetConfig
        || kind == Kind::CreateUser || kind == Kind::SetPassword || kind == Kind::Grant
        || (kind == Kind::LoadData && peg::load_data_definition(parsed->root()).contains_sensitive_data);
    const auto parameters = parsed->parameters();
    result.question_mark_ctx_.count_ = parameters;
    result.question_mark_ctx_.by_ordinal_ = parsed->has_ordinal_parameters();
    result.question_mark_ctx_.by_name_ = parsed->named_parameters();
    result.parse_mode_ = mode;
    result.pl_context_ = parsed->is_pl();
    if (dynamic_sql) {
      ObString owned;
      const int ret = ob_write_string(allocator_, sql, owned, true);
      if (ret != OB_SUCCESS) return ret;
      result.no_param_sql_ = owned.ptr();
      result.no_param_sql_len_ = owned.length();
    }
    owner.original_source_ = sql;
    owner.syntax_.emplace(std::move(parsed));
    return OB_SUCCESS;
  });
}

int ObParser::parse(const ObString &query, ParsedSql &parsed, ParseMode mode)
{
  const auto entry = mode == TRIGGER_MODE ? peg::ParseMode::Trigger
      : mode == METHOD_OPT_MODE ? peg::ParseMode::Statistics : peg::ParseMode::Sql;
  return SMART_CALL_LARGE(parse_entry(query, parsed, entry, mode == DYNAMIC_SQL_MODE));
}

int ObParser::parse_expression(const ObString &text, ParsedSql &parsed)
{
  return parse_entry(text, parsed, peg::ParseMode::Expression);
}

int ObParser::parse_pl_sql(const ObString &text, ParsedSql &parsed)
{
  return parse_entry(text, parsed, peg::ParseMode::PlSql);
}

int ObParser::parse_pl_expression(const ObString &text, ParsedSql &parsed)
{
  return parse_entry(text, parsed, peg::ParseMode::PlExpression);
}

int ObParser::parse_routine_body(const ObString &text, ParsedSql &parsed)
{
  return parse_entry(text, parsed, peg::ParseMode::Routine);
}

int ObParser::parse_routine_definition(const ObString &definition, ParsedSql &parsed)
{
  return parse_entry(definition, parsed, peg::ParseMode::RoutineDefinition);
}

bool ObParser::is_explain_stmt(const ObString &stmt, const char *&body)
{
  const auto offset = peg::explain_body_offset(sql_bytes(stmt), peg::SqlMode{});
  const bool explained = offset < static_cast<size_t>(stmt.length());
  body = explained ? stmt.ptr() + offset : nullptr;
  return explained;
}

int ObParser::split_multiple_stmt(const ObString &stmt, ObIArray<ObString> &queries,
                                  ObMPParseStat &parse_stat, bool first_only)
{
  parse_stat.reset();
  if (peg::is_single_statement(sql_bytes(stmt))) return queries.push_back(stmt);
  return run_parser([&](peg::StackBounds bounds, bool &stack_limit) -> int {
    int ret = OB_SUCCESS;
    const auto split = peg::split_sql(sql_bytes(stmt), parser_mode(sql_mode_), first_only, bounds);
    stack_limit = split.failed && split.error_kind == peg::ParseErrorKind::StackLimit;
    if (stack_limit) {
      parse_stat.parse_fail_ = true;
      parse_stat.fail_ret_ = OB_SIZE_OVERFLOW;
      return OB_SIZE_OVERFLOW;
    }
    parse_stat.reset();
    size_t offset = 0;
    for (size_t end : split.ends) {
      if (OB_FAIL(queries.push_back(ObString(end - offset, stmt.ptr() + offset)))) break;
      offset = end;
    }
    if (OB_SUCC(ret) && split.failed) {
      parse_stat.parse_fail_ = true;
      parse_stat.fail_query_idx_ = queries.count() - 1;
      parse_stat.fail_ret_ = OB_ERR_PARSE_SQL;
    }
    return ret;
  });
}

int ObParser::inspect_expression(const ObString &text, peg::ExpressionInfo &info)
{
  return run_parser([&](peg::StackBounds bounds, bool &stack_limit) -> int {
    info = peg::inspect_expression(sql_bytes(text), parser_mode(sql_mode_), bounds);
    stack_limit = info.error_kind == peg::ParseErrorKind::StackLimit;
    return stack_limit ? OB_SIZE_OVERFLOW : OB_SUCCESS;
  });
}

int ObParser::pre_parse(const ObString &stmt, PreParseResult &res)
{
  const auto info = peg::classify_statement(sql_bytes(stmt), peg::SqlMode{});
  res.trace_id_.reset();
  if (info.trace_end > info.trace_start) {
    const size_t length = std::min(info.trace_end - info.trace_start,
                                  static_cast<size_t>(OB_MAX_TRACE_ID_BUFFER_SIZE));
    res.trace_id_.assign_ptr(stmt.ptr() + info.trace_start, static_cast<int64_t>(length));
  }
  return OB_SUCCESS;
}

int ParsedSql::preserve_source(common::ObIAllocator &allocator)
{
  return common::ob_write_string(allocator, original_source_, original_source_);
}

int statement_type(const peg::Node &statement, stmt::StmtType &type)
{
  const auto mapped_type = native_statement_type(statement.kind());
  if (!mapped_type) return common::OB_ERR_UNEXPECTED;
  type = *mapped_type;
  return common::OB_SUCCESS;
}

int ParsedSql::statement_type(stmt::StmtType &type) const
{
  return syntax_ ? sql::statement_type((*syntax_)->root(), type)
                 : common::OB_ERR_UNEXPECTED;
}

bool ParsedSql::is_end_transaction() const
{
  if (!syntax_) return false;
  const auto kind = (*syntax_)->root().kind();
  return kind == peg::NodeKind::Commit || kind == peg::NodeKind::Rollback;
}

}
