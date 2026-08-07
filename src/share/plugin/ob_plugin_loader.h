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

#ifndef OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_LOADER_H_
#define OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_LOADER_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "seekdb/plugin/seekdb_plugin_abi.h"
#include "seekdb/plugin/execution_spi.h"
#include "share/plugin/ob_plugin_registry.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

struct ObPluginArtifactMetadata
{
  ObPluginArtifactMetadata();

  std::string plugin_id_;
  std::string build_id_;
  std::string package_digest_;
  seekdb_plugin_semantic_version_t package_version_;
  uint32_t catalog_version_;
  uint32_t data_format_version_;
};

// This is deliberately only the minimum identity envelope consumed by the
// runtime loader.  It is not a complete signed-package model: production
// activation must additionally reconcile ABI, provides/requires, permissions,
// file hashes and catalog objects (or bind them with an authenticated canonical
// manifest digest) before the verifier may be treated as a trust boundary.

// A successful verifier returns an ownership token for the exact immutable
// artifact it verified.  The token must pin load_path() against replacement
// (for example by holding a content-addressed store lease) until destruction.
// The minimum identity metadata above comes from the verified package and is
// compared with the binary manifest before any plugin callback runs.  A
// production verifier must extend that reconciliation to the complete package
// contract described above.
class ObPluginVerifiedArtifact
{
public:
  virtual ~ObPluginVerifiedArtifact() = default;
  virtual const std::string &load_path() const = 0;
  virtual const ObPluginArtifactMetadata &metadata() const = 0;
};

// Verification is mandatory.  A production verifier combines a package
// catalog allow-list, signature/hash verification and an immutable artifact
// lease.  Catalog discovery and batch dependency-DAG loading remain upper-layer
// responsibilities in loader v1.
class ObPluginVerifier
{
public:
  virtual ~ObPluginVerifier() = default;

  // Implementations must not throw; the loader nevertheless catches exceptions
  // at this boundary.  Success with a null artifact is rejected.  Verification
  // runs under a logical load reservation but without the global loader mutex;
  // it must not recursively start another management operation.  Catalog
  // authorization is deliberately separate and is obtained through the
  // activation guard after this immutable identity has been verified.
  virtual int verify_and_pin(
      const std::string &canonical_path,
      std::unique_ptr<ObPluginVerifiedArtifact> &artifact,
      std::string &error) const = 0;
};

enum class ObPluginActivationMode : uint8_t
{
  ACTIVATE = 0,
  STARTUP_RECOVERY
};

enum class ObPluginActivationPhase : uint8_t
{
  NONE = 0,
  CATALOG_BEGIN,
  LOADING,
  INITIALIZING,
  STARTING,
  DISCOVERING,
  PREPARING_CANDIDATE,
  CATALOG_FINISH,
  PROMOTING,
  COMPLETE
};

// Immutable identity presented to the catalog before any plugin lifecycle
// callback runs.  package_digest comes from the pinned verifier artifact, not
// from the DSO.  A recovery request additionally binds the unfinished durable
// intent so the catalog cannot silently create a fresh activation.
struct ObPluginActivationRequest
{
  ObPluginActivationRequest();

  ObPluginActivationMode mode_;
  std::string relative_path_;
  std::string plugin_id_;
  std::string build_id_;
  std::string package_digest_;
  seekdb_plugin_semantic_version_t package_version_;
  uint32_t catalog_version_;
  uint32_t data_format_version_;
  uint64_t expected_generation_;
  std::string expected_runtime_incarnation_;
  std::string expected_operation_id_;
};

// Host-owned description of one service dependency resolved for the candidate
// generation.  Persisting the provider identity together with the requested
// range lets the catalog enforce RESTRICT and rebuild a startup DAG without
// retaining pointers into either DSO.  Optional requirements which were not
// resolved are deliberately absent: they do not create a durable dependency.
struct ObPluginRuntimeServiceDependency
{
  ObPluginRuntimeServiceDependency();

  std::string service_id_;
  seekdb_plugin_version_range_t requested_version_;
  uint64_t required_capabilities_;
  bool optional_;
  std::string provider_plugin_id_;
  uint64_t provider_generation_;
  seekdb_plugin_semantic_version_t provider_version_;
};

