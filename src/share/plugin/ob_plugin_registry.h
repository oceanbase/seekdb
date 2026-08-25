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

#ifndef OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_REGISTRY_H_
#define OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_REGISTRY_H_

#include <condition_variable>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "seekdb/plugin/extension_spi.h"
#include "seekdb/plugin/sql_catalog.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

// Runtime state belongs to one loaded generation, not to the package name.
// Side-by-side upgrade may therefore drain generation N while N+1 is ACTIVE.
enum class ObPluginState : uint8_t
{
  DISCOVERED = 0,
  VALIDATED,
  LOADED,
  INITIALIZING,
  ACTIVE,
  QUIESCING,
  STOPPED,
  FAILED,
  BLOCKED
};

class ObPluginServiceRegistry;
class ObPluginRegistration;
class ObPluginActivationCandidate;
class ObPluginPreparedActivation;
class ObPluginLoader;
class ObPluginExtensionLease;

// Capability token for the one transition that must never be exposed as an
// ordinary runtime operation.  Only ObPluginLoader can construct it, and only
// while executing its terminal process-exit shutdown path.
class ObPluginTerminalStopAuthority final
{
public:
  ObPluginTerminalStopAuthority(const ObPluginTerminalStopAuthority &) = delete;
  ObPluginTerminalStopAuthority &operator=(
      const ObPluginTerminalStopAuthority &) = delete;

private:
  friend class ObPluginLoader;
  ObPluginTerminalStopAuthority() = default;
};

class ObPluginGeneration
{
public:
  ObPluginGeneration(const std::string &plugin_id, const uint64_t generation);
  ~ObPluginGeneration() = default;

  ObPluginGeneration(const ObPluginGeneration &) = delete;
  ObPluginGeneration &operator=(const ObPluginGeneration &) = delete;

  const std::string &plugin_id() const { return plugin_id_; }
  uint64_t generation() const { return generation_; }
  ObPluginState state() const;
  int64_t lease_count() const;

  // Lifecycle transitions are deliberately checked in one place.  Callers
  // cannot skip validation, publish a stopped generation, or reactivate a
  // generation which has started quiescing.
  int transition_to(const ObPluginState next);
  int wait_for_drain(const int64_t timeout_us);

private:
  friend class ObPluginServiceRegistry;
  friend class ObPluginLease;
  friend class ObPluginExtensionLease;

  bool try_acquire_lease();
  void release_lease();
  int reserve_activation();
  void abort_reserved_activation();
  void promote_reserved_activation();
  int begin_quiesce();
  int terminal_mark_stopped();

private:
  const std::string plugin_id_;
  const uint64_t generation_;
  mutable std::mutex mutex_;
  std::condition_variable drained_cv_;
  ObPluginState state_;
  int64_t lease_count_;
  bool activation_reserved_;
};

// A lease is the only supported way to invoke a service implementation.  It
// pins the owning generation until destruction.  In particular, copying is
// forbidden so a refcount cannot be silently duplicated or released twice.
class ObPluginLease
{
public:
  ObPluginLease();
  ~ObPluginLease();

  ObPluginLease(const ObPluginLease &) = delete;
  ObPluginLease &operator=(const ObPluginLease &) = delete;
  ObPluginLease(ObPluginLease &&other) noexcept;
  ObPluginLease &operator=(ObPluginLease &&other) noexcept;

  bool is_valid() const { return nullptr != service_ && nullptr != owner_; }
  const void *service() const { return service_; }
  uint32_t service_minor() const { return service_minor_; }
  uint32_t service_patch() const { return service_patch_; }
  uint64_t service_capabilities() const { return service_capabilities_; }
  const char *owner_plugin_id() const;
  uint64_t owner_generation() const;
  void reset();

private:
  friend class ObPluginServiceRegistry;
  ObPluginLease(const std::shared_ptr<ObPluginGeneration> &owner,
                const void *service,
                const uint32_t service_minor,
                const uint32_t service_patch,
                const uint64_t service_capabilities);

private:
  std::shared_ptr<ObPluginGeneration> owner_;
  const void *service_;
  uint32_t service_minor_;
  uint32_t service_patch_;
  uint64_t service_capabilities_;
};

struct ObPluginServiceSpec
{
  ObPluginServiceSpec();
  ObPluginServiceSpec(const std::string &name,
                      const uint32_t abi_major,
                      const uint32_t abi_minor,
                      const void *service);
  ObPluginServiceSpec(const std::string &name,
                      const uint32_t abi_major,
                      const uint32_t abi_minor,
                      const uint32_t abi_patch,
                      const uint64_t capabilities,
                      const void *service);

