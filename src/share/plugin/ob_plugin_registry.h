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
class ObPluginLoader;

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

  bool try_acquire_lease();
  void release_lease();
  int begin_quiesce();
  int terminal_mark_stopped();

private:
  const std::string plugin_id_;
  const uint64_t generation_;
  mutable std::mutex mutex_;
  std::condition_variable drained_cv_;
  ObPluginState state_;
  int64_t lease_count_;
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

// Registration is a staging transaction.  Staged entries are invisible until
// commit(), and commit validates the complete set before publishing any entry.
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
  bool open_;
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
  ObPluginServiceRegistry() = default;
  ~ObPluginServiceRegistry() = default;

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

  // Logical unload: stop new acquisitions and unpublish every service owned by
  // this generation atomically.  Existing leases remain valid and are drained
  // before stop/deinit.  The DSO must not be dlclose()d by this operation.
  int quiesce(const std::shared_ptr<ObPluginGeneration> &owner);
  int mark_stopped(const std::shared_ptr<ObPluginGeneration> &owner);
  int mark_stopped(
      const std::shared_ptr<ObPluginGeneration> &owner,
      const ObPluginTerminalStopAuthority &terminal_authority);

  int list_services(std::vector<ObPluginServiceInfo> &services) const;
  int64_t service_count() const;

private:
  friend class ObPluginRegistration;

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

  int commit_registration(ObPluginRegistration &registration);

private:
  mutable std::mutex mutex_;
  std::map<ServiceKey, ServiceEntry> services_;
};

} // namespace plugin
} // namespace share
} // namespace oceanbase

#endif // OCEANBASE_SHARE_PLUGIN_OB_PLUGIN_REGISTRY_H_
