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
#include "plugin_runtime.h"
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

int runtime_status_to_ob(const int32_t status)
{
  switch (status) {
    case SEEKDB_RUNTIME_OK: return OB_SUCCESS;
    case SEEKDB_RUNTIME_INVALID: return OB_INVALID_ARGUMENT;
    case SEEKDB_RUNTIME_STATE_MISMATCH: return OB_STATE_NOT_MATCH;
    case SEEKDB_RUNTIME_BUSY: return OB_EAGAIN;
    case SEEKDB_RUNTIME_TIMEOUT: return OB_TIMEOUT;
    default: return OB_ERR_UNEXPECTED;
  }
}

static_assert(static_cast<uint8_t>(ObPluginState::DISCOVERED) == SEEKDB_RUNTIME_DISCOVERED,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::VALIDATED) == SEEKDB_RUNTIME_VALIDATED,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::LOADED) == SEEKDB_RUNTIME_LOADED,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::INITIALIZING) == SEEKDB_RUNTIME_INITIALIZING,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::ACTIVE) == SEEKDB_RUNTIME_ACTIVE,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::QUIESCING) == SEEKDB_RUNTIME_QUIESCING,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::STOPPED) == SEEKDB_RUNTIME_STOPPED,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::FAILED) == SEEKDB_RUNTIME_FAILED,
              "Rust/C++ plugin state mismatch");
static_assert(static_cast<uint8_t>(ObPluginState::BLOCKED) == SEEKDB_RUNTIME_BLOCKED,
              "Rust/C++ plugin state mismatch");

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
    : plugin_id_(plugin_id), generation_(generation),
      runtime_(seekdb_runtime_generation_create())
{
  if (nullptr == runtime_) {
    throw std::bad_alloc();
  }
}

ObPluginGeneration::~ObPluginGeneration()
{
  seekdb_runtime_generation_destroy(runtime_);
}

ObPluginState ObPluginGeneration::state() const
{
  return static_cast<ObPluginState>(seekdb_runtime_generation_state(runtime_));
}

int64_t ObPluginGeneration::lease_count() const
{
  return seekdb_runtime_generation_leases(runtime_);
}

int ObPluginGeneration::transition_to(const ObPluginState next)
{
  return runtime_status_to_ob(seekdb_runtime_generation_transition(
      runtime_, static_cast<uint8_t>(next)));
}

int ObPluginGeneration::reserve_activation()
{
  return runtime_status_to_ob(seekdb_runtime_generation_reserve(runtime_));
}

void ObPluginGeneration::abort_reserved_activation()
{
  if (SEEKDB_RUNTIME_OK != seekdb_runtime_generation_abort(runtime_)) {
    std::terminate();
  }
}

void ObPluginGeneration::promote_reserved_activation()
{
  // A prepared registry candidate owns the reservation; publication cannot fail.
  if (SEEKDB_RUNTIME_OK != seekdb_runtime_generation_promote(runtime_)) {
    std::terminate();
  }
}

bool ObPluginGeneration::try_acquire_lease()
{
  return 0 != seekdb_runtime_generation_acquire(runtime_);
}

void ObPluginGeneration::release_lease()
{
  if (SEEKDB_RUNTIME_OK != seekdb_runtime_generation_release(runtime_)) {
    std::terminate();
  }
}

int ObPluginGeneration::begin_quiesce()
{
  return runtime_status_to_ob(seekdb_runtime_generation_quiesce(runtime_));
}

int ObPluginGeneration::wait_for_drain(const int64_t timeout_us)
{
  return runtime_status_to_ob(seekdb_runtime_generation_drain(runtime_, timeout_us));
}

