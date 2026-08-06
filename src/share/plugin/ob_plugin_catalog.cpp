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

#define USING_LOG_PREFIX SHARE

#include "share/plugin/ob_plugin_catalog.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <new>
#include <set>
#include <utility>

#include "lib/ob_errno.h"
#include "lib/time/ob_time_utility.h"
#include "share/plugin/ob_plugin_sql_catalog.h"

namespace oceanbase
{
namespace share
{
namespace plugin
{

using namespace oceanbase::common;

namespace
{

const char *PLUGIN_OPERATION_SEQUENCE = "plugin-operation";
const uint64_t MAX_DURABLE_GENERATION =
    static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
const size_t MAX_CATALOG_ERROR_BYTES = 4096;
const size_t MAX_CATALOG_PATH_BYTES = 4096;
const size_t MAX_DIGEST_TEXT_BYTES = sizeof("sha256:") - 1 + 64;

bool valid_exact_text(const std::string &value,
                      const size_t maximum,
                      const bool allow_empty = false)
{
  return (allow_empty || !value.empty()) && value.size() <= maximum &&
         value.find('\0') == std::string::npos;
}

bool identifier_char(const unsigned char c)
{
  return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
         (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
}

bool catalog_valid_identifier(const std::string &value, const bool allow_empty = false)
{
  bool valid = valid_exact_text(
      value, SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, allow_empty);
  for (size_t i = 0; valid && i < value.size(); ++i) {
    valid = identifier_char(static_cast<unsigned char>(value[i]));
  }
  return valid;
}

bool catalog_valid_digest(const std::string &value, const bool allow_empty = false)
{
  bool valid = allow_empty && value.empty();
  const size_t prefix_size = sizeof("sha256:") - 1;
  if (!valid && value.size() == prefix_size + 64 &&
      value.compare(0, prefix_size, "sha256:") == 0 &&
      value.find('\0') == std::string::npos) {
    valid = true;
    for (size_t i = prefix_size; valid && i < value.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(value[i]);
      valid = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    }
  }
  return valid;
}

bool valid_relative_path(const std::string &path)
{
  bool valid = valid_exact_text(path, MAX_CATALOG_PATH_BYTES) &&
               path[0] != '/' && path[0] != '\\' &&
               path.find(':') == std::string::npos;
  size_t segment_begin = 0;
  for (size_t i = 0; valid && i <= path.size(); ++i) {
    if (i == path.size() || path[i] == '/' || path[i] == '\\') {
      const size_t size = i - segment_begin;
      valid = size != 0 &&
              !(size == 1 && path[segment_begin] == '.') &&
              !(size == 2 && path[segment_begin] == '.' &&
                path[segment_begin + 1] == '.');
      segment_begin = i + 1;
    }
  }
  return valid;
}

bool catalog_same_version(const seekdb_plugin_semantic_version_t &left,
                          const seekdb_plugin_semantic_version_t &right)
{
  return left.major == right.major && left.minor == right.minor &&
         left.patch == right.patch;
}

bool catalog_same_version_range(const seekdb_plugin_version_range_t &left,
                                const seekdb_plugin_version_range_t &right)
{
  return catalog_same_version(left.minimum_inclusive,
                              right.minimum_inclusive) &&
         catalog_same_version(left.maximum_exclusive,
                              right.maximum_exclusive);
}

int catalog_compare_version(const seekdb_plugin_semantic_version_t &left,
                            const seekdb_plugin_semantic_version_t &right)
{
  int result = 0;
  if (left.major != right.major) {
    result = left.major < right.major ? -1 : 1;
  } else if (left.minor != right.minor) {
    result = left.minor < right.minor ? -1 : 1;
  } else if (left.patch != right.patch) {
    result = left.patch < right.patch ? -1 : 1;
  }
  return result;
}

bool catalog_unbounded_version(
    const seekdb_plugin_semantic_version_t &version)
{
  return version.major == 0 && version.minor == 0 && version.patch == 0;
}

bool catalog_valid_version_range(const seekdb_plugin_version_range_t &range)
{
  bool reserved_zero = true;
  for (size_t i = 0;
       reserved_zero && i < sizeof(range.reserved) / sizeof(range.reserved[0]);
       ++i) {
    reserved_zero = range.reserved[i] == 0;
  }
  const bool unbounded = catalog_unbounded_version(range.maximum_exclusive);
  const bool same_major =
      range.maximum_exclusive.major == range.minimum_inclusive.major;
  const bool next_major =
      range.minimum_inclusive.major < std::numeric_limits<uint32_t>::max() &&
      range.maximum_exclusive.major == range.minimum_inclusive.major + 1 &&
      range.maximum_exclusive.minor == 0 &&
      range.maximum_exclusive.patch == 0;
  return range.struct_size == sizeof(seekdb_plugin_version_range_t) &&
         range.minimum_inclusive.major != 0 && reserved_zero &&
         (unbounded ||
          (same_major && catalog_compare_version(
                             range.minimum_inclusive,
                             range.maximum_exclusive) < 0) ||
          next_major);
}

bool catalog_version_in_range(
    const seekdb_plugin_semantic_version_t &version,
    const seekdb_plugin_version_range_t &range)
{
  return version.major == range.minimum_inclusive.major &&
         catalog_compare_version(version, range.minimum_inclusive) >= 0 &&
         (catalog_unbounded_version(range.maximum_exclusive) ||
          catalog_compare_version(version, range.maximum_exclusive) < 0);
}

bool catalog_empty_version_range(const seekdb_plugin_version_range_t &range)
{
  bool reserved_zero = true;
  for (size_t i = 0;
       reserved_zero && i < sizeof(range.reserved) / sizeof(range.reserved[0]);
       ++i) {
    reserved_zero = range.reserved[i] == 0;
  }
  return range.struct_size == sizeof(seekdb_plugin_version_range_t) &&
         catalog_unbounded_version(range.minimum_inclusive) &&
         catalog_unbounded_version(range.maximum_exclusive) && reserved_zero;
}

bool catalog_valid_capabilities(const uint64_t capabilities)
{
  const uint64_t known_runtime_capabilities =
      SEEKDB_PLUGIN_CAPABILITY_THREAD_SAFE |
      SEEKDB_PLUGIN_CAPABILITY_MULTI_INSTANCE |
      SEEKDB_PLUGIN_CAPABILITY_SIDE_BY_SIDE_UPGRADE |
      SEEKDB_PLUGIN_CAPABILITY_PERSISTENT_DATA |
      SEEKDB_PLUGIN_CAPABILITY_TRANSACTIONAL_SERVICES;
  return (capabilities & ~known_runtime_capabilities) == 0;
}

bool valid_install_spec(const ObPluginPackageInstallSpec &spec)
{
  return valid_relative_path(spec.relative_path_) &&
         catalog_valid_identifier(spec.artifact_.plugin_id_) &&
         valid_exact_text(spec.artifact_.build_id_,
                          SEEKDB_PLUGIN_MAX_BUILD_ID_BYTES) &&
         catalog_valid_digest(spec.artifact_.package_digest_) &&
         spec.artifact_.package_version_.major != 0 &&
         spec.verification_level_ != ObPluginVerificationLevel::NOT_VERIFIED &&
         spec.verification_level_ <=
             ObPluginVerificationLevel::SIGNATURE_VERIFIED &&
         catalog_valid_identifier(spec.operator_id_, true) &&
         catalog_valid_identifier(spec.audit_id_, true);
}

bool valid_dependency(const ObPluginDependencySpec &dependency)
{
  const int consumer_kind = static_cast<int>(dependency.consumer_kind_);
  const int dependency_kind = static_cast<int>(dependency.dependency_kind_);
  const bool plugin_consumer =
      dependency.consumer_kind_ == ObPluginDependencyConsumerKind::PLUGIN;
  const bool valid_consumer_identity =
      plugin_consumer
          ? (!dependency.consumer_plugin_id_.empty() &&
             dependency.consumer_id_ == dependency.consumer_plugin_id_ &&
             dependency.consumer_generation_ != 0)
          : (dependency.consumer_plugin_id_.empty() &&
             dependency.consumer_generation_ == 0);
  const bool service_dependency =
      dependency.dependency_kind_ == ObPluginDependencyKind::SERVICE;
  const bool persistent_format_dependency =
      dependency.dependency_kind_ ==
      ObPluginDependencyKind::PERSISTENT_FORMAT;
  const bool valid_dependency_contract =
      service_dependency
          ? (dependency.service_abi_major_ != 0 &&
             catalog_valid_version_range(dependency.requested_version_) &&
             dependency.requested_version_.minimum_inclusive.major ==
                 dependency.service_abi_major_ &&
             catalog_valid_capabilities(dependency.required_capabilities_))
          : (dependency.service_abi_major_ == 0 &&
             dependency.required_capabilities_ == 0 &&
             (persistent_format_dependency
                  ? catalog_valid_version_range(dependency.requested_version_)
                  : catalog_empty_version_range(
                        dependency.requested_version_)));
  return consumer_kind >=
             static_cast<int>(ObPluginDependencyConsumerKind::PLUGIN) &&
         consumer_kind <=
             static_cast<int>(ObPluginDependencyConsumerKind::BACKGROUND_JOB) &&
         dependency_kind >= static_cast<int>(ObPluginDependencyKind::SERVICE) &&
         dependency_kind <=
             static_cast<int>(ObPluginDependencyKind::PERSISTENT_FORMAT) &&
         valid_exact_text(dependency.consumer_id_,
                          SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES) &&
         catalog_valid_identifier(dependency.consumer_plugin_id_, true) &&
         valid_consumer_identity &&
         catalog_valid_identifier(dependency.provider_plugin_id_) &&
         valid_exact_text(dependency.dependency_id_,
                          SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES) &&
         dependency.provider_generation_ != 0 && valid_dependency_contract &&
         (!plugin_consumer || service_dependency);
}

bool valid_activation_request(const ObPluginActivationRequest &request)
{
  const bool recovery =
      request.mode_ == ObPluginActivationMode::STARTUP_RECOVERY;
  return (request.mode_ == ObPluginActivationMode::ACTIVATE || recovery) &&
         valid_relative_path(request.relative_path_) &&
         catalog_valid_identifier(request.plugin_id_) &&
         valid_exact_text(request.build_id_,
                          SEEKDB_PLUGIN_MAX_BUILD_ID_BYTES) &&
         catalog_valid_digest(request.package_digest_) &&
         request.package_version_.major != 0 &&
         ((!recovery && request.expected_generation_ == 0 &&
           request.expected_runtime_incarnation_.empty() &&
           request.expected_operation_id_.empty()) ||
          (recovery && request.expected_generation_ != 0 &&
           catalog_valid_identifier(request.expected_runtime_incarnation_) &&
           catalog_valid_identifier(request.expected_operation_id_)));
}

bool valid_runtime_dependency(
    const ObPluginRuntimeServiceDependency &dependency)
{
  return catalog_valid_identifier(dependency.service_id_) &&
         catalog_valid_version_range(dependency.requested_version_) &&
         catalog_valid_identifier(dependency.provider_plugin_id_) &&
         dependency.provider_generation_ != 0 &&
         dependency.provider_version_.major != 0 &&
         dependency.provider_version_.major ==
             dependency.requested_version_.minimum_inclusive.major &&
         catalog_version_in_range(dependency.provider_version_,
                                  dependency.requested_version_) &&
         catalog_valid_capabilities(dependency.required_capabilities_);
}

bool valid_candidate_result(const ObPluginRuntimeActivationResult &result,
                            const std::string &plugin_id,
                            const uint64_t generation,
                            const std::string &runtime_incarnation,
                            const std::string &operation_id)
{
  bool valid = result.status_ == OB_SUCCESS &&
               result.generation_ == generation &&
               result.runtime_incarnation_ == runtime_incarnation &&
               result.operation_id_ == operation_id &&
               result.actual_state_ == ObPluginState::INITIALIZING &&
               result.phase_ == ObPluginActivationPhase::CATALOG_FINISH &&
               result.candidate_prepared_ &&
               result.services_.size() <= SEEKDB_PLUGIN_MAX_SERVICES &&
               result.extensions_.size() <= SEEKDB_PLUGIN_MAX_EXTENSIONS &&
               result.dependencies_.size() <= SEEKDB_PLUGIN_MAX_SERVICES;
  for (size_t i = 0; valid && i < result.services_.size(); ++i) {
    const ObPluginServiceInfo &service = result.services_[i];
    valid = catalog_valid_identifier(service.name_) && service.abi_major_ != 0 &&
            service.owner_plugin_id_ == plugin_id &&
            service.owner_generation_ == generation;
  }
  for (size_t i = 0; valid && i < result.extensions_.size(); ++i) {
    const ObPluginExtensionInfo &extension = result.extensions_[i];
    const ObPluginExtensionSpec &spec = extension.spec_;
    valid = spec.kind_ >= SEEKDB_PLUGIN_EXTENSION_TYPE &&
            spec.kind_ <= SEEKDB_PLUGIN_EXTENSION_CATALOG_OBJECT &&
            catalog_valid_identifier(spec.object_id_) &&
            valid_exact_text(spec.sql_name_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.physical_format_id_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.source_type_id_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.target_type_id_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.static_result_type_id_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.hook_point_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.catalog_object_kind_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.schema_name_,
                             SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES, true) &&
            valid_exact_text(spec.definition_digest_,
                             MAX_DIGEST_TEXT_BYTES, true) &&
            catalog_valid_identifier(spec.implementation_.service_id_, true) &&
            extension.owner_plugin_id_ == plugin_id &&
            extension.owner_generation_ == generation;
  }
  for (size_t i = 0; valid && i < result.dependencies_.size(); ++i) {
    valid = valid_runtime_dependency(result.dependencies_[i]);
  }
  return valid;
}

bool valid_complete_result(const ObPluginRuntimeActivationResult &result,
                           const uint64_t generation,
                           const std::string &runtime_incarnation,
                           const std::string &operation_id)
{
  return result.status_ == OB_SUCCESS && result.generation_ == generation &&
         result.runtime_incarnation_ == runtime_incarnation &&
         result.operation_id_ == operation_id &&
         result.actual_state_ == ObPluginState::ACTIVE &&
         result.phase_ == ObPluginActivationPhase::COMPLETE &&
         result.candidate_prepared_;
}

bool valid_disable_runtime_result(const ObPluginRuntimeDisableResult &result)
{
  const bool phase_valid =
      result.phase_ >= ObPluginDisablePhase::NONE &&
      result.phase_ <= ObPluginDisablePhase::COMPLETE;
  const bool stopped = result.status_ == OB_SUCCESS &&
                       result.actual_state_ == ObPluginState::STOPPED &&
                       result.phase_ == ObPluginDisablePhase::COMPLETE &&
                       result.stop_entered_;
  const bool safe_abort = result.status_ != OB_SUCCESS &&
                          result.actual_state_ == ObPluginState::ACTIVE &&
                          !result.stop_entered_ &&
                          (result.phase_ == ObPluginDisablePhase::NONE ||
                           result.phase_ == ObPluginDisablePhase::QUIESCE);
  const bool drain_failure = result.status_ != OB_SUCCESS &&
                             result.actual_state_ ==
                                 ObPluginState::QUIESCING &&
                             !result.stop_entered_ &&
                             result.phase_ == ObPluginDisablePhase::DRAIN;
  const bool unsafe_stop_failure =
      result.status_ != OB_SUCCESS && result.stop_entered_ &&
      result.phase_ >= ObPluginDisablePhase::STOP &&
      result.phase_ <= ObPluginDisablePhase::MARK_STOPPED &&
      (result.actual_state_ == ObPluginState::QUIESCING ||
       result.actual_state_ == ObPluginState::BLOCKED);
  return phase_valid &&
         (stopped || safe_abort || drain_failure || unsafe_stop_failure);
}

int bind_string(ObPluginSqlBinder &binder, const std::string &value)
{
  return binder.bind_text(value.c_str(), static_cast<int>(value.size()));
}

std::string read_string(ObPluginSqlRowReader &reader, const int column)
{
  int length = 0;
  const char *value = reader.get_text(column, &length);
  return nullptr == value ? std::string() : std::string(value, length);
}

std::string bounded_error(const std::string &error)
{
  return error.size() <= MAX_CATALOG_ERROR_BYTES
             ? error
             : error.substr(0, MAX_CATALOG_ERROR_BYTES);
}

int query_count(ObPluginSqlConnection &connection,
                const char *sql,
                const std::function<int(ObPluginSqlBinder &)> &binder,
                int64_t &count)
{
  count = 0;
  return connection.query(
      sql, binder,
      [&](ObPluginSqlRowReader &reader) {
        count = reader.get_int64(0);
        return OB_ITER_END;
      });
}

int insert_service(ObPluginSqlConnection &connection,
                   const std::string &plugin_id,
                   const uint64_t generation,
                   const ObPluginServiceInfo &service)
{
  return connection.execute(
      "INSERT INTO __all_plugin_service("
      "plugin_id,generation,service_id,abi_major,abi_minor,abi_patch,"
      "capabilities) VALUES(?,?,?,?,?,?,?)",
      [&](ObPluginSqlBinder &binder) {
        int ret = bind_string(binder, plugin_id);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(static_cast<int64_t>(generation));
        if (OB_SUCCESS == ret) ret = bind_string(binder, service.name_);
        if (OB_SUCCESS == ret) ret = binder.bind_int64(service.abi_major_);
        if (OB_SUCCESS == ret) ret = binder.bind_int64(service.abi_minor_);
        if (OB_SUCCESS == ret) ret = binder.bind_int64(service.abi_patch_);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              static_cast<int64_t>(service.capabilities_));
        return ret;
      });
}

int insert_extension(ObPluginSqlConnection &connection,
                     const std::string &plugin_id,
                     const uint64_t generation,
                     const ObPluginExtensionInfo &extension)
{
  const ObPluginExtensionSpec &spec = extension.spec_;
  static const char SQL[] =
      "INSERT INTO __all_plugin_extension("
      "plugin_id,generation,kind,object_id,sql_name,physical_format_id,"
      "source_type_id,target_type_id,static_result_type_id,hook_point,"
      "catalog_object_kind,schema_name,definition_digest,"
      "physical_format_version,minimum_arity,maximum_arity,cast_context,cost,"
      "priority,flags,implementation_service_id,"
      "implementation_min_version_major,implementation_min_version_minor,"
      "implementation_min_version_patch,implementation_max_version_major,"
      "implementation_max_version_minor,implementation_max_version_patch,"
      "required_capabilities) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,"
      "?,?,?,?,?,?,?,?)";
  return connection.execute(
      SQL,
      [&](ObPluginSqlBinder &binder) {
        int ret = bind_string(binder, plugin_id);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(static_cast<int64_t>(generation));
        if (OB_SUCCESS == ret) ret = binder.bind_int(spec.kind_);
        if (OB_SUCCESS == ret) ret = bind_string(binder, spec.object_id_);
        if (OB_SUCCESS == ret) ret = bind_string(binder, spec.sql_name_);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.physical_format_id_);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.source_type_id_);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.target_type_id_);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.static_result_type_id_);
        if (OB_SUCCESS == ret) ret = bind_string(binder, spec.hook_point_);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.catalog_object_kind_);
        if (OB_SUCCESS == ret) ret = bind_string(binder, spec.schema_name_);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.definition_digest_);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(spec.physical_format_version_);
        if (OB_SUCCESS == ret) ret = binder.bind_int64(spec.minimum_arity_);
        if (OB_SUCCESS == ret) ret = binder.bind_int64(spec.maximum_arity_);
        if (OB_SUCCESS == ret) ret = binder.bind_int(spec.cast_context_);
        if (OB_SUCCESS == ret) ret = binder.bind_int64(spec.cost_);
        if (OB_SUCCESS == ret) ret = binder.bind_int(spec.priority_);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(static_cast<int64_t>(spec.flags_));
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, spec.implementation_.service_id_);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              spec.implementation_.version_range_.minimum_inclusive.major);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              spec.implementation_.version_range_.minimum_inclusive.minor);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              spec.implementation_.version_range_.minimum_inclusive.patch);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              spec.implementation_.version_range_.maximum_exclusive.major);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              spec.implementation_.version_range_.maximum_exclusive.minor);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              spec.implementation_.version_range_.maximum_exclusive.patch);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(static_cast<int64_t>(
              spec.implementation_.required_capabilities_));
        return ret;
      });
}

