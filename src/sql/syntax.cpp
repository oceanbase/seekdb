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
#include "sql/ob_sql_utils.h"
#include "lib/oblog/ob_log.h"
#include "lib/utility/ob_print_utils.h"
#include "lib/utility/ob_smart_call.h"
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

struct NativeStatementTraits
{
  stmt::StmtType type;
  bool readonly;
  bool implicit_commit;
};

std::optional<NativeStatementTraits> native_statement_traits(Kind kind)
{
  switch (kind) {
    case Kind::PlBlock:
    case Kind::PlLabeledBlock: return NativeStatementTraits{stmt::T_ANONYMOUS_BLOCK, false, false};
    case Kind::Select: return NativeStatementTraits{stmt::T_SELECT, true, false};
    case Kind::Insert:
    case Kind::Replace: return NativeStatementTraits{stmt::T_INSERT, false, false};
    case Kind::Update: return NativeStatementTraits{stmt::T_UPDATE, false, false};
    case Kind::Delete: return NativeStatementTraits{stmt::T_DELETE, false, false};
    case Kind::Explain: return NativeStatementTraits{stmt::T_EXPLAIN, false, false};
    case Kind::Begin: return NativeStatementTraits{stmt::T_START_TRANS, true, false};
    case Kind::Commit: return NativeStatementTraits{stmt::T_END_TRANS, true, false};
    case Kind::Rollback: return NativeStatementTraits{stmt::T_END_TRANS, true, false};
    case Kind::LockTables:
    case Kind::UnlockTables: return NativeStatementTraits{stmt::T_LOCK_TABLE, false, false};
    case Kind::Savepoint: return NativeStatementTraits{stmt::T_CREATE_SAVEPOINT, false, false};
    case Kind::ReleaseSavepoint: return NativeStatementTraits{stmt::T_RELEASE_SAVEPOINT, false, false};
    case Kind::RollbackSavepoint: return NativeStatementTraits{stmt::T_ROLLBACK_SAVEPOINT, false, false};
    case Kind::CreateView: return NativeStatementTraits{stmt::T_CREATE_VIEW, false, true};
    case Kind::CreateOutline: return NativeStatementTraits{stmt::T_CREATE_OUTLINE, false, true};
    case Kind::AlterOutline: return NativeStatementTraits{stmt::T_ALTER_OUTLINE, false, true};
    case Kind::DropOutline: return NativeStatementTraits{stmt::T_DROP_OUTLINE, false, true};
    case Kind::VariableSet: return NativeStatementTraits{stmt::T_VARIABLE_SET, true, false};
    case Kind::SetNames:
    case Kind::SetCharset: return NativeStatementTraits{stmt::T_SET_NAMES, true, false};
    case Kind::Kill: return NativeStatementTraits{stmt::T_KILL, false, false};
    case Kind::LoadData: return NativeStatementTraits{stmt::T_LOAD_DATA, false, true};
    case Kind::Diagnostics: return NativeStatementTraits{stmt::T_DIAGNOSTICS, false, false};
    case Kind::Prepare: return NativeStatementTraits{stmt::T_PREPARE, false, false};
    case Kind::Execute: return NativeStatementTraits{stmt::T_EXECUTE, false, false};
    case Kind::Deallocate: return NativeStatementTraits{stmt::T_DEALLOCATE, false, false};
    case Kind::CreateUser: return NativeStatementTraits{stmt::T_CREATE_USER, false, true};
    case Kind::DropUser: return NativeStatementTraits{stmt::T_DROP_USER, false, true};
    case Kind::LockUser: return NativeStatementTraits{stmt::T_LOCK_USER, false, true};
    case Kind::RenameUser: return NativeStatementTraits{stmt::T_RENAME_USER, false, true};
    case Kind::SetPassword: return NativeStatementTraits{stmt::T_SET_PASSWORD, false, true};
    case Kind::CreateRole: return NativeStatementTraits{stmt::T_CREATE_ROLE, false, false};
    case Kind::DropRole: return NativeStatementTraits{stmt::T_DROP_ROLE, false, false};
    case Kind::Grant: return NativeStatementTraits{stmt::T_GRANT, false, true};
    case Kind::Revoke: return NativeStatementTraits{stmt::T_REVOKE, false, true};
    case Kind::RevokeAll: return NativeStatementTraits{stmt::T_REVOKE, false, true};
    case Kind::GrantRole: return NativeStatementTraits{stmt::T_GRANT_ROLE, false, false};
    case Kind::RevokeRole: return NativeStatementTraits{stmt::T_REVOKE_ROLE, false, false};
    case Kind::SetRole: return NativeStatementTraits{stmt::T_SET_ROLE, true, false};
    case Kind::SetDefaultRole: return NativeStatementTraits{stmt::T_ALTER_USER_ROLE, false, false};
    case Kind::AlterUserDefaultRole: return NativeStatementTraits{stmt::T_ALTER_USER_ROLE, false, false};
    case Kind::CreateTable: return NativeStatementTraits{stmt::T_CREATE_TABLE, false, true};
    case Kind::AlterTable: return NativeStatementTraits{stmt::T_ALTER_TABLE, false, false};
    case Kind::CreateIndex: return NativeStatementTraits{stmt::T_CREATE_INDEX, false, true};
    case Kind::DropView: return NativeStatementTraits{stmt::T_DROP_VIEW, false, false};
    case Kind::DropTable: return NativeStatementTraits{stmt::T_DROP_TABLE, false, true};
    case Kind::DropIndex: return NativeStatementTraits{stmt::T_DROP_INDEX, false, true};
    case Kind::CreateTableLike: return NativeStatementTraits{stmt::T_CREATE_TABLE_LIKE, false, false};
    case Kind::ForkTable: return NativeStatementTraits{stmt::T_FORK_TABLE, false, false};
    case Kind::ForkDatabase: return NativeStatementTraits{stmt::T_FORK_DATABASE, false, false};
    case Kind::MergeTable: return NativeStatementTraits{stmt::T_MERGE_TABLE, false, false};
    case Kind::DiffTable: return NativeStatementTraits{stmt::T_DIFF_TABLE, true, false};
    case Kind::FlushDagWarnings: return NativeStatementTraits{stmt::T_FLUSH_DAG_WARNINGS, false, false};
    case Kind::RefreshIoCalibration: return NativeStatementTraits{stmt::T_REFRESH_IO_CALIBRATION, false, false};
    case Kind::SwitchoverToPrimary: return NativeStatementTraits{stmt::T_SWITCHOVER_TO_PRIMARY, false, false};
    case Kind::SwitchoverToStandby: return NativeStatementTraits{stmt::T_SWITCHOVER_TO_STANDBY, false, false};
    case Kind::ActivateStandby: return NativeStatementTraits{stmt::T_ACTIVATE_STANDBY, false, false};
    case Kind::Freeze: return NativeStatementTraits{stmt::T_FREEZE, false, false};
    case Kind::SuspendMerge:
    case Kind::ResumeMerge: return NativeStatementTraits{stmt::T_ADMIN_MERGE, false, false};
    case Kind::ClearMergeError: return NativeStatementTraits{stmt::T_CLEAR_MERGE_ERROR, false, false};
    case Kind::CancelTask: return NativeStatementTraits{stmt::T_CANCEL_TASK, false, false};
    case Kind::FlushCache: return NativeStatementTraits{stmt::T_FLUSH_CACHE, false, false};
    case Kind::FlushKvCache: return NativeStatementTraits{stmt::T_FLUSH_KVCACHE, false, false};
    case Kind::SetConfig: return NativeStatementTraits{stmt::T_ALTER_SYSTEM_SET_PARAMETER, false, false};
    case Kind::SetTracepoint: return NativeStatementTraits{stmt::T_ALTER_SYSTEM_SETTP, false, false};
    case Kind::CreateDatabase: return NativeStatementTraits{stmt::T_CREATE_DATABASE, false, true};
    case Kind::AlterDatabase: return NativeStatementTraits{stmt::T_ALTER_DATABASE, false, true};
    case Kind::UseDatabase: return NativeStatementTraits{stmt::T_USE_DATABASE, true, false};
    case Kind::DropDatabase: return NativeStatementTraits{stmt::T_DROP_DATABASE, false, true};
    case Kind::RenameTables: return NativeStatementTraits{stmt::T_RENAME_TABLE, false, false};
    case Kind::TruncateTable: return NativeStatementTraits{stmt::T_TRUNCATE_TABLE, false, false};
    case Kind::PurgeTable: return NativeStatementTraits{stmt::T_PURGE_TABLE, false, false};
    case Kind::RestoreTable: return NativeStatementTraits{stmt::T_RECYCLEBIN_RESTORE_TABLE, false, false};
    case Kind::RestoreDatabase: return NativeStatementTraits{stmt::T_RECYCLEBIN_RESTORE_DATABASE, false, false};
    case Kind::PurgeIndex: return NativeStatementTraits{stmt::T_PURGE_INDEX, false, false};
    case Kind::PurgeDatabase: return NativeStatementTraits{stmt::T_PURGE_DATABASE, false, false};
    case Kind::PurgeRecyclebin: return NativeStatementTraits{stmt::T_PURGE_RECYCLEBIN, false, false};
    case Kind::OptimizeTable: return NativeStatementTraits{stmt::T_OPTIMIZE_TABLE, false, false};
    case Kind::SetTransaction: return NativeStatementTraits{stmt::T_VARIABLE_SET, true, false};
    case Kind::ShowTables: return NativeStatementTraits{stmt::T_SHOW_TABLES, true, false};
    case Kind::ShowTableStatus: return NativeStatementTraits{stmt::T_SHOW_TABLE_STATUS, true, false};
    case Kind::ShowProcedureStatus: return NativeStatementTraits{stmt::T_SHOW_PROCEDURE_STATUS, true, false};
    case Kind::ShowFunctionStatus: return NativeStatementTraits{stmt::T_SHOW_FUNCTION_STATUS, true, false};
    case Kind::ShowTriggers: return NativeStatementTraits{stmt::T_SHOW_TRIGGERS, true, false};
    case Kind::ShowProfile: return NativeStatementTraits{stmt::T_SHOW_PROFILE, true, false};
    case Kind::ShowEngines: return NativeStatementTraits{stmt::T_SHOW_ENGINES, true, false};
    case Kind::ShowEngine: return NativeStatementTraits{stmt::T_SHOW_ENGINE, true, false};
    case Kind::ShowOpenTables: return NativeStatementTraits{stmt::T_SHOW_OPEN_TABLES, true, false};
    case Kind::ShowPrivileges: return NativeStatementTraits{stmt::T_SHOW_PRIVILEGES, true, false};
    case Kind::ShowRecyclebin: return NativeStatementTraits{stmt::T_SHOW_RECYCLEBIN, true, false};
    case Kind::CheckTable: return NativeStatementTraits{stmt::T_SHOW_CHECK_TABLE, true, false};
    case Kind::ShowDatabases: return NativeStatementTraits{stmt::T_SHOW_DATABASES, true, false};
    case Kind::ShowVariables: return NativeStatementTraits{stmt::T_SHOW_VARIABLES, true, false};
    case Kind::ShowStatus: return NativeStatementTraits{stmt::T_SHOW_STATUS, true, false};
    case Kind::ShowColumns: return NativeStatementTraits{stmt::T_SHOW_COLUMNS, true, false};
    case Kind::ShowCreateDatabase: return NativeStatementTraits{stmt::T_SHOW_CREATE_DATABASE, true, false};
    case Kind::ShowCreateUser: return NativeStatementTraits{stmt::T_SHOW_CREATE_USER, true, false};
    case Kind::ShowCreateTable: return NativeStatementTraits{stmt::T_SHOW_CREATE_TABLE, true, false};
    case Kind::ShowCreateView: return NativeStatementTraits{stmt::T_SHOW_CREATE_VIEW, true, false};
    case Kind::ShowCreateProcedure: return NativeStatementTraits{stmt::T_SHOW_CREATE_PROCEDURE, true, false};
    case Kind::ShowCreateFunction: return NativeStatementTraits{stmt::T_SHOW_CREATE_FUNCTION, true, false};
    case Kind::ShowParameters: return NativeStatementTraits{stmt::T_SHOW_PARAMETERS, true, false};
    case Kind::ShowTrace: return NativeStatementTraits{stmt::T_SHOW_TRACE, true, false};
    case Kind::ShowIndexes: return NativeStatementTraits{stmt::T_SHOW_INDEXES, true, false};
    case Kind::ShowProcesslist: return NativeStatementTraits{stmt::T_SHOW_PROCESSLIST, true, false};
    case Kind::ShowWarnings: return NativeStatementTraits{stmt::T_SHOW_WARNINGS, true, false};
    case Kind::ShowErrors: return NativeStatementTraits{stmt::T_SHOW_ERRORS, true, false};
    case Kind::ShowCharset: return NativeStatementTraits{stmt::T_SHOW_CHARSET, true, false};
    case Kind::ShowCollation: return NativeStatementTraits{stmt::T_SHOW_COLLATION, true, false};
    case Kind::ShowGrants: return NativeStatementTraits{stmt::T_SHOW_GRANTS, true, false};
    case Kind::ShowCreateTrigger: return NativeStatementTraits{stmt::T_SHOW_CREATE_TRIGGER, true, false};
    case Kind::EmptyQuery: return NativeStatementTraits{stmt::T_EMPTY_QUERY, false, false};
    case Kind::FlushPrivileges: return NativeStatementTraits{stmt::T_EMPTY_QUERY, false, false};
    case Kind::CreateProcedure: return NativeStatementTraits{stmt::T_CREATE_ROUTINE, false, true};
    case Kind::AlterProcedure: return NativeStatementTraits{stmt::T_ALTER_ROUTINE, false, true};
    case Kind::PlCall: return NativeStatementTraits{stmt::T_CALL_PROCEDURE, false, false};
    case Kind::CreateFunction: return NativeStatementTraits{stmt::T_CREATE_ROUTINE, false, true};
    case Kind::AlterFunction: return NativeStatementTraits{stmt::T_ALTER_ROUTINE, false, true};
    case Kind::DropProcedure: return NativeStatementTraits{stmt::T_DROP_ROUTINE, false, true};
    case Kind::DropFunction: return NativeStatementTraits{stmt::T_DROP_ROUTINE, false, true};
    case Kind::CreatePackage: return NativeStatementTraits{stmt::T_CREATE_PACKAGE, false, true};
    case Kind::CreatePackageBody: return NativeStatementTraits{stmt::T_CREATE_PACKAGE_BODY, false, true};
    case Kind::DropPackage: return NativeStatementTraits{stmt::T_DROP_PACKAGE, false, true};
    case Kind::Analyze: return NativeStatementTraits{stmt::T_ANALYZE, false, false};
    case Kind::CreateTrigger: return NativeStatementTraits{stmt::T_CREATE_TRIGGER, false, true};
    case Kind::DropTrigger: return NativeStatementTraits{stmt::T_DROP_TRIGGER, false, true};
    case Kind::ResetConfig: return NativeStatementTraits{stmt::T_ALTER_SYSTEM_RESET_PARAMETER, false, false};
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
  if (!diagnostic.detail.empty()) {
    description += "; ";
    description.append(diagnostic.detail.data(), diagnostic.detail.size());
  }
  if (offset < static_cast<size_t>(sql.length())) {
    description += " near '";
    for (size_t i = 0; i < length; ++i) {
      if (sql.ptr()[offset + i] == '\0') description += "\\0";
      else description.push_back(sql.ptr()[offset + i]);
    }
    if (length < sql.length() - offset) description += "...";
    description += "'";
  }
  description += " at line " + std::to_string(line);
  common::log_user_message(common::USER_ERROR, OB_ERR_PARSE_SQL, "%s", description.c_str());
  std::string expected;
  for (const auto &candidate : diagnostic.expected) {
    if (!expected.empty()) expected += ", ";
    expected.append(candidate.data(), candidate.size());
  }
  LOG_WARN_RET(OB_ERR_PARSE_SQL, "SQL syntax error", "description", description.c_str(),
      "span_start", offset, "span_end", diagnostic.span.end, "line", line, "column", column,
      "expected", expected.c_str());
  return common::OB_ERR_PARSE_SQL;
}

int ObParser::parse_entry(const ObString &query, ParsedSql &owner, peg::ParseMode mode,
                          bool dynamic_sql)
{
  return run_parser([&](peg::StackBounds bounds, bool &stack_limit) -> int {
    owner.reset();
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
    ParseResult &result = owner.metadata_;
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
  const auto traits = native_statement_traits(statement.kind());
  if (!traits) return common::OB_ERR_UNEXPECTED;
  type = traits->type;
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

void ParsedSql::sql_traits(ObSqlTraits &traits)
{
  statement_type(traits.stmt_type_);
  if (syntax_) {
    const auto &root = (*syntax_)->root();
    const auto kind = root.kind();
    const auto syntax_traits = *native_statement_traits(kind);
    traits.is_readonly_stmt_ = syntax_traits.readonly;
    if (kind == Kind::Select) {
      traits.is_readonly_stmt_ = !peg::query_has_lock(root);
    } else if (kind == Kind::Explain) {
      const auto &statement = peg::explain_statement(root);
      traits.is_readonly_stmt_ = statement.kind() == Kind::Select && !peg::query_has_lock(statement);
    }
    traits.is_cause_implicit_commit_ = syntax_traits.implicit_commit;
    traits.is_commit_stmt_ = kind == peg::NodeKind::Commit;
    traits.allowed_on_standby_ = kind == Kind::SetConfig
        || kind == Kind::ResetConfig || kind == Kind::SetTracepoint;
  }
}

}