// Complete host-owned candidate/failure observation used to durably advance an
// activation intent.  Before promote, services_/extensions_ are still
// invisible to ordinary registry readers.
struct ObPluginRuntimeActivationResult
{
  ObPluginRuntimeActivationResult();

  // status_ is the primary activation failure (or OB_SUCCESS for a prepared/
  // ACTIVE result).  Rollback stop failure does not overwrite that cause;
  // actual_state_=BLOCKED plus error_ records the cleanup outcome.
  int status_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
  ObPluginState actual_state_;
  ObPluginActivationPhase phase_;
  bool start_entered_;
  bool candidate_prepared_;
  uint64_t candidate_base_epoch_;
  std::vector<ObPluginServiceInfo> services_;
  std::vector<ObPluginExtensionInfo> extensions_;
  std::vector<ObPluginRuntimeServiceDependency> dependencies_;
  std::string error_;
};

typedef int32_t ObPluginActivationDecision;
enum ObPluginActivationDecisionValue
{
  OB_PLUGIN_ACTIVATION_NOT_COMMITTED = 0,
  OB_PLUGIN_ACTIVATION_PROMOTE = 1,
  OB_PLUGIN_ACTIVATION_UNKNOWN = 2
};

// Token returned only after the catalog transaction has durably installed
// ownership/dependency rows and PROMOTE_PENDING.  complete() records actual
// ACTIVE and clears the intent after no-fail registry promotion.  A failure or
// unfinished destructor must retain a replayable PROMOTE_PENDING record;
// runtime is already ACTIVE and must never be rolled back merely because this
// final catalog write failed.
class ObPluginActivationCommit
{
public:
  virtual ~ObPluginActivationCommit() noexcept = default;
  virtual int complete(const ObPluginRuntimeActivationResult &runtime_result,
                       std::string &error) noexcept = 0;
};

// A durable, identity-scoped activation intent.  begin_activation() has
// authorized the exact pinned package and serialized this identity before the
// permit is returned.  It must not retain a catalog mutex while plugin code is
// called.  generation/runtime_incarnation/operation_id are catalog-assigned
// immutable fencing identities and must remain valid for the permit lifetime.
// For one plugin identity, a generation value which has ever been assigned
// must never be assigned to a different attempt, even after abort, uninstall,
// or archival.  Runtime incarnation and operation id are likewise unique in
// durable history.  Startup recovery reuses the original tuple; a new attempt
// receives a wholly new tuple.  This non-reuse rule prevents stale metadata
// from binding a different runtime through an ABA collision.
//
// commit_candidate() durably records object ownership/dependencies and
// PROMOTE_PENDING.  Only decision=PROMOTE with a non-NULL commit token
// authorizes registry publication.  NOT_COMMITTED permits safe rollback;
// UNKNOWN forbids promote, abort and identity reuse until recovery.  abort()
// is used for a final failed/BLOCKED runtime result before commit, and is also
// mandatory after commit_candidate() returns NOT_COMMITTED and runtime cleanup
// finishes.  commit_candidate() is attempted at most once; PROMOTE transfers
// finalization to ObPluginActivationCommit.  Destruction while unresolved must
// leave or mark a recoverable durable intent.
class ObPluginActivationPermit
{
public:
  virtual ~ObPluginActivationPermit() noexcept = default;
  virtual uint64_t generation() const noexcept = 0;
  virtual const std::string &runtime_incarnation() const noexcept = 0;
  virtual const std::string &operation_id() const noexcept = 0;
  virtual int commit_candidate(
      const ObPluginRuntimeActivationResult &candidate_result,
      ObPluginActivationDecision &decision,
      std::unique_ptr<ObPluginActivationCommit> &commit,
      std::string &error) noexcept = 0;
  virtual int abort(const ObPluginRuntimeActivationResult &runtime_result,
                    std::string &error) noexcept = 0;
};

class ObPluginActivationGuard
{
public:
  virtual ~ObPluginActivationGuard() noexcept = default;
  virtual int begin_activation(
      const ObPluginActivationRequest &request,
      std::unique_ptr<ObPluginActivationPermit> &permit,
      std::string &error) const noexcept = 0;
};

struct ObPluginRecoveryActivation
{
  ObPluginRecoveryActivation();