int insert_runtime_dependency(
    ObPluginSqlConnection &connection,
    const std::string &consumer_plugin_id,
    const uint64_t consumer_generation,
    const ObPluginRuntimeServiceDependency &dependency)
{
  static const char SQL[] =
      "INSERT INTO __all_plugin_dependency("
      "consumer_kind,consumer_id,consumer_plugin_id,consumer_generation,"
      "provider_plugin_id,provider_generation,dependency_kind,dependency_id,"
      "service_abi_major,requested_min_version_major,requested_min_version_minor,"
      "requested_min_version_patch,requested_max_version_major,"
      "requested_max_version_minor,requested_max_version_patch,"
      "required_capabilities,optional,provider_version_major,"
      "provider_version_minor,provider_version_patch) "
      "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
  return connection.execute(
      SQL,
      [&](ObPluginSqlBinder &binder) {
        int ret = binder.bind_int(static_cast<int32_t>(
            ObPluginDependencyConsumerKind::PLUGIN));
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, consumer_plugin_id);
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, consumer_plugin_id);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              static_cast<int64_t>(consumer_generation));
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, dependency.provider_plugin_id_);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              static_cast<int64_t>(dependency.provider_generation_));
        if (OB_SUCCESS == ret)
          ret = binder.bind_int(
              static_cast<int32_t>(ObPluginDependencyKind::SERVICE));
        if (OB_SUCCESS == ret)
          ret = bind_string(binder, dependency.service_id_);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(dependency.provider_version_.major);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              dependency.requested_version_.minimum_inclusive.major);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              dependency.requested_version_.minimum_inclusive.minor);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              dependency.requested_version_.minimum_inclusive.patch);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              dependency.requested_version_.maximum_exclusive.major);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              dependency.requested_version_.maximum_exclusive.minor);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(
              dependency.requested_version_.maximum_exclusive.patch);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(static_cast<int64_t>(
              dependency.required_capabilities_));
        if (OB_SUCCESS == ret)
          ret = binder.bind_int(dependency.optional_ ? 1 : 0);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(dependency.provider_version_.major);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(dependency.provider_version_.minor);
        if (OB_SUCCESS == ret)
          ret = binder.bind_int64(dependency.provider_version_.patch);
        return ret;
      });
}

struct CatalogDependencyRequirement
{
  CatalogDependencyRequirement()
      : kind_(ObPluginDependencyKind::SERVICE), dependency_id_(),
        service_abi_major_(0), requested_version_(),
        required_capabilities_(0)
  {
    std::memset(&requested_version_, 0, sizeof(requested_version_));
    requested_version_.struct_size = sizeof(requested_version_);
  }

  ObPluginDependencyKind kind_;
  std::string dependency_id_;
  uint32_t service_abi_major_;
  seekdb_plugin_version_range_t requested_version_;
  uint64_t required_capabilities_;
};

struct CatalogStableDependency
{
  CatalogStableDependency()
      : consumer_kind_(ObPluginDependencyConsumerKind::CATALOG_OBJECT),
        consumer_id_(), consumer_plugin_id_(), consumer_generation_(0),
        old_provider_generation_(0), requirement_(), resolved_version_{0, 0, 0}
  {}

  ObPluginDependencyConsumerKind consumer_kind_;
  std::string consumer_id_;
  std::string consumer_plugin_id_;
  uint64_t consumer_generation_;
  uint64_t old_provider_generation_;
  CatalogDependencyRequirement requirement_;
  seekdb_plugin_semantic_version_t resolved_version_;
};

struct CatalogReadyDependency
{
  CatalogReadyDependency()
      : provider_plugin_id_(), provider_generation_(0), requirement_(),
        expected_version_{0, 0, 0}
  {}

  std::string provider_plugin_id_;
  uint64_t provider_generation_;
  CatalogDependencyRequirement requirement_;
  seekdb_plugin_semantic_version_t expected_version_;
};

struct CatalogDurableRuntimeDependency
{
  CatalogDurableRuntimeDependency()
      : dependency_(), service_abi_major_(0)
  {}

  ObPluginRuntimeServiceDependency dependency_;
  uint32_t service_abi_major_;
};

bool valid_catalog_requirement(
    const CatalogDependencyRequirement &requirement)
{
  const bool service = requirement.kind_ == ObPluginDependencyKind::SERVICE;
  const bool persistent_format =
      requirement.kind_ == ObPluginDependencyKind::PERSISTENT_FORMAT;
  return valid_exact_text(requirement.dependency_id_,
                          SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES) &&
         (service
              ? (requirement.service_abi_major_ != 0 &&
                 catalog_valid_version_range(
                     requirement.requested_version_) &&
                 requirement.requested_version_.minimum_inclusive.major ==
                     requirement.service_abi_major_ &&
                 catalog_valid_capabilities(
                     requirement.required_capabilities_))
              : (requirement.service_abi_major_ == 0 &&
                 requirement.required_capabilities_ == 0 &&
                 (persistent_format
                      ? catalog_valid_version_range(
                            requirement.requested_version_)
                      : (requirement.kind_ ==
                             ObPluginDependencyKind::EXTENSION_OBJECT &&
                         catalog_empty_version_range(
                             requirement.requested_version_)))));
}

bool catalog_same_service(const ObPluginServiceInfo &left,
                          const ObPluginServiceInfo &right)
{
  return left.name_ == right.name_ && left.abi_major_ == right.abi_major_ &&
         left.abi_minor_ == right.abi_minor_ &&
         left.abi_patch_ == right.abi_patch_ &&
         left.capabilities_ == right.capabilities_ &&
         left.owner_plugin_id_ == right.owner_plugin_id_ &&
         left.owner_generation_ == right.owner_generation_;
}

bool catalog_same_extension(const ObPluginExtensionInfo &left,
                            const ObPluginExtensionInfo &right)
{
  const ObPluginExtensionSpec &lhs = left.spec_;
  const ObPluginExtensionSpec &rhs = right.spec_;
  return lhs.kind_ == rhs.kind_ && lhs.object_id_ == rhs.object_id_ &&
         lhs.sql_name_ == rhs.sql_name_ &&
         lhs.physical_format_id_ == rhs.physical_format_id_ &&
         lhs.source_type_id_ == rhs.source_type_id_ &&
         lhs.target_type_id_ == rhs.target_type_id_ &&
         lhs.static_result_type_id_ == rhs.static_result_type_id_ &&
         lhs.hook_point_ == rhs.hook_point_ &&
         lhs.catalog_object_kind_ == rhs.catalog_object_kind_ &&
         lhs.schema_name_ == rhs.schema_name_ &&
         lhs.definition_digest_ == rhs.definition_digest_ &&
         lhs.physical_format_version_ == rhs.physical_format_version_ &&
         lhs.minimum_arity_ == rhs.minimum_arity_ &&
         lhs.maximum_arity_ == rhs.maximum_arity_ &&
         lhs.cast_context_ == rhs.cast_context_ && lhs.cost_ == rhs.cost_ &&
         lhs.priority_ == rhs.priority_ && lhs.flags_ == rhs.flags_ &&
         lhs.implementation_.service_id_ ==
             rhs.implementation_.service_id_ &&
         catalog_same_version_range(
             lhs.implementation_.version_range_,
             rhs.implementation_.version_range_) &&
         lhs.implementation_.required_capabilities_ ==
             rhs.implementation_.required_capabilities_ &&
         left.owner_plugin_id_ == right.owner_plugin_id_ &&
         left.owner_generation_ == right.owner_generation_;
}

bool candidate_resolves_requirement(
    const ObPluginRuntimeActivationResult &candidate,
    const CatalogDependencyRequirement &requirement,
    seekdb_plugin_semantic_version_t &resolved_version)
{
  uint64_t matches = 0;
  resolved_version = {0, 0, 0};
  if (requirement.kind_ == ObPluginDependencyKind::SERVICE) {
    for (size_t i = 0; i < candidate.services_.size(); ++i) {
      const ObPluginServiceInfo &service = candidate.services_[i];
      const seekdb_plugin_semantic_version_t version = {
          service.abi_major_, service.abi_minor_, service.abi_patch_};
      if (service.name_ == requirement.dependency_id_ &&
          service.abi_major_ == requirement.service_abi_major_ &&
          catalog_version_in_range(version, requirement.requested_version_) &&
          (service.capabilities_ & requirement.required_capabilities_) ==
              requirement.required_capabilities_) {
        ++matches;
        resolved_version = version;
      }
    }
  } else if (requirement.kind_ ==
             ObPluginDependencyKind::EXTENSION_OBJECT) {
    for (size_t i = 0; i < candidate.extensions_.size(); ++i) {
      if (candidate.extensions_[i].spec_.object_id_ ==
          requirement.dependency_id_) {
        ++matches;
      }
    }
  } else if (requirement.kind_ ==
             ObPluginDependencyKind::PERSISTENT_FORMAT) {
    for (size_t i = 0; i < candidate.extensions_.size(); ++i) {
      const ObPluginExtensionSpec &extension = candidate.extensions_[i].spec_;
      const seekdb_plugin_semantic_version_t format_version = {
          extension.physical_format_version_, 0, 0};
      if (extension.kind_ == SEEKDB_PLUGIN_EXTENSION_TYPE &&
          extension.physical_format_id_ == requirement.dependency_id_ &&
          catalog_version_in_range(format_version,
                                   requirement.requested_version_)) {
        ++matches;
        resolved_version = format_version;
      }
    }
  }
  return matches == 1;
}

int durable_provider_resolves_requirement(
    ObPluginSqlConnection &connection,
    const std::string &provider_plugin_id,
    const uint64_t provider_generation,
    const CatalogDependencyRequirement &requirement,
    seekdb_plugin_semantic_version_t &resolved_version,
    bool &resolved)
{
  int ret = OB_SUCCESS;
  int64_t matches = 0;
  resolved = false;
  resolved_version = {0, 0, 0};
  if (requirement.kind_ == ObPluginDependencyKind::SERVICE) {
    ret = connection.query(
        "SELECT abi_minor,abi_patch,capabilities FROM __all_plugin_service "
        "WHERE plugin_id=? AND generation=? AND service_id=? AND abi_major=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = bind_string(binder, provider_plugin_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                static_cast<int64_t>(provider_generation));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, requirement.dependency_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(requirement.service_abi_major_);
          return bind_ret;
        },
        [&](ObPluginSqlRowReader &reader) {
          const seekdb_plugin_semantic_version_t version = {
              requirement.service_abi_major_,
              static_cast<uint32_t>(reader.get_int64(0)),
              static_cast<uint32_t>(reader.get_int64(1))};
          const uint64_t capabilities =
              static_cast<uint64_t>(reader.get_int64(2));
          if (catalog_version_in_range(version,
                                       requirement.requested_version_) &&
              (capabilities & requirement.required_capabilities_) ==
                  requirement.required_capabilities_) {
            ++matches;
            resolved_version = version;
          }
          return OB_SUCCESS;
        });
  } else if (requirement.kind_ ==
             ObPluginDependencyKind::EXTENSION_OBJECT) {
    ret = query_count(
        connection,
        "SELECT COUNT(*) FROM __all_plugin_extension WHERE plugin_id=? "
        "AND generation=? AND object_id=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = bind_string(binder, provider_plugin_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                static_cast<int64_t>(provider_generation));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, requirement.dependency_id_);
          return bind_ret;
        },
        matches);
  } else if (requirement.kind_ ==
             ObPluginDependencyKind::PERSISTENT_FORMAT) {
    ret = connection.query(
        "SELECT physical_format_version FROM __all_plugin_extension "
        "WHERE plugin_id=? AND generation=? AND kind=? AND "
        "physical_format_id=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = bind_string(binder, provider_plugin_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                static_cast<int64_t>(provider_generation));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(SEEKDB_PLUGIN_EXTENSION_TYPE);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, requirement.dependency_id_);
          return bind_ret;
        },
        [&](ObPluginSqlRowReader &reader) {
          const seekdb_plugin_semantic_version_t version = {
              static_cast<uint32_t>(reader.get_int64(0)), 0, 0};
          if (catalog_version_in_range(version,
                                       requirement.requested_version_)) {
            ++matches;
            resolved_version = version;
          }
          return OB_SUCCESS;
        });
  } else {
    ret = OB_INVALID_DATA;
  }
  if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
  if (OB_SUCCESS == ret) resolved = matches == 1;
  return ret;
}

int load_durable_services(ObPluginSqlConnection &connection,
                          const std::string &plugin_id,
                          const uint64_t generation,
                          std::vector<ObPluginServiceInfo> &services)
{
  services.clear();
  int ret = connection.query(
      "SELECT service_id,abi_major,abi_minor,abi_patch,capabilities "
      "FROM __all_plugin_service WHERE plugin_id=? AND generation=?",
      [&](ObPluginSqlBinder &binder) {
        int bind_ret = bind_string(binder, plugin_id);
        if (OB_SUCCESS == bind_ret)
          bind_ret = binder.bind_int64(static_cast<int64_t>(generation));
        return bind_ret;
      },
      [&](ObPluginSqlRowReader &reader) {
        ObPluginServiceInfo service;
        service.name_ = read_string(reader, 0);
        service.abi_major_ = static_cast<uint32_t>(reader.get_int64(1));
        service.abi_minor_ = static_cast<uint32_t>(reader.get_int64(2));
        service.abi_patch_ = static_cast<uint32_t>(reader.get_int64(3));
        service.capabilities_ =
            static_cast<uint64_t>(reader.get_int64(4));
        service.owner_plugin_id_ = plugin_id;
        service.owner_generation_ = generation;
        services.push_back(service);
        return OB_SUCCESS;
      });
  return OB_ENTRY_NOT_EXIST == ret ? OB_SUCCESS : ret;
}

int load_durable_extensions(ObPluginSqlConnection &connection,
                            const std::string &plugin_id,
                            const uint64_t generation,
                            std::vector<ObPluginExtensionInfo> &extensions)
{
  extensions.clear();
  int ret = connection.query(
      "SELECT kind,object_id,sql_name,physical_format_id,source_type_id,"
      "target_type_id,static_result_type_id,hook_point,catalog_object_kind,"
      "schema_name,definition_digest,physical_format_version,minimum_arity,"
      "maximum_arity,cast_context,cost,priority,flags,implementation_service_id,"
      "implementation_min_version_major,implementation_min_version_minor,"
      "implementation_min_version_patch,implementation_max_version_major,"
      "implementation_max_version_minor,implementation_max_version_patch,"
      "required_capabilities FROM __all_plugin_extension WHERE plugin_id=? "
      "AND generation=?",
      [&](ObPluginSqlBinder &binder) {
        int bind_ret = bind_string(binder, plugin_id);
        if (OB_SUCCESS == bind_ret)
          bind_ret = binder.bind_int64(static_cast<int64_t>(generation));
        return bind_ret;
      },
      [&](ObPluginSqlRowReader &reader) {
        ObPluginExtensionInfo extension;
        ObPluginExtensionSpec &spec = extension.spec_;
        spec.kind_ = static_cast<seekdb_plugin_extension_kind_t>(
            reader.get_int(0));
        spec.object_id_ = read_string(reader, 1);
        spec.sql_name_ = read_string(reader, 2);
        spec.physical_format_id_ = read_string(reader, 3);
        spec.source_type_id_ = read_string(reader, 4);
        spec.target_type_id_ = read_string(reader, 5);
        spec.static_result_type_id_ = read_string(reader, 6);
        spec.hook_point_ = read_string(reader, 7);
        spec.catalog_object_kind_ = read_string(reader, 8);
        spec.schema_name_ = read_string(reader, 9);
        spec.definition_digest_ = read_string(reader, 10);
        spec.physical_format_version_ =
            static_cast<uint32_t>(reader.get_int64(11));
        spec.minimum_arity_ = static_cast<uint32_t>(reader.get_int64(12));
        spec.maximum_arity_ = static_cast<uint32_t>(reader.get_int64(13));
        spec.cast_context_ = static_cast<seekdb_plugin_cast_context_t>(
            reader.get_int(14));
        spec.cost_ = static_cast<uint32_t>(reader.get_int64(15));
        spec.priority_ = reader.get_int(16);
        spec.flags_ = static_cast<uint64_t>(reader.get_int64(17));
        spec.implementation_.service_id_ = read_string(reader, 18);
        spec.implementation_.version_range_.minimum_inclusive = {
            static_cast<uint32_t>(reader.get_int64(19)),
            static_cast<uint32_t>(reader.get_int64(20)),
            static_cast<uint32_t>(reader.get_int64(21))};
        spec.implementation_.version_range_.maximum_exclusive = {
            static_cast<uint32_t>(reader.get_int64(22)),
            static_cast<uint32_t>(reader.get_int64(23)),
            static_cast<uint32_t>(reader.get_int64(24))};
        spec.implementation_.required_capabilities_ =
            static_cast<uint64_t>(reader.get_int64(25));
        extension.owner_plugin_id_ = plugin_id;
        extension.owner_generation_ = generation;
        extensions.push_back(extension);
        return OB_SUCCESS;
      });
  return OB_ENTRY_NOT_EXIST == ret ? OB_SUCCESS : ret;
}

int load_durable_runtime_dependencies(
    ObPluginSqlConnection &connection,
    const std::string &consumer_plugin_id,
    const uint64_t consumer_generation,
    std::vector<CatalogDurableRuntimeDependency> &dependencies)
{
  dependencies.clear();
  int ret = connection.query(
      "SELECT consumer_id,dependency_kind,provider_plugin_id,"
      "provider_generation,dependency_id,"
      "service_abi_major,requested_min_version_major,"
      "requested_min_version_minor,requested_min_version_patch,"
      "requested_max_version_major,requested_max_version_minor,"
      "requested_max_version_patch,required_capabilities,optional,"
      "provider_version_major,provider_version_minor,provider_version_patch "
      "FROM __all_plugin_dependency WHERE consumer_kind=? AND "
      "consumer_plugin_id=? AND consumer_generation=?",
      [&](ObPluginSqlBinder &binder) {
        int bind_ret = binder.bind_int(static_cast<int32_t>(
            ObPluginDependencyConsumerKind::PLUGIN));
        if (OB_SUCCESS == bind_ret)
          bind_ret = bind_string(binder, consumer_plugin_id);
        if (OB_SUCCESS == bind_ret)
          bind_ret = binder.bind_int64(
              static_cast<int64_t>(consumer_generation));
        return bind_ret;
      },
      [&](ObPluginSqlRowReader &reader) {
        if (read_string(reader, 0) != consumer_plugin_id ||
            reader.get_int(1) != static_cast<int32_t>(
                                     ObPluginDependencyKind::SERVICE)) {
          return OB_INVALID_DATA;
        }
        CatalogDurableRuntimeDependency durable;
        durable.dependency_.provider_plugin_id_ = read_string(reader, 2);
        durable.dependency_.provider_generation_ =
            static_cast<uint64_t>(reader.get_int64(3));
        durable.dependency_.service_id_ = read_string(reader, 4);
        durable.service_abi_major_ =
            static_cast<uint32_t>(reader.get_int64(5));
        durable.dependency_.requested_version_.minimum_inclusive = {
            static_cast<uint32_t>(reader.get_int64(6)),
            static_cast<uint32_t>(reader.get_int64(7)),
            static_cast<uint32_t>(reader.get_int64(8))};
        durable.dependency_.requested_version_.maximum_exclusive = {
            static_cast<uint32_t>(reader.get_int64(9)),
            static_cast<uint32_t>(reader.get_int64(10)),
            static_cast<uint32_t>(reader.get_int64(11))};
        durable.dependency_.required_capabilities_ =
            static_cast<uint64_t>(reader.get_int64(12));
        durable.dependency_.optional_ = 0 != reader.get_int(13);
        durable.dependency_.provider_version_ = {
            static_cast<uint32_t>(reader.get_int64(14)),
            static_cast<uint32_t>(reader.get_int64(15)),
            static_cast<uint32_t>(reader.get_int64(16))};
        dependencies.push_back(durable);
        return OB_SUCCESS;
      });
  return OB_ENTRY_NOT_EXIST == ret ? OB_SUCCESS : ret;
}

bool same_runtime_dependency_contract(
    const CatalogDurableRuntimeDependency &durable,
    const ObPluginRuntimeServiceDependency &candidate)
{
  return durable.dependency_.service_id_ == candidate.service_id_ &&
         durable.service_abi_major_ == candidate.provider_version_.major &&
         catalog_same_version_range(durable.dependency_.requested_version_,
                                    candidate.requested_version_) &&
         durable.dependency_.required_capabilities_ ==
             candidate.required_capabilities_ &&
         durable.dependency_.optional_ == candidate.optional_ &&
         durable.dependency_.provider_plugin_id_ ==
             candidate.provider_plugin_id_;
}

template <typename Durable, typename Candidate, typename Equal>
bool catalog_same_unordered_set(const std::vector<Durable> &durable,
                                const std::vector<Candidate> &candidate,
                                Equal equal)
{
  bool same = durable.size() == candidate.size();
  std::vector<bool> matched(candidate.size(), false);
  for (size_t i = 0; same && i < durable.size(); ++i) {
    size_t match = candidate.size();
    for (size_t j = 0; match == candidate.size() && j < candidate.size(); ++j) {
      if (!matched[j] && equal(durable[i], candidate[j])) match = j;
    }
    if (match == candidate.size()) {
      same = false;
    } else {
      matched[match] = true;
    }
  }
  return same;
}

