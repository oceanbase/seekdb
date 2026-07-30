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

#include "share/plugin/ob_plugin_registry.h"

#include <chrono>
#include <cstring>
#include <new>
#include <set>

#include "lib/ob_errno.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

bool is_valid_transition(const ObPluginState from, const ObPluginState to)
{
  bool valid = false;
  switch (from) {
    case ObPluginState::DISCOVERED:
      valid = ObPluginState::VALIDATED == to || ObPluginState::FAILED == to;
      break;
    case ObPluginState::VALIDATED:
      valid = ObPluginState::LOADED == to || ObPluginState::FAILED == to;
      break;
    case ObPluginState::LOADED:
      valid = ObPluginState::INITIALIZING == to || ObPluginState::FAILED == to;
      break;
    case ObPluginState::INITIALIZING:
      valid = ObPluginState::ACTIVE == to || ObPluginState::FAILED == to ||
              ObPluginState::BLOCKED == to;
      break;
    case ObPluginState::ACTIVE:
      valid = ObPluginState::QUIESCING == to || ObPluginState::FAILED == to ||
              ObPluginState::BLOCKED == to;
      break;
    case ObPluginState::QUIESCING:
      valid = ObPluginState::STOPPED == to || ObPluginState::FAILED == to ||
              ObPluginState::BLOCKED == to;
      break;
    case ObPluginState::FAILED:
      valid = ObPluginState::QUIESCING == to || ObPluginState::STOPPED == to;
      break;
    case ObPluginState::BLOCKED:
      // BLOCKED means a plugin callback may still be live.  It can converge
      // only through terminal_mark_stopped(), whose authority is held by the
      // process-exit loader path.
      valid = false;
      break;
    case ObPluginState::STOPPED:
      valid = false;
      break;
  }
  return valid;
}

bool is_valid_service_name(const char *name)
{
  static const size_t MAX_SERVICE_NAME_LENGTH = 255;
  bool valid = nullptr != name && '\0' != name[0];
  if (valid) {
    const size_t length = std::strlen(name);
    valid = length <= MAX_SERVICE_NAME_LENGTH;
    for (size_t i = 0; valid && i < length; ++i) {
      const char c = name[i];
      valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
          || '.' == c || '_' == c || '-' == c;
    }
  }
  return valid;
}

} // namespace

ObPluginGeneration::ObPluginGeneration(const std::string &plugin_id,
                                       const uint64_t generation)
    : plugin_id_(plugin_id),
      generation_(generation),
      mutex_(),
      drained_cv_(),
      state_(ObPluginState::DISCOVERED),
      lease_count_(0)
{
}

ObPluginState ObPluginGeneration::state() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  return state_;
}

int64_t ObPluginGeneration::lease_count() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  return lease_count_;
}

int ObPluginGeneration::transition_to(const ObPluginState next)
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(mutex_);
  if (!is_valid_transition(state_, next)) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    state_ = next;
  }
  return ret;
}

bool ObPluginGeneration::try_acquire_lease()
{
  bool acquired = false;
  std::lock_guard<std::mutex> guard(mutex_);
  if (ObPluginState::ACTIVE == state_) {
    ++lease_count_;
    acquired = true;
  }
  return acquired;
}

void ObPluginGeneration::release_lease()
{
  std::lock_guard<std::mutex> guard(mutex_);
  if (lease_count_ > 0) {
    --lease_count_;
    if (0 == lease_count_) {
      drained_cv_.notify_all();
    }
  }
}

int ObPluginGeneration::begin_quiesce()
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(mutex_);
  if (ObPluginState::ACTIVE == state_ || ObPluginState::FAILED == state_) {
    state_ = ObPluginState::QUIESCING;
  } else if (ObPluginState::QUIESCING != state_) {
    ret = OB_STATE_NOT_MATCH;
  }
  return ret;
}