  std::string relative_path_;
  std::string plugin_id_;
  std::string package_digest_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
};

enum class ObPluginDisablePhase : uint8_t
{
  NONE = 0,
  QUIESCE,
  DRAIN,
  STOP,
  DEINIT,
  MARK_STOPPED,
  COMPLETE
};

// Complete runtime observation passed back to the catalog recovery protocol.
// status_ alone is insufficient: a drain timeout is retryable QUIESCING, while
// a stop callback failure is BLOCKED and forbids identity reuse.
struct ObPluginRuntimeDisableResult
{
  ObPluginRuntimeDisableResult();

  int status_;
  uint64_t generation_;
  ObPluginState actual_state_;
  ObPluginDisablePhase phase_;
  bool stop_entered_;
  std::string error_;
};

// A catalog-issued logical exclusion for one plugin.  begin_restricted_disable()
// has already persisted DISABLING (or an equivalent recovery marker), rejected
// durable dependants under RESTRICT, and blocked creation of new durable
// dependencies before returning this permit.  It must not retain a catalog
// mutex: plugin callbacks may need catalog services while runtime is drained.
//
// record_stop_entered() is committed outside the loader mutex immediately
// before the first fallible stop callback.  Only its proven success authorizes
// callback entry.  finish() is then called outside the loader mutex after the
// runtime attempt.  It must persist the actual runtime result (including
// QUIESCING or STOPPED recovery state); a successful stop cannot be made active
// again merely because catalog finalization fails.  Destroying an unfinished
// permit must leave a durable recovery marker and release the logical
// exclusion.  Both operations are noexcept and replay is keyed by the permit's
// durable intent.
class ObPluginDisablePermit
{
public:
  virtual ~ObPluginDisablePermit() noexcept = default;
  // This checkpoint must commit before the loader enters the first fallible
  // stop callback.  A non-success result forbids entering stop; the caller
  // must destroy the unresolved permit so an uncertain checkpoint remains
  // fail-closed for startup recovery.
  virtual int record_stop_entered(std::string &error) noexcept = 0;
  virtual int finish(const ObPluginRuntimeDisableResult &runtime_result,
                     std::string &error) noexcept = 0;
};

// The loader is an execution layer, not the authoritative catalog.  The
// coordinator must atomically enter DISABLING, exclude new durable dependencies
// and enforce RESTRICT before issuing a permit.  The begin/finish calls are
// deliberately made without the loader mutex, establishing the only lock order:
// catalog state transition -> loader runtime transition -> catalog finalization.
// expected_generation binds the catalog intent to the exact runtime instance.
// A non-success return must either leave no durable mutation or return a permit
// whose no-throw destructor can recover it; implementations must never throw.
class ObPluginDisableGuard
{
public:
  virtual ~ObPluginDisableGuard() noexcept = default;
  virtual int begin_restricted_disable(
      const std::string &plugin_id,
      uint64_t expected_generation,
      std::unique_ptr<ObPluginDisablePermit> &permit,
      std::string &error) const noexcept = 0;
};

struct ObPluginStatusSnapshot
{
  ObPluginStatusSnapshot();

  std::string plugin_id_;
  std::string canonical_path_;
  seekdb_plugin_semantic_version_t version_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
  ObPluginState state_;
  int64_t lease_count_;
  std::string last_error_;
};

// Structured classification for the most recent load()/startup-recovery
// attempt.  The return status remains the authoritative error code; this
// narrow reason lets coordinators distinguish a retryable dependency ordering
// failure from unrelated failures which happen to share OB_ENTRY_NOT_EXIST.
enum class ObPluginLoadFailureReason : uint8_t
{
  NONE = 0,
  REQUIRED_SERVICE_UNAVAILABLE,
  OTHER
};

// A process-local activation execution engine.  It intentionally loads one
// plugin at a time and rejects an unresolved mandatory dependency; callers
// which need batch loads must order a verified catalog/DAG and invoke load() in
// that order.  The permit/candidate protocol prevents catalog/runtime partial
// publication, but this class is not by itself a production plugin manager: it
// still requires a durable catalog coordinator, startup ready gate and
// production package verifier supplied by the server integration.
class ObPluginLoader
{
public:
  ObPluginLoader();
  ~ObPluginLoader();