int validate_and_rebind_stable_dependencies(
    ObPluginSqlConnection &connection,
    const std::string &provider_plugin_id,
    const uint64_t new_provider_generation,
    const ObPluginRuntimeActivationResult &candidate,
    std::string &error)
{
  int ret = OB_SUCCESS;
  bool invalid_consumer_identity = false;
  std::vector<CatalogStableDependency> dependencies;
  ret = connection.query(
      "SELECT consumer_kind,consumer_id,consumer_plugin_id,"
      "consumer_generation,provider_generation,dependency_kind,dependency_id,"
      "service_abi_major,requested_min_version_major,"
      "requested_min_version_minor,requested_min_version_patch,"
      "requested_max_version_major,requested_max_version_minor,"
      "requested_max_version_patch,required_capabilities,optional "
      "FROM __all_plugin_dependency WHERE provider_plugin_id=? AND "
      "consumer_kind<>?",
      [&](ObPluginSqlBinder &binder) {
        int bind_ret = bind_string(binder, provider_plugin_id);
        if (OB_SUCCESS == bind_ret)
          bind_ret = binder.bind_int(static_cast<int32_t>(
              ObPluginDependencyConsumerKind::PLUGIN));
        return bind_ret;
      },
      [&](ObPluginSqlRowReader &reader) {
        CatalogStableDependency dependency;
        const int32_t consumer_kind = reader.get_int(0);
        const int64_t consumer_generation = reader.get_int64(3);
        const int64_t old_provider_generation = reader.get_int64(4);
        const int32_t optional = reader.get_int(15);
        dependency.consumer_kind_ = static_cast<
            ObPluginDependencyConsumerKind>(consumer_kind);
        dependency.consumer_id_ = read_string(reader, 1);
        dependency.consumer_plugin_id_ = read_string(reader, 2);
        // Stable consumers are catalog-owned identities, never plugin runtime
        // generations.  Validate the persisted row before resolving the new
        // provider so corruption cannot be mistaken for metadata to repair.
        if (consumer_kind < static_cast<int32_t>(
                                ObPluginDependencyConsumerKind::CATALOG_OBJECT) ||
            consumer_kind > static_cast<int32_t>(
                                ObPluginDependencyConsumerKind::BACKGROUND_JOB) ||
            !valid_exact_text(dependency.consumer_id_,
                              SEEKDB_PLUGIN_MAX_IDENTIFIER_BYTES) ||
            !dependency.consumer_plugin_id_.empty() ||
            consumer_generation != 0 || old_provider_generation <= 0 ||
            optional != 0) {
          invalid_consumer_identity = true;
          return OB_INVALID_DATA;
        }
        dependency.consumer_generation_ = 0;
        dependency.old_provider_generation_ =
            static_cast<uint64_t>(old_provider_generation);
        dependency.requirement_.kind_ =
            static_cast<ObPluginDependencyKind>(reader.get_int(5));
        dependency.requirement_.dependency_id_ = read_string(reader, 6);
        dependency.requirement_.service_abi_major_ =
            static_cast<uint32_t>(reader.get_int64(7));
        dependency.requirement_.requested_version_.minimum_inclusive = {
            static_cast<uint32_t>(reader.get_int64(8)),
            static_cast<uint32_t>(reader.get_int64(9)),
            static_cast<uint32_t>(reader.get_int64(10))};
        dependency.requirement_.requested_version_.maximum_exclusive = {
            static_cast<uint32_t>(reader.get_int64(11)),
            static_cast<uint32_t>(reader.get_int64(12)),
            static_cast<uint32_t>(reader.get_int64(13))};
        dependency.requirement_.required_capabilities_ =
            static_cast<uint64_t>(reader.get_int64(14));
        if (!valid_catalog_requirement(dependency.requirement_)) {
          return OB_INVALID_DATA;
        }
        if (!candidate_resolves_requirement(
                candidate, dependency.requirement_,
                dependency.resolved_version_)) {
          return OB_STATE_NOT_MATCH;
        }
        dependencies.push_back(dependency);
        return OB_SUCCESS;
      });
  if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
  if (OB_STATE_NOT_MATCH == ret) {
    error = "plugin candidate no longer satisfies a stable durable dependency";
  } else if (OB_INVALID_DATA == ret) {
    error = invalid_consumer_identity
                ? "stable durable dependency consumer identity is invalid"
                : "stable durable dependency contract is invalid";
  }

  for (size_t i = 0; OB_SUCCESS == ret && i < dependencies.size(); ++i) {
    const CatalogStableDependency &dependency = dependencies[i];
    int64_t affected_rows = 0;
    ret = connection.execute(
        "UPDATE __all_plugin_dependency SET provider_generation=?,"
        "provider_version_major=?,provider_version_minor=?,"
        "provider_version_patch=? WHERE consumer_kind=? AND consumer_id=? "
        "AND consumer_plugin_id=? AND consumer_generation=? AND "
        "provider_plugin_id=? AND provider_generation=? AND dependency_kind=? "
        "AND dependency_id=? AND service_abi_major=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int64(
              static_cast<int64_t>(new_provider_generation));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(dependency.resolved_version_.major);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(dependency.resolved_version_.minor);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(dependency.resolved_version_.patch);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(dependency.consumer_kind_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.consumer_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.consumer_plugin_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.consumer_generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, provider_plugin_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.old_provider_generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(dependency.requirement_.kind_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(
                binder, dependency.requirement_.dependency_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requirement_.service_abi_major_);
          return bind_ret;
        },
        &affected_rows);
    if (OB_SUCCESS != ret) {
      error = "stable plugin dependency rebind failed; durable edge may conflict";
    } else if (affected_rows != 1) {
      ret = OB_STATE_NOT_MATCH;
      error = "stable plugin dependency changed during generation rebind";
    }
  }
  return ret;
}

int validate_and_rebind_exact_replay(
    ObPluginSqlConnection &connection,
    const std::string &consumer_plugin_id,
    const uint64_t consumer_generation,
    const ObPluginRuntimeActivationResult &candidate,
    std::string &error)
{
  int ret = OB_SUCCESS;
  std::vector<ObPluginServiceInfo> durable_services;
  std::vector<ObPluginExtensionInfo> durable_extensions;
  std::vector<CatalogDurableRuntimeDependency> durable_dependencies;
  if (OB_FAIL(load_durable_services(connection, consumer_plugin_id,
                                    consumer_generation,
                                    durable_services))) {
    error = "cannot read durable services for exact activation replay";
  } else if (!catalog_same_unordered_set(
                 durable_services, candidate.services_,
                 [](const ObPluginServiceInfo &left,
                    const ObPluginServiceInfo &right) {
                   return catalog_same_service(left, right);
                 })) {
    ret = OB_INVALID_DATA;
    error = "exact activation replay changed its durable service set";
  } else if (OB_FAIL(load_durable_extensions(
                 connection, consumer_plugin_id, consumer_generation,
                 durable_extensions))) {
    error = "cannot read durable extensions for exact activation replay";
  } else if (!catalog_same_unordered_set(
                 durable_extensions, candidate.extensions_,
                 [](const ObPluginExtensionInfo &left,
                    const ObPluginExtensionInfo &right) {
                   return catalog_same_extension(left, right);
                 })) {
    ret = OB_INVALID_DATA;
    error = "exact activation replay changed its durable extension set";
  } else if (OB_FAIL(load_durable_runtime_dependencies(
                 connection, consumer_plugin_id, consumer_generation,
                 durable_dependencies))) {
    error = "cannot read durable dependencies for exact activation replay";
  }

  std::vector<bool> matched(candidate.dependencies_.size(), false);
  if (OB_SUCCESS == ret &&
      durable_dependencies.size() != candidate.dependencies_.size()) {
    ret = OB_INVALID_DATA;
    error = "exact activation replay changed its durable dependency set";
  }
  for (size_t i = 0; OB_SUCCESS == ret && i < durable_dependencies.size();
       ++i) {
    size_t match = candidate.dependencies_.size();
    for (size_t j = 0;
         match == candidate.dependencies_.size() &&
         j < candidate.dependencies_.size();
         ++j) {
      if (!matched[j] && same_runtime_dependency_contract(
                             durable_dependencies[i],
                             candidate.dependencies_[j])) {
        match = j;
      }
    }
    if (match == candidate.dependencies_.size()) {
      ret = OB_INVALID_DATA;
      error = "exact activation replay changed a durable dependency contract";
    } else {
      matched[match] = true;
      const ObPluginRuntimeServiceDependency &replacement =
          candidate.dependencies_[match];
      const ObPluginRuntimeServiceDependency &durable =
          durable_dependencies[i].dependency_;
      if (durable.provider_generation_ != replacement.provider_generation_ ||
          !catalog_same_version(durable.provider_version_,
                                replacement.provider_version_)) {
        int64_t affected_rows = 0;
        ret = connection.execute(
            "UPDATE __all_plugin_dependency SET provider_generation=?,"
            "provider_version_major=?,provider_version_minor=?,"
            "provider_version_patch=? WHERE consumer_kind=? AND "
            "consumer_id=? AND consumer_plugin_id=? AND "
            "consumer_generation=? AND "
            "provider_plugin_id=? AND provider_generation=? AND "
            "dependency_kind=? AND dependency_id=? AND service_abi_major=?",
            [&](ObPluginSqlBinder &binder) {
              int bind_ret = binder.bind_int64(static_cast<int64_t>(
                  replacement.provider_generation_));
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(
                    replacement.provider_version_.major);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(
                    replacement.provider_version_.minor);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(
                    replacement.provider_version_.patch);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDependencyConsumerKind::PLUGIN));
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, consumer_plugin_id);
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, consumer_plugin_id);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(
                    static_cast<int64_t>(consumer_generation));
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, durable.provider_plugin_id_);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(static_cast<int64_t>(
                    durable.provider_generation_));
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginDependencyKind::SERVICE));
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, durable.service_id_);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(
                    durable_dependencies[i].service_abi_major_);
              return bind_ret;
            },
            &affected_rows);
        if (OB_SUCCESS != ret) {
          error = "exact activation dependency fence rebind failed";
        } else if (affected_rows != 1) {
          ret = OB_STATE_NOT_MATCH;
          error = "durable dependency changed during exact activation replay";
        }
      }
    }
  }
  return ret;
}

int begin_write(ObPluginSqlConnection &connection)
{
  // ObMySQLTransaction starts a WAL-backed seekdb transaction on the regular
  // SQL catalog connection.  Writer serialization is provided by seekdb;
  // there is no file-backed database-specific BEGIN variant here.
  return connection.begin_transaction();
}

void rollback_noexcept(ObPluginSqlConnection &connection) noexcept
{
  if (connection.is_in_transaction()) {
    (void)connection.rollback();
  }
}

bool unfinished_operation_state(const int32_t state)
{
  return state ==
             static_cast<int32_t>(ObPluginCatalogOperationState::CATALOG_BEGIN) ||
         state == static_cast<int32_t>(
                      ObPluginCatalogOperationState::PROMOTE_PENDING) ||
         state ==
             static_cast<int32_t>(ObPluginCatalogOperationState::DISABLING) ||
         state == static_cast<int32_t>(
                      ObPluginCatalogOperationState::RECOVERY_REQUIRED);
}

} // namespace

ObPluginPackageInstallSpec::ObPluginPackageInstallSpec()
    : relative_path_(), artifact_(),
      verification_level_(ObPluginVerificationLevel::NOT_VERIFIED),
      operator_id_(), audit_id_()
{}

ObPluginCatalogRecord::ObPluginCatalogRecord()
    : plugin_id_(), relative_path_(), build_id_(), package_digest_(),
      package_version_{0, 0, 0}, catalog_version_(0), data_format_version_(0),
      verification_level_(ObPluginVerificationLevel::NOT_VERIFIED),
      desired_state_(ObPluginDesiredState::UNINSTALLED),
      actual_state_(ObPluginState::DISCOVERED), generation_(0),
      runtime_incarnation_(), operation_id_(), last_phase_(0),
      last_status_(OB_SUCCESS), last_error_(), operator_id_(), audit_id_(),
      created_at_us_(0), modified_at_us_(0)
{}

ObPluginDependencySpec::ObPluginDependencySpec()
    : consumer_kind_(ObPluginDependencyConsumerKind::PLUGIN), consumer_id_(),
      consumer_plugin_id_(), consumer_generation_(0), provider_plugin_id_(),
      provider_generation_(0), dependency_kind_(ObPluginDependencyKind::SERVICE),
      dependency_id_(), service_abi_major_(0), requested_version_(),
      required_capabilities_(0)
{
  std::memset(&requested_version_, 0, sizeof(requested_version_));
  requested_version_.struct_size = sizeof(requested_version_);
}

ObPluginStartupEntry::ObPluginStartupEntry()
    : plugin_id_(), relative_path_(), exact_recovery_(false), recovery_()
{}

ObPluginStartupReport::ObPluginStartupReport()
    : planned_(0), activated_(0), exact_replays_(0), failed_plugin_id_()
{}

struct ObPluginCatalog::Impl
{
  class ActivationPermit;
  class ActivationCommit;
  class DisablePermit;

  Impl()
      : mutex_(), sql_client_(nullptr), initialized_(false),
        startup_prepared_(false), startup_plan_()
  {}

  int initialize_schema();
  int install(const ObPluginPackageInstallSpec &spec, std::string &error);
  int get(const std::string &plugin_id, ObPluginCatalogRecord &record) const;
  int list(std::vector<ObPluginCatalogRecord> &records) const;
  int next_operation_id(ObPluginSqlConnection &connection,
                        uint64_t &sequence,
                        std::string &operation_id,
                        std::string &runtime_incarnation);
  int load_record(ObPluginSqlConnection &connection,
                  const std::string &plugin_id,
                  ObPluginCatalogRecord &record) const;
  int has_unfinished_operation(ObPluginSqlConnection &connection,
                               const std::string &plugin_id,
                               bool &has_unfinished) const;
  int begin_activation(const ObPluginActivationRequest &request,
                       std::unique_ptr<ObPluginActivationPermit> &permit,
                       std::string &error) noexcept;
  int commit_activation(ActivationPermit &permit,
                        const ObPluginRuntimeActivationResult &candidate,
                        ObPluginActivationDecision &decision,
                        std::unique_ptr<ObPluginActivationCommit> &commit,
                        std::string &error) noexcept;
  int complete_activation(ActivationCommit &commit,
                          const ObPluginRuntimeActivationResult &result,
                          std::string &error) noexcept;
  int abort_activation(ActivationPermit &permit,
                       const ObPluginRuntimeActivationResult &result,
                       std::string &error) noexcept;
  int begin_disable(const std::string &plugin_id,
                    uint64_t expected_generation,
                    std::unique_ptr<ObPluginDisablePermit> &permit,
                    std::string &error) noexcept;
  int checkpoint_disable_stop(DisablePermit &permit,
                              std::string &error) noexcept;
  int finish_disable(DisablePermit &permit,
                     const ObPluginRuntimeDisableResult &result,
                     std::string &error) noexcept;
  int mutate_dependency(ObPluginSqlConnection &connection,
                        const ObPluginDependencySpec &dependency,
                        bool add,
                        std::string &error);
  int list_blockers(ObPluginSqlConnection &connection,
                    const std::string &plugin_id,
                    std::vector<ObPluginRestrictBlocker> &blockers) const;
  int uninstall(const std::string &plugin_id,
                const std::string &operator_id,
                const std::string &audit_id,
                std::vector<ObPluginRestrictBlocker> &blockers,
                std::string &error);
  int prepare_startup(std::vector<ObPluginStartupEntry> &entries,
                      std::string &error);
  int ready(std::string &error) const;
  int mark_recovery_required(const std::string &operation_id,
                             const std::string &reason) noexcept;
  int mark_disable_recovery_required(
      const std::string &operation_id,
      int status,
      ObPluginState actual_state,
      ObPluginDisablePhase phase,
      bool stop_entered,
      const std::string &reason) noexcept;

  void consume_startup_entry(const std::string &plugin_id)
  {
    std::lock_guard<std::mutex> lock(mutex_);
    startup_plan_.erase(
        std::remove_if(
            startup_plan_.begin(), startup_plan_.end(),
            [&](const ObPluginStartupEntry &entry) {
              return entry.plugin_id_ == plugin_id;
            }),
        startup_plan_.end());
  }

  mutable std::mutex mutex_;
  common::ObISQLClient *sql_client_;
  std::atomic<bool> initialized_;
  bool startup_prepared_;
  std::vector<ObPluginStartupEntry> startup_plan_;
};

class ObPluginCatalog::Impl::ActivationCommit final
    : public ObPluginActivationCommit
{
public:
  ActivationCommit(Impl *owner,
                   const std::string &plugin_id,
                   const uint64_t generation,
                   const std::string &runtime_incarnation,
                   const std::string &operation_id)
      : owner_(owner), plugin_id_(plugin_id), generation_(generation),
        runtime_incarnation_(runtime_incarnation), operation_id_(operation_id),
        completed_(false)
  {}

  int complete(const ObPluginRuntimeActivationResult &runtime_result,
               std::string &error) noexcept override
  {
    return owner_->complete_activation(*this, runtime_result, error);
  }

  Impl *owner_;
  std::string plugin_id_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
  bool completed_;
};

class ObPluginCatalog::Impl::ActivationPermit final
    : public ObPluginActivationPermit
{
public:
  ActivationPermit(Impl *owner,
                   const std::string &plugin_id,
                   const uint64_t generation,
                   const std::string &runtime_incarnation,
                   const std::string &operation_id,
                   const bool recovery,
                   const bool prior_catalog_commit)
      : owner_(owner), plugin_id_(plugin_id), generation_(generation),
        runtime_incarnation_(runtime_incarnation), operation_id_(operation_id),
        recovery_(recovery), armed_(false), commit_attempted_(false),
        abort_allowed_(!prior_catalog_commit), resolved_(false),
        prior_catalog_commit_(prior_catalog_commit)
  {}

  ~ActivationPermit() noexcept override
  {
    if (armed_ && !resolved_) {
      (void)owner_->mark_recovery_required(
          operation_id_, "activation permit was destroyed unresolved");
    }
  }

  uint64_t generation() const noexcept override { return generation_; }
  const std::string &runtime_incarnation() const noexcept override
  {
    return runtime_incarnation_;
  }
  const std::string &operation_id() const noexcept override
  {
    return operation_id_;
  }

  int commit_candidate(
      const ObPluginRuntimeActivationResult &candidate_result,
      ObPluginActivationDecision &decision,
      std::unique_ptr<ObPluginActivationCommit> &commit,
      std::string &error) noexcept override
  {
    return owner_->commit_activation(
        *this, candidate_result, decision, commit, error);
  }

  int abort(const ObPluginRuntimeActivationResult &runtime_result,
            std::string &error) noexcept override
  {
    return owner_->abort_activation(*this, runtime_result, error);
  }

  Impl *owner_;
  std::string plugin_id_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
  bool recovery_;
  bool armed_;
  bool commit_attempted_;
  bool abort_allowed_;
  bool resolved_;
  bool prior_catalog_commit_;
};

class ObPluginCatalog::Impl::DisablePermit final
    : public ObPluginDisablePermit
{
public:
  DisablePermit(Impl *owner,
                const std::string &plugin_id,
                const uint64_t generation,
                const std::string &runtime_incarnation,
                const std::string &operation_id)
      : owner_(owner), plugin_id_(plugin_id), generation_(generation),
        runtime_incarnation_(runtime_incarnation), operation_id_(operation_id),
        armed_(false), finished_(false), observation_received_(false),
        stop_checkpointed_(false),
        observed_status_(OB_STATE_NOT_MATCH),
        observed_actual_state_(ObPluginState::DISCOVERED),
        observed_phase_(ObPluginDisablePhase::NONE),
        observed_stop_entered_(false), observed_error_()
  {}

  ~DisablePermit() noexcept override
  {
    if (armed_ && !finished_) {
      if (observation_received_) {
        (void)owner_->mark_disable_recovery_required(
            operation_id_, observed_status_, observed_actual_state_,
            observed_phase_, observed_stop_entered_, observed_error_);
      } else {
        (void)owner_->mark_recovery_required(
            operation_id_, "disable permit was destroyed unresolved");
      }
    }
  }

  int finish(const ObPluginRuntimeDisableResult &runtime_result,
             std::string &error) noexcept override
  {
    return owner_->finish_disable(*this, runtime_result, error);
  }

  int record_stop_entered(std::string &error) noexcept override
  {
    return owner_->checkpoint_disable_stop(*this, error);
  }

  Impl *owner_;
  std::string plugin_id_;
  uint64_t generation_;
  std::string runtime_incarnation_;
  std::string operation_id_;
  bool armed_;
  bool finished_;
  bool observation_received_;
  bool stop_checkpointed_;
  int observed_status_;
  ObPluginState observed_actual_state_;
  ObPluginDisablePhase observed_phase_;
  bool observed_stop_entered_;
  std::string observed_error_;
};

int ObPluginCatalog::Impl::initialize_schema()
{
  // The plugin catalog is provisioned as ordinary seekdb system tables by
  // bootstrap.  This method intentionally performs no DDL and never opens a
  // side database: the SQL proxy is the sole persistence boundary.
  return nullptr == sql_client_ ? OB_NOT_INIT : OB_SUCCESS;
}

