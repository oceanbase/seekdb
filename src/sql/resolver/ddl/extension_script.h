/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SQL_EXTENSION_SCRIPT_H_
#define SEEKDB_SQL_EXTENSION_SCRIPT_H_

#include "share/plugin/extension_package.h"
#include "share/plugin/extension_install.h"
#include "sql/parser/ob_parser.h"
#include "lib/container/ob_se_array.h"

namespace oceanbase {
namespace query { class ObIRootCommandService; }
namespace sql {
struct ObSqlCtx;

struct ExtensionScriptStatement
{
  common::ObString sql_;
  ParseNode *node_ = nullptr;
  TO_STRING_KV("sql_length", sql_.length(), "node_type", nullptr == node_ ? -1 : static_cast<int>(node_->type_));
};

// Owns the UTF-8 package source and kernel parse trees. Views remain valid until
// reset/load/destruction; resolvers must finish before then. Not thread-safe.
// Parsing creates no schema objects and confers no authority to execute a tree.
class ExtensionScript final
{
public:
  ExtensionScript() : arena_("ExtensionSQL") {}
  ExtensionScript(const ExtensionScript &) = delete;
  ExtensionScript &operator=(const ExtensionScript &) = delete;
  // Inputs must not alias source(), statements(), or the error output.
  int load(const std::string &root, const std::string &name, const std::string &version,
           ObSQLMode sql_mode, std::string &error);
  // Parses only selected update edges; no-op/empty updates may yield no trees.
  // No durable version/identity or privilege check occurs here. Installation
  // adapters must not consume these plans as new packages.
  int load_update(const std::string &root, const std::string &name,
                  const std::string &installed_version, const std::string &version,
                  ObSQLMode sql_mode, std::string &error);
  // Host-supplied, already selected memory source. Validates through Rust and
  // deep-copies before parsing. May alias this object's current source(); not
  // error. No filesystem is consulted. This is not a native authority token;
  // normal resolver/Root privilege, dependency and transaction checks still run.
  int load_source(const share::plugin::ExtensionPackageSource &source,
                  ObSQLMode sql_mode, std::string &error);
  // Query-time catalog input, not an Extension package. Owns one UTF-8 routine
  // CREATE/ALTER/DROP; no fabricated package identity or member affiliation.
  // Rejects multiple statements and unsupported object classes before effects.
  int load_routine_statement(const std::string &sql, ObSQLMode sql_mode, std::string &error);
  // One optional native preparation per loaded installation. SQL fragments
  // stay separate from static files; success is parsing, not object admission.
  // Keep the ICatalogDeclarations/module lease alive through installation.
  int append_catalog_declarations(const std::vector<std::string> &sql, std::string &error);
  void reset();
  const share::plugin::ExtensionPackageSource &source() const { return source_; }
  const common::ObIArray<ExtensionScriptStatement> &statements() const { return statements_; }
  ObSQLMode sql_mode() const { return sql_mode_; }
  size_t sql_bytes() const {
    size_t bytes = 0;
    for (const auto &script : source_.scripts_) bytes += script.sql_.size();
    for (const auto &fragment : generated_) bytes += fragment.size();
    return bytes;
  }

private:
  int load_impl(const std::string &root, const std::string &name, const std::string *installed_version,
                const std::string &version, ObSQLMode sql_mode, std::string &error);
  int parse_source(ObSQLMode sql_mode, std::string &error);
  share::plugin::ExtensionPackageSource source_;
  common::ObArenaAllocator arena_;
  common::ObSEArray<ExtensionScriptStatement, 16> statements_;
  ObSQLMode sql_mode_ = 0;
  std::vector<std::string> generated_;
  bool catalog_prepared_ = false;
};

// Owned update planning input: one observed catalog identity + the selected
// Rust update path and kernel parse trees. ready() is NOT schema admission or
// authorization; execution must recheck the fixed ID/source version under lock.
// No semantic resolution, SQL execution, transaction or module activation here.
class ExtensionUpdatePlan final
{
public:
  ExtensionUpdatePlan() = default;
  ExtensionUpdatePlan(const ExtensionUpdatePlan &) = delete;
  ExtensionUpdatePlan &operator=(const ExtensionUpdatePlan &) = delete;
  // Host supplies its startup-controlled root. Inputs must not alias this plan
  // or error. Observation must come from an authenticated Root lookup, not a
  // native plugin. This overload binds data only, it cannot attest that origin.
  int load(const std::string &root, uint64_t tenant_id, uint64_t database_id,
           const std::string &name, const share::plugin::ExtensionVersionSnapshot &observed,
           const std::string &target_version, ObSQLMode sql_mode, std::string &error);
  // Normal host path: validate context, release the old schema guard, fetch the
  // authenticated observation through Root, then load the fixed-source plan.
  // Once lookup is attempted the caller must reacquire a guard for resolution,
  // even if preparation fails. Does not silently reread/replan a stale version.
  int prepare(const std::string &root, const std::string &name, const std::string &target_version,
              const ObSqlCtx &context, query::ObIRootCommandService &commands, std::string &error);
  void reset();
  bool ready() const { return ready_; }
  const ExtensionScript &script() const { return script_; }
  const share::plugin::ExtensionUpdateRequest &request() const { return request_; }
  const share::plugin::ExtensionVersionSnapshot &observed() const { return observed_; }
private:
  ExtensionScript script_;
  share::plugin::ExtensionUpdateRequest request_;
  share::plugin::ExtensionVersionSnapshot observed_;
  bool ready_ = false;
};

} }
#endif