  std::string name_;
  uint32_t abi_major_;
  uint32_t abi_minor_;
  uint32_t abi_patch_;
  uint64_t capabilities_;
  const void *service_;
};

struct ObPluginServiceInfo
{
  std::string name_;
  uint32_t abi_major_;
  uint32_t abi_minor_;
  uint32_t abi_patch_;
  uint64_t capabilities_;
  std::string owner_plugin_id_;
  uint64_t owner_generation_;
};

struct ObPluginImplementationSpec
{
  ObPluginImplementationSpec();

  std::string service_id_;
  seekdb_plugin_version_range_t version_range_;
  uint64_t required_capabilities_;
};

struct PluginSqlColumn
{
  std::string sql_name_;
  std::string type_id_;
  bool nullable_ = true;
};

// Pointer-free, host-owned copy of one public extension descriptor.  Fields
// which do not apply to kind_ remain zero/empty.  Keeping the normalized copy
// independent from the DSO makes catalog inspection safe while a generation
// is quiescing; the lease still pins executable implementation services.
struct ObPluginExtensionSpec
{
  ObPluginExtensionSpec();

  seekdb_plugin_extension_kind_t kind_;
  std::string object_id_;
  std::string sql_name_;
  std::string physical_format_id_;
  std::string source_type_id_;
  std::string target_type_id_;
  std::string static_result_type_id_;
  std::vector<std::string> argument_type_ids_;
  std::vector<PluginSqlColumn> result_columns_;
  std::string hook_point_;
  std::string catalog_object_kind_;
  std::string schema_name_;
  std::string definition_digest_;
  uint32_t physical_format_version_;
  uint32_t minimum_arity_;
  uint32_t maximum_arity_;
  uint32_t signature_flags_;
  seekdb_plugin_cast_context_t cast_context_;
  uint32_t cost_;
  int32_t priority_;
  uint64_t flags_;
  ObPluginImplementationSpec implementation_;
};

struct ObPluginExtensionInfo
{
  ObPluginExtensionSpec spec_;
  std::string owner_plugin_id_;
  uint64_t owner_generation_;
};

// Plans, prepared statements, iterators and asynchronous tasks retain this
// lease for as long as they depend on an extension object.  It shares the same
// generation counter as executable service leases, so stop cannot overtake
// either metadata or code consumers.
class ObPluginExtensionLease
{
public:
  ObPluginExtensionLease();
  ~ObPluginExtensionLease();

  ObPluginExtensionLease(const ObPluginExtensionLease &) = delete;
  ObPluginExtensionLease &operator=(const ObPluginExtensionLease &) = delete;
  ObPluginExtensionLease(ObPluginExtensionLease &&other) noexcept;
  ObPluginExtensionLease &operator=(ObPluginExtensionLease &&other) noexcept;

  bool is_valid() const { return nullptr != info_ && nullptr != owner_; }
  const ObPluginExtensionInfo *info() const { return info_.get(); }
  const char *owner_plugin_id() const;
  uint64_t owner_generation() const;
  void reset();

private:
  friend class ObPluginServiceRegistry;
  ObPluginExtensionLease(
      const std::shared_ptr<ObPluginGeneration> &owner,
      const std::shared_ptr<const ObPluginExtensionInfo> &info);

private:
  std::shared_ptr<ObPluginGeneration> owner_;
  std::shared_ptr<const ObPluginExtensionInfo> info_;
};

// Registration is a staging transaction.  Staged entries are invisible until
// commit(), or may be consumed by prepare() for catalog work followed by a
// no-fail candidate promotion.  Both paths validate the complete set before
// publishing any entry.
class ObPluginRegistration
{
public:
  ObPluginRegistration();
  ~ObPluginRegistration();

  ObPluginRegistration(const ObPluginRegistration &) = delete;
  ObPluginRegistration &operator=(const ObPluginRegistration &) = delete;