int ObPluginCatalog::Impl::load_record(
    ObPluginSqlConnection &connection,
    const std::string &plugin_id,
    ObPluginCatalogRecord &record) const
{
  static const char SQL[] =
      "SELECT plugin_id,relative_path,build_id,package_digest,"
      "version_major,version_minor,version_patch,catalog_version,"
      "data_format_version,verification_level,desired_state,actual_state,"
      "generation,runtime_incarnation,operation_id,last_phase,last_status,"
      "last_error,operator_id,audit_id,gmt_create,gmt_modified "
      "FROM __all_plugin_package WHERE plugin_id=?";
  bool found = false;
  int ret = connection.query(
      SQL,
      [&](ObPluginSqlBinder &binder) { return bind_string(binder, plugin_id); },
      [&](ObPluginSqlRowReader &reader) {
        found = true;
        record.plugin_id_ = read_string(reader, 0);
        record.relative_path_ = read_string(reader, 1);
        record.build_id_ = read_string(reader, 2);
        record.package_digest_ = read_string(reader, 3);
        record.package_version_.major =
            static_cast<uint32_t>(reader.get_int64(4));
        record.package_version_.minor =
            static_cast<uint32_t>(reader.get_int64(5));
        record.package_version_.patch =
            static_cast<uint32_t>(reader.get_int64(6));
        record.catalog_version_ = static_cast<uint32_t>(reader.get_int64(7));
        record.data_format_version_ =
            static_cast<uint32_t>(reader.get_int64(8));
        record.verification_level_ =
            static_cast<ObPluginVerificationLevel>(reader.get_int(9));
        record.desired_state_ =
            static_cast<ObPluginDesiredState>(reader.get_int(10));
        record.actual_state_ = static_cast<ObPluginState>(reader.get_int(11));
        record.generation_ = static_cast<uint64_t>(reader.get_int64(12));
        record.runtime_incarnation_ = read_string(reader, 13);
        record.operation_id_ = read_string(reader, 14);
        record.last_phase_ = reader.get_int(15);
        record.last_status_ = reader.get_int(16);
        record.last_error_ = read_string(reader, 17);
        record.operator_id_ = read_string(reader, 18);
        record.audit_id_ = read_string(reader, 19);
        record.created_at_us_ = reader.get_int64(20);
        record.modified_at_us_ = reader.get_int64(21);
        return OB_ITER_END;
      });
  if (OB_SUCCESS == ret && !found) {
    ret = OB_ENTRY_NOT_EXIST;
  }
  return ret;
}

int ObPluginCatalog::Impl::has_unfinished_operation(
    ObPluginSqlConnection &connection,
    const std::string &plugin_id,
    bool &has_unfinished) const
{
  has_unfinished = false;
  const char SQL[] =
      "SELECT state FROM __all_plugin_operation WHERE plugin_id=? "
      "ORDER BY gmt_create DESC";
  int ret = connection.query(
      SQL,
      [&](ObPluginSqlBinder &binder) { return bind_string(binder, plugin_id); },
      [&](ObPluginSqlRowReader &reader) {
        if (unfinished_operation_state(reader.get_int(0))) {
          has_unfinished = true;
          return OB_ITER_END;
        }
        return OB_SUCCESS;
      });
  return OB_ENTRY_NOT_EXIST == ret ? OB_SUCCESS : ret;
}

int ObPluginCatalog::Impl::next_operation_id(
    ObPluginSqlConnection &connection,
    uint64_t &sequence,
    std::string &operation_id,
    std::string &runtime_incarnation)
{
  int64_t next = 0;
  bool found = false;
  int ret = connection.query(
      "SELECT next_value FROM __all_plugin_sequence WHERE sequence_name=?",
      [&](ObPluginSqlBinder &binder) {
        return bind_string(binder, PLUGIN_OPERATION_SEQUENCE);
      },
      [&](ObPluginSqlRowReader &reader) {
        found = true;
        next = reader.get_int64(0);
        return OB_ITER_END;
      });
  if (OB_SUCCESS == ret && !found) {
    // The sequence row is data, not schema.  Older/newly bootstrapped
    // installations may have the system table but no seed row yet.  Seed it
    // inside the caller's writer transaction so the first operation gets the
    // same durable identity semantics as subsequent operations.
    int64_t affected_rows = 0;
    ret = connection.execute(
        "INSERT INTO __all_plugin_sequence(sequence_name,next_value) "
        "VALUES(?,?) ON DUPLICATE KEY UPDATE sequence_name=VALUES(sequence_name)",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = bind_string(binder, PLUGIN_OPERATION_SEQUENCE);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(2);
          return bind_ret;
        },
        &affected_rows);
    if (OB_SUCCESS == ret) {
      next = 1;
    }
  }
  if (OB_SUCCESS == ret && (next <= 0 || next == std::numeric_limits<int64_t>::max())) {
    ret = OB_SIZE_OVERFLOW;
  }
  if (OB_SUCCESS == ret && found) {
    int64_t affected_rows = 0;
    ret = connection.execute(
        "UPDATE __all_plugin_sequence SET next_value=? "
        "WHERE sequence_name=? AND next_value=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int64(next + 1);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, PLUGIN_OPERATION_SEQUENCE);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(next);
          return bind_ret;
        },
        &affected_rows);
    if (OB_SUCCESS == ret && affected_rows != 1) ret = OB_EAGAIN;
  }
  if (OB_SUCCESS == ret) {
    sequence = static_cast<uint64_t>(next);
    operation_id = std::string("plugin-op-") + std::to_string(sequence);
    runtime_incarnation =
        std::string("plugin-runtime-") + std::to_string(sequence);
  }
  return ret;
}

