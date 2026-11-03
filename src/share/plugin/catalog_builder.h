/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_PLUGIN_CATALOG_BUILDER_H_
#define SEEKDB_SHARE_PLUGIN_CATALOG_BUILDER_H_

#include <cstdint>
#include <string>

namespace oceanbase { namespace share { namespace plugin {
struct ExtensionInstallSpec;
enum class CatalogRoutineKind : uint32_t { FUNCTION = 1, PROCEDURE = 2 };

// Host-only transaction-local construction capability. One new FUNCTION or
// PROCEDURE per call, through the normal parser/resolver/privilege path. On
// success the reserved ID and object are visible to subsequent construction in
// this installation's schema view, but are not committed or globally published.
// Borrowed for build(), same-thread, no transaction control or SQL execution.
class ICatalogRoutineBuilder
{
public:
  virtual ~ICatalogRoutineBuilder() = default;
  virtual int create_routine(const std::string &sql, uint64_t &object_id, std::string &error) = 0;
  // Unquoted name in the installation database; normal visibility and name
  // comparison. Success with ID=0 means absent. Does not create a dependency.
  virtual int lookup_routine(CatalogRoutineKind kind, const std::string &name,
                             uint64_t &object_id, std::string &error) = 0;
};

// Bound by the host; NOT a native plugin ABI. The program and its backing
// module lease must outlive the synchronous install and schema publication.
// Preflight is read-only and may repeat. Build executes at most once after the
// static script, under the existing catalog coordinator's schema transaction.
// No external side effects: any construction failure rolls back the install.
class ICatalogBuildProgram
{
public:
  virtual ~ICatalogBuildProgram() = default;
  virtual int preflight(const ExtensionInstallSpec &spec, std::string &error) = 0;
  virtual int build(ICatalogRoutineBuilder &builder, std::string &error) = 0;
};

} } }
#endif