  int add_service(const char *name,
                  const uint32_t abi_major,
                  const uint32_t abi_minor,
                  const void *service);
  int add_service(const char *name,
                  const uint32_t abi_major,
                  const uint32_t abi_minor,
                  const uint32_t abi_patch,
                  const uint64_t capabilities,
                  const void *service);
  int add_extension(const ObPluginExtensionSpec &extension);
  // Materializes a complete, validated next registry snapshot.  All copying,
  // allocation and implementation binding happens before this call returns;
  // the resulting candidate remains invisible until promote().  A successful
  // prepare consumes this registration.
  int prepare(ObPluginActivationCandidate &candidate);
  int commit();
  void rollback();
  bool is_open() const { return open_; }

private:
  friend class ObPluginServiceRegistry;
  void open(ObPluginServiceRegistry *registry,
            const std::shared_ptr<ObPluginGeneration> &owner);
  void close();

private:
  ObPluginServiceRegistry *registry_;
  std::shared_ptr<ObPluginGeneration> owner_;
  std::vector<ObPluginServiceSpec> staged_;
  std::vector<ObPluginExtensionSpec> staged_extensions_;
  bool open_;
};

// A prepared activation permit.  The candidate owns a complete immutable next
// registry snapshot but is not visible to list/find/acquire until promote().
// Destruction and abort() discard it without changing the registry epoch or
// the generation lifecycle.  prepare() installs one global hidden reservation
// which blocks competing registry mutations.  Thus promote() of a legal token
// cannot encounter a late conflict after catalog activation has succeeded.
// The registry normally outlives the candidate; defensive registry destruction
// disarms an outstanding token, after which is_prepared() is false and only
// abort()/destruction is valid.
class ObPluginActivationCandidate
{
public:
  ObPluginActivationCandidate();
  ~ObPluginActivationCandidate();

  ObPluginActivationCandidate(const ObPluginActivationCandidate &) = delete;
  ObPluginActivationCandidate &operator=(
      const ObPluginActivationCandidate &) = delete;

  // promote() is valid exactly once for a prepared token.  The global
  // reservation makes this a no-fail operation; misuse is a programming
  // invariant violation and terminates instead of exposing a business-level
  // rollback path after catalog activation.
  void promote() noexcept;
  void abort() noexcept;
  bool is_prepared() const noexcept;
  uint64_t base_epoch() const;
  // Host-owned, allocation-free views of this generation's contribution.
  // They are stable until promote(), abort(), or candidate destruction.
  const std::vector<ObPluginServiceInfo> &contributed_services() const noexcept;
  const std::vector<ObPluginExtensionInfo> &
      contributed_extensions() const noexcept;

private:
  friend class ObPluginServiceRegistry;
  std::unique_ptr<ObPluginPreparedActivation> prepared_;
};

// Thread-safe, process-wide registry for versioned service interfaces.
//
// Locking invariant: registry mutex is acquired before a generation mutex.
// Lease destruction only acquires the generation mutex, so there is no inverse
// lock order.  A successful quiesce both flips the generation state and removes
// all of its entries while holding registry mutex; consequently an acquire is
// either fully before quiesce (and pinned) or fully after it (and rejected).
class ObPluginServiceRegistry
{
public:
  ObPluginServiceRegistry();
  ~ObPluginServiceRegistry();

  ObPluginServiceRegistry(const ObPluginServiceRegistry &) = delete;
  ObPluginServiceRegistry &operator=(const ObPluginServiceRegistry &) = delete;

  int begin_registration(const std::shared_ptr<ObPluginGeneration> &owner,
                         ObPluginRegistration &registration);

  // required_minor is a minimum.  Providers may append ABI fields in a newer
  // minor version but must retain the semantics of the same major version.
  int acquire(const char *name,
              const uint32_t abi_major,
              const uint32_t required_minor,
              ObPluginLease &lease);
  int acquire(const char *name,
              const uint32_t abi_major,
              const uint32_t required_minor,
              const uint32_t required_patch,
              const uint64_t required_capabilities,
              ObPluginLease &lease);

  // Atomically resolves an executable extension and its implementation
  // service.  This closes the acquire-extension/quiesce/acquire-service race
  // that would otherwise make a valid descriptor unusable between two calls.
  // expected is a previous name-resolution result; owner/generation matching
  // prevents stale metadata from silently binding a replacement generation.
  int acquire_extension_with_implementation(
      const ObPluginExtensionInfo &expected,
      ObPluginExtensionLease &extension_lease,
      ObPluginLease &implementation_lease);

  // Logical unload: stop new acquisitions and unpublish every service owned by
  // this generation atomically.  Existing leases remain valid and are drained
  // before stop/deinit.  The DSO must not be dlclose()d by this operation.
  int quiesce(const std::shared_ptr<ObPluginGeneration> &owner);
  int mark_stopped(const std::shared_ptr<ObPluginGeneration> &owner);
  int mark_stopped(
      const std::shared_ptr<ObPluginGeneration> &owner,
      const ObPluginTerminalStopAuthority &terminal_authority);