int ObPluginCatalog::Impl::begin_activation(
    const ObPluginActivationRequest &request,
    std::unique_ptr<ObPluginActivationPermit> &permit,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  permit.reset();
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    ObPluginCatalogRecord record;
    uint64_t generation = 0;
    uint64_t operation_sequence = 0;
    std::string operation_id;
    std::string runtime_incarnation;
    std::unique_ptr<ActivationPermit> prepared;
    bool prior_catalog_commit = false;
    const bool recovery =
        request.mode_ == ObPluginActivationMode::STARTUP_RECOVERY;
    const int64_t now = ObTimeUtility::current_time();

    if (!initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!valid_activation_request(request)) {
      ret = OB_INVALID_ARGUMENT;
      error = "plugin activation request identity is invalid";
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin catalog writer";
    } else if (OB_FAIL(load_record(*guard.get_connection(), request.plugin_id_,
                                   record))) {
      error = OB_ENTRY_NOT_EXIST == ret
                  ? "plugin package is not installed"
                  : "cannot read plugin package authorization";
    } else if (record.desired_state_ != ObPluginDesiredState::ACTIVE ||
               record.verification_level_ ==
                   ObPluginVerificationLevel::NOT_VERIFIED) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin package is not authorized for activation";
    } else if (record.relative_path_ != request.relative_path_ ||
               record.plugin_id_ != request.plugin_id_ ||
               record.build_id_ != request.build_id_ ||
               record.package_digest_ != request.package_digest_ ||
               !catalog_same_version(record.package_version_,
                             request.package_version_) ||
               record.catalog_version_ != request.catalog_version_ ||
               record.data_format_version_ != request.data_format_version_) {
      ret = OB_INVALID_DATA;
      error = "activation artifact does not match installed catalog package";
    }

    if (OB_SUCCESS == ret && recovery) {
      int32_t operation_kind = -1;
      int32_t operation_state = -1;
      bool candidate_prepared = false;
      std::string operation_path;
      std::string operation_digest;
      ret = guard->query(
          "SELECT kind,state,relative_path,package_digest,candidate_prepared "
          "FROM __all_plugin_operation WHERE operation_id=? AND plugin_id=? "
          "AND generation=? AND runtime_incarnation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                bind_string(binder, request.expected_operation_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, request.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(
                  static_cast<int64_t>(request.expected_generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(
                  binder, request.expected_runtime_incarnation_);
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            operation_kind = reader.get_int(0);
            operation_state = reader.get_int(1);
            operation_path = read_string(reader, 2);
            operation_digest = read_string(reader, 3);
            candidate_prepared = 0 != reader.get_int(4);
            return OB_ITER_END;
          });
      if (OB_SUCCESS != ret) {
        error = "startup recovery activation intent was not found";
      } else if (operation_kind != static_cast<int32_t>(
                                       ObPluginCatalogOperationKind::ACTIVATE) ||
                 !unfinished_operation_state(operation_state) ||
                 operation_state == static_cast<int32_t>(
                                        ObPluginCatalogOperationState::DISABLING) ||
                 operation_path != request.relative_path_ ||
                 operation_digest != request.package_digest_ ||
                 record.generation_ != request.expected_generation_ ||
                 record.runtime_incarnation_ !=
                     request.expected_runtime_incarnation_ ||
                 record.operation_id_ != request.expected_operation_id_) {
        ret = OB_STATE_NOT_MATCH;
        error = "startup recovery activation fence does not match durable intent";
      } else if ((operation_state == static_cast<int32_t>(
                                          ObPluginCatalogOperationState::PROMOTE_PENDING) &&
                  !candidate_prepared) ||
                 (operation_state == static_cast<int32_t>(
                                          ObPluginCatalogOperationState::CATALOG_BEGIN) &&
                  candidate_prepared)) {
        ret = OB_INVALID_DATA;
        error = "startup recovery activation decision marker is inconsistent";
      } else {
        generation = request.expected_generation_;
        runtime_incarnation = request.expected_runtime_incarnation_;
        operation_id = request.expected_operation_id_;
        prior_catalog_commit = candidate_prepared;
      }
    } else if (OB_SUCCESS == ret) {
      bool unfinished = false;
      if (record.actual_state_ == ObPluginState::ACTIVE ||
          record.actual_state_ == ObPluginState::QUIESCING ||
          record.actual_state_ == ObPluginState::BLOCKED) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin catalog runtime state cannot start a new activation";
      } else if (OB_FAIL(has_unfinished_operation(
                     *guard.get_connection(), request.plugin_id_,
                     unfinished))) {
        error = "cannot inspect plugin activation intents";
      } else if (unfinished) {
        ret = OB_EAGAIN;
        error = "plugin has an unfinished catalog operation";
      } else if (record.generation_ >= MAX_DURABLE_GENERATION) {
        ret = OB_SIZE_OVERFLOW;
        error = "plugin generation space is exhausted";
      } else if (OB_FAIL(next_operation_id(
                     *guard.get_connection(), operation_sequence,
                     operation_id, runtime_incarnation))) {
        error = "cannot allocate durable plugin operation identity";
      } else {
        generation = record.generation_ + 1;
        ret = guard->execute(
            "INSERT INTO __all_plugin_operation("
            "operation_id,plugin_id,generation,runtime_incarnation,kind,state,"
            "relative_path,package_digest,phase,status,actual_state,"
            "start_entered,candidate_prepared,stop_entered,error,operator_id,"
            "audit_id,gmt_create,gmt_modified) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            [&](ObPluginSqlBinder &binder) {
              int bind_ret = bind_string(binder, operation_id);
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, request.plugin_id_);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int64(
                    static_cast<int64_t>(generation));
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, runtime_incarnation);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginCatalogOperationKind::ACTIVATE));
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginCatalogOperationState::CATALOG_BEGIN));
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, request.relative_path_);
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, request.package_digest_);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginActivationPhase::CATALOG_BEGIN));
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(OB_SUCCESS);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_int(
                    static_cast<int32_t>(ObPluginState::DISCOVERED));
              if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
              if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
              if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
              if (OB_SUCCESS == bind_ret)
                bind_ret = binder.bind_text("", 0);
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, record.operator_id_);
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, record.audit_id_);
              if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
              if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
              return bind_ret;
            });
        if (OB_SUCCESS == ret) {
          ret = guard->execute(
              "UPDATE __all_plugin_package SET actual_state=?,generation=?,"
              "runtime_incarnation=?,operation_id=?,last_phase=?,last_status=0,"
              "last_error='',gmt_modified=? WHERE plugin_id=?",
              [&](ObPluginSqlBinder &binder) {
                int bind_ret = binder.bind_int(
                    static_cast<int32_t>(ObPluginState::DISCOVERED));
                if (OB_SUCCESS == bind_ret)
                  bind_ret = binder.bind_int64(
                      static_cast<int64_t>(generation));
                if (OB_SUCCESS == bind_ret)
                  bind_ret = bind_string(binder, runtime_incarnation);
                if (OB_SUCCESS == bind_ret)
                  bind_ret = bind_string(binder, operation_id);
                if (OB_SUCCESS == bind_ret)
                  bind_ret = binder.bind_int(static_cast<int32_t>(
                      ObPluginActivationPhase::CATALOG_BEGIN));
                if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
                if (OB_SUCCESS == bind_ret)
                  bind_ret = bind_string(binder, request.plugin_id_);
                return bind_ret;
              });
        }
        if (OB_SUCCESS != ret) {
          error = "cannot persist plugin activation intent";
        }
      }
    }

    if (OB_SUCCESS == ret) {
      prepared.reset(new (std::nothrow) ActivationPermit(
          this, request.plugin_id_, generation, runtime_incarnation,
          operation_id, recovery, prior_catalog_commit));
      if (!prepared) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        error = "cannot allocate plugin activation permit";
      }
    }
    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin activation begin commit outcome is unknown";
      } else {
        prepared->armed_ = true;
        permit = std::move(prepared);
      }
    }
    if (OB_SUCCESS != ret && guard)
      rollback_noexcept(*guard.get_connection());
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    try {
      error = "plugin activation catalog allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    try {
      error = "unexpected plugin activation catalog failure";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::commit_activation(
    ActivationPermit &activation_permit,
    const ObPluginRuntimeActivationResult &candidate,
    ObPluginActivationDecision &decision,
    std::unique_ptr<ObPluginActivationCommit> &commit,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
  commit.reset();
  error.clear();
  bool durable_decision = activation_permit.prior_catalog_commit_;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    std::unique_ptr<ActivationCommit> prepared;
    ObPluginSqlConnectionGuard guard(sql_client_);
    int32_t operation_state = -1;
    int32_t operation_kind = -1;
    bool durable_candidate_prepared = false;
    ObPluginCatalogRecord record;
    const int64_t now = ObTimeUtility::current_time();

    if (!initialized_ || !activation_permit.armed_ ||
        activation_permit.resolved_ ||
        activation_permit.commit_attempted_) {
      ret = OB_STATE_NOT_MATCH;
      decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
      error = "plugin activation permit cannot commit a candidate";
    } else {
      activation_permit.commit_attempted_ = true;
    }
    if (OB_SUCCESS == ret &&
        !valid_candidate_result(candidate, activation_permit.plugin_id_,
                                activation_permit.generation_,
                                activation_permit.runtime_incarnation_,
                                activation_permit.operation_id_)) {
      ret = OB_INVALID_DATA;
      decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
      error = "plugin activation candidate does not match its durable permit";
    } else if (OB_SUCCESS == ret && !guard) {
      ret = OB_NOT_INIT;
      decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
      error = "plugin catalog database is unavailable";
    } else if (OB_SUCCESS == ret &&
               OB_FAIL(begin_write(*guard.get_connection()))) {
      decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
      error = "cannot reserve plugin catalog writer";
    } else if (OB_SUCCESS == ret &&
               OB_FAIL(load_record(*guard.get_connection(),
                                   activation_permit.plugin_id_, record))) {
      decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
      error = "plugin package disappeared during activation";
    } else if (OB_SUCCESS == ret &&
               (record.desired_state_ != ObPluginDesiredState::ACTIVE ||
                record.generation_ != activation_permit.generation_ ||
                record.runtime_incarnation_ !=
                    activation_permit.runtime_incarnation_ ||
                record.operation_id_ != activation_permit.operation_id_)) {
      ret = OB_STATE_NOT_MATCH;
      decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
      error = "plugin package activation fence changed";
    }

    if (OB_SUCCESS == ret) {
      ret = guard->query(
          "SELECT kind,state,candidate_prepared FROM __all_plugin_operation "
          "WHERE operation_id=? AND plugin_id=? AND generation=? "
          "AND runtime_incarnation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                bind_string(binder, activation_permit.operation_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(
                  binder, activation_permit.runtime_incarnation_);
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            operation_kind = reader.get_int(0);
            operation_state = reader.get_int(1);
            durable_candidate_prepared = 0 != reader.get_int(2);
            return OB_ITER_END;
          });
      if (OB_SUCCESS != ret ||
          operation_kind != static_cast<int32_t>(
                                ObPluginCatalogOperationKind::ACTIVATE) ||
          (operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::CATALOG_BEGIN) &&
           operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::PROMOTE_PENDING) &&
           operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::RECOVERY_REQUIRED))) {
        if (OB_SUCCESS == ret) ret = OB_STATE_NOT_MATCH;
        decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
        error = "durable activation intent cannot accept a candidate";
      } else if ((operation_state == static_cast<int32_t>(
                                          ObPluginCatalogOperationState::PROMOTE_PENDING) &&
                  !durable_candidate_prepared) ||
                 (operation_state == static_cast<int32_t>(
                                          ObPluginCatalogOperationState::CATALOG_BEGIN) &&
                  durable_candidate_prepared)) {
        ret = OB_INVALID_DATA;
        error = "durable activation decision marker is inconsistent";
      } else {
        durable_decision = durable_candidate_prepared;
      }
    }

    // Preallocate the post-commit token before mutating durable ownership.
    if (OB_SUCCESS == ret) {
      prepared.reset(new (std::nothrow) ActivationCommit(
          this, activation_permit.plugin_id_, activation_permit.generation_,
          activation_permit.runtime_incarnation_,
          activation_permit.operation_id_));
      if (!prepared) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
        error = "cannot allocate durable activation commit token";
      }
    }

    // Registry conflict checks are repeated against the durable current
    // generations.  The hidden candidate has already validated the process
    // snapshot; this closes the independent catalog namespace race.
    for (size_t i = 0; OB_SUCCESS == ret && i < candidate.services_.size();
         ++i) {
      int64_t conflicts = 0;
      const ObPluginServiceInfo &service = candidate.services_[i];
      ret = query_count(
          *guard.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_service s "
          "JOIN __all_plugin_package p ON p.plugin_id=s.plugin_id "
          "AND p.generation=s.generation "
          "WHERE s.service_id=? AND s.abi_major=? AND "
          "NOT(s.plugin_id=? AND s.generation=?) AND p.desired_state=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = bind_string(binder, service.name_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(service.abi_major_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDesiredState::ACTIVE));
            return bind_ret;
          },
          conflicts);
      if (OB_SUCCESS == ret && conflicts != 0) {
        ret = OB_ENTRY_EXIST;
        error = "durable plugin service ownership conflicts with another package";
      }
    }
    for (size_t i = 0; OB_SUCCESS == ret && i < candidate.extensions_.size();
         ++i) {
      int64_t conflicts = 0;
      const ObPluginExtensionSpec &spec = candidate.extensions_[i].spec_;
      ret = query_count(
          *guard.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_extension e "
          "JOIN __all_plugin_package p ON p.plugin_id=e.plugin_id "
          "AND p.generation=e.generation "
          "WHERE e.kind=? AND e.object_id=? AND "
          "NOT(e.plugin_id=? AND e.generation=?) AND p.desired_state=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(spec.kind_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, spec.object_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDesiredState::ACTIVE));
            return bind_ret;
          },
          conflicts);
      if (OB_SUCCESS == ret && conflicts != 0) {
        ret = OB_ENTRY_EXIST;
        error = "durable plugin extension ownership conflicts with another package";
      }
    }

    // Resolve the runtime observation against the durable provider catalog in
    // the same writer transaction which installs the dependency edge.  This
    // is the serialization point with restricted disable: either this edge is
    // committed first and becomes a blocker, or disable wins and this
    // activation is rejected before its hidden candidate can be published.
    for (size_t i = 0; OB_SUCCESS == ret && i < candidate.dependencies_.size();
         ++i) {
      const ObPluginRuntimeServiceDependency &dependency =
          candidate.dependencies_[i];
      ObPluginCatalogRecord provider;
      bool unfinished = false;
      int64_t matching_services = 0;
      if (OB_FAIL(load_record(*guard.get_connection(),
                              dependency.provider_plugin_id_, provider))) {
        error = "plugin dependency provider is not installed";
      } else if (provider.desired_state_ != ObPluginDesiredState::ACTIVE ||
                 provider.actual_state_ != ObPluginState::ACTIVE ||
                 provider.generation_ != dependency.provider_generation_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin dependency provider is not production ACTIVE";
      } else if (OB_FAIL(has_unfinished_operation(
                     *guard.get_connection(), dependency.provider_plugin_id_,
                     unfinished))) {
        error = "cannot inspect plugin dependency provider operation";
      } else if (unfinished) {
        ret = OB_EAGAIN;
        error = "plugin dependency provider is changing state";
      } else if (OB_FAIL(query_count(
                     *guard.get_connection(),
                     "SELECT COUNT(*) FROM __all_plugin_service WHERE "
                     "plugin_id=? AND generation=? AND service_id=? AND "
                     "abi_major=? AND abi_minor=? AND abi_patch=? AND "
                     "(capabilities & ?)=?",
                     [&](ObPluginSqlBinder &binder) {
                       int bind_ret = bind_string(
                           binder, dependency.provider_plugin_id_);
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = binder.bind_int64(static_cast<int64_t>(
                             dependency.provider_generation_));
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = bind_string(binder,
                                                dependency.service_id_);
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = binder.bind_int64(
                             dependency.provider_version_.major);
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = binder.bind_int64(
                             dependency.provider_version_.minor);
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = binder.bind_int64(
                             dependency.provider_version_.patch);
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = binder.bind_int64(static_cast<int64_t>(
                             dependency.required_capabilities_));
                       if (OB_SUCCESS == bind_ret)
                         bind_ret = binder.bind_int64(static_cast<int64_t>(
                             dependency.required_capabilities_));
                       return bind_ret;
                     },
                     matching_services))) {
        error = "cannot validate durable plugin dependency service";
      } else if (matching_services != 1) {
        ret = OB_STATE_NOT_MATCH;
        error = "runtime dependency does not match a durable provider service";
      }
    }

    if (OB_SUCCESS == ret) {
      // Stable catalog/user/data/job edges describe a dependency on this
      // immutable R1 package, not on one process incarnation.  Rebind them to
      // the new runtime fence only after the hidden candidate and its durable
      // package identity have both been validated.  Plugin-consumer edges are
      // generation-owned and are recreated by that consumer's activation.
      ret = validate_and_rebind_stable_dependencies(
          *guard.get_connection(), activation_permit.plugin_id_,
          activation_permit.generation_, candidate, error);
    }

    if (OB_SUCCESS == ret && durable_decision) {
      // PROMOTE_PENDING is already a durable decision.  Exact replay may
      // refresh only a compatible provider generation fence; it must never
      // rewrite the service, extension, or logical dependency contribution.
      ret = validate_and_rebind_exact_replay(
          *guard.get_connection(), activation_permit.plugin_id_,
          activation_permit.generation_, candidate, error);
    }

    if (OB_SUCCESS == ret && !durable_decision) {
      ret = guard->execute(
          "DELETE FROM __all_plugin_service WHERE plugin_id=? AND generation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret && !durable_decision) {
      ret = guard->execute(
          "DELETE FROM __all_plugin_extension WHERE plugin_id=? AND generation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret && !durable_decision) {
      ret = guard->execute(
          "DELETE FROM __all_plugin_dependency WHERE consumer_kind=? "
          "AND consumer_plugin_id=? AND consumer_generation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyConsumerKind::PLUGIN));
            if (OB_SUCCESS == bind_ret)
              bind_ret =
                  bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            return bind_ret;
          });
    }
    for (size_t i = 0;
         OB_SUCCESS == ret && !durable_decision && i < candidate.services_.size();
         ++i) {
      ret = insert_service(*guard.get_connection(),
                           activation_permit.plugin_id_,
                           activation_permit.generation_,
                           candidate.services_[i]);
    }
    for (size_t i = 0;
         OB_SUCCESS == ret && !durable_decision && i < candidate.extensions_.size();
         ++i) {
      ret = insert_extension(*guard.get_connection(),
                             activation_permit.plugin_id_,
                             activation_permit.generation_,
                             candidate.extensions_[i]);
    }
    for (size_t i = 0;
         OB_SUCCESS == ret && !durable_decision && i < candidate.dependencies_.size();
         ++i) {
      ret = insert_runtime_dependency(
          *guard.get_connection(), activation_permit.plugin_id_,
          activation_permit.generation_, candidate.dependencies_[i]);
    }

    if (OB_SUCCESS == ret) {
      const std::string candidate_error = bounded_error(candidate.error_);
      ret = guard->execute(
          "UPDATE __all_plugin_operation SET state=?,phase=?,status=?,"
          "actual_state=?,start_entered=?,candidate_prepared=?,error=?,"
          "gmt_modified=? WHERE operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginCatalogOperationState::PROMOTE_PENDING));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(candidate.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(candidate.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(candidate.actual_state_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(candidate.start_entered_ ? 1 : 0);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(candidate.candidate_prepared_ ? 1 : 0);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, candidate_error);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.operation_id_);
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      const std::string candidate_error = bounded_error(candidate.error_);
      ret = guard->execute(
          "UPDATE __all_plugin_package SET actual_state=?,last_phase=?,"
          "last_status=?,last_error=?,gmt_modified=? WHERE plugin_id=? "
          "AND generation=? AND operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(
                static_cast<int32_t>(candidate.actual_state_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(candidate.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(candidate.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, candidate_error);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.operation_id_);
            return bind_ret;
          });
    }

    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        // The database may have committed even though the wrapper could not
        // prove it.  Set this before constructing diagnostics so a secondary
        // allocation failure cannot downgrade UNKNOWN to NOT_COMMITTED.
        durable_decision = true;
        ret = OB_TRANS_UNKNOWN;
        decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
        activation_permit.abort_allowed_ = false;
        activation_permit.resolved_ = true;
        error = "plugin activation catalog commit outcome is unknown";
      } else {
        decision = OB_PLUGIN_ACTIVATION_PROMOTE;
        activation_permit.abort_allowed_ = false;
        activation_permit.resolved_ = true;
        commit = std::move(prepared);
      }
    } else {
      if (guard) rollback_noexcept(*guard.get_connection());
      if (durable_decision) {
        // A previous process already committed PROMOTE_PENDING.  Failure to
        // reconstruct the runtime candidate cannot turn that decision into an
        // abortable transaction; retain the exact identity for another
        // startup replay.
        ret = OB_TRANS_UNKNOWN;
        decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
        activation_permit.abort_allowed_ = false;
        activation_permit.resolved_ = true;
        if (error.empty())
          error = "previously committed activation could not be replayed";
      } else {
        decision = OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
        activation_permit.abort_allowed_ = true;
        if (error.empty())
          error = "plugin activation candidate was not committed";
      }
    }
  } catch (const std::bad_alloc &) {
    ret = durable_decision ? OB_TRANS_UNKNOWN : OB_ALLOCATE_MEMORY_FAILED;
    decision = durable_decision ? OB_PLUGIN_ACTIVATION_UNKNOWN
                                : OB_PLUGIN_ACTIVATION_NOT_COMMITTED;
    activation_permit.abort_allowed_ = !durable_decision;
    activation_permit.resolved_ = durable_decision;
    try {
      error = "plugin activation catalog allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    decision = OB_PLUGIN_ACTIVATION_UNKNOWN;
    activation_permit.abort_allowed_ = false;
    activation_permit.resolved_ = true;
    try {
      error = "plugin activation catalog outcome is unknown";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::complete_activation(
    ActivationCommit &activation_commit,
    const ObPluginRuntimeActivationResult &result,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    int32_t operation_kind = -1;
    int32_t operation_state = -1;
    const int64_t now = ObTimeUtility::current_time();
    if (!initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (activation_commit.completed_) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin activation commit was completed twice";
    } else if (!valid_complete_result(
                   result, activation_commit.generation_,
                   activation_commit.runtime_incarnation_,
                   activation_commit.operation_id_)) {
      ret = OB_INVALID_DATA;
      error = "active runtime result does not match durable commit token";
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin catalog writer";
    }
    if (OB_SUCCESS == ret) {
      ret = guard->query(
          "SELECT kind,state FROM __all_plugin_operation WHERE operation_id=? "
          "AND plugin_id=? AND generation=? AND runtime_incarnation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = bind_string(binder, activation_commit.operation_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_commit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_commit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(
                  binder, activation_commit.runtime_incarnation_);
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            operation_kind = reader.get_int(0);
            operation_state = reader.get_int(1);
            return OB_ITER_END;
          });
      if (OB_SUCCESS != ret ||
          operation_kind != static_cast<int32_t>(
                                ObPluginCatalogOperationKind::ACTIVATE) ||
          (operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::PROMOTE_PENDING) &&
           operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::COMPLETED))) {
        if (OB_SUCCESS == ret) ret = OB_STATE_NOT_MATCH;
        error = "durable activation is not waiting for completion";
      }
    }
    if (OB_SUCCESS == ret &&
        operation_state != static_cast<int32_t>(
                               ObPluginCatalogOperationState::COMPLETED)) {
      ret = guard->execute(
          "UPDATE __all_plugin_operation SET state=?,phase=?,status=?,"
          "actual_state=?,error='',gmt_modified=? WHERE operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginCatalogOperationState::COMPLETED));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(result.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginState::ACTIVE));
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_commit.operation_id_);
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      ret = guard->execute(
          "UPDATE __all_plugin_package SET actual_state=?,last_phase=?,"
          "last_status=0,last_error='',gmt_modified=? WHERE plugin_id=? "
          "AND generation=? AND runtime_incarnation=? AND operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                binder.bind_int(static_cast<int32_t>(ObPluginState::ACTIVE));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(result.phase_));
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_commit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_commit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(
                  binder, activation_commit.runtime_incarnation_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_commit.operation_id_);
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin activation completion commit outcome is unknown";
      } else {
        activation_commit.completed_ = true;
      }
    } else if (guard) {
      rollback_noexcept(*guard.get_connection());
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    try {
      error = "plugin activation completion allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    try {
      error = "plugin activation completion outcome is unknown";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::abort_activation(
    ActivationPermit &activation_permit,
    const ObPluginRuntimeActivationResult &result,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    int32_t operation_kind = -1;
    int32_t operation_state = -1;
    const int64_t now = ObTimeUtility::current_time();
    const std::string runtime_error = bounded_error(result.error_);
    if (!initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!activation_permit.armed_ || activation_permit.resolved_ ||
               !activation_permit.abort_allowed_ ||
               result.generation_ != activation_permit.generation_ ||
               result.runtime_incarnation_ !=
                   activation_permit.runtime_incarnation_ ||
               result.operation_id_ != activation_permit.operation_id_) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin activation permit cannot be aborted";
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin catalog writer";
    }
    if (OB_SUCCESS == ret) {
      ret = guard->query(
          "SELECT kind,state FROM __all_plugin_operation WHERE operation_id=? "
          "AND plugin_id=? AND generation=? AND runtime_incarnation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                bind_string(binder, activation_permit.operation_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(
                  binder, activation_permit.runtime_incarnation_);
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            operation_kind = reader.get_int(0);
            operation_state = reader.get_int(1);
            return OB_ITER_END;
          });
      if (OB_SUCCESS != ret ||
          operation_kind != static_cast<int32_t>(
                                ObPluginCatalogOperationKind::ACTIVATE) ||
          (operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::CATALOG_BEGIN) &&
           operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::RECOVERY_REQUIRED))) {
        if (OB_SUCCESS == ret) ret = OB_STATE_NOT_MATCH;
        error = "durable activation intent cannot be aborted";
      }
    }
    if (OB_SUCCESS == ret) {
      ret = guard->execute(
          "UPDATE __all_plugin_operation SET state=?,phase=?,status=?,"
          "actual_state=?,start_entered=?,candidate_prepared=?,error=?,"
          "gmt_modified=? WHERE operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginCatalogOperationState::ABORTED));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(result.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(result.actual_state_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.start_entered_ ? 1 : 0);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.candidate_prepared_ ? 1 : 0);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, runtime_error);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.operation_id_);
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      ret = guard->execute(
          "UPDATE __all_plugin_package SET actual_state=?,last_phase=?,"
          "last_status=?,last_error=?,gmt_modified=? WHERE plugin_id=? "
          "AND generation=? AND operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                binder.bind_int(static_cast<int32_t>(result.actual_state_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(result.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, runtime_error);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  activation_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, activation_permit.operation_id_);
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin activation abort commit outcome is unknown";
      } else {
        activation_permit.resolved_ = true;
      }
    } else if (guard) {
      rollback_noexcept(*guard.get_connection());
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    try {
      error = "plugin activation abort allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    try {
      error = "plugin activation abort outcome is unknown";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::mark_recovery_required(
    const std::string &operation_id,
    const std::string &reason) noexcept
{
  int ret = OB_SUCCESS;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    const std::string stored_reason = bounded_error(reason);
    const int64_t now = ObTimeUtility::current_time();
    int64_t affected_rows = 0;
    if (!initialized_ || !catalog_valid_identifier(operation_id)) {
      ret = OB_STATE_NOT_MATCH;
    } else if (!guard) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
    } else if (OB_FAIL(guard->execute(
                   "UPDATE __all_plugin_operation SET state=?,status=?,"
                   "error=?,gmt_modified=? WHERE operation_id=? AND "
                   "state IN(?,?,?,?)",
                   [&](ObPluginSqlBinder &binder) {
                     int bind_ret = binder.bind_int(static_cast<int32_t>(
                         ObPluginCatalogOperationState::RECOVERY_REQUIRED));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(OB_TRANS_UNKNOWN);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = bind_string(binder, stored_reason);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int64(now);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = bind_string(binder, operation_id);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::CATALOG_BEGIN));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::PROMOTE_PENDING));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::DISABLING));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::RECOVERY_REQUIRED));
                     return bind_ret;
                   },
                   &affected_rows))) {
    } else if (affected_rows != 1) {
      ret = OB_STATE_NOT_MATCH;
    } else if (OB_FAIL(guard->commit())) {
      ret = OB_TRANS_UNKNOWN;
    }
    if (OB_SUCCESS != ret && guard)
      rollback_noexcept(*guard.get_connection());
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginCatalog::Impl::mark_disable_recovery_required(
    const std::string &operation_id,
    const int status,
    const ObPluginState actual_state,
    const ObPluginDisablePhase phase,
    const bool stop_entered,
    const std::string &reason) noexcept
{
  int ret = OB_SUCCESS;
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    const std::string stored_reason = bounded_error(
        reason.empty() ? "disable catalog finalization was unresolved" :
                         reason);
    const bool safe_abort = status != OB_SUCCESS &&
                            actual_state == ObPluginState::ACTIVE &&
                            !stop_entered &&
                            (phase == ObPluginDisablePhase::NONE ||
                             phase == ObPluginDisablePhase::QUIESCE);
    const bool stopped = status == OB_SUCCESS &&
                         actual_state == ObPluginState::STOPPED &&
                         phase == ObPluginDisablePhase::COMPLETE &&
                         stop_entered;
    const ObPluginCatalogOperationState final_operation_state =
        safe_abort ? ObPluginCatalogOperationState::ABORTED
                   : (stopped ? ObPluginCatalogOperationState::COMPLETED
                              : ObPluginCatalogOperationState::
                                    RECOVERY_REQUIRED);
    const ObPluginDesiredState final_desired_state =
        safe_abort ? ObPluginDesiredState::ACTIVE
                   : ObPluginDesiredState::DISABLED;
    const int64_t now = ObTimeUtility::current_time();
    int64_t affected_rows = 0;
    int64_t package_affected_rows = 0;
    if (!initialized_ || !catalog_valid_identifier(operation_id)) {
      ret = OB_STATE_NOT_MATCH;
    } else if (!guard) {
      ret = OB_NOT_INIT;
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
    } else if (OB_FAIL(guard->execute(
                   "UPDATE __all_plugin_operation SET state=?,phase=?,"
                   "status=?,actual_state=?,stop_entered=?,error=?,"
                   "gmt_modified=? WHERE operation_id=? AND kind=? AND "
                   "state IN(?,?,?)",
                   [&](ObPluginSqlBinder &binder) {
                     int bind_ret = binder.bind_int(
                         static_cast<int32_t>(final_operation_state));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(
                           static_cast<int32_t>(phase));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(status);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(
                           static_cast<int32_t>(actual_state));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(stop_entered ? 1 : 0);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = bind_string(binder, stored_reason);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int64(now);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = bind_string(binder, operation_id);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationKind::DISABLE));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::DISABLING));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::RECOVERY_REQUIRED));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(static_cast<int32_t>(
                           ObPluginCatalogOperationState::CATALOG_BEGIN));
                     return bind_ret;
                   },
                   &affected_rows))) {
    } else if (affected_rows != 1) {
      // A terminal operation may already have committed.  Do not overwrite
      // it with a fallback observation, but report that this transaction did
      // not establish the outcome itself.
      ret = OB_STATE_NOT_MATCH;
    } else if (OB_FAIL(guard->execute(
                   "UPDATE __all_plugin_package SET desired_state=?,"
                   "actual_state=?,last_phase=?,last_status=?,last_error=?,"
                   "gmt_modified=? WHERE operation_id=?",
                   [&](ObPluginSqlBinder &binder) {
                     int bind_ret = binder.bind_int(
                         static_cast<int32_t>(final_desired_state));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(
                         static_cast<int32_t>(actual_state));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(
                           static_cast<int32_t>(phase));
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int(status);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = bind_string(binder, stored_reason);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = binder.bind_int64(now);
                     if (OB_SUCCESS == bind_ret)
                       bind_ret = bind_string(binder, operation_id);
                     return bind_ret;
                   },
                   &package_affected_rows))) {
    } else if (package_affected_rows != 1) {
      ret = OB_STATE_NOT_MATCH;
    } else if (OB_FAIL(guard->commit())) {
      ret = OB_TRANS_UNKNOWN;
    }
    if (OB_SUCCESS != ret && guard)
      rollback_noexcept(*guard.get_connection());
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginCatalog::Impl::list_blockers(
    ObPluginSqlConnection &connection,
    const std::string &plugin_id,
    std::vector<ObPluginRestrictBlocker> &blockers) const
{
  int ret = OB_SUCCESS;
  blockers.clear();
  std::vector<ObPluginRestrictBlocker> candidates;
  ret = connection.query(
      "SELECT consumer_kind,consumer_id,consumer_plugin_id,"
      "consumer_generation,dependency_kind,dependency_id,service_abi_major "
      "FROM __all_plugin_dependency WHERE provider_plugin_id=? "
      "ORDER BY consumer_kind,consumer_id,consumer_plugin_id",
      [&](ObPluginSqlBinder &binder) { return bind_string(binder, plugin_id); },
      [&](ObPluginSqlRowReader &reader) {
        ObPluginRestrictBlocker blocker;
        blocker.consumer_kind_ =
            static_cast<ObPluginDependencyConsumerKind>(reader.get_int(0));
        blocker.consumer_id_ = read_string(reader, 1);
        blocker.consumer_plugin_id_ = read_string(reader, 2);
        blocker.consumer_generation_ =
            static_cast<uint64_t>(reader.get_int64(3));
        blocker.dependency_kind_ =
            static_cast<ObPluginDependencyKind>(reader.get_int(4));
        blocker.dependency_id_ = read_string(reader, 5);
        blocker.service_abi_major_ =
            static_cast<uint32_t>(reader.get_int64(6));
        candidates.push_back(blocker);
        return OB_SUCCESS;
      });
  if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
  for (size_t i = 0; OB_SUCCESS == ret && i < candidates.size(); ++i) {
    const ObPluginRestrictBlocker &candidate = candidates[i];
    bool blocks = true;
    if (candidate.consumer_kind_ ==
        ObPluginDependencyConsumerKind::PLUGIN) {
      if (candidate.consumer_plugin_id_ == plugin_id) {
        blocks = false;
      } else {
        ObPluginCatalogRecord consumer;
        const int consumer_ret = load_record(
            connection, candidate.consumer_plugin_id_, consumer);
        if (OB_ENTRY_NOT_EXIST == consumer_ret) {
          // An orphaned dependency row is catalog corruption.  Keep it as a
          // blocker rather than silently permitting data loss.
          blocks = true;
        } else if (OB_SUCCESS != consumer_ret) {
          ret = consumer_ret;
        } else {
          blocks = consumer.desired_state_ !=
                       ObPluginDesiredState::UNINSTALLED &&
                   consumer.generation_ == candidate.consumer_generation_;
        }
      }
    }
    if (OB_SUCCESS == ret && blocks) blockers.push_back(candidate);
  }
  return ret;
}

