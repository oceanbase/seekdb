/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#include "sql/resolver/ddl/extension_script.h"
#include "sql/ob_sql_context.h"
#include "sql/session/ob_sql_session_info.h"
#include "query/command/ob_root_command_service.h"
#include "lib/charset/ob_charset.h"
#include <new>

namespace oceanbase { namespace sql {
using namespace common;

void ExtensionScript::reset()
{
  statements_.reset();
  arena_.reset();
  source_ = share::plugin::ExtensionPackageSource{};
  sql_mode_ = 0;
  generated_.clear();
  catalog_prepared_ = false;
}

int ExtensionScript::load(const std::string &root, const std::string &name,
                          const std::string &version, ObSQLMode sql_mode, std::string &error)
{
  return load_impl(root, name, nullptr, version, sql_mode, error);
}

int ExtensionScript::load_update(const std::string &root, const std::string &name,
                                 const std::string &installed_version, const std::string &version,
                                 ObSQLMode sql_mode, std::string &error)
{
  return load_impl(root, name, &installed_version, version, sql_mode, error);
}

int ExtensionScript::load_impl(const std::string &root, const std::string &name,
                               const std::string *installed_version, const std::string &version,
                               ObSQLMode sql_mode, std::string &error)
{
  // Inputs must not alias this object's previously returned source/views.
  reset();
  int ret = OB_SUCCESS;
  try {
    ret = installed_version == nullptr ? share::plugin::read_extension_package(root, name, version, source_, error) :
        share::plugin::read_extension_update(root, name, *installed_version, version, source_, error);
    if (OB_SUCC(ret)) ret = parse_source(sql_mode, error);
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret)) reset();
  else sql_mode_ = sql_mode;
  return ret;
}

int ExtensionScript::load_source(const share::plugin::ExtensionPackageSource &source,
                                 ObSQLMode sql_mode, std::string &error)
{
  int ret = OB_SUCCESS;
  try {
    ret = share::plugin::validate_extension_package_source(source, error);
    if (OB_SUCC(ret)) {
      // Copy before reset, including for load_source(this->source(), ...).
      auto owned = source;
      reset();
      source_ = std::move(owned);
      ret = parse_source(sql_mode, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret)) reset();
  else sql_mode_ = sql_mode;
  return ret;
}

int ExtensionScript::load_routine_statement(const std::string &sql, ObSQLMode sql_mode, std::string &error)
{
  error.clear();
  int ret = OB_SUCCESS;
  try {
    int64_t valid_bytes = 0;
    if (sql.empty() || sql.size() > 4 * 1024 * 1024 || sql.find('\0') != std::string::npos) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(ObCharset::well_formed_len(CS_TYPE_UTF8MB4_BIN, sql.data(), sql.size(), valid_bytes))) {
    } else if (valid_bytes != static_cast<int64_t>(sql.size())) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      auto owned = sql;
      reset();
      generated_.push_back(std::move(owned));
      if (OB_FAIL(parse_source(sql_mode, error))) {
      } else if (statements_.count() != 1) {
        ret = OB_NOT_SUPPORTED;
      } else {
        const auto *node = statements_.at(0).node_;
        if (node == nullptr) ret = OB_ERR_UNEXPECTED;
        else if (!(((node->type_ == T_SF_CREATE || node->type_ == T_SP_CREATE) && node->value_ == 0) ||
                   node->type_ == T_SF_ALTER || node->type_ == T_SP_ALTER ||
                   node->type_ == T_SF_DROP || node->type_ == T_SP_DROP)) ret = OB_NOT_SUPPORTED;
      }
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (OB_FAIL(ret)) {
    reset();
    error = "query catalog requires one UTF-8 routine CREATE, ALTER or DROP statement";
  } else sql_mode_ = sql_mode;
  return ret;
}

int ExtensionScript::parse_source(ObSQLMode sql_mode, std::string &error)
{
  int ret = OB_SUCCESS;
  // Package files are UTF-8, independent of the client's wire charset. The
  // caller explicitly chooses SQL mode; no session state is changed here.
  ObParser parser(arena_, sql_mode);
  int64_t query_count = 0;
  std::vector<const std::string *> inputs;
  for (const auto &file : source_.scripts_) inputs.push_back(&file.sql_);
  for (const auto &sql : generated_) inputs.push_back(&sql);
  for (const auto *file : inputs) {
    if (OB_FAIL(ret)) break;
    if (file->empty()) continue; // Explicit empty update edge, not a missing file.
    // Each update is its own SQL input, never a textual suffix of the base.
    const ObString sql(static_cast<int32_t>(file->size()), file->data());
    ObSEArray<ObString, 16> queries;
    ObMPParseStat split_status;
    if (OB_FAIL(parser.split_multiple_stmt(sql, queries, split_status))) {
    } else if (split_status.parse_fail_) {
      // The splitter may return SUCCESS with valid prefix statements and a
      // failing tail. Never expose that prefix as an installable script.
      ret = split_status.fail_ret_ == OB_SUCCESS ? OB_ERR_PARSE_SQL : split_status.fail_ret_;
    } else if (queries.count() > 4096 - query_count) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      query_count += queries.count();
      for (int64_t i = 0; OB_SUCC(ret) && i < queries.count(); ++i) {
        ParseResult result{};
        const int parse_ret = parser.parse(queries.at(i), result);
        if (parse_ret == OB_ERR_EMPTY_QUERY) {
          // Blank/comment-only statements are not Extension members.
        } else if (OB_SUCCESS != parse_ret) {
          ret = parse_ret;
        } else if (nullptr == result.result_tree_ || result.result_tree_->num_child_ != 1 ||
                   nullptr == result.result_tree_->children_ ||
                   nullptr == result.result_tree_->children_[0]) {
          ret = OB_ERR_PARSE_SQL;
        } else if (result.result_tree_->children_[0]->type_ == T_EMPTY_QUERY) {
          // The MySQL grammar returns SUCCESS + T_EMPTY_QUERY for comments
          // and empty delimiters, unlike the empty-input error above.
        } else {
          ExtensionScriptStatement statement{queries.at(i), result.result_tree_->children_[0]};
          ret = statements_.push_back(statement);
        }
        // The kernel parser's tree storage belongs to arena_. Do not reuse
        // that arena between statements; SQL resolvers consume these trees.
      }
    }
  }
  if (OB_SUCC(ret) && statements_.empty() && source_.from_version_.empty() &&
      (!source_.native_install_ || catalog_prepared_)) ret = OB_ERR_EMPTY_QUERY;
  if (OB_FAIL(ret)) error = "extension SQL parsing failed; no statements are available for installation/update";
  return ret;
}

int ExtensionScript::append_catalog_declarations(const std::vector<std::string> &sql, std::string &error)
{
  error.clear();
  int ret = OB_SUCCESS;
  try {
    if (catalog_prepared_ || source_.name_.empty() || !source_.from_version_.empty() ||
        (statements_.empty() && !source_.native_install_)) {
      ret = OB_STATE_NOT_MATCH;
    } else if (sql.size() > 4096) {
      ret = OB_SIZE_OVERFLOW;
    } else {
      size_t total = 0;
      for (const auto &file : source_.scripts_) total += file.sql_.size();
      for (const auto &fragment : sql) {
        if (OB_FAIL(ret)) break;
        if (total > 4 * 1024 * 1024 || fragment.size() > 4 * 1024 * 1024 - total) ret = OB_SIZE_OVERFLOW;
        else {
          auto input = source_;
          input.native_install_ = false; // Validate the emitted fragment, not a native-source plan.
          input.scripts_ = {{"", source_.version_, fragment}};
          ret = share::plugin::validate_extension_package_source(input, error);
          total += fragment.size();
        }
      }
      if (OB_SUCC(ret)) {
        generated_ = sql;
        statements_.reset(); arena_.reset();
        catalog_prepared_ = true; // A completed native preparation must produce actual statements.
        ret = parse_source(sql_mode_, error);
      }
    }
  } catch (const std::bad_alloc &) { ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) { ret = OB_ERR_UNEXPECTED; }
  if (OB_FAIL(ret)) reset();
  return ret;
}

void ExtensionUpdatePlan::reset()
{
  ready_ = false;
  script_.reset();
  request_ = share::plugin::ExtensionUpdateRequest{};
  observed_ = share::plugin::ExtensionVersionSnapshot{};
}

int ExtensionUpdatePlan::load(const std::string &root, uint64_t tenant_id, uint64_t database_id,
                               const std::string &name, const share::plugin::ExtensionVersionSnapshot &observed,
                               const std::string &target_version, ObSQLMode sql_mode, std::string &error)
{
  reset();
  error.clear();
  int ret = OB_SUCCESS;
  try {
    const auto valid_id = [](uint64_t id) { return id != 0 && id <= static_cast<uint64_t>(INT64_MAX); };
    if (!valid_id(tenant_id) || !valid_id(database_id) || !valid_id(observed.extension_id_) ||
        !valid_id(observed.owner_id_) || observed.version_.empty() || observed.version_.size() > 255 ||
        observed.native_module_id_.size() > 255) {
      ret = OB_INVALID_ARGUMENT;
    } else if (OB_FAIL(script_.load_update(root, name, observed.version_, target_version, sql_mode, error))) {
    } else if (script_.source().name_ != name || script_.source().from_version_ != observed.version_ ||
               (!target_version.empty() && script_.source().version_ != target_version)) {
      ret = OB_STATE_NOT_MATCH;
      error = "update source does not match the observed installation and requested target";
    } else if (script_.source().native_module_ != observed.native_module_id_) {
      // SQL version updates currently preserve module identity in the catalog.
      // Do not silently substitute another native provider from changed files.
      ret = OB_STATE_NOT_MATCH;
      error = "update package changes the installed native module identity";
    } else {
      share::plugin::ExtensionUpdateRequest request;
      request.tenant_id_ = tenant_id;
      request.database_id_ = database_id;
      request.name_ = name;
      request.expected_extension_id_ = observed.extension_id_;
      request.from_version_ = observed.version_;
      request.to_version_ = script_.source().version_; // includes control-default resolution
      request.requires_ = script_.source().requires_;
      request.prerequisites_ = script_.source().prerequisites_;
      observed_ = observed;
      request_ = std::move(request);
      ready_ = true;
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret)) reset();
  return ret;
}

int ExtensionUpdatePlan::prepare(const std::string &root, const std::string &name,
                                  const std::string &target_version, const ObSqlCtx &context,
                                  query::ObIRootCommandService &commands, std::string &error)
{
  reset();
  error.clear();
  if (context.session_info_ == nullptr || context.schema_guard_ == nullptr) return OB_NOT_INIT;
  if (context.disable_privilege_check_ != PRIV_CHECK_FLAG_NORMAL) return OB_ERR_NO_PRIVILEGE;
  auto &session = *context.session_info_;
  if (session.is_in_transaction() || session.is_inner() || session.is_nested_session()) return OB_NOT_SUPPORTED;
  const uint64_t database_id = session.get_database_id();
  const ObSQLMode sql_mode = session.get_sql_mode();
  if (database_id == 0 || database_id == OB_INVALID_ID) return OB_ERR_NO_DB_SELECTED;
  int ret = OB_SUCCESS;
  try {
    share::plugin::ExtensionVersionSnapshot observed;
    // Never hold the caller's old guard while waiting for Root DDL publication.
    if (OB_FAIL(context.schema_guard_->reset())) {
    } else if (OB_FAIL(commands.read_extension_update_source(1, database_id, name, session, observed, error))) {
    } else if (session.get_database_id() != database_id || session.get_sql_mode() != sql_mode) {
      ret = OB_STATE_NOT_MATCH;
      error = "session database or SQL mode changed during update planning";
    } else {
      ret = load(root, 1, database_id, name, observed, target_version, sql_mode, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  if (OB_FAIL(ret)) reset();
  return ret;
}

} }
