/* Copyright (c) 2026 OceanBase. Licensed under the Apache License, Version 2.0. */
#ifndef SEEKDB_SHARE_EXTENSION_INSTALL_H_
#define SEEKDB_SHARE_EXTENSION_INSTALL_H_

#include <cstdint>
#include <string>
#include <vector>

namespace oceanbase {
namespace common { class ObMySQLTransaction; }
namespace share {
class ObPluginSqlConnection;
namespace plugin {

struct ExtensionMemberIdentity
{
  uint32_t object_class_ = 0;
  uint64_t object_id_ = 0;
};

struct ExtensionInstallSpec
{
  uint64_t tenant_id_ = 0;
  uint64_t database_id_ = 0;
  uint64_t owner_id_ = 0;
  std::string name_;
  std::string version_;
  // Empty for pure SQL; a logical module ID, never a filename/generation.
  std::string native_module_id_;
  std::vector<ExtensionMemberIdentity> members_;
  std::vector<std::string> requires_; // Existing Extensions in this tenant/database, never auto-installed.
  std::vector<std::string> prerequisites_{}; // Migration-only providers, excluded from final dependency edges.
};

struct ExtensionDropRequest
{
  uint64_t tenant_id_ = 0;
  uint64_t database_id_ = 0;
  std::string name_;
  // Zero means lookup by name; a nonzero ID fences a previously resolved name.
  uint64_t expected_extension_id_ = 0;
  bool cascade_ = false;
};

struct ExtensionSnapshot
{
  uint64_t extension_id_ = 0;
  ExtensionInstallSpec installed_;
};
using ExtensionDropSnapshot = ExtensionSnapshot;
using ExtensionUpdateSnapshot = ExtensionSnapshot;

struct ExtensionUpdateRequest
{
  uint64_t tenant_id_ = 0;
  uint64_t database_id_ = 0;
  std::string name_;
  // Both are mandatory: a plan must not update a recreated name or newer version.
  uint64_t expected_extension_id_ = 0;
  std::string from_version_;
  std::string to_version_;
  std::vector<std::string> requires_; // Complete target set; only a versioned update may change it.
  std::vector<std::string> prerequisites_{}; // Selected intermediate versions, not retained in final catalog.
};

// One non-locking installation-row observation for choosing an update path.
// Not a member snapshot or authority to execute the plan. An update must bind
// this ID/version into ExtensionUpdateRequest and recheck them under its lock.
struct ExtensionVersionSnapshot
{
  uint64_t extension_id_ = 0;
  uint64_t owner_id_ = 0;
  std::string version_;
  std::string native_module_id_;
};

// Core-only update adapter. Admit checks the locked source version, owner,
// privileges, full script support and dependency effects before detach. Apply
// uses the same transaction for all schema changes and returns the COMPLETE
// surviving/new membership, not only a delta. Unchanged members must be retained.
// Never start/end transactions, publish schema, or change installation metadata.
class IExtensionSchemaUpdater
{
public:
  virtual ~IExtensionSchemaUpdater() = default;
  virtual int preflight(const ExtensionUpdateRequest &request, std::string &error) = 0;
  virtual int admit(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                    const ExtensionUpdateSnapshot &snapshot, std::string &error) = 0;
  virtual int apply(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                    const ExtensionUpdateSnapshot &snapshot,
                    std::vector<ExtensionMemberIdentity> &members, std::string &error) = 0;
};

class IExtensionCatalogUpdater
{
public:
  virtual ~IExtensionCatalogUpdater() = default;
  // Core-only, read-only: no transaction ownership, member reads, package I/O,
  // module loading or publication. Root checks authenticated owner/SUPER before
  // returning this observation to the command. Failure clears snapshot/error
  // first, then may set a diagnostic. name must not alias snapshot/error storage.
  virtual int read_update_source(uint64_t tenant_id, uint64_t database_id, const std::string &name,
                                 ExtensionVersionSnapshot &snapshot, std::string &error) = 0;
  // Requires normal Root DDL serialization, one UNSTARTED DDL transaction and
  // refreshed schema version. Success preserves ID/owner/module. changed is false
  // for an authorized same-version no-op, true for a committed version change.
  // Unknown commit never sets either output; reconcile before any retry.
  virtual int update_extension(const ExtensionUpdateRequest &request, IExtensionSchemaUpdater &updater,
                               uint64_t &extension_id, bool &changed, std::string &error,
                               common::ObMySQLTransaction *ddl_transaction,
                               int64_t refreshed_schema_version) = 0;
};

// Host-only schema removal capability. Admission uses the LOCKED catalog
// snapshot and must check authenticated ownership, supported object classes and
// incoming dependencies before any member protection is detached. Apply must
// drop every member using this same transaction, without publishing schema or
// starting/ending transactions. Catalog coordinates rollback and metadata only.
class IExtensionSchemaDropper
{
public:
  virtual ~IExtensionSchemaDropper() = default;
  virtual int preflight(const ExtensionDropRequest &request, std::string &error) = 0;
  virtual int admit(ObPluginSqlConnection &connection, const ExtensionDropRequest &request,
                    const ExtensionDropSnapshot &snapshot, std::string &error) = 0;
  virtual int apply(ObPluginSqlConnection &connection, const ExtensionDropSnapshot &snapshot,
                    std::string &error) = 0;
};

class IExtensionCatalogDropper
{
public:
  virtual ~IExtensionCatalogDropper() = default;
  virtual int drop_extension(const ExtensionDropRequest &request, IExtensionSchemaDropper &dropper,
                             uint64_t &dropped_extension_id, std::string &error,
                             common::ObMySQLTransaction *ddl_transaction,
                             int64_t refreshed_schema_version) = 0;
};

// Core schema adapter, NOT handed to native plugins. Preflight performs
// privilege/package/DDL-support checks without writes. Apply uses only the
// supplied transaction and returns actual schema identities. No independent
// commits, implicit-commit DDL, early hook publication or external tasks.
class IExtensionSchemaInstaller
{
public:
  virtual ~IExtensionSchemaInstaller() = default;
  virtual int preflight(const ExtensionInstallSpec &spec, std::string &error) = 0;
  virtual int apply(ObPluginSqlConnection &connection, const ExtensionInstallSpec &spec,
                    std::vector<ExtensionMemberIdentity> &members, std::string &error) = 0;
};

// Composition boundary: Rootserver supplies schema work; the sole catalog
// implementation owns installation coordination. No loader/registry exposure.
// Caller retains the provider and an UNSTARTED DDL transaction throughout the
// call. Active user transactions cannot be joined by this autocommit path.
// Unknown commit outcomes require reconciliation, never blind retries.
class IExtensionCatalogInstaller
{
public:
  virtual ~IExtensionCatalogInstaller() = default;
  virtual int install_extension(const ExtensionInstallSpec &spec, IExtensionSchemaInstaller &installer,
                                uint64_t &extension_id, std::string &error,
                                common::ObMySQLTransaction *ddl_transaction = nullptr,
                                int64_t refreshed_schema_version = 0) = 0;
};

} } }
#endif