int ObPluginCatalog::Impl::mutate_dependency(
    ObPluginSqlConnection &connection,
    const ObPluginDependencySpec &dependency,
    const bool add,
    std::string &error)
{
  int ret = OB_SUCCESS;
  seekdb_plugin_semantic_version_t resolved_provider_version = {0, 0, 0};
  if (!connection.is_in_transaction()) {
    ret = OB_STATE_NOT_MATCH;
    error = "dependency mutation requires an active SQL catalog transaction";
  } else if (!valid_dependency(dependency)) {
    ret = OB_INVALID_ARGUMENT;
    error = "plugin dependency identity is invalid";
  }
  if (OB_SUCCESS == ret && add) {
    ObPluginCatalogRecord provider;
    bool unfinished = false;
    bool provider_resolves = false;
    if (OB_FAIL(load_record(connection, dependency.provider_plugin_id_,
                            provider))) {
      error = "plugin dependency provider is not installed";
    } else if (provider.desired_state_ != ObPluginDesiredState::ACTIVE ||
               provider.actual_state_ != ObPluginState::ACTIVE ||
               provider.generation_ != dependency.provider_generation_) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin dependency provider is not production ACTIVE";
    } else if (OB_FAIL(has_unfinished_operation(
                   connection, dependency.provider_plugin_id_, unfinished))) {
      error = "cannot inspect provider dependency barrier";
    } else if (unfinished) {
      ret = OB_EAGAIN;
      error = "plugin provider is changing state";
    } else {
      CatalogDependencyRequirement requirement;
      requirement.kind_ = dependency.dependency_kind_;
      requirement.dependency_id_ = dependency.dependency_id_;
      requirement.service_abi_major_ = dependency.service_abi_major_;
      requirement.requested_version_ = dependency.requested_version_;
      requirement.required_capabilities_ =
          dependency.required_capabilities_;
      if (OB_FAIL(durable_provider_resolves_requirement(
              connection, dependency.provider_plugin_id_,
              dependency.provider_generation_, requirement,
              resolved_provider_version, provider_resolves))) {
        error = "cannot validate the durable plugin dependency target";
      } else if (!provider_resolves) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin provider does not publish the requested durable target";
      }
    }
    if (OB_SUCCESS == ret &&
        dependency.consumer_kind_ ==
            ObPluginDependencyConsumerKind::PLUGIN) {
      ObPluginCatalogRecord consumer;
      if (dependency.consumer_plugin_id_.empty() ||
          dependency.consumer_id_ != dependency.consumer_plugin_id_ ||
          OB_FAIL(load_record(connection, dependency.consumer_plugin_id_,
                              consumer))) {
        if (OB_SUCCESS == ret) ret = OB_INVALID_ARGUMENT;
        error = "plugin dependency consumer is invalid";
      } else if (consumer.desired_state_ ==
                     ObPluginDesiredState::UNINSTALLED ||
                 consumer.generation_ != dependency.consumer_generation_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin dependency consumer generation is stale";
      }
    }
  }

  int64_t affected_rows = 0;
  if (OB_SUCCESS == ret && add) {
    static const char INSERT_SQL[] =
        "INSERT INTO __all_plugin_dependency("
        "consumer_kind,consumer_id,consumer_plugin_id,consumer_generation,"
        "provider_plugin_id,provider_generation,dependency_kind,dependency_id,"
        "service_abi_major,requested_min_version_major,requested_min_version_minor,"
        "requested_min_version_patch,requested_max_version_major,"
        "requested_max_version_minor,requested_max_version_patch,"
        "required_capabilities,optional,provider_version_major,"
        "provider_version_minor,provider_version_patch) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) "
        "ON DUPLICATE KEY UPDATE provider_generation=VALUES(provider_generation),"
        "provider_version_major=VALUES(provider_version_major),"
        "provider_version_minor=VALUES(provider_version_minor),"
        "provider_version_patch=VALUES(provider_version_patch)";
    ret = connection.execute(
        INSERT_SQL,
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int(
              static_cast<int32_t>(dependency.consumer_kind_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.consumer_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.consumer_plugin_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.consumer_generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.provider_plugin_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.provider_generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(dependency.dependency_kind_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.dependency_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(dependency.service_abi_major_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requested_version_.minimum_inclusive.major);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requested_version_.minimum_inclusive.minor);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requested_version_.minimum_inclusive.patch);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requested_version_.maximum_exclusive.major);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requested_version_.maximum_exclusive.minor);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                dependency.requested_version_.maximum_exclusive.patch);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.required_capabilities_));
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(resolved_provider_version.major);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(resolved_provider_version.minor);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(resolved_provider_version.patch);
          return bind_ret;
        },
        &affected_rows);
    if (OB_SUCCESS == ret && affected_rows == 0) {
      ret = OB_ENTRY_EXIST;
      error = "plugin dependency already exists";
    }
  } else if (OB_SUCCESS == ret) {
    ret = connection.execute(
        "DELETE FROM __all_plugin_dependency WHERE consumer_kind=? "
        "AND consumer_id=? AND consumer_plugin_id=? AND consumer_generation=? "
        "AND provider_plugin_id=? AND provider_generation=? "
        "AND dependency_kind=? AND dependency_id=? AND service_abi_major=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int(
              static_cast<int32_t>(dependency.consumer_kind_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.consumer_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.consumer_plugin_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.consumer_generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.provider_plugin_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(static_cast<int64_t>(
                dependency.provider_generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(dependency.dependency_kind_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, dependency.dependency_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(dependency.service_abi_major_);
          return bind_ret;
        },
        &affected_rows);
    if (OB_SUCCESS == ret && affected_rows == 0) {
      ret = OB_ENTRY_NOT_EXIST;
      error = "plugin dependency does not exist";
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::begin_disable(
    const std::string &plugin_id,
    const uint64_t expected_generation,
    std::unique_ptr<ObPluginDisablePermit> &permit,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  permit.reset();
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    ObPluginCatalogRecord record;
    std::vector<ObPluginRestrictBlocker> blockers;
    bool unfinished = false;
    uint64_t operation_sequence = 0;
    std::string operation_id;
    std::string unused_runtime_identity;
    std::unique_ptr<DisablePermit> prepared;
    const int64_t now = ObTimeUtility::current_time();
    if (!initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!catalog_valid_identifier(plugin_id) || expected_generation == 0 ||
               expected_generation > MAX_DURABLE_GENERATION) {
      ret = OB_INVALID_ARGUMENT;
      error = "plugin disable identity is invalid";
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin catalog writer";
    } else if (OB_FAIL(load_record(*guard.get_connection(), plugin_id,
                                   record))) {
      error = "plugin package is not installed";
    } else if (record.desired_state_ != ObPluginDesiredState::ACTIVE ||
               record.actual_state_ != ObPluginState::ACTIVE ||
               record.generation_ != expected_generation) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin generation is not production ACTIVE";
    } else if (OB_FAIL(has_unfinished_operation(
                   *guard.get_connection(), plugin_id, unfinished))) {
      error = "cannot inspect plugin catalog operations";
    } else if (unfinished) {
      ret = OB_EAGAIN;
      error = "plugin has an unfinished catalog operation";
    } else if (OB_FAIL(list_blockers(*guard.get_connection(), plugin_id,
                                     blockers))) {
      error = "cannot inspect durable plugin dependencies";
    } else if (!blockers.empty()) {
      ret = OB_OP_NOT_ALLOW;
      error = "UNINSTALL/DISABLE RESTRICT is blocked by durable dependencies";
    } else if (OB_FAIL(next_operation_id(
                   *guard.get_connection(), operation_sequence, operation_id,
                   unused_runtime_identity))) {
      error = "cannot allocate durable disable operation identity";
    }
    if (OB_SUCCESS == ret) {
      ret = guard->execute(
          "INSERT INTO __all_plugin_operation("
          "operation_id,plugin_id,generation,runtime_incarnation,kind,state,"
          "relative_path,package_digest,phase,status,actual_state,"
          "start_entered,candidate_prepared,stop_entered,error,operator_id,"
          "audit_id,gmt_create,gmt_modified) "
          "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = bind_string(binder, operation_id);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, plugin_id);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(
                  static_cast<int64_t>(expected_generation));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, record.runtime_incarnation_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationKind::DISABLE));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::DISABLING));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, record.relative_path_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, record.package_digest_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginDisablePhase::NONE));
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(OB_SUCCESS);
            if (OB_SUCCESS == bind_ret)
              bind_ret =
                  binder.bind_int(static_cast<int32_t>(ObPluginState::ACTIVE));
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_text("", 0);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, record.operator_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, record.audit_id_);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      ret = guard->execute(
          "UPDATE __all_plugin_package SET desired_state=?,operation_id=?,"
          "last_phase=?,last_status=0,last_error='',gmt_modified=? "
          "WHERE plugin_id=? AND generation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(
                static_cast<int32_t>(ObPluginDesiredState::DISABLED));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, operation_id);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginDisablePhase::NONE));
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, plugin_id);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(
                  static_cast<int64_t>(expected_generation));
            return bind_ret;
          });
    }
    if (OB_SUCCESS == ret) {
      prepared.reset(new (std::nothrow) DisablePermit(
          this, plugin_id, expected_generation, record.runtime_incarnation_,
          operation_id));
      if (!prepared) {
        ret = OB_ALLOCATE_MEMORY_FAILED;
        error = "cannot allocate durable disable permit";
      }
    }
    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin disable begin commit outcome is unknown";
      } else {
        prepared->armed_ = true;
        permit = std::move(prepared);
      }
    }
    if (OB_SUCCESS != ret && guard)
      rollback_noexcept(*guard.get_connection());
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    try {
      error = "plugin disable catalog allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    try {
      error = "unexpected plugin disable catalog failure";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::checkpoint_disable_stop(
    DisablePermit &disable_permit,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    int32_t operation_kind = -1;
    int32_t operation_state = -1;
    int32_t persisted_phase = -1;
    int32_t persisted_actual_state = -1;
    bool persisted_stop_entered = false;
    bool already_checkpointed = false;
    const int64_t now = ObTimeUtility::current_time();
    const char checkpoint_error[] =
        "plugin stop callback entered; completion is not yet durable";
    if (!initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!disable_permit.armed_ || disable_permit.finished_) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin disable permit cannot enter stop";
    } else if (disable_permit.stop_checkpointed_) {
      return OB_SUCCESS;
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin stop checkpoint writer";
    }
    if (OB_SUCCESS == ret) {
      ret = guard->query(
          "SELECT kind,state,phase,actual_state,stop_entered FROM "
          "__all_plugin_operation WHERE operation_id=? AND plugin_id=? "
          "AND generation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = bind_string(binder, disable_permit.operation_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  disable_permit.generation_));
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            operation_kind = reader.get_int(0);
            operation_state = reader.get_int(1);
            persisted_phase = reader.get_int(2);
            persisted_actual_state = reader.get_int(3);
            persisted_stop_entered = 0 != reader.get_int(4);
            return OB_ITER_END;
          });
      if (OB_SUCCESS != ret ||
          operation_kind != static_cast<int32_t>(
                                ObPluginCatalogOperationKind::DISABLE) ||
          (operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::DISABLING) &&
           operation_state != static_cast<int32_t>(
                                  ObPluginCatalogOperationState::
                                      RECOVERY_REQUIRED))) {
        if (OB_SUCCESS == ret) ret = OB_STATE_NOT_MATCH;
        error = "durable disable intent cannot enter stop";
      } else if (persisted_stop_entered) {
        already_checkpointed =
            persisted_phase ==
                static_cast<int32_t>(ObPluginDisablePhase::STOP) &&
            persisted_actual_state ==
                static_cast<int32_t>(ObPluginState::QUIESCING);
        if (!already_checkpointed) {
          ret = OB_INVALID_DATA;
          error = "durable stop checkpoint is internally inconsistent";
        }
      } else if (persisted_phase !=
                     static_cast<int32_t>(ObPluginDisablePhase::NONE) ||
                 persisted_actual_state !=
                     static_cast<int32_t>(ObPluginState::ACTIVE)) {
        ret = OB_INVALID_DATA;
        error = "durable disable intent has an invalid pre-stop state";
      }
    }
    if (OB_SUCCESS == ret && !already_checkpointed) {
      int64_t affected_rows = 0;
      ret = guard->execute(
          "UPDATE __all_plugin_operation SET phase=?,status=?,actual_state=?,"
          "stop_entered=1,error=?,gmt_modified=? WHERE operation_id=? AND "
          "stop_entered=0",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(
                static_cast<int32_t>(ObPluginDisablePhase::STOP));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(OB_TRANS_UNKNOWN);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginState::QUIESCING));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_text(
                  checkpoint_error, sizeof(checkpoint_error) - 1);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.operation_id_);
            return bind_ret;
          },
          &affected_rows);
      if (OB_SUCCESS == ret && affected_rows != 1) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin stop checkpoint fence changed";
      }
    }
    if (OB_SUCCESS == ret && !already_checkpointed) {
      int64_t affected_rows = 0;
      ret = guard->execute(
          "UPDATE __all_plugin_package SET actual_state=?,last_phase=?,"
          "last_status=?,last_error=?,gmt_modified=? WHERE plugin_id=? AND "
          "generation=? AND operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(
                static_cast<int32_t>(ObPluginState::QUIESCING));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginDisablePhase::STOP));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(OB_TRANS_UNKNOWN);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_text(
                  checkpoint_error, sizeof(checkpoint_error) - 1);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(static_cast<int64_t>(
                  disable_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.operation_id_);
            return bind_ret;
          },
          &affected_rows);
      if (OB_SUCCESS == ret && affected_rows != 1) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin package stop checkpoint fence changed";
      }
    }
    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin stop checkpoint commit outcome is unknown";
      } else {
        disable_permit.stop_checkpointed_ = true;
      }
    } else if (guard) {
      rollback_noexcept(*guard.get_connection());
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    try {
      error = "plugin stop checkpoint allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    try {
      error = "plugin stop checkpoint outcome is unknown";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::finish_disable(
    DisablePermit &disable_permit,
    const ObPluginRuntimeDisableResult &result,
    std::string &error) noexcept
{
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(mutex_);
    ObPluginSqlConnectionGuard guard(sql_client_);
    int32_t operation_kind = -1;
    int32_t operation_state = -1;
    int32_t persisted_phase = -1;
    int persisted_status = OB_SUCCESS;
    int32_t persisted_actual_state = -1;
    bool persisted_stop_entered = false;
    std::string persisted_error;
    bool already_finished = false;
    int64_t operation_affected_rows = 0;
    int64_t package_affected_rows = 0;
    const int64_t now = ObTimeUtility::current_time();
    const std::string runtime_error = bounded_error(result.error_);
    const bool safe_abort = result.actual_state_ == ObPluginState::ACTIVE &&
                            !result.stop_entered_ &&
                            (result.phase_ == ObPluginDisablePhase::NONE ||
                             result.phase_ == ObPluginDisablePhase::QUIESCE);
    const bool stopped = result.status_ == OB_SUCCESS &&
                         result.actual_state_ == ObPluginState::STOPPED &&
                         result.phase_ == ObPluginDisablePhase::COMPLETE;
    const ObPluginCatalogOperationState final_operation_state =
        safe_abort ? ObPluginCatalogOperationState::ABORTED
                   : (stopped ? ObPluginCatalogOperationState::COMPLETED
                              : ObPluginCatalogOperationState::RECOVERY_REQUIRED);
    const ObPluginDesiredState final_desired_state =
        safe_abort ? ObPluginDesiredState::ACTIVE
                   : ObPluginDesiredState::DISABLED;
    if (!initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!disable_permit.armed_ ||
               result.generation_ != disable_permit.generation_ ||
               (result.stop_entered_ &&
                !disable_permit.stop_checkpointed_) ||
               !valid_disable_runtime_result(result)) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin disable result does not match its durable permit";
    }
    if (OB_SUCCESS == ret) {
      // Retain the precise runtime observation before the first fallible
      // catalog write.  If finalization later fails, the permit destructor
      // persists this evidence instead of a weaker generic marker.
      disable_permit.observed_status_ = result.status_;
      disable_permit.observed_actual_state_ = result.actual_state_;
      disable_permit.observed_phase_ = result.phase_;
      disable_permit.observed_stop_entered_ = result.stop_entered_;
      disable_permit.observation_received_ = true;
      disable_permit.observed_error_ = runtime_error;
    }
    if (OB_SUCCESS == ret && !guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_SUCCESS == ret &&
               OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin catalog writer";
    }
    if (OB_SUCCESS == ret) {
      ret = guard->query(
          "SELECT kind,state,phase,status,actual_state,stop_entered,error "
          "FROM __all_plugin_operation WHERE operation_id=? "
          "AND plugin_id=? AND generation=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = bind_string(binder, disable_permit.operation_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(
                  static_cast<int64_t>(disable_permit.generation_));
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            operation_kind = reader.get_int(0);
            operation_state = reader.get_int(1);
            persisted_phase = reader.get_int(2);
            persisted_status = reader.get_int(3);
            persisted_actual_state = reader.get_int(4);
            persisted_stop_entered = 0 != reader.get_int(5);
            persisted_error = read_string(reader, 6);
            return OB_ITER_END;
          });
      if (OB_SUCCESS != ret || operation_kind != static_cast<int32_t>(
                                                  ObPluginCatalogOperationKind::DISABLE)) {
        if (OB_SUCCESS == ret) ret = OB_STATE_NOT_MATCH;
        error = "durable disable intent cannot accept a runtime result";
      } else if (persisted_stop_entered && !result.stop_entered_) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin disable result regressed the durable stop checkpoint";
      } else if (result.stop_entered_ &&
                 (!persisted_stop_entered ||
                  persisted_phase < static_cast<int32_t>(
                                        ObPluginDisablePhase::STOP))) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin disable result has no durable stop checkpoint";
      } else if (operation_state == static_cast<int32_t>(
                                        ObPluginCatalogOperationState::COMPLETED) ||
                 operation_state == static_cast<int32_t>(
                                        ObPluginCatalogOperationState::ABORTED)) {
        already_finished =
            operation_state == static_cast<int32_t>(final_operation_state) &&
            persisted_phase == static_cast<int32_t>(result.phase_) &&
            persisted_status == result.status_ &&
            persisted_actual_state ==
                static_cast<int32_t>(result.actual_state_) &&
            persisted_stop_entered == result.stop_entered_ &&
            persisted_error == runtime_error;
        if (!already_finished) {
          ret = OB_STATE_NOT_MATCH;
          error = "durable disable result conflicts with its completed intent";
        }
      } else if (operation_state != static_cast<int32_t>(
                                         ObPluginCatalogOperationState::DISABLING) &&
                 operation_state != static_cast<int32_t>(
                                         ObPluginCatalogOperationState::RECOVERY_REQUIRED)) {
        ret = OB_STATE_NOT_MATCH;
        error = "durable disable intent cannot accept a runtime result";
      }
    }
    if (OB_SUCCESS == ret && already_finished) {
      ObPluginCatalogRecord record;
      if (OB_FAIL(load_record(*guard.get_connection(),
                              disable_permit.plugin_id_, record))) {
        error = "completed disable package record is unavailable";
      } else if (record.generation_ != disable_permit.generation_ ||
                 record.operation_id_ != disable_permit.operation_id_ ||
                 record.desired_state_ != final_desired_state ||
                 record.actual_state_ != result.actual_state_ ||
                 record.last_phase_ != static_cast<int32_t>(result.phase_) ||
                 record.last_status_ != result.status_ ||
                 record.last_error_ != runtime_error) {
        ret = OB_INVALID_DATA;
        error = "completed disable intent does not match its package record";
      }
    }
    if (OB_SUCCESS == ret && !already_finished) {
      ret = guard->execute(
          "UPDATE __all_plugin_operation SET state=?,phase=?,status=?,"
          "actual_state=?,stop_entered=?,error=?,gmt_modified=? "
          "WHERE operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                binder.bind_int(static_cast<int32_t>(final_operation_state));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(result.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(result.actual_state_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.stop_entered_ ? 1 : 0);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, runtime_error);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.operation_id_);
            return bind_ret;
          },
          &operation_affected_rows);
      if (OB_SUCCESS == ret && operation_affected_rows != 1) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin disable operation fence changed during finalization";
      }
    }
    if (OB_SUCCESS == ret && !already_finished) {
      ret = guard->execute(
          "UPDATE __all_plugin_package SET desired_state=?,actual_state=?,"
          "last_phase=?,last_status=?,last_error=?,gmt_modified=? "
          "WHERE plugin_id=? AND generation=? AND operation_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret =
                binder.bind_int(static_cast<int32_t>(final_desired_state));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(result.actual_state_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(result.phase_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(result.status_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, runtime_error);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.plugin_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int64(
                  static_cast<int64_t>(disable_permit.generation_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, disable_permit.operation_id_);
            return bind_ret;
          },
          &package_affected_rows);
      if (OB_SUCCESS == ret && package_affected_rows != 1) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin package fence changed during disable finalization";
      }
    }
    if (OB_SUCCESS == ret) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin disable completion commit outcome is unknown";
      } else {
        disable_permit.finished_ = true;
      }
    } else if (guard) {
      rollback_noexcept(*guard.get_connection());
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    try {
      error = "plugin disable completion allocation failed";
    } catch (...) {
    }
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    try {
      error = "plugin disable completion outcome is unknown";
    } catch (...) {
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::uninstall(
    const std::string &plugin_id,
    const std::string &operator_id,
    const std::string &audit_id,
    std::vector<ObPluginRestrictBlocker> &blockers,
    std::string &error)
{
  int ret = OB_SUCCESS;
  blockers.clear();
  ObPluginSqlConnectionGuard guard(sql_client_);
  ObPluginCatalogRecord record;
  bool unfinished = false;
  uint64_t operation_sequence = 0;
  std::string operation_id;
  std::string unused_runtime_identity;
  const int64_t now = ObTimeUtility::current_time();
  if (!guard) {
    ret = OB_NOT_INIT;
    error = "plugin catalog database is unavailable";
  } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
    error = "cannot reserve plugin catalog writer";
  } else if (OB_FAIL(load_record(*guard.get_connection(), plugin_id, record))) {
    error = "plugin package is not installed";
  } else if (record.desired_state_ == ObPluginDesiredState::UNINSTALLED) {
    ret = OB_ENTRY_NOT_EXIST;
    error = "plugin package is already uninstalled";
  } else if (OB_FAIL(list_blockers(*guard.get_connection(), plugin_id,
                                   blockers))) {
    error = "cannot inspect durable plugin dependencies";
  } else if (!blockers.empty()) {
    ret = OB_OP_NOT_ALLOW;
    error = "UNINSTALL EXTENSION RESTRICT is blocked by durable dependencies";
  } else if (record.actual_state_ != ObPluginState::STOPPED &&
             record.actual_state_ != ObPluginState::FAILED &&
             record.actual_state_ != ObPluginState::DISCOVERED) {
    ret = OB_STATE_NOT_MATCH;
    error = "plugin runtime must be stopped before uninstall";
  } else if (OB_FAIL(has_unfinished_operation(
                 *guard.get_connection(), plugin_id, unfinished))) {
    error = "cannot inspect plugin catalog operations";
  } else if (unfinished) {
    ret = OB_EAGAIN;
    error = "plugin has an unfinished catalog operation";
  } else if (OB_FAIL(next_operation_id(
                 *guard.get_connection(), operation_sequence, operation_id,
                 unused_runtime_identity))) {
    error = "cannot allocate durable uninstall operation identity";
  }
  if (OB_SUCCESS == ret) {
    ret = guard->execute(
        "INSERT INTO __all_plugin_operation("
        "operation_id,plugin_id,generation,runtime_incarnation,kind,state,"
        "relative_path,package_digest,phase,status,actual_state,start_entered,"
        "candidate_prepared,stop_entered,error,operator_id,audit_id,"
        "gmt_create,gmt_modified) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = bind_string(binder, operation_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, plugin_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(
                static_cast<int64_t>(record.generation_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, record.runtime_incarnation_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginCatalogOperationKind::UNINSTALL));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginCatalogOperationState::COMPLETED));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, record.relative_path_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, record.package_digest_);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(OB_SUCCESS);
          if (OB_SUCCESS == bind_ret)
            bind_ret =
                binder.bind_int(static_cast<int32_t>(ObPluginState::STOPPED));
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int(0);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_text("", 0);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, operator_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, audit_id);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
          return bind_ret;
        });
  }
  if (OB_SUCCESS == ret) {
    ret = guard->execute(
        "DELETE FROM __all_plugin_dependency WHERE consumer_kind=? "
        "AND consumer_plugin_id=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int(static_cast<int32_t>(
              ObPluginDependencyConsumerKind::PLUGIN));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, plugin_id);
          return bind_ret;
        });
  }
  if (OB_SUCCESS == ret) {
    ret = guard->execute(
        "UPDATE __all_plugin_package SET desired_state=?,actual_state=?,"
        "operation_id=?,last_phase=0,last_status=0,last_error='',"
        "operator_id=?,audit_id=?,gmt_modified=? WHERE plugin_id=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int(
              static_cast<int32_t>(ObPluginDesiredState::UNINSTALLED));
          if (OB_SUCCESS == bind_ret)
            bind_ret =
                binder.bind_int(static_cast<int32_t>(ObPluginState::STOPPED));
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, operation_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, operator_id);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, audit_id);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, plugin_id);
          return bind_ret;
        });
  }
  if (OB_SUCCESS == ret) {
    if (OB_FAIL(guard->commit())) {
      ret = OB_TRANS_UNKNOWN;
      error = "plugin uninstall commit outcome is unknown";
    }
  } else if (guard) {
    rollback_noexcept(*guard.get_connection());
  }
  return ret;
}