  int list_services(std::vector<ObPluginServiceInfo> &services) const;
  int list_extensions(std::vector<ObPluginExtensionInfo> &extensions) const;
  // Returns host-owned binding candidates and the epoch observed atomically.
  // Copies do not pin code.  A chosen result must be passed unchanged to
  // acquire_extension_with_implementation(), which rejects stale generations.
  int find_extensions_by_sql_name(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      std::vector<ObPluginExtensionInfo> &extensions,
      uint64_t &registry_epoch) const;
  // SQL-facing overload resolution. Exact type matches win over implicit
  // plugin casts; legacy untyped descriptors remain eligible as a fallback.
  int resolve_sql_extension(
      seekdb_plugin_extension_kind_t kind,
      const char *sql_name,
      const char *const *argument_type_ids,
      uint32_t argument_count,
      ObPluginExtensionInfo &extension,
      uint64_t &registry_epoch) const;
  // A provider cast is eligible when its declared context is at least as
  // permissive as requested_context (IMPLICIT > ASSIGNMENT > EXPLICIT).
  // Eligible candidates are returned by ascending cost, then object_id.
  int find_casts(const char *source_type_id,
                 const char *target_type_id,
                 seekdb_plugin_cast_context_t requested_context,
                 std::vector<ObPluginExtensionInfo> &extensions,
                 uint64_t &registry_epoch) const;
  int find_hooks(seekdb_plugin_extension_kind_t kind,
                 const char *hook_point,
                 std::vector<ObPluginExtensionInfo> &extensions,
                 uint64_t &registry_epoch) const;
  int find_catalog_objects(const char *object_kind,
                           const char *schema_name,
                           const char *sql_name,
                           std::vector<ObPluginExtensionInfo> &extensions,
                           uint64_t &registry_epoch) const;
  int64_t service_count() const;
  int64_t extension_count() const;
  uint64_t registry_epoch() const;

private:
  friend class ObPluginRegistration;
  friend class ObPluginActivationCandidate;
  friend class ObPluginPreparedActivation;

  struct ServiceKey
  {
    ServiceKey();
    ServiceKey(const std::string &name, const uint32_t abi_major);
    bool operator<(const ServiceKey &other) const;

    std::string name_;
    uint32_t abi_major_;
  };

  struct ServiceEntry
  {
    ServiceEntry();
    ServiceEntry(const ObPluginServiceSpec &spec,
                 const std::shared_ptr<ObPluginGeneration> &owner);

    uint32_t abi_minor_;
    uint32_t abi_patch_;
    uint64_t capabilities_;
    const void *service_;
    std::shared_ptr<ObPluginGeneration> owner_;
  };

  struct ExtensionKey
  {
    ExtensionKey();
    ExtensionKey(seekdb_plugin_extension_kind_t kind,
                 const std::string &object_id);
    bool operator<(const ExtensionKey &other) const;

    seekdb_plugin_extension_kind_t kind_;
    std::string object_id_;
  };

  struct ExtensionEntry
  {
    ExtensionEntry();
    ExtensionEntry(const ObPluginExtensionSpec &spec,
                   const std::shared_ptr<ObPluginGeneration> &owner);

    std::shared_ptr<const ObPluginExtensionInfo> info_;
    std::shared_ptr<ObPluginGeneration> owner_;
  };

  struct RegistrySnapshot;

  int prepare_registration(ObPluginRegistration &registration,
                           ObPluginActivationCandidate &candidate);
  void promote_candidate(ObPluginActivationCandidate &candidate) noexcept;
  void abort_candidate(ObPluginActivationCandidate &candidate) noexcept;
  int commit_registration(ObPluginRegistration &registration);
  bool candidate_conflicts_locked(
      const ObPluginPreparedActivation &candidate) const;

private:
  mutable std::mutex mutex_;
  // Snapshots are immutable after publication.  prepare() may safely copy a
  // captured snapshot without holding mutex_; every mutation publishes a new
  // prebuilt snapshot through a noexcept shared_ptr swap.
  std::shared_ptr<const RegistrySnapshot> live_snapshot_;
  uint64_t registry_epoch_;
  // Non-owning pointer into the sole prepared candidate.  Protected by
  // mutex_.  Its owner must call promote() or abort() before another registry
  // mutation may proceed.
  ObPluginPreparedActivation *activation_reservation_;
};

} // namespace plugin
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_REGISTRY_H_
