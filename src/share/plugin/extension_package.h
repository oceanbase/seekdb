/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_PLUGIN_EXTENSION_PACKAGE_H_
#define SEEKDB_SHARE_PLUGIN_EXTENSION_PACKAGE_H_

#include <string>
#include <vector>

namespace oceanbase { namespace share { namespace plugin {
class ICatalogBuildProgram;

// Host-owned preparation plus module lease. Keep alive until installation and
// schema publication finish. Returned SQL is immutable; no plugin pointers.
class ICatalogDeclarations
{
public:
  virtual ~ICatalogDeclarations() = default;
  virtual const std::vector<std::string> &sql() const = 0;
  virtual ICatalogBuildProgram *program() { return nullptr; }
};

struct ExtensionPackageScript
{
  std::string from_version_; // Empty for the base installation script.
  std::string to_version_;
  std::string sql_;
};

struct ExtensionPackageSource
{
  std::string name_;
  std::string from_version_; // Empty for fresh install; nonempty for update (including no-op).
  std::string version_;
  std::string native_module_;
  std::string schema_;
  // Ordered, separately parsed files. Concatenation changes SQL token/comment
  // boundaries and must not be used to execute an installation/update chain.
  std::vector<ExtensionPackageScript> scripts_;
  std::vector<std::string> requires_;
  bool relocatable_ = false;
  bool native_install_ = false; // Explicit control source; fresh install has no static SQL.
  std::vector<std::string> prerequisites_{}; // Intermediate-only requirements, locked but not persisted.
};

// Read-only core package source API. Empty requested_version selects the
// control default. root is administrator-controlled, immutable during reads.
// No SQL parsing/execution, dependency installation, native loading or privilege
// escalation occurs here. Inputs must not alias source or error. On error source
// is empty; diagnostics are best effort (allocation/internal errors may omit them).
int read_extension_package(const std::string &root, const std::string &name,
                           const std::string &requested_version,
                           ExtensionPackageSource &source, std::string &error);

// Same source/ownership contract, but chooses only edges from installed_version.
// Empty installed_version is invalid. No-op success has zero scripts and equal
// from/version; this is not proof of catalog identity/version or authorization.
int read_extension_update(const std::string &root, const std::string &name,
                          const std::string &installed_version, const std::string &requested_version,
                          ExtensionPackageSource &source, std::string &error);

// Host-only in-memory source admission, delegated to the Rust package model.
// Validates a preselected install/update chain, not a filesystem version graph.
// Does not parse SQL, check caller privileges, or create Extension membership.
// Input is immutable during the call and must not alias error. No module loads.
int validate_extension_package_source(const ExtensionPackageSource &source, std::string &error);

} } }
#endif