int ObPluginCatalog::Impl::prepare_startup(
    std::vector<ObPluginStartupEntry> &entries,
    std::string &error)
{
  struct UnfinishedOperation
  {
    UnfinishedOperation()
        : kind_(-1), state_(-1), generation_(0), operation_id_(),
          runtime_incarnation_(), relative_path_(), package_digest_(),
          phase_(0), status_(0), actual_state_(0),
          candidate_prepared_(false), stop_entered_(false)
    {}
    int32_t kind_;
    int32_t state_;
    uint64_t generation_;
    std::string operation_id_;
    std::string runtime_incarnation_;
    std::string relative_path_;
    std::string package_digest_;
    int32_t phase_;
    int status_;
    int32_t actual_state_;
    bool candidate_prepared_;
    bool stop_entered_;
  };

  int ret = OB_SUCCESS;
  entries.clear();
  ObPluginSqlConnectionGuard guard(sql_client_);
  std::vector<std::string> plugin_ids;
  std::map<std::string, ObPluginCatalogRecord> records;
  std::map<std::string, ObPluginStartupEntry> unordered_entries;
  const int64_t now = ObTimeUtility::current_time();
  if (!guard) {
    ret = OB_NOT_INIT;
    error = "plugin catalog database is unavailable";
  } else if (startup_prepared_) {
    entries = startup_plan_;
    return OB_SUCCESS;
  } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
    error = "cannot reserve plugin startup catalog writer";
  } else {
    ret = guard->query(
        "SELECT plugin_id FROM __all_plugin_package ORDER BY plugin_id",
        nullptr,
        [&](ObPluginSqlRowReader &reader) {
          plugin_ids.push_back(read_string(reader, 0));
          return OB_SUCCESS;
        });
    if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
  }

  for (size_t i = 0; OB_SUCCESS == ret && i < plugin_ids.size(); ++i) {
    ObPluginCatalogRecord record;
    if (OB_FAIL(load_record(*guard.get_connection(), plugin_ids[i], record))) {
      error = "cannot read plugin package during startup recovery";
    } else {
      records.insert(std::make_pair(record.plugin_id_, record));
    }
  }

  for (size_t i = 0; OB_SUCCESS == ret && i < plugin_ids.size(); ++i) {
    ObPluginCatalogRecord &record = records.find(plugin_ids[i])->second;
    std::vector<UnfinishedOperation> unfinished;
    ret = guard->query(
        "SELECT kind,state,generation,operation_id,runtime_incarnation,"
        "relative_path,package_digest,phase,status,actual_state,"
        "candidate_prepared,stop_entered FROM __all_plugin_operation "
        "WHERE plugin_id=? ORDER BY gmt_create",
        [&](ObPluginSqlBinder &binder) {
          return bind_string(binder, record.plugin_id_);
        },
        [&](ObPluginSqlRowReader &reader) {
          const int32_t state = reader.get_int(1);
          if (unfinished_operation_state(state)) {
            UnfinishedOperation operation;
            operation.kind_ = reader.get_int(0);
            operation.state_ = state;
            operation.generation_ =
                static_cast<uint64_t>(reader.get_int64(2));
            operation.operation_id_ = read_string(reader, 3);
            operation.runtime_incarnation_ = read_string(reader, 4);
            operation.relative_path_ = read_string(reader, 5);
            operation.package_digest_ = read_string(reader, 6);
            operation.phase_ = reader.get_int(7);
            operation.status_ = reader.get_int(8);
            operation.actual_state_ = reader.get_int(9);
            operation.candidate_prepared_ = 0 != reader.get_int(10);
            operation.stop_entered_ = 0 != reader.get_int(11);
            unfinished.push_back(operation);
          }
          return OB_SUCCESS;
        });
    if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
    if (OB_SUCCESS == ret && unfinished.size() > 1) {
      ret = OB_INVALID_DATA;
      error = "plugin has multiple unfinished durable operations";
    }
    const UnfinishedOperation *operation =
        unfinished.empty() ? nullptr : &unfinished[0];

    if (OB_SUCCESS == ret &&
        record.desired_state_ == ObPluginDesiredState::ACTIVE) {
      if (record.actual_state_ == ObPluginState::BLOCKED) {
        ret = OB_STATE_NOT_MATCH;
        error = "blocked plugin requires explicit administrator recovery";
      } else if (nullptr != operation &&
                 operation->kind_ != static_cast<int32_t>(
                                         ObPluginCatalogOperationKind::ACTIVATE)) {
        ret = OB_INVALID_DATA;
        error = "ACTIVE plugin has a non-activation recovery intent";
      } else {
        ObPluginStartupEntry entry;
        bool fresh_attempt = nullptr == operation;
        entry.plugin_id_ = record.plugin_id_;
        entry.relative_path_ = record.relative_path_;
        if (nullptr != operation) {
          if (operation->generation_ != record.generation_ ||
              operation->operation_id_ != record.operation_id_ ||
              operation->runtime_incarnation_ !=
                  record.runtime_incarnation_ ||
              operation->relative_path_ != record.relative_path_ ||
              operation->package_digest_ != record.package_digest_) {
            ret = OB_INVALID_DATA;
            error = "activation recovery intent does not match package fence";
          } else if ((operation->state_ == static_cast<int32_t>(
                                               ObPluginCatalogOperationState::PROMOTE_PENDING) &&
                       !operation->candidate_prepared_) ||
                      (operation->state_ == static_cast<int32_t>(
                                               ObPluginCatalogOperationState::CATALOG_BEGIN) &&
                       operation->candidate_prepared_)) {
            ret = OB_INVALID_DATA;
            error = "activation recovery decision marker is inconsistent";
          } else if (!operation->candidate_prepared_) {
            // No ownership/dependency decision was committed.  The old
            // runtime is gone, so archive this pre-commit attempt and let the
            // normal loader create a new identity.  Its manifest dependencies
            // can then participate in bounded startup retry ordering.
            ret = guard->execute(
                "UPDATE __all_plugin_operation SET state=?,status=?,"
                "actual_state=?,error=?,gmt_modified=? WHERE operation_id=?",
                [&](ObPluginSqlBinder &binder) {
                  int bind_ret = binder.bind_int(static_cast<int32_t>(
                      ObPluginCatalogOperationState::ABORTED));
                  if (OB_SUCCESS == bind_ret)
                    bind_ret = binder.bind_int(OB_CANCELED);
                  if (OB_SUCCESS == bind_ret)
                    bind_ret = binder.bind_int(
                        static_cast<int32_t>(ObPluginState::DISCOVERED));
                  if (OB_SUCCESS == bind_ret)
                    bind_ret = binder.bind_text(
                        "pre-commit activation archived during startup",
                        sizeof("pre-commit activation archived during startup") -
                            1);
                  if (OB_SUCCESS == bind_ret)
                    bind_ret = binder.bind_int64(now);
                  if (OB_SUCCESS == bind_ret)
                    bind_ret = bind_string(binder, operation->operation_id_);
                  return bind_ret;
                });
            fresh_attempt = OB_SUCCESS == ret;
          } else {
            entry.exact_recovery_ = true;
            entry.recovery_.relative_path_ = operation->relative_path_;
            entry.recovery_.plugin_id_ = record.plugin_id_;
            entry.recovery_.package_digest_ = operation->package_digest_;
            entry.recovery_.generation_ = operation->generation_;
            entry.recovery_.runtime_incarnation_ =
                operation->runtime_incarnation_;
            entry.recovery_.operation_id_ = operation->operation_id_;
          }
        }
        if (OB_SUCCESS == ret && fresh_attempt) {
          // A completed ACTIVE record or an archived pre-commit attempt has no
          // runtime in this process.  A new activation receives a fresh fence.
          ret = guard->execute(
              "UPDATE __all_plugin_package SET actual_state=?,last_phase=0,"
              "last_status=0,last_error='',gmt_modified=? WHERE plugin_id=?",
              [&](ObPluginSqlBinder &binder) {
                int bind_ret = binder.bind_int(
                    static_cast<int32_t>(ObPluginState::DISCOVERED));
                if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
                if (OB_SUCCESS == bind_ret)
                  bind_ret = bind_string(binder, record.plugin_id_);
                return bind_ret;
              });
          record.actual_state_ = ObPluginState::DISCOVERED;
        }
        if (OB_SUCCESS == ret)
          unordered_entries.insert(std::make_pair(record.plugin_id_, entry));
      }
    } else if (OB_SUCCESS == ret &&
               record.desired_state_ == ObPluginDesiredState::DISABLED) {
      const bool unentered_disable =
          nullptr != operation && !operation->stop_entered_ &&
          operation->phase_ ==
              static_cast<int32_t>(ObPluginDisablePhase::NONE) &&
          operation->actual_state_ ==
              static_cast<int32_t>(ObPluginState::ACTIVE) &&
          (operation->status_ == OB_SUCCESS ||
           operation->status_ == OB_TRANS_UNKNOWN);
      const bool safe_drain_failure =
          nullptr != operation && !operation->stop_entered_ &&
          operation->phase_ ==
              static_cast<int32_t>(ObPluginDisablePhase::DRAIN) &&
          operation->actual_state_ ==
              static_cast<int32_t>(ObPluginState::QUIESCING) &&
          operation->status_ != OB_SUCCESS;
      const bool unsafe_stop_observation =
          nullptr != operation && operation->stop_entered_ &&
          operation->phase_ >=
              static_cast<int32_t>(ObPluginDisablePhase::STOP) &&
          operation->phase_ <=
              static_cast<int32_t>(ObPluginDisablePhase::COMPLETE) &&
          (operation->actual_state_ ==
               static_cast<int32_t>(ObPluginState::QUIESCING) ||
           operation->actual_state_ ==
               static_cast<int32_t>(ObPluginState::BLOCKED) ||
           operation->actual_state_ ==
               static_cast<int32_t>(ObPluginState::STOPPED));
      if (nullptr != operation &&
          operation->kind_ != static_cast<int32_t>(
                                  ObPluginCatalogOperationKind::DISABLE)) {
        ret = OB_INVALID_DATA;
        error = "DISABLED plugin has a non-disable recovery intent";
      } else if (nullptr != operation &&
                 (operation->generation_ != record.generation_ ||
                  operation->operation_id_ != record.operation_id_ ||
                  operation->runtime_incarnation_ !=
                      record.runtime_incarnation_ ||
                  operation->relative_path_ != record.relative_path_ ||
                  operation->package_digest_ != record.package_digest_)) {
        ret = OB_INVALID_DATA;
        error = "disable recovery intent does not match package fence";
      } else if ((nullptr == operation &&
                  record.actual_state_ != ObPluginState::STOPPED) ||
                 (nullptr != operation && !unentered_disable &&
                  !safe_drain_failure && !unsafe_stop_observation)) {
        ret = OB_INVALID_DATA;
        error = "disable recovery observation is internally inconsistent";
      } else if (record.actual_state_ == ObPluginState::BLOCKED ||
                 unsafe_stop_observation) {
        // A process boundary drains leases and removes ordinary in-process
        // runtime, but it cannot prove that a fallible stop callback or its
        // external side effects completed.  Preserve the evidence and require
        // explicit administrative recovery.
        ret = OB_STATE_NOT_MATCH;
        error = "failed plugin stop requires explicit administrator recovery";
      } else {
        if (nullptr != operation) {
          ret = guard->execute(
              "UPDATE __all_plugin_operation SET state=?,status=0,"
              "actual_state=?,phase=?,error='',gmt_modified=? "
              "WHERE operation_id=?",
              [&](ObPluginSqlBinder &binder) {
                int bind_ret = binder.bind_int(static_cast<int32_t>(
                    ObPluginCatalogOperationState::COMPLETED));
                if (OB_SUCCESS == bind_ret)
                  bind_ret = binder.bind_int(
                      static_cast<int32_t>(ObPluginState::STOPPED));
                if (OB_SUCCESS == bind_ret)
                  bind_ret = binder.bind_int(static_cast<int32_t>(
                      ObPluginDisablePhase::COMPLETE));
                if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
                if (OB_SUCCESS == bind_ret)
                  bind_ret = bind_string(binder, operation->operation_id_);
                return bind_ret;
              });
        }
        if (OB_SUCCESS == ret) {
          ret = guard->execute(
              "UPDATE __all_plugin_package SET actual_state=?,last_phase=?,"
              "last_status=0,last_error='',gmt_modified=? WHERE plugin_id=?",
              [&](ObPluginSqlBinder &binder) {
                int bind_ret = binder.bind_int(
                    static_cast<int32_t>(ObPluginState::STOPPED));
                if (OB_SUCCESS == bind_ret)
                  bind_ret = binder.bind_int(static_cast<int32_t>(
                      ObPluginDisablePhase::COMPLETE));
                if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
                if (OB_SUCCESS == bind_ret)
                  bind_ret = bind_string(binder, record.plugin_id_);
                return bind_ret;
              });
          record.actual_state_ = ObPluginState::STOPPED;
        }
      }
    } else if (OB_SUCCESS == ret) {
      if (nullptr != operation) {
        ret = OB_INVALID_DATA;
        error = "uninstalled plugin retains an unfinished operation";
      } else if (record.actual_state_ != ObPluginState::STOPPED) {
        ret = guard->execute(
            "UPDATE __all_plugin_package SET actual_state=?,gmt_modified=? "
            "WHERE plugin_id=?",
            [&](ObPluginSqlBinder &binder) {
              int bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginState::STOPPED));
              if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
              if (OB_SUCCESS == bind_ret)
                bind_ret = bind_string(binder, record.plugin_id_);
              return bind_ret;
            });
        record.actual_state_ = ObPluginState::STOPPED;
      }
    }
  }

  // Persistent plugin-to-plugin edges determine startup order.  Edges from
  // archived generations are ignored; orphaned/missing providers fail closed.
  std::map<std::string, std::set<std::string> > outgoing;
  std::map<std::string, size_t> indegree;
  for (const auto &item : unordered_entries) indegree[item.first] = 0;
  if (OB_SUCCESS == ret && !unordered_entries.empty()) {
    ret = guard->query(
        "SELECT consumer_plugin_id,consumer_generation,provider_plugin_id "
        "FROM __all_plugin_dependency WHERE consumer_kind=?",
        [&](ObPluginSqlBinder &binder) {
          return binder.bind_int(static_cast<int32_t>(
              ObPluginDependencyConsumerKind::PLUGIN));
        },
        [&](ObPluginSqlRowReader &reader) {
          const std::string consumer = read_string(reader, 0);
          const uint64_t consumer_generation =
              static_cast<uint64_t>(reader.get_int64(1));
          const std::string provider = read_string(reader, 2);
          const auto consumer_record = records.find(consumer);
          if (consumer_record != records.end() &&
              consumer_record->second.desired_state_ ==
                  ObPluginDesiredState::ACTIVE &&
              consumer_record->second.generation_ == consumer_generation) {
            const auto provider_record = records.find(provider);
            if (provider_record == records.end() ||
                provider_record->second.desired_state_ !=
                    ObPluginDesiredState::ACTIVE ||
                unordered_entries.count(provider) == 0) {
              return OB_STATE_NOT_MATCH;
            }
            if (provider != consumer &&
                outgoing[provider].insert(consumer).second) {
              ++indegree[consumer];
            }
          }
          return OB_SUCCESS;
        });
    if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
    if (OB_STATE_NOT_MATCH == ret)
      error = "plugin startup dependency provider is not desired ACTIVE";
  }

  if (OB_SUCCESS == ret) {
    std::set<std::string> ready_plugins;
    for (const auto &item : indegree) {
      if (item.second == 0) ready_plugins.insert(item.first);
    }
    while (!ready_plugins.empty()) {
      const std::string plugin_id = *ready_plugins.begin();
      ready_plugins.erase(ready_plugins.begin());
      entries.push_back(unordered_entries.find(plugin_id)->second);
      const auto dependants = outgoing.find(plugin_id);
      if (dependants != outgoing.end()) {
        for (const std::string &dependant : dependants->second) {
          size_t &degree = indegree[dependant];
          if (degree == 0) {
            ret = OB_ERR_UNEXPECTED;
            error = "plugin startup DAG accounting underflow";
            break;
          }
          --degree;
          if (degree == 0) ready_plugins.insert(dependant);
        }
      }
      if (OB_SUCCESS != ret) break;
    }
    if (OB_SUCCESS == ret && entries.size() != unordered_entries.size()) {
      ret = OB_INVALID_DATA;
      error = "plugin startup dependency graph contains a cycle";
    }
  }

  std::vector<ObPluginStartupEntry> prepared_plan;
  if (OB_SUCCESS == ret) prepared_plan = entries;
  if (OB_SUCCESS == ret) {
    if (!startup_prepared_) {
      if (OB_FAIL(guard->commit())) {
        ret = OB_TRANS_UNKNOWN;
        error = "plugin startup preparation commit outcome is unknown";
      } else {
        startup_plan_.swap(prepared_plan);
        startup_prepared_ = true;
      }
    }
  } else if (guard) {
    entries.clear();
    rollback_noexcept(*guard.get_connection());
  }
  return ret;
}