int ObPluginGeneration::wait_for_drain(const int64_t timeout_us)
{
  int ret = OB_SUCCESS;
  if (timeout_us < 0) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    std::unique_lock<std::mutex> guard(mutex_);
    if (ObPluginState::QUIESCING != state_ && ObPluginState::FAILED != state_) {
      ret = OB_STATE_NOT_MATCH;
    } else if (lease_count_ > 0) {
      const bool drained = drained_cv_.wait_for(
          guard, std::chrono::microseconds(timeout_us), [this]() { return 0 == lease_count_; });
      if (!drained) {
        ret = OB_TIMEOUT;
      }
    }
  }
  return ret;
}

int ObPluginGeneration::terminal_mark_stopped()
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(mutex_);
  if (ObPluginState::BLOCKED != state_) {
    ret = OB_STATE_NOT_MATCH;
  } else if (0 != lease_count_) {
    ret = OB_EAGAIN;
  } else {
    state_ = ObPluginState::STOPPED;
  }
  return ret;
}

ObPluginLease::ObPluginLease()
    : owner_(), service_(nullptr), service_minor_(0), service_patch_(0),
      service_capabilities_(0)
{
}

ObPluginLease::ObPluginLease(const std::shared_ptr<ObPluginGeneration> &owner,
                             const void *service,
                             const uint32_t service_minor,
                             const uint32_t service_patch,
                             const uint64_t service_capabilities)
    : owner_(owner), service_(service), service_minor_(service_minor),
      service_patch_(service_patch), service_capabilities_(service_capabilities)
{
}

ObPluginLease::~ObPluginLease()
{
  reset();
}

ObPluginLease::ObPluginLease(ObPluginLease &&other) noexcept
    : owner_(std::move(other.owner_)),
      service_(other.service_),
      service_minor_(other.service_minor_),
      service_patch_(other.service_patch_),
      service_capabilities_(other.service_capabilities_)
{
  other.service_ = nullptr;
  other.service_minor_ = 0;
  other.service_patch_ = 0;
  other.service_capabilities_ = 0;
}

ObPluginLease &ObPluginLease::operator=(ObPluginLease &&other) noexcept
{
  if (this != &other) {
    reset();
    owner_ = std::move(other.owner_);
    service_ = other.service_;
    service_minor_ = other.service_minor_;
    service_patch_ = other.service_patch_;
    service_capabilities_ = other.service_capabilities_;
    other.service_ = nullptr;
    other.service_minor_ = 0;
    other.service_patch_ = 0;
    other.service_capabilities_ = 0;
  }
  return *this;
}

const char *ObPluginLease::owner_plugin_id() const
{
  return nullptr == owner_ ? nullptr : owner_->plugin_id().c_str();
}

uint64_t ObPluginLease::owner_generation() const
{
  return nullptr == owner_ ? 0 : owner_->generation();
}

void ObPluginLease::reset()
{
  if (nullptr != owner_) {
    owner_->release_lease();
    owner_.reset();
  }
  service_ = nullptr;
  service_minor_ = 0;
  service_patch_ = 0;
  service_capabilities_ = 0;
}

ObPluginServiceSpec::ObPluginServiceSpec()
    : name_(), abi_major_(0), abi_minor_(0), abi_patch_(0), capabilities_(0),
      service_(nullptr)
{
}

ObPluginServiceSpec::ObPluginServiceSpec(const std::string &name,
                                         const uint32_t abi_major,
                                         const uint32_t abi_minor,
                                         const void *service)
    : ObPluginServiceSpec(name, abi_major, abi_minor, 0, 0, service)
{
}

ObPluginServiceSpec::ObPluginServiceSpec(const std::string &name,
                                         const uint32_t abi_major,
                                         const uint32_t abi_minor,
                                         const uint32_t abi_patch,
                                         const uint64_t capabilities,
                                         const void *service)
    : name_(name), abi_major_(abi_major), abi_minor_(abi_minor), abi_patch_(abi_patch),
      capabilities_(capabilities), service_(service)
{
}

ObPluginRegistration::ObPluginRegistration()
    : registry_(nullptr), owner_(), staged_(), open_(false)
{
}

ObPluginRegistration::~ObPluginRegistration()
{
  rollback();
}

void ObPluginRegistration::open(
    ObPluginServiceRegistry *registry,
    const std::shared_ptr<ObPluginGeneration> &owner)
{
  registry_ = registry;
  owner_ = owner;
  staged_.clear();
  open_ = true;
}