int ObPluginGeneration::terminal_mark_stopped()
{
  return runtime_status_to_ob(seekdb_runtime_generation_terminal_stop(runtime_));
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

struct ObPluginServiceRegistry::ExtensionSignature
{
  // Built once alongside immutable metadata, shared across registry snapshot
  // copies. Never rebuild every argument span of every overload on a lookup.
  std::vector<seekdb_runtime_text_t> types_;
};

ObPluginServiceRegistry::ExtensionEntry::ExtensionEntry()
    : info_(), signature_(), owner_()
{
}

ObPluginServiceRegistry::ExtensionEntry::ExtensionEntry(
    const ObPluginExtensionSpec &spec,
    const std::shared_ptr<ObPluginGeneration> &owner)
    : info_(), signature_(), owner_(owner)
{
  std::shared_ptr<ObPluginExtensionInfo> info(
      new ObPluginExtensionInfo());
  info->spec_ = spec;
  info->owner_plugin_id_ = owner->plugin_id();
  info->owner_generation_ = owner->generation();
  info_ = info;
  if (!info->spec_.argument_type_ids_.empty()) {
    auto signature = std::make_shared<ExtensionSignature>();
    signature->types_.reserve(info->spec_.argument_type_ids_.size());
    for (const auto &type : info->spec_.argument_type_ids_) {
      signature->types_.push_back({reinterpret_cast<const uint8_t *>(type.data()),
                                   static_cast<uint32_t>(type.size())});
    }
    signature_ = signature;
  }
}

struct ObPluginServiceRegistry::ExtensionCatalog
{
  ExtensionCatalog() : handle_(seekdb_runtime_objects_create())
  { if (nullptr == handle_) throw std::bad_alloc(); }
  ExtensionCatalog(const ExtensionCatalog &other)
      : handle_(seekdb_runtime_objects_clone(other.handle_))
  { if (nullptr == handle_) throw std::bad_alloc(); }
  ExtensionCatalog &operator=(const ExtensionCatalog &) = delete;
  ~ExtensionCatalog() { seekdb_runtime_objects_destroy(handle_); }

  static void release(void *payload) noexcept
  { delete static_cast<ExtensionEntry *>(payload); }
  uint32_t size() const noexcept { return seekdb_runtime_objects_count(handle_); }
  const ExtensionEntry &at(uint32_t index) const noexcept
  {
    const auto *entry = static_cast<const ExtensionEntry *>(seekdb_runtime_objects_at(handle_, index));
    if (nullptr == entry) std::terminate();
    return *entry;
  }
  const ExtensionEntry *find(seekdb_plugin_extension_kind_t kind,
                             const std::string &id) const noexcept
  {
    return static_cast<const ExtensionEntry *>(seekdb_runtime_objects_find(
        handle_, static_cast<uint32_t>(kind), reinterpret_cast<const uint8_t *>(id.data()),
        static_cast<uint32_t>(id.size())));
  }
  int insert(const ObPluginExtensionSpec &spec,
             const std::shared_ptr<ObPluginGeneration> &owner)
  {
    std::unique_ptr<ExtensionEntry> entry(new ExtensionEntry(spec, owner));
    const int32_t status = seekdb_runtime_objects_insert(handle_, spec.kind_,
        reinterpret_cast<const uint8_t *>(spec.object_id_.data()),
        static_cast<uint32_t>(spec.object_id_.size()), entry.get(), release);
    if (SEEKDB_RUNTIME_OK == status) { entry.release(); return OB_SUCCESS; }
    if (SEEKDB_RUNTIME_NO_MEMORY == status) return OB_ALLOCATE_MEMORY_FAILED;
    if (SEEKDB_RUNTIME_CONFLICT == status) return OB_ENTRY_EXIST;
    if (SEEKDB_RUNTIME_LIMIT == status) return OB_SIZE_OVERFLOW;
    return OB_INVALID_ARGUMENT;
  }
  static uint8_t owned_by(const void *context, const void *payload) noexcept
  {
    return static_cast<const ExtensionEntry *>(payload)->owner_.get() ==
           static_cast<const ObPluginGeneration *>(context) ? 1 : 0;
  }
  void remove_owner(const std::shared_ptr<ObPluginGeneration> &owner) noexcept
  {
    if (SEEKDB_RUNTIME_OK != seekdb_runtime_objects_remove_if(handle_, owner.get(), owned_by)) {
      std::terminate();
    }
  }

  seekdb_runtime_object_catalog *handle_;
};

struct ObPluginServiceRegistry::RegistrySnapshot
{
  RegistrySnapshot() : services_(), extensions_() {}
  RegistrySnapshot(const RegistrySnapshot &) = default;

  std::map<ServiceKey, ServiceEntry> services_;
  ExtensionCatalog extensions_;
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
        for (uint32_t i = 0; OB_SUCCESS == ret && i < mutable_next->extensions_.size(); ++i) {
          const auto &entry = mutable_next->extensions_.at(i);
          if (entry.info_ && has_conflicting_extension_identity(entry.info_->spec_, spec)) {
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
          ret = mutable_next->extensions_.insert(spec, registration.owner_);
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
    for (uint32_t i = 0; !conflict && i < live_snapshot_->extensions_.size(); ++i) {
      const auto &entry = live_snapshot_->extensions_.at(i);
      conflict = entry.info_ && has_conflicting_extension_identity(entry.info_->spec_, spec);
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
    ObPluginLease &implementation_lease,
    const uint64_t expected_epoch)
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
      const auto *extension = live_snapshot_->extensions_.find(kind, object_id);
      if (expected_epoch != 0 && expected_epoch != registry_epoch_) {
        ret = OB_STATE_NOT_MATCH;
      } else if (nullptr == extension ||
          !extension->info_ ||
          extension->info_->owner_plugin_id_ !=
              expected.owner_plugin_id_ ||
          extension->info_->owner_generation_ !=
              expected.owner_generation_) {
        ret = OB_ENTRY_NOT_EXIST;
      } else {
        const ObPluginImplementationSpec &implementation =
            extension->info_->spec_.implementation_;
        const seekdb_plugin_version_range_t &range =
            implementation.version_range_;
        const auto service_it = live_snapshot_->services_.find(ServiceKey(
            implementation.service_id_, range.minimum_inclusive.major));
        if (live_snapshot_->services_.end() == service_it ||
            service_it->second.owner_ != extension->owner_ ||
            !extension_version_in_range(
                {range.minimum_inclusive.major, service_it->second.abi_minor_,
                 service_it->second.abi_patch_},
                range) ||
            (service_it->second.capabilities_ &
             implementation.required_capabilities_) !=
                implementation.required_capabilities_) {
          ret = OB_ENTRY_NOT_EXIST;
        } else if (!extension->owner_->try_acquire_lease()) {
          ret = OB_STATE_NOT_MATCH;
        } else if (!service_it->second.owner_->try_acquire_lease()) {
          // Both entries share one generation and registry mutex excludes
          // quiesce, so this branch is defensive against future state changes.
          extension->owner_->release_lease();
          ret = OB_STATE_NOT_MATCH;
        } else {
          extension_lease = ObPluginExtensionLease(
              extension->owner_, extension->info_);
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
          mutable_next->extensions_.remove_owner(owner);
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
    for (uint32_t i = 0; i < live_snapshot_->extensions_.size(); ++i) {
      const auto &entry = live_snapshot_->extensions_.at(i);
      if (entry.info_) {
        candidate.push_back(*entry.info_);
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

int ObPluginServiceRegistry::find_type_by_id(const char *logical_type_id,
    ObPluginExtensionInfo &extension, uint64_t &registry_epoch, uint64_t expected_epoch) const
try {
  extension = {}; registry_epoch = 0;
  if (!is_valid_service_name(logical_type_id)) return OB_INVALID_ARGUMENT;
  std::shared_ptr<const RegistrySnapshot> snapshot;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    if (expected_epoch && expected_epoch != registry_epoch_) return OB_STATE_NOT_MATCH;
    snapshot = live_snapshot_;
    epoch = registry_epoch_;
  }
  const auto *entry = snapshot->extensions_.find(SEEKDB_PLUGIN_EXTENSION_TYPE, logical_type_id);
  if (!entry || !entry->info_) return OB_ENTRY_NOT_EXIST;
  extension = *entry->info_;
  registry_epoch = epoch;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) {
  extension = {}; registry_epoch = 0; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  extension = {}; registry_epoch = 0; return OB_ERR_UNEXPECTED;
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
      for (uint32_t i = 0; i < live_snapshot_->extensions_.size(); ++i) {
        const auto &entry = live_snapshot_->extensions_.at(i);
        if (entry.info_ && entry.info_->spec_.kind_ == kind &&
            entry.info_->spec_.sql_name_ == sql_name) {
          candidate.push_back(*entry.info_);
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
  try {
    std::vector<seekdb_runtime_text_t> arguments(argument_count);
    for (uint32_t i = 0; i < argument_count; ++i) {
      if (argument_type_ids[i] != nullptr) {
        if (!is_valid_service_name(argument_type_ids[i])) return OB_INVALID_ARGUMENT;
        arguments[i] = {reinterpret_cast<const uint8_t *>(argument_type_ids[i]),
                        static_cast<uint32_t>(std::strlen(argument_type_ids[i]))};
      }
    }
    std::shared_ptr<const RegistrySnapshot> snapshot;
    {
      std::lock_guard<std::mutex> guard(mutex_);
      snapshot = live_snapshot_;
      registry_epoch = registry_epoch_;
    }
    // Snapshot ownership pins every borrowed string and generation while Rust
    // resolves. No registry mutex is held during marshaling or cost matching;
    // subsequent acquire still validates the returned generation atomically.
    const auto text = [](const std::string &value) -> seekdb_runtime_text_t {
      return {reinterpret_cast<const uint8_t *>(value.data()), static_cast<uint32_t>(value.size())};
    };
    std::vector<seekdb_runtime_sql_candidate_t> candidates;
    std::vector<const ObPluginExtensionInfo *> objects;
    std::vector<seekdb_runtime_sql_cast_t> casts;
    for (uint32_t i = 0; i < snapshot->extensions_.size(); ++i) {
      const auto &entry = snapshot->extensions_.at(i);
      if (!entry.info_) continue;
      const auto &spec = entry.info_->spec_;
      if (SEEKDB_PLUGIN_EXTENSION_CAST == spec.kind_) {
        casts.push_back({text(spec.source_type_id_), text(spec.target_type_id_),
                         static_cast<uint32_t>(spec.cast_context_), spec.cost_});
      }
      if (kind != spec.kind_ || spec.sql_name_ != sql_name) continue;
      const auto *signature = entry.signature_.get();
      candidates.push_back({text(spec.object_id_), signature == nullptr ? nullptr : signature->types_.data(),
          static_cast<uint32_t>(spec.argument_type_ids_.size()), spec.minimum_arity_, spec.maximum_arity_, 0});
      objects.push_back(entry.info_.get());
    }
    uint32_t selected = UINT32_MAX;
    const int32_t status = seekdb_runtime_resolve_sql(
        candidates.data(), static_cast<uint32_t>(candidates.size()),
        casts.data(), static_cast<uint32_t>(casts.size()), arguments.data(), argument_count,
        SEEKDB_PLUGIN_EXTENSION_TYPE == kind ? 0 : 1, &selected);
    if (SEEKDB_RUNTIME_OK == status && selected < objects.size()) {
      extension = *objects[selected];
      ret = OB_SUCCESS;
    } else if (SEEKDB_RUNTIME_AMBIGUOUS == status) {
      ret = OB_ENTRY_EXIST;
    } else if (SEEKDB_RUNTIME_NOT_FOUND == status) {
      ret = OB_ENTRY_NOT_EXIST;
    } else {
      ret = SEEKDB_RUNTIME_INVALID == status ? OB_INVALID_ARGUMENT : OB_ERR_UNEXPECTED;
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginServiceRegistry::resolve_cast(
    const char *source_type_id, const char *target_type_id,
    const seekdb_plugin_cast_context_t requested_context,
    ObPluginExtensionInfo &extension, uint64_t &registry_epoch) const
try {
  if (!is_valid_service_name(source_type_id) || !is_valid_service_name(target_type_id) ||
      requested_context < SEEKDB_PLUGIN_CAST_EXPLICIT || requested_context > SEEKDB_PLUGIN_CAST_IMPLICIT)
    return OB_INVALID_ARGUMENT;
  std::shared_ptr<const RegistrySnapshot> snapshot;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    snapshot = live_snapshot_;
    epoch = registry_epoch_;
  }
  const auto text = [](const char *value) -> seekdb_runtime_text_t {
    return {reinterpret_cast<const uint8_t *>(value), static_cast<uint32_t>(std::strlen(value))};
  };
  std::vector<seekdb_runtime_sql_cast_t> casts;
  std::vector<const ObPluginExtensionInfo *> objects;
  for (uint32_t i = 0; i < snapshot->extensions_.size(); ++i) {
    const auto &entry = snapshot->extensions_.at(i);
    if (!entry.info_ || entry.info_->spec_.kind_ != SEEKDB_PLUGIN_EXTENSION_CAST) continue;
    const auto &spec = entry.info_->spec_;
    casts.push_back({text(spec.source_type_id_.c_str()), text(spec.target_type_id_.c_str()),
                     static_cast<uint32_t>(spec.cast_context_), spec.cost_});
    objects.push_back(entry.info_.get());
  }
  uint32_t selected = UINT32_MAX;
  const int status = seekdb_runtime_resolve_cast(casts.data(), static_cast<uint32_t>(casts.size()),
      text(source_type_id), text(target_type_id), static_cast<uint32_t>(requested_context), &selected);
  if (status == SEEKDB_RUNTIME_NOT_FOUND) return OB_ENTRY_NOT_EXIST;
  if (status == SEEKDB_RUNTIME_AMBIGUOUS) return OB_ENTRY_EXIST;
  if (status != SEEKDB_RUNTIME_OK || selected >= objects.size())
    return status == SEEKDB_RUNTIME_INVALID ? OB_INVALID_ARGUMENT : OB_ERR_UNEXPECTED;
  ObPluginExtensionInfo chosen = *objects[selected];
  extension = std::move(chosen);
  registry_epoch = epoch;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) { return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) { return OB_ERR_UNEXPECTED; }

int ObPluginServiceRegistry::resolve_common_type(const char *const *type_ids, uint32_t count,
    std::string &common_type, uint64_t &registry_epoch) const
try {
  common_type.clear(); registry_epoch = 0;
  if (count > SEEKDB_PLUGIN_MAX_ARGUMENTS || (count && !type_ids)) return OB_INVALID_ARGUMENT;
  std::vector<seekdb_runtime_text_t> arguments(count);
  for (uint32_t i = 0; i < count; ++i) {
    if (type_ids[i]) {
      if (!is_valid_service_name(type_ids[i])) return OB_INVALID_ARGUMENT;
      arguments[i] = {reinterpret_cast<const uint8_t *>(type_ids[i]), static_cast<uint32_t>(std::strlen(type_ids[i]))};
    }
  }
  std::shared_ptr<const RegistrySnapshot> snapshot;
  uint64_t epoch = 0;
  {
    std::lock_guard<std::mutex> guard(mutex_);
    snapshot = live_snapshot_;
    epoch = registry_epoch_;
  }
  const auto text = [](const std::string &id) -> seekdb_runtime_text_t {
    return {reinterpret_cast<const uint8_t *>(id.data()), static_cast<uint32_t>(id.size())};
  };
  std::vector<seekdb_runtime_sql_cast_t> casts;
  for (uint32_t i = 0; i < snapshot->extensions_.size(); ++i) {
    const auto &entry = snapshot->extensions_.at(i);
    if (!entry.info_ || entry.info_->spec_.kind_ != SEEKDB_PLUGIN_EXTENSION_CAST) continue;
    const auto &spec = entry.info_->spec_;
    casts.push_back({text(spec.source_type_id_), text(spec.target_type_id_),
                    static_cast<uint32_t>(spec.cast_context_), spec.cost_});
  }
  uint32_t selected = UINT32_MAX;
  const int status = seekdb_runtime_resolve_common_type(arguments.data(), count, casts.data(), casts.size(), &selected);
  if (status == SEEKDB_RUNTIME_NOT_FOUND) return OB_ENTRY_NOT_EXIST;
  if (status == SEEKDB_RUNTIME_AMBIGUOUS) return OB_ENTRY_EXIST;
  if (status == SEEKDB_RUNTIME_NO_MEMORY) return OB_ALLOCATE_MEMORY_FAILED;
  if (status != SEEKDB_RUNTIME_OK || selected >= count || !type_ids[selected])
    return status == SEEKDB_RUNTIME_INVALID ? OB_INVALID_ARGUMENT : OB_ERR_UNEXPECTED;
  common_type = type_ids[selected]; registry_epoch = epoch;
  return OB_SUCCESS;
} catch (const std::bad_alloc &) {
  common_type.clear(); registry_epoch = 0; return OB_ALLOCATE_MEMORY_FAILED;
} catch (...) {
  common_type.clear(); registry_epoch = 0; return OB_ERR_UNEXPECTED;
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
        for (uint32_t i = 0; i < live_snapshot_->extensions_.size(); ++i) {
          const auto &entry = live_snapshot_->extensions_.at(i);
          if (entry.info_ && SEEKDB_PLUGIN_EXTENSION_CAST == entry.info_->spec_.kind_ &&
              entry.info_->spec_.source_type_id_ == source_type_id &&
              entry.info_->spec_.target_type_id_ == target_type_id &&
              entry.info_->spec_.cast_context_ >= requested_context) {
            candidate.push_back(*entry.info_);
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
        for (uint32_t i = 0; i < live_snapshot_->extensions_.size(); ++i) {
          const auto &entry = live_snapshot_->extensions_.at(i);
          if (entry.info_ && kind == entry.info_->spec_.kind_ &&
              entry.info_->spec_.hook_point_ == hook_point) {
            candidate.push_back(*entry.info_);
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
      for (uint32_t i = 0; i < live_snapshot_->extensions_.size(); ++i) {
        const auto &entry = live_snapshot_->extensions_.at(i);
        if (entry.info_ && SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT == entry.info_->spec_.kind_ &&
            entry.info_->spec_.catalog_object_kind_ == object_kind &&
            entry.info_->spec_.schema_name_ == schema_name &&
            entry.info_->spec_.sql_name_ == sql_name) {
          candidate.push_back(*entry.info_);
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