  ObPluginLoader(const ObPluginLoader &) = delete;
  ObPluginLoader &operator=(const ObPluginLoader &) = delete;

  // trusted_directory must already exist.  It is canonicalized once and all
  // later load requests are constrained beneath that canonical directory.
  // Shared ownership is mandatory because a failed process-exit drain retains
  // the complete runtime domain rather than unmapping code beneath callbacks.
  int init(const std::string &trusted_directory,
           const std::shared_ptr<const ObPluginVerifier> &verifier,
           const std::shared_ptr<const ObPluginActivationGuard> &activation_guard,
           const std::shared_ptr<const ObPluginDisableGuard> &disable_guard,
           const std::shared_ptr<ObPluginServiceRegistry> &registry);
  bool is_initialized() const;

  // relative_path is an untrusted catalog-relative path.  Absolute paths,
  // traversal, symlink escapes and non-regular files are rejected.  The
  // verifier pins the artifact; the activation guard independently authorizes
  // the exact digest and assigns the durable runtime identity.  A
  // stopped/resident or catalog-uncertain identity cannot be loaded again
  // before process exit.
  int load(const std::string &relative_path, uint64_t *loaded_generation = nullptr);

  // Replays one catalog-provided unfinished/desired activation before server
  // ready.  The pinned artifact and permit identities must exactly match the
  // durable record; mismatch fails closed.  The catalog coordinator owns DAG
  // ordering and calls this once per entry in topological order.
  int recover_startup_activation(
      const ObPluginRecoveryActivation &recovery,
      uint64_t *loaded_generation = nullptr);

  // Logical unload only.  The mandatory catalog coordinator holds a logical
  // durable-dependency exclusion permit across runtime quiesce and records its
  // actual result afterward.  No catalog or loader mutex is held while calling
  // the other subsystem.  The DSO remains mapped until
  // shutdown_for_process_exit().  Process shutdown itself does not alter
  // catalog state and therefore bypasses that coordinator.
  int disable(const std::string &plugin_id, int64_t drain_timeout_us);

  // Quiesces active plugins in reverse load order, then closes every retained
  // DSO.  This is only valid from the server's terminal process-exit phase,
  // after new work has been stopped.  A timeout leaves affected modules mapped
  // and the call may be retried; it is not a runtime hot-unload operation.  A
  // successful call permanently consumes this loader object: init() cannot be
  // used to start a second runtime domain afterward.
  int shutdown_for_process_exit(int64_t drain_timeout_us);

  // Invoke one executable function service while holding a registry lease.
  // The lease pins the provider generation for the complete callback, so a
  // concurrent logical disable cannot unmap or stop the implementation under
  // an executing SQL expression.
  int execute_function(const char *service_id,
                       uint32_t abi_major,
                       uint32_t required_minor,
                       const seekdb_plugin_execution_context_v1_t *context,
                       const seekdb_plugin_execution_value_v1_t *arguments,
                       uint32_t argument_count);

  // Resolve a catalog function extension by SQL name, atomically acquire its
  // extension/implementation leases, and invoke the same execution SPI. This
  // is the generic path used by SQL adapters; callers need not embed a
  // plugin-specific service id in core code.
  int execute_extension(seekdb_plugin_extension_kind_t kind,
                        const char *sql_name,
                        const seekdb_plugin_execution_context_v1_t *context,
                        const seekdb_plugin_execution_value_v1_t *arguments,
                        uint32_t argument_count);

  int get_status(const std::string &plugin_id, ObPluginStatusSnapshot &status) const;
  int list_status(std::vector<ObPluginStatusSnapshot> &statuses) const;
  std::string last_error() const;
  ObPluginLoadFailureReason last_failure_reason() const;
  std::string trusted_directory() const;

private:
  int activate_internal(const std::string &relative_path,
                        const ObPluginRecoveryActivation *recovery,
                        uint64_t *loaded_generation);

  struct Impl;
  std::unique_ptr<Impl> impl_;
};

// Manager is a useful name at call sites; there is deliberately one
// implementation so lifecycle and module ownership cannot diverge.
using ObPluginManager = ObPluginLoader;

} // namespace plugin
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_LOADER_H_
