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
  seekdb_plugin_semantic_version_t package_version_;
  uint32_t catalog_version_;
  uint32_t data_format_version_;
};

// This is deliberately only the minimum identity envelope used by the R0
// loader preview.  It is not a complete signed-package model: production
// activation must additionally reconcile ABI, provides/requires, permissions,
// file hashes and catalog objects (or bind them with an authenticated canonical
// manifest digest) before this loader may be treated as a trust boundary.

// A successful verifier returns an ownership token for the exact immutable
// artifact it verified.  The token must pin load_path() against replacement
// (for example by holding a content-addressed store lease) until destruction.
// The minimum identity metadata above comes from the verified package and is
// compared with the binary manifest before any plugin callback runs.  R1 must
// extend that reconciliation to the complete package contract described above.
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
  // it must not recursively start another management operation.  R0 still
  // requires a precomputed catalog authorization snapshot, while R1 replaces
  // that temporary convention with an activation permit.
  virtual int verify_and_pin(
      const std::string &canonical_path,
      std::unique_ptr<ObPluginVerifiedArtifact> &artifact,
      std::string &error) const = 0;
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
// finish() is called exactly once, outside the loader mutex, after the runtime
// attempt.  It must persist the actual runtime result (including QUIESCING or
// STOPPED recovery state); a successful stop cannot be made active again merely
// because catalog finalization fails.  Destroying an unfinished permit must
// leave a durable recovery marker and release the logical exclusion.  finish is
// noexcept and idempotence/replay must be keyed by the permit's durable intent.
class ObPluginDisablePermit
{
public:
  virtual ~ObPluginDisablePermit() noexcept = default;
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
  ObPluginState state_;
  int64_t lease_count_;
  std::string last_error_;
};

// A process-local R0 experimental loader.  It intentionally loads one plugin
// at a time and rejects an unresolved mandatory dependency; callers which need
// batch loads should order a catalog/DAG and invoke load() in that order.  It is
// excluded from the default build until the R1 catalog activation permit is
// implemented; do not expose load() as a production management entry point.
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
           const std::shared_ptr<const ObPluginDisableGuard> &disable_guard,
           const std::shared_ptr<ObPluginServiceRegistry> &registry);
  bool is_initialized() const;

  // relative_path is an untrusted catalog-relative path.  Absolute paths,
  // traversal, symlink escapes and non-regular files are rejected.  In R0 the
  // verifier must also carry a pre-authorized catalog allow-list decision.
  // A stopped/resident identity cannot be loaded again before process exit.
  int load(const std::string &relative_path, uint64_t *loaded_generation = nullptr);

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

  int get_status(const std::string &plugin_id, ObPluginStatusSnapshot &status) const;
  int list_status(std::vector<ObPluginStatusSnapshot> &statuses) const;
  std::string last_error() const;
  std::string trusted_directory() const;

private:
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