int ObPluginCatalog::Impl::ready(std::string &error) const
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> lock(mutex_);
  ObPluginSqlConnectionGuard guard(sql_client_);
  if (!initialized_) {
    ret = OB_NOT_INIT;
    error = "plugin catalog is not initialized";
  } else if (!startup_prepared_) {
    ret = OB_STATE_NOT_MATCH;
    error = "plugin startup recovery gate was not executed";
  } else if (!guard) {
    ret = OB_NOT_INIT;
    error = "plugin catalog database is unavailable";
  } else {
    int64_t count = 0;
    ret = query_count(
        *guard.get_connection(),
        "SELECT COUNT(*) FROM __all_plugin_package WHERE "
        "(desired_state=? AND actual_state<>?) OR actual_state=?",
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = binder.bind_int(
              static_cast<int32_t>(ObPluginDesiredState::ACTIVE));
          if (OB_SUCCESS == bind_ret)
            bind_ret =
                binder.bind_int(static_cast<int32_t>(ObPluginState::ACTIVE));
          if (OB_SUCCESS == bind_ret)
            bind_ret =
                binder.bind_int(static_cast<int32_t>(ObPluginState::BLOCKED));
          return bind_ret;
        },
        count);
    if (OB_SUCCESS == ret && count != 0) {
      ret = OB_STATE_NOT_MATCH;
      error = "plugin desired/actual state has not converged before server ready";
    }
    if (OB_SUCCESS == ret) {
      ret = query_count(
          *guard.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_operation WHERE state IN(?,?,?,?)",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginCatalogOperationState::CATALOG_BEGIN));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::PROMOTE_PENDING));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::DISABLING));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginCatalogOperationState::RECOVERY_REQUIRED));
            return bind_ret;
          },
          count);
      if (OB_SUCCESS == ret && count != 0) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin catalog retains unfinished operations before server ready";
      }
    }
    if (OB_SUCCESS == ret) {
      // Every live dependency must resolve to a current production ACTIVE
      // provider.  Archived plugin-consumer edges are ignored by the query.
      ret = query_count(
          *guard.get_connection(),
          "SELECT COUNT(*) FROM __all_plugin_dependency d "
          "LEFT JOIN __all_plugin_package p ON p.plugin_id=d.provider_plugin_id "
          "LEFT JOIN __all_plugin_package c ON c.plugin_id=d.consumer_plugin_id "
          "WHERE (d.consumer_kind=? AND c.plugin_id IS NULL) OR "
          "((d.consumer_kind<>? OR (c.desired_state<>? AND "
          "c.generation=d.consumer_generation)) AND "
          "(p.plugin_id IS NULL OR p.desired_state<>? OR p.actual_state<>? "
          "OR p.generation<>d.provider_generation))",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyConsumerKind::PLUGIN));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDependencyConsumerKind::PLUGIN));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDesiredState::UNINSTALLED));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                static_cast<int32_t>(ObPluginDesiredState::ACTIVE));
            if (OB_SUCCESS == bind_ret)
              bind_ret =
                  binder.bind_int(static_cast<int32_t>(ObPluginState::ACTIVE));
            return bind_ret;
          },
          count);
      if (OB_SUCCESS == ret && count != 0) {
        ret = OB_STATE_NOT_MATCH;
        error = "plugin dependency provider is not ACTIVE before server ready";
      }
    }
    if (OB_SUCCESS == ret) {
      std::vector<CatalogReadyDependency> dependencies;
      ret = guard->query(
          "SELECT d.provider_plugin_id,d.provider_generation,"
          "d.dependency_kind,d.dependency_id,d.service_abi_major,"
          "d.requested_min_version_major,d.requested_min_version_minor,"
          "d.requested_min_version_patch,d.requested_max_version_major,"
          "d.requested_max_version_minor,d.requested_max_version_patch,"
          "d.required_capabilities,d.provider_version_major,"
          "d.provider_version_minor,d.provider_version_patch "
          "FROM __all_plugin_dependency d LEFT JOIN __all_plugin_package c "
          "ON c.plugin_id=d.consumer_plugin_id WHERE d.consumer_kind<>? OR "
          "(c.plugin_id IS NOT NULL AND c.desired_state<>? AND "
          "c.generation=d.consumer_generation)",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(static_cast<int32_t>(
                ObPluginDependencyConsumerKind::PLUGIN));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDesiredState::UNINSTALLED));
            return bind_ret;
          },
          [&](ObPluginSqlRowReader &reader) {
            CatalogReadyDependency dependency;
            dependency.provider_plugin_id_ = read_string(reader, 0);
            dependency.provider_generation_ =
                static_cast<uint64_t>(reader.get_int64(1));
            dependency.requirement_.kind_ =
                static_cast<ObPluginDependencyKind>(reader.get_int(2));
            dependency.requirement_.dependency_id_ = read_string(reader, 3);
            dependency.requirement_.service_abi_major_ =
                static_cast<uint32_t>(reader.get_int64(4));
            dependency.requirement_.requested_version_.minimum_inclusive = {
                static_cast<uint32_t>(reader.get_int64(5)),
                static_cast<uint32_t>(reader.get_int64(6)),
                static_cast<uint32_t>(reader.get_int64(7))};
            dependency.requirement_.requested_version_.maximum_exclusive = {
                static_cast<uint32_t>(reader.get_int64(8)),
                static_cast<uint32_t>(reader.get_int64(9)),
                static_cast<uint32_t>(reader.get_int64(10))};
            dependency.requirement_.required_capabilities_ =
                static_cast<uint64_t>(reader.get_int64(11));
            dependency.expected_version_ = {
                static_cast<uint32_t>(reader.get_int64(12)),
                static_cast<uint32_t>(reader.get_int64(13)),
                static_cast<uint32_t>(reader.get_int64(14))};
            if (!catalog_valid_identifier(dependency.provider_plugin_id_) ||
                dependency.provider_generation_ == 0 ||
                !valid_catalog_requirement(dependency.requirement_)) {
              return OB_INVALID_DATA;
            }
            dependencies.push_back(dependency);
            return OB_SUCCESS;
          });
      if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
      if (OB_INVALID_DATA == ret)
        error = "live plugin dependency contract is invalid before server ready";
      for (size_t i = 0; OB_SUCCESS == ret && i < dependencies.size(); ++i) {
        bool resolved = false;
        seekdb_plugin_semantic_version_t resolved_version = {0, 0, 0};
        if (OB_FAIL(durable_provider_resolves_requirement(
                *guard.get_connection(), dependencies[i].provider_plugin_id_,
                dependencies[i].provider_generation_,
                dependencies[i].requirement_, resolved_version, resolved))) {
          error = "cannot validate live plugin dependency target";
        } else if (!resolved ||
                   !catalog_same_version(
                       resolved_version, dependencies[i].expected_version_)) {
          ret = OB_STATE_NOT_MATCH;
          error = "live plugin dependency target changed before server ready";
        }
      }
    }
  }
  return ret;
}

int ObPluginCatalog::Impl::install(const ObPluginPackageInstallSpec &spec,
                                   std::string &error)
{
  int ret = OB_SUCCESS;
  ObPluginSqlConnectionGuard guard(sql_client_);
  ObPluginCatalogRecord existing;
  bool exists = false;
  const int64_t now = ObTimeUtility::current_time();
  if (!guard) {
    ret = OB_NOT_INIT;
    error = "plugin catalog database is unavailable";
  } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
    error = "cannot reserve plugin catalog writer";
  } else {
    const int get_ret = load_record(*guard.get_connection(),
                                    spec.artifact_.plugin_id_, existing);
    if (OB_SUCCESS == get_ret) {
      exists = true;
    } else if (OB_ENTRY_NOT_EXIST != get_ret) {
      ret = get_ret;
      error = "cannot read existing plugin package state";
    }
  }

  if (OB_SUCCESS == ret && exists) {
    if (existing.desired_state_ != ObPluginDesiredState::UNINSTALLED) {
      ret = OB_ENTRY_EXIST;
      error = "plugin package is already installed";
    } else if (existing.relative_path_ != spec.relative_path_ ||
               existing.build_id_ != spec.artifact_.build_id_ ||
               existing.package_digest_ != spec.artifact_.package_digest_ ||
               !catalog_same_version(existing.package_version_,
                             spec.artifact_.package_version_) ||
               existing.catalog_version_ != spec.artifact_.catalog_version_ ||
               existing.data_format_version_ !=
                   spec.artifact_.data_format_version_) {
      ret = OB_NOT_SUPPORTED;
      error = "R1 cannot replace an uninstalled plugin identity with a different package";
    } else {
      ret = guard->execute(
          "UPDATE __all_plugin_package SET verification_level=?,"
          "desired_state=?,actual_state=?,runtime_incarnation='',"
          "operation_id='',last_phase=0,last_status=0,last_error='',"
          "operator_id=?,audit_id=?,gmt_modified=? WHERE plugin_id=?",
          [&](ObPluginSqlBinder &binder) {
            int bind_ret = binder.bind_int(
                static_cast<int32_t>(spec.verification_level_));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(static_cast<int32_t>(
                  ObPluginDesiredState::ACTIVE));
            if (OB_SUCCESS == bind_ret)
              bind_ret = binder.bind_int(
                  static_cast<int32_t>(ObPluginState::DISCOVERED));
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, spec.operator_id_);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, spec.audit_id_);
            if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
            if (OB_SUCCESS == bind_ret)
              bind_ret = bind_string(binder, spec.artifact_.plugin_id_);
            return bind_ret;
          });
    }
  } else if (OB_SUCCESS == ret) {
    static const char INSERT_SQL[] =
        "INSERT INTO __all_plugin_package("
        "plugin_id,relative_path,build_id,package_digest,version_major,"
        "version_minor,version_patch,catalog_version,data_format_version,"
        "verification_level,desired_state,actual_state,generation,"
        "runtime_incarnation,operation_id,last_phase,last_status,last_error,"
        "operator_id,audit_id,gmt_create,gmt_modified) "
        "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,'','',0,0,'',?,?,?,?)";
    ret = guard->execute(
        INSERT_SQL,
        [&](ObPluginSqlBinder &binder) {
          int bind_ret = bind_string(binder, spec.artifact_.plugin_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, spec.relative_path_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, spec.artifact_.build_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, spec.artifact_.package_digest_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(spec.artifact_.package_version_.major);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(spec.artifact_.package_version_.minor);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(spec.artifact_.package_version_.patch);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(spec.artifact_.catalog_version_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int64(spec.artifact_.data_format_version_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(spec.verification_level_));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(ObPluginDesiredState::ACTIVE));
          if (OB_SUCCESS == bind_ret)
            bind_ret = binder.bind_int(
                static_cast<int32_t>(ObPluginState::DISCOVERED));
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(0);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, spec.operator_id_);
          if (OB_SUCCESS == bind_ret)
            bind_ret = bind_string(binder, spec.audit_id_);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
          if (OB_SUCCESS == bind_ret) bind_ret = binder.bind_int64(now);
          return bind_ret;
        });
  }

  if (OB_SUCCESS == ret) {
    if (OB_FAIL(guard->commit())) {
      ret = OB_TRANS_UNKNOWN;
      error = "plugin install commit outcome is unknown";
    }
  } else if (guard) {
    rollback_noexcept(*guard.get_connection());
  }
  return ret;
}

int ObPluginCatalog::Impl::get(const std::string &plugin_id,
                               ObPluginCatalogRecord &record) const
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> lock(mutex_);
  if (!initialized_) {
    ret = OB_NOT_INIT;
  } else if (!catalog_valid_identifier(plugin_id)) {
    ret = OB_INVALID_ARGUMENT;
  } else {
    ObPluginSqlConnectionGuard guard(sql_client_);
    ret = guard ? load_record(*guard.get_connection(), plugin_id, record)
                : OB_NOT_INIT;
  }
  return ret;
}

int ObPluginCatalog::Impl::list(
    std::vector<ObPluginCatalogRecord> &records) const
{
  int ret = OB_SUCCESS;
  std::lock_guard<std::mutex> lock(mutex_);
  records.clear();
  if (!initialized_) {
    ret = OB_NOT_INIT;
  } else {
    ObPluginSqlConnectionGuard guard(sql_client_);
    if (!guard) {
      ret = OB_NOT_INIT;
    } else {
      ret = guard->query(
          "SELECT plugin_id FROM __all_plugin_package ORDER BY plugin_id",
          nullptr,
          [&](ObPluginSqlRowReader &reader) {
            ObPluginCatalogRecord record;
            record.plugin_id_ = read_string(reader, 0);
            records.push_back(record);
            return OB_SUCCESS;
          });
      if (OB_ENTRY_NOT_EXIST == ret) ret = OB_SUCCESS;
      for (size_t i = 0; OB_SUCCESS == ret && i < records.size(); ++i) {
        const std::string plugin_id = records[i].plugin_id_;
        ret = load_record(*guard.get_connection(), plugin_id, records[i]);
      }
    }
  }
  return ret;
}

ObPluginCatalog::ObPluginCatalog() : impl_(new (std::nothrow) Impl())
{}

ObPluginCatalog::~ObPluginCatalog() = default;

int ObPluginCatalog::init(common::ObISQLClient *sql_client)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (impl_->initialized_) {
      ret = OB_INIT_TWICE;
    } else if (nullptr == sql_client) {
      ret = OB_INVALID_ARGUMENT;
    } else {
      impl_->sql_client_ = sql_client;
      try {
        ret = impl_->initialize_schema();
      } catch (...) {
        // Keep cleanup under the same mutex as initialization.  A concurrent
        // init must never observe or overwrite a partially initialized SQL
        // client binding.
        impl_->sql_client_ = nullptr;
        throw;
      }
      if (OB_SUCCESS == ret) {
        impl_->initialized_ = true;
      } else {
        impl_->sql_client_ = nullptr;
      }
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

bool ObPluginCatalog::is_initialized() const
{
  if (!impl_) return false;
  return impl_->initialized_.load(std::memory_order_acquire);
}

int ObPluginCatalog::install_package(const ObPluginPackageInstallSpec &spec,
                                     std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!valid_install_spec(spec)) {
      ret = OB_INVALID_ARGUMENT;
      error = "plugin install package identity is invalid";
    } else {
      ret = impl_->install(spec, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin catalog install allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin catalog install failure";
  }
  return ret;
}

int ObPluginCatalog::get_record(const std::string &plugin_id,
                                ObPluginCatalogRecord &record) const
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  try {
    return impl_->get(plugin_id, record);
  } catch (const std::bad_alloc &) {
    return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    return OB_ERR_UNEXPECTED;
  }
}

int ObPluginCatalog::list_records(
    std::vector<ObPluginCatalogRecord> &records) const
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  try {
    return impl_->list(records);
  } catch (const std::bad_alloc &) {
    records.clear();
    return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    records.clear();
    return OB_ERR_UNEXPECTED;
  }
}

int ObPluginCatalog::uninstall_restrict(
    const std::string &plugin_id,
    const std::string &operator_id,
    const std::string &audit_id,
    std::vector<ObPluginRestrictBlocker> &blockers,
    std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  blockers.clear();
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!catalog_valid_identifier(plugin_id) ||
               !catalog_valid_identifier(operator_id, true) ||
               !catalog_valid_identifier(audit_id, true)) {
      ret = OB_INVALID_ARGUMENT;
      error = "plugin uninstall identity is invalid";
    } else {
      ret = impl_->uninstall(
          plugin_id, operator_id, audit_id, blockers, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin uninstall catalog allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin uninstall catalog failure";
  }
  return ret;
}

int ObPluginCatalog::add_dependency(
    const ObPluginDependencySpec &dependency,
    std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    ObPluginSqlConnectionGuard guard(impl_->sql_client_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin dependency writer";
    } else if (OB_FAIL(impl_->mutate_dependency(
                   *guard.get_connection(), dependency, true, error))) {
    } else if (OB_FAIL(guard->commit())) {
      ret = OB_TRANS_UNKNOWN;
      error = "plugin dependency commit outcome is unknown";
    }
    if (OB_SUCCESS != ret && guard)
      rollback_noexcept(*guard.get_connection());
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin dependency allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin dependency mutation failure";
  }
  return ret;
}

int ObPluginCatalog::remove_dependency(
    const ObPluginDependencySpec &dependency,
    std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    ObPluginSqlConnectionGuard guard(impl_->sql_client_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else if (!guard) {
      ret = OB_NOT_INIT;
      error = "plugin catalog database is unavailable";
    } else if (OB_FAIL(begin_write(*guard.get_connection()))) {
      error = "cannot reserve plugin dependency writer";
    } else if (OB_FAIL(impl_->mutate_dependency(
                   *guard.get_connection(), dependency, false, error))) {
    } else if (OB_FAIL(guard->commit())) {
      ret = OB_TRANS_UNKNOWN;
      error = "plugin dependency removal commit outcome is unknown";
    }
    if (OB_SUCCESS != ret && guard)
      rollback_noexcept(*guard.get_connection());
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin dependency removal allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin dependency removal failure";
  }
  return ret;
}

int ObPluginCatalog::add_dependency(
    ObPluginSqlConnection &connection,
    const ObPluginDependencySpec &dependency,
    std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  try {
    // The caller already owns the SQL writer transaction.  Never acquire the
    // catalog mutex here: the ordinary catalog path takes mutex -> writer, so
    // doing writer -> mutex would deadlock with restricted disable.  SQL
    // writer exclusion is the serialization authority for this overload.
    if (!impl_->initialized_.load(std::memory_order_acquire)) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else {
      ret = impl_->mutate_dependency(connection, dependency, true, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin dependency allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin dependency mutation failure";
  }
  return ret;
}

int ObPluginCatalog::remove_dependency(
    ObPluginSqlConnection &connection,
    const ObPluginDependencySpec &dependency,
    std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  try {
    // Same lock-order rule as the transaction-scoped add overload above.
    if (!impl_->initialized_.load(std::memory_order_acquire)) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else {
      ret = impl_->mutate_dependency(connection, dependency, false, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin dependency removal allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin dependency removal failure";
  }
  return ret;
}

int ObPluginCatalog::list_restrict_blockers(
    const std::string &plugin_id,
    std::vector<ObPluginRestrictBlocker> &blockers) const
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  blockers.clear();
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    ObPluginSqlConnectionGuard guard(impl_->sql_client_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
    } else if (!catalog_valid_identifier(plugin_id)) {
      ret = OB_INVALID_ARGUMENT;
    } else if (!guard) {
      ret = OB_NOT_INIT;
    } else {
      ret = impl_->list_blockers(*guard.get_connection(), plugin_id, blockers);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
  }
  return ret;
}

int ObPluginCatalog::begin_activation(
    const ObPluginActivationRequest &request,
    std::unique_ptr<ObPluginActivationPermit> &permit,
    std::string &error) const noexcept
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  return impl_->begin_activation(request, permit, error);
}

int ObPluginCatalog::begin_restricted_disable(
    const std::string &plugin_id,
    const uint64_t expected_generation,
    std::unique_ptr<ObPluginDisablePermit> &permit,
    std::string &error) const noexcept
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  return impl_->begin_disable(plugin_id, expected_generation, permit, error);
}

int ObPluginCatalog::prepare_startup_recovery(
    std::vector<ObPluginStartupEntry> &entries,
    std::string &error)
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  int ret = OB_SUCCESS;
  error.clear();
  try {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    if (!impl_->initialized_) {
      ret = OB_NOT_INIT;
      error = "plugin catalog is not initialized";
    } else {
      ret = impl_->prepare_startup(entries, error);
    }
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin startup plan allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin startup planning failure";
  }
  return ret;
}

int ObPluginCatalog::recover_before_server_ready(
    ObPluginLoader &loader,
    ObPluginStartupReport &report,
    std::string &error)
{
  int ret = OB_SUCCESS;
  std::vector<ObPluginStartupEntry> entries;
  try {
    report = ObPluginStartupReport();
    error.clear();
    if (OB_FAIL(prepare_startup_recovery(entries, error))) {
    } else {
      report.planned_ = entries.size();
      for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].exact_recovery_) ++report.exact_replays_;
      }
      std::vector<ObPluginStartupEntry> pending(entries);
      while (OB_SUCCESS == ret && !pending.empty()) {
        bool made_progress = false;
        int deferred_ret = OB_SUCCESS;
        std::string deferred_error;
        std::string deferred_plugin_id;
        std::vector<ObPluginStartupEntry> deferred;
        deferred.reserve(pending.size());
        for (size_t i = 0; OB_SUCCESS == ret && i < pending.size(); ++i) {
          const ObPluginStartupEntry &entry = pending[i];
          uint64_t loaded_generation = 0;
          const int activation_ret = entry.exact_recovery_
              ? loader.recover_startup_activation(entry.recovery_,
                                                  &loaded_generation)
              : loader.load(entry.relative_path_, &loaded_generation);
          if (OB_SUCCESS == activation_ret) {
            made_progress = true;
            ++report.activated_;
            impl_->consume_startup_entry(entry.plugin_id_);
          } else if (!entry.exact_recovery_ &&
                     activation_ret == OB_ENTRY_NOT_EXIST &&
                     loader.last_failure_reason() ==
                         ObPluginLoadFailureReason::
                             REQUIRED_SERVICE_UNAVAILABLE) {
            // A pre-commit crash has no durable dependency edge to order by.
            // Defer only the loader's precise "required service unavailable"
            // class; another successful activation may publish its provider.
            if (deferred.empty()) {
              deferred_ret = activation_ret;
              deferred_error = loader.last_error();
              deferred_plugin_id = entry.plugin_id_;
            }
            deferred.push_back(entry);
          } else {
            ret = activation_ret;
            report.failed_plugin_id_ = entry.plugin_id_;
            error = loader.last_error();
            if (error.empty()) error = "plugin startup activation failed";
          }
        }
        if (OB_SUCCESS == ret && !deferred.empty()) {
          if (!made_progress) {
            ret = deferred_ret;
            report.failed_plugin_id_ = deferred_plugin_id;
            error = deferred_error;
            if (error.empty())
              error = "plugin startup dependency could not be resolved";
          } else {
            pending.swap(deferred);
          }
        } else {
          pending.clear();
        }
      }
    }
    if (OB_SUCCESS == ret) ret = check_server_ready(error);
  } catch (const std::bad_alloc &) {
    ret = OB_ALLOCATE_MEMORY_FAILED;
    error = "plugin startup recovery allocation failed";
  } catch (...) {
    ret = OB_ERR_UNEXPECTED;
    error = "unexpected plugin startup recovery failure";
  }
  return ret;
}

int ObPluginCatalog::check_server_ready(std::string &error) const
{
  if (!impl_) return OB_ALLOCATE_MEMORY_FAILED;
  error.clear();
  try {
    return impl_->ready(error);
  } catch (const std::bad_alloc &) {
    error = "plugin server-ready check allocation failed";
    return OB_ALLOCATE_MEMORY_FAILED;
  } catch (...) {
    error = "unexpected plugin server-ready check failure";
    return OB_ERR_UNEXPECTED;
  }
}

} // namespace plugin
} // namespace share
} // namespace oceanbase
