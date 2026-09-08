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

#include <algorithm>
#include <chrono>
#include <cstring>
#include <exception>
#include <limits>
#include <new>
#include <set>

#include "lib/ob_errno.h"
#include "seekdb/plugin/execution_spi.h"

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
  size_t length = 0;
  if (valid) {
    while (length <= MAX_SERVICE_NAME_LENGTH && '\0' != name[length]) {
      ++length;
    }
    valid = length <= MAX_SERVICE_NAME_LENGTH;
    for (size_t i = 0; valid && i < length; ++i) {
      const char c = name[i];
      valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9')
          || '.' == c || '_' == c || '-' == c;
    }
  }
  return valid;
}

bool is_valid_service_name(const std::string &name)
{
  static const size_t MAX_SERVICE_NAME_LENGTH = 255;
  bool valid = !name.empty() && name.size() <= MAX_SERVICE_NAME_LENGTH;
  for (size_t i = 0; valid && i < name.size(); ++i) {
    const char c = name[i];
    valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
            '.' == c || '_' == c || '-' == c;
  }
  return valid;
}

bool has_only_known_service_capabilities(const uint64_t capabilities)
{
  static const uint64_t KNOWN_CAPABILITIES =
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
      SEEKDB_PLUGIN_CAPABILITY_MULTI_INSTANCE |
      SEEKDB_PLUGIN_CAPABILITY_SIDE_BY_SIDE_UPGRADE |
      SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA |
      SEEKDB_PLUGIN_CAPABILITY_TRANSACTIONAL_SERVICES |
      SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG;
  return 0 == (capabilities & ~KNOWN_CAPABILITIES);
}

bool has_only_runtime_implementation_capabilities(const uint64_t capabilities)
{
  return has_only_known_service_capabilities(capabilities) &&
         0 == (capabilities & SEEKDB_PLUGIN_CAPABILITY_EXTENSION_CATALOG);
}

bool is_valid_sql_name(const char *name, const bool allow_qualified)
{
  bool valid = nullptr != name && '\0' != name[0];
  size_t length = 0;
  while (valid && length <= 255 && '\0' != name[length]) {
    ++length;
  }
  valid = valid && length <= 255;
  for (size_t i = 0; valid && i < length; ++i) {
    const char c = name[i];
    if ('.' == c) {
      valid = allow_qualified && i > 0 && i + 1 < length &&
              '.' != name[i - 1];
    } else {
      valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              '_' == c || '$' == c;
    }
  }
  return valid;
}

