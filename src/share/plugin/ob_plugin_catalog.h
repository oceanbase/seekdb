/*
 * Copyright (c) 2026 OceanBase.
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

#ifndef OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_CATALOG_H_
#define OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_CATALOG_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "share/plugin/ob_plugin_loader.h"
#include "share/plugin/extension_install.h"
#include "common/mysqlclient/ob_isql_client.h"

namespace oceanbase
{
namespace common { class ObMySQLTransaction; }
namespace share
{

class ObPluginSqlConnection;

namespace plugin
{

// Desired state is independent from the last observed runtime state.  Rows are
// retained after UNINSTALLED so generation and operation identities can never
// be reassigned after an uninstall/reinstall cycle.
enum class ObPluginDesiredState : uint8_t
{
  ACTIVE = 0,
  DISABLED,
  UNINSTALLED
};

// This is evidence recorded by the package installation path, not a claim the
// catalog can derive itself.  In particular, IDENTITY_PINNED is the current R0
// verifier boundary and must not be displayed as hash/signature verification.
enum class ObPluginVerificationLevel : uint8_t
{
  NOT_VERIFIED = 0,
  IDENTITY_PINNED,
  HASH_VERIFIED,
  SIGNATURE_VERIFIED
};

enum class ObPluginCatalogOperationKind : uint8_t
{
  ACTIVATE = 0,
  DISABLE,
  UNINSTALL
};

enum class ObPluginCatalogOperationState : uint8_t
{
  CATALOG_BEGIN = 0,
  PROMOTE_PENDING,
  DISABLING,
  RECOVERY_REQUIRED,
  COMPLETED,
  ABORTED
};

enum class ObPluginDependencyConsumerKind : uint8_t
{
  PLUGIN = 0,
  CATALOG_OBJECT,
  USER_OBJECT,
  PERSISTENT_DATA,
  BACKGROUND_JOB
};

enum class ObPluginDependencyKind : uint8_t
{
  SERVICE = 0,
  EXTENSION_OBJECT,
  PERSISTENT_FORMAT
};

struct ObPluginPackageInstallSpec
{
  ObPluginPackageInstallSpec();

  std::string relative_path_;
  ObPluginArtifactMetadata artifact_;
  ObPluginVerificationLevel verification_level_;
  std::string operator_id_;
  std::string audit_id_;
};

struct ObPluginCatalogRecord
{
  ObPluginCatalogRecord();

  std::string plugin_id_;
  std::string relative_path_;
  std::string build_id_;
  std::string package_digest_;
  seekdb_plugin_semantic_version_t package_version_;
  uint32_t catalog_version_;
  uint32_t data_format_version_;
  ObPluginVerificationLevel verification_level_;
  ObPluginDesiredState desired_state_;
  ObPluginState actual_state_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
  int32_t last_phase_;
  int last_status_;
  std::string last_error_;
  std::string operator_id_;
  std::string audit_id_;
  int64_t created_at_us_;
  int64_t modified_at_us_;
};

// Generic durable edge used both by activation (plugin -> provider service)
// and by core catalog/schema adapters (user object/data/job -> plugin).  Core
// DDL code must add/remove its edge in the same SQL catalog transaction as
// the corresponding local schema mutation; the convenience methods below own
// their own transaction and are intended for callers without a wider one.
struct ObPluginDependencySpec
{
  ObPluginDependencySpec();

  ObPluginDependencyConsumerKind consumer_kind_;
  std::string consumer_id_;
  std::string consumer_plugin_id_;
  uint64_t consumer_generation_;
  std::string provider_plugin_id_;
  uint64_t provider_generation_;
  ObPluginDependencyKind dependency_kind_;
  std::string dependency_id_;
  // Required for SERVICE edges and zero for non-service dependency kinds.
  // The ABI major is part of durable dependency identity; service_id alone is
  // not sufficient because one provider may publish multiple ABI majors.
  uint32_t service_abi_major_;
  seekdb_plugin_version_range_t requested_version_;
  uint64_t required_capabilities_;
};

struct ObPluginRestrictBlocker
{
  ObPluginDependencyConsumerKind consumer_kind_;
  std::string consumer_id_;
  std::string consumer_plugin_id_;
  uint64_t consumer_generation_;
  ObPluginDependencyKind dependency_kind_;
  std::string dependency_id_;
  uint32_t service_abi_major_;
};

struct ObPluginStartupEntry
{
  ObPluginStartupEntry();

  std::string plugin_id_;
  std::string relative_path_;
  bool exact_recovery_;
  ObPluginRecoveryActivation recovery_;
};

struct ObPluginStartupReport
{
  ObPluginStartupReport();

  uint64_t planned_;
  uint64_t activated_;
  uint64_t exact_replays_;
  std::string failed_plugin_id_;
};

// Single-node durable plugin catalog backed by seekdb's WAL-backed SQL system catalog.
// It implements the loader guard contracts and is the only component allowed
// to assign generation/runtime/operation fencing identities.  The class never
// holds its catalog mutex while invoking loader or plugin code.
class ObPluginCatalog final : public ObPluginActivationGuard,
                              public ObPluginDisableGuard,
                              public IExtensionCatalogInstaller,
                              public IExtensionCatalogDropper,
                              public IExtensionCatalogUpdater
{
public:
  ObPluginCatalog();
  ~ObPluginCatalog() override;

  ObPluginCatalog(const ObPluginCatalog &) = delete;
  ObPluginCatalog &operator=(const ObPluginCatalog &) = delete;

  int init(common::ObISQLClient *sql_client);
  bool is_initialized() const;

  int install_package(const ObPluginPackageInstallSpec &spec,
                      std::string &error);
  // Autocommit management path: owns ONE SQL catalog transaction spanning
  // schema apply and membership recording. spec.members_ must initially be
  // empty. Does not join a user's existing transaction; SQL command dispatch
  // must choose transaction semantics explicitly before invoking this API.
  // COMMIT/rollback transport failures report OB_TRANS_UNKNOWN and must be
  // reconciled by database/name, never blindly retried. No plugin callbacks or
  // schema work runs while holding the package catalog mutex.
  int install_extension(const ExtensionInstallSpec &spec,
                         IExtensionSchemaInstaller &installer,
                         uint64_t &extension_id, std::string &error,
                         common::ObMySQLTransaction *ddl_transaction = nullptr,
                         int64_t refreshed_schema_version = 0) override;
  // One owned DDL transaction: lock snapshot/admit, detach members, drop actual
  // schema objects, remove instance, commit. No package files/module required.
  // Only a committed result sets dropped_extension_id. Caller publishes schema
  // after commit and MUST hold the normal Root DDL serialization authority.
  int drop_extension(const ExtensionDropRequest &request, IExtensionSchemaDropper &dropper,
                      uint64_t &dropped_extension_id, std::string &error,
                      common::ObMySQLTransaction *ddl_transaction,
                      int64_t refreshed_schema_version) override;
  int update_extension(const ExtensionUpdateRequest &request, IExtensionSchemaUpdater &updater,
                        uint64_t &extension_id, bool &changed, std::string &error,
                        common::ObMySQLTransaction *ddl_transaction,
                        int64_t refreshed_schema_version) override;
  int read_update_source(uint64_t tenant_id, uint64_t database_id, const std::string &name,
                         ExtensionVersionSnapshot &snapshot, std::string &error) override;
  // A supplied DDL transaction must be unstarted and outlive this call. Its
  // virtual start(proxy, schema_version)/end methods own the SAME transaction
  // used for schema and membership writes (including DDL watermark/signals).
  // A positive, freshly checked schema version is required. Already active
  // transactions are rejected without ending them. The schema caller publishes
  // only after success; publication failure does not undo the committed install.
  // Host install-coordinator API, not a public plugin bypass. Caller verifies
  // database/owner privileges, canonicalizes name, and creates/validates every
  // schema member in this SAME active write transaction before recording it.
  // No commit, rollback, independent transaction or registry publication here.
  // On any failure caller MUST roll back its whole install transaction. Output
  // is provisional until that transaction commits. IDs are never generations.
  int record_extension_install(ObPluginSqlConnection &connection,
                               const ExtensionInstallSpec &spec,
                               uint64_t &extension_id, std::string &error);
  // Called only after the coordinator has dropped/reassigned all member schema
  // objects in the same transaction. This removes ownership metadata, not the
  // objects themselves. Expected owner and stable installation ID fence stale
  // requests. No module availability requirement: broken installs need cleanup.
  int record_extension_drop(ObPluginSqlConnection &connection,
                            uint64_t tenant_id, uint64_t database_id,
                            uint64_t extension_id, uint64_t expected_owner_id,
                            std::string &error);
  int uninstall_restrict(const std::string &plugin_id,
                         const std::string &operator_id,
                         const std::string &audit_id,
                         std::vector<ObPluginRestrictBlocker> &blockers,
                         std::string &error);

  int add_dependency(const ObPluginDependencySpec &dependency,
                     std::string &error);
  int remove_dependency(const ObPluginDependencySpec &dependency,
                        std::string &error);
  // Transaction-scoped variants for schema/DDL integration.  connection must
  // already be in a write transaction. Additions lock the provider package row
  // before admission; RESTRICT locks that same row before its current-read
  // dependency check. Removal deletes the explicitly fenced dependency row.
  // These overloads never acquire the catalog mutex.  While holding that
  // external writer transaction, the caller MUST NOT call any other catalog
  // API which acquires the mutex; it must commit/rollback first.  This single
  // writer -> no catalog-mutex rule prevents inversion with management paths,
  // whose order is catalog mutex -> SQL row locks.
  int add_dependency(ObPluginSqlConnection &connection,
                     const ObPluginDependencySpec &dependency,
                     std::string &error);
  int remove_dependency(ObPluginSqlConnection &connection,
                        const ObPluginDependencySpec &dependency,
                        std::string &error);
  // Column metadata identifies a logical type, NOT a module generation.
  // Resolve the current durable provider fence in the caller's schema write
  // transaction. Removal uses the recorded edge even if the module is stopped.
  // No catalog mutex, independent transaction, commit or runtime lookup here.
  int mutate_type_dependency(ObPluginSqlConnection &connection,
                             const seekdb_plugin_sql_binding_v1_t &identity,
                             uint64_t table_id, uint64_t column_id, bool add,
                             std::string &error);
  int list_restrict_blockers(
      const std::string &plugin_id,
      std::vector<ObPluginRestrictBlocker> &blockers) const;

  int get_record(const std::string &plugin_id,
                 ObPluginCatalogRecord &record) const;
  int list_records(std::vector<ObPluginCatalogRecord> &records) const;

  // Called once after core schema and SQL catalog are usable and before business
  // ready.  It turns completed ACTIVE rows from the previous process into new
  // activation attempts, preserves unfinished intent tuples for exact replay,
  // topologically orders persistent plugin dependencies, and fails closed.
  int prepare_startup_recovery(std::vector<ObPluginStartupEntry> &entries,
                               std::string &error);
  int recover_before_server_ready(ObPluginLoader &loader,
                                  ObPluginStartupReport &report,
                                  std::string &error);
  int check_server_ready(std::string &error) const;

  int begin_activation(
      const ObPluginActivationRequest &request,
      std::unique_ptr<ObPluginActivationPermit> &permit,
      std::string &error) const noexcept override;
  int begin_restricted_disable(
      const std::string &plugin_id,
      uint64_t expected_generation,
      std::unique_ptr<ObPluginDisablePermit> &permit,
      std::string &error) const noexcept override;

private:
  int lock_extension_requirements(ObPluginSqlConnection &connection, const ExtensionInstallSpec &spec,
                                  std::vector<uint64_t> &identities, std::string &error);
  int check_extension_restrict(ObPluginSqlConnection &connection, uint64_t tenant_id,
                               uint64_t database_id, uint64_t extension_id);
  int prepare_extension_requirement_update(ObPluginSqlConnection &connection,
      const ExtensionUpdateRequest &request, std::vector<uint64_t> &identities, std::string &error);
  // Only the coordinator may detach, after authenticated schema/dependency
  // admission. These are not independent metadata-deletion entry points.
  int lock_extension_for_drop(ObPluginSqlConnection &connection, const ExtensionDropRequest &request,
                              ExtensionDropSnapshot &snapshot, std::string &error);
  int detach_extension_members(ObPluginSqlConnection &connection, const ExtensionDropSnapshot &snapshot);
  int record_extension_update(ObPluginSqlConnection &connection, const ExtensionUpdateRequest &request,
                              const ExtensionUpdateSnapshot &snapshot,
                              const std::vector<ExtensionMemberIdentity> &members,
                              const std::vector<uint64_t> &dependencies);
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace plugin
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_CATALOG_H_