void ObPluginRegistration::close()
{
  registry_ = nullptr;
  owner_.reset();
  staged_.clear();
  open_ = false;
}

int ObPluginRegistration::add_service(const char *name,
                                      const uint32_t abi_major,
                                      const uint32_t abi_minor,
                                      const void *service)
{
  return add_service(name, abi_major, abi_minor, 0, 0, service);
}

int ObPluginRegistration::add_service(const char *name,
                                      const uint32_t abi_major,
                                      const uint32_t abi_minor,
                                      const uint32_t abi_patch,
                                      const uint64_t capabilities,
                                      const void *service)
{
  int ret = OB_SUCCESS;
  if (!open_) {
    ret = OB_NOT_INIT;
  } else if (!is_valid_service_name(name) || 0 == abi_major || nullptr == service) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    for (const ObPluginServiceSpec &item : staged_) {
      if (item.name_ == name && item.abi_major_ == abi_major) {
        ret = OB_ENTRY_EXIST;
        break;
      }
    }
    if (OB_SUCCESS == ret) {
      try {
        staged_.push_back(ObPluginServiceSpec(name, abi_major, abi_minor, abi_patch,
                                              capabilities, service));
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObPluginRegistration::commit()
{
  return open_ && nullptr != registry_ ? registry_->commit_registration(*this) : OB_NOT_INIT;
}

void ObPluginRegistration::rollback()
{
  if (open_) {
    close();
  }
}

ObPluginServiceRegistry::ServiceKey::ServiceKey()
    : name_(), abi_major_(0)
{
}

ObPluginServiceRegistry::ServiceKey::ServiceKey(const std::string &name,
                                                const uint32_t abi_major)
    : name_(name), abi_major_(abi_major)
{
}

bool ObPluginServiceRegistry::ServiceKey::operator<(const ServiceKey &other) const
{
  return name_ < other.name_ || (name_ == other.name_ && abi_major_ < other.abi_major_);
}

ObPluginServiceRegistry::ServiceEntry::ServiceEntry()
    : abi_minor_(0), abi_patch_(0), capabilities_(0), service_(nullptr), owner_()
{
}

ObPluginServiceRegistry::ServiceEntry::ServiceEntry(
    const ObPluginServiceSpec &spec,
    const std::shared_ptr<ObPluginGeneration> &owner)
    : abi_minor_(spec.abi_minor_), abi_patch_(spec.abi_patch_),
      capabilities_(spec.capabilities_), service_(spec.service_), owner_(owner)
{
}

int ObPluginServiceRegistry::begin_registration(
    const std::shared_ptr<ObPluginGeneration> &owner,
    ObPluginRegistration &registration)
{
  int ret = OB_SUCCESS;
  if (nullptr == owner || owner->plugin_id().empty() || 0 == owner->generation()) {
    ret = OB_INVALID_ARGUMENT;
  } else if (registration.is_open()) {
    ret = OB_INIT_TWICE;
  } else {
    const ObPluginState state = owner->state();
    if (ObPluginState::LOADED != state && ObPluginState::INITIALIZING != state) {
      ret = OB_STATE_NOT_MATCH;
    } else {
      registration.open(this, owner);
    }
  }
  return ret;
}

int ObPluginServiceRegistry::commit_registration(ObPluginRegistration &registration)
{
  int ret = OB_SUCCESS;
  if (!registration.open_ || registration.registry_ != this || nullptr == registration.owner_) {
    ret = OB_INVALID_ARGUMENT;
  } else if (ObPluginState::INITIALIZING != registration.owner_->state()) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    std::lock_guard<std::mutex> guard(mutex_);
    // Build the complete next snapshot off to the side.  Allocation failure,
    // duplicate validation, or an unexpected exception therefore leaves both
    // the live registry and the owner state untouched.
    std::map<ServiceKey, ServiceEntry> candidate;
    try {
      candidate = services_;
      for (const ObPluginServiceSpec &spec : registration.staged_) {
        const auto inserted = candidate.insert(
            std::make_pair(ServiceKey(spec.name_, spec.abi_major_),
                           ServiceEntry(spec, registration.owner_)));
        if (!inserted.second) {
          ret = OB_ENTRY_EXIST;
          break;
        }
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
    if (OB_SUCCESS == ret) {
      ret = registration.owner_->transition_to(ObPluginState::ACTIVE);
    }
    if (OB_SUCCESS == ret) {
      services_.swap(candidate);
    }
  }

  if (OB_SUCCESS == ret) {
    registration.close();
  }
  return ret;
}

int ObPluginServiceRegistry::acquire(const char *name,
                                     const uint32_t abi_major,
                                     const uint32_t required_minor,
                                     ObPluginLease &lease)
{
  return acquire(name, abi_major, required_minor, 0, 0, lease);
}

int ObPluginServiceRegistry::acquire(const char *name,
                                     const uint32_t abi_major,
                                     const uint32_t required_minor,
                                     const uint32_t required_patch,
                                     const uint64_t required_capabilities,
                                     ObPluginLease &lease)
{
  int ret = OB_SUCCESS;
  if (!is_valid_service_name(name) || 0 == abi_major || lease.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    try {
      std::lock_guard<std::mutex> guard(mutex_);
      const auto it = services_.find(ServiceKey(name, abi_major));
      if (services_.end() == it || it->second.abi_minor_ < required_minor ||
          (it->second.abi_minor_ == required_minor && it->second.abi_patch_ < required_patch) ||
          (it->second.capabilities_ & required_capabilities) != required_capabilities) {
        ret = OB_ENTRY_NOT_EXIST;
      } else if (!it->second.owner_->try_acquire_lease()) {
        ret = OB_STATE_NOT_MATCH;
      } else {
        lease = ObPluginLease(it->second.owner_, it->second.service_, it->second.abi_minor_,
                              it->second.abi_patch_, it->second.capabilities_);
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObPluginServiceRegistry::quiesce(const std::shared_ptr<ObPluginGeneration> &owner)
{
  int ret = OB_SUCCESS;
  if (nullptr == owner) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    std::lock_guard<std::mutex> guard(mutex_);
    if (OB_SUCCESS != (ret = owner->begin_quiesce())) {
      // State remains unchanged and services stay visible on failure.
    } else {
      for (auto it = services_.begin(); it != services_.end();) {
        if (it->second.owner_ == owner) {
          it = services_.erase(it);
        } else {
          ++it;
        }
      }
    }
  }
  return ret;
}

int ObPluginServiceRegistry::mark_stopped(
    const std::shared_ptr<ObPluginGeneration> &owner)
{
  int ret = OB_SUCCESS;
  if (nullptr == owner) {
    ret = OB_INVALID_ARGUMENT;
  } else if (0 != owner->lease_count()) {
    ret = OB_EAGAIN;
  } else {
    ret = owner->transition_to(ObPluginState::STOPPED);
  }
  return ret;
}

int ObPluginServiceRegistry::mark_stopped(
    const std::shared_ptr<ObPluginGeneration> &owner,
    const ObPluginTerminalStopAuthority &terminal_authority)
{
  (void)terminal_authority;
  return nullptr == owner ? OB_INVALID_ARGUMENT : owner->terminal_mark_stopped();
}

int ObPluginServiceRegistry::list_services(std::vector<ObPluginServiceInfo> &services) const
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    std::vector<ObPluginServiceInfo> candidate;
    candidate.reserve(services_.size());
    for (const auto &item : services_) {
      ObPluginServiceInfo info;
      info.name_ = item.first.name_;
      info.abi_major_ = item.first.abi_major_;
      info.abi_minor_ = item.second.abi_minor_;
      info.abi_patch_ = item.second.abi_patch_;
      info.capabilities_ = item.second.capabilities_;
      info.owner_plugin_id_ = item.second.owner_->plugin_id();
      info.owner_generation_ = item.second.owner_->generation();
      candidate.push_back(info);
    }
    services.swap(candidate);
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int64_t ObPluginServiceRegistry::service_count() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  return static_cast<int64_t>(services_.size());
}

} // namespace plugin
} // namespace share
} // namespace oceanbase