bool is_valid_sql_name(const std::string &name, const bool allow_qualified)
{
  bool valid = !name.empty() && name.size() <= 255;
  for (size_t i = 0; valid && i < name.size(); ++i) {
    const char c = name[i];
    if ('.' == c) {
      valid = allow_qualified && i > 0 && i + 1 < name.size() &&
              '.' != name[i - 1];
    } else {
      valid = (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
              '_' == c || '$' == c;
    }
  }
  return valid;
}

bool is_valid_sql_name(const std::string &name)
{
  return is_valid_sql_name(name, true);
}

bool is_valid_sql_segment(const std::string &name)
{
  return is_valid_sql_name(name, false);
}

bool is_zero_version(const seekdb_plugin_semantic_version_t &version)
{
  return 0 == version.major && 0 == version.minor && 0 == version.patch;
}

bool version_less(const seekdb_plugin_semantic_version_t &left,
                  const seekdb_plugin_semantic_version_t &right)
{
  return left.major < right.major ||
         (left.major == right.major && left.minor < right.minor) ||
         (left.major == right.major && left.minor == right.minor &&
          left.patch < right.patch);
}

bool extension_version_in_range(
    const seekdb_plugin_semantic_version_t &version,
    const seekdb_plugin_version_range_t &range)
{
  return version.major == range.minimum_inclusive.major &&
         !version_less(version, range.minimum_inclusive) &&
         (is_zero_version(range.maximum_exclusive) ||
          version_less(version, range.maximum_exclusive));
}

bool has_valid_major_ceiling(const seekdb_plugin_version_range_t &range)
{
  const seekdb_plugin_semantic_version_t &maximum = range.maximum_exclusive;
  return is_zero_version(maximum) ||
         maximum.major == range.minimum_inclusive.major ||
         (range.minimum_inclusive.major <
              std::numeric_limits<uint32_t>::max() &&
          maximum.major == range.minimum_inclusive.major + 1 &&
          0 == maximum.minor && 0 == maximum.patch);
}

bool is_valid_implementation(const ObPluginImplementationSpec &implementation)
{
  bool reserved_zero = true;
  for (size_t i = 0; reserved_zero &&
                     i < sizeof(implementation.version_range_.reserved) /
                             sizeof(implementation.version_range_.reserved[0]);
       ++i) {
    reserved_zero = 0 == implementation.version_range_.reserved[i];
  }
  return is_valid_service_name(implementation.service_id_) &&
         has_only_runtime_implementation_capabilities(
             implementation.required_capabilities_) &&
         implementation.version_range_.struct_size ==
             sizeof(seekdb_plugin_version_range_t) &&
         implementation.version_range_.minimum_inclusive.major > 0 &&
         has_valid_major_ceiling(implementation.version_range_) &&
         (is_zero_version(implementation.version_range_.maximum_exclusive) ||
          version_less(implementation.version_range_.minimum_inclusive,
                       implementation.version_range_.maximum_exclusive)) &&
         reserved_zero;
}

bool is_valid_digest(const std::string &digest)
{
  static const char PREFIX[] = "sha256:";
  bool valid = digest.size() == sizeof(PREFIX) - 1 + 64 &&
               0 == digest.compare(0, sizeof(PREFIX) - 1, PREFIX);
  for (size_t i = sizeof(PREFIX) - 1; valid && i < digest.size(); ++i) {
    const char c = digest[i];
    valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
  }
  return valid;
}

bool is_valid_extension_spec(const ObPluginExtensionSpec &spec)
{
  static const uint64_t KNOWN_FLAGS =
      SEEKDB_PLUGIN_EXTENSION_FLAG_DETERMINISTIC |
      SEEKDB_PLUGIN_EXTENSION_FLAG_IMMUTABLE |
      SEEKDB_PLUGIN_EXTENSION_FLAG_NULL_PROPAGATING |
      SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
      SEEKDB_PLUGIN_EXTENSION_FLAG_PARALLEL_SAFE |
      SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG;
  bool valid = spec.kind_ >= SEEKDB_PLUGIN_EXTENSION_TYPE &&
               spec.kind_ <= SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION &&
               is_valid_service_name(spec.object_id_) &&
               0 == (spec.flags_ & ~KNOWN_FLAGS);
  switch (spec.kind_) {
    case SEEKDB_PLUGIN_EXTENSION_TYPE:
      valid = valid && is_valid_sql_name(spec.sql_name_) &&
              is_valid_service_name(spec.physical_format_id_) &&
              spec.physical_format_version_ > 0 &&
              is_valid_implementation(spec.implementation_);
      break;
    case SEEKDB_PLUGIN_EXTENSION_FUNCTION:
    case SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION:
      valid = valid && is_valid_sql_name(spec.sql_name_) &&
              spec.minimum_arity_ <= spec.maximum_arity_ &&
              spec.maximum_arity_ <= SEEKDB_PLUGIN_MAX_ARGUMENTS &&
              0 == (spec.signature_flags_ &
                    ~SEEKDB_PLUGIN_SIGNATURE_FLAG_VARIADIC) &&
              (spec.static_result_type_id_.empty() ||
               is_valid_service_name(spec.static_result_type_id_)) &&
              is_valid_implementation(spec.implementation_);
      if (valid && !spec.argument_type_ids_.empty()) {
        valid = spec.argument_type_ids_.size() <= spec.maximum_arity_ &&
                ((0 != (spec.signature_flags_ &
                        SEEKDB_PLUGIN_SIGNATURE_FLAG_VARIADIC)) ||
                 spec.argument_type_ids_.size() == spec.maximum_arity_);
        for (const std::string &type_id : spec.argument_type_ids_) {
          valid = valid && is_valid_service_name(type_id);
        }
      } else if (valid && spec.signature_flags_ != 0) {
        valid = false;
      }
      if (valid && SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION == spec.kind_) {
        valid = !spec.result_columns_.empty();
        std::set<std::string> column_names;
        for (const PluginSqlColumn &column : spec.result_columns_) {
          valid = valid && is_valid_sql_segment(column.sql_name_) &&
                  is_valid_service_name(column.type_id_) &&
                  column_names.insert(column.sql_name_).second;
        }
      } else if (valid && !spec.result_columns_.empty()) {
        valid = false;
      }
      break;
    case SEEKDB_PLUGIN_EXTENSION_CAST:
      valid = valid && is_valid_service_name(spec.source_type_id_) &&
              is_valid_service_name(spec.target_type_id_) &&
              spec.cast_context_ >= SEEKDB_PLUGIN_CAST_EXPLICIT &&
              spec.cast_context_ <= SEEKDB_PLUGIN_CAST_IMPLICIT &&
              is_valid_implementation(spec.implementation_);
      break;
    case SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD:
      valid = valid && is_valid_sql_name(spec.sql_name_) &&
              is_valid_implementation(spec.implementation_);
      break;
    case SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK:
    case SEEKDB_PLUGIN_EXTENSION_DAS_HOOK:
      valid = valid && is_valid_service_name(spec.hook_point_) &&
              is_valid_implementation(spec.implementation_);
      break;
    case SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT:
      valid = valid && is_valid_service_name(spec.catalog_object_kind_) &&
              is_valid_sql_segment(spec.schema_name_) &&
              is_valid_sql_segment(spec.sql_name_) &&
              is_valid_digest(spec.definition_digest_) &&
              (spec.flags_ & (SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
                              SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG)) ==
                  (SEEKDB_PLUGIN_EXTENSION_FLAG_PERSISTENT |
                   SEEKDB_PLUGIN_EXTENSION_FLAG_REQUIRES_CATALOG);
      break;
    default:
      valid = false;
      break;
  }
  return valid;
}

bool has_conflicting_extension_identity(const ObPluginExtensionSpec &left,
                                        const ObPluginExtensionSpec &right)
{
  bool conflict = left.object_id_ == right.object_id_ ||
                  (left.kind_ == SEEKDB_PLUGIN_EXTENSION_TYPE &&
                   right.kind_ == SEEKDB_PLUGIN_EXTENSION_TYPE &&
                   left.physical_format_id_ == right.physical_format_id_ &&
                   left.physical_format_version_ ==
                       right.physical_format_version_) ||
                  (left.kind_ == SEEKDB_PLUGIN_EXTENSION_CAST &&
                  right.kind_ == SEEKDB_PLUGIN_EXTENSION_CAST &&
                  left.source_type_id_ == right.source_type_id_ &&
                  left.target_type_id_ == right.target_type_id_ &&
                  left.cast_context_ == right.cast_context_);
  if (!conflict && left.kind_ == right.kind_ && !left.sql_name_.empty() &&
      left.sql_name_ == right.sql_name_) {
    switch (left.kind_) {
      case SEEKDB_PLUGIN_EXTENSION_FUNCTION:
      case SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION:
        // object_id identifies an overload.  Equal SQL names are expected and
        // later resolution uses the complete candidate set plus argument
        // metadata supplied by the implementation service.
        conflict = !left.argument_type_ids_.empty() &&
                   left.argument_type_ids_ == right.argument_type_ids_ &&
                   left.minimum_arity_ == right.minimum_arity_ &&
                   left.maximum_arity_ == right.maximum_arity_ &&
                   left.signature_flags_ == right.signature_flags_;
        break;
      case SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT:
        conflict = left.schema_name_ == right.schema_name_ &&
                   left.catalog_object_kind_ == right.catalog_object_kind_;
        break;
      default:
        conflict = true;
        break;
    }
  }
  return conflict;
}

bool exceeds_live_entry_limit(const size_t live_count,
                              const size_t staged_count,
                              const size_t maximum_count)
{
  return live_count > maximum_count ||
         staged_count > maximum_count - live_count;
}

} // namespace

ObPluginGeneration::ObPluginGeneration(const std::string &plugin_id,
                                       const uint64_t generation)
    : plugin_id_(plugin_id),
      generation_(generation),
      mutex_(),
      drained_cv_(),
      state_(ObPluginState::DISCOVERED),
      lease_count_(0),
      activation_reserved_(false)
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
  if (activation_reserved_) {
    ret = OB_EAGAIN;
  } else if (!is_valid_transition(state_, next)) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    state_ = next;
  }
  return ret;
}

int ObPluginGeneration::reserve_activation()
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(mutex_);
  if (activation_reserved_) {
    ret = OB_EAGAIN;
  } else if (ObPluginState::INITIALIZING != state_) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    activation_reserved_ = true;
  }
  return ret;
}

void ObPluginGeneration::abort_reserved_activation()
{
  std::lock_guard<std::mutex> guard(mutex_);
  activation_reserved_ = false;
}

void ObPluginGeneration::promote_reserved_activation()
{
  std::lock_guard<std::mutex> guard(mutex_);
  // Only the registry holding the matching global reservation can call this.
  // transition_to() rejects all competing lifecycle changes while reserved.
  state_ = ObPluginState::ACTIVE;
  activation_reserved_ = false;
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

ObPluginImplementationSpec::ObPluginImplementationSpec()
    : service_id_(), version_range_(), required_capabilities_(0)
{
  std::memset(&version_range_, 0, sizeof(version_range_));
}

ObPluginExtensionSpec::ObPluginExtensionSpec()
    : kind_(0), object_id_(), sql_name_(), physical_format_id_(),
      source_type_id_(), target_type_id_(), static_result_type_id_(),
      argument_type_ids_(), result_columns_(), hook_point_(),
      catalog_object_kind_(), schema_name_(), definition_digest_(),
      physical_format_version_(0), minimum_arity_(0), maximum_arity_(0),
      signature_flags_(0), cast_context_(0), cost_(0), priority_(0), flags_(0),
      implementation_()
{
}

ObPluginExtensionLease::ObPluginExtensionLease()
    : owner_(), info_()
{
}

ObPluginExtensionLease::ObPluginExtensionLease(
    const std::shared_ptr<ObPluginGeneration> &owner,
    const std::shared_ptr<const ObPluginExtensionInfo> &info)
    : owner_(owner), info_(info)
{
}

ObPluginExtensionLease::~ObPluginExtensionLease()
{
  reset();
}

ObPluginExtensionLease::ObPluginExtensionLease(
    ObPluginExtensionLease &&other) noexcept
    : owner_(std::move(other.owner_)), info_(std::move(other.info_))
{
}

ObPluginExtensionLease &ObPluginExtensionLease::operator=(
    ObPluginExtensionLease &&other) noexcept
{
  if (this != &other) {
    reset();
    owner_ = std::move(other.owner_);
    info_ = std::move(other.info_);
  }
  return *this;
}

const char *ObPluginExtensionLease::owner_plugin_id() const
{
  return nullptr == owner_ ? nullptr : owner_->plugin_id().c_str();
}

uint64_t ObPluginExtensionLease::owner_generation() const
{
  return nullptr == owner_ ? 0 : owner_->generation();
}

void ObPluginExtensionLease::reset()
{
  if (nullptr != owner_) {
    owner_->release_lease();
    owner_.reset();
  }
  info_.reset();
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
    : registry_(nullptr), owner_(), staged_(), staged_extensions_(), open_(false)
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
  staged_extensions_.clear();
  open_ = true;
}

void ObPluginRegistration::close()
{
  registry_ = nullptr;
  owner_.reset();
  staged_.clear();
  staged_extensions_.clear();
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
  } else if (!has_only_runtime_implementation_capabilities(capabilities)) {
    ret = OB_NOT_SUPPORTED;
  } else if (staged_.size() >= SEEKDB_PLUGIN_MAX_SERVICES) {
    ret = OB_SIZE_OVERFLOW;
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

int ObPluginRegistration::add_extension(const ObPluginExtensionSpec &extension)
{
  int ret = OB_SUCCESS;
  if (!open_) {
    ret = OB_NOT_INIT;
  } else if (!is_valid_extension_spec(extension)) {
    ret = OB_INVALID_ARGUMENT;
  } else if (staged_extensions_.size() >= SEEKDB_PLUGIN_MAX_EXTENSIONS) {
    ret = OB_SIZE_OVERFLOW;
  } else {
    for (const ObPluginExtensionSpec &item : staged_extensions_) {
      if (has_conflicting_extension_identity(item, extension)) {
        ret = OB_ENTRY_EXIST;
        break;
      }
    }
    if (OB_SUCCESS == ret) {
      try {
        staged_extensions_.push_back(extension);
      } catch (const std::bad_alloc &) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
      } catch (...) {
        ret = OB_ERR_UNEXPECTED;
      }
    }
  }
  return ret;
}

int ObPluginRegistration::prepare(ObPluginActivationCandidate &candidate)
{
  return open_ && nullptr != registry_
             ? registry_->prepare_registration(*this, candidate)
             : OB_NOT_INIT;
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

ObPluginServiceRegistry::ExtensionKey::ExtensionKey()
    : kind_(0), object_id_()
{
}

ObPluginServiceRegistry::ExtensionKey::ExtensionKey(
    const seekdb_plugin_extension_kind_t kind,
    const std::string &object_id)
    : kind_(kind), object_id_(object_id)
{
}

bool ObPluginServiceRegistry::ExtensionKey::operator<(
    const ExtensionKey &other) const
{
  return kind_ < other.kind_ ||
         (kind_ == other.kind_ && object_id_ < other.object_id_);
}

ObPluginServiceRegistry::ExtensionEntry::ExtensionEntry()
    : info_(), owner_()
{
}

ObPluginServiceRegistry::ExtensionEntry::ExtensionEntry(
    const ObPluginExtensionSpec &spec,
    const std::shared_ptr<ObPluginGeneration> &owner)
    : info_(), owner_(owner)
{
  std::shared_ptr<ObPluginExtensionInfo> info(
      new ObPluginExtensionInfo());
  info->spec_ = spec;
  info->owner_plugin_id_ = owner->plugin_id();
  info->owner_generation_ = owner->generation();
  info_ = info;
}

struct ObPluginServiceRegistry::RegistrySnapshot
{
  RegistrySnapshot() : services_(), extensions_() {}
  RegistrySnapshot(const RegistrySnapshot &) = default;

  std::map<ServiceKey, ServiceEntry> services_;
  std::map<ExtensionKey, ExtensionEntry> extensions_;
};

// Kept out of the public header so candidate consumers cannot mutate or even
// inspect a not-yet-visible registry image.  The staged vectors are retained
// for allocation-free conflict revalidation while installing the reservation.
class ObPluginPreparedActivation
{
public:
  ObPluginPreparedActivation()
      : registry_(nullptr), owner_(), base_snapshot_(), next_snapshot_(),
        base_epoch_(0), staged_services_(), staged_extensions_(),
        contributed_services_(), contributed_extensions_()
  {}

  ObPluginServiceRegistry *registry_;
  std::shared_ptr<ObPluginGeneration> owner_;
  std::shared_ptr<const ObPluginServiceRegistry::RegistrySnapshot>
      base_snapshot_;
  std::shared_ptr<const ObPluginServiceRegistry::RegistrySnapshot>
      next_snapshot_;
  uint64_t base_epoch_;
  std::vector<ObPluginServiceSpec> staged_services_;
  std::vector<ObPluginExtensionSpec> staged_extensions_;
  std::vector<ObPluginServiceInfo> contributed_services_;
  std::vector<ObPluginExtensionInfo> contributed_extensions_;
};

ObPluginServiceRegistry::~ObPluginServiceRegistry()
{
  // A candidate normally lives inside a loader operation and therefore cannot
  // outlive its shared registry.  Keep the public staging API safe as well:
  // disarm an outstanding candidate before this registry's storage vanishes,
  // so a later candidate destructor never follows a stale raw pointer.
  std::lock_guard<std::mutex> guard(mutex_);
  if (nullptr != activation_reservation_) {
    if (activation_reservation_->owner_) {
      activation_reservation_->owner_->abort_reserved_activation();
    }
    activation_reservation_->registry_ = nullptr;
    activation_reservation_ = nullptr;
  }
}

ObPluginActivationCandidate::ObPluginActivationCandidate()
    : prepared_()
{
}

ObPluginActivationCandidate::~ObPluginActivationCandidate()
{
  abort();
}

void ObPluginActivationCandidate::promote() noexcept
{
  if (nullptr == prepared_ || nullptr == prepared_->registry_) {
    std::terminate();
  }
  prepared_->registry_->promote_candidate(*this);
}

void ObPluginActivationCandidate::abort() noexcept
{
  if (prepared_ && prepared_->registry_) {
    prepared_->registry_->abort_candidate(*this);
  } else {
    prepared_.reset();
  }
}

bool ObPluginActivationCandidate::is_prepared() const noexcept
{
  return nullptr != prepared_ && nullptr != prepared_->registry_;
}

uint64_t ObPluginActivationCandidate::base_epoch() const
{
  return is_prepared() ? prepared_->base_epoch_ : 0;
}

const std::vector<ObPluginServiceInfo> &
ObPluginActivationCandidate::contributed_services() const noexcept
{
  if (!is_prepared()) {
    std::terminate();
  }
  return prepared_->contributed_services_;
}

const std::vector<ObPluginExtensionInfo> &
ObPluginActivationCandidate::contributed_extensions() const noexcept
{
  if (!is_prepared()) {
    std::terminate();
  }
  return prepared_->contributed_extensions_;
}

ObPluginServiceRegistry::ObPluginServiceRegistry()
    : mutex_(), live_snapshot_(std::make_shared<RegistrySnapshot>()),
      registry_epoch_(0), activation_reservation_(nullptr)
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

int ObPluginServiceRegistry::prepare_registration(
    ObPluginRegistration &registration,
    ObPluginActivationCandidate &candidate)
{
  int ret = OB_SUCCESS;
  std::shared_ptr<const RegistrySnapshot> base_snapshot;
  uint64_t base_epoch = 0;
  if (!registration.open_ || registration.registry_ != this ||
      nullptr == registration.owner_) {
    ret = OB_INVALID_ARGUMENT;
  } else if (candidate.is_prepared()) {
    ret = OB_INIT_TWICE;
  } else if (ObPluginState::INITIALIZING != registration.owner_->state()) {
    ret = OB_STATE_NOT_MATCH;
  } else {
    // Capturing an immutable shared snapshot is allocation-free.  All
    // potentially failing work below deliberately runs without mutex_.
    {
      std::lock_guard<std::mutex> guard(mutex_);
      if (nullptr != activation_reservation_) {
        ret = OB_EAGAIN;
      } else if (std::numeric_limits<uint64_t>::max() == registry_epoch_) {
        ret = OB_SIZE_OVERFLOW;
      } else if (exceeds_live_entry_limit(
                     live_snapshot_->services_.size(),
                     registration.staged_.size(),
                     SEEKDB_PLUGIN_MAX_SERVICES) ||
                 exceeds_live_entry_limit(
                     live_snapshot_->extensions_.size(),
                     registration.staged_extensions_.size(),
                     SEEKDB_PLUGIN_MAX_EXTENSIONS)) {
        // Reject before copying the immutable image.  Otherwise a stream of
        // individually bounded registrations could still make every prepare
        // consume unbounded O(N) memory and copy time.
        ret = OB_SIZE_OVERFLOW;
      } else {
        base_snapshot = live_snapshot_;
        base_epoch = registry_epoch_;
      }
    }
  }

  std::shared_ptr<RegistrySnapshot> mutable_next;
  std::unique_ptr<ObPluginPreparedActivation> prepared;
  if (OB_SUCCESS == ret) {
    try {
      mutable_next = std::make_shared<RegistrySnapshot>(*base_snapshot);
      prepared.reset(new ObPluginPreparedActivation());

      for (const ObPluginServiceSpec &spec : registration.staged_) {
        const auto inserted = mutable_next->services_.insert(
            std::make_pair(ServiceKey(spec.name_, spec.abi_major_),
                           ServiceEntry(spec, registration.owner_)));
        if (!inserted.second) {
          ret = OB_ENTRY_EXIST;
          break;
        }
      }
      for (const ObPluginExtensionSpec &spec :
           registration.staged_extensions_) {
        for (auto it = mutable_next->extensions_.begin();
             OB_SUCCESS == ret && it != mutable_next->extensions_.end(); ++it) {
          if (it->second.info_ && has_conflicting_extension_identity(
                                      it->second.info_->spec_, spec)) {
            ret = OB_ENTRY_EXIST;
          }
        }
        if (OB_SUCCESS == ret &&
            SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT != spec.kind_) {
          const seekdb_plugin_version_range_t &range =
              spec.implementation_.version_range_;
          const auto service_it = mutable_next->services_.find(ServiceKey(
              spec.implementation_.service_id_,
              range.minimum_inclusive.major));
          if (mutable_next->services_.end() == service_it ||
              service_it->second.owner_ != registration.owner_) {
            // R1 initially requires an implementation from the same atomic
            // generation.  Cross-plugin executable dependencies need a
            // catalog activation DAG and are not inferred from a bare name.
            ret = OB_ENTRY_NOT_EXIST;
          } else {
            const seekdb_plugin_semantic_version_t actual = {
                range.minimum_inclusive.major,
                service_it->second.abi_minor_,
                service_it->second.abi_patch_};
            if (!extension_version_in_range(actual, range) ||
                (service_it->second.capabilities_ &
                 spec.implementation_.required_capabilities_) !=
                    spec.implementation_.required_capabilities_) {
              ret = OB_NOT_SUPPORTED;
            }
          }
        }
        if (OB_SUCCESS == ret) {
          const auto inserted = mutable_next->extensions_.insert(
              std::make_pair(ExtensionKey(spec.kind_, spec.object_id_),
                             ExtensionEntry(spec, registration.owner_)));
          if (!inserted.second) {
            ret = OB_ENTRY_EXIST;
          }
        }
      }

      // Catch a lifecycle change which raced the expensive snapshot build.
      if (OB_SUCCESS == ret && ObPluginState::INITIALIZING !=
                                   registration.owner_->state()) {
        ret = OB_STATE_NOT_MATCH;
      }
      if (OB_SUCCESS == ret) {
        prepared->registry_ = this;
        prepared->owner_ = registration.owner_;
        prepared->base_snapshot_ = base_snapshot;
        prepared->next_snapshot_ = mutable_next;
        prepared->base_epoch_ = base_epoch;
        prepared->staged_services_ = registration.staged_;
        prepared->staged_extensions_ = registration.staged_extensions_;
        prepared->contributed_services_.reserve(registration.staged_.size());
        for (const ObPluginServiceSpec &spec : registration.staged_) {
          ObPluginServiceInfo info;
          info.name_ = spec.name_;
          info.abi_major_ = spec.abi_major_;
          info.abi_minor_ = spec.abi_minor_;
          info.abi_patch_ = spec.abi_patch_;
          info.capabilities_ = spec.capabilities_;
          info.owner_plugin_id_ = registration.owner_->plugin_id();
          info.owner_generation_ = registration.owner_->generation();
          prepared->contributed_services_.push_back(info);
        }
        prepared->contributed_extensions_.reserve(
            registration.staged_extensions_.size());
        for (const ObPluginExtensionSpec &spec :
             registration.staged_extensions_) {
          ObPluginExtensionInfo info;
          info.spec_ = spec;
          info.owner_plugin_id_ = registration.owner_->plugin_id();
          info.owner_generation_ = registration.owner_->generation();
          prepared->contributed_extensions_.push_back(info);
        }

        // Establish the global hidden reservation only after every allocation,
        // identity check and implementation binding has completed.  The
        // captured immutable base makes the final stale/conflict check cheap
        // and allocation-free.
        {
          std::lock_guard<std::mutex> guard(mutex_);
          if (nullptr != activation_reservation_) {
            ret = OB_EAGAIN;
          } else if (candidate_conflicts_locked(*prepared)) {
            ret = OB_ENTRY_EXIST;
          } else if (prepared->base_epoch_ != registry_epoch_ ||
                     prepared->base_snapshot_.get() != live_snapshot_.get()) {
            ret = OB_EAGAIN;
          } else if (std::numeric_limits<uint64_t>::max() == registry_epoch_) {
            ret = OB_SIZE_OVERFLOW;
          } else if (OB_SUCCESS !=
                     (ret = prepared->owner_->reserve_activation())) {
            // Lifecycle changed while the off-lock image was being built.
          } else {
            activation_reservation_ = prepared.get();
            candidate.prepared_ = std::move(prepared);
          }
        }
        if (OB_SUCCESS == ret) {
          registration.close();
        }
      }
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

bool ObPluginServiceRegistry::candidate_conflicts_locked(
    const ObPluginPreparedActivation &candidate) const
{
  bool conflict = false;
  for (const ObPluginServiceSpec &spec : candidate.staged_services_) {
    for (auto it = live_snapshot_->services_.begin();
         !conflict && it != live_snapshot_->services_.end(); ++it) {
      conflict = it->first.abi_major_ == spec.abi_major_ &&
                 it->first.name_ == spec.name_;
    }
  }
  for (const ObPluginExtensionSpec &spec : candidate.staged_extensions_) {
    for (auto it = live_snapshot_->extensions_.begin();
         !conflict && it != live_snapshot_->extensions_.end(); ++it) {
      conflict = it->second.info_ && has_conflicting_extension_identity(
                                          it->second.info_->spec_, spec);
    }
  }
  return conflict;
}

void ObPluginServiceRegistry::promote_candidate(
    ObPluginActivationCandidate &candidate) noexcept
{
  if (!candidate.prepared_ || candidate.prepared_->registry_ != this ||
      !candidate.prepared_->owner_ ||
      !candidate.prepared_->base_snapshot_ ||
      !candidate.prepared_->next_snapshot_) {
    std::terminate();
  }
  ObPluginPreparedActivation &prepared = *candidate.prepared_;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (activation_reservation_ != &prepared) {
      std::terminate();
    }
    // The reservation has prevented every registry and owner lifecycle
    // mutation since prepare.  For a legal token, no operation from here to
    // the end of the critical section can allocate or fail.
    prepared.owner_->promote_reserved_activation();
    live_snapshot_.swap(prepared.next_snapshot_);
    ++registry_epoch_;
    activation_reservation_ = nullptr;
  }
  // Releasing the old snapshot can run destructors/deallocations, so do it
  // only after mutex_ has been released.
  candidate.abort();
}

void ObPluginServiceRegistry::abort_candidate(
    ObPluginActivationCandidate &candidate) noexcept
{
  std::unique_ptr<ObPluginPreparedActivation> discarded;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (candidate.prepared_ &&
        activation_reservation_ == candidate.prepared_.get()) {
      candidate.prepared_->owner_->abort_reserved_activation();
      activation_reservation_ = nullptr;
    }
    discarded.swap(candidate.prepared_);
  }
  // Snapshot and staging storage destruction stays outside registry mutex_.
}

int ObPluginServiceRegistry::commit_registration(
    ObPluginRegistration &registration)
{
  ObPluginActivationCandidate candidate;
  int ret = prepare_registration(registration, candidate);
  if (OB_SUCCESS == ret) {
    promote_candidate(candidate);
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
      const auto it = live_snapshot_->services_.find(ServiceKey(name, abi_major));
      if (live_snapshot_->services_.end() == it ||
          it->second.abi_minor_ < required_minor ||
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

int ObPluginServiceRegistry::acquire_extension_with_implementation(
    const ObPluginExtensionInfo &expected,
    ObPluginExtensionLease &extension_lease,
    ObPluginLease &implementation_lease)
{
  int ret = OB_SUCCESS;
  const seekdb_plugin_extension_kind_t kind = expected.spec_.kind_;
  const std::string &object_id = expected.spec_.object_id_;
  if (kind < SEEKDB_PLUGIN_EXTENSION_TYPE ||
      kind > SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION ||
      kind == SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT ||
      !is_valid_service_name(object_id) || expected.owner_plugin_id_.empty() ||
      0 == expected.owner_generation_ || extension_lease.is_valid() ||
      implementation_lease.is_valid()) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    try {
      std::lock_guard<std::mutex> guard(mutex_);
      const auto extension_it = live_snapshot_->extensions_.find(
          ExtensionKey(kind, object_id));
      if (live_snapshot_->extensions_.end() == extension_it ||
          !extension_it->second.info_ ||
          extension_it->second.info_->owner_plugin_id_ !=
              expected.owner_plugin_id_ ||
          extension_it->second.info_->owner_generation_ !=
              expected.owner_generation_) {
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        const ObPluginImplementationSpec &implementation =
            extension_it->second.info_->spec_.implementation_;
        const seekdb_plugin_version_range_t &range =
            implementation.version_range_;
        const auto service_it = live_snapshot_->services_.find(ServiceKey(
            implementation.service_id_, range.minimum_inclusive.major));
        if (live_snapshot_->services_.end() == service_it ||
            service_it->second.owner_ != extension_it->second.owner_ ||
            !extension_version_in_range(
                {range.minimum_inclusive.major, service_it->second.abi_minor_,
                 service_it->second.abi_patch_},
                range) ||
            (service_it->second.capabilities_ &
             implementation.required_capabilities_) !=
                implementation.required_capabilities_) {
          ret = OB_ENTRY_NOT_EXIST;
        } else if (!extension_it->second.owner_->try_acquire_lease()) {
          ret = OB_STATE_NOT_MATCH;
        } else if (!service_it->second.owner_->try_acquire_lease()) {
          // Both entries share one generation and registry mutex excludes
          // quiesce, so this branch is defensive against future state changes.
          extension_it->second.owner_->release_lease();
          ret = OB_STATE_NOT_MATCH;
        } else {
          extension_lease = ObPluginExtensionLease(
              extension_it->second.owner_, extension_it->second.info_);
          implementation_lease = ObPluginLease(
              service_it->second.owner_, service_it->second.service_,
              service_it->second.abi_minor_, service_it->second.abi_patch_,
              service_it->second.capabilities_);
        }
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
    bool published = false;
    while (OB_SUCCESS == ret && !published) {
      std::shared_ptr<const RegistrySnapshot> base_snapshot;
      uint64_t base_epoch = 0;
      {
        std::lock_guard<std::mutex> guard(mutex_);
        if (nullptr != activation_reservation_) {
          ret = OB_EAGAIN;
        } else if (std::numeric_limits<uint64_t>::max() == registry_epoch_) {
          ret = OB_SIZE_OVERFLOW;
        } else {
          base_snapshot = live_snapshot_;
          base_epoch = registry_epoch_;
        }
      }

      std::shared_ptr<const RegistrySnapshot> next_snapshot;
      if (OB_SUCCESS == ret) {
        try {
          std::shared_ptr<RegistrySnapshot> mutable_next =
              std::make_shared<RegistrySnapshot>(*base_snapshot);
          for (auto it = mutable_next->services_.begin();
               it != mutable_next->services_.end();) {
            if (it->second.owner_ == owner) {
              it = mutable_next->services_.erase(it);
            } else {
              ++it;
            }
          }
          for (auto it = mutable_next->extensions_.begin();
               it != mutable_next->extensions_.end();) {
            if (it->second.owner_ == owner) {
              it = mutable_next->extensions_.erase(it);
            } else {
              ++it;
            }
          }
          next_snapshot = mutable_next;
        } catch (const std::bad_alloc &) {
          ret = OB_ALLOCATE_MEMORY_FAILED;
        } catch (...) {
          ret = OB_ERR_UNEXPECTED;
        }
      }

      if (OB_SUCCESS == ret) {
        std::lock_guard<std::mutex> guard(mutex_);
        if (nullptr != activation_reservation_) {
          ret = OB_EAGAIN;
        } else if (base_epoch != registry_epoch_ ||
            base_snapshot.get() != live_snapshot_.get()) {
          // Another immutable snapshot won the race.  Rebuild outside the
          // lock so it cannot be overwritten by this quiesce operation.
        } else if (std::numeric_limits<uint64_t>::max() == registry_epoch_) {
          ret = OB_SIZE_OVERFLOW;
        } else if (OB_SUCCESS != (ret = owner->begin_quiesce())) {
          // State remains unchanged and services stay visible on failure.
        } else {
          live_snapshot_.swap(next_snapshot);
          ++registry_epoch_;
          published = true;
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
    candidate.reserve(live_snapshot_->services_.size());
    for (const auto &item : live_snapshot_->services_) {
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

int ObPluginServiceRegistry::list_extensions(
    std::vector<ObPluginExtensionInfo> &extensions) const
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    std::vector<ObPluginExtensionInfo> candidate;
    candidate.reserve(live_snapshot_->extensions_.size());
    for (const auto &item : live_snapshot_->extensions_) {
      if (item.second.info_) {
        candidate.push_back(*item.second.info_);
      }
    }
    extensions.swap(candidate);
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginServiceRegistry::find_extensions_by_sql_name(
    const seekdb_plugin_extension_kind_t kind,
    const char *sql_name,
    std::vector<ObPluginExtensionInfo> &extensions,
    uint64_t &registry_epoch) const
{
  int ret = OB_SUCCESS;
  if ((SEEKDB_PLUGIN_EXTENSION_TYPE != kind &&
       SEEKDB_PLUGIN_EXTENSION_FUNCTION != kind &&
       SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION != kind &&
       SEEKDB_PLUGIN_EXTENSION_INDEX_ACCESS_METHOD != kind) ||
      nullptr == sql_name || !is_valid_sql_name(sql_name, true)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    std::lock_guard<std::mutex> guard(mutex_);
    try {
      std::vector<ObPluginExtensionInfo> candidate;
      for (const auto &item : live_snapshot_->extensions_) {
        if (item.first.kind_ == kind && item.second.info_ &&
            item.second.info_->spec_.sql_name_ == sql_name) {
          candidate.push_back(*item.second.info_);
        }
      }
      extensions.swap(candidate);
      registry_epoch = registry_epoch_;
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObPluginServiceRegistry::resolve_sql_extension(
    const seekdb_plugin_extension_kind_t kind,
    const char *sql_name,
    const char *const *argument_type_ids,
    const uint32_t argument_count,
    ObPluginExtensionInfo &extension,
    uint64_t &registry_epoch) const
{
  if ((SEEKDB_PLUGIN_EXTENSION_FUNCTION != kind &&
       SEEKDB_PLUGIN_EXTENSION_TABLE_FUNCTION != kind &&
       SEEKDB_PLUGIN_EXTENSION_TYPE != kind) ||
      !is_valid_sql_name(sql_name, true) ||
      argument_count > SEEKDB_PLUGIN_MAX_ARGUMENTS ||
      (argument_count != 0 && nullptr == argument_type_ids)) {
    return OB_INVALID_ARGUMENT;
  }

  int ret = OB_ENTRY_NOT_EXIST;
  std::lock_guard<std::mutex> guard(mutex_);
  try {
    bool has_known_argument_type = false;
    for (uint32_t i = 0; i < argument_count; ++i) {
      if (nullptr != argument_type_ids[i]) {
        has_known_argument_type = true;
        break;
      }
    }
    const ObPluginExtensionInfo *best = nullptr;
    uint64_t best_cost = std::numeric_limits<uint64_t>::max();
    bool ambiguous = false;
    for (const auto &entry : live_snapshot_->extensions_) {
      if (kind != entry.first.kind_ || !entry.second.info_ ||
          entry.second.info_->spec_.sql_name_ != sql_name) {
        continue;
      }
      const ObPluginExtensionInfo &candidate = *entry.second.info_;
      const ObPluginExtensionSpec &spec = candidate.spec_;
      if (SEEKDB_PLUGIN_EXTENSION_TYPE != kind &&
          (argument_count < spec.minimum_arity_ ||
           argument_count > spec.maximum_arity_)) {
        continue;
      }

      uint64_t cost = spec.argument_type_ids_.empty() ? 1000000 : 0;
      bool compatible = true;
      for (uint32_t i = 0; compatible && i < argument_count; ++i) {
        if (nullptr == argument_type_ids[i]) {
          // Resolver may ask for name/arity existence before child typing.
          cost += spec.argument_type_ids_.empty() ? 0 : 1;
          continue;
        }
        if (!is_valid_service_name(argument_type_ids[i])) {
          return OB_INVALID_ARGUMENT;
        }
        if (spec.argument_type_ids_.empty()) {
          continue;
        }
        const size_t signature_index =
            std::min(static_cast<size_t>(i), spec.argument_type_ids_.size() - 1);
        const std::string &expected = spec.argument_type_ids_[signature_index];
        if (expected == argument_type_ids[i]) {
          continue;
        }

        uint32_t best_cast_cost = std::numeric_limits<uint32_t>::max();
        for (const auto &cast_entry : live_snapshot_->extensions_) {
          if (SEEKDB_PLUGIN_EXTENSION_CAST != cast_entry.first.kind_ ||
              !cast_entry.second.info_) {
            continue;
          }
          const ObPluginExtensionSpec &cast = cast_entry.second.info_->spec_;
          if (cast.source_type_id_ == argument_type_ids[i] &&
              cast.target_type_id_ == expected &&
              cast.cast_context_ == SEEKDB_PLUGIN_CAST_IMPLICIT) {
            best_cast_cost = std::min(best_cast_cost, cast.cost_);
          }
        }
        if (best_cast_cost == std::numeric_limits<uint32_t>::max()) {
          compatible = false;
        } else {
          cost += 1 + best_cast_cost;
        }
      }
      if (!compatible) {
        continue;
      }
      if (nullptr == best || cost < best_cost) {
        best = &candidate;
        best_cost = cost;
        ambiguous = false;
      } else if (cost == best_cost) {
        if (has_known_argument_type || argument_count == 0) {
          ambiguous = true;
        } else if (candidate.spec_.object_id_ < best->spec_.object_id_) {
          // Name/arity probing precedes child typing.  Any overload proves the
          // SQL name exists; defer ambiguity checks until types are available.
          best = &candidate;
        }
      }
    }
    registry_epoch = registry_epoch_;
    if (ambiguous) {
      ret = OB_ENTRY_EXIST;
    } else if (nullptr != best) {
      extension = *best;
      ret = OB_SUCCESS;
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginServiceRegistry::find_casts(
    const char *source_type_id,
    const char *target_type_id,
    const seekdb_plugin_cast_context_t requested_context,
    std::vector<ObPluginExtensionInfo> &extensions,
    uint64_t &registry_epoch) const
{
  int ret = OB_SUCCESS;
  if (!is_valid_service_name(source_type_id) ||
      !is_valid_service_name(target_type_id) ||
      requested_context < SEEKDB_PLUGIN_CAST_EXPLICIT ||
      requested_context > SEEKDB_PLUGIN_CAST_IMPLICIT) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    try {
      std::vector<ObPluginExtensionInfo> candidate;
      uint64_t observed_epoch = 0;
      {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto &item : live_snapshot_->extensions_) {
          if (SEEKDB_PLUGIN_EXTENSION_CAST == item.first.kind_ &&
              item.second.info_ &&
              item.second.info_->spec_.source_type_id_ == source_type_id &&
              item.second.info_->spec_.target_type_id_ == target_type_id &&
              item.second.info_->spec_.cast_context_ >= requested_context) {
            candidate.push_back(*item.second.info_);
          }
        }
        observed_epoch = registry_epoch_;
      }
      std::sort(candidate.begin(), candidate.end(),
                [](const ObPluginExtensionInfo &left,
                   const ObPluginExtensionInfo &right) {
                  return left.spec_.cost_ < right.spec_.cost_ ||
                         (left.spec_.cost_ == right.spec_.cost_ &&
                          left.spec_.object_id_ < right.spec_.object_id_);
                });
      extensions.swap(candidate);
      registry_epoch = observed_epoch;
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObPluginServiceRegistry::find_hooks(
    const seekdb_plugin_extension_kind_t kind,
    const char *hook_point,
    std::vector<ObPluginExtensionInfo> &extensions,
    uint64_t &registry_epoch) const
{
  int ret = OB_SUCCESS;
  if ((SEEKDB_PLUGIN_EXTENSION_OPTIMIZER_HOOK != kind &&
       SEEKDB_PLUGIN_EXTENSION_DAS_HOOK != kind) ||
      !is_valid_service_name(hook_point)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    try {
      std::vector<ObPluginExtensionInfo> candidate;
      uint64_t observed_epoch = 0;
      {
        std::lock_guard<std::mutex> guard(mutex_);
        for (const auto &item : live_snapshot_->extensions_) {
          if (kind == item.first.kind_ && item.second.info_ &&
              item.second.info_->spec_.hook_point_ == hook_point) {
            candidate.push_back(*item.second.info_);
          }
        }
        observed_epoch = registry_epoch_;
      }
      std::sort(candidate.begin(), candidate.end(),
                [](const ObPluginExtensionInfo &left,
                   const ObPluginExtensionInfo &right) {
                  return left.spec_.priority_ > right.spec_.priority_ ||
                         (left.spec_.priority_ == right.spec_.priority_ &&
                          left.spec_.object_id_ < right.spec_.object_id_);
                });
      extensions.swap(candidate);
      registry_epoch = observed_epoch;
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int ObPluginServiceRegistry::find_catalog_objects(
    const char *object_kind,
    const char *schema_name,
    const char *sql_name,
    std::vector<ObPluginExtensionInfo> &extensions,
    uint64_t &registry_epoch) const
{
  int ret = OB_SUCCESS;
  if (!is_valid_service_name(object_kind) || nullptr == schema_name ||
      nullptr == sql_name ||
      !is_valid_sql_name(schema_name, false) ||
      !is_valid_sql_name(sql_name, false)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    try {
      std::vector<ObPluginExtensionInfo> candidate;
      std::lock_guard<std::mutex> guard(mutex_);
      for (const auto &item : live_snapshot_->extensions_) {
        if (SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT == item.first.kind_ &&
            item.second.info_ &&
            item.second.info_->spec_.catalog_object_kind_ == object_kind &&
            item.second.info_->spec_.schema_name_ == schema_name &&
            item.second.info_->spec_.sql_name_ == sql_name) {
          candidate.push_back(*item.second.info_);
        }
      }
      extensions.swap(candidate);
      registry_epoch = registry_epoch_;
    } catch (const std::bad_alloc &) {
      ret = OB_ALLOCATE_MEMORY_FAILED;
    } catch (...) {
      ret = OB_ERR_UNEXPECTED;
    }
  }
  return ret;
}

int64_t ObPluginServiceRegistry::service_count() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  return static_cast<int64_t>(live_snapshot_->services_.size());
}

int64_t ObPluginServiceRegistry::extension_count() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  return static_cast<int64_t>(live_snapshot_->extensions_.size());
}

uint64_t ObPluginServiceRegistry::registry_epoch() const
{
  std::lock_guard<std::mutex> guard(mutex_);
  return registry_epoch_;
}

} // namespace plugin
} // namespace share
} // namespace oceanbase
